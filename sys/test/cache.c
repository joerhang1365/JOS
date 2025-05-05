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
#include "cache.h"

#define LOCK_ITER 5

void lock_test_fn(struct lock * test_lock, int iter);
void test_cache();

extern char _kimg_blob_start[];
extern char _kimg_blob_end[];

void main(void) {
    extern char _kimg_end[];
    int i;

    intrmgr_init();
    timer_init();
    devmgr_init();
    thrmgr_init();

    heap_init(_kimg_end, RAM_END);

    for (i = 0; i < 3; i++)
        uart_attach((void*)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));

    for (i = 0; i < 8; i++)
        virtio_attach((void*)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));

    test_cache();
}


void test_cache(){
    static struct cache * cache;
    unsigned long long len;
    // unsigned long long new_len;
    int result;
    kprintf("hello \n");
    // uint64_t blob_size = ((uint64_t) &_kimg_blob_end - (uint64_t) &_kimg_blob_start);

    // kprintf("Blob_size: %u \n", blob_size);
    // struct io * memio = create_memory_io(&_kimg_blob_start, blob_size);
    struct io * blkio;

    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    open_device("vioblk", 0, &blkio);
    create_cache(blkio, &cache);

    uint8_t arr[512];
    uint8_t buf[512];
    uint8_t num_blocks = 200;
    kprintf ("WRITE CACHE\n");
    for (uint8_t i = 0; i < num_blocks; i++) {
        //kprintf ("block %d\n", i);
        for (int j = 0; j < 512; j++) {
            arr[j] = i;
            //kprintf ("%d ", arr[j]);
        }
            //kprintf ("\n");


        cache_write_at(cache, i*512, arr, 512);
    }

    cache_flush(cache);

    kprintf ("READ CACHE\n");
    for (uint8_t i = 0; i < num_blocks; i++) {
        //kprintf ("block %d\n", i);
        cache_read_at(cache, i * 512, buf, 512);
        for (int j = 0; j < 512; j++) {
            assert(buf[j] == i);
            //kprintf ("%d ", buf[j]);
    }
    //kprintf ("\n");
  	}

    int blkno = 100;
    int blkoff = 500;
    len = 8;

    cache_write_at(cache, blkno * 512 + blkoff, buf, len);
    uint8_t buf2[512];

    cache_read_at(cache, blkno * 512, buf2, 512);
    kprintf("\n");
    for (int j = 0; j < 512; j++) {
        kprintf ("%d ", buf2[j]);
    }
    kprintf("\n");
    kprintf ("Cache test passed\n");
}
