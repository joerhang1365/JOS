// uart.c - NS8250-compatible uart port
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef UART_TRACE
#define TRACE
#endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "uart.h"
#include "device.h"
#include "intr.h"
#include "heap.h"
#include "ioimpl.h"
#include "console.h"
#include "thread.h"
#include "error.h"
#include <stdint.h>

// COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_NAME
#define UART_NAME "uart"
#endif

// INTERNAL TYPE DEFINITIONS
//

struct uart_regs
{
    union
    {
        char rbr;    // DLAB=0 read
        char thr;    // DLAB=0 write
        uint8_t dll; // DLAB=1
    };

    union
    {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };

    union
    {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

struct ringbuf
{
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

struct uart_device
{
    volatile struct uart_regs * regs;
    struct io io;
    int irqno;
    int instno;

    struct condition rxbuf_not_empty;
    struct condition txbuf_not_full;

    struct ringbuf rxbuf;
    struct ringbuf txbuf;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int uart_open(struct io ** ioptr, void * aux);
static void uart_close(struct io * io);
static long uart_read(struct io * io, void * buf, long bufsz);
static long uart_write(struct io * io, const void * buf, long len);

static void uart_isr(int srcno, void * driver_private);

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// EXPORTED FUNCTION DEFINITIONS
//

void uart_attach(void * mmio_base, int irqno)
{
    static const struct iointf uart_iointf =
    {
        .close = &uart_close,
        .read = &uart_read,
        .write = &uart_write
    };

    struct uart_device * uart;

    uart = kcalloc(1, sizeof(struct uart_device));
    uart->regs = mmio_base;
    uart->irqno = irqno;
    ioinit0(&uart->io, &uart_iointf);

    // Check if we're trying to attach UART0, which is used for the console. It
    // had already been initialized and should not be accessed as a normal
    // device.

    if (mmio_base != (void*)UART0_MMIO_BASE)
    {

        uart->regs->ier = 0;
        uart->regs->lcr = LCR_DLAB;
        // fence o,o ?
        uart->regs->dll = 0x01;
        uart->regs->dlm = 0x00;
        // fence o,o ?
        uart->regs->lcr = 0; // DLAB=0

        uart->instno = register_device(UART_NAME, uart_open, uart);

    }
    else
    {
        uart->instno = register_device(UART_NAME, NULL, NULL);
    }
}

int uart_open(struct io ** ioptr, void * aux)
{
    struct uart_device * const uart = aux;

    trace("%s()", __func__);

    if (iorefcnt(&uart->io) != 0)
    {
        return -EBUSY;
    }

    // reset receive and transmit buffers
    rbuf_init(&uart->rxbuf);
    rbuf_init(&uart->txbuf);
    condition_init(&uart->rxbuf_not_empty,"rxbuf_not_empty");
    condition_init(&uart->txbuf_not_full,"txbuf_not_full");

    // read receive buffer register to flush any stale data in hardware buffer
    uart->regs->rbr;

    // enable the interrupt source
    enable_intr_source(uart->irqno, UART_INTR_PRIO, uart_isr, aux);

    // get the io pointer to device
    *ioptr = ioaddref(&uart->io);

    // enable the interrupt for data ready
    uart->regs->ier = IER_DRIE;

    return 0;
}

void uart_close(struct io * io)
{
    trace("%s()", __func__);
    assert(io != NULL && iorefcnt(io) == 0);

    struct uart_device * const uart =
        (void*)io - offsetof(struct uart_device, io);

    disable_intr_source(uart->irqno);
}

long uart_read(struct io * io, void * buf, long bufsz)
{
    struct uart_device * const uart =
        (void*)io - offsetof(struct uart_device, io);

    long read_bytes;
    char * bufptr;
    char c;
    int pie;

    trace("%s(bufsz=%ld)",__func__,bufsz);

    if (bufsz < 0)
    {
        return -EINVAL;
    }
    else if (bufsz == 0)
    {
        return 0;
    }
    else if (bufsz > UART_RBUFSZ)
    {
        bufsz = UART_RBUFSZ;
    }

    // copy at most bufsz number of bytes from recieve ring buffer to buf

    pie = disable_interrupts();
    while (rbuf_empty(&uart->rxbuf))
    {
        condition_wait(&uart->rxbuf_not_empty);
    }

    restore_interrupts(pie);
    read_bytes = 0;
    bufptr = (char *)buf;

    do
    {
        c = rbuf_getc(&uart->rxbuf);
        *bufptr = c;
        bufptr++;
        read_bytes++;

        if (rbuf_empty(&uart->rxbuf))
        {
            break;
        }

    }
    while (read_bytes < bufsz);

    // enable data receive interrupt
    uart->regs->ier |= IER_DRIE;

    return read_bytes;
}

long uart_write(struct io * io, const void * buf, long len)
{
    struct uart_device * const uart =
        (void*)io - offsetof(struct uart_device, io);

    long write_bytes;
    char * bufptr;
    char c;
    int pie;

    if (len < 0)
    {
        return -EINVAL;
    }
    else if (len > UART_RBUFSZ)
    {
        len = UART_RBUFSZ;
    }

    write_bytes = 0;
    bufptr = (char *)buf;

    while (write_bytes < len)
    {
        pie = disable_interrupts();
        while (rbuf_full(&uart->txbuf))
        {
            condition_wait(&uart->txbuf_not_full);
        }

        restore_interrupts(pie);

        c = *bufptr;
        bufptr++;
        write_bytes++;

        rbuf_putc(&uart->txbuf, c);
    }

    // enable transmit holding register empty interrupt
    uart->regs->ier |= IER_THREIE;

    return len;
}

void uart_isr(int srcno, void * aux)
{
    struct uart_device* uart = aux;
    char c;

    // if the data ready bit is enabled, copy a character from
    // the RBR to the rxbuf
    if (uart->regs->lsr & LSR_DR)
    {
        c = uart->regs->rbr;
        if (!rbuf_full(&uart->rxbuf))
        {
            rbuf_putc(&uart->rxbuf, c);
        }
        else
        {
            uart->regs->ier &= ~IER_DRIE;
        }

        condition_broadcast(&uart->rxbuf_not_empty);
    }

    // if the THRE is enabled, copy a character from the tx buf to the THR
    if (uart->regs->lsr & LSR_THRE)
    {
        if (!rbuf_empty(&uart->txbuf))
        {
            uart->regs->thr = rbuf_getc(&uart->txbuf);
        }
        else
        {
            uart->regs->ier &= ~IER_THREIE;
        }

        condition_broadcast(&uart->txbuf_not_full);
    }
}

void rbuf_init(struct ringbuf * rbuf)
{
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}

int rbuf_empty(const struct ringbuf * rbuf)
{
    return (rbuf->hpos == rbuf->tpos);
}

int rbuf_full(const struct ringbuf * rbuf)
{
    return ((uint16_t)(rbuf->tpos - rbuf->hpos) == UART_RBUFSZ);
}

void rbuf_putc(struct ringbuf * rbuf, char c)
{
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf)
{
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}

// The functions below provide polled uart input and output for the console.

#define UART0 (*(volatile struct uart_regs*)UART0_MMIO_BASE)

void console_device_init(void)
{
    UART0.ier = 0x00;

    // Configure UART0. We set the baud rate divisor to 1, the lowest value,
    // for the fastest baud rate. In a physical system, the actual baud rate
    // depends on the attached oscillator frequency. In a virtualized system,
    // it doesn't matter.

    UART0.lcr = LCR_DLAB;
    UART0.dll = 0x01;
    UART0.dlm = 0x00;

    // The com0_putc and com0_getc functions assume DLAB=0.

    UART0.lcr = 0;
}

void console_device_putc(char c)
{
    // Spin until THR is empty
    while (!(UART0.lsr & LSR_THRE))
    {
        continue;
    }

    UART0.thr = c;
}

char console_device_getc(void)
{
    // Spin until RBR contains a byte
    while (!(UART0.lsr & LSR_DR))
    {
        continue;
    }

    return UART0.rbr;
}
