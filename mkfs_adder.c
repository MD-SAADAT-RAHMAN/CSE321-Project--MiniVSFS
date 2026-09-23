#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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
    // Terminal Input: ./mkfs_adder --input out.img --output out2.img --file <file>
    // Index argv[]:         0          1       2        3        4       5     6

    const char *input = NULL;
    const char *output = NULL;
    const char *file = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
        {
            i += 1;
            input = argv[i];
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
        {
            i += 1;
            output = argv[i];
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            i += 1;
            file = argv[i];
        }
        else
        {
            fprintf(stderr, "Usage: %s --input out.img --output out2.img --file <file>\n", argv[0]);
            return 1;
        }
    }

    // Error handling for incorrect input arguments
    if (input == NULL || output == NULL || file == NULL)
    {
        fprintf(stderr, "Usage: %s --input out.img --output out2.img --file <file>\n", argv[0]);
        return 1;
    }

    // Load input out.img file into memory
    FILE *fin = fopen(input, "rb");
    // Error handling if file does not load into fin
    if (fin == NULL)
    {
        fprintf(stderr, "Error: Cannot open input file %s: %s\n", input, strerror(errno));
        return 1;
    }

    // Load file Metadata using stat library
    struct stat statistics;
    if (stat(input, &statistics) != 0)
    {
        fprintf(stderr, "Error: Cannot access file %s: %s\n", input, strerror(errno));
        fclose(fin);
        return 1;
    }

    // Get file size from metadata and allocate memory
    off_t image_size_bytes = statistics.st_size;
    if (image_size_bytes <= BS)
    {
        fprintf(stderr, "Error: File size is less than minimum file size for %s\n", input);
        fclose(fin);
        return 1;
    }

    uint8_t *image = malloc(image_size_bytes);
    if (image == NULL)
    {
        fprintf(stderr, "Error: Cannot allocate memory for image: %s\n", strerror(errno));
        fclose(fin);
        return 1;
    }

    // Transfer file contents to allocated memory for image
    size_t read_bytes = fread(image, 1, image_size_bytes, fin);

    // Error handling if entire file has successfully been transferred into allocated memory
    if (read_bytes != (size_t)image_size_bytes)
    {
        fprintf(stderr, "Error: Cannot read input file %s: %s\n", input, strerror(errno));
        free(image);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    // Convert image into superblock struct
    superblock_t *sb = (superblock_t *)image;

    // Check if superblock follows specified default values
    if (sb->checksum != superblock_crc_finalize(sb))
    {
        fprintf(stderr, "Error: Invalid superblock checksum in %s\n", input);
        free(image);
        return 1;
    }

    // Locate inode and data bitmaps
    uint8_t *inode_bitmap = image + sb->inode_bitmap_start * BS;
    uint8_t *data_bitmap = image + sb->data_bitmap_start * BS;

    // Locate Free Inode, defaulted at last memory location
    uint64_t free_inode_index = UINT64_MAX;
    for (uint64_t i = 0; i < sb->inode_count; i++)
    {
        uint64_t byte = i / 8;
        uint8_t bit = i % 8;
        if (!(inode_bitmap[byte] & (1 << bit)))
        {
            free_inode_index = i;
            break;
        }
    }

    // Error handling if no free inode is found and free inode is still last memory location
    if (free_inode_index == UINT64_MAX)
    {
        fprintf(stderr, "Error: No free inode available in the file system\n");
        free(image);
        return 1;
    }

    // New inode number is index + 1 since inodes are 1-indexed
    uint32_t new_inode_no = (uint32_t)(free_inode_index + 1);

    // Verify of the input file is actually a file and valid
    struct stat file_st;
    if (stat(file, &file_st) != 0)
    {
        fprintf(stderr, "Error: Cannot access file %s: %s\n", file, strerror(errno));
        free(image);
        return 1;
    }

    // st_mode = 1 means it is a file
    // st_mode = 2 means it is a directory
    if (!S_ISREG(file_st.st_mode))
    {
        fprintf(stderr, "Error: %s is not a regular file\n", file);
        free(image);
        return 1;
    }

    // Check if file size is less that 48kib (12 blocks * 4096 bytes)
    off_t file_size = file_st.st_size;
    if (file_size > (12 * 4096))
    {
        fprintf(stderr, "Error: File %s is too large (max 48KiB)\n", file);
        free(image);
        return 1;
    }

    // Compute number of blocks from file size (ceiling)
    uint32_t blocks_needed = (file_size + BS - 1) / BS;

    // Locate inode table and root inode
    inode_t *inode_table = (inode_t *)(image + sb->inode_table_start * BS);
    inode_t *root_inode = &inode_table[sb->root_inode - 1];

    // Create new Directory Entry dirent64_t
    dirent64_t new_entry;

    // Clear new_entry memory by assigning 0 values
    memset(&new_entry, 0, sizeof(new_entry));

    // Input variable values for new_entry
    new_entry.inode_no = new_inode_no;
    new_entry.type = 1;

    // Check if file name is too long (>58 characters), file path has no '/'
    const char *basename = file;
    if (strlen(basename) > 58)
    {
        fprintf(stderr, "Error: File name is too long (>58 characters)\n");
        free(image);
        return 1;
    }

    // File name can't exist already in root directory
    if (strcmp(basename, ".") == 0 || strcmp(basename, "..") == 0)
    {
        fprintf(stderr, "Error: '.' or '..' cannot be added as a file name.\n");
        free(image);
        return 1;
    }

    int name_exists = 0;
    for (int dir_idx = 0; dir_idx < DIRECT_MAX; ++dir_idx)
    {
        uint32_t block_no = root_inode->direct[dir_idx];
        if (block_no == 0)
            break; // no more directory blocks

        dirent64_t *entries = (dirent64_t *)(image + (uint64_t)block_no * BS);
        int entries_per_block = BS / (int)sizeof(dirent64_t);

        for (int entry_idx = 0; entry_idx < entries_per_block; ++entry_idx)
        {
            if (entries[entry_idx].inode_no == 0)
                continue;

            char entry_name[59];
            memcpy(entry_name, entries[entry_idx].name, 58);
            entry_name[58] = '\0';

            if (strcmp(entry_name, basename) == 0)
            {
                name_exists = 1;
                break;
            }
        }
        if (name_exists)
            break;
    }
    if (name_exists)
    {
        fprintf(stderr, "Error: A file named '%s' already exists in the root directory.\n", basename);
        free(image);
        return 1;
    }

    // Copy the base name into the directory entry name field
    strncpy(new_entry.name, basename, sizeof(new_entry.name));

    // Locate free data block in root directory for new entry
    uint32_t dir_block_index = UINT32_MAX;
    uint32_t dir_entry_index = UINT32_MAX;

    uint64_t dir_entries_per_block = BS / sizeof(dirent64_t);
    uint64_t already_occupied_bytes = root_inode->size_bytes;
    uint64_t already_occupied_entries = (already_occupied_bytes + sizeof(dirent64_t) - 1) / sizeof(dirent64_t);

    // Loop through all direct pointers of root inode to find free slot
    for (int dir_idx = 0; dir_idx < DIRECT_MAX; dir_idx++)
    {
        uint32_t block_no = root_inode->direct[dir_idx];
        if (block_no == 0)
        {
            break;
        }
        dirent64_t *entries = (dirent64_t *)(image + (uint64_t)block_no * BS);
        for (int entry_idx = 0; entry_idx < (int)dir_entries_per_block; entry_idx++)
        {
            uint64_t entry_number = (uint64_t)dir_idx * dir_entries_per_block + entry_idx;
            if (entry_number >= already_occupied_entries || entries[entry_idx].inode_no == 0)
            {
                dir_block_index = dir_idx;
                dir_entry_index = entry_idx;
                break;
            }
        }
        if (dir_block_index != UINT32_MAX || dir_entry_index != UINT32_MAX)
        {
            break;
        }
    }

    // Error handling if no free slot is found in existing blocks, then send error
    if (dir_block_index == UINT32_MAX || dir_entry_index == UINT32_MAX)
    {
        fprintf(stderr, "Error: No free slot available in root directory\n");
        free(image);
        return 1;
    }

    // Insert new directory entry into determined slot
    uint32_t target_block_no = root_inode->direct[dir_block_index];
    dirent64_t *dir_entries = (dirent64_t *)(image + (uint64_t)target_block_no * BS);

    // Check if new_entry is valid
    dirent_checksum_finalize(&new_entry);

    dir_entries[dir_entry_index] = new_entry;

    // Update root directory inode's size
    uint64_t new_entry_offset = (uint64_t)dir_block_index * BS + dir_entry_index * sizeof(dirent64_t);
    if (new_entry_offset + sizeof(dirent64_t) > root_inode->size_bytes)
    {
        root_inode->size_bytes = new_entry_offset + sizeof(dirent64_t);
    }

    // Update time
    time_t now = time(NULL);
    root_inode->mtime = (uint64_t)now;
    root_inode->ctime = (uint64_t)now;

    // THEN ADD THE SPECIFIED FILE TO YOUR FILE SYSTEM
    // UPDATE THE .IMG FILE ON DISK
 
    inode_t *new_inode;      // take the address of the free inode slot
    new_inode = &inode_table[free_inode_index];  // pointer to the free inode
    
    size_t inode_size = sizeof(inode_t);     // figure out how many bytes an inode takes
    memset(new_inode, 0, inode_size);    // call memset to set all bytes of the inode to zero

    // Fill in inode basic information
    uint16_t tmp_mode;// file type
    tmp_mode = 0x8000;                 // regular file type (octal 0100000)
    new_inode->mode = tmp_mode;

    uint16_t tmp_links;// number of links
    tmp_links = 1;                      // new file has 1 link (from root directory)
    new_inode->links += tmp_links;

    // Increment link of root directory as it contains new file
    root_inode->links += 1;

    uint32_t tmp_uid;// user ID
    tmp_uid = 0;                        // owner user ID
    new_inode->uid = tmp_uid;
    uint32_t tmp_gid;// group ID
    tmp_gid = 2;                        // owner group ID
    new_inode->gid = tmp_gid;

    uint64_t tmp_size;    // file size
    tmp_size = (uint64_t)file_size;     // cast file size to 64-bit
    new_inode->size_bytes = tmp_size;
    //####SSet timestamps (atime, mtime, ctime)
    time_t current_time = time(NULL);  // store current epoch time
    now = current_time;                // assign to 'now' variable

    time_t access_time = now;// Assign access time
    new_inode->atime = access_time;

    time_t modification_time = now;// Assign modification time
    new_inode->mtime = modification_time;

    time_t creation_time = now; // Assign creation/change time
    new_inode->ctime = creation_time;

    // Allocate data blocks for the file content if there is at least one block needed
    if (blocks_needed > 0)
    {
        // Attempt to open the file in read-binary mode
    FILE *fadd;                 // declare file pointer
    fadd = fopen(file, "rb"); // open file
    FILE *file_check = fadd;  // Store into a temporary variable (pointer check)
   
    if (file_check == NULL)      // Check if fopen failed (NULL pointer means failure)
    {
        const char *errmsg = strerror(errno);
        fprintf(stderr, "Error: Cannot open the file %s: %s\n", file, errmsg);
        //Free allocated memory for the image
        free(image);
        return 1;
    }
      // Allocate memory to hold the entire file content          // file_size is in bytes

    uint8_t *file_buffer;        // pointer to hold the allocated memory
    file_buffer = malloc(file_size);  // request file_size bytes from the system

    if (file_buffer == NULL)  //Check if malloc returned NULL (allocation failed)
    {
       
        const char *filename = file;
        intmax_t requested_size = (intmax_t)file_size;  //Prepare error message
        fprintf(stderr, "Error: Here, not enough memory available to read the file %s (%jd bytes)\n", filename, requested_size);

        fclose(fadd);
        free(image);
        return 1;
    }
    //If execution reaches here, memory allocation succeeded
    // file_buffer now points to valid memory for file_size bytes
    // Read the file content into memory
    size_t rf;                        // variable to store number of bytes read
    rf = fread(file_buffer,   // pointer to memory where data will be stored  // fread returnsnumber of items successfully read
            1,                     // size of each item to read (1 byte)
            file_size,             // number of items to read
            fadd);                 // file pointer to read from
    fclose(fadd);
    //Check if the number of bytes read matches the file size, else reading failed or file is incomplete
   
    if (rf != (size_t)file_size)
    {
        // Step 3a: Prepare error message
        const char *filename = file;

        fprintf(stderr, "Error: Failed to read the entire file %s.\n", filename);
        free(file_buffer);
        free(image);
        return 1;
    }

// Step 4: If we reach here, the file was successfully read into memory
// file_buffer contains the full file content
////////////////////////////////////////////////////
    // Step 1: Store total number of available data blocks
    uint64_t data_blocks_total = sb->data_region_blocks;

    // Step 2: Loop through each block we need for this file
    for (uint32_t b = 0; b < blocks_needed; ++b)
    {
        // Step 2a: Initialize variable to track a free data block
        uint64_t free_data_index = UINT64_MAX; // UINT64_MAX means "no block found yet"

       
        for (uint64_t j = 0; j < data_blocks_total; ++j)  //Search for a free data block in the bitmap
        {
            uint64_t byte = j / 8;       //Determine which byte of bitmap holds this block's bit
            uint8_t bit = j % 8;        // Determine which bit in that byte represents this block

            if (!(data_bitmap[byte] & (1 << bit)))  //Check if this block is free // bit is 0 => free
            {   //Mark this block as allocated in bitmap
                free_data_index = j;
                data_bitmap[byte] |= (1 << bit);   // set bit to 1 => block allocated
                break;                             // stop searching; we found a free block
            }
        }
        if (free_data_index == UINT64_MAX)   //Check if no free block was found
        {fprintf(stderr, "Error: Not enough free data blocks (needed %u, allocated %u).\n", blocks_needed, b);
        free(file_buffer);
        free(image); 
            return 1;
        }

        uint32_t new_block_no = (uint32_t)(sb->data_region_start + free_data_index); //Compute absolute block number in the file system
        new_inode->direct[b] = new_block_no;//assign this block number to the current direct pointer of the inode
       
        // Compute where in the image buffer we will copy the file data
        uint64_t copy_offset = (uint64_t)new_block_no * BS;  // byte offset in image

        uint64_t copy_size;//Determine how many bytes to copy into this block
        if (b < blocks_needed - 1)  // for all blocks except last
        {
            copy_size = BS;          // full block
        }
        else                        // for the last block
        {
            copy_size = (uint64_t)file_size - (uint64_t)b * BS;  // remaining bytes
        }

        // Step 2h: Copy the file data into the image buffer at the calculated offset
        memcpy(image + copy_offset,               // destination in image
            file_buffer + (uint64_t)b * BS,  // source from file buffer
            copy_size);                       // number of bytes to copy

        // Step 2i: If this block is not completely full, zero out the remaining space
        if (copy_size < BS)
        {
            memset(image + copy_offset + copy_size,  // start of empty space
                0,                               // fill with zeros
                BS - copy_size);                 // length of empty space
        }
    }

    // Step 3: Free the memory used to store the file contents
    free(file_buffer);
}
/////////////////////////////////////////////////////////////////////////
    //Mark the new inode as allocated in the inode bitmap
    uint64_t byte_index = free_inode_index / 8;    // which byte holds the bit for this inode
    uint8_t bit_index = free_inode_index % 8;     // which bit in that byte represents this inode
    inode_bitmap[byte_index] |= (1 << bit_index); // set the bit to 1 => inode allocated

    
    inode_crc_finalize(new_inode);//compute and store CRC checksum for the new file inode

   
    inode_crc_finalize(root_inode); //Update root inode's CRC checksum as well

    // Step 4: Open the output file for writing the image
    FILE *fout = fopen(output, "wb");

    // Step 5: Check if the output file was opened successfully
    if (fout == NULL)
    {
        fprintf(stderr, "Error: Here, cannot open the output file %s: %s\n", output, strerror(errno));
        free(image); //Free the image memory
        return 1;
    } 
    size_t written = fwrite(image, 1, image_size_bytes, fout);//Write the entire image buffer to the output file

     if (written != (size_t)image_size_bytes) //Check if the write operation wrote all bytes
    {
        
        fprintf(stderr, "Error: Here, Failed to write the full output image (wrote %zu out of %jd bytes)\n", written, (intmax_t)image_size_bytes);
        fclose(fout); // Print an error message with bytes written then close file
        free(image);
        return 1;
    }
    fclose(fout);
    free(image);//Free the memory used for the image buff
    return 0;
}
