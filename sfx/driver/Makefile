obj-m += mctp_bridge.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f test
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean


test:
	gcc test.c -o test
