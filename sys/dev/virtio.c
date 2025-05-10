// virtio.c - MMIO-based VirtIO
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "virtio.h"
#include "assert.h"
#include "console.h"
#include "error.h"

#include <stddef.h>

#define VIRTIO_MAGIC 0x74726976

// EXPORTED FUNCTION DEFINITIONS
//

void virtio_attach(void * mmio_base, int irqno)
{
    volatile struct virtio_mmio_regs * const regs = mmio_base;

    extern void viocons_attach (
        volatile struct virtio_mmio_regs * regs, int irqno); // viocons.c

    extern void vioblk_attach (
        volatile struct virtio_mmio_regs * regs, int irqno); // vioblk.c

    extern void viorng_attach (
        volatile struct virtio_mmio_regs * regs, int irqno); // viorng.c

    extern void viogpu_attach (
        volatile struct virtio_mmio_regs * regs, int irqno); // viogpu.c

    extern void viohi_attach (
        volatile struct virtio_mmio_regs * regs, int irqno); // viohi.c

    if (regs->magic_value != VIRTIO_MAGIC)
    {
        kprintf("%p: No virtio magic number found\n", mmio_base);
        return;
    }

    if (regs->version != 2)
    {
        kprintf("%p: Unexpected virtio version (found %u, expected %u)\n",
            mmio_base, (unsigned int)regs->version, 2);
        return;
    }

    if (regs->device_id == VIRTIO_ID_NONE)
    {
        return;
    }

    regs->status = 0; // reset
    regs->status = VIRTIO_STAT_ACKNOWLEDGE;

    switch (regs->device_id)
    {
    case VIRTIO_ID_CONSOLE:
        debug("%p: Found virtio console device", regs);
        viocons_attach(regs, irqno);
        break;
    case VIRTIO_ID_BLOCK:
        debug("%p: Found virtio block device", regs);
        vioblk_attach(regs, irqno);
        break;
    case VIRTIO_ID_RNG:
        debug("%p: Found virtio rng device", regs);
        viorng_attach(regs, irqno);
        break;
    case VIRTIO_ID_GPU:
        debug("%p: Found virtio gpu device", regs);
        viogpu_attach(regs, irqno);
        break;
    case VIRTIO_ID_INPUT:
        debug("%p: Found virtio input device", regs);
        viohi_attach(regs, irqno);
        break;
    default:
        kprintf("%p: Unknown virtio device type %u ignored\n",
            mmio_base, (unsigned int) regs->device_id);
        return;
    }
}

int virtio_negotiate_features (
    volatile struct virtio_mmio_regs * regs,
    virtio_featset_t enabled,
    const virtio_featset_t wanted,
    const virtio_featset_t needed)
{
    uint_fast32_t i;

    // Check if all needed features are offered

    for (i = 0; i < VIRTIO_FEATLEN; i++)
    {
        if (needed[i] != 0)
        {
            regs->device_features_sel = i;
            __sync_synchronize(); // fence o,i
            if ((regs->device_features & needed[i]) != needed[i])
            {
                return -ENOTSUP;
            }
        }
    }

    // All required features are available. Now request the desired ones (which
    // should be a superset of the required ones).

    for (i = 0; i < VIRTIO_FEATLEN; i++)
    {
        if (wanted[i] != 0)
        {
            regs->device_features_sel = i;
            regs->driver_features_sel = i;
            __sync_synchronize(); // fence o,i
            enabled[i] = regs->device_features & wanted[i];
            regs->driver_features = enabled[i];
            __sync_synchronize(); // fence o,o
        }
    }

    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert (regs->status & VIRTIO_STAT_FEATURES_OK);

    return 0;
}

struct virtqueue * virtio_create_virtq(int len)
{
    struct virtqueue * virtq;
    uint32_t desc_size, avail_size, used_size, desc_virt_size;
    int i;

    // calcuate alignments

    desc_size = ALIGN(len * sizeof(struct virtq_desc), 4);
    avail_size = ALIGN(sizeof(struct virtq_avail) + len * sizeof(uint16_t), 4);
    used_size = ALIGN(sizeof(struct virtq_used) + len * sizeof(struct virtq_used_elem), 4);
    desc_virt_size = ALIGN(len * sizeof(void *))

    virtq = kcalloc(1, desc_size + avail_size + used_size + desc_virt_size);

    if (virtq == NULL)
    {
        kprintf("ERROR: virtqueue failed to allocate memory\n");
        return NULL;
    }

    virtq->len = len;

    virtq->desc = (struct virtq_desc *)((char *)virtq + sizeof(struct virtqueue));
    virtq->avail = (struct virtq_avail *)((char *)virtq->desc + desc_size);
    virtq->used = (struct virtq_used *)((char *)virtq->avail + avail_size);
    virtq->desc_virt = (void **)((char *)virtq->used + used_size);

    for (i = 0; i < len - 1; i++)
    {
        virtq->desc[i].next = i + 1;
    }

    virtq->desc[len - 1].next = 0;
    virtq->free_desc = 0;

    virtq->avail->idx = 0;
    virtq->used->idx = 0;
    virtq->seen_used = virtq->used->idx;

    return virtq;
}

void virtio_attach_virtq (
    volatile struct virtio_mmio_regs * regs, struct virtqueue * virtq, int qid)
{
    regs->queue_sel = qid;
    __sync_synchronize(); // fence o,o
    regs->queue_num = virtq->len;
    regs->queue_desc = vritq->desc;
    regs->queue_device = virtq->used;
    regs->queue_driver = virtq->avail;
    __sync_synchronize(); // fence o,o
}

int virtio_alloc_virtq_desc(struct virtqueue * virtq, void * addr)
{
    uint32_t desc = virtq->free_desc;
    uint32_t next = virtq->desc[desc].next;

    if (next == virtq->len)
    {
        kprintf("ERROR: ran out of virtqueue descriptors\n");
    }

    virtq->free_desc = next;
    virtq->desc[desc].addr = addr;
    virtq->desc_virt[desc] = addr;

    return desc;
}

void virtio_free_virtq_desc(struct virtqueue * virtq, int desc)
{
    virtq->desc[desc].next = virtq->free_desc;
    virtq->free_desc = desc;
    virtq->desc_virt[desc] = NULL;
}

// The following provide weak no-op attach functions that are overridden if the
// appropriate device driver is linked in.

void __attribute__ ((weak)) viocons_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    // nothing
}

void __attribute__ ((weak)) vioblk_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    // nothing
}

void __attribute__ ((weak)) viorng_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    // nothing
}

void __attribute__ ((weak)) viogpu_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    // nothing
}

void __attribute__ ((weak)) viohi_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    // nothing
}
