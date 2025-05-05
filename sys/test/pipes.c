#include "conf.h"
#include "assert.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "dev/uart.h"
#include "timer.h"
#include "ktfs.h"
#include "io.h"
#include "fs.h"
#include "string.h"
#include "process.h"
#include "memory.h"

#define LOCK_ITER 5

void lock_test_fn(struct lock * test_lock, int iter);
void test_pipes();
void test_open_files();
void test_open_files2();


extern char _kimg_blob_start[];
extern char _kimg_blob_end[];
void main(void) {
    // extern char _kimg_end[];
    int i;

    console_init();
    memory_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    procmgr_init();

    //heap_init(_kimg_end, RAM_END);

    for (i = 0; i < 3; i++)
        uart_attach((void*)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));

    for (i = 0; i < 8; i++)
        virtio_attach((void*)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));

    //test_ktfs();
    //test_open_files2();
    test_pipes();


}

void test_pipes(){

    struct io * wioptr;
    struct io * rioptr;
    struct io * rioptr2;
    

    create_pipe(&wioptr, &rioptr);

    char test[] = "hello my name is jeff";
    int len = sizeof(test);

    char buf[512];
    char buf2[512];

    //ioclose(rioptr);

    kprintf("IOwrite: %d \n", iowrite(wioptr, test, len));

    //ioclose(wioptr);

    kprintf("IOREAD: %d \n", ioread(rioptr, buf, 11));

    rioptr2 = ioaddref(rioptr);

    kprintf("IOREAD: %d \n", ioread(rioptr2, buf2, 11));

    for(int i = 0; i < len; i++){
        kprintf("%c", buf[i]);
    }
    kprintf("\n");

    for(int i = 0; i < len; i++){
        kprintf("%c", buf2[i]);
    }
    kprintf("\n");



    
    

}