# SPDX-License-Identifier: GPL-2.0
# Makefile for CXL Fabric Interconnect

obj-m += cxl_interconnect.o

cxl_interconnect-y := fabric.o
cxl_interconnect-y += switch.o
cxl_interconnect-y += endpoint.o
cxl_interconnect-y += memnet.o
cxl_interconnect-y += route.o

ccflags-y := -I$(src)/../../../include

# Build targets
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

.PHONY: all clean install
