// cache.c
//

#ifdef CACHE_TRACE
#define TRACE
#endif

#ifdef CACHE_DEBUG
#define DEBUG
#endif

#include "heap.h"
#include "ioimpl.h"
#include "error.h"
#include "string.h"
#include "console.h"
#include "cache.h"
#include "io.h"
#include "thread.h"
#include "conf.h"

// INTERNAL CONSTANT DEFINITIONS
//

#ifndef CACHE_CAPACITY
#define CACHE_CAPACITY 64
#endif

#define CACHE_BLKSZ 512UL

#define CACHE_USED  (1 << 0)
#define CACHE_DIRTY (1 << 1)
#define CACHE_VALID (1 << 2)

// INTERNAL MACRO DEFINITIONS
//

#define CACHE_ISUSED(cache_entry) (((cache_entry).flags & CACHE_USED) != 0)
#define CACHE_ISDIRTY(cache_entry) (((cache_entry).flags & CACHE_DIRTY) != 0)
#define CACHE_ISVALID(cache_entry) (((cache_entry).flags & CACHE_VALID) != 0)

// EXTERNAL TYPE DEFINITIONS
//

struct cache_entry {
    uint32_t block_id;
    uint_fast8_t flags;
};

struct cache {
    struct cache_entry table[CACHE_CAPACITY];
    uint32_t clock_idx;
    uint32_t last_read_idx;
};

// INTERNAL GLOBAL VARIABLES
//

static struct io * backend; // backing device pointer
static uint8_t cache_data[CACHE_CAPACITY][CACHE_BLKSZ];
static struct lock cache_locks[CACHE_CAPACITY];

// EXTERNAL FUNCTION DEFINITIONS
//

int create_cache(struct io * bkgio, struct cache ** cptr) {
    struct cache * my_cache;

    trace("%s()", __func__);

    my_cache = kcalloc(1, sizeof(struct cache));
    backend = ioaddref(bkgio); // save backend io
    *cptr = my_cache; // return cache pointer

    for (int i = 0; i < CACHE_CAPACITY; i++) {
        my_cache->table[i].flags = 0;
        lock_init(&cache_locks[i]);
    }

    my_cache->clock_idx = 0;
    my_cache->last_read_idx = 0;

    return 0;
}

// TODO: rewrite for this function to return idx found

// clock (second chance) algorithm
int cache_get_block(struct cache * cache, unsigned long long pos, void ** pptr) {
    trace("%s(pos=%lld, pptr=%p)", __func__, pos, pptr);

    // check if pos is aligned with block size
    if (pos % CACHE_BLKSZ != 0) {
        debug("pos=%lld is not %d aligned", pos, CACHE_BLKSZ);
        return -EINVAL;
    }

    uint64_t block_id = pos / CACHE_BLKSZ;
    debug("block=%lld", block_id);

    // check if block is already in cache
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        if (cache->table[i].block_id == block_id &&
            CACHE_ISVALID(cache->table[i]))
        {
            debug("already in cache");
            lock_acquire(&cache_locks[i]);
            cache->table[i].flags |= CACHE_USED;
            cache->last_read_idx = i;
            *pptr = cache_data[i];
            return 0;
        }

    }

    // search for cache entry that has not been (used=0) in a while
    // if it has been (used=1) give it a "second chance" by making used=0
    // but moving to the next entry in the table
    // if the entry gets called again (used=1) it will be saved the next
    // time cache idx comes around
    // otherwise the entry will be replaced
    // iterate through the cache table like a ring

    while (1) {
        if (!CACHE_ISUSED(cache->table[cache->clock_idx]))
            break;

        // if used=1
        cache->table[cache->clock_idx].flags &= ~CACHE_USED;
        cache->clock_idx = (cache->clock_idx + 1) % CACHE_CAPACITY;
    }

    uint32_t idx = cache->clock_idx;

    debug("adding to cache");
    debug("cache clock idx=%d", idx);
    debug("old block=%lld", cache->table[idx].block_id);

    // put back data stored in old cache idx
    // then get new block of data and store at old cache idx

    lock_acquire(&cache_locks[idx]);
    debug("cache idx=%d lock acquired", idx);
    ioreadat(backend, pos, cache_data[idx], CACHE_BLKSZ);

    cache->table[idx].block_id = block_id;
    cache->table[idx].flags = CACHE_USED | CACHE_VALID;
    cache->last_read_idx = idx;
    *pptr = cache_data[idx];

    return 0;
}

int cache_read_at(struct cache * cache, unsigned long long pos, void * buf, long bufsz) {
    uint8_t * block;
    uint32_t block_pos;
    uint32_t block_off;
    uint32_t idx;

    trace("%s(rw=%d, pos=%lld, buf=%p, bufsz=%ld)",
            __func__, 0, pos, buf, bufsz);

    block_pos = pos / CACHE_BLKSZ * CACHE_BLKSZ;
    block_off = pos % CACHE_BLKSZ;

    if (bufsz + block_off > CACHE_BLKSZ) {
        bufsz = CACHE_BLKSZ - block_off;
    }

    cache_get_block(cache, block_pos, (void **)&block);
    memcpy(buf, block + block_off, bufsz);

    idx = ((uint64_t)block - (uint64_t)&cache_data) / 512;
    cache_release_block(cache, cache_data[idx], CACHE_ISDIRTY(cache->table[idx]));

    return bufsz;
}

int cache_write_at(struct cache * cache, unsigned long long pos, const void * buf, long len) {
    uint8_t * block;
    uint32_t block_pos;
    uint32_t block_off;
    uint32_t idx;

    trace("%s(rw=%d, pos=%lld, buf=%p, len=%ld)",
            __func__, 1, pos, buf, len);

    block_pos = pos / CACHE_BLKSZ * CACHE_BLKSZ;
    block_off = pos % CACHE_BLKSZ;

    if (len + block_off > CACHE_BLKSZ) {
        len = CACHE_BLKSZ - block_off;
    }

    cache_get_block(cache, block_pos, (void **) &block);
    memcpy(block + block_off, buf, len);

    idx = ((uint64_t)block - (uint64_t)&cache_data) / 512;
    cache->table[idx].flags |= CACHE_DIRTY;
    cache_release_block(cache, cache_data[idx], CACHE_ISDIRTY(cache->table[idx]));

    return len;
}

// TODO: rewrite this function to just take cache block id

void cache_release_block(struct cache * cache, void * pblk, int dirty) {
    uint32_t idx;
    uint32_t block_id;

    trace("%s(pblk=%p, dirty=%d)", __func__, pblk, dirty);

    idx = ((uint64_t)pblk - (uint64_t)&cache_data) / 512;
    block_id = cache->table[idx].block_id;
    debug("release_block: idx=%d, block_id=%d", idx, block_id);

    if(dirty == 1)
        iowriteat(backend, block_id * CACHE_BLKSZ, pblk, CACHE_BLKSZ);

    cache->table[idx].flags &= ~CACHE_DIRTY;

    if (cache_locks[idx].owner == current_thread())
        lock_release(&cache_locks[idx]);
}

int cache_flush(struct cache * cache) {
    trace("%s()", __func__);

    for(int i = 0; i < CACHE_CAPACITY; i++) {
        cache_release_block(
                cache, cache_data[i], CACHE_ISDIRTY(cache->table[i]));
    }

    return 0;
}
