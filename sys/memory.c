// memory.c - Physical and virtual memory manager
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef MEMORY_TRACE
#define TRACE
#endif

#ifdef MEMORY_DEBUG
#define DEBUG
#endif

#include "memory.h"
#include "conf.h"
#include "riscv.h"
#include "heap.h"
#include "console.h"
#include "assert.h"
#include "string.h"
#include "thread.h"
#include "process.h"
#include "error.h"

// COMPILE-TIME CONFIGURATION
//

// Minimum amount of memory in the initial heap block.

#ifndef HEAP_INIT_MIN
#define HEAP_INIT_MIN 256
#endif

// INTERNAL CONSTANT DEFINITIONS
//

#define MEGA_SIZE ((1UL << 9) * PAGE_SIZE) // megapage size
#define GIGA_SIZE ((1UL << 9) * MEGA_SIZE) // gigapage size

#define PTE_ORDER 3
#define PTE_CNT (1U << (PAGE_ORDER - PTE_ORDER))

#ifndef PAGING_MODE
#define PAGING_MODE RISCV_SATP_MODE_Sv39
#endif

#ifndef ROOT_LEVEL
#define ROOT_LEVEL 2
#endif

// IMPORTED GLOBAL SYMBOLS
//

// linker-provided (kernel.ld)
extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// EXPORTED GLOBAL VARIABLES
//

char memory_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

// We keep free physical pages in a linked list of _chunks_, where each chunk
// consists of several consecutive pages of memory. Initially, all free pages
// are in a single large chunk. To allocate a block of pages, we break up the
// smallest chunk on the list.

struct page_chunk
{
    struct page_chunk * next;   // next page in list
    unsigned long pagecnt;      // number of pages in chunk
};

// The Page Table Entry
// index | flags   | description
// ============================
// 0     | V=VALID | V=1 PTE is vaild, V=0 PTE not valid causes page fault excp
// 1     | R=READ  | R=1 page is readable (can load from it)
// 2     | W=WRITE | W=1 page is writable (can store to it)
// 3     | X=EXEC  | X=1 executable (can fetch instructions from it)
// 4     | U=USER  | U=1 accessible in user mode, U=0 accessible in super. mode
// 5     | G=GLOBAL| G=1 mapping present in all address space
// 6     | A=ACCESS| A=1 page was accessed (read or fetched from)
// 7     | D=DIRTY | D=1 page was written to

struct pte
{
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN(vma) ((vma) / PAGE_SIZE)
#define VPN2(vma) ((VPN(vma) >> (2*9)) % PTE_CNT)
#define VPN1(vma) ((VPN(vma) >> (1*9)) % PTE_CNT)
#define VPN0(vma) ((VPN(vma) >> (0*9)) % PTE_CNT)
#define MIN(a,b) (((a)<(b))?(a):(b))

// The following macros test is a PTE is valid, global, or a leaf. The argument
// is a struct pte (*not* a pointer to a struct pte).

#define PTE_VALID(pte) (((pte).flags & PTE_V) != 0)
#define PTE_GLOBAL(pte) (((pte).flags & PTE_G) != 0)
#define PTE_LEAF(pte) (((pte).flags & (PTE_R | PTE_W | PTE_X)) != 0)

// INTERNAL FUNCTION DECLARATIONS
//

static inline mtag_t active_space_mtag(void);
static inline mtag_t ptab_to_mtag(struct pte * root, unsigned int asid);
static inline struct pte * mtag_to_ptab(mtag_t mtag);
static inline struct pte * active_space_ptab(void);

static inline void * pageptr(uintptr_t n);
static inline uintptr_t pagenum(const void * p);
static inline int wellformed(uintptr_t vma);

static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static struct pte * walk_and_alloc_pte(mtag_t mspace, uintptr_t vma);
static struct pte * walk_pte(mtag_t mspace, uintptr_t vma);

// INTERNAL GLOBAL VARIABLES
//

static mtag_t main_mtag;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

static struct page_chunk * free_chunk_list;

// EXPORTED FUNCTION DECLARATIONS
//

void memory_init(void)
{
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;

    void * heap_start;
    void * heap_end;

    uintptr_t pma;      // physical memory address
    const void * pp;    // physical memory pointer

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("RAM           : [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("Kernel image  : [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)

    if (MEGA_SIZE < _kimg_end - _kimg_start)
    {
        panic(NULL);
    }

    // Initialize main page table with the following direct mapping:
    //
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB

    // Identity mapping of MMIO region as two gigapage mappings
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
    {
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    }

    // Third gigarange has a second-level subtable
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] = ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE)
    {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE)
    {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE)
    {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE)
    {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging; this part always makes me nervous.

    main_mtag = ptab_to_mtag(main_pt2, 0);
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = (void*)ROUND_UP((uintptr_t)heap_start, PAGE_SIZE);

    if (heap_end - heap_start < HEAP_INIT_MIN)
    {
        heap_end += ROUND_UP (HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
    {
        panic("out of memory");
    }

    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    // Initialize the free chunk

    free_chunk_list = (struct page_chunk *) heap_end;
    free_chunk_list->pagecnt = (RAM_END - heap_end) / PAGE_SIZE;
    free_chunk_list->next = NULL;

    debug("INITIALIZING free chunk list: pp=%p, pages=%d",
             free_chunk_list, free_chunk_list->pagecnt);

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling supervisor access to user memory when we are explicitly trying
    // to access user memory, and disable it at other times. This would catch
    // bugs that cause inadvertent access to user memory (due to bugs).

    csrs_sstatus(RISCV_SSTATUS_SUM);
    memory_initialized = 1;
}

// Gets the active memory space
mtag_t active_mspace(void)
{
    return active_space_mtag();
}

// Switches the active memory space by writing the satp register.
mtag_t switch_mspace(mtag_t mtag)
{
    mtag_t prev;

    prev = csrrw_satp(mtag);
    sfence_vma();
    return prev;
}

// Copies all pages and page tables from the active memory space into
// newly allocated memory. Only does 4KB pages could do 2MB or GB
// but user space is only 4KB pages right now
mtag_t clone_active_mspace(void)
{
    mtag_t clone_mspace;

    uintptr_t vma;

    struct pte * og_pt2;
    struct pte * og_pte;
    struct pte * clone_pt2;
    struct pte * clone_pte;

    void * og_pp;
    void * clone_pp;

    trace("%s()", __func__);

    // shallow copy global page table entries

    og_pt2 = active_space_ptab();
    clone_pt2 = alloc_phys_pages(1);
    memset(clone_pt2, 0, PAGE_SIZE);

    for (int i = 0; i < PTE_CNT; i++)
    {
        if (PTE_VALID(og_pt2[i]) && PTE_GLOBAL(og_pt2[i]))
        {
            clone_pt2[i] = og_pt2[i];
        }
    }

    // deep copy user space page table entries and data

    clone_mspace = ptab_to_mtag(clone_pt2, 0);

    for (vma = UMEM_START_VMA; vma < UMEM_END_VMA; vma += PAGE_SIZE)
    {
        og_pte = walk_pte(active_mspace(), vma);

        if (PTE_VALID(*og_pte) && !PTE_GLOBAL(*og_pte))
        {
            // copy page
            og_pp = pageptr(og_pte->ppn);
            clone_pp = alloc_phys_pages(1);
            memset(clone_pp, 0, PAGE_SIZE);
            memcpy(clone_pp, og_pp, PAGE_SIZE);

            // map clone
            clone_pte = walk_and_alloc_pte(clone_mspace, vma);
            *clone_pte = leaf_pte(clone_pp, og_pte->flags);
        }
    }

    return clone_mspace;
}

// Unmaps and frees all non-global pages from the active memory space.
void reset_active_mspace(void)
{
    void * vp = UMEM_START;
    size_t size = UMEM_END_VMA - UMEM_START_VMA;

    unmap_and_free_range(vp, size);
}

// Switches memory spaces to main, unmaps and frees all non-global pages
// from the previously active memory space.
mtag_t discard_active_mspace(void)
{
    reset_active_mspace();
    switch_mspace(main_mtag);
    return main_mtag;
}

static struct pte * walk_pte(mtag_t mspace, uintptr_t vma)
{
    struct pte * pt2;
    struct pte * pt1;
    struct pte * pt0;
    struct pte * pte;
    struct pte null;

    trace("%s(mspace=%p, vma=%p)", __func__, mspace, vma);
    assert (wellformed(vma));
    assert ((uintptr_t)vma % PAGE_SIZE == 0);

    pt2 = mtag_to_ptab(mspace);
    null = null_pte();
    pte = &null;

    if (!PTE_VALID(pt2[VPN2(vma)]))
    {
        return pte;
    }

    pt1 = (struct pte *)pageptr(pt2[VPN2(vma)].ppn);

    if (!PTE_VALID(pt1[VPN1(vma)]))
    {
        return pte;
    }

    pt0 = (struct pte *)pageptr(pt1[VPN1(vma)].ppn);
    pte = &pt0[VPN0(vma)];

    return pte;
}


// gets page table pointer by translating virtual memory address
// does all the heavy lifting fr
static struct pte * walk_and_alloc_pte(mtag_t mspace, uintptr_t vma)
{
    struct pte * pt2;
    struct pte * pt1;
    struct pte * pt0;
    struct pte * pte;

    void * pp;

    trace("%s(mspace=%p, vma=%p)", __func__, mspace, vma);
    assert (wellformed(vma));
    assert ((uintptr_t)vma % PAGE_SIZE == 0);

    pt2 = mtag_to_ptab(mspace);

    // check if page table 2 has a valid entry to page table 1
    // if not allocate and map new entry
    if (!PTE_VALID(pt2[VPN2(vma)]))
    {
        pp = alloc_phys_pages(1);
        memset(pp, 0, PAGE_SIZE);
        pt2[VPN2(vma)] = ptab_pte(pp, 0);
    }

    pt1 = (struct pte *)pageptr(pt2[VPN2(vma)].ppn);

    // check if page table 1 has a valid entry to page table 0
    if (!PTE_VALID(pt1[VPN1(vma)]))
    {
        pp = alloc_phys_pages(1);
        memset(pp, 0, PAGE_SIZE);
        pt1[VPN1(vma)] = ptab_pte(pp, 0);
    }

    pt0 = (struct pte *)pageptr(pt1[VPN1(vma)].ppn);
    pte = &pt0[VPN0(vma)];

    return pte;
}

// The map_page() function maps a single page into the active address space at
// the specified address. The map_range() function maps a range of contiguous
// pages into the active address space. Note that map_page() is a special case
// of map_range(), so it can be implemented by calling map_range(). Or
// map_range() can be implemented by calling map_page() for each page in the
// range. The current implementation does the latter.

// We currently map 4K pages only. At some point it may be disirable to support
// mapping megapages and gigapages.

void * map_page(uintptr_t vma, void * pp, int rwxug_flags)
{
    struct pte * pte;

    trace("%s(vma=%p, pp=%p, flags=%x)",
            __func__, vma, pp, rwxug_flags);
    assert (wellformed(vma));
    assert (vma % PAGE_SIZE == 0);
    assert ((uintptr_t)pp % PAGE_SIZE == 0);

    pte = walk_and_alloc_pte(active_mspace(), vma);
    *pte = leaf_pte(pp, rwxug_flags);

    sfence_vma();

    return (void *)vma;
}

void * map_range(uintptr_t vma, size_t size, void * pp, int rwxug_flags)
{
    uintptr_t offset;

    trace("%s(vma=%p, size=%ld, pp=%p, flags=%x)",
            __func__, vma, size, pp, rwxug_flags);

    size = ROUND_UP(size, PAGE_SIZE);

    for (offset = 0; offset < size; offset += PAGE_SIZE)
    {
        map_page(vma + offset, pp + offset, rwxug_flags);
    }

    sfence_vma();

    return (void *)vma;
}

// Allocates memory for and maps a range of pages starting at provided virtual
// memory address. Rounds up size to be a multiple of PAGE_SIZE.
void * alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags)
{
    uintptr_t vptr; // virtual pointer
    void * pp;      // physical pointer

    trace("%s(vma=%p, size=%zu, flags=%x)",
          __func__, vma, size, rwxug_flags);

    size = ROUND_UP(size, PAGE_SIZE);

    for (vptr = vma; vptr < vma + size; vptr += PAGE_SIZE)
    {
        pp = alloc_phys_pages(1);
        memset(pp, 0, PAGE_SIZE);
        map_page(vptr, pp, rwxug_flags);
    }

    return (void *)vma;
}

// Sets passed flags for pages in range. Rounds up size to be a
// multiple of PAGE_SIZE.
void set_range_flags(const void * vp, size_t size, int rwxug_flags)
{
    uintptr_t vma;
    struct pte * pte;

    trace("%s(vp=%p, size=%zu, flags=%x)",
            __func__, vp, size, rwxug_flags);

    vma = (uintptr_t)vp;
    assert (vma % PAGE_SIZE == 0);
    size = ROUND_UP(size, PAGE_SIZE);

    while (vma < (uintptr_t)vp + size)
    {
        pte = walk_pte(active_mspace(), vma);
        vma += PAGE_SIZE;

        if (PTE_VALID(*pte) && !PTE_GLOBAL(*pte))
        {
            pte->flags = rwxug_flags | PTE_A | PTE_D | PTE_V;
        }
    }

    sfence_vma();
}

// Unmaps a range of pages starting at provided virtual memory address
// and frees the pages. Rounds up size to be a multiple of PAGE_SIZE.
void unmap_and_free_range(void * vp, size_t size)
{
    uintptr_t vma;
    struct pte * pte;
    void * pp;

    trace("%s(vp=%p, size=%zu)", __func__, vp, size);

    vma = (uintptr_t)vp;
    assert (vma % PAGE_SIZE == 0);
    size = ROUND_UP(size, PAGE_SIZE);

    while (vma < (uintptr_t)vp + size)
    {
        pte = walk_pte(active_mspace(), vma);
        vma += PAGE_SIZE;

        if (PTE_VALID(*pte) && !PTE_GLOBAL(*pte))
        {
            pp = pageptr(pte->ppn);
            free_phys_pages(pp, 1);
            *pte = null_pte();
        }
    }

    // TODO: could add check to see if page table 1 has no more entrys
    // then unmap and free but idk if it matters

    sfence_vma();
}

void * alloc_phys_page(void)
{
    return alloc_phys_pages(1);
}

void free_phys_page(void * pp)
{
    free_phys_pages(pp, 1);
}

// Allocates the passed number of physical pages from the free chunk list.
//
// Finds best chunk that fits the requested number of pages. If chunk
// exactly matches the number of pages requested, removes chunk from free
// chunk list, otherwise breaks off the component of chunk that matches
// requested number of pages. Panics if no chunk can be found that satisfies
// the request.
//
// Basically best fit helps with fragmentation but is slower
// an alternate method would be first fit that gets the first largest chunk
// This is super fast but creates fragmentation. In order to counteract this
// would need to implement coalescing that merges adjacent free chunks together
// this is slow
//
// coud also do the buddy allocator but fuck that
void * alloc_phys_pages(unsigned int cnt)
{
    struct page_chunk * current;
    struct page_chunk * prev;
    struct page_chunk * best;
    struct page_chunk * prev_best;
    struct page_chunk dummy;

    trace("%s(cnt=%d)", __func__, cnt);

    if (free_chunk_list == NULL)
    {
        panic("FATAL: out of free memory");
    }

    current = free_chunk_list;
    prev = NULL;
    dummy.pagecnt = UINT64_MAX;
    best = &dummy;

    // search for smallest chunk with required amount of pages

    while (current != NULL)
    {
        if (current->pagecnt >= cnt &&
            current->pagecnt <= best->pagecnt)
        {
            best = current;
            prev_best = prev;

            if (best->pagecnt == cnt)
            {
                break;
            }
        }

        prev = current;
        current = current->next;
    }

    if (best == &dummy)
    {
        panic("FATAL: could not find free pages");
    }

    debug("found chunk: pp=%p, pages=%d", best, best->pagecnt);

    unsigned int pages_left = best->pagecnt - cnt;
    debug("pages left=%d", pages_left);

    // if best chunk matches number of pages just return that chunk
    // and remove from the free chunk list

    if (pages_left == 0)
    {
        if (prev_best == NULL)
        {
            free_chunk_list = best->next;
        }

        if (prev_best != NULL)
        {
            prev_best->next = best->next;
        }

        return (void *)best;
    }

    // otherwise split chunk by allocating lowest address

    struct page_chunk * allocated;
    struct page_chunk * remaining;

    allocated = (struct page_chunk *)((uintptr_t)best + pages_left * PAGE_SIZE);
    remaining = best;
    remaining->pagecnt = pages_left;

    debug("allocated pp=%p", allocated);
    debug("remaining pp=%p", remaining);

    return (void *)allocated;
}

// Adds chunk consisting of passed count of pages at passed pointer back to
// free chunk list
// Every free chunk in the list is sorted by lowest to highest physical address
// Check next and previous chunks on free chunk list to see if they border
// the newly freed chunk. If so merge the two chunks to create BIGGER chunk
void free_phys_pages(void * pp, unsigned int cnt)
{
    struct page_chunk * new;
    struct page_chunk * target;
    struct page_chunk * prev;
    uintptr_t prev_end;
    uintptr_t new_end;

    trace("%s(pp=%p, pages=%d)", __func__,  pp, cnt);
    assert ((uintptr_t)pp % PAGE_SIZE == 0);

    new = (struct page_chunk *)pp;
    new->pagecnt = cnt;
    target = free_chunk_list;
    prev = NULL;

    // find insert location sorted by memory address
    while (target != NULL && target < new)
    {
        prev = target;
        target = target->next;
    }

    new->next = target;

    // insert new chunk into free chunk list
    // check if we can merge with previous free chunk
    if (prev != NULL)
    {
        prev->next = new;
        prev_end = (uintptr_t)prev + prev->pagecnt * PAGE_SIZE;
        debug("previous chunk end pma=%p", prev_end);

        if (prev_end == (uintptr_t)new)
        {
            debug("merging previous free chunk");
            prev->pagecnt += new->pagecnt;
            prev->next = new->next;
            new = prev;
        }
    }
    else
    {
        free_chunk_list = new;
    }

    // check if we can merge with next free chunk
    if (target != NULL)
    {
        new_end = (uintptr_t)new + new->pagecnt * PAGE_SIZE;
        debug("new chunk end pma=%p", new_end);

        if (new_end == (uintptr_t)new->next)
        {
            debug("merging next free chunk");
            new->pagecnt += new->next->pagecnt;
            new->next = new->next->next;
        }
    }
 }

// Counts the number of pages remaining in the free chunk list.
unsigned long free_phys_page_count(void)
{
    struct page_chunk * target;
    uint64_t cnt;

    trace("%s()", __func__);

    target = free_chunk_list;
    cnt = 0;

    while (target != NULL)
    {
        debug("chunk: pp=%p, pages=%d", target, target->pagecnt);
        cnt += target->pagecnt;
        target = target->next;
    }

    return cnt;
}

// Called by handle_umode_exception() in excp.c to handle U mode load and
// store page faults. It returns 1 to indicate the fault has been handled
// (the instruction should be restarted) and 0 to indicate that the page
// fault is fatal and the process should be terminated.
int handle_umode_page_fault(struct trap_frame * tfr, uintptr_t vma)
{
    struct pte * pte;
    uint32_t cause;

    trace("%s(vma=%p)", vma);

    if (vma < UMEM_START_VMA || vma >= UMEM_END_VMA)
    {
        kprintf("Error: trying to access memory outside of user space\n");
        return 0;
    }

    vma = ROUND_DOWN(vma, PAGE_SIZE);
    pte = walk_pte(active_mspace(), vma);

    if (PTE_VALID(*pte))
    {
        cause = csrr_scause();

        switch (cause)
        {
        case RISCV_SCAUSE_LOAD_PAGE_FAULT:
            if (((*pte).flags & PTE_R) == 0)
            {
                kprintf("ERROR: invalid read permissions\n");
            }
        case RISCV_SCAUSE_STORE_PAGE_FAULT:
            if (((*pte).flags & PTE_W) == 0)
            {
                kprintf("ERROR: invalid write permissions\n");
            }
        default:
            kprintf("ERROR: page table already mapped\n");
        }

        return 0;
    }

    // lazy load page
    alloc_and_map_range(vma, PAGE_SIZE, PTE_U | PTE_R | PTE_W);

    return 1;
}

int memory_validate_vptr_len (
        const void * vp, size_t len,
        uint_fast8_t rwxug_flags)
{
    uintptr_t vma;
    uintptr_t offset;
    struct pte * pte;

    if (vp == NULL)
    {
        return -EINVAL;
    }

    vma = ROUND_DOWN((uintptr_t)vp, PAGE_SIZE);
    offset = 0;

    while (offset < (uintptr_t)len)
    {
        pte = walk_pte(active_mspace(), vma + offset);
        offset += PAGE_SIZE;

        if (!PTE_VALID(*pte) ||
            (pte->flags & rwxug_flags) != rwxug_flags)
        {
            return -EACCESS;
        }
    }

    return 0;
}

int memory_validate_vstr (
        const char * vs, uint_fast8_t ug_flags)
{
    const char * p;
    uintptr_t vma;
    struct pte * pte;

    if (vs == NULL)
    {
        return -EINVAL;
    }

    p = vs;

    vma = ROUND_DOWN((uintptr_t)p, PAGE_SIZE);
    pte = walk_pte(active_mspace(), vma);

    if (!PTE_VALID(*pte) ||
        (pte->flags & ug_flags) != ug_flags)
    {
        return -EACCESS;
    }

    while (*p != '\0')
    {
         p += 1;

        if (p == NULL)
        {
            return -EINVAL;
        }

        vma = ROUND_DOWN((uintptr_t)p, PAGE_SIZE);
        pte = walk_pte(active_mspace(), vma);

        if (!PTE_VALID(*pte) ||
            (pte->flags & ug_flags) != ug_flags)
        {
            return -EACCESS;
        }
    }

    return 0;
}

// Reads satp to retrieve tag for active memory space
mtag_t active_space_mtag(void)
{
    return csrr_satp();
}

// Constructs tag from page table address and address space identifier.
static inline mtag_t ptab_to_mtag(struct pte * ptab, unsigned int asid)
{
    return (((unsigned long)PAGING_MODE << RISCV_SATP_MODE_shift) |
        ((unsigned long)asid << RISCV_SATP_ASID_shift) |
        pagenum(ptab) << RISCV_SATP_PPN_shift);
}

// Retrives a page table address from a tag.
static inline struct pte * mtag_to_ptab(mtag_t mtag)
{
    return (struct pte *)((mtag << 20) >> 8);
}

// Returns the address of the page table corresponding
// to the active memory space
static inline struct pte * active_space_ptab(void)
{
    return mtag_to_ptab(active_space_mtag());
}

// Constructs a physical pointer from a physical page number.
static inline void * pageptr(uintptr_t n)
{
    return (void*)(n << PAGE_ORDER);
}

// Constructs a physical page number from a pointer.
static inline unsigned long pagenum(const void * p)
{
    return (unsigned long)p >> PAGE_ORDER;
}

// Checks if bits 63:38 of passed virtual memory address are all 1 or all 0.
static inline int wellformed(uintptr_t vma)
{
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (uintptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

// Constructs a page table entry corresponding to a leaf.
static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags)
{
    return (struct pte)
    {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pagenum(pp)
    };
}

// Constructs a page table entry corresponding to a page table.
static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag)
{
    return (struct pte)
    {
        .flags = g_flag | PTE_V,
        .ppn = pagenum(pt)
    };
}

// Returns an empty pte.
static inline struct pte null_pte(void)
{
    return (struct pte) { };
}
