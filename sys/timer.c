// timer.c
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "assert.h"
#include "intr.h"
#include "conf.h"
#include "see.h" // for set_stcmp

// EXPORTED GLOBAL VARIABLE DEFINITIONS
//

char timer_initialized = 0;

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

static struct alarm * sleep_list;

// INTERNAL FUNCTION DECLARATIONS
//

static void alarm_insert_sorted(struct alarm * al);

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void)
{
    set_stcmp(UINT64_MAX);
    timer_initialized = 1;
    sleep_list = NULL;

	// set up initial timer interrupt for preemptive scheduling
    //set_stcmp(rdtime() + 1000 * (TIMER_FREQ / 1000 / 1000));
}

// void alarm_init(struct alarm * al, const char * name)
//
// this function initializes an alarm updating its twake
// the _name_ argument is optional
//
// args: struct alarm * al
//       const char * name
// return: void

void alarm_init(struct alarm * al, const char * name)
{
    long long now = rdtime();

    trace("%s()",__func__);

    if (name == NULL)
    {
        name = "wake tf up";
    }

    condition_init(&al->cond, name);
    al->next = NULL;
    al->twake = now;
}

// void alarm_sleep(struct alarm * al, unsigned long long tcnt)
//
// the function puts the current thread to sleep for some number of ticks
// the _tcnt_ argument specifies the number of timer ticks relative to the most recent
// alarm event, either init, wake-up, or reset
//
// args: struct alarm * al
//       unsigned long long tcnt
// return: void

void alarm_sleep(struct alarm * al, unsigned long long tcnt)
{
    unsigned long long now = rdtime();
    int pie;

    trace("%s(now=%lld)",__func__,now);

    // if the tcnt is so large it wraps around, set it to UINT64_MAX
    if (UINT64_MAX - al->twake < tcnt)
    {
        al->twake = UINT64_MAX;
    }
    else
    {
        al->twake += tcnt;
    }

    // if the wake-up time has already passed, return
    if (al->twake <= now)
    {
        return;
    }

    // insert alarm at next earliest position
    pie = disable_interrupts();
    alarm_insert_sorted(al);
    restore_interrupts(pie);

    // set trigger if new alarm has earliest time
    if (sleep_list == al)
    {
        set_stcmp(al->twake);
    }

    csrs_sie(RISCV_SIE_STIE); // enable timer interrupts
    condition_wait(&al->cond); // put current thread to sleep
}

// void alarm_reset(struct alarm * al)
//
// resets the alarm so that the next sleep increment is relative to the time
// alarm_reset is called
//
// args: struct alarm * al
// return: void

void alarm_reset(struct alarm * al)
{
    al->twake = rdtime();
}

void alarm_sleep_sec(struct alarm * al, unsigned int sec)
{
    alarm_sleep(al, sec * TIMER_FREQ);
}

void alarm_sleep_ms(struct alarm * al, unsigned long ms)
{
    alarm_sleep(al, ms * (TIMER_FREQ / 1000));
}

void alarm_sleep_us(struct alarm * al, unsigned long us)
{
    alarm_sleep(al, us * (TIMER_FREQ / 1000 / 1000));
}

void sleep_sec(unsigned int sec)
{
    sleep_ms(1000UL * sec);
}

void sleep_ms(unsigned long ms)
{
    sleep_us(1000UL * ms);
}

void sleep_us(unsigned long us)
{
    struct alarm al;

    alarm_init(&al, "sleep");
    alarm_sleep_us(&al, us);
}

// void handle_timer_interrupt(void)
//
// this function handles the timer interrrupt service routine
// wakes up all threads that are past their alarms by placing them onto the ready_list
// if there are no alarms on the sleep_list disable timer interrupts
// if there are alarms set the next scheduled interrupt time
// handle_timer_interrupt() is dispatched from intr_handler in intr.c
//
// newly implemented preemptive scheduling so if no alarms are on the list
// automatically set a 10ms timer forcing the thread to enter an interrupt
// and yeild to the thread scheduler
//
// args: void
// return: void

void handle_timer_interrupt(void)
{
    struct alarm * target;
    uint64_t now = rdtime();
    int32_t pie;

    trace("[%lu] %s()", now, __func__);
    debug("[%lu] mtcmp = %lu", now, rdtime());

    pie = disable_interrupts();

    // check if any alarms are past due

    while (sleep_list != NULL)
    {
        target = sleep_list;
        sleep_list = target->next;
        target->next = NULL;

        if (target->twake <= now)
        {
            condition_broadcast(&target->cond);
        }
        else
        {
            break; // list ordered from least to greatest
        }
    }

    // check if there are no more alarms

    if (sleep_list == NULL)
    {
		// always have one timer set for preemptive scheduling
        //set_stcmp(now + 1000 * (TIMER_FREQ / 1000 / 1000));
        csrc_sie(RISCV_SIE_STIE); // disable timer interrupts
    }
    else
    {
        // set next timer interrupt trigger
        set_stcmp(sleep_list->twake);
    }

    restore_interrupts(pie);
}

// static void alarm_insert_sorted(struct alarm * al)
//
// helper function for alarm_sleep
// inserts an alarm into the sleep_list in order from earliest time
// to lastest time
//
// args: struct alarm * al
// return: void

static void alarm_insert_sorted(struct alarm * al)
{
    struct alarm ** target;

    trace("%s(al=%p)", __func__, al);

    target = &sleep_list;

    // insert alarm from lowest twake to highest twake
    while (*target != NULL && (*target)->twake < al->twake)
    {
        target = &(*target)->next;
    }

    al->next = *target;
    *target = al;
}
