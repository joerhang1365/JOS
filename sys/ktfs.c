// ktfs.c - KTFS implementation
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef KTFS_TRACE
#define TRACE
#endif

#ifdef KTFS_DEBUG
#define DEBUG
#endif

#include <assert.h>
#include "heap.h"
#include "fs.h"
#include "ioimpl.h"
#include "ktfs.h"
#include "error.h"
#include "thread.h"
#include "string.h"
#include "console.h"
#include "cache.h"
#include "io.h"
#include "dev/virtio.h"

// INTERNAL CONSTANT DEFINITIONS
//

#define KTFS_FILE_IN_USE    (1 << 0)
#define KTFS_FILE_FREE      (0 << 0)

// INTERNAL TYPE DEFINITIONS
//

uint32_t ktfs_get_new_block();
int ktfs_release_block(uint32_t block_id);
int ktfs_get_new_inode(uint16_t * inode_num);
int ktfs_release_inode(uint16_t inode_id);
long ktfs_writeat(struct io* io, unsigned long long pos, const void * buf, long len);

struct file_system
{
    struct ktfs_superblock superblock;
    uint8_t * inode_bitmap;
};

struct ktfs_file
{
    struct io io;
    struct ktfs_dir_entry entry;
    size_t file_size;
    int in_use;

    struct ktfs_file * next;
};

// INTERNAL GLOBAL VARIABLES
//

struct io * backend;
struct file_system * fs; // global file system

struct ktfs_file * open_files;

static struct cache * cache;

// INTERNAL FUNCTION DECLARATIONS
//

static int ktfs_mount(struct io * io);

static int ktfs_open(const char * name, struct io ** ioptr);
static void ktfs_close(struct io* io);
static long ktfs_readat (
        struct io* io,
        unsigned long long pos,
        void * buf,
        long len);
long ktfs_writeat (
        struct io* io,
        unsigned long long pos,
        const void * buf,
        long len);
static int ktfs_cntl(struct io *io, int cmd, void *arg);
static int ktfs_flush(void);

unsigned int ktfs_get_new_block();
int ktfs_release_block(uint32_t block_id);
int ktfs_get_new_inode(uint16_t * inode_num);
int ktfs_release_inode(uint16_t inode_id);

static int read_data_blockat(
        struct ktfs_inode * inode,
        uint32_t dblock_id,
        uint32_t dblock_offset,
        void * buf,
        long len);
static int write_data_blockat(
        struct ktfs_inode * inode,
        uint32_t dblock_id,
        uint32_t dblock_offset,
        const void * buf,
        long len);

static int set_inode_bitmap(int inode_num);
static int init_inode_bitmap();


// FUNCTION ALIASES
//

int fsmount(struct io * io)
    __attribute__ ((alias("ktfs_mount")));

int fsopen(const char * name, struct io ** ioptr)
    __attribute__ ((alias("ktfs_open")));

int fsflush(void)
    __attribute__ ((alias("ktfs_flush")));

int fscreate(const char * name)
    __attribute__ ((alias("ktfs_create")));

int fsdelete(const char * name)
    __attribute__ ((alias("ktfs_delete")));

// INTERNAL FUNCTION DEFINITIONS
//

int set_inode_bitmap(int inode_num)
{
    uint8_t buf;
    int byte_pos = inode_num / 8;
    int bit_pos = inode_num % 8;

    memcpy(&buf, fs->inode_bitmap + byte_pos, 1);

    buf = buf | (1 << bit_pos);

    memcpy(fs->inode_bitmap + byte_pos, &buf, 1);

    return 0;
}

int init_inode_bitmap()
{
    struct ktfs_inode root_inode;
    struct ktfs_dir_entry dentry;
    uint32_t root_dir_inode_blk_cnt;
    uint32_t pos;

    uint32_t num_inodes_per_block;
    uint32_t num_inodes_in_use;

    num_inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;

    fs->inode_bitmap = kcalloc(1, (fs->superblock.inode_block_count * num_inodes_per_block / 8) + 1);
    set_inode_bitmap(fs->superblock.root_directory_inode);

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);
    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;
    num_inodes_in_use = root_inode.size / KTFS_DENSZ;

    if (root_inode.size % KTFS_BLKSZ != 0)
    {
        root_dir_inode_blk_cnt++;
    }

    // read out all the existing dentries and set the correponding
    // bit in inode_bitmap

    uint32_t inode_cnt = 0;

    for (int i = 0; i < root_dir_inode_blk_cnt; i++)
    {
        for (int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++)
        {
            if (inode_cnt < num_inodes_in_use)
            {
                read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &dentry, KTFS_DENSZ);
                set_inode_bitmap(dentry.inode);
                inode_cnt++;
            }
            else
            {
                return 0;
            }
        }
    }

    return 0;
}

int ktfs_mount(struct io * io)
{
    uint64_t read_bytes;
    char buf[512]; // FIXME: scary

    fs = kcalloc(1, sizeof(struct file_system));
    read_bytes = ioreadat(io, 0, &buf, 512);
    memcpy(&fs->superblock, &buf, 14);

    if (read_bytes < 0)
    {
        return read_bytes;
    }

    backend = ioaddref(io);
    create_cache(backend, &cache);
    init_inode_bitmap();
    open_files = NULL;

    return 0;
}

void insert_file_to_list(struct ktfs_file * fs_file)
{
    fs_file->next = open_files;
    open_files = fs_file;
}

void delete_file_from_list(const char * name)
{
    struct ktfs_file *curr = open_files;
    struct ktfs_file *prev = NULL;

    while (curr != NULL)
    {
        if (strcmp(curr->entry.name, name) == 0)
        {
            if(prev != NULL)
            {
                prev->next = curr->next;
            }
            else
            {
                open_files = curr->next;
            }

            kfree(curr);
            return;
        }
        else
        {
            prev = curr;
            curr = curr->next;
        }
    }
}

uint32_t ktfs_get_new_block()
{
    unsigned long long pos;
    uint8_t byte;

    for (uint32_t i = 0; i < fs->superblock.bitmap_block_count * KTFS_BLKSZ; i++)
    {
        // read 1 byte of bitmap a time
        pos = 1 * KTFS_BLKSZ + i;
        cache_readat(cache, pos, &byte, 1);

        for (uint32_t j = 0; j < 8; j++)
        {
            if (((byte >> j) & 0x1) == 0)
            {
                byte |=  (1 << j);
                cache_writeat(cache, pos, &byte, 1);

                // return id of block
                return (i * 8) + j;
            }
        }
    }

    return 0;
}


int ktfs_release_block(uint32_t block_id)
{
    uint32_t pos;
    uint32_t byte_pos;
    uint32_t bit_pos;
    uint8_t byte;

    byte_pos = block_id / 8;
    bit_pos = block_id % 8;
    pos = byte_pos + 1 * KTFS_BLKSZ;

    cache_readat(cache, pos, &byte, 1);

    byte = byte & ~(1 << bit_pos); // clear

    cache_writeat(cache, pos, &byte, 1);

    return 0;
}

int release_data_block(struct ktfs_inode * inode, uint32_t dblock_id)
{
    uint64_t pos;
    uint64_t start_pos_dblock;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1;
    uint32_t data_block_idx2;
    uint32_t dindirect_instance;

    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;

    start_pos_dblock = 1 + fs->superblock.bitmap_block_count;
    start_pos_dblock += fs->superblock.inode_block_count;

    // if the dblock_id is less than 3, then it is a direct block
    if (dblock_id < 3)
    {
        ktfs_release_block(inode->block[dblock_id]);
        return 0;
    }
    // 128 is the number of data blocks for indirect reference.
    else if ((dblock_id - 3) < 128)
    {
        // release inderct data block
        if (dblock_id == 3)
        {
            ktfs_release_block (inode->indirect);
        }

        // read the pointer of the block I need in the indirect data block

        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ;
        pos += (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        ktfs_release_block(data_block_idx1);

        return 0;
    }
    else
    {
        // 128 * 128 is the number of data blocks in each double indirect
        // instance. 131 is the number of data blocks from direct and indirect
        // data blocks

        if((dblock_id - 131) < (128 * 128))
        {
            // 0 as it is the first instance of the double indirect
            dindirect_instance = 0;
            // get the adjusted datablock id of the double indirect data blocks
            // this is used to calculate the pos
            adj_dblock_id = dblock_id - 131;
        }
        else
        {
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128;
        }

        dindirect_offset1 = adj_dblock_id / 128;
        dindirect_offset2 = adj_dblock_id % 128;

        if (adj_dblock_id == 0)
        {
            ktfs_release_block(inode->dindirect[dindirect_instance]);
        }

        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ;
        pos += dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);

        // if release the first (0th) entry of the second indirect block,
        // also release the second indirect data block

        if (dindirect_offset2 == 0)
        {
            ktfs_release_block(data_block_idx1);
        }

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ;
        pos += dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);
        ktfs_release_block(data_block_idx2);

        return 0;
    }
}

int ktfs_get_new_inode(uint16_t * inode_num)
{
    uint32_t num_inodes_per_block;
    uint32_t total_inode_count;
    uint8_t byte;

    num_inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    total_inode_count = fs->superblock.inode_block_count * num_inodes_per_block;

    for (uint32_t i = 0; i < total_inode_count / 8; i++)
    {
        memcpy(&byte, fs->inode_bitmap + i, 1);

        for (int j = 0; j < 8; j++)
        {
            if (((byte >> j) & 0x1) == 0)
            {
                // set the bit
                byte |= (1 << j);
                memcpy(fs->inode_bitmap + i, &byte, 1);
                *inode_num = i * 8 + j;

                return 0;
            }
        }
    }

    return -ENOINODEBLKS;
}

int ktfs_release_inode(uint16_t inode_id)
{
    uint32_t inode_pos = inode_id / 8;
    uint32_t inode_off = inode_id % 8;
    uint8_t byte;

    memcpy(&byte, fs->inode_bitmap+inode_pos, 1);

    byte = byte & ~(1 << inode_off);

    memcpy(fs->inode_bitmap+inode_pos, &byte, 1);
    return 0;
}

// Helper function for open and readat. It takes a provided data block id and
// a offset and finds the proper data block and reads the data block up to len.
// This function allows callers to treat the data_blocks as one contiguous block
// allower the caller to not worry about offsets and entering indirect data
// blocks to find blocks etc.

int read_data_blockat (
        struct ktfs_inode * inode,
        uint32_t dblock_id,
        uint32_t dblock_offset,
        void * buf,
        long len)
{
    uint64_t pos;
    uint64_t start_pos_dblock;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1;
    uint32_t data_block_idx2;
    uint32_t dindirect_instance;

    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;

    start_pos_dblock = 1 + fs->superblock.bitmap_block_count;
    start_pos_dblock += fs->superblock.inode_block_count;

    if (dblock_id < 3)
    {
        pos = (start_pos_dblock + inode->block[dblock_id]) * KTFS_BLKSZ;
        pos += dblock_offset;
        cache_readat(cache, pos, buf, len);
        return 0;
    }
    else if ((dblock_id - 3) < 128)
    {
        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ;
        pos += (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dblock_offset;
        cache_readat(cache, pos, buf, len);
        return 0;
    }
    else
    {
        if ((dblock_id - 131) < (128 * 128))
        {
            dindirect_instance = 0;
            adj_dblock_id = dblock_id - 131;
        }
        else
        {
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128;
        }

        dindirect_offset1 = adj_dblock_id / 128;
        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ;
        pos += dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);

        dindirect_offset2 = adj_dblock_id % 128;
        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ;
        pos += dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);

        pos = (start_pos_dblock + data_block_idx2) * KTFS_BLKSZ + dblock_offset;
        cache_readat(cache, pos, buf, len);

        return 0;
    }
}

int write_data_blockat (
        struct ktfs_inode * inode,
        uint32_t dblock_id,
        uint32_t dblock_offset,
        const void * buf,
        long len)
{
    uint64_t pos;
    uint64_t start_pos_dblock;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1;
    uint32_t data_block_idx2;
    uint32_t dindirect_instance;

    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;

    start_pos_dblock = 1 + fs->superblock.bitmap_block_count;
    start_pos_dblock += fs->superblock.inode_block_count;

    if (dblock_id < 3)
    {
        pos = (start_pos_dblock + inode->block[dblock_id]) * KTFS_BLKSZ;
        pos += dblock_offset;
        cache_writeat(cache, pos, buf, len);
        return 0;
    }
    else if ((dblock_id - 3) < 128)
    {
        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ;
        pos += (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dblock_offset;
        cache_writeat(cache, pos, buf, len);
        return 0;
    }
    else
    {
        if ((dblock_id - 131) < (128 * 128))
        {
            dindirect_instance = 0;
            adj_dblock_id = dblock_id - 131;
        }
        else
        {
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128;
        }

        dindirect_offset1 = adj_dblock_id / 128;
        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ;
        pos += dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);

        dindirect_offset2 = adj_dblock_id % 128;
        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ;
        pos += dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);

        pos = (start_pos_dblock + data_block_idx2) * KTFS_BLKSZ + dblock_offset;
        cache_writeat(cache, pos, buf, len);

        return 0;
    }
}

int allocate_new_data_block(struct ktfs_inode * inode, uint32_t dblock_id)
{
    uint32_t pos;
    uint32_t start_pos_dblock;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1;
    uint32_t dindirect_instance;

    uint32_t temp;
    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;
    uint32_t new_dblock_id = ktfs_get_new_block();

    start_pos_dblock = 1 + fs->superblock.bitmap_block_count;
    start_pos_dblock += fs->superblock.inode_block_count;
    new_dblock_id = ktfs_get_new_block();

    if (new_dblock_id == 0)
    {
        return -ENODATABLKS;
    }

    if (dblock_id < 3)
    {
        inode->block[dblock_id] = new_dblock_id;
        return 0;
    }
    else if ((dblock_id - 3) < 128)
    {
        if (dblock_id == 3)
        {
            temp = ktfs_get_new_block();
            if (temp == 0)
            {
                return -ENODATABLKS;
            }

            inode->indirect = temp;
        }

        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ;
        pos += (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_writeat(cache, pos, &new_dblock_id, KTFS_DATA_BLOCK_PTR_SIZE);

        return 0;
    }
    else
    {
        if ((dblock_id - 131) < (128 * 128))
        {
            dindirect_instance = 0;
            adj_dblock_id = dblock_id - 131;
        }
        else
        {
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128;
        }

        dindirect_offset1 = adj_dblock_id / 128;
        dindirect_offset2 = adj_dblock_id % 128;

        // get new block of indirect pointers

        if (adj_dblock_id == 0)
        {
            temp = ktfs_get_new_block();
            if(temp == 0)
            {
                return -ENODATABLKS;
            }

            inode->dindirect[dindirect_instance] = temp;
        }

        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ;
        pos += dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE;

        if (dindirect_offset2 == 0)
        {
            temp = ktfs_get_new_block();
            if (temp == 0)
            {
                return -ENODATABLKS;
            }

            data_block_idx1 = temp;
            cache_writeat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        }
        else
        {
            cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        }

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ;
        pos += dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE;
        cache_writeat(cache, pos, &new_dblock_id, KTFS_DATA_BLOCK_PTR_SIZE);

        return 0;
    }
}

int ktfs_open(const char * name, struct io ** ioptr)
{
    static const struct iointf ktfs_intf =
    {
        .readat = &ktfs_readat,
        .writeat = &ktfs_writeat,
        .cntl = &ktfs_cntl,
        .close = &ktfs_close

    };

    struct ktfs_file * my_file = kcalloc(1, sizeof(struct ktfs_file));
    struct ktfs_inode root_inode;
    uint64_t  pos;

    uint32_t num_inodes;
    uint32_t root_dir_inode_blk_cnt;
    uint32_t inode_count;

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;
    if(root_inode.size % KTFS_BLKSZ != 0)
    {
        root_dir_inode_blk_cnt++;
    }

    num_inodes = root_inode.size / KTFS_DENSZ;
    inode_count = 0;

    for (int i = 0; i < root_dir_inode_blk_cnt; i++)
    {
        for (int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++)
        {
            read_data_blockat(
                &root_inode, i, j * KTFS_DENSZ, &my_file->entry, KTFS_DENSZ);

            inode_count++;

            if (strncmp(my_file->entry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0)
            {
                struct ktfs_inode my_inode;

                pos = my_file->entry.inode * KTFS_INOSZ;
                pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
                cache_readat(cache, pos, &my_inode, KTFS_INOSZ);

                my_file->file_size = my_inode.size;

                insert_file_to_list(my_file);
				*ioptr = create_seekable_io(ioinit1(&my_file->io, &ktfs_intf));

                return 0;
            }

            // check if read past the last inode
            if (inode_count == num_inodes)
            {
                return -ENOENT; // it cooked
            }
        }
    }

    return -ENOENT;
}

void ktfs_close(struct io* io)
{
    struct ktfs_file * my_file;

    my_file = (void*)io - offsetof(struct ktfs_file, io);
    delete_file_from_list(my_file->entry.name);
    ktfs_flush();
    return;
}

long ktfs_readat(struct io* io, unsigned long long pos, void * buf, long len)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);
    struct ktfs_inode my_inode;
    uint64_t inode_pos;

    char blkbuf[KTFS_BLKSZ]; // FIXME: this seems bad
    uint32_t blkno;
    uint32_t blkoff;
    uint64_t remaining;
    uint64_t cpycnt;

    inode_pos = my_file->entry.inode * KTFS_INOSZ;
    inode_pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);
    debug("position=%d\n, len=%d", pos, len);

    if (pos >= my_file->file_size || len < 0)
    {
        return -EINVAL; //cooked
    }

    // truncate the write if len is too big
    if ((pos + len) > my_file->file_size)
    {
        len = my_file->file_size - pos;
    }

    blkno = pos / KTFS_BLKSZ;
    blkoff = pos % KTFS_BLKSZ;

    remaining = len;
    cpycnt = KTFS_BLKSZ - blkoff;
    if (cpycnt > len)
    {
        cpycnt = len;
    }

    read_data_blockat(&my_inode, blkno, 0, blkbuf, KTFS_BLKSZ);
    memcpy(buf, blkbuf + blkoff, cpycnt);
    remaining -= cpycnt;
	buf += cpycnt;

    blkno++;

    // if data is stored in more than one block

    while (remaining != 0)
    {
        read_data_blockat(&my_inode, blkno, 0, blkbuf, KTFS_BLKSZ);

        if (remaining > KTFS_BLKSZ)
        {
            cpycnt = KTFS_BLKSZ;
        }
        else
        {
            cpycnt = remaining;
        }

        memcpy(buf, blkbuf, cpycnt);
		buf += cpycnt;
        remaining -= cpycnt;

        blkno++;
    }

    return len;
}

long ktfs_writeat (
        struct io* io,
        unsigned long long pos,
        const void * buf,
        long len)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);
    struct ktfs_inode my_inode;
    uint64_t inode_pos;

    uint32_t blkno;
    uint32_t blkoff;
    uint64_t remaining;
    uint64_t cpycnt;

    inode_pos = my_file->entry.inode * KTFS_INOSZ;
    inode_pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);
    debug("position: %d\nlen: %d\n", pos, len);

    if (pos >= my_file->file_size || len < 0)
    {
        return -EINVAL; //cooked
    }

    // truncate the write if len is too big
    if ((pos + len) > my_file->file_size)
    {
        len = my_file->file_size - pos;
    }

    blkno = pos / KTFS_BLKSZ;
    blkoff = pos % KTFS_BLKSZ;

    remaining = len;
    cpycnt = KTFS_BLKSZ - blkoff;

    if (cpycnt > len)
    {
        cpycnt = len;
    }

    write_data_blockat(&my_inode, blkno, blkoff, buf, cpycnt);

    remaining -= cpycnt;
	buf += cpycnt;

    blkno++;

    // if data is stored in more than one block

    while (remaining != 0)
    {
        if (remaining > KTFS_BLKSZ)
        {
            cpycnt = KTFS_BLKSZ;
        }
        else
        {
            cpycnt = remaining;
        }

        write_data_blockat(&my_inode, blkno, 0, buf, cpycnt);

		buf += cpycnt;
        remaining -= cpycnt;

        blkno++;
    }

    return len;
}


int ktfs_create(const char *name)
{
    struct ktfs_inode root_inode;
    struct ktfs_inode new_inode;
    struct ktfs_dir_entry temp_dentry;
    uint64_t pos;
    uint32_t root_dir_inode_blk_cnt;
    uint32_t dentry_cnt;

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;

    if (root_inode.size % KTFS_BLKSZ != 0)
    {
        root_dir_inode_blk_cnt++;
    }

    if (strlen(name) > KTFS_MAX_FILENAME_LEN)
    {
        return -EINVAL;
    }

    // check if there is already an existing file
    for (int i = 0; i < root_dir_inode_blk_cnt; i++)
    {
        for (int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++)
        {
            if (dentry_cnt == (root_inode.size / KTFS_DENSZ))
            {
                goto create_continue;
            }

            read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &temp_dentry, KTFS_DENSZ);
            if (strncmp(temp_dentry.name, name,
                KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0)
            {
                return -EINVAL;
            }

            dentry_cnt++;
        }
    }

create_continue:
    uint32_t blkoff = root_inode.size % KTFS_BLKSZ;
    uint32_t blkno = root_inode.size / KTFS_BLKSZ;

    // block offset is 0 we need a new block
    if (blkoff == 0)
    {
        if (allocate_new_data_block(&root_inode, blkno) < 0)
        {
            return -ENODATABLKS;
        }

        cache_writeat(cache, pos, &root_inode, KTFS_INOSZ);
    }

    struct ktfs_dir_entry dentry;
    uint16_t new_inode_num;

    if (ktfs_get_new_inode(&new_inode_num) < 0)
    {
        return -ENOINODEBLKS;
    }

    dentry.inode = new_inode_num;
    memcpy(dentry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t));
    write_data_blockat(&root_inode, blkno, blkoff, &dentry, KTFS_DENSZ);
    root_inode.size += KTFS_DENSZ;

    // update root inode
    cache_writeat(cache, pos, &root_inode, KTFS_INOSZ);

    // set initilze file size to 0
    new_inode.size = 0;

    pos = dentry.inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_writeat(cache, pos, &new_inode, KTFS_INOSZ);

    // ensure changes persist on disk
    ktfs_flush();

    return 0;
}

//TODO Add error codes for Not datablocks avail and no inodeblks avail

int ktfs_ext_len(struct ktfs_file * my_file, void * arg)
{
    struct ktfs_inode my_inode;
    uint64_t inode_pos;

    uint64_t len;
    uint32_t start_dblock_id;
    uint32_t last_dblock_id;
    size_t old_size;

    len = *(uint64_t *) arg;

    // TODO: max file size
    if (len <= my_file->file_size || len == 0)
    {
        return 0;
    }

    old_size = my_file->file_size;
    my_file->file_size = len;

    inode_pos = my_file->entry.inode * KTFS_INOSZ;
    inode_pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);

    my_inode.size = len;
    cache_writeat(cache, inode_pos, &my_inode, KTFS_INOSZ);

    last_dblock_id = (len - 1) / KTFS_BLKSZ;

    if (old_size == 0)
    {
        start_dblock_id = 0;
    }
    else
    {
        // start at the next block needed
        start_dblock_id = (old_size - 1) / KTFS_BLKSZ + 1;
    }

    for (int i = start_dblock_id; i <= last_dblock_id; i++ )
    {
        if (allocate_new_data_block(&my_inode, i) < 0)
        {
            return -ENODATABLKS;
        }

        cache_writeat(cache, inode_pos, &my_inode, KTFS_INOSZ);
    }

    return 0;
}


int ktfs_delete(const char * name)
{
    struct ktfs_inode root_inode;
    struct ktfs_inode my_inode;
    struct ktfs_dir_entry temp_dentry;
    struct ktfs_dir_entry last_dentry;

    int dentry_cnt = 0;
    uint32_t root_dir_inode_blk_cnt;

    uint64_t pos;

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;

    if (root_inode.size % KTFS_BLKSZ != 0)
    {
        root_dir_inode_blk_cnt++;
    }

    // check if file name is valid
    if (strlen(name) > KTFS_MAX_FILENAME_LEN)
    {
        return -EINVAL;
    }

    // check if a file exists
    for (int i = 0; i < root_dir_inode_blk_cnt; i++)
    {
        for (int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++)
        {
            if (dentry_cnt == (root_inode.size / KTFS_DENSZ))
            {
                return -ENOENT;
            }

            read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &temp_dentry, KTFS_DENSZ);
            if (strncmp(temp_dentry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0)
            {
                goto found_name;
            }

            dentry_cnt++;
        }
    }

    // file not found
    return -ENOENT; //file not found

found_name:

    uint64_t inode_pos;
    uint32_t data_block_count;

    inode_pos = temp_dentry.inode * KTFS_INOSZ;
    inode_pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);

    data_block_count = my_inode.size / KTFS_BLKSZ;

    if (my_inode.size % KTFS_BLKSZ != 0)
    {
        data_block_count++;
    }

    for (int i = data_block_count - 1; i >= 0; i--)
    {
        release_data_block(&my_inode, i);
    }

    ktfs_release_inode(temp_dentry.inode);

    // get the block info of the last dentry

    uint32_t last_blkoff = (root_inode.size - KTFS_DENSZ) % KTFS_BLKSZ;
    uint32_t last_blkno = (root_inode.size - KTFS_DENSZ) / KTFS_BLKSZ;

    uint32_t curr_blkoff = (dentry_cnt * KTFS_DENSZ) % KTFS_BLKSZ;
    uint32_t curr_blkno = (dentry_cnt * KTFS_DENSZ) / KTFS_BLKSZ;

    read_data_blockat(&root_inode, last_blkno, last_blkoff, &last_dentry, KTFS_DENSZ);
    write_data_blockat(&root_inode, curr_blkno, curr_blkoff, &last_dentry, KTFS_DENSZ);

    // release the dentry block if it is the last entry left in the block
    if (last_blkoff == 0)
    {
        release_data_block(&root_inode, last_blkno);
    }

    // decrease the size of filesystem
    root_inode.size -= KTFS_DENSZ;

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ;
    pos += (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; // trust issues

    cache_writeat(cache, pos, &root_inode, KTFS_INOSZ);

    // TODO: need to create a table of file names and their ioptr for
    // the close function
    delete_file_from_list(name);
    ktfs_flush();

    return 0;
}

int ktfs_cntl(struct io *io, int cmd, void *arg)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);
	size_t * szarg = arg;
    int result;

    switch (cmd)
    {
    case IOCTL_GETBLKSZ:
        return 1;
        break;
    case IOCTL_SETEND:
        return ktfs_ext_len(my_file, arg);
        break;
    case IOCTL_GETEND:
		*szarg = my_file->file_size;
		result = 0;
        break;
    default:
        result = -EINVAL;
    }

    return result;
}

int ktfs_flush(void)
{
    cache_flush(cache);
    return 0;
}
