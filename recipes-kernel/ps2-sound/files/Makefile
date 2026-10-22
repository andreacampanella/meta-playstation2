# The kernel module snd-ps2, built outside the kernel tree.
obj-m := snd-ps2.o

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
