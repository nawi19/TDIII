-include .config

ccflags-$(CONFIG_HELLO_VERBOSE) += -DCONFIG_HELLO_VERBOSE

obj-m += hello.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
