.PHONY: all clean install dkms_install

MODULE_NAME ?= r1000v1_rs485_autoflow
MODULE_VER ?= 0.0.2

obj-m := $(MODULE_NAME).o
ccflags-y := -std=gnu11 -Wno-declaration-after-statement \
	-DMODULE_NAME=\"$(MODULE_NAME)\" \
	-DMODULE_VER=\"$(MODULE_VER)\"

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

DKMSDIR := /usr/src/$(MODULE_NAME)-$(MODULE_VER)
DKMSSRC := $(MODULE_NAME).c
DKMSCONF := dkms.conf

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	@depmod -a

dkms_install:
	@mkdir -p $(DKMSDIR)
	@install -m 644 ./Makefile $(DKMSDIR)
	@install -m 644 $(DKMSSRC) $(DKMSDIR)
	@install -m 644 $(DKMSCONF) $(DKMSDIR)
	@dkms add -m $(MODULE_NAME) -v $(MODULE_VER)
	@dkms build -m $(MODULE_NAME) -v $(MODULE_VER)
	@dkms install -m $(MODULE_NAME) -v $(MODULE_VER)
