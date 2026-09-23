#!/usr/bin/env python3
import struct, sys

BS = 4096

SUPER_FMT = "<III" + "Q"*12 + "II"  # 116 bytes
INODE_SIZE = 128
DIRECT_MAX = 12

def read_at(f, off, n):
    f.seek(off)
    b = f.read(n)
    if len(b) != n:
        raise IOError("short read")
    return b

def main(path):
    with open(path, "rb") as f:
        # Read superblock (block 0)
        sb_raw = read_at(f, 0, BS)
        fields = struct.unpack(SUPER_FMT, sb_raw[:116])
        (magic, version, block_size,
         total_blocks, inode_count,
         inode_bmap_start, inode_bmap_blocks,
         data_bmap_start, data_bmap_blocks,
         inode_tbl_start, inode_tbl_blocks,
         data_start, data_blocks,
         root_ino, mtime_epoch, flags, checksum) = fields

        if magic != 0x4D565346 or block_size != BS:
            print("Not a MiniVSFS image (bad magic or block size).")
            return

        print(f"MiniVSFS OK | blocks={total_blocks} inodes={inode_count}")
        print(f"inode_bitmap@{inode_bmap_start}, data_bitmap@{data_bmap_start}")
        print(f"inode_table@{inode_tbl_start} (+{inode_tbl_blocks} blk)")
        print(f"data_region@{data_start} (+{data_blocks} blk)")
        print(f"root inode = {root_ino}")

        # Read root inode
        root_idx = root_ino - 1
        inode_off = (inode_tbl_start * BS) + (root_idx * INODE_SIZE)
        ino_raw = read_at(f, inode_off, INODE_SIZE)

        # Inode layout:
        # <HHII Q Q Q Q 12I 3I I I Q Q
        INODE_FMT = "<HHII" + "Q"*4 + "I"*12 + "I"*3 + "I" + "I" + "Q" + "Q"
        ino = struct.unpack(INODE_FMT, ino_raw)
        mode, links, uid, gid = ino[:4]
        size_bytes, atime, mtime, ctime = ino[4:8]
        direct = list(ino[8:20])
        # rest unused here

        print(f"root: mode=0x{mode:04x} links={links} size={size_bytes} bytes")
        if direct[0] == 0:
            print("Root has no data block? (unexpected)")
            return

        # Read root directory block (first direct pointer)
        dir_block_no = direct[0]
        dir_raw = read_at(f, dir_block_no * BS, BS)

        # dirent64_t: <I B 58s B  (64 bytes)
        DIRENT_FMT = "<IB58sB"
        ents_per_block = BS // 64
        print("Root directory entries:")
        for i in range(ents_per_block):
            rec = dir_raw[i*64:(i+1)*64]
            inode_no, typ, name_raw, csum = struct.unpack(DIRENT_FMT, rec)
            if inode_no == 0:
                continue
            name = name_raw.split(b'\x00', 1)[0].decode('utf-8', errors='replace')
            kind = "file" if typ == 1 else ("dir" if typ == 2 else f"type{typ}")
            print(f"  [{i:02}] ino={inode_no} type={kind} name='{name}'")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} image.img")
        sys.exit(1)
    main(sys.argv[1])
