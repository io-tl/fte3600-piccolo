obj-m += focal_spi.o

KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
M ?= $(CURDIR)

.PHONY: all modules clean install

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(M) modules

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

install: modules
	$(MAKE) -C $(KDIR) M=$(M) modules_install
	depmod -a $(KERNELRELEASE)
