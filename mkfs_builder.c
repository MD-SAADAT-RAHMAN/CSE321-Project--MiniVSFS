#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
#pragma pack(push, 1)

typedef struct
{
    // CREATE YOUR SUPERBLOCK HERE
    // ADD ALL FIELDS AS PROVIDED BY THE SPECIFICATION
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t flags;
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum; // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push, 1)
typedef struct
{
    // CREATE YOUR INODE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[DIRECT_MAX];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc; // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size mismatch");

#pragma pack(push, 1)
typedef struct
{
    // CREATE YOUR DIRECTORY ENTRY STRUCTURE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum; // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t) == 64, "dirent size mismatch");

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        CRC32_TAB[i] = c;
    }
}
uint32_t crc32(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = CRC32_TAB[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb)
{
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t *ino)
{
    uint8_t tmp[INODE_SIZE];
    memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t *de)
{
    const uint8_t *p = (const uint8_t *)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++)
        x ^= p[i]; // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

int main(int argc, char *argv[])
{
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    // PARSE YOUR CLI PARAMETERS

    // inital variables jeigula dorkar
    const char *image_path = NULL;
    long size_kib = -1;
    long inodes_num = -1;
    int i;
    // now comand line ke parsing koretesi
    for (i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc)
        {
            image_path = argv[++i]; // output image path er jonno
        }
        else if (strcmp(argv[i], "--size-kib") == 0 && i + 1 < argc)
        {
            errno = 0;                              // error check korte
            size_kib = strtol(argv[++i], NULL, 10); // strol = string ke long e convert kortesi jate error handeling bhalo hoy
            if (errno != 0)
            {
                fprintf(stderr, "Error: Invalid number for --size-kib\n"); // fprinf use kortesi jate error ta teminal e thikmoto dekhay
                return 1;
            }
        }
        else if (strcmp(argv[i], "--inodes") == 0 && i + 1 < argc)
        {
            errno = 0;
            inodes_num = strtol(argv[++i], NULL, 10);
            if (errno != 0)
            {
                fprintf(stderr, "Error: Invalid number for --inodes\n");
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "Usage: %s --image <output.img> --size-kib <180-4096> --inodes <128-512>\n", argv[0]); // if terminal e bhul kichu dei jeita lagtesena
            return 1;
        }
    }
    // akn check  korbo, shob parameter thik ase naki
    if (!image_path || size_kib == -1 || inodes_num == -1)
    {
        fprintf(stderr, "Usage: %s --image <output.img> --size-kib <180-4096> --inodes <128-512>\n", argv[0]);
        return 1;
    }
    if (size_kib < 180 || size_kib > 4096 || (size_kib % 4) != 0)
    {
        fprintf(stderr, "Error: --size-kib must be between 180 and 4096 and a multiple of 4.\n");
        return 1;
    }
    if (inodes_num < 128 || inodes_num > 512)
    {
        fprintf(stderr, "Error: --inodes must be between 128 and 512.\n");
        return 1;
    }
    // akn amra image tar size ber korbo and or jonno memory allocate korbo
    uint64_t min_blocks_needed = 3;                                                       // superblock, inode bitmap, data bitmap er jonno minimum block faka rakha lagbe, atlease 3 ta
    uint64_t inodes_per_block = BS / INODE_SIZE;                                          // aikhane 4096/128 = 32 inodes per block ashtese
    uint64_t inode_table_blocks = (inodes_num + inodes_per_block - 1) / inodes_per_block; // ceiling division, ai table e all inodes store korbo and table tar jonno koyta block lagbe to store all inodes oita ber kortesi (inodes koyta lagbe oita terminal theke nitesi)
    min_blocks_needed += inode_table_blocks;
    min_blocks_needed += 1;                                    // root directory er jonno atlease 1 ta data block faka rakha lagbe
    uint64_t total_blocks = (uint64_t)size_kib * 1024ULL / BS; // size ta terminal theke dewa, aikhane total block of image ber kortesi
    if (total_blocks < min_blocks_needed)
    {
        fprintf(stderr, "Error: Image size is too small to contain the requested number of inodes.\n"); // check kortesi size ta requested inodes er jonno enough naki
        return 1;
    }

    size_t image_size_bytes = (size_t)total_blocks * BS; // total image size bytes e ber kortesi
    uint8_t *image = calloc(1, image_size_bytes);        // calloc use kortesi jate sob byte 0 diye initialize hoy and memory allocate hoy
    if (image == NULL)
    {
        fprintf(stderr, "Error: Unable to allocate %zu bytes for image.\n", image_size_bytes); // jodi memory allocate na hoy
        return 1;
    }

    // THEN ADD THE SPECIFIED FILE TO YOUR FILE SYSTEM
    // akn superblock er structure create and initialize kortesi block 0 e
    superblock_t superblock;
    superblock.magic = 0x4D565346;          //  magic number dewa
    superblock.version = 1;                 // diye dewa
    superblock.block_size = BS;             // block size
    superblock.total_blocks = total_blocks; //
    superblock.inode_count = inodes_num;

    superblock.inode_bitmap_start = 1;                  // inode bitmap block 1 e thakbe
    superblock.inode_bitmap_blocks = 1;                 // inode bitmap er jonno 1 ta block lagbe
    superblock.data_bitmap_start = 2;                   // data bitmap block 2 e thakbe
    superblock.data_bitmap_blocks = 1;                  // data bitmap er jonno 1 ta block
    superblock.inode_table_start = 3;                   // inode table block 3 theke start hobe
    superblock.inode_table_blocks = inode_table_blocks; // inode table er jonno koyta block lagbe oita agei calculate kore nisilam

    superblock.data_region_start = 3 + inode_table_blocks;                       // data region er first block index, inode table er por theke start hobe
    superblock.data_region_blocks = total_blocks - superblock.data_region_start; // data region er koyta block thakbe oita ber kortesi
    superblock.root_inode = ROOT_INO;                                            // diye dewa
    superblock.mtime_epoch = time(NULL);                                         // current time set kortesi
    superblock.flags = 0;                                                        // diye dewa
    // checksum ta finalize kortesi
    memcpy(image, &superblock, sizeof(superblock)); // superblock ta image er first block e copy kortesi
    superblock_crc_finalize((superblock_t *)image);

    // akn bitmap er kaaj korbo
    uint8_t *inode_bitmap = image + superblock.inode_bitmap_start * BS; // inode bitmap er starting address
    uint8_t *data_bitmap = image + superblock.data_bitmap_start * BS;   // data bitmap er starting address
    // akn root er jonno bitmap modify korbo
    inode_bitmap[0] |= 0x1;
    data_bitmap[0] |= 0x1;

    // inote table setup korbo
    inode_t *inode_table = (inode_t *)(image + superblock.inode_table_start * BS); // inode table er starting address
    inode_t inode = {0};                                                           // root inode initilazed to 0

    inode.mode = 0x4000;
    inode.links = 2; // for "." and ".."
    inode.uid = 0;
    inode.gid = 0;
    inode.size_bytes = 2 * sizeof(dirent64_t);
    time_t current_time = time(NULL);
    // shobgula time field initially current time e set kortesi
    inode.atime = current_time;
    inode.mtime = current_time;
    inode.ctime = current_time;

    // root directory r jonno first data block lagbe
    inode.direct[0] = (uint32_t)(superblock.data_region_start); // first data block er absolute block number
    inode.reserved_0 = 0;
    inode.reserved_1 = 0;
    inode.reserved_2 = 0;
    inode.proj_id = 2;
    inode.uid16_gid16 = 0;
    inode.xattr_ptr = 0;
    // inode er jonno crc finalize kortesi
    inode_crc_finalize(&inode);
    inode_table[ROOT_INO - 1] = inode; // root inode ta inode table er first entry te copy kortesi

    // akn root directory er data block setup korbo
    uint8_t *root_dir_block = image + (uint64_t)superblock.data_region_start * BS; // root directory er data block er starting address
    dirent64_t *root_dir_entries = (dirent64_t *)root_dir_block;                   // root directory er entries er starting address
    dirent64_t dirent = {0};                                                       // this is for . entryi
    dirent.inode_no = ROOT_INO;
    dirent.type = 2; // as aita akta directory
    strncpy(dirent.name, ".", sizeof(dirent.name));
    dirent_checksum_finalize(&dirent);
    root_dir_entries[0] = dirent; // first entry

    dirent = (dirent64_t){0}; // reset for .. entry
    dirent.inode_no = ROOT_INO;
    dirent.type = 2; // as aita akta directory
    strncpy(dirent.name, "..", sizeof(dirent.name));
    dirent_checksum_finalize(&dirent);
    root_dir_entries[1] = dirent; // second entry

    // UPDATE THE .IMG FILE ON DISK
    // akn amra imgage ta disk e write korbo
    FILE *fimg = fopen(image_path, "wb"); // image file open kortesi write korar jonno
    if (fimg == NULL)
    {
        fprintf(stderr, "Error: Cannot open output image %s: %s\n", image_path, strerror(errno)); // jodi file open na hoy
        free(image);                                                                              // memory free kore dibo
        return 1;
    }
    size_t written = fwrite(image, 1, image_size_bytes, fimg); // image ta file e write kortesi
    if (written != image_size_bytes)
    {
        fprintf(stderr, "Error: Failed to write full image to disk (written %zu out of %zu bytes)\n", written, image_size_bytes); // jodi full image write na hoy
        fclose(fimg);                                                                                                             // file close kore dibo
        free(image);                                                                                                              // memory free kore dibo
        return 1;
    }
    fclose(fimg); // file close kore dibo
    free(image);  // memory free kore dibo

    return 0;
}
