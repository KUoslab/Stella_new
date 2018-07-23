obj-m := gos_start.o gos.o disk_stat.o oios-iosched.o vif.o

KDIR := /lib/modules/$(shell uname -r)/build
#KBUILD_EXTRA_SYMBOLS += ./Module.symvers

#CFLAG_lkm_test.o := -lrt

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
