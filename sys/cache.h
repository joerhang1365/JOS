// cache.h - Block cache for a storage device
//
// Copyright (c) 2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _CACHE_H_
#define _CACHE_H_

struct cache; // opaque decl.

extern int create_cache(struct io * bkgio, struct cache ** cptr);
extern int cache_get_block (
        struct cache * cache, unsigned long long pos, void ** pptr);

extern int cache_readat (
        struct cache * cache, unsigned long long pos, void * buf, long bufsz);

extern int cache_writeat (
        struct cache * cache, unsigned long long pos, const void * buf,
        long bufsz);

extern void cache_release_block(struct cache * cache, void * pblk, int dirty);
extern int cache_flush(struct cache * cache);

#endif // _CACHE_H_
