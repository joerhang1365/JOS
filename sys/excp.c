// excp.c - Exception handlers
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef EXCP_TRACE
#define TRACE
#endif

#ifdef EXCP_DEBUG
#define DEBUG
#endif


#include "trap.h"
#include "riscv.h"
#include "memory.h"
#include "thread.h"
#include "intr.h"
#include "console.h"
#include "assert.h"
#include "string.h"

#include <stddef.h>

// EXPORTED FUNCTION DECLARATIONS
//

// The following two functions, defined below, are called to handle an exception
// from trap.s.

extern void handle_smode_exception(unsigned int cause, struct trap_frame * tfr);
extern void handle_umode_exception(unsigned int cause, struct trap_frame * tfr);

// IMPORTED FUNCTION DECLARATIONS
//

extern void handle_syscall(struct trap_frame * tfr); // syscall.c

// INTERNAL GLOBAL VARIABLES
//

static const char * const excp_names[] =
{
    [RISCV_SCAUSE_INSTR_ADDR_MISALIGNED] = "Misaligned instruction address",
    [RISCV_SCAUSE_INSTR_ACCESS_FAULT] = "Instruction access fault",
    [RISCV_SCAUSE_ILLEGAL_INSTR] = "Illegal instruction",
    [RISCV_SCAUSE_BREAKPOINT] = "Breakpoint",
    [RISCV_SCAUSE_LOAD_ADDR_MISALIGNED] = "Misaligned load address",
    [RISCV_SCAUSE_LOAD_ACCESS_FAULT] = "Load access fault",
    [RISCV_SCAUSE_STORE_ADDR_MISALIGNED] = "Misaligned store address",
    [RISCV_SCAUSE_STORE_ACCESS_FAULT] = "Store access fault",
    [RISCV_SCAUSE_ECALL_FROM_UMODE] = "Environment call from U mode",
    [RISCV_SCAUSE_ECALL_FROM_SMODE] = "Environment call from S mode",
    [RISCV_SCAUSE_INSTR_PAGE_FAULT] = "Instruction page fault",
    [RISCV_SCAUSE_LOAD_PAGE_FAULT] = "Load page fault",
    [RISCV_SCAUSE_STORE_PAGE_FAULT] = "Store page fault"
};

// EXPORTED FUNCTION DEFINITIONS
//

void handle_smode_exception(unsigned int cause, struct trap_frame * tfr)
{
    const char * name = NULL;
    char msgbuf[80];
    int result;

    if (0 <= cause && cause < sizeof(excp_names) / sizeof(excp_names[0]))
    {
        name = excp_names[cause];
    }

    if (name != NULL)
    {
        switch (cause)
        {
        case RISCV_SCAUSE_LOAD_PAGE_FAULT:
        case RISCV_SCAUSE_STORE_PAGE_FAULT:
        case RISCV_SCAUSE_INSTR_PAGE_FAULT:
        case RISCV_SCAUSE_LOAD_ADDR_MISALIGNED:
        case RISCV_SCAUSE_STORE_ADDR_MISALIGNED:
        case RISCV_SCAUSE_INSTR_ADDR_MISALIGNED:
        case RISCV_SCAUSE_LOAD_ACCESS_FAULT:
        case RISCV_SCAUSE_STORE_ACCESS_FAULT:
        case RISCV_SCAUSE_INSTR_ACCESS_FAULT:
            snprintf(msgbuf, sizeof(msgbuf),
                "%s at %p for %p in S mode",
                name, (void*)tfr->sepc, (void*)csrr_stval());
            break;
        case RISCV_SCAUSE_ECALL_FROM_SMODE:
            handle_syscall(tfr);
            result = 1;
            break;
        default:
            snprintf(msgbuf, sizeof(msgbuf),
                "%s at %p in S mode",
                name, (void*)tfr->sepc);
        }
    }
    else
    {
        snprintf(msgbuf, sizeof(msgbuf),
            "Exception %d at %p in S mode",
            cause, (void*)tfr->sepc);
    }

    if (result == 1)
    {
        return;
    }

    panic(msgbuf);
}

void handle_umode_exception(unsigned int cause, struct trap_frame * tfr) {
    const char * name = NULL;
    char msgbuf[80];
    uintptr_t vma = (uintptr_t)csrr_stval();
    int result;

    trace("%s(cause=%d)", __func__, cause);

    if (0 <= cause && cause < sizeof(excp_names) / sizeof(excp_names[0]))
    {
        name = excp_names[cause];
    }

    if (name != NULL)
    {
        switch (cause)
        {
        case RISCV_SCAUSE_LOAD_PAGE_FAULT:
            result = handle_umode_page_fault(tfr, vma);
            break;
        case RISCV_SCAUSE_STORE_PAGE_FAULT:
            result = handle_umode_page_fault(tfr, vma);
            break;
        case RISCV_SCAUSE_INSTR_PAGE_FAULT:
        case RISCV_SCAUSE_LOAD_ADDR_MISALIGNED:
        case RISCV_SCAUSE_STORE_ADDR_MISALIGNED:
        case RISCV_SCAUSE_INSTR_ADDR_MISALIGNED:
        case RISCV_SCAUSE_LOAD_ACCESS_FAULT:
        case RISCV_SCAUSE_STORE_ACCESS_FAULT:
        case RISCV_SCAUSE_INSTR_ACCESS_FAULT:
            snprintf(msgbuf, sizeof(msgbuf),
                "%s at %p for %p in U mode",
                name, (void*)tfr->sepc, (void*)vma);
            break;
        case RISCV_SCAUSE_ECALL_FROM_UMODE:
            handle_syscall(tfr);
            result = 1;
            break;
        default:
            snprintf(msgbuf, sizeof(msgbuf),
                "%s at %p in U mode",
                name, (void*)tfr->sepc);
        }
    }
    else
    {
        snprintf(msgbuf, sizeof(msgbuf),
            "Exception %d at %p in U mode",
            cause, (void*)tfr->sepc);
    }

    if (result == 1)
    {
        return;
    }

    panic(msgbuf);
}
