obj-m := srfs.o
srfs-objs := ksrfs.o super.o inode.o file.o
CFLAGS_srfs.o := -DDEBUG
CFLAGS_super.o := -DDEBUG
CFLAGS_inode.o := -DDEBUG
CFLAGS_file.o := -DDEBUG

all: ko

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
