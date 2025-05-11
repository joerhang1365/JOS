// thread.c - Threads
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef THREAD_TRACE
#define TRACE
#endif

#ifdef THREAD_DEBUG
#define DEBUG
#endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "assert.h"
#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "memory.h"
#include "error.h"
#include "process.h"

#include <stdarg.h>

// COMPILE-TIME PARAMETERS
//

// NTHR is the maximum number of threads

#ifndef NTHR
#define NTHR 16
#endif

#ifndef STACK_SIZE
#define STACK_SIZE 4000
#endif

// EXPORTED GLOBAL VARIABLES
//

char thrmgr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

enum thread_state
{
    THREAD_UNINITIALIZED = 0,
    THREAD_WAITING,
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context
{
    uint64_t s[12];
    void * ra;
    void * sp;
};

struct thread_stack_anchor
{
    struct thread * ktp;
    void * kgp;
};

struct thread
{
    struct thread_context ctx;  // must be first member (thrasm.s)
    int id;                     // index into thrtab[]
    enum thread_state state;
    const char * name;
    struct thread_stack_anchor * stack_anchor;
    void * stack_lowest;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;
    struct lock * lock_list;
    struct process * proc;
};

// INTERNAL MACRO DEFINITIONS
//

// Pointer to running thread, which is kept in the tp (x4) register.

#define TP ((struct thread*)__builtin_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// INTERNAL FUNCTION DECLARATIONS
//

// Initializes the main and idle threads. called from threads_init().

static void init_main_thread(void);
static void init_idle_thread(void);

// Sets the RISC-V thread pointer to point to a thread.

static void set_running_thread(struct thread * thr);

// Returns a string representing the state name. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.

static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused));

// Reclaims a thread's slot in thrtab and makes its parent the parent of its
// children. Frees the struct thread of the thread.

static void thread_reclaim(int tid);

// Creates and initializes a new thread structure. The new thread is not added
// to any list and does not have a valid context (_thread_switch cannot be
// called to switch to the new thread).

static struct thread * create_thread(const char * name);

// Suspends the currently running thread and resumes the next thread on the
// ready-to-run list using _thread_swtch (in threasm.s). Must be called with
// interrupts enabled. Returns when the current thread is next scheduled for
// execution. If the current thread is TP, it is marked READY and placed
// on the ready-to-run list. Note that running_thread_suspend will only return if the
// current thread becomes READY.

static void running_thread_suspend(void);

// The following functions manipulate a thread list (struct thread_list). Note
// that threads form a linked list via the list_next member of each thread
// structure. Thread lists are used for the ready-to-run list (ready_list) and
// for the list of waiting threads of each condition variable. These functions
// are not interrupt-safe! The caller must disable interrupts before calling any
// thread list function that may modify a list that is used in an ISR.

static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED FUNCTION DECLARATIONS defined in thrasm.s
//

extern struct thread * _thread_swtch(struct thread * thr);
extern void _thread_startup(void);

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_TID 0
#define IDLE_TID (NTHR - 1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s

static struct thread main_thread =
{
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_RUNNING,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit.name = "main.child_exit"
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread =
{
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = (void *)&_thread_startup,
    .ctx.s[8] = (uintptr_t) &idle_thread_func
};

static struct thread * thrtab[NTHR] =
{
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list =
{
    .head = &idle_thread,
    .tail = &idle_thread
};

// EXPORTED FUNCTION DEFINITIONS
//

int running_thread(void)
{
    return TP->id;
}

void thrmgr_init(void)
{
    trace("%s()", __func__);
    init_main_thread();
    init_idle_thread();
    set_running_thread(&main_thread);
    thrmgr_initialized = 1;
}

// creates and starts a new thread
// sets the new threads context return address to _thread_startup
// which will set the functions entry and exit point
// sets up the stack pointer
// passes thread function arguments to save registers which are set in _thread_startup

int thread_spawn(
    const char * name,
    void (*entry)(void),
    ...) // arguments
{
    struct thread * child;
    va_list ap;
    int pie;
    int i;

    if (name == NULL)
    {
        name = "orphan";
    }

    child = create_thread(name);

    if (child == NULL)
    {
        return -EMTHR;
    }

    set_thread_state(child, THREAD_READY);
    condition_init(&child->child_exit, "child exit");

    pie = disable_interrupts();
    tlinsert(&ready_list, child);
    restore_interrupts(pie);

    // pass entry function's arguments

    child->ctx.ra = &_thread_startup;
    child->ctx.sp = (void*)child->stack_anchor;

    va_start(ap, entry);

    for (i = 0; i < 8; i++)
    {
        child->ctx.s[i] = va_arg(ap, uint64_t);
    }

    va_end(ap);

    child->ctx.s[8] = (uintptr_t)entry;

    return child->id;
}

// this function terminates the current running thread
// check if the current thread is main in which case halt the entire program
// otherwise signal the parent that the thread is exiting and yield to another thread

void thread_exit(void)
{
    struct lock * head;

    // if the current thread is the main thread, HALT the entire program.

    if(TP->id == MAIN_TID)
    {
        halt_success();
    }

    // signal parent thread that it has exited

    if (TP->parent != NULL)
    {
        condition_broadcast(&TP->parent->child_exit);
    }

    // check if thread is still holding locks, if so release all of them

    head = TP->lock_list;

    while (head != NULL)
    {
        lock_release(head);
        head = head->next;
    }

    set_thread_state(TP,THREAD_EXITED);
    running_thread_suspend();

    // if return from running_thread_suspend something when wrong
    // cuz this thread aint supposed to run anymore

    halt_failure();
}

void thread_yield(void)
{
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    running_thread_suspend();
}

// this function waits for a child thread to exit
// if the tid is 0 (main_thread) wait for any thread to exit
// otherwise wait for the identified thread to exit

int thread_join(int tid)
{
    struct thread * child_thread;
    int child_tid;

    if (tid < 0 || tid > NTHR - 1)
    {
        return -EINVAL;
    }

    if (tid == 0)
    {
        // find active child of current thread to wait on
        // tid 0 and NTHR are main and idle threads
        for (child_tid = 1; child_tid < NTHR - 1; child_tid++)
        {
            child_thread = thrtab[child_tid];

            if (child_thread != NULL && child_thread->parent == TP)
            {
                break;
            }
        }
    }
    else
    {
        child_tid = tid;
        child_thread = thrtab[tid];
    }

    if (child_thread == NULL || child_thread->parent != TP)
    {
        return -ECHILD;
    }

 	condition_init(&child_thread->parent->child_exit, child_thread->name);

	while (child_thread->state != THREAD_EXITED)
    {
		debug("waiting on child to exit\n");
		condition_wait(&thrtab[child_tid]->parent->child_exit);
	}

	thread_reclaim(child_tid);

    return child_tid;
}

const char * thread_name(int tid)
{
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void)
{
    return TP->name;
}

void condition_init(struct condition * cond, const char * name)
{
    int pie = disable_interrupts();
    tlclear(&cond->wait_list);
    restore_interrupts(pie);
    cond->name = name;
}

// puts the thread to sleep
void condition_wait(struct condition * cond)
{
    int pie;

    trace("%s(cond=<%s>) in <%s:%d>", __func__,
        cond->name, TP->name, TP->id);

    assert(TP->state == THREAD_RUNNING);

    // insert current thread into condition wait list

    set_thread_state(TP, THREAD_WAITING);
    TP->wait_cond = cond;
    TP->list_next = NULL;

    pie = disable_interrupts();
    tlinsert(&cond->wait_list, TP);
    restore_interrupts(pie);

    running_thread_suspend();
}

// this function wakes up threads waiting on the condition variable
// this function may be called from an ISR
// calling condition broadcast does not cause a context switch from the currently running thread
// waiting threads are added to the ready-to-run list in the order they were added to
// the wait queue

void condition_broadcast(struct condition * cond)
{
    struct thread * head;
    int pie;

    trace("%s(cond=<%s>) in <%s:%d>", __func__,
        cond->name, TP->name, TP->id);

    head = cond->wait_list.head;

    if (head == NULL)
    {
        return;
    }

    pie = disable_interrupts();

    while (head != NULL)
    {
        set_thread_state(head, THREAD_READY);
        head = head->list_next;
    }

    // put all waiting threads onto the ready list

    tlappend(&ready_list, &cond->wait_list);
    tlclear(&cond->wait_list);
    restore_interrupts(pie);
}

// LOCKING FUNCTION DEFFINITIONS
//

void lock_init(struct lock * lock)
{
    condition_init(&lock->released, "lock");
    lock->owner = NULL;
    lock->next = NULL;
    lock->cnt = 0;
}

void lock_acquire(struct lock * lock)
{
    int pie;

    if (lock->owner == TP)
    {
        lock->cnt++;
        debug("thread %s already owns lock", TP->name);
        return;
    }

    // if another thread is trying to acquire an already acquired lock
    // wait until lock is avaiable

    pie = disable_interrupts();

    while (lock->owner != NULL)
    {
        debug("thread %s failed to acquire lock", TP->name);
        condition_wait(&lock->released);
    }

    restore_interrupts(pie);

    // set the lock owner to current thread pointer
    // and add to head of lock_list

    lock->cnt = 1;
    lock->owner = TP;
    lock->next = TP->lock_list;
    TP->lock_list = lock;

    debug("thread %s acquired lock", TP->name);
    debug("lock count=%d", lock->cnt);
}

void lock_release(struct lock * lock)
{
    struct lock * target;
    struct lock * previous;

    assert (lock->owner == TP);

    lock->cnt--;

    debug("thread %s tried to release lock", TP->name);
    debug("lock count=%d", lock->cnt);

    if (lock->cnt > 0)
    {
        return;
    }

    debug("thread %s released lock", TP->name);

    lock->owner = NULL;

    // remove lock from current thread pointer lock_list

    target = TP->lock_list;
    previous = NULL;

    while (target != NULL)
    {
        if (target == lock)
            break;

        previous = target;
        target = target->next;
    }

    if (target == NULL)
    {
        condition_broadcast(&lock->released);
        return;
    }

    if (target == TP->lock_list)
    {
        TP->lock_list = target->next;
    }
    else
    {
        previous->next = target->next;
    }

    condition_broadcast(&lock->released);
}

struct process * thread_process(int tid)
{
    return thrtab[tid]->proc;
}

struct process * running_thread_process(void)
{
    return TP->proc;
}

void thread_set_process(int tid, struct process * proc)
{
    thrtab[tid]->proc = proc;
}

extern struct thread_stack_anchor * running_thread_stack_anchor(void)
{
    return TP->stack_anchor;
}

extern struct thread * current_thread(void)
{
    return TP;
}

// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void)
{
    main_thread.stack_anchor->ktp = &main_thread;
}

void init_idle_thread(void)
{
    idle_thread.stack_anchor->ktp = &idle_thread;
}

static void set_running_thread(struct thread * thr)
{
    asm inline ("mv tp, %0" :: "r"(thr) : "tp");
}

const char * thread_state_name(enum thread_state state)
{
    static const char * const names[] =
    {
        [THREAD_UNINITIALIZED] = "UNINITIALIZED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_RUNNING] = "RUNNING",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
    {
        return names[state];
    }
    else
    {
        return "UNDEFINED";
    }
};

void thread_reclaim(int tid)
{
    struct thread * const thr = thrtab[tid];
    int ctid;

    assert (0 < tid && tid < NTHR && thr != NULL);
    assert (thr->state == THREAD_EXITED);

    // Make our parent thread the parent of our child threads. We need to scan
    // all threads to find our children. We could keep a list of all of a
    // thread's children to make this operation more efficient.

    for (ctid = 1; ctid < NTHR; ctid++)
    {
        if (thrtab[ctid] != NULL && thrtab[ctid]->parent == thr)
        {
            thrtab[ctid]->parent = thr->parent;
        }
    }

    thrtab[tid] = NULL;
    kfree(thr);
}

struct thread * create_thread(const char * name)
{
    struct thread_stack_anchor * anchor;
    void * stack_page;
    struct thread * thr;
    int tid;

    trace("%s(name=\"%s\") in <%s:%d>", __func__, name, TP->name, TP->id);

    // Find a free thread slot.

    tid = 0;
    while (++tid < NTHR)
    {
        if (thrtab[tid] == NULL)
        {
            break;
        }
    }

    if (tid == NTHR)
    {
        return NULL;
    }

    // Allocate a struct thread and a stack

    thr = kcalloc(1, sizeof(struct thread));

    stack_page = alloc_phys_page();
    memset(stack_page, 0, STACK_SIZE);
    anchor = stack_page + STACK_SIZE;
    anchor -= 1; // anchor is at base of stack
    thr->stack_lowest = stack_page;
    thr->stack_anchor = anchor;
    anchor->ktp = thr;
    anchor->kgp = NULL;

    thrtab[tid] = thr;

    thr->id = tid;
    thr->name = name;
    thr->parent = TP;

    return thr;
}

// suspends the currently running thread and resumes the next thread on the
// ready-to-run list using thread swtch
// implementation is a simple round-robin scheduler
// must be called with interrupts enabled
// returns when the current thread is next scheduled for execution
// if the current thread is running it is marked THREAD READY and placed on
// the ready-to-run list
// if the thread is in the exited state free its stack

void running_thread_suspend(void)
{
    struct thread * prev_thread;
    struct thread * next_thread;
    int pie;

    trace("%s(state\"%s\"\")", __func__,
            thread_state_name(TP->state));

    // if the thread is running put back into the ready list
    // to run later

    pie = disable_interrupts();

    if (TP->state == THREAD_RUNNING)
    {
        set_thread_state(TP, THREAD_READY);
        tlinsert(&ready_list, TP);
    }

    next_thread = tlremove(&ready_list);
    restore_interrupts(pie);

    // get the next threads memory space
    if (next_thread->proc != NULL)
    {
        switch_mspace(next_thread->proc->mtag);
    }

    // pretty big

    enable_interrupts();
    set_thread_state(next_thread, THREAD_RUNNING);
    prev_thread = _thread_swtch(next_thread);

    if (prev_thread->state == THREAD_EXITED)
    {
        free_phys_page(prev_thread->stack_lowest);
    }

    restore_interrupts(pie);
}

void tlclear(struct thread_list * list)
{
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list)
{
    return (list->head == NULL);
}

void tlinsert(struct thread_list * list, struct thread * thr)
{
    thr->list_next = NULL;

    if (thr == NULL)
        return;

    if (list->tail != NULL)
    {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    }
    else
    {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list)
{
    struct thread * thr;

    thr = list->head;

    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;

    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

// Appends elements of l1 to the end of l0 and clears l1.

void tlappend(struct thread_list * l0, struct thread_list * l1)
{
    if (l0->head != NULL)
    {
        assert(l0->tail != NULL);

        if (l1->head != NULL)
        {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    }
    else
    {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void idle_thread_func(void)
{
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;)
    {
        // If there are runnable threads, yield to them.
        while (!tlempty(&ready_list))
            thread_yield();

        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}
