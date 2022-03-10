obj-m += vaultfs.o
vaultfs-objs := super.o inode.o file.o dir.o extent.o kprobes/vaultfs_kprobes.o syscalls/lib/usctm.o syscalls/lib/vtpmo.o syscalls/vaultfs_syscalls.o vaultfs_src.o device/vaultfs_controller.o 

KDIR ?= /lib/modules/$(shell uname -r)/build

MKFS = mkfs.vaultfs

IMAGE ?= test.img
PASSWORD = 1234
IMAGESIZE ?= 1000
FLAGS = -lcrypto # do not use LDFLAGS as a name, kernel 4 will try to link that name with the kernel module too

all: $(MKFS) $(IMAGE)
	make -C $(KDIR) M=$(PWD) modules

module:
	make -C $(KDIR) M=$(PWD) modules

$(MKFS):  mkfs.c
	$(CC) -std=gnu99 -Wall -o $@ $< $(FLAGS)

$(IMAGE): $(MKFS)
	dd if=/dev/zero of=${IMAGE} bs=1M count=${IMAGESIZE}
	./$< $(IMAGE) $(PASSWORD)

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(MKFS) $(IMAGE)

.PHONY: all clean