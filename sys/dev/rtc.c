// rtc.c - Goldfish RTC driver
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "rtc.h"
#include "device.h"
#include "ioimpl.h"
#include "console.h"
#include "string.h"
#include "heap.h"

#include "error.h"

#include <stdint.h>


// INTERNAL TYPE DEFINITIONS
//

struct rtc_regs
{
    uint32_t low_time; // too fat need two reads
    uint32_t high_time;
};

struct rtc_device
{
    volatile struct rtc_regs* regs;
    struct io io;
    int instno;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int rtc_open(struct io ** ioptr, void * aux);
static void rtc_close(struct io * io);
static int rtc_cntl(struct io * io, int cmd, void * arg);
static long rtc_read(struct io * io, void * buf, long bufsz);

static uint64_t read_real_time(volatile struct rtc_regs * regs);

// EXPORTED FUNCTION DEFINITIONS
//

void rtc_attach(void * mmio_base)
{
    static const struct iointf rtc_iointf =
    {
        .close = &rtc_close,
        .cntl = &rtc_cntl,
        .read = &rtc_read
    }; // io interface

    struct rtc_device * rtc;

    rtc = kcalloc(1, sizeof(struct rtc_device));
    rtc->regs = mmio_base;
    ioinit0(&rtc->io, &rtc_iointf);
    rtc->instno = register_device("rtc", rtc_open, rtc);
}

int rtc_open(struct io ** ioptr, void * aux)
{
    struct rtc_device* rtc = aux;

    trace("%s()",__func__);
    *ioptr = ioaddref(&rtc->io); // increment the ref count
    return 0;
}

void rtc_close(struct io * io)
{
    trace("%s()",__func__);
    assert (iorefcnt(io) == 0);
}

int rtc_cntl(struct io * io, int cmd, void * arg)
{
    if(cmd == IOCTL_GETBLKSZ)
    {
        return 8;
    }
    else
    {
        return -ENOTSUP;
    }
}

long rtc_read(struct io * io, void * buf, long bufsz)
{
    struct rtc_device * rtc = (void*) io - offsetof(struct rtc_device, io);
    uint64_t time;

    trace("%s(bufsz=%ld)", __func__, bufsz);

    if(bufsz == 0)
    {
        return 0;
    }

    // if the bufsize is less than 64 bit return -EINVAL as
    // it is going to read enough
    if(bufsz < sizeof(uint64_t))
    {
        return -EINVAL;
    }

    time = read_real_time(rtc->regs);
    memcpy(buf, &time, sizeof(uint64_t));
    return sizeof(uint64_t);
}

uint64_t read_real_time(volatile struct rtc_regs * regs)
{
    // read low 32 bits first then high 32 bits
    uint32_t low = regs->low_time;
    uint32_t high = regs->high_time;

    // combine both reads to get 64 bit time
    return ((uint64_t) high << 32) + low;
}
