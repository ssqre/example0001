# Common Makefile

ifeq ($(KERNELRELEASE),)

    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers *.order

.PHONY: modules modules_install clean

else

    obj-m	:= pci_device.o
#    hello-objs := hello.o \

endif
