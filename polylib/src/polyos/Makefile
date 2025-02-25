KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
LIB_DIR := $(abspath ../)
BUILD_DIR := $(abspath ./build/)

obj-m := polystore_controller.o
polystore_controller-y := polyos.o inode.o operation.o taskctx.o mm.o #persist.o

all: polystore_controller polystore_controller_large_index

polystore_controller:
	make -C $(KERNEL_SOURCE) M=$(PWD) modules

polystore_controller_large_index:
	make -C $(KERNEL_SOURCE) M=$(PWD) EXTRA_CFLAGS="-DPOLYSTORE_LARGE_INDEX" modules
	cp polystore_controller.ko polystore_controller_large_index.ko	

clean:
	make -C $(KERNEL_SOURCE) M=$(PWD) clean
