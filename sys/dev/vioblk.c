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

struct __attribute__((packed)) vioblk_config
{
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct
    {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    }
    geometry;

    uint32_t blksz;
    struct
    {
        uint8_t physical_block_exp;
        uint8_t alignment_offset;
        uint16_t min_io_size;
        uint32_t opt_io_size;
    }
    topology;

    uint8_t writeback;
    char unused0;
    uint16_t num_queues;
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t write_zeroes_may_unmap;
    char unused1[3];
    uint32_t max_secure_erase_sectors;
    uint32_t max_secure_erase_seg;
    uint32_t secure_erase_sector_alignment;
};

struct vioblk_request
{
    uint32_t type;      // read, write, discard, etc...
    uint32_t reserved;
    uint64_t sector;    // offset * 512 where read or write occured
    // NO DATA INCLUDED
    uint8_t status;
};

struct vioblk_device
{
    volatile struct virtio_mmio_regs * regs;
    struct io io;
    int irqno;
    int instno;

    struct virtqueue * virtq;
    struct vioblk_config conf;

    struct condition ready;
    struct lock vlock;
};

static vioblk_request req;

// INTERNAL FUNCTION DECLARATIONS
//

static int vioblk_open(struct io ** ioptr, void * aux);
static void vioblk_close(struct io * io);
static long vioblk_readat(struct io * io, unsigned long long pos, void * buf, long bufsz);
static long vioblk_writeat(struct io * io, unsigned long long pos, const void * buf, long len);
static int vioblk_cntl(struct io * io, int cmd, void * arg);

static void vioblk_isr(int srcno, void * aux);

static int request_block (
        struct vioblk_device * vioblk, uint32_t type, uint64_t sector,
        uint32_t queue, uint32_t queue_max, char * data);

// EXPORTED FUNCTION DEFINITIONS
//

// Attaches a VirtIO block device. Declared and called directly from virtio.c

// Negotiate features. We need:
//  - VIRTIO_F_RING_RESET and
//  - VIRTIO_F_INDIRECT_DESC
// We want:
//  - VIRTIO_BLK_F_BLK_SIZE and
//  - VIRTIO_BLK_F_TOPOLOGY.

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno)
{
    struct vioblk_device * vioblk;
    struct virtqueue;
    uint32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

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

    if (result != 0)
    {
        kprintf("%p: FAILED virtio feature negotiation\n", regs);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    // if the device provides a block size, use it. otherwise, use 512
    result = virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE);

    if (result > 0)
    {
        blksz = regs->config.blk.blk_size;
    }
    else
    {
        blksz = 512;
    }

    // blksz must be a power of two
    assert (((blksz - 1) & blksz) == 0);

    printf("vioblk has 0x%xsectors\n" conf->capacity);
	printf("vioblk queue_num_max %u\n", regs->queue_num_max);
	printf("vioblk status %x\n", regs->status);
	printf("vioblk interrupt_status %x\n", regs->interrupt_status);

    // attach virtqueue

    virtq = virtio_create_virtq(128);
    virtio_attach_virtq(regs, virtq, 0);
    virtio_enable_virtq(regs, 0);

    // initialize and attach vioblk device

    vioblk = kcalloc(1, sizeof(struct vioblk_device));

    if (vioblk == NULL)
    {
        kpritnf("ERROR: virtio block device failed to allocate memory\n");
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    vioblk->regs = regs;
    vioblk->irqno = irqno;
    vioblk->virtq = virtq
    vioblk->conf = regs->confg;
    vioblk->conf->blksz = blksz;
    condition_init(&vioblk->ready, "vioblk data ready");
    lock_init(&vioblk->vlock);

    static const struct iointf vioblk_iointf =
    {
        .close = &vioblk_close,
        .cntl = &vioblk_cntl,
        .readat = &vioblk_readat,
        .writeat = &vioblk_writeat,
    };

    ioinit0(&vioblk->io, &vioblk_iointf);
    vioblk->instno = register_device(VIOBLK_NAME, vioblk_open, vioblk);

    if (regs->queue_ready != 1)
    {
        kprintf("ERROR: queue %d not ready\n", 0);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    regs->status |= VIRTIO_STAT_DRIVER_OK;
}

int vioblk_open(struct io ** ioptr, void * aux)
{
    struct vioblk_device * const vioblk = aux;

    // register the vioblk interrupt handler
    enable_intr_source(vioblk->irqno, VIOBLK_INTR_PRIO, vioblk_isr, aux);
    *ioptr = ioaddref(&vioblk->io);

    return 0;
}

void vioblk_close(struct io * io)
{
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
    {
        return -EINVAL;
    }

    // read must be 512 byte aligned
    else if(pos % vioblk->blksz != 0 || bufsz % vioblk->blksz != 0)
    {
        return -EINVAL;
    }

    sector = pos / vioblk->blksz;
    read_bytes = 0;

    // read each block and copy to buf
    while (read_bytes < bufsz)
    {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > vioblk->capacity)
        {
            return -EACCESS;
        }

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
    {
        return -EINVAL;
    }
    // write must be 512 byte aligned
    else if (pos % vioblk->blksz != 0 ||
             len % vioblk->blksz != 0)
    {
        return -EINVAL;
    }

    sector = pos / vioblk->blksz;
    write_bytes = 0;

    while (write_bytes < len)
    {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > vioblk->capacity)
        {
            return -EACCESS;
        }

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

int vioblk_cntl(struct io * io, int cmd, void * arg)
{
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    size_t * szarg = arg;
    int result;

    switch (cmd)
    {
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

void vioblk_isr(int srcno, void * aux)
{
    struct vioblk_device * vioblk = aux;
    uint16_t used_idx;
    //uint32_t id;

    while (vioblk->virtq->last_used != vioblk->virtq->used->idx)
    {
        used_idx = vioblk->virtq->last_used % vioblk->virtq->len;
        //id = vioblk->virtq->used->ring[used_idx].id;

        virtio_free_virtq_desc(vioblk->virtq, req.)
    }

    if (vioblk->regs->interrupt_status & VIRTQ_INTR_USED)
    {
        vioblk->vq.last_used_idx = vioblk->vq.used.idx;
        vioblk->regs->interrupt_ack = VIRTQ_INTR_USED;
    }

    vioblk->virtq->last_used = vioblk->virtq->idx;
    condition_broadcast(&vioblk->block_ready);
    __sync_synchronize();
}

int request_block (
        struct vioblk_device * vioblk,
        uint32_t type,
        uint64_t sector,
        uint32_t queue,
        uint32_t queue_max
        char * buf)
{
    struct vioblk_request * req;
    uint32_t head, data, status;
    uint32_t write_flag;
    uint16_t avail_idx;
    int pie;

    // specify which block to read from
    req.type = type;
    req.sector = sector;

    head = virtio_alloc_virtq_desc(vioblk->virtq, req);
    vioblk->virtq->desc[head].len = VIRTIO_BLK_REQ_HEADER_SIZE;
    vioblk->virtq->desc[head].flags = VIRTQ_DESC_F_NEXT;

    write_flag = 0;

    // if trying to read make page writeble
    if (type == VIRTIO_BLK_T_IN)
    {
        write_flag = VIRTQ_DESC_F_WRITE;
    }

    data = virtio_alloc_virtq_desc(vioblk->virtq, buf);
    vioblk->virtq->desc[data].len = VIRTIO_BLK_SECTOR_SIZE;
    vioblk->virtq->desc[data].flags = write_flag | VIRTQ_DESC_F_NEXT;

    status = virtio_alloc_virtq_desc(vioblk->virtq, req + VIRTIO_BLK_REQ_HEADER_SIZE);
    vioblk->virtq->desc[status].len = VIRTIO_BLK_REQ_FOOTER_SIZE;
    vioblk->virtq->desc[status].flags = VIRTQ_DESC_F_WRITE;

    vioblk->virtq->desc[head].next = data;
    vioblk->virtq->desc[data].next = status;

    // put descriptors into available ring buffer
    avail_idx = vioblk->vq.avail.idx % queue_max;
    vioblk->vq.avail.ring[avail_idx] = head;
    __sync_synchronize();
    vioblk->vq.avail.idx++;
    __sync_synchronize();

    virtio_notify_avail(vioblk->regs, queue);

    return 0;
}
