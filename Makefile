# Makefile - MyOS ビルドシステム (gcc + GAS版)
CC  = gcc
AS  = as
LD  = ld

CFLAGS = -std=gnu99 -m32 -ffreestanding -O2 -Wall -Wextra \
         -Wno-unused-parameter -Wno-unused-function -Wno-incompatible-pointer-types \
         -Iinclude -fno-stack-protector -fno-builtin \
         -nostdlib -nostdinc -fno-pie -fno-pic \
         -fno-omit-frame-pointer

ASFLAGS = --32

LDFLAGS = -T linker.ld -nostdlib -n -m elf_i386

# Cソース
C_SRCS = \
    kernel/main.c \
    kernel/gdt.c \
    kernel/idt.c \
    mm/pmm.c \
    mm/vmm.c \
    mm/heap.c \
    proc/proc.c \
    fs/vfs.c \
    fs/ramfs.c \
    drivers/tty.c \
    drivers/irq.c \
    syscall/syscall.c \
    libc/libc.c \
    userland/sh.c \
    userland/exec.c

# アセンブリソース (.S = GAS)
S_SRCS = \
    boot/boot.S \
    kernel/isr_stubs.S \
    proc/switch.S

C_OBJS = $(C_SRCS:.c=.o)
S_OBJS = $(S_SRCS:.S=.o)
OBJS   = $(S_OBJS) $(C_OBJS)

KERNEL = myos.bin
ISO    = myos.iso

.PHONY: all clean iso run run-serial run-debug check-deps

all: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo ""
	@echo "=============================="
	@echo "  Build SUCCESS: $(KERNEL)"
	@echo "=============================="
	@size $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	@printf 'set timeout=0\nset default=0\nmenuentry "MyOS" {\n    multiboot /boot/myos.bin\n    boot\n}\n' \
	    > isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir 2>/dev/null
	@echo "ISO: $(ISO)"

run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std -no-reboot -no-shutdown

run-serial: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std -serial stdio -no-reboot -no-shutdown

run-debug: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std -serial stdio -s -S -no-reboot -no-shutdown &
	gdb $(KERNEL) -ex "target remote :1234" -ex "break kernel_main" -ex "continue"

clean:
	rm -f $(OBJS) $(KERNEL) $(ISO)
	rm -rf isodir

check-deps:
	@$(CC) -m32 -ffreestanding -nostdlib -nostartfiles -fno-pie -x c /dev/null -o /dev/null 2>/dev/null \
	    && echo "[OK] gcc -m32 support" \
	    || echo "[ERR] -m32 not supported - install: sudo apt install gcc-multilib"
	@which qemu-system-i386 > /dev/null 2>&1 \
	    && echo "[OK] qemu-system-i386" \
	    || echo "[WARN] qemu not found - install: sudo apt install qemu-system-x86"
