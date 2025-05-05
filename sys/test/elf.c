#include "conf.h"
#include "assert.h"
#include "console.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "dev/uart.h"
#include "timer.h"
#include "io.h"
#include "elf.h"


//#define buf_size 64
void main(void) {
	extern char _kimg_blob_start[];
	extern char _kimg_blob_end[];
	extern char _kimg_end[];

	console_init();
	intrmgr_init();
	timer_init();
	devmgr_init();
	thrmgr_init();

	heap_init(_kimg_end, RAM_END);
	
	uint64_t blob_size = ((uint64_t) &_kimg_blob_end - (uint64_t) &_kimg_blob_start);
	kprintf("%d\n", blob_size);
    
	for (int i = 0; i < 64; i++) {
		kprintf("%x ", _kimg_blob_start[i]);
	}
	kprintf("\n");

    struct io * mio = create_memory_io(&_kimg_blob_start, blob_size);
	char c;
	for (int i = 0; i < 128; i++) {
		ioreadat(mio, i, &c, 1);
		kprintf("%x ", c);
	}
	kprintf("\n");
	struct io * sio = create_seekable_io(mio);

	void (*exe_entry)(struct io *);
	elf_load(sio, (void (**)(void)) &exe_entry);
	char hello_name[10] = "hello";

	for (int i = 0; i < 3; i++)
		uart_attach((void*)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));

	enable_interrupts();
	struct io * termio;
	int result = open_device("uart", 1, &termio);
	assert(result == 0);

	thread_spawn((char *) &hello_name, (void (*)(void)) exe_entry, termio); 
	thread_join(0);
}
