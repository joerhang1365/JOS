// vioblk.c - VirtIO serial port (console)
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include "virtio.h"
#include "intr.h"
#include "assert.h"
#include "heap.h"
#include "ioimpl.h"
#include "io.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "conf.h"

#include <limits.h>

// COMPILE-TIME PARAMETERS
//

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// INTERNAL CONSTANT DEFINITIONS
//

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

// VirtIO block device requests

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_T_GET_ID         8
#define VIRTIO_BLK_T_GET_LIFETIME   10
#define VIRTIO_BLK_T_DISCARD        11
#define VIRTIO_BLK_T_WRITE_ZEROES   13
#define VIRTIO_BLK_T_SECURE_ERASE   14

// VirtIO block device statuses

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

// Virtio block device request sizes

#define VIRTIO_BLK_REQ_HEADER_SIZE 16
#define VIRTIO_BLK_REQ_SECTOR_SIZE 512
#define VIRTIO_BLK_REQ_FOOTER_SIZE 1

 // INTERNAL TYPE DEFINITIONS
 //

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    int irqno;
    int instno;
    struct io io;

    struct {
        uint16_t last_used_idx;

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        struct virtq_desc desc_table[3];
        struct virtq_desc indirect_desc;

    } vq;

    struct {
        uint32_t type;  // read, write, discard, etc...
        uint32_t reserved;
        uint64_t sector; // offset * 512 where read or write occured
        char * buf;
        char status;
    } req;

    uint64_t capacity;
    uint32_t blksz;

    struct condition block_ready;
    struct lock vlock;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int vioblk_open(struct io ** ioptr, void * aux);
static void vioblk_close(struct io * io);

static long vioblk_readat (
    struct io * io,
    unsigned long long pos,
    void * buf,
    long bufsz);

static long vioblk_writeat (
    struct io * io,
    unsigned long long pos,
    const void * buf,
    long len);

static int vioblk_cntl(struct io * io, int cmd, void * arg);

static void vioblk_isr(int srcno, void * aux);

static int request_block (
        struct vioblk_device * vioblk,
        uint32_t type,
        uint64_t sector,
        uint32_t queue,
        uint32_t queue_max);

// EXPORTED FUNCTION DEFINITIONS
//

// Attaches a VirtIO block device. Declared and called directly from virtio.c

// Negotiate features. We need:
//  - VIRTIO_F_RING_RESET and
//  - VIRTIO_F_INDIRECT_DESC
// We want:
//  - VIRTIO_BLK_F_BLK_SIZE and
//  - VIRTIO_BLK_F_TOPOLOGY.

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    struct vioblk_device * vioblk;
    uint32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);
    debug("device id=%d", regs->device_id);

    regs->status |= VIRTIO_STAT_DRIVER;
    virtio_featset_t enabled_features, wanted_features, needed_features;

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs, enabled_features,
            wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: FAILED virtio feature negotiation\n", regs);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    // if the device provides a block size, use it. otherwise, use 512
    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    // blksz must be a power of two
    assert (((blksz - 1) & blksz) == 0);

    // initialize and attach vioblk device

    vioblk = kcalloc(1, sizeof(struct vioblk_device));
    vioblk->req.buf = kmalloc(blksz);

    assert (vioblk != NULL);
    assert (vioblk->req.buf != NULL);

    vioblk->regs = regs;
    vioblk->irqno = irqno;
    vioblk->capacity = regs->config.blk.capacity;
    vioblk->blksz = blksz;
    condition_init(&vioblk->block_ready, "vioblk_block_ready");
    lock_init(&vioblk->vlock);

    static const struct iointf vioblk_iointf = {
        .close = &vioblk_close,
        .cntl = &vioblk_cntl,
        .readat = &vioblk_readat,
        .writeat = &vioblk_writeat,
    };

    ioinit0(&vioblk->io, &vioblk_iointf);
    vioblk->instno = register_device(VIOBLK_NAME, vioblk_open, vioblk);
    debug("instance no=%d", vioblk->instno);

    // attach virtqueue

    vioblk->regs->queue_sel = 0;
    vioblk->regs->queue_num = 1;

    // indirect descriptor
    vioblk->vq.indirect_desc.addr = (uint64_t)&vioblk->vq.desc_table;
    vioblk->vq.indirect_desc.len = VIRTQ_DESC_SIZE * 3;
    vioblk->vq.indirect_desc.flags = VIRTQ_DESC_F_INDIRECT;
    vioblk->vq.indirect_desc.next = 0;

    // header
    vioblk->vq.desc_table[0].addr = (uint64_t)&vioblk->req;
    vioblk->vq.desc_table[0].len = VIRTIO_BLK_REQ_HEADER_SIZE;
    vioblk->vq.desc_table[0].flags = VIRTQ_DESC_F_NEXT;
    vioblk->vq.desc_table[0].next = 1;

    // sector
    vioblk->vq.desc_table[1].addr = (uint64_t)vioblk->req.buf;
    vioblk->vq.desc_table[1].len = blksz;
    vioblk->vq.desc_table[1].flags = VIRTQ_DESC_F_NEXT;
    vioblk->vq.desc_table[1].next = 2;

    // footer
    vioblk->vq.desc_table[2].addr = (uint64_t)&vioblk->req.status;
    vioblk->vq.desc_table[2].len = VIRTIO_BLK_REQ_FOOTER_SIZE;
    vioblk->vq.desc_table[2].flags = VIRTQ_DESC_F_WRITE;
    vioblk->vq.desc_table[2].next = 0;

    virtio_attach_virtq(regs, 0, 1, (uint64_t)&vioblk->vq.indirect_desc,
            (uint64_t)&vioblk->vq.used, (uint64_t)&vioblk->vq.avail);
    virtio_enable_virtq(regs, 0);

    if (regs->queue_ready != 1) {
        kprintf("%p: FAILED queue %d not ready\n", regs, 0);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    regs->status |= VIRTIO_STAT_DRIVER_OK;
}

int vioblk_open(struct io ** ioptr, void * aux) {
    struct vioblk_device * const vioblk = aux;

    // register the VIOBLK interrupt handler
    enable_intr_source(vioblk->irqno, VIOBLK_INTR_PRIO, vioblk_isr, aux);
    *ioptr = ioaddref(&vioblk->io);

    return 0;
}

void vioblk_close(struct io * io) {
    assert(io != NULL && iorefcnt(io) == 0);

    struct vioblk_device * vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    disable_intr_source(vioblk->irqno);
    virtio_reset_virtq(vioblk->regs, 0);
}

long vioblk_readat (
    struct io * io,
    unsigned long long pos,
    void * buf,
    long bufsz)
{
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    uint64_t sector;
    uint64_t read_bytes;

    lock_acquire(&vioblk->vlock);
    trace("%s(pos=%lld, bufsz=%ld)", __func__, pos, bufsz);

    if (bufsz < 0)
        return -EINVAL;

    // read must be 512 byte aligned
    if (pos % vioblk->blksz != 0 ||
        bufsz % vioblk->blksz != 0)
        return -EINVAL;

    sector = pos / vioblk->blksz;
    read_bytes = 0;

    // read each block and copy to buf
    while (read_bytes < bufsz) {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > vioblk->capacity)
            return -EACCESS;

        request_block(vioblk, VIRTIO_BLK_T_IN, sector, 0, 1);
        memcpy(buf + read_bytes, vioblk->req.buf, vioblk->blksz);

        read_bytes += vioblk->blksz;

        debug("sector=%lld", sector);
        debug("read_bytes=%lld", read_bytes);

        sector++;
    }

    __sync_synchronize();
    lock_release(&vioblk->vlock);

    return read_bytes;
}

long vioblk_writeat (
    struct io * io,
    unsigned long long pos,
    const void * buf,
    long len)
{
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    uint64_t sector;
    uint64_t write_bytes;

    lock_acquire(&vioblk->vlock);
    trace("%s(pos=%lld, len=%ld)", __func__, pos, len);

    if (len < 0)
        return -EINVAL;

    // write must be 512 byte aligned
    if (pos % vioblk->blksz != 0 ||
        len % vioblk->blksz != 0)
        return -EINVAL;

    sector = pos / vioblk->blksz;
    write_bytes = 0;

    while (write_bytes < len) {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > vioblk->capacity)
            return -EACCESS;

        memcpy(vioblk->req.buf, buf + write_bytes, vioblk->blksz);
        request_block(vioblk, VIRTIO_BLK_T_OUT, sector, 0, 1);

        write_bytes += vioblk->blksz;

        debug("sector=%lld", sector);
        debug ("write_bytes=%lld", write_bytes);

        sector++;
    }

    __sync_synchronize();
    lock_release(&vioblk->vlock);

    return write_bytes;
}

int vioblk_cntl(struct io * io, int cmd, void * arg) {
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    size_t * szarg = arg;
    int result;

    switch (cmd) {
    case IOCTL_GETBLKSZ:
        result = vioblk->blksz;
        break;
    case IOCTL_GETEND:
        *szarg = vioblk->capacity * vioblk->blksz;
        result = 0;
        break;
    default:
        result = -ENOTSUP;
    }

    return result;
}

void vioblk_isr(int srcno, void * aux) {
    struct vioblk_device * vioblk = aux;

    if (vioblk->regs->interrupt_status & VIRTQ_INTR_USED) {

        vioblk->vq.last_used_idx = vioblk->vq.used.idx;
        vioblk->regs->interrupt_ack = VIRTQ_INTR_USED;

        condition_broadcast(&vioblk->block_ready);
    }

    __sync_synchronize();
}

int request_block (
        struct vioblk_device * vioblk,
        uint32_t type,
        uint64_t sector,
        uint32_t queue,
        uint32_t queue_max)
{
    uint16_t avail_idx;
    // int pie;

    // specify which block to read from
    vioblk->req.type = type;
    vioblk->req.sector = sector;
    vioblk->vq.desc_table[1].flags = VIRTQ_DESC_F_NEXT;

    // if trying to read make page writeble
    if (type == VIRTIO_BLK_T_IN)
        vioblk->vq.desc_table[1].flags |= VIRTQ_DESC_F_WRITE;

    // put descriptors into available ring buffer
    avail_idx = vioblk->vq.avail.idx % queue_max;
    vioblk->vq.avail.ring[avail_idx] = 0; // head descriptor
    vioblk->vq.avail.idx++;
    virtio_notify_avail(vioblk->regs, queue);

    // wait until data is ready to read
    // pie = disable_interrupts();
    while (vioblk->vq.avail.idx != vioblk->vq.used.idx) {
        //condition_wait(&vioblk->block_ready);
        continue;
    }

    // restore_interrupts(pie);
    __sync_synchronize();

    if (vioblk->req.status != VIRTIO_BLK_S_OK) {
        return -EIO;
    }

    return 0;
}
