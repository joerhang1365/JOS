# Based on xv6-riscv Makefile; see LICENSE.

TOOLPREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(TOOLPREFIX)gcc
AS=$(TOOLPREFIX)as
LD=$(TOOLPREFIX)ld
OBJCOPY=$(TOOLPREFIX)objcopy
OBJDUMP=$(TOOLPREFIX)objdump

UMODE := 1

ULIB_OBJS = \
	io.o \
	string.o \
	syscall.o \
	heap.o \

ULIB_LD = no_umode.ld

ifeq ($(UMODE), 1)
	ULIB_OBJS += start.o
	ULIB_LD = umode.ld
endif

ALL_TARGETS = \
	hello \
	syscall \
	skyline \
	fib \
	trekfib \
	trek_wrapper \
	shell \

CFLAGS = -Wall -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64g -mabi=lp64d
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -I.

ifeq ($(UMODE), 1)
	CFLAGS += -DUMODE
endif

all: $(ALL_TARGETS)

bin:
	mkdir $@

clean:
	rm -rf *.o test/*.o *.elf *.asm

# shell
shell: $(ULIB_OBJS) shell.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

hello: $(ULIB_OBJS) test/hello.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

skyline: $(ULIB_OBJS) test/obj/skyline.o test/mp1.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

fib: $(ULIB_OBJS) test/fib.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

trekfib: $(ULIB_OBJS) test/trekfib.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

trek_wrapper: $(ULIB_OBJS) test/trek_wrapper.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

# test syscalls
syscall: $(ULIB_OBJS) test/syscall.o | bin
	$(LD) -T $(ULIB_LD) -o bin/$@ $^

