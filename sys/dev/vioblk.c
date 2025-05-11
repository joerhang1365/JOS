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
#define VIRTIO_BLK_VIRTQ_LEN 3

 // INTERNAL TYPE DEFINITIONS
 //

struct vioblk_virtq
{
    uint16_t last_seen; // last used ring buffer serviced

    union
    {
        struct virtq_avail avail;
        char _avail_filler[VIRTQ_AVAIL_SIZE(VIRTIO_BLK_VIRTQ_LEN)];
    };

    union
    {
        struct virtq_used used;
        char _used_filler[VIRTQ_USED_SIZE(VIRTIO_BLK_VIRTQ_LEN)];
    };

    struct virtq_desc indirect_desc;
    struct virtq_desc desc_table[VIRTIO_BLK_VIRTQ_LEN];
};

struct vioblk_config
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

    uint32_t blk_size;
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
    uint32_t type;  // read, write, discard, etc...
    uint32_t reserved;
    uint64_t sector; // offset * block size where read or write is
    char data[VIRTIO_BLK_REQ_SECTOR_SIZE];
    uint8_t status;
};

struct vioblk_device
{
    volatile struct virtio_mmio_regs * regs;
    struct io io;
    int irqno;
    int instno;

    struct vioblk_virtq virtq;
    struct vioblk_config * conf;

    struct condition ready;
    struct lock lock;
};

// GLOBAL VARIABLE DECLARATIONS
//

// TODO: make per request buffers
// I can do this by having some pre allocated requests then
// cycling through them whenever something requests data
static struct vioblk_request req;

// INTERNAL FUNCTION DECLARATIONS
//

static int vioblk_open(struct io ** ioptr, void * aux);
static void vioblk_close(struct io * io);
static long vioblk_readat(struct io * io, unsigned long long pos, void * buf,
        long bufsz);
static long vioblk_writeat(struct io * io, unsigned long long pos,
        const void * buf, long len);
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

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno)
{
    struct vioblk_device * vioblk;
    struct vioblk_config * conf;
    uint32_t blk_size;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);
    kprintf("device id=%d", regs->device_id);

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
        kprintf("%p: failed virtio feature negotiation\n", regs);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    conf = (struct vioblk_config *)regs->config;

    // check if the device provides a block size
    // does not effect requests but will change read and write
    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
    {
        blk_size = conf->blk_size;
    }
    else
    {
        blk_size = 512;
    }

    // block size must be a power of two
    assert (((blk_size - 1) & blk_size) == 0);

    // initialize and attach vioblk device

    vioblk = kcalloc(1, sizeof(struct vioblk_device));
    assert (vioblk != NULL);

    vioblk->regs = regs;
    vioblk->irqno = irqno;
    vioblk->conf = conf;
    condition_init(&vioblk->ready, "virtio block ready");
    lock_init(&vioblk->lock);

    static const struct iointf vioblk_iointf =
    {
        .close = &vioblk_close,
        .cntl = &vioblk_cntl,
        .readat = &vioblk_readat,
        .writeat = &vioblk_writeat,
    };

    ioinit0(&vioblk->io, &vioblk_iointf);
    vioblk->instno = register_device(VIOBLK_NAME, vioblk_open, vioblk);

    kprintf("instance no=%d\n", vioblk->instno);
    kprintf("sectors=0x%x\n", vioblk->conf->capacity);
    kprintf("block size=%d\n", vioblk->conf->blk_size);
    kprintf("queue max=%u\n", regs->queue_num_max);

    // attach virtqueue

    vioblk->virtq.indirect_desc.addr = (uint64_t)&vioblk->virtq.desc_table[0];
    vioblk->virtq.indirect_desc.len = VIRTQ_DESC_SIZE * VIRTIO_BLK_VIRTQ_LEN;
    vioblk->virtq.indirect_desc.flags = VIRTQ_DESC_F_INDIRECT;
    vioblk->virtq.indirect_desc.next = 0;

    virtio_attach_virtq(regs, 0, 1, (uint64_t)&vioblk->virtq.indirect_desc,
        (uint64_t)&vioblk->virtq.used, (uint64_t)&vioblk->virtq.avail);
    virtio_enable_virtq(regs, 0);

    if (regs->queue_ready != 1)
    {
        kprintf("%p: failed queue %d not ready\n", regs, 0);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    regs->interrupt_ack = regs->interrupt_status;
    regs->status |= VIRTIO_STAT_DRIVER_OK;
}

int vioblk_open(struct io ** ioptr, void * aux)
{
    struct vioblk_device * const vioblk = aux;

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

long vioblk_readat(struct io * io, unsigned long long  pos, void * buf,
        long bufsz)
{
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    uint64_t capacity;
    uint32_t blk_size;
    uint64_t sector;
    uint64_t read_bytes;

    lock_acquire(&vioblk->lock);
    trace("%s(pos=%lld, bufsz=%ld)", __func__, pos, bufsz);

    capacity = vioblk->conf->capacity;
    blk_size = vioblk->conf->blk_size;

    if (bufsz < 0)
    {
        return -EINVAL;
    }
    else if (pos % blk_size != 0 || bufsz % blk_size != 0)
    {
        return -EINVAL;
    }

    sector = pos / blk_size;
    read_bytes = 0;

    while (read_bytes < bufsz)
    {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > capacity)
        {
            return -EACCESS;
        }

        request_block(vioblk, VIRTIO_BLK_T_IN, sector, 0, 1);
        memcpy(buf + read_bytes, req.data, blk_size);
        read_bytes += blk_size;

        debug("sector=%lld", sector);
        debug("read_bytes=%lld", read_bytes);

        sector++;
    }

    __sync_synchronize();
    lock_release(&vioblk->lock);

    return read_bytes;
}

long vioblk_writeat(struct io * io, unsigned long long pos, const void * buf,
        long len)
{
    struct vioblk_device * const vioblk =
        (void*)io - offsetof(struct vioblk_device, io);

    uint64_t capacity;
    uint32_t blk_size;
    uint64_t sector;
    uint64_t write_bytes;

    lock_acquire(&vioblk->lock);
    trace("%s(pos=%lld, len=%ld)", __func__, pos, len);

    capacity = vioblk->conf->capacity;
    blk_size = vioblk->conf->blk_size;

    if (len < 0)
    {
        return -EINVAL;
    }
    else if (pos % blk_size != 0 ||
             len % blk_size != 0)
    {
        return -EINVAL;
    }

    sector = pos / blk_size;
    write_bytes = 0;

    while (write_bytes < len)
    {
        // make sure we are not trying to read more than hard-drive capacity
        if (sector > capacity)
        {
            return -EACCESS;
        }

        memcpy(req.data, buf + write_bytes, blk_size);
        request_block(vioblk, VIRTIO_BLK_T_OUT, sector, 0, 1);
        write_bytes += blk_size;

        debug("sector=%lld", sector);
        debug ("write_bytes=%lld", write_bytes);

        sector++;
    }

    __sync_synchronize();
    lock_release(&vioblk->lock);

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
        result = vioblk->conf->blk_size;
        break;
    case IOCTL_GETEND:
        *szarg = vioblk->conf->capacity * vioblk->conf->blk_size;
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

    while (vioblk->virtq.last_seen != vioblk->virtq.used.idx)
    {
        vioblk->virtq.last_seen++;
    }

    vioblk->regs->interrupt_ack = vioblk->regs->interrupt_status;
    __sync_synchronize();
    condition_broadcast(&vioblk->ready);
}

// chain 3 descriptors
// TODO: Figure out why condition waiting breaks everything
// also implementing multiple virtqueues and larger chains

int request_block (
        struct vioblk_device * vioblk,
        uint32_t type,
        uint64_t sector,
        uint32_t queue,
        uint32_t queue_max)
{
    uint32_t head, data, status;
    uint16_t avail_idx;
    uint16_t write_flag;
    // int pie

    // specify read or write location

    req.type = type;
    req.sector = sector;

    // setup header descriptor

    head = 0;
    vioblk->virtq.desc_table[head].addr = (uint64_t)&req;
    vioblk->virtq.desc_table[head].len = VIRTIO_BLK_REQ_HEADER_SIZE;
    vioblk->virtq.desc_table[head].flags = VIRTQ_DESC_F_NEXT;

    // make the descriptor writeable if trying to read
    if (type == VIRTIO_BLK_T_IN)
    {
        write_flag = VIRTQ_DESC_F_WRITE;
    }
    else
    {
        write_flag = 0;
    }

    // setup data descriptor

    data = 1;
    vioblk->virtq.desc_table[data].addr = (uint64_t)req.data;
    vioblk->virtq.desc_table[data].len = VIRTIO_BLK_REQ_SECTOR_SIZE;
    vioblk->virtq.desc_table[data].flags = write_flag | VIRTQ_DESC_F_NEXT;

    // setup status descriptor

    status = 2;
    vioblk->virtq.desc_table[status].addr = (uint64_t)&req.status;
    vioblk->virtq.desc_table[status].len = VIRTIO_BLK_REQ_FOOTER_SIZE;
    vioblk->virtq.desc_table[status].flags = VIRTQ_DESC_F_WRITE;

    // link descriptors in a chain

    vioblk->virtq.desc_table[head].next = data;
    vioblk->virtq.desc_table[data].next = status;
    vioblk->virtq.desc_table[status].next = 0;

    // put descriptors into available ring buffer

    avail_idx = vioblk->virtq.avail.idx % queue_max;
    vioblk->virtq.avail.ring[avail_idx] = head;
    __sync_synchronize();
    vioblk->virtq.avail.idx++;
    __sync_synchronize();
    virtio_notify_avail(vioblk->regs, queue);

    // wait until data is ready to read or has written

    // pie = disable_interrupts();
    while (vioblk->virtq.avail.idx != vioblk->virtq.used.idx)
    {
        //condition_wait(&vioblk->ready);
        continue;
    }
    // restore_interrupts(pie);
    __sync_synchronize();

    if (req.status != VIRTIO_BLK_S_OK)
    {
        return -EIO;
    }

    return 0;
}
