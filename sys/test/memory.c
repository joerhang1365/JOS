#include "conf.h"
#include "heap.h"
#include "console.h"
#include "assert.h"
#include "thread.h"
#include "io.h"
#include "device.h"
#include "dev/rtc.h"
#include "dev/uart.h"
#include "intr.h"
#include "dev/virtio.h"
#include "memory.h"
#include "string.h"
#include "fs.h"
#include "elf.h"

static void test_alloc_and_free();
static void test_mapping();
static void test_memory_validation();
static void test_clone_memory();

extern char _kimg_end[];

void main(void) {
    struct io * blkio;
    int result;
    int i;

    console_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    memory_init();

    for (i = 0; i < 3; i++)
        uart_attach((void*)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));

    for (i = 0; i < 8; i++)
        virtio_attach((void*)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));

    result = open_device("vioblk", 0, &blkio);
    assert (result == 0);

    //test_alloc_and_free();
    //test_mapping();
    //test_memory_validation();
    int num = 1;
    test_clone_memory();
}

void test_alloc_and_free() {
    void * pp1 = alloc_phys_pages(1);
    free_phys_page_count();
    free_phys_pages(pp1, 1);
    free_phys_page_count();

    void * pp10 = alloc_phys_pages(10);
    free_phys_page_count();
    pp1 = alloc_phys_pages(1);
    void * pp2 = alloc_phys_pages(2);
    free_phys_page_count();
    free_phys_pages(pp2, 2);
    free_phys_page_count();
    free_phys_pages(pp10, 10);
    free_phys_page_count();
    free_phys_pages(pp1, 1);
    free_phys_page_count();
}


void test_mapping() {
    // test single page
    void * pp1 = alloc_phys_pages(1);
    void * vp1 = map_page(UMEM_START_VMA, pp1, PTE_R | PTE_W | PTE_U);

    *((int *)vp1) = 42;
    int value = *((int *)vp1);

    kprintf("%d\n", value);
    assert(value == 42);

    // test range
    // alloc and map 10 pages
    void * vp10 = alloc_and_map_range(UMEM_START_VMA + 1 * PAGE_SIZE, 10 * PAGE_SIZE, PTE_R | PTE_W | PTE_U);

    for (uintptr_t i = 0; i < 10; i+=1) {
        *((int *)vp10 + i) = i;
        kprintf("%d\n", *((int *)vp10 + i));
    }

    // git rid of permissions
    //set_range_flags(vp, 10 * PAGE_SIZE, PTE_U);
    // should fault
    /*for (uintptr_t i = 0; i < 10; i += 1) {
        *((int *)vp) = i;
        kprintf("%d\n", *((int *)vp + i));
    }*/

    // test unmapping
    unmap_and_free_range(vp1, PAGE_SIZE);
    // should fault
    //*((int *)vp1) = 69;
    //value = *((int *)vp1);
    //kprintf("%d\n", value);

    unmap_and_free_range(vp10, 10 * PAGE_SIZE);

    unmap_and_free_range(vp1, 10 * PAGE_SIZE);


}

static void test_memory_validation(){
    int result;

    void * pp1 = alloc_phys_pages(1);
    void * vp1 = map_page(UMEM_START_VMA, pp1, PTE_R | PTE_W | PTE_U);

    int len = 9;
    char * string = "hello world";

    memcpy(vp1, string, 9);
    set_range_flags(vp1, PAGE_SIZE, PTE_R | PTE_U);

    result = memory_validate_vptr_len(vp1, len, PTE_R | PTE_W | PTE_U);
    if (result < 0)
        kprintf("test 1 failed\n");

    ((char *)vp1)[6] = 0;

    result = memory_validate_vstr(vp1, PTE_U);
    if (result < 0)
        kprintf("test 2 failed\n");

    void * pp2 = alloc_phys_pages(1);
    void * vp2 = map_page(UMEM_START_VMA + PAGE_SIZE, pp2, PTE_R | PTE_W | PTE_U);

    //(char *)vp2 = "a";
    //char * string2 = (char *)vp2;

    /*()result = memory_validate_vptr_len(vp1, 100, PTE_G | PTE_R | PTE_W | PTE_U);
    if (result < 0)
        kprintf("test 3 failed\n");

    result = memory_validate_vstr(vp1, PTE_U);
    if (result < 0)
        kprintf("test 4 failed\n");
*/
}

static void test_clone_memory() {
    void * pp1 = alloc_phys_pages(1);
    void * vp1 = map_page(UMEM_START_VMA, pp1, PTE_R | PTE_W | PTE_U);

    *((int *)vp1) = 42;
    int value = *((int *)vp1);

    //int cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);

    mtag_t new_space = clone_active_mspace();
    kprintf("active mspace=%p\n", active_mspace());
    kprintf("clone mspace=%p\n", (uintptr_t)new_space);
    //cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);


    mtag_t old_space = switch_mspace(new_space);

    *((int *)vp1) = 10;
    value = *((int *)vp1);

    kprintf("value=%d\n", value);

    void * pp2 = alloc_phys_pages(1);
    void * vp2 = map_page(UMEM_START_VMA + PAGE_SIZE, pp2, PTE_R | PTE_W | PTE_U);

    //cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);

    *((int *)vp2) = 22;
    value = *((int *)vp2);
    kprintf("value=%d\n", value);

    mtag_t another_one = clone_active_mspace();
    switch_mspace(another_one);

    //cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);


    *((int *)vp2) = 100;
    value = *((int *)vp2);

    kprintf("value=%d\n", value);

    switch_mspace(new_space);
    discard_active_mspace();
    switch_mspace(another_one);

    //cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);

     *((int *)vp2) = 40;
    value = *((int *)vp2);

    kprintf("value=%d\n", value);

    discard_active_mspace();

    //cnt = free_phys_page_count();
    //kprintf("cnt=%d\n", cnt);

}
