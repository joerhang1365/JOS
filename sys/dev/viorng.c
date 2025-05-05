// viorng.c - VirtIO rng device
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "ioimpl.h"
#include "assert.h"
#include "conf.h"
#include "console.h"

// INTERNAL CONSTANT DEFINITIONS
//

#ifndef VIORNG_BUFSZ
#define VIORNG_BUFSZ 256
#endif

#ifndef VIORNG_NAME
#define VIORNG_NAME "rng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

#define VIRTQ_USED_F_NO_NOTIFY      1
#define VIRTQ_AVAIL_F_NO_INTERRUPT  1

// INTERNAL TYPE DEFINITIONS
//

struct viorng_device {
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

        // The first descriptor is a regular descriptor and is the one used in
        // the avail and used rings.

        struct virtq_desc desc[1];
    } vq;

    // bufcnt is the number of bytes left in buffer. The usable bytes are
    // between buf+0 and buf+bufcnt. (We read from the end of the buffer.)

    unsigned int bufcnt;
    char buf[VIORNG_BUFSZ];

    struct condition bytes_ready;
    struct lock vlock;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int viorng_open(struct io ** ioptr, void * aux);
static void viorng_close(struct io * io);
static long viorng_read(struct io * io, void * buf, long bufsz);
static void viorng_isr(int irqno, void * aux);

// EXPORTED FUNCTION DEFINITIONS
//

// Attaches a VirtIO rng device. Declared and called directly from virtio.c.

// void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno)
//
// this function initializes the VirtIO Entropy device with the necessary IO
// operation functions
// sets the required feature bits
// fills out the descriptors in the virtqueue struct
// attaches the virtq avail and virtq used structs using the virtio attach
// virtq function
// registers the device
//
// args: volatile struct virtio_mmio_regs * regs
//       int irqno
// return: void

void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    virtio_featset_t enabled_features, wanted_features, needed_features;
    int result;

    assert (regs->device_id == VIRTIO_ID_RNG);
    regs->status |= VIRTIO_STAT_DRIVER;

    // negotiate features
    // no mandatory features were specified

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);
    if (result != 0) {
        kprintf("%p: FAILED virtio feature negotiation\n", regs);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    // initialize and attach viorng device

    struct viorng_device * viorng;
    viorng = kcalloc(1, sizeof(struct viorng_device));
    assert (viorng != NULL);

    viorng->regs = regs;
    viorng->irqno = irqno;
    condition_init(&viorng->bytes_ready, "viorng_bytes_ready");
    lock_init(&viorng->vlock);

    static const struct iointf viorng_iointf = {
        .close = &viorng_close,
        .read = &viorng_read,
    };

    ioinit0(&viorng->io, &viorng_iointf);
    viorng->instno = register_device(VIORNG_NAME, viorng_open, viorng);

    // attach virtqueue

    regs->queue_sel = 0;
    regs->queue_num = 1;

    viorng->vq.desc[0].addr = (uint64_t)&viorng->buf;
    viorng->vq.desc[0].len = VIORNG_BUFSZ;
    viorng->vq.desc[0].flags = VIRTQ_DESC_F_WRITE;
    viorng->vq.desc[0].next = 0; // we aint doin indirect descriptors

    virtio_attach_virtq(regs, 0, 1, (uint64_t)&viorng->vq.desc,
            (uint64_t)&viorng->vq.used, (uint64_t)&viorng->vq.avail);
    virtio_enable_virtq(regs, 0);

    if (regs->queue_ready != 1) {
        kprintf("%p: FAILED queue %d not ready\n", regs, 0);
        regs->status |= VIRTIO_STAT_FAILED;
        return;
    }

    regs->status |= VIRTIO_STAT_DRIVER_OK;
}

// int viorng_open(struct io ** ioptr, void * aux)
//
// this function makes the virtq avail and virtq used queues available for use
// enables the interrupt source for the device with the ISR
// the IO operations are returned via ioptr
//
// args: struct io ** ioptr
//       void * aux
// return: int [returns 0 on success]

static int viorng_open(struct io ** ioptr, void * aux) {
    struct viorng_device * const viorng = aux;

    trace("%s()",__func__);

    enable_intr_source(viorng->irqno, VIORNG_INTR_PRIO, viorng_isr, aux);
    virtio_notify_avail(viorng->regs, 0);
    *ioptr = ioaddref(&viorng->io);

    return 0;
}

// void viorng_close(struct io * io)
//
// this function resets the virtq_avail and virtq_used queue and prevents
// further interrupts
//
// args: struct io * io
// return: void

static void viorng_close(struct io * io){
    trace("%s()",__func__);
    assert(io != NULL && iorefcnt(io) == 0);

    struct viorng_device * viorng =
        (void*)io - offsetof(struct viorng_device, io);

    disable_intr_source(viorng->irqno);
    virtio_reset_virtq(viorng->regs, 0);
}

// long viorng_read(struct io * io, void * buf, long bufsz)
//
// this function reads up to bufsz bytes from the VirtIO Entropy device and
// writes them to buf
// wait until the randomness has been placed into a buffer then writes that
// data out to buf
//
// args: struct io * io
//       void * buf
//       long bufsz
// return: long [number of bytes read]

static long viorng_read(struct io * io, void * buf, long bufsz) {
    struct viorng_device * const viorng =
        (void*)io - offsetof(struct viorng_device, io);

    long read_bytes;
    uint16_t avail_idx;
    uint16_t used_idx;
    // int pie;

    lock_acquire(&viorng->vlock);
    trace("%s(bufsz=%ld)",__func__,bufsz);

    if (bufsz < 0)
        return -EINVAL;

    if (bufsz > VIORNG_BUFSZ)
        bufsz = VIORNG_BUFSZ;

    read_bytes = 0;

    // get random bytes from viorng

    while (read_bytes < bufsz) {
        while (viorng->bufcnt > 0) {
            ((char*)buf)[read_bytes] =
                viorng->buf[viorng->bufcnt - 1];
            read_bytes++;
            viorng->bufcnt--;

            if (read_bytes >= bufsz)
                return read_bytes;
        }

        // request more random bytes from viorng

        avail_idx = viorng->vq.avail.idx % 1; // queue_num
        viorng->vq.avail.ring[avail_idx] = 0; // head
        viorng->vq.avail.idx++; // number of descriptors added
        virtio_notify_avail(viorng->regs, 0);

        // wait for random bytes
        // OPTIONAL: create hash to store random numbers then pull when
        // needed can also disable interrupts or something look at docs

        // pie = disable_interrupts();
        while (viorng->vq.avail.idx != viorng->vq.used.idx) {
            // condition_wait(&viorng->bytes_ready);
            continue;
        }

        // restore_interrupts(pie);
        __sync_synchronize(); // fence_o,io

        used_idx = viorng->vq.last_used_idx % 1;
        viorng->bufcnt = viorng->vq.used.ring[used_idx].len;
    }
    lock_release(&viorng->vlock);

    return read_bytes;
}

// void viorng_isr(int irqno, void * aux)
//
// this function handles interrupts for viorng
// checks the viorng interrupt status and updates the interrupt acknowledge
// register
// the two interrupt types are VIRTQ_INTR_USED and VIRTQ_INTR_CONF
// wakes up threads waiting on bytes from viorng
//
// args: int irqno
//       void * aux
// return: void

static void viorng_isr(int irqno, void * aux) {
    struct viorng_device * viorng = aux;

    // check if used buffer was updated
    if (viorng->regs->interrupt_status & VIRTQ_INTR_USED) {
        // let device know interrupt was handled
        viorng->regs->interrupt_ack = VIRTQ_INTR_USED;
        viorng->vq.last_used_idx++;

        condition_broadcast(&viorng->bytes_ready);
    }

    if (viorng->regs->interrupt_status & VIRTQ_INTR_CONF) {
        viorng->regs->interrupt_ack = VIRTQ_INTR_CONF;
    }

    __sync_synchronize(); // fence_o,io
}
