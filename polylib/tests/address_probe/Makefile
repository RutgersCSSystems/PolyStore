KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build

obj-m += example_module.o

all: example_user example_module.ko

example_module.ko: example_module.c
	make -C $(KERNEL_SOURCE) M=$(PWD) modules

example_user: example_user.c
	gcc -o example_user example_user.c

clean:
	make -C $(KERNEL_SOURCE) M=$(PWD) clean
	rm -f example_user

