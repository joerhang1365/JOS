// io.c - Unified I/O object
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "io.h"
#include "ioimpl.h"
#include "assert.h"
#include "string.h"
#include "heap.h"
#include "error.h"
#include "thread.h"
#include "memory.h"
#include "intr.h"
#include <stddef.h>
#include <limits.h>

// INTERNAL TYPE DEFINITIONS
//

struct nullio
{
    struct io io;
};

struct memio
{
    struct io io;       // I/O struct of memory I/O
    void * buf;         // Block of memory
    size_t size;        // Size of memory block
    struct lock lock;
};

struct seekio
{
    struct io io;           // I/O struct of seek I/O
    struct io * bkgio;      // Backing I/O supporting _readat_ and _writeat_
    unsigned long long pos; // Current position within backing endpoint
    unsigned long long end; // End position in backing endpoint
    int blksz;              // Block size of backing endpoint
};

struct pipe
{
    struct io wio;
    struct io rio;
    char * buf;
    unsigned int hpos;
    unsigned int tpos;
    struct condition buf_empty;
    struct condition buf_full;
};

// INTERNAL FUNCTION DEFINITIONS
//

static long nullio_read(struct io * io, void * buf, long bufsz);
static long nullio_write(struct io * io, const void * buf, long len);

static int memio_cntl(struct io * io, int cmd, void * arg);
static long memio_readat (
        struct io * io, unsigned long long pos, void * buf, long bufsz);
static long memio_writeat (
    struct io * io, unsigned long long pos, const void * buf, long len);

static void seekio_close(struct io * io);
static int seekio_cntl(struct io * io, int cmd, void * arg);
static long seekio_read(struct io * io, void * buf, long bufsz);
static long seekio_write(struct io * io, const void * buf, long len);
static long seekio_readat (
    struct io * io, unsigned long long pos, void * buf, long bufsz);
static long seekio_writeat (
    struct io * io, unsigned long long pos, const void * buf, long len);

void create_pipe(struct io ** wioptr, struct io ** rioptr);
static long pipe_read(struct io * rio, void * buf, long len);
static long pipe_write(struct io * wio, const void * buf, long len);
static void pipe_close_wio(struct io * wio);
static void pipe_close_rio(struct io * rio);

static int pipe_rbuf_empty(const struct pipe * pipe);
static int pipe_rbuf_full(const struct pipe * pipe);
static void pipe_rbuf_putc(struct pipe * pipe, uint8_t c);
static uint8_t pipe_rbuf_getc(struct pipe * pipe);

// EXPORTED FUNCTION DEFINITIONS
//

struct io * ioinit0(struct io * io, const struct iointf * intf)
{
    assert (io != NULL);
    assert (intf != NULL);
    io->intf = intf;
    io->refcnt = 0;
    return io;
}

struct io * ioinit1(struct io * io, const struct iointf * intf)
{
    assert (io != NULL);
    io->intf = intf;
    io->refcnt = 1;
    return io;
}

unsigned long iorefcnt(const struct io * io)
{
    assert (io != NULL);
    return io->refcnt;
}

struct io * ioaddref(struct io * io)
{
    assert (io != NULL);
    io->refcnt += 1;
    return io;
}

void ioclose(struct io * io)
{
    assert (io != NULL);
    assert (io->intf != NULL);

    assert (io->refcnt != 0);
    io->refcnt -= 1;

    if (io->refcnt == 0 && io->intf->close != NULL)
    {
        io->intf->close(io);
    }
}

long ioread(struct io * io, void * buf, long bufsz)
{
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->read == NULL)
    {
        return -ENOTSUP;
    }
    else if (bufsz < 0)
    {
        return -EINVAL;
    }

    return io->intf->read(io, buf, bufsz);
}

long iofill(struct io * io, void * buf, long bufsz)
{
    long bufpos = 0; // position in buffer for next read
    long nread; // result of last read

    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->read == NULL)
    {
        return -ENOTSUP;
    }
    else if (bufsz < 0)
    {
        return -EINVAL;
    }

    while (bufpos < bufsz)
    {
        nread = io->intf->read(io, buf+bufpos, bufsz-bufpos);

        if (nread <= 0)
        {
            return (nread < 0) ? nread : bufpos;
        }

        bufpos += nread;
    }

    return bufpos;
}

long iowrite(struct io * io, const void * buf, long len)
{
    long bufpos = 0; // position in buffer for next write
    long n; // result of last write

    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->write == NULL)
    {
        return -ENOTSUP;
    }
    else if (len < 0)
    {
        return -EINVAL;
    }

    do
    {
        n = io->intf->write(io, buf+bufpos, len-bufpos);

        if (n <= 0)
        {
            return (n < 0) ? n : bufpos;
        }

        bufpos += n;
    }
    while (bufpos < len);

    return bufpos;
}

long ioreadat(struct io * io, unsigned long long pos, void * buf, long bufsz)
{
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->readat == NULL)
    {
        return -ENOTSUP;
    }
    else if (bufsz < 0)
    {
        return -EINVAL;
    }

    return io->intf->readat(io, pos, buf, bufsz);
}

long iowriteat (
        struct io * io, unsigned long long pos, const void * buf, long len)
{
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->writeat == NULL)
    {
        return -ENOTSUP;
    }
    else if (len < 0)
    {
        return -EINVAL;
    }

    return io->intf->writeat(io, pos, buf, len);
}

int ioctl(struct io * io, int cmd, void * arg)
{
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->cntl != NULL)
    {
        return io->intf->cntl(io, cmd, arg);
    }
    else if (cmd == IOCTL_GETBLKSZ)
    {
        return 1; // default block size
    }
    else
    {
        return -ENOTSUP;
    }
}

int ioblksz(struct io * io)
{
    return ioctl(io, IOCTL_GETBLKSZ, NULL);
}

int ioseek(struct io * io, unsigned long long pos)
{
    return ioctl(io, IOCTL_SETPOS, &pos);
}

struct io * create_null_io()
{
    static const struct iointf null_iointf =
    {
        .read = &nullio_read,
        .write = &nullio_write,
    };

    struct nullio * nullio;

    nullio = kcalloc(1, sizeof(struct nullio));
    return ioinit1(&nullio->io, &null_iointf);
}

long nullio_read(struct io * io, void * buf, long bufsz)
{
    return 0;
}

long nullio_write(struct io * io, const void * buf, long len)
{
    return 0;
}

struct io * create_memory_io(void * buf, size_t size)
{
    static const struct iointf mem_iointf =
    {
        .readat = &memio_readat,
        .writeat = &memio_writeat,
        .cntl = &memio_cntl
    };

    struct memio * mio;

    mio = kcalloc(1, sizeof(struct memio));
    mio->buf = buf;
    mio->size = size;

    lock_init(&mio->lock);
    return ioinit1(&mio->io, &mem_iointf);
}

struct io * create_seekable_io(struct io * io)
{
    static const struct iointf seekio_iointf =
    {
        .close = &seekio_close,
        .cntl = &seekio_cntl,
        .read = &seekio_read,
        .write = &seekio_write,
        .readat = &seekio_readat,
        .writeat = &seekio_writeat
    };

    struct seekio * sio;
    unsigned long end;
    int result;
    int blksz;

    blksz = ioblksz(io);
    assert (0 < blksz);

    // block size must be power of two
    assert ((blksz & (blksz - 1)) == 0);

    result = ioctl(io, IOCTL_GETEND, &end);
    assert (result == 0);

    sio = kcalloc(1, sizeof(struct seekio));
    sio->pos = 0;
    sio->end = end;
    sio->blksz = blksz;
    sio->bkgio = ioaddref(io);

    return ioinit1(&sio->io, &seekio_iointf);
};

// INTERNAL FUNCTION DEFINITIONS
//

long memio_readat (
    struct io * io,
    unsigned long long pos,
    void * buf, long bufsz)
{
    if (bufsz < 0)
    {
        return -EINVAL;
    }
    else if (bufsz == 0)
    {
        return 0;
    }

    struct memio * const mio = (void*)io - offsetof(struct memio, io);

    // block of memory used for memio with offset added
    void * membuf = mio->buf + pos;
    long read_bytes;

    if (pos > mio->size)
    {
        return -EINVAL;
    }

    // number of bytes that can be read
    if (pos + bufsz > mio->size)
    {
        read_bytes = mio->size - pos;
    }
    else
    {
        read_bytes = bufsz;
    }

    lock_acquire(&mio->lock);
    memcpy(buf, membuf, read_bytes);
    lock_release(&mio->lock);

    return read_bytes;
}

long memio_writeat (
    struct io * io,
    unsigned long long pos,
    const void * buf, long len)
{
    struct memio * const mio = (void*)io - offsetof(struct memio, io);
    void * membuf;;
    long write_bytes;

    if (len < 0)
    {
        return -EINVAL;
    }
    else if (len == 0)
    {
        return 0;
    }

    if (pos > mio->size)
    {
        return -EINVAL;
    }

    membuf = mio->buf + pos;

    if (pos + len > mio->size)
    {
        write_bytes = mio->size - len;
    }
    else
    {
        write_bytes = len;
    }

    lock_acquire(&mio->lock);
    memcpy(membuf, buf, write_bytes);
    lock_release(&mio->lock);

    return write_bytes;
}

int memio_cntl(struct io * io, int cmd, void * arg)
{
    struct memio * const mio = (void*)io - offsetof(struct memio, io);
    size_t * szarg = arg;
    int result;

    switch (cmd)
    {
    case IOCTL_GETBLKSZ:
        result = 1;
        break;
    case IOCTL_GETEND:
        *szarg = mio->size;
        result = 0;
        break;
    case IOCTL_SETEND:
        if (*szarg > mio->size)
        {
            result = -EINVAL;
        }
        else
        {
            mio->size = *szarg;
            result = 0;
        }
    default:
        result = -EINVAL;
    }

    return result;
}

void seekio_close(struct io * io)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    sio->bkgio->refcnt--;
    ioclose(sio->bkgio);
    kfree(sio);
}

int seekio_cntl(struct io * io, int cmd, void * arg)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    unsigned long long * ullarg = arg;
    int result;

    switch (cmd)
    {
    case IOCTL_GETBLKSZ:
        result = sio->blksz;
        break;
    case IOCTL_GETPOS:
        *ullarg = sio->pos;
        result = 0;
        break;
    case IOCTL_SETPOS:
        // new position must be multiple of block size
        // or must not be past end
        if ((*ullarg & (sio->blksz - 1)) != 0 ||
            *ullarg > sio->end)
        {
            result = -EINVAL;
        }
        else
        {
            sio->pos = *ullarg;
            result =  0;
        }
        break;
    case IOCTL_GETEND:
        *ullarg = sio->end;
        result = 0;
        break;
    case IOCTL_SETEND:
        // call backing endpoint ioctl and save result
        result = ioctl(sio->bkgio, IOCTL_SETEND, ullarg);
        if (result == 0)
        {
            sio->end = *ullarg;
        }
        break;
    default:
        result = ioctl(sio->bkgio, cmd, arg);
    }

    return result;
}

long seekio_read(struct io * io, void * buf, long bufsz)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    uint64_t const pos = sio->pos;
    uint64_t const end = sio->end;
    long rcnt;

    debug("seekio pos=%x\n", pos);
    debug("seekio bufsz=%x\n", bufsz);
    debug("seekio blksz=%x\n", sio->blksz);

    if (end - pos < bufsz)
    {
        bufsz = end - pos;
    }
    else if (bufsz == 0)
    {
        return 0;
    }
    else if (bufsz < sio->blksz)
    {
        return -EINVAL;
    }

    // truncate buffer size to multiple of blksz
    bufsz &= ~(sio->blksz - 1);
    rcnt = ioreadat(sio->bkgio, pos, buf, bufsz);
    sio->pos = pos + ((rcnt < 0) ? 0 : rcnt);
    return rcnt;
}

long seekio_write(struct io * io, const void * buf, long len)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    unsigned long long const pos = sio->pos;
    unsigned long long end = sio->end;
    int result;
    long wcnt;

    if (len == 0)
    {
        return 0;
    }
    else if (len < sio->blksz)
    {
        return -EINVAL;
    }

    // truncate length to multiple of blksz
    len &= ~(sio->blksz - 1);

    // check if write is past end. If it is, we need to change end position.
    if (end - pos < len)
    {
        if (ULLONG_MAX - pos < len)
        {
            return -EINVAL;
        }

        end = pos + len;
        result = ioctl(sio->bkgio, IOCTL_SETEND, &end);

        if (result != 0)
        {
            return result;
        }

        sio->end = end;
    }

    wcnt = iowriteat(sio->bkgio, sio->pos, buf, len);
    sio->pos = pos + ((wcnt < 0) ? 0 : wcnt);
    return wcnt;
}

long seekio_readat (
        struct io * io, unsigned long long pos, void * buf, long bufsz)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    return ioreadat(sio->bkgio, pos, buf, bufsz);
}

long seekio_writeat (
        struct io * io, unsigned long long pos, const void * buf, long len)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    return iowriteat(sio->bkgio, pos, buf, len);
}

void create_pipe(struct io ** wioptr, struct io ** rioptr)
{
    static const struct iointf pipe_iointf_w =
    {
        .write = &pipe_write,
        .close = &pipe_close_wio
    };

    static const struct iointf pipe_iointf_r =
    {
        .read = &pipe_read,
        .close = &pipe_close_rio
    };

    struct pipe * pipe;

    pipe = kcalloc(1, sizeof(struct pipe));
    pipe->buf = alloc_phys_page();
    *wioptr = ioinit1(&pipe->wio, &pipe_iointf_w);
    *rioptr = ioinit1(&pipe->rio, &pipe_iointf_r);
    condition_init(&pipe->buf_full, "buf_full");
    condition_init(&pipe->buf_empty, "buf_empty");
}

void pipe_rbuf_putc(struct pipe * pipe, uint8_t c)
{
    uint_fast16_t tpos;

    tpos = pipe->tpos;
    pipe->buf[tpos % PAGE_SIZE] = c;
    //memcpy(pipe->buf + (tpos % PAGE_SIZE), &c, 1);
    asm volatile ("" ::: "memory");
    pipe->tpos = tpos + 1;
}

uint8_t pipe_rbuf_getc(struct pipe * pipe)
{
    uint_fast16_t hpos;
    char c;

    hpos = pipe->hpos;
    c = pipe->buf[hpos % PAGE_SIZE];
    //memcpy(&c, pipe->buf + (hpos % PAGE_SIZE), 1);
    asm volatile ("" ::: "memory");
    pipe->hpos = hpos + 1;
    return c;
}

int pipe_rbuf_empty(const struct pipe * pipe)
{
    return (pipe->hpos == pipe->tpos);
}

int pipe_rbuf_full(const struct pipe * pipe)
{
    return ((uint16_t)(pipe->tpos - pipe->hpos) == PAGE_SIZE);
}

long pipe_write(struct io * wio, const void * buf, long len)
{
    struct pipe * my_pipe;
    long write_bytes;
    char * bufptr;
    char c;
    int pie;

    my_pipe = (void*) wio - offsetof(struct pipe, wio);

    if (wio->refcnt == 0 ||
        my_pipe->rio.refcnt == 0)
    {
        return -EPIPE;
    }
    else if (len <= 0)
    {
        return 0;
    }

    if (len > PAGE_SIZE)
    {
        len = PAGE_SIZE;
    }

    write_bytes = 0;
    bufptr = (char *)buf;

    while (write_bytes < len)
    {
        pie = disable_interrupts();

        while (pipe_rbuf_full(my_pipe))
        {
            condition_wait(&my_pipe->buf_full);
        }

        restore_interrupts(pie);

        if (wio->refcnt == 0 || my_pipe->rio.refcnt == 0)
        {
            return -EPIPE;
        }

        c = *bufptr;
        bufptr++;
        write_bytes++;

        pipe_rbuf_putc(my_pipe, c);
    }

    condition_broadcast(&my_pipe->buf_empty);

    return len;
}


long pipe_read(struct io * rio, void * buf, long len)
{
    struct pipe * my_pipe = (void*)rio - offsetof(struct pipe, rio);
    long read_bytes;
    char * bufptr;
    char c;
    int pie;

    if (rio->refcnt == 0)
    {
        return -EPIPE;
    }
    else if (my_pipe->wio.refcnt == 0)
    {
        return 0;
    }

    if (len > PAGE_SIZE)
    {
        len = PAGE_SIZE;
    }

    pie = disable_interrupts();

    if (pipe_rbuf_empty(my_pipe))
    {
        condition_wait(&my_pipe->buf_empty);
        if (rio->refcnt == 0)
        {
            restore_interrupts(pie);
            return -EPIPE;
        }
    }

    restore_interrupts(pie);
    read_bytes = 0;
    bufptr = (char *)buf;

    do
    {
        c = pipe_rbuf_getc(my_pipe);
        *bufptr = c;
        bufptr++;
        read_bytes++;

        if (pipe_rbuf_empty(my_pipe))
        {
            break;
        }

    }
    while (read_bytes < len);

    condition_broadcast(&my_pipe->buf_full);

    return read_bytes;
}

void pipe_close_wio(struct io * wio)
{
    struct pipe * my_pipe = (void*)wio - offsetof(struct pipe, wio);

    condition_broadcast(&my_pipe->buf_full);
    condition_broadcast(&my_pipe->buf_empty);

    if (my_pipe->rio.refcnt == 0 && my_pipe->wio.refcnt == 0)
    {
        free_phys_page(my_pipe->buf);
        kfree(my_pipe);
    }
}

void pipe_close_rio(struct io * rio)
{
    struct pipe * my_pipe = (void*)rio - offsetof(struct pipe, rio);

    condition_broadcast(&my_pipe->buf_full);
    condition_broadcast(&my_pipe->buf_empty);

    if(my_pipe->rio.refcnt == 0 && my_pipe->wio.refcnt == 0)
    {
        free_phys_page(my_pipe->buf);
        kfree(my_pipe);
        // condition broadcast ?
    }
}
