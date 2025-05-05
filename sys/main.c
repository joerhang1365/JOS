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

#define VIRTIO_MMIO_STEP (VIRTIO1_MMIO_BASE-VIRTIO0_MMIO_BASE)
#define INIT_NAME "trekfib"
#define NUM_UARTS 5

void interrupter();

void test_syscall();
void test_trek_cp2();
void test_zork();
void test_rogue();
void test_shit_shell();
void test_shell();
void test_doom();
void test_skyline();
void test_fib();
void test_trekfib();
void test_trek_wrapper();

extern char _kimg_end[];

void main(void) {
    struct io * blkio;
    int result;
    int i;

    console_init();
    memory_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    procmgr_init();
    timer_init();

    //uart_attach((void*)UART0_MMIO_BASE, UART0_INTR_SRCNO+0);
    //uart_attach((void*)UART1_MMIO_BASE, UART0_INTR_SRCNO+1);
    rtc_attach((void*)RTC_MMIO_BASE);

    for (i = 0; i < NUM_UARTS; i++)
        uart_attach((void *)UART_MMIO_BASE(i), UART0_INTR_SRCNO+i);

    for (i = 0; i < 8; i++) {
        virtio_attach ((void *)VIRTIO0_MMIO_BASE + i*VIRTIO_MMIO_STEP, VIRTIO0_INTR_SRCNO + i);
    }

    enable_interrupts();

    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    result = fsmount(blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to mount filesystem\n");
    }

    result = open_device("uart", 1, &current_process()->iotab[2]);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open UART\n");
    }

    //test_syscall();
    //test_trek_cp2();
    //test_zork();
    //test_rogue();
    //test_shit_shell();
    test_shell();
    //test_doom();
    //test_skyline();
    //test_fib();
    //test_trekfib();
    //test_trek_wrapper();
}

void test_syscall()
{
    struct io * syscallio;
    char print_string[] = "\n Asynchronous Grade: Passing \n";
    char * argv[2] = {0};
    int result;

    argv[0] = print_string;
    argv[1] = NULL;
    result = fsopen("syscall", &syscallio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open syscall test\n");
    }

    result = process_exec(syscallio, 1, (char **)argv);
}

void test_trek_cp2()
{
    struct io * trekio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = "trek_cp2";
    argv[1] = NULL;
    argv[2] = NULL;
    argc = 1;
    result = fsopen("trek_cp2", &trekio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open trek\n");
    }

    process_exec(trekio, argc, argv);
}

void test_zork()
{
    struct io * zorkio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = "zork";
    argv[1] = NULL;
    argv[2] = NULL;
    argc = 1;
    result = fsopen("zork", &zorkio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open zork");
    }

    process_exec(zorkio, argc, argv);
}

void test_rogue()
{
    struct io * rogueio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = "rogue";
    //argv[1] = "-r";
    //argv[2] = "rogue.sav";
    argv[1] = NULL;
    argc = 1;
    result = fsopen("rogue", &rogueio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open rogue");
    }

    process_exec(rogueio, argc, argv);
}

void test_shit_shell(){
    struct io * shellio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argc = 0;
    result = fsopen("shell.elf", &shellio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open shell");
    }

    process_exec(shellio, argc, argv);
}

void test_shell() {
    struct io * shellio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argc = 0;
    result = fsopen("shell", &shellio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open shell");
    }

    process_exec(shellio, argc, argv);
}


void test_doom() {
    struct io * doomio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = "doom";
    argv[1] = NULL;
    argc = 1;
    result = fsopen("doomlauncher", &doomio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open doom launcher");
    }

    process_exec(doomio, argc, argv);
}

void test_skyline() {
    struct io * skylineio;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argv[1] = NULL;
    argc = 0;
    result = fsopen("skyline", &skylineio);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open skyline");
    }

    process_exec(skylineio, argc, argv);
}

void test_fib() {
    struct io * io;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argc = 0;
    result = fsopen("fib", &io);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open fib");
    }

    process_exec(io, argc, argv);
}

void test_trekfib() {
    struct io * io;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argc = 0;
    result = fsopen("trekfib", &io);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open trekfib");
    }

    process_exec(io, argc, argv);
}

void test_trek_wrapper() {
    struct io * io;
    char * argv[3];
    int argc;
    int result;

    argv[0] = NULL;
    argc = 0;
    result = fsopen("trek_wrapper", &io);

    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open trek wrapper");
    }

    process_exec(io, argc, argv);
}


