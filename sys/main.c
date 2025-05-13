#include "conf.h"
#include "heap.h"
#include "console.h"
#include "elf.h"
#include "assert.h"
#include "thread.h"
#include "fs.h"
#include "io.h"
#include "device.h"
#include "dev/rtc.h"
#include "dev/uart.h"
#include "intr.h"
#include "dev/virtio.h"
#include "memory.h"
#include "process.h"
#include "timer.h"

#define NUM_UARTS 5

extern char _kimg_end[];

void main(void)
{
    struct io * blkio;
    struct io * initio;
    int result;
    int i;

    console_init();
    memory_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    procmgr_init();
    timer_init();

    rtc_attach((void*)RTC_MMIO_BASE);

    for (i = 0; i < NUM_UARTS; i++)
    {
        uart_attach((void *)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));
    }

    for (i = 0; i < 8; i++)
    {
        virtio_attach ((void *)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));
    }

    enable_interrupts();

    result = open_device("vioblk", 0, &blkio);
    if (result < 0)
    {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    result = fsmount(blkio);
    if (result < 0)
    {
        kprintf("Error: %d\n", result);
        panic("Failed to mount filesystem\n");
    }

    result = fsopen("init", &initio);
    if (result < 0)
    {
        kprintf("Error: %d\n", result);
        panic("Failed to open init file\n");
    }
    process_exec(initio, 0, NULL);
}
