// process.c - user process
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "process.h"
#include "elf.h"
#include "fs.h"
#include "io.h"
#include "ioimpl.h"
#include "string.h"
#include "thread.h"
#include "riscv.h"
#include "trap.h"
#include "memory.h"
#include "heap.h"
#include "error.h"
#include "intr.h"

// COMPILE-TIME PARAMETERS
//

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

static int build_stack(void * stack, int argc, char ** argv);
static void fork_func(struct condition * forked, struct trap_frame * tfr);

// INTERNAL GLOBAL VARIABLES
//

static struct process main_proc;
static struct process * proctab[NPROC] = { &main_proc };

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void procmgr_init(void)
{
    assert (memory_initialized && heap_initialized);
    assert (!procmgr_initialized);

    main_proc.idx = 0;
    main_proc.tid = running_thread();
    main_proc.mtag = active_mspace();
    thread_set_process(main_proc.tid, &main_proc);
	main_proc.iotab[0] = create_null_io();
    procmgr_initialized = 1;
}

int process_exec(struct io * exeio, int argc, char ** argv)
{
	struct trap_frame tfr;
	void (*exe_entry)(struct io *);
	void * stack;
	void * stkvptr;
	int stksz;
	int result;

	trace("%s()", __func__);

	// set up process stack by allocating page, moving
	// arguments, and mapping memory space

	stack = alloc_phys_page();
	memset(stack, 0, PAGE_SIZE);
	stksz = build_stack(stack, argc, argv);
	reset_active_mspace();
	stkvptr = map_range(
		(uintptr_t)(UMEM_END - PAGE_SIZE),
		PAGE_SIZE, stack, PTE_R | PTE_W | PTE_U);

	// load elf into new mspace
	result = elf_load(exeio, (void (**)(void))&exe_entry);

	if (result < 0)
	{
		panic("elf did not read correctly :(");
	}

	// create trap frame
	// a1 points to the next argv

	tfr.a0 = argc;
	tfr.a1 = (long)stkvptr + PAGE_SIZE - stksz;
	tfr.sp = stkvptr;
	tfr.tp = current_thread();

	tfr.sstatus = csrr_sstatus();
	tfr.sstatus |= RISCV_SSTATUS_SPIE; // enable interrupts
	tfr.sstatus &= ~RISCV_SSTATUS_SPP; // set privilage mode to user

	tfr.sepc = exe_entry;

	debug("THIS IS ABOUT TO GET SHEIST");
	trap_frame_jump(&tfr, (void *) running_thread_stack_anchor());
	thread_exit();

    return -EMPROC;
}

int process_fork(const struct trap_frame * tfr)
{
	struct process * proc;
	struct trap_frame child_tfr;
	struct condition forked;
	int pn, fd, child_tid;

	trace("%s()", __func__);

	// find a free process

	for (pn = 1; pn < NPROC; pn++)
	{
		if (proctab[pn] == NULL)
		{
			break;
		}
	}

	if (pn == NPROC)
	{
		return -EMPROC;
	}

	// set up process thread

	condition_init(&forked, "child forked");
	child_tfr = *tfr;
	child_tid = thread_spawn("child fork", (void (*)(void)) &fork_func,
		&forked, child_tfr);

	if (child_tid < 0)
	{
		return -EMTHR;
	}

	// create process memory space

	mtag_t child_mtag = clone_active_mspace();

	// allocate process

	proc = kcalloc(1, sizeof(struct process));
	proc->idx = pn;
	proc->tid = child_tid;
	proc->mtag = child_mtag;

	proctab[pn] = proc;
	thread_set_process(child_tid, proc);

	for (fd = 0; fd < PROCESS_IOMAX; fd++)
	{
		if (current_process()->iotab[fd] != NULL)
		{
			proc->iotab[fd] = ioaddref(current_process()->iotab[fd]);
		}
	}

	condition_wait(&forked);

	return child_tid;
}

struct process * current_process(void)
{
    return running_thread_process();
}

void process_exit(void)
{
	debug("idx=%d process exited\n", current_process()->tid);

	if (current_process()->tid == 0)
	{
		panic("main process exited");
	}

	for (int i = 0; i < PROCESS_IOMAX; i++)
	{
		if (current_process()->iotab[i] != NULL)
		{
			ioclose(current_process()->iotab[i]);
		}
	}

	fsflush();
	kfree(current_process());
	discard_active_mspace();
	thread_exit();
}

// INTERNAL FUNCTION DEFINITIONS
//

int build_stack(void * stack, int argc, char ** argv)
{
    size_t stksz, argsz;
    uintptr_t * newargv;
    char * p;
    int i;

    // We need to be able to fit argv[] on the initial stack page, so _argc_
    // cannot be too large. Note that argv[] contains argc+1 elements (last one
    // is a NULL pointer).

    if (PAGE_SIZE / sizeof(char*) - 1 < argc)
	{
        return -ENOMEM;
	}

    stksz = (argc + 1) * sizeof(char*);

    // Add the sizes of the null-terminated strings that argv[] points to.

    for (i = 0; i < argc; i++)
	{
        argsz = strlen(argv[i]) + 1;
        if (PAGE_SIZE - stksz < argsz)
		{
            return -ENOMEM;
		}
        stksz += argsz;
    }

    // Round up stksz to a multiple of 16 (RISC-V ABI requirement).

    stksz = ROUND_UP(stksz, 16);
    assert (stksz <= PAGE_SIZE);

    // Set _newargv_ to point to the location of the argument vector on the new
    // stack and set _p_ to point to the stack space after it to which we will
    // copy the strings. Note that the string pointers we write to the new
    // argument vector must point to where the user process will see the stack.
    // The user stack will be at the highest page in user memory, the address of
    // which is `(UMEM_END_VMA - PAGE_SIZE)`. The offset of the _p_ within the
    // stack is given by `p - newargv'.

    newargv = stack + PAGE_SIZE - stksz;
    p = (char*)(newargv + argc + 1);

    for (i = 0; i < argc; i++)
	{
        newargv[i] = (UMEM_END_VMA - PAGE_SIZE) + ((void*)p - (void*)stack);
        argsz = strlen(argv[i]) + 1;
        memcpy(p, argv[i], argsz);
        p += argsz;
    }

    newargv[argc] = 0;

    return stksz;
}

void fork_func(struct condition * forked, struct trap_frame * tfr)
{
	tfr->a0 = 0;
	tfr->tp = current_thread();

	condition_broadcast(forked);
	trap_frame_jump(tfr, (void *) running_thread_stack_anchor());
}
