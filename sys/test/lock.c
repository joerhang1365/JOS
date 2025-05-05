#include "conf.h"
#include "assert.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "dev/uart.h"
#include "timer.h"

#define LOCK_ITER 5

void lock_test_fn(struct lock * test_lock, int iter);

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

    enable_interrupts();

    struct lock test_lock;
    lock_init(&test_lock);

    int test1id = thread_spawn("test1", (void(*)(void)) &lock_test_fn, &test_lock, LOCK_ITER);
    assert (0 < test1id);

    int test2id = thread_spawn("test2", (void(*)(void)) &lock_test_fn, &test_lock, LOCK_ITER);
    assert (0 < test2id);

    thread_yield();

    thread_join(0);
}

void lock_test_fn(struct lock * test_lock, int iter)
{
    for (int i = 0; i < iter; i++)
    {
        lock_acquire(test_lock);
        kprintf("Thread %s has acquired test_lock %d times\n", running_thread_name(), test_lock->cnt);
        thread_yield();
    }

    for (int i = 0; i < iter; i++)
    {
        lock_release(test_lock);
        kprintf("Thread %s has released test_lock ", running_thread_name());
        kprintf("cnt = %d\n", test_lock->cnt);
        thread_yield();
    }
}
