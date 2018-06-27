CONFIG_MODULE_SIG=n
KERNEL_DIR=/lib/modules/$(shell uname -r)/build
obj-m = vif.o

KDIR := $(KERNEL_DIR)
PWD :=$(shell pwd)

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	@rm -rf *.ko
	@rm -rf *.mod.*
	@rm -rf .*.cmd
	@rm -rf *.o
	@rm -rf *.ko.*
	@rm -rf modules.order
	@rm -rf Module.symvers
	@rm -rf .tmp_versions


