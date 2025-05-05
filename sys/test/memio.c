#include "conf.h"
#include "assert.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "dev/uart.h"
#include "timer.h"
#include "io.h"
#include "console.h"



//#define buf_size 64
void main(void) {
	extern char _kimg_blob_start[];
	extern char _kimg_blob_end[];
	extern char _kimg_end[];

	thrmgr_init();

	heap_init(_kimg_end, RAM_END);
    char buf[64];
	
	uint64_t blob_size = ((uint64_t) &_kimg_blob_end - (uint64_t) &_kimg_blob_start);
	kprintf("%d\n", blob_size);
	kprintf("printing blob data\n");
	for (uint64_t l = 0; l < blob_size; l++) {
		kprintf("%c", _kimg_blob_start[l]);
	}
	kprintf("\n");
    
    struct io * mio = create_memory_io(&_kimg_blob_start, blob_size);

    ioreadat(mio, 0, buf, blob_size);

    for(int i = 0; i < blob_size; i++){
        kprintf("%d: %c\n", i, buf[i]);
    }
}
