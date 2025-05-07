// viohi.c
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

// INTERNAL CONSTANT DEFINITIONS

#ifndef VIOHI_NAME
#define VIOHI_NAME "input"
#endif

#ifndef VIOHI_IRQ_PRIO
#define VIOHI_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//

struct viohi_device
{
    volatile struct virtio_mmio_regs * regs;
    int irqno;
    int instno;

    struct io io;

    struct
    {
        uint16_t last_used_idx;

        union
        {
            struct virtq_avail avail;
            char
        }
    }

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

}
