DFLAGS=-g -DDEBUG
KDIR=/lib/modules/`uname -r`/build

kbuild:
	make -C $(KDIR) M=`pwd`
debug:
	make -C $(KDIR) M=`pwd` EXTRA_CFLAGS="$(DFLAGS)"

clean: 
	make -C $(KDIR) M=`pwd` clean
