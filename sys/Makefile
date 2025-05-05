# Makefile
#

PREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(PREFIX)gcc
AS=$(PREFIX)as
LD=$(PREFIX)ld
OBJCOPY=$(PREFIX)objcopy
OBJDUMP=$(PREFIX)objdump

OBJS = \
	assert.o \
	console.o \
	thread.o \
	device.o \
	elf.o \
	error.o \
	excp.o \
	heap0.o \
	intr.o \
	io.o \
	plic.o \
	see.o \
	start.o \
	string.o \
	timer.o \
	trap.o \
	thrasm.o \
	dev/viorng.o \
	dev/virtio.o \
	dev/rtc.o \
	dev/uart.o \
	ktfs.o \
	dev/vioblk.o \
	dev/obj/viogpu.o \
	dev/obj/viohi.o \
	cache.o \
	blob.o \
	memory.o \
	process.o \
	syscall.o \

BLOB_OBJCOPY_FLAGS = \
	--add-section .rodata.blob=blob.raw \
	--set-section-flags .rodata.blob=alloc,contents,load,readonly

CFLAGS = -Wall -Werror=implicit-function-declaration
CFLAGS += -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.

#CFLAGS += -DDEBUG #-DTRACE # Everything!
#CFLAGS += -DMEMORY_DEBUG -DMEMORY_TRACE
#CFLAGS += -DHEAP_DEBUG -DHEAP_TRACE
#CFLAGS += -DEZFS_DEBUG -DEZFS_TRACE
#CFLAGS += -DTHREAD_DEBUG -DTHREAD_TRACE
#CFLAGS += -DMAIN_DEBUG -DMAIN_TRACE
#CFLAGS += -DTIMER_DEBUG -DTIMER_TRACE
#CFLAGS += -DCACHE_DEBUG -DCACHE_TRACE
#CFLAGS += -DKTFS_DEBUG -DKTFS_TRACE
#CFLAGS += -DVIOBLK_DEBUG -DVIOBLK_TRACE
#CFLAGS += -DELF_DEBUG -DELF_TRACE
#CFLAGS += -DSYSCALL_DEBUG -DSYSCALL_TRACE
#CFLAGS += -DPROCESS_DEBUG -DPROCESS_TRACE
#CFLAGS += -DEXCP_DEBUG -DEXCP_TRACE

ASFLAGS = -march=rv64imazicsr

LDFLAGS = -melf64lriscv

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -bios none -nographic

# viorng device
QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0

# vioblk device
QEMUOPTS += -drive file=ktfs.raw,id=blk0,if=none,format=raw,readonly=false
QEMUOPTS += -device virtio-blk-device,drive=blk0

# viogpu device
QEMUOPTS += -device virtio-gpu-device -display gtk
QEMUOPTS += -monitor pty

# viohi device
QEMUOPTS += -device virtio-keyboard-device -device virtio-tablet-device

# serial device
QEMUOPTS += -serial mon:stdio
QEMUOPTS += -serial pty
QEMUOPTS += -serial pty
QEMUOPTS += -serial pty
QEMUOPTS += -serial pty

# console
QEMUOPTS += -monitor pty

all: kernel.elf mkfs_ktfs

kernel.elf: $(OBJS) main.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run: kernel.elf mkfs_ktfs
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

SIZE=16M
INODES=32

mkfs_ktfs:
	./../util/fs/mkfs_ktfs ktfs.raw $(SIZE) $(INODES) ../usr/bin/*

debug: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

blob.o:
	echo .end | $(AS) $(ASFLAGS) -o blob.o
	[ ! -f blob.raw ] || $(OBJCOPY) $(BLOB_OBJCOPY_FLAGS) $@

clean:
	rm -rf *.o dev/*.o test/*.o demo/*.o *.elf *.raw

# TEST TARGETS
#

# lock
test-lock.elf: $(OBJS) test/lock.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-lock: test-lock.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-lock: test-lock.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# memio
test-memio.elf: $(OBJS) test/memio.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-memio: test-memio.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-memio: test-memio.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# elf
test-elf.elf: $(OBJS) test/elf.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-elf: test-elf.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-elf: test-elf.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# viorng
test-viorng.elf: $(OBJS) test/viorng.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-viorng: test-viorng.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< | python3 test/viorng.py

debug-test-viorng: test-viorng.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s | python3 test/viorng.py

# vioblk
test-vioblk.elf: $(OBJS) test/vioblk.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-vioblk: test-vioblk.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-vioblk: test-vioblk.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# cache
test-cache.elf: $(OBJS) test/cache.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-cache: test-cache.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-cache: test-cache.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# ktfs
test-ktfs.elf: $(OBJS) test/ktfs.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-ktfs: test-ktfs.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-ktfs: test-ktfs.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# memory
test-memory.elf: $(OBJS) test/memory.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-memory: test-memory.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-memory: test-memory.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

# pipes
test-pipes.elf: $(OBJS) test/pipes.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

test-pipes: test-pipes.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test-pipes: test-pipes.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s
