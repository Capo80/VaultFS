obj-m += ransomfs.o
ransomfs-objs := ransomfs_src.o super.o inode.o file.o dir.o extent.o

KDIR ?= /lib/modules/$(shell uname -r)/build

MKFS = mkfs.ransomfs

IMAGE ?= test.img
IMAGESIZE ?= 1000

all: $(MKFS) $(IMAGE)
	make -C $(KDIR) M=$(PWD) modules

$(MKFS): mkfs.c
	$(CC) -std=gnu99 -Wall -o $@ $<

$(IMAGE): $(MKFS)
	dd if=/dev/zero of=${IMAGE} bs=1M count=${IMAGESIZE}
	./$< $(IMAGE)

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(MKFS) $(IMAGE)

.PHONY: all clean