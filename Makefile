obj-m = dtu-driver.o
KERNEL_SRC := /home/c674s876/Documents/firesim/firesim/target-design/chipyard/software/firemarshal/boards/firechip/linux/
CROSS_COMPILE := riscv64-unknown-linux-gnu-
ARCH := riscv
PWD := $(CURDIR)
EXTRA_CFLAGS += -DFIRESIM
all: user
	make -C $(KERNEL_SRC) M=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE)  ARCH=$(ARCH) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

user:
	riscv64-unknown-linux-gnu-gcc dtu-driver-test.c -o dtu-driver-test

clean:
	rm *.o *.ko *.mod.*