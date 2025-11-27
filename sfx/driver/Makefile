obj-m += mctp_bridge.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f demo send *.o
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean


demo:
	gcc demo.c pldm.c -o demo

send:
	gcc send.c -o send
