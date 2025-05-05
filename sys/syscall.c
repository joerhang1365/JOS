// syscall.c - system call handlers
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef SYSCALL_TRACE
#define TRACE
#endif

#ifdef SYSCALL_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "scnum.h"
#include "process.h"
#include "memory.h"
#include "io.h"
#include "device.h"
#include "fs.h"
#include "intr.h"
#include "timer.h"
#include "error.h"
#include "thread.h"
#include "process.h"

// EXPORTED FUNCTION DECLARATIONS
//

extern void handle_syscall(struct trap_frame * tfr); // called from excp.c

// INTERNAL FUNCTION DECLARATIONS
//

static int64_t syscall(const struct trap_frame * tfr);

static int sysexit(void);
static int sysexec(int fd, int argc, char ** argv);
static int sysfork(const struct trap_frame * tfr);
static int syswait(int tid);
static int sysprint(const char * msg);
static int sysusleep(unsigned long us);

static int sysdevopen(int fd, const char * name, int instno);
static int sysfsopen(int fd, const char * name);
static int sysfscreate(const char* name);
static int sysfsdelete(const char* name);

static int sysclose(int fd);
static long sysread(int fd, void * buf, size_t bufsz);
static long syswrite(int fd, const void * buf, size_t len);
static int sysioctl(int fd, int cmd, void * arg);
static int syspipe(int * wfdptr, int * rfdptr);
static int sysiodup(int oldfd, int newfd);

// EXPORTED FUNCTION DEFINITIONS
//

// Initiates syscall present in trap frame struct and stores the return address
// into the sepc. Imported function definition from syscall.c that handles
// system calls from user mode. sepc will be used to return back to program
// execution after interrupt is handled and sret is called
void handle_syscall(struct trap_frame * tfr) {
	trace("%s()", __func__);
	tfr->sepc += 4;
	tfr->a0 = syscall(tfr);
}

// INTERNAL FUNCTION DEFINITIONS
//

// Calls specified syscall and passes arguments.
// Called by handle_syscall to handle all syscalls.
// Jumps to a system call based on the specified system call number.
// Provides the correct arguments to the syscall from the trap frame.
int64_t syscall(const struct trap_frame * tfr) {
	int64_t scnum = tfr->a7;
	int64_t result;

	trace("%s(scnum=%ld)", __func__, scnum);

	switch (scnum) {
	case SYSCALL_EXIT:
		result = sysexit();
		break;
	case SYSCALL_EXEC:
		result = sysexec(tfr->a0, tfr->a1, (char **)tfr->a2);
		break;
	case SYSCALL_FORK:
		result = sysfork(tfr);
		break;
	case SYSCALL_WAIT:
		result = syswait(tfr->a0);
		break;
	case SYSCALL_PRINT:
		result = sysprint((char *)tfr->a0);
		break;
	case SYSCALL_USLEEP:
		result = sysusleep(tfr->a0);
		break;
	case SYSCALL_DEVOPEN:
		result = sysdevopen(tfr->a0, (char *)tfr->a1, tfr->a2);
		break;
	case SYSCALL_FSOPEN:
		result = sysfsopen(tfr->a0, (char *)tfr->a1);
		break;
	case SYSCALL_CLOSE:
		result = sysclose(tfr->a0);
		break;
	case SYSCALL_READ:
		result = sysread(tfr->a0, (void *)tfr->a1, tfr->a2);
		break;
	case SYSCALL_WRITE:
		result = syswrite(tfr->a0, (void *)tfr->a1, tfr->a2);
		break;
	case SYSCALL_IOCTL:
		result = sysioctl(tfr->a0, tfr->a1, (void *)tfr->a2);
		break;
	case SYSCALL_PIPE:
		result = syspipe((int *)tfr->a0, (int *)tfr->a1);
		break;
	case SYSCALL_FSCREATE:
		result = sysfscreate((char *)tfr->a0);
		break;
	case SYSCALL_FSDELETE:
		result = sysfsdelete((char *)tfr->a0);
		break;
	case SYSCALL_IODUP:
		result = sysiodup(tfr->a0, tfr->a1);
	default:
		result = -ENOTSUP;
	}

	return result;
}

// Exits the currently running process.
int sysexit(void) {
	process_exit();
    return 0;
}

// Executes new process given a executable and arguments.
// This function will execute the program associated with the io device
// associated with fd. This function must do validity checks as well as relevant
// cleanup. Variadic arguments should be passed to the program to execute.
int sysexec(int fd, int argc, char ** argv) {
	trace("%s(fd=%d, argc=%d, argv=%p)", __func__, fd, argc, argv);

	// TODO: validate argv

	if (current_process()->iotab[fd] == NULL)
		return -EBADFD;

	process_exec(current_process()->iotab[fd], argc, argv);

    return 0;
}

// This syscall will create a new child process that is a clone of the caller.
int sysfork(const struct trap_frame * tfr) {
	trace("%s()", __func__);
	return process_fork(tfr);
}

// Sleeps until a specified child process completes.
int syswait(int tid) {
	trace("%s(tid=%d)", __func__, tid);

	if (0 <= tid)
		return thread_join(tid);
	else
		return -ECHILD;
}

// Prints to console.
// Validates that msg string is valid (NULL-terminated) and then prints it to
// the console in the following format: <thread_name:thread_num> msg
int sysprint(const char * msg) {
	int result;

	trace("%s(msg=%p)", __func__, msg);

	result = memory_validate_vstr(msg, PTE_U);

	if (result != 0)
		return result;

	kprintf("Thread <%s:%d> says: %s\n",
			thread_name(running_thread()),
			running_thread(), msg);

    return 0;
}

// Sleeps process until the specified amount of time has passed.
int sysusleep(unsigned long us) {
	trace("%s(us=%ld)", __func__, us);
	sleep_us(us);
    return 0;
}

// Opens unique device instance for current process.
// Will allocate a valid and unused file descriptor if fd = -1.
// Otherwise, it will use the file descriptor given, if it is valid and unused.
int sysdevopen(int fd, const char * name, int instno) {
	int result;

	trace("%s(fd=%d, name=%s)", __func__, fd, name);

	result = memory_validate_vstr(name, PTE_U);

	if (result != 0)
		return result;

	if (fd >= PROCESS_IOMAX)
		return -EBADFD;

	if (fd < 0) {
		for (fd = 0; fd < PROCESS_IOMAX; fd++) {
			if (current_process()->iotab[fd] == NULL)
				break;
		}
	}

	if (fd >= PROCESS_IOMAX)
		return -EMFILE;

    result = open_device(name, instno, &current_process()->iotab[fd]);

	if (result != 0)
		return result;

	return fd;
}

// Opens file in filesystem for current process.
// Will allocate a valid and unused file descriptor if fd = -1.
// Otherwise, it will use the file descriptor given, if it is valid and unused.
int sysfsopen(int fd, const char * name) {
	int result;

	trace("%s(fd=%d, name=%s)", __func__, fd, name);

	result = memory_validate_vstr(name, PTE_U);

	if (result != 0)
		return result;

	if (fd >= PROCESS_IOMAX)
		return -EBADFD;

	if (fd < 0) {
		for (fd = 0; fd < PROCESS_IOMAX; fd++) {
			if (current_process()->iotab[fd] == NULL)
				break;
		}
	}

	if (fd >= PROCESS_IOMAX)
		return -EMFILE;

	result = fsopen(name, &current_process()->iotab[fd]);

	if (result != 0)
		return result;

	return fd;
}

// Creates a new file named name of length 0 in the filesystem.
int sysfscreate(const char * name) {
	int result;

	trace("%s(name=%s)", __func__, name);

	result = memory_validate_vstr(name, PTE_U);

	if (result != 0)
		return result;

    return fscreate(name);
}

// Deletes a file named name from the filesystem.
int sysfsdelete(const char * name) {
	int result;

	trace("%s(name=%s)", __func__, name);

	result = memory_validate_vstr(name, PTE_U);

	if (result != 0)
		return result;

    return fsdelete(name);
}


// Closes the file or device associated with the provided fd.
// Should mark the file descriptor as unused after closing.
int sysclose(int fd) {
	trace("%s(fd=%d)", __func__, fd);

	if (fd < 0 || fd >= PROCESS_IOMAX ||
		current_process()->iotab[fd] == NULL)
	{
		return -EBADFD;
	}

    ioclose(current_process()->iotab[fd]);
	current_process()->iotab[fd] = NULL;

	return 0;
}

// Reads from the file/device associated with fd into the given buffer (buf)
// This function must make sure that at most bufsz number of bytes is read into
// buf, among other validity checks.
long sysread(int fd, void * buf, size_t bufsz) {
	int result;

	trace("%s(fd=%d, buf=%p, bufsz=%ld)", __func__, fd, buf, bufsz);

	result = memory_validate_vptr_len(buf, bufsz, PTE_R | PTE_U);

	if (result != 0)
		return result;

	if (fd < 0 || fd >= PROCESS_IOMAX ||
		current_process()->iotab[fd] == NULL)
	{
		return -EBADFD;
	}

    result = ioread(current_process()->iotab[fd], buf, bufsz);

	if (result < bufsz)
		return -EINVAL;

	return result;
}

// Writes to the file/device from the provided buffer (buf)
// This function must make sure that at most bufsz number of bytes is written
// from the buf into the file/device, among other validity checks.
long syswrite(int fd, const void * buf, size_t len) {
	int result;

	trace("%s(fd=%d, buf=%p, len=%ld)", __func__, fd, buf, len);

	result = memory_validate_vptr_len(buf, len, PTE_W | PTE_U);

	// DOOM makes a syswrite call with a NULL buffer
	// in order to perform a flush
	// need to create exception for this when length = 0

	if (result != 0 && len != 0)
		return result;

	if (fd < 0 || fd >= PROCESS_IOMAX ||
		current_process()->iotab[fd] == NULL)
	{
		return -EBADFD;
	}

    result = iowrite(current_process()->iotab[fd], buf, len);

	if (result < len)
		return -EINVAL;

	return result;
}

// Calls device ioctl commands for a given device instance, specified by fd.
int sysioctl(int fd, int cmd, void * arg) {
	trace("%s(fd=%d, cmd=%d)", __func__, fd, cmd);

	if (fd < 0 || fd >= PROCESS_IOMAX ||
		current_process()->iotab[fd] == NULL)
	{
		return -EBADFD;
	}

    return ioctl(current_process()->iotab[fd], cmd, arg);
}

// This function will allocate a valid and unused file descriptor for wfdptr
// and rfdptr if their value is negative. Otherwise, if they are both
// non-negative, this function will create a pipe using those fds, assuming
// that they are empty. The fds should be considered invalid if they are both
// equal, regardless if found by the function or provided (in the non-negative case).
int syspipe(int * wfdptr, int * rfdptr) {
	int wfd, rfd;

	trace("%s(wfdptr=%p, rfdptr=%p)", __func__, wfdptr, rfdptr);

	if(wfdptr == NULL || rfdptr == NULL)
		return -EINVAL;

	wfd = *wfdptr;
	rfd = *rfdptr;

	if (wfd >= PROCESS_IOMAX || rfd >= PROCESS_IOMAX)
		return -EBADFD;

	if (wfd < 0) {
		for (wfd = 0; wfd < PROCESS_IOMAX; wfd++) {
			if (current_process()->iotab[wfd] == NULL)
				break;
		}
	}

	// TODO: they cannot be equal so either need to start
	// where wfdptr left off or add condition

	if (rfd < 0) {
		for (rfd = 0; rfd < PROCESS_IOMAX; rfd++) {
			if (rfd != wfd &&
				current_process()->iotab[rfd] == NULL)
			{
				break;
			}
		}
	}

	if (wfd == PROCESS_IOMAX || rfd == PROCESS_IOMAX)
		return -EMFILE;

	// pipe is invalid if both fds are equal
	if (wfd == rfd)
		return -EINVAL;

	create_pipe(&current_process()->iotab[wfd],
			&current_process()->iotab[rfd]);

	*wfdptr = wfd;
	*rfdptr = rfd;

    return 0;
}

// Allocates a new file descriptor that refers to the same open
// file description as the descriptor oldfd.
int sysiodup(int oldfd, int newfd) {
	trace("%s(oldfd=%d, newfd=%d)", __func__, oldfd, newfd);

	if (current_process()->iotab[oldfd] == NULL)
		return -EMFILE;

	if (oldfd < 0 || oldfd >= PROCESS_IOMAX || newfd >= PROCESS_IOMAX)
		return -EBADFD;

	if (newfd < 0) {
		for (newfd = 0; newfd < PROCESS_IOMAX; newfd++) {
			if (current_process()->iotab[newfd] == NULL)
				break;
		}
	}

	if (newfd >= PROCESS_IOMAX)
		return -EMFILE;

	current_process()->iotab[newfd] =
		ioaddref(current_process()->iotab[oldfd]);

	return newfd;
}
