# Assumptions:
# 	This makefile assumes that kernel modules are installed in 
#	      /lib/modules/'uname -r'/.
#	Should the assumption be incorrect, please use:
#	    make MODULE_ROOT="/path/to/modules/for/your/kernel"
#	to override assumed path

MODULE=sur40

srcs := $(MODULE).c
obj-m := $(srcs:%.c=%.o)

MODULE_ROOT:= /lib/modules/$(shell uname -r)
BUILDDIR   := $(MODULE_ROOT)/build
MODDIR     := $(MODULE_ROOT)/updates
PWD        := $(shell pwd)

override install_targets := $(srcs:%.c=install_%)

default:
	$(MAKE) -C $(BUILDDIR) SUBDIRS=$(PWD) "obj-m=$(obj-m)" modules

.PHONY: default install all clean help $(install_targets)

install:
	install -m 644 $(MODULE).ko $(MODDIR)
	depmod -a
	modprobe -r $(MODULE)
	modprobe $(MODULE)

debug:
	@echo
	@echo "srcs="$(srcs)
	@echo "obj-m="$(obj-m)
	@echo "MODULE_ROOT="$(MODULE_ROOT)
	@echo "PWD="$(PWD)
	@echo "install targets: " $(install_targets)
	@echo

clean:
	-rm -rf *.o *.ko $(MODULE)*.mod.c Module.symvers .$(MODULE)* .tmp_versions modules.order

