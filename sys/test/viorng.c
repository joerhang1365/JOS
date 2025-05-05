// test/viorng.c - virtio rng tests
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "conf.h"
#include "assert.h"
#include "console.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "string.h"

void main(void) {
    extern char _kimg_end[];
    static unsigned long hist[256];
    static unsigned char rngbuf[1025];
    struct io * rngio;
    int result;
    long rcnt;
    int i, j;

    intrmgr_init();
    devmgr_init();
    thrmgr_init();

    heap_init(_kimg_end, RAM_END);

    for (i = 0; i < 8; i++)
        virtio_attach((void*)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));

    result = open_device("rng", 0, &rngio);
    assert (result == 0);

    enable_interrupts();

    memset(hist, 0, sizeof(hist));

    for (j = 0; j < 100; j++) {
        memset(rngbuf, 0, sizeof(rngbuf));
        rcnt = ioread(rngio, rngbuf, sizeof(rngbuf));
        assert (rcnt > 0);

        for (i = 0; i < rcnt; i++)
            hist[rngbuf[i]] += 1;
    }

    for (i = 0; i < 256; i++)
        kprintf("%lu\n", hist[i]);
}
