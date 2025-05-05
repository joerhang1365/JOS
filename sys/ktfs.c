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


struct file_system {
    struct ktfs_superblock superblock;
    //uint8_t padding[BLOCK_SIZE - sizeof(struct ktfs_superblock)];
    // struct ktfs_bitmap * bitmaps;
    uint8_t * inode_bitmap;
    // struct ktfs_data_block* data_blocks;
};

struct ktfs_file {
    struct io io;
    size_t file_size;
    struct ktfs_dir_entry entry;
    int in_use;
    struct ktfs_file * next;
};

// INTERNAL GLOBAL VARIABLES
//

struct file_system * fs; // global file system
struct io * backend;

struct ktfs_file * open_files;



static struct cache * cache;

// INTERNAL FUNCTION DECLARATIONS
//

static int ktfs_mount(struct io * io);

static int ktfs_open(const char * name, struct io ** ioptr);
static void ktfs_close(struct io* io);
static long ktfs_readat(struct io* io, unsigned long long pos, void * buf, long len);
static int ktfs_cntl(struct io *io, int cmd, void *arg);

//static int ktfs_getblksz(struct ktfs_file *fd);
//static int ktfs_getend(struct ktfs_file *fd, void *arg);
static int ktfs_flush(void);

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

//static int cache_rw_at(struct cache * cache, int rw, unsigned long long pos, void * buf, long bufsz);


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

// EXPORTED FUNCTION DEFINITIONS
//

int set_inode_bitmap(int inode_num) {
    uint8_t buf;
    int byte_loc = inode_num / 8;
    int bit_loc = inode_num % 8;
    memcpy(&buf, fs->inode_bitmap+byte_loc, 1);
    buf = buf | (1 << bit_loc);
    memcpy(fs->inode_bitmap+byte_loc, &buf, 1);
    return 0;
  }


int init_inode_bitmap() {

    struct ktfs_inode root_inode;
    unsigned long long pos = fs->superblock.root_directory_inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    uint32_t num_inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    fs->inode_bitmap = kcalloc(1, (fs->superblock.inode_block_count * num_inodes_per_block / 8) + 1); //1 byte can represent 8 inodes, so we divide by 8 and add 1 byte incase the inode count isn't a multiple of 8
    uint32_t root_dir_inode_blk_cnt;

    uint32_t num_inodes_in_use = root_inode.size / KTFS_DENSZ;
    int inode_cnt = 0;
    struct ktfs_dir_entry dentry;

    // set the root_dirctory_inode bit in the bitmap
    set_inode_bitmap(fs->superblock.root_directory_inode);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ; //see how many exisiting inode blocks you need to loop through

    if(root_inode.size % KTFS_BLKSZ != 0)
        root_dir_inode_blk_cnt++;

    // read out all the existing dentries and set the correponding bit in inode_bitmap
    for(int i = 0; i < root_dir_inode_blk_cnt; i++){
        for(int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++){
            if (inode_cnt < num_inodes_in_use) {
                read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &dentry, KTFS_DENSZ);
                set_inode_bitmap(dentry.inode);
                inode_cnt++;
            }
            else
                return 0;
        }

    }

    return 0;

}


int ktfs_mount(struct io * io)
{
    long rcnt;

    //assert(ioctl(io, IOCTL_GETBLKSZ, NULL) == 512);

    fs = kcalloc(1, sizeof(struct file_system));    //create the file system device

    //rcnt = iofill(io, &fs->superblock, sizeof(struct ktfs_superblock)); //fill in the super block from the backend device
    debug("superblock size: %d\n", sizeof(struct ktfs_superblock));
    char buf[512];
    rcnt = ioreadat(io, 0, &buf, 512);

    memcpy(&fs->superblock, &buf, 14);

    //kprintf("Inode Block count: %d \n", fs->superblock.inode_block_count);
    if(rcnt < 0)
        return rcnt;

    /*
    if(rcnt != sizeof(struct ktfs_superblock)) //iofill should copy the size of superblock to the superblock struct. If it didn't, then the struct isn't properly filled
        return -EIO;
    */

    // fs->bitmaps = kcalloc(fs->superblock.bitmap_block_count, sizeof(struct ktfs_bitmap));   //create an array of the size of bitmap block count


    //???need to fill bitmap
    // fs->inodes = kcalloc(fs->superblock.inode_block_count, sizeof(struct ktfs_inode));  //create an array of size of inode block count

    // fs->data_blocks = kcalloc(fs->superblock.block_count, sizeof(struct ktfs_data_block));  //???create an array of size of block_count
    // if(fs->bitmaps==NULL)
    //     return -ENOMEM;

    // if(fs->inodes!=NULL)
    //     return -ENOMEM;
    // if(fs->data_blocks!=NULL)
    //     return -ENOMEM;

    //use ioreadat to fill bitmat, inode etc


    backend = ioaddref(io); //save the backend io for future use
    create_cache(backend, &cache);

    init_inode_bitmap(); //init the inode biut map
    open_files = NULL; //initalize the open files list


    return 0;
}

// INTERNAL FUNCTION DEFINITIONS


void insert_file_to_list(struct ktfs_file * fs_file){
    fs_file->next = open_files;
    open_files = fs_file;
}

void delete_file_from_list(const char * name){

    struct ktfs_file *curr = open_files;
    struct ktfs_file *prev = NULL;
    while (curr != NULL) {
        if(strcmp(curr->entry.name, name) == 0){
            if(prev != NULL)
                prev->next = curr->next;
            else{
                open_files = curr->next;
            }
            kfree(curr);
            //ktfs_close(&curr->io);
            return;
        }
        else{
            prev = curr;
            curr = curr->next;
        }

    }


}



uint32_t ktfs_get_new_block() {
    unsigned long long pos;
    uint8_t buf;

    for (int i = 0; i < fs->superblock.bitmap_block_count*KTFS_BLKSZ; i++) {
        //read 1-byte of bitmap a time
        pos = 1 * KTFS_BLKSZ + i;
        cache_readat(cache, pos, &buf, 1);
        for (int j = 0; j < 8; j++)
            if (((buf >> j) & 0x1) == 0) { //Right shift to check is LSB is 0
                // set the bit
                buf = buf | (1 << j);   //set the bit in the bit map
                cache_writeat(cache, pos, &buf, 1);
                return (i * 8) + j; //return the id of the block
            }

    }
    return 0;

}


int ktfs_release_block(uint32_t block_id) {
    unsigned long long byte_pos;
    int bit_pos;
    unsigned long long pos;
    uint8_t buf;

    byte_pos = block_id / 8;
    bit_pos = block_id % 8;
    pos = byte_pos + 1*KTFS_BLKSZ;
    cache_readat(cache, pos, &buf, 1);   //read 8 bits from the bit map block
    // clear the bit
    buf = buf & ~(1 << bit_pos);    //set the bit to 0 at bit_pos
    cache_writeat(cache, pos, &buf, 1);   //write the updated bit map back

    return 0;
}

int release_data_block(struct ktfs_inode * inode, uint32_t dblock_id){
    unsigned long long pos;
    unsigned long long start_pos_dblock = 1 + fs->superblock.bitmap_block_count + fs->superblock.inode_block_count;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1; //used to contain indirect datablock indexes in order to find the needed data block
    uint32_t data_block_idx2;
    uint32_t dindirect_instance; //there are 2 dindirects in inode


    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;

    if(dblock_id < 3){  //if the dblock_id is less than 3, then it is a direct block
        ktfs_release_block (inode->block[dblock_id]);
        return 0;
    }
    else if((dblock_id - 3) < 128){ //128 is the number of data blocks for indirect reference.
        if (dblock_id == 3) // release inderct data block
            ktfs_release_block (inode->indirect);

        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ + (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE; //read the pointer of the block I need in the indirect data block
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        ktfs_release_block (data_block_idx1);
        return 0;
    }
    else{
        if((dblock_id - 131) < (128 * 128)){    //128*128 is the number of data blocks in each double indirect instance. 131 is the number of data blocks from direct and indirect data blocks
            dindirect_instance = 0; //0 as it is the first instance of the double indirect
            adj_dblock_id = dblock_id - 131;    //get the adjusted datablock id of the double indirect data blocks. This is used to calculate the pos
        }
        else{
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128; //get the adjusted data block if the id is a part of the 2nd double indirect reference.
        }

        dindirect_offset1 = adj_dblock_id / 128; //offset in first indirect block
        dindirect_offset2 = adj_dblock_id % 128; //offset in second indrect block

        if (adj_dblock_id == 0) //if release the very first datablock, also need to release the first indirect data block
          ktfs_release_block (inode->dindirect[dindirect_instance]);

        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ + dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE; //position of the double indirect block.
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        if (dindirect_offset2 == 0) {//if release the first (0th) entry of the second indirect block, also release the second indirect data block
          ktfs_release_block (data_block_idx1);
        }

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE; //calculate the position of the direct pointer in the indirect pointer
        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);
        ktfs_release_block (data_block_idx2);
        return 0;
    }
}


// int cache_rw_at(int rw, unsigned long long pos, void * buf, long bufsz) {
//     uint8_t * block;
//     uint32_t block_pos = pos / KTFS_BLKSZ;
//     block_pos *= KTFS_BLKSZ;
//     uint32_t block_off = pos % KTFS_BLKSZ;

//     cache_get_block(cache, block_pos, (void **) &block);
//     memcpy(buf, block + block_off, bufsz);

//     return bufsz;
// }

// int cache_rw_at(int rw, unsigned long long pos, void * buf, long bufsz) {
//     uint8_t * block;

//     uint32_t block_pos = pos / KTFS_BLKSZ;
//     block_pos *= KTFS_BLKSZ;
//     uint32_t block_off = pos % KTFS_BLKSZ;
//     int cache_idx;

//     cache_get_block(cache, block_pos, (void **) &block); //FIXME in the cache_get_block we need to return
//     //cache_idx = cache->last_r_idx; //FIXME invalid use of undefined type 'struct cache'
//     if (rw) { // write to cache
//         memcpy(block + block_off, buf, bufsz);
//         //cache->table[cache_idx].dirty = 1; //FIXME invalid use of undefined type 'struct cache'
//     }
//     else  // read from cache
//      memcpy(buf, block + block_off, bufsz);

//     return bufsz;
// }

int ktfs_get_new_inode(uint16_t * inode_num) {
    uint32_t num_inodes_per_block = KTFS_BLKSZ/KTFS_INOSZ;
    uint32_t total_inode_count = fs->superblock.inode_block_count * num_inodes_per_block;
    uint8_t buf;
    for (int i = 0; i < total_inode_count / 8; i++) {
        memcpy(&buf, fs->inode_bitmap + i, 1);
        for (int j = 0; j < 8; j++)
            if (((buf >> j) & 0x1) == 0) {
                // set the bit
                buf = buf | (1 << j);
                memcpy(fs->inode_bitmap+i, &buf, 1);
                *inode_num = i*8 + j;
                return 0;
            }
    }
    return -ENOINODEBLKS;
}

int ktfs_release_inode(uint16_t inode_id) {
    //uint32_t num_inodes_per_block = KTFS_BLKSZ/KTFS_INOSZ;
    //uint32_t total_inode_count = fs->superblock.inode_block_count * num_inodes_per_block;
    uint8_t buf;

    int inode_pos = inode_id / 8;
    int inode_off = inode_id % 8;
    memcpy(&buf, fs->inode_bitmap+inode_pos, 1);
    buf = buf & ~(1 << inode_off);
    memcpy(fs->inode_bitmap+inode_pos, &buf, 1);
    return 0;
}

/*
int rw_data_block_at(rw, struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, void * buf, long len)

Input: struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, void * buf, long len

Output: 0 for success

Description: Helper function for open and readat. It takes a provided data block id and a offset and finds the proper data block and
reads the data block up to len. This function allows callers to treat the data_blocks as one contiguous block allower the caller
to not worry about offsets and entering indirect data blocks to find blocks etc.

Sideeffects: None

*/
int read_data_blockat(struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, void * buf, long len){
    unsigned long long pos;
    unsigned long long start_pos_dblock = 1 + fs->superblock.bitmap_block_count + fs->superblock.inode_block_count;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1; //used to contain indirect datablock indexes in order to find the needed data block
    uint32_t data_block_idx2;
    uint32_t dindirect_instance; //there are 2 dindirects in inode


    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;
    if(dblock_id < 3){  //if the dblock_id is less than 3, then it is a direct block
        pos = (start_pos_dblock + inode->block[dblock_id]) * KTFS_BLKSZ + dblock_offset;    //calculate the dblock position
        cache_readat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len);   //read the data and copy it to buf
        return 0;
    }
    else if((dblock_id - 3) < 128){ //128 is the number of data blocks for indirect reference.
        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ + (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE; //read the pointer of the block I need in the indirect data block
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE); //always 0 because it is used to read indirect
        //ioreadat(backend, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);  //read the indirect data block index

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dblock_offset; //use the indirect datablock index and the offset to calculate the actual position of the datablock for the indirect block
        cache_readat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len);   //read the data block
        return 0;
    }
    else{
        if((dblock_id - 131) < (128 * 128)){    //128*128 is the number of data blocks in each double indirect instance. 131 is the number of data blocks from direct and indirect data blocks
            dindirect_instance = 0; //0 as it is the first instance of the double indirect
            adj_dblock_id = dblock_id - 131;    //get the adjusted datablock id of the double indirect data blocks. This is used to calculate the pos
        }
        else{
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128; //get the adjusted data block if the id is a part of the 2nd double indirect reference.
        }


        dindirect_offset1 = adj_dblock_id / 128; //select offset of indirect pointer in the double indirect block

        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ + dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE; //position of the double indirect block.

        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        //ioreadat(backend, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);  //read the pointer of the indirect block inside of the double indirect block

        dindirect_offset2 = adj_dblock_id % 128; //the offset of direct pointer in the indirect block

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE; //calculate the position of the direct pointer in the indirect pointer

        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);
        //ioreadat(backend, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE); //read the direct data block pointer


        pos = (start_pos_dblock + data_block_idx2) * KTFS_BLKSZ + dblock_offset;    //calculate the position of the data block

        cache_readat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len); //read the data block to the buf
        return 0;
    }
}

int write_data_blockat(struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, const void * buf, long len){
    unsigned long long pos;
    unsigned long long start_pos_dblock = 1 + fs->superblock.bitmap_block_count + fs->superblock.inode_block_count;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1; //used to contain indirect datablock indexes in order to find the needed data block
    uint32_t data_block_idx2;
    uint32_t dindirect_instance; //there are 2 dindirects in inode


    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;
    if(dblock_id < 3){  //if the dblock_id is less than 3, then it is a direct block
        pos = (start_pos_dblock + inode->block[dblock_id]) * KTFS_BLKSZ + dblock_offset;    //calculate the dblock position
        cache_writeat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len);   //read the data and copy it to buf
        return 0;
    }
    else if((dblock_id - 3) < 128){ //128 is the number of data blocks for indirect reference.
        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ + (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE; //read the pointer of the block I need in the indirect data block
        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE); //always 0 because it is used to read indirect
        //ioreadat(backend, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);  //read the indirect data block index

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dblock_offset; //use the indirect datablock index and the offset to calculate the actual position of the datablock for the indirect block
        cache_writeat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len);   //read the data block
        return 0;
    }
    else{
        if((dblock_id - 131) < (128 * 128)){    //128*128 is the number of data blocks in each double indirect instance. 131 is the number of data blocks from direct and indirect data blocks
            dindirect_instance = 0; //0 as it is the first instance of the double indirect
            adj_dblock_id = dblock_id - 131;    //get the adjusted datablock id of the double indirect data blocks. This is used to calculate the pos
        }
        else{
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128; //get the adjusted data block if the id is a part of the 2nd double indirect reference.
        }


        dindirect_offset1 = adj_dblock_id / 128; //select offset of indirect pointer in the double indirect block

        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ + dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE; //position of the double indirect block.

        cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        //ioreadat(backend, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);  //read the pointer of the indirect block inside of the double indirect block

        dindirect_offset2 = adj_dblock_id % 128; //the offset of direct pointer in the indirect block

        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE; //calculate the position of the direct pointer in the indirect pointer

        cache_readat(cache, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE);
        //ioreadat(backend, pos, &data_block_idx2, KTFS_DATA_BLOCK_PTR_SIZE); //read the direct data block pointer


        pos = (start_pos_dblock + data_block_idx2) * KTFS_BLKSZ + dblock_offset;    //calculate the position of the data block

        cache_writeat(cache, pos, buf, len);
        //ioreadat(backend, pos, buf, len); //read the data block to the buf
        return 0;
    }
}


int allocate_new_data_block(struct ktfs_inode * inode, uint32_t dblock_id){
    unsigned long long pos;
    unsigned long long start_pos_dblock = 1 + fs->superblock.bitmap_block_count + fs->superblock.inode_block_count;
    uint32_t adj_dblock_id;

    uint32_t data_block_idx1; //used to contain indirect datablock indexes in order to find the needed data block
    //uint32_t data_block_idx2;
    uint32_t dindirect_instance; //there are 2 dindirects in inode

    uint32_t temp;
    uint32_t dindirect_offset1;
    uint32_t dindirect_offset2;
    uint32_t new_dblock_id = ktfs_get_new_block();

    if(new_dblock_id == 0)
        return -ENODATABLKS;

    if(dblock_id < 3){  //if the dblock_id is less than 3, then it is a direct block
        inode->block[dblock_id] = new_dblock_id;
        return 0;
    }
    else if((dblock_id - 3) < 128){ //128 is the number of data blocks for indirect reference.

        if(dblock_id == 3){
            temp = ktfs_get_new_block();
            if(temp == 0)
                return -ENODATABLKS;
            inode->indirect = temp;
        } //allocate the new block of pointers


        pos = (start_pos_dblock + inode->indirect) * KTFS_BLKSZ + (dblock_id - 3) * KTFS_DATA_BLOCK_PTR_SIZE; //read the pointer of the block I need in the indirect data block
        cache_writeat(cache, pos, &new_dblock_id, KTFS_DATA_BLOCK_PTR_SIZE); //set the new dblock id

        return 0;
    }
    else{
        if((dblock_id - 131) < (128 * 128)){    //128*128 is the number of data blocks in each double indirect instance. 131 is the number of data blocks from direct and indirect data blocks
            dindirect_instance = 0; //0 as it is the first instance of the double indirect
            adj_dblock_id = dblock_id - 131;    //get the adjusted datablock id of the double indirect data blocks. This is used to calculate the pos
        }
        else{
            dindirect_instance = 1;
            adj_dblock_id = dblock_id - 131 - 128 * 128; //get the adjusted data block if the id is a part of the 2nd double indirect reference.
        }

        dindirect_offset1 = adj_dblock_id / 128; //select offset of indirect pointer in the double indirect block
        dindirect_offset2 = adj_dblock_id % 128; //the offset of direct pointer in the indirect block

        if(adj_dblock_id == 0){
            temp = ktfs_get_new_block();
            if(temp == 0)
                return -ENODATABLKS;
            inode->dindirect[dindirect_instance] = temp;
        }  //get the new block of indirect pointers



        pos = (start_pos_dblock + inode->dindirect[dindirect_instance]) * KTFS_BLKSZ + dindirect_offset1 * KTFS_DATA_BLOCK_PTR_SIZE; //position of the double indirect block.

        if(dindirect_offset2 == 0){ //allocate the block if it is the first ptr of the 2nd indirect block
            temp = ktfs_get_new_block();
            if(temp == 0)
                return -ENODATABLKS;
            data_block_idx1 = temp;
            cache_writeat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);
        }
        else    //get the data_block_idx1 in order to get into it
            cache_readat(cache, pos, &data_block_idx1, KTFS_DATA_BLOCK_PTR_SIZE);


        pos = (start_pos_dblock + data_block_idx1) * KTFS_BLKSZ + dindirect_offset2 * KTFS_DATA_BLOCK_PTR_SIZE; //calculate the position of the direct pointer in the indirect pointer


        cache_writeat(cache, pos, &new_dblock_id, KTFS_DATA_BLOCK_PTR_SIZE);
        return 0;
    }
}

int ktfs_open(const char * name, struct io ** ioptr)
{
    static const struct iointf ktfs_intf = {
        .readat = &ktfs_readat,
        .writeat = &ktfs_writeat,
        .cntl = &ktfs_cntl,
        .close = &ktfs_close

    };

    struct ktfs_file* my_file = kcalloc(1, sizeof(struct ktfs_file));
    struct ktfs_inode root_inode;


    unsigned long long pos = fs->superblock.root_directory_inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;

    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);
    //ioreadat(backend, pos, &root_inode, KTFS_INOSZ);


    //int rw_data_block_at(struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, void * buf, long len)
    uint32_t num_inodes = root_inode.size / KTFS_DENSZ;   //number of inodes
    uint32_t inode_count = 0;
    uint32_t root_dir_inode_blk_cnt;
    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;

    if(root_inode.size % KTFS_BLKSZ != 0) //edge case. Ex. 513/512
        root_dir_inode_blk_cnt++;

    for(int i = 0; i < root_dir_inode_blk_cnt; i++){
        for(int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++){
            read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &my_file->entry, KTFS_DENSZ);
            inode_count++;
            if(strncmp(my_file->entry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0){
                //??? Do I need to fill out the file size here? Instructions say only write/writeat needs to update it
                struct ktfs_inode my_inode;
                pos = my_file->entry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;
                cache_readat(cache, pos, &my_inode, KTFS_INOSZ);

                //ioreadat(backend, pos, &my_inode, KTFS_INOSZ);
                my_file->file_size = my_inode.size;

                insert_file_to_list(my_file); //insert file to the open file list
				*ioptr = create_seekable_io(ioinit1(&my_file->io, &ktfs_intf));
                return 0;
            }

            if(inode_count == num_inodes)   //making sure it doesn't read past the last inode.
                return -ENOENT; //it cooked

        }

    }

    return -ENOENT;
}

void ktfs_close(struct io* io)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);
    delete_file_from_list(my_file->entry.name);
    ktfs_flush();
    //kfree(my_file);
    return;
}

long ktfs_readat(struct io* io, unsigned long long pos, void * buf, long len)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);

    char blkbuf[KTFS_BLKSZ];

    long remaining_len = len;
    uint32_t blkno;

    uint32_t blkoff;

    long cpycnt;
    struct ktfs_inode my_inode;
    unsigned long long inode_pos = my_file->entry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //check this stuff??? on 3-29-25

    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);
    //ioreadat(backend, inode_pos, &my_inode, KTFS_INOSZ);
    debug("position: %d\nlen: %d\n", pos, len);


    if(pos >= my_file->file_size || len < 0)
        return -EINVAL; //cooked

    if((pos + len) > my_file->file_size) //truncate the write if len is too big
        len = my_file->file_size - pos;

    remaining_len = len;

    blkno = pos / KTFS_BLKSZ;
    blkoff = pos % KTFS_BLKSZ;

    cpycnt = KTFS_BLKSZ - blkoff;
    if(cpycnt > len)   // changed < to > //for the case where the data doesn't go to the end of the block
        cpycnt = len;

    read_data_blockat(&my_inode, blkno, 0, blkbuf, KTFS_BLKSZ);
    memcpy(buf, blkbuf + blkoff, cpycnt);
    remaining_len -= cpycnt;
	buf += cpycnt;

    blkno++; //read the next block
    //if data is stored in more than one block
    while(remaining_len != 0){ //made change to >
        read_data_blockat(&my_inode, blkno, 0, blkbuf, KTFS_BLKSZ);

        if(remaining_len > KTFS_BLKSZ)
            cpycnt = KTFS_BLKSZ;
        else
            cpycnt = remaining_len;
        memcpy(buf, blkbuf, cpycnt);//dies here cpycnt gets corrupted
		buf += cpycnt;

        remaining_len -= cpycnt;
        blkno++;
    }


    return len;
}

long ktfs_writeat(struct io* io, unsigned long long pos, const void * buf, long len)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);

    //char blkbuf[KTFS_BLKSZ];

    long remaining_len = len;
    uint32_t blkno;

    uint32_t blkoff;

    long cpycnt;
    struct ktfs_inode my_inode;
    unsigned long long inode_pos = my_file->entry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //check this stuff??? on 3-29-25

    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);

    debug("position: %d\nlen: %d\n", pos, len);

    if(pos >= my_file->file_size || len < 0)
        return -EINVAL; //cooked

    if((pos + len) > my_file->file_size) //truncate the write if len is too big
        len = my_file->file_size - pos;

    remaining_len = len;

    blkno = pos / KTFS_BLKSZ;
    blkoff = pos % KTFS_BLKSZ;

    cpycnt = KTFS_BLKSZ - blkoff;
    if(cpycnt > len)   // changed < to > //for the case where the data doesn't go to the end of the block
        cpycnt = len;

    write_data_blockat(&my_inode, blkno, blkoff, buf, cpycnt);

    remaining_len -= cpycnt;
	buf += cpycnt;

    blkno++; //read the next block
    //if data is stored in more than one block
    while(remaining_len != 0){ //made change to >

        if(remaining_len > KTFS_BLKSZ)
            cpycnt = KTFS_BLKSZ;
        else
            cpycnt = remaining_len;

        write_data_blockat(&my_inode, blkno, 0, buf, cpycnt);

		buf += cpycnt;

        remaining_len -= cpycnt;
        blkno++;
    }


    return len;
}


int ktfs_create(const char *name){

    //rw_data_block_at(int rw, struct ktfs_inode * inode, uint32_t dblock_id, uint32_t dblock_offset, void * buf, long len)
    struct ktfs_inode root_inode;
    struct ktfs_inode my_inode; //newly created inode
    struct ktfs_dir_entry temp_dentry; //temp dentry used for checking file names
    int dentry_cnt = 0;
    uint32_t root_dir_inode_blk_cnt;

    unsigned long long pos = fs->superblock.root_directory_inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;

    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;

    if(root_inode.size % KTFS_BLKSZ != 0) //edge case. Ex. 513/512
        root_dir_inode_blk_cnt++;

    if(strlen(name) > KTFS_MAX_FILENAME_LEN) //checking to see if file name is valid
        return -EINVAL;

    for(int i = 0; i < root_dir_inode_blk_cnt; i++){    //checking to see if there is an existing file name
        for(int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++){
            if (dentry_cnt == (root_inode.size / KTFS_DENSZ))
                goto create_continue;
            read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &temp_dentry, KTFS_DENSZ);
            if(strncmp(temp_dentry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0){
                return -EINVAL;
            }
            dentry_cnt++;

        }

    }

    create_continue:
    uint32_t blkoff = root_inode.size % KTFS_BLKSZ;
    uint32_t blkno = root_inode.size / KTFS_BLKSZ;



    if(blkoff == 0){    //if the blkoff is 0, we need a new block
        if(allocate_new_data_block(&root_inode, blkno) < 0)
            return -ENODATABLKS;
        cache_writeat(cache, pos, &root_inode, KTFS_INOSZ);

    }

    struct ktfs_dir_entry dentry;
    uint16_t new_inode_num;
    if(ktfs_get_new_inode(&new_inode_num) < 0)
        return -ENOINODEBLKS;
    dentry.inode = new_inode_num;  //get a new inode id

    memcpy(dentry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)); //copy the name to the dentry name

    write_data_blockat(&root_inode, blkno, blkoff, &dentry, KTFS_DENSZ);   //add the new dentry into the root directory inode

    root_inode.size += KTFS_DENSZ;  //increment the size in the root directory inode

    cache_writeat(cache, pos, &root_inode, KTFS_INOSZ); //update the root inode

    my_inode.size = 0;  //set the file size to length 0

    pos = dentry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //get the position of the new inode

    cache_writeat(cache, pos, &my_inode, KTFS_INOSZ); //write the inode to the disk

    ktfs_flush(); //To ensure changes persist on the disk.

    return 0;

}

//TODO Add error codes for Not datablocks avail and no inodeblks avail
int ktfs_ext_len(struct ktfs_file * my_file, void * arg){
    unsigned long long len = *(unsigned long long *) arg;

    if(len <= my_file->file_size || len == 0) //TODO max file size
        return 0;

    size_t old_size = my_file->file_size;

    uint32_t start_dblock_id;

    uint32_t last_dblock_id;

    my_file->file_size = len;


    struct ktfs_inode my_inode;
    unsigned long long inode_pos = my_file->entry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //check this stuff??? on 3-29-25


    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);   //read the inode struct

    my_inode.size = len;    //set the new len

    cache_writeat(cache, inode_pos, &my_inode, KTFS_INOSZ);   //write the updated size back

    last_dblock_id = (len - 1) / KTFS_BLKSZ;

    if(old_size == 0){
        start_dblock_id = 0;
    }
    else
        start_dblock_id = (old_size - 1) / KTFS_BLKSZ + 1;    //start at the next block needed

    for(int i = start_dblock_id; i <= last_dblock_id; i++ ){
        if(allocate_new_data_block(&my_inode, i) < 0)
            return -ENODATABLKS;
        cache_writeat(cache, inode_pos, &my_inode, KTFS_INOSZ);
    }
    return 0;


}


int ktfs_delete(const char * name){
    struct ktfs_inode root_inode;
    struct ktfs_inode my_inode;
    struct ktfs_dir_entry dentry; //temp dentry used for checking file names
    struct ktfs_dir_entry last_dentry; //used to insert the last dentry into the one being deleted. Avoids having to shift dentries
    int dentry_cnt = 0;
    uint32_t root_dir_inode_blk_cnt;

    unsigned long long pos = fs->superblock.root_directory_inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ;

    cache_readat(cache, pos, &root_inode, KTFS_INOSZ);

    root_dir_inode_blk_cnt = root_inode.size / KTFS_BLKSZ;

    if(root_inode.size % KTFS_BLKSZ != 0) //edge case. Ex. 513/512
        root_dir_inode_blk_cnt++;

    if(strlen(name) > KTFS_MAX_FILENAME_LEN) //checking to see if file name is valid
        return -EINVAL;

    for(int i = 0; i < root_dir_inode_blk_cnt; i++){    //checking to see if there is an existing file name
        for(int j = 0; j < (KTFS_BLKSZ / KTFS_DENSZ); j++){
            if (dentry_cnt == (root_inode.size / KTFS_DENSZ))
                return -ENOENT; // reached last dentry
            read_data_blockat(&root_inode, i, j * KTFS_DENSZ, &dentry, KTFS_DENSZ);
            if(strncmp(dentry.name, name, KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)) == 0){
                goto found_name;
            }
            dentry_cnt++;

        }

    }
    return -ENOENT; //file not found. Cooked

    found_name:

    unsigned long long inode_pos = dentry.inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //get the inode of the file

    cache_readat(cache, inode_pos, &my_inode, KTFS_INOSZ);   //read the inode


    uint32_t data_block_count = my_inode.size / KTFS_BLKSZ;

    if(my_inode.size % KTFS_BLKSZ != 0) //edge case. Ex. 513/512
        data_block_count++;

    for(int i = data_block_count - 1; i >= 0; i--){ //loop through and release the data blocks
        release_data_block(&my_inode, i);
    }

    //kprintf("Release Dentry Inode: %d \n", dentry.inode);

    ktfs_release_inode(dentry.inode); //release the inode

    //get the blk info of the last dentry
    uint32_t last_blkoff = (root_inode.size - KTFS_DENSZ) % KTFS_BLKSZ;
    uint32_t last_blkno = (root_inode.size - KTFS_DENSZ) / KTFS_BLKSZ;

    uint32_t curr_blkoff = (dentry_cnt * KTFS_DENSZ) % KTFS_BLKSZ;
    uint32_t curr_blkno = (dentry_cnt * KTFS_DENSZ) / KTFS_BLKSZ;


    read_data_blockat(&root_inode, last_blkno, last_blkoff, &last_dentry, KTFS_DENSZ); //read the last dentry

    write_data_blockat(&root_inode, curr_blkno, curr_blkoff, &last_dentry, KTFS_DENSZ); //copy the last dentry to the dentry we are trying to delete

    if(last_blkoff == 0)    //release the dentry block if it is the last entry left in the block
        release_data_block(&root_inode, last_blkno);

    root_inode.size -= KTFS_DENSZ;  //decrement the size in Root Directory Inode

    pos = fs->superblock.root_directory_inode * KTFS_INOSZ + (1 + fs->superblock.bitmap_block_count) * KTFS_BLKSZ; //trust issues

    cache_writeat(cache, pos, &root_inode, KTFS_INOSZ); //write the updated root directory inode to disk


    //TODO need to create a table of file names and their ioptr for the close function
    delete_file_from_list(name);

    ktfs_flush();




    return 0;

}

int ktfs_cntl(struct io *io, int cmd, void *arg)
{
    struct ktfs_file * my_file = (void*)io - offsetof(struct ktfs_file, io);
	size_t * szarg = arg;
    int result;

    switch (cmd) {
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
    return 0;
}

int ktfs_flush(void)
{
    cache_flush(cache);
    return 0;
}
