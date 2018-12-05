# If you want or need to change something herein, please look approx. 30 lines
# below for the 'User Config Section'

########################################################################
# The next lines are hacks for automatic detection of different kernel setups.
# These things must happen first, please don't change them here. They can be
# overridden later.
#
# we try to guess some settings from kernel configuration :) 
-include /usr/src/linux/.config
#
# guess some special flags for CPU type - used for optimization only
CPUFLAGS=-U__linux__ -fPIC
ifeq ($(CONFIG_M386),y)
CPUFLAGS= -m386 -DCPU=386 -DUSE_ASM
endif
ifeq ($(CONFIG_M486),y)
CPUFLAGS= -m486 -DCPU=486 -DUSE_ASM
endif
ifeq ($(CONFIG_M586),y)
CPUFLAGS= -m486 -DCPU=586 -DUSE_ASM
endif
# this one is new in 2.2.x kernels...
ifeq ($(CONFIG_M586TSC),y)
CPUFLAGS= -m486 -DCPU=586 -DUSE_ASM
endif
ifeq ($(CONFIG_M686),y)
CPUFLAGS= -m486 -DCPU=686 -DUSE_ASM
endif
# this one is new in 2.3.x kernels...
ifeq ($(CONFIG_MK6),y)
CPUFLAGS= -m486 -DCPU=586 -DUSE_ASM
endif
ifeq ($(CONFIG_MK7),y)
CPUFLAGS= -m486 -DCPU=686 -DUSE_ASM
endif
#
# Okay, that's it :)
########################################################################

########################################################################
# *** User Config Section ***
# Here you can look for settings to change if the code refuses to compile
# out of the box. There are so many different systems. So many versions
# of compilers. So many different bugs. :(
#
# Select compiler
CC=gcc
#
# If cpu specific optimization fails, uncomment the next line to switch it
# off - there are some gas versions around that don't like the cpu specific
# asm instructions in dmsdos...
# CPUFLAGS=
#
# Some gnu assembler versions have a bug in the syntax for the xlat 
# instruction. This enables a workaround. It should only be needed for very
# old binutils versions. I don't know for which ones :(
# CPUFLAGS+= -DGAS_XLAT_BUG
#
# The next lines should detect SMP configuration automatically.
# Comment them out or set it manually if it fails.
ifeq ($(SMP),1)
CPUFLAGS+= -D__SMP__
endif
# 2.2.x kernels have this one...
ifeq ($(CONFIG_SMP),y)
CPUFLAGS+= -D__SMP__
endif
#
# The next lines are for libc6 and enable some compatibility hacks -
# uncomment the line with -D__FOR_LIBC6 if the code does not compile
# *** Currently not used - code should compile under libc6 without hacks
LIBC6FLAGS=
#LIBC6FLAGS= -D__FOR_LIBC6
#
# CFLAGS for dmsdos module
# note: some macro expansions require at least -O
CFLAGS= -Wstrict-prototypes -O3 -fomit-frame-pointer
CFLAGS+= $(CPUFLAGS)
#
# The next lines add some stuff automatically for people using modversions
# if they fail, comment them out and set the required flags manually
ifeq ($(CONFIG_MODVERSIONS),y)
CFLAGS+= -DMODVERSIONS -include /usr/include/linux/modversions.h
endif
#
# CFLAGS for the dmsdos daemon
# note: some macro expansions require at least -O
DCFLAGS= -O3 -D__DMSDOS_DAEMON__
DCFLAGS+= $(CPUFLAGS)
#
# CFLAGS for the dmsdos library
# note: some macro expansions require at least -O
LCFLAGS= -O -ggdb -D__DMSDOS_LIB__ -DUSE_FLOCK
LCFLAGS+= $(CPUFLAGS) $(LIBC6FLAGS)
# uncomment the next line if you want a shared library
#LIB_SHARED = 1
#
# locations where to install the module, the executables and the manpages
# note: `uname -r` expands to the *currently running* kernel version - if it
# is different from that in /usr/src/linux you'd better edit the next line :)
MODULE_PREFIX=/lib/modules/`uname -r`/fs
#MODULE_PREFIX=/lib/modules/misc
EXEC_PREFIX=/usr/local/bin
#
# Okay, that's the end of the User Config Section.
##########################################################################

##########################################################################
# The rest of this file are rules how to build which programs.

without_kernel: dcread dmsdosfsck mcdmsdos cvflist cvftest

all: dmsdos-config.h dmsdos.o dutil dmsdosd $(LIBDMSDOS) dcread dmsdosfsck \
     mcdmsdos cvflist cvftest

min: dmsdos-config.h dmsdos.o dutil

dmsdos-config.h:
	/bin/bash conf-wrapper.sh
	make dep

config:
	/bin/bash Configure
	make dep

menuconfig:
	/bin/bash Menuconfig
	make dep

dep:
	./check.sh
	$(CC) -w -E -MM -D__KERNEL__ -D__MODULE__ -DMODULE dblspace*.c dstacker*.c > depend

depend:
	@touch depend

DBL_OBJS =  dblspace_compr.o dblspace_tables.o \
	    dblspace_alloc.o dblspace_dec.o dblspace_virtual.o \
	    dblspace_buffer.o dblspace_methsq.o \
	    dblspace_chk.o dblspace_interface.o \
	    dstacker_alloc.o dstacker_compr.o dstacker_dec.o \
	    dblspace_fileops.o dblspace_ioctl.o

$(DBL_OBJS): dmsdos.h dmsdos-config.h

dblspace_fileops.o : dblspace_fileops.c dblspace_fileops.c-2.0.33 \
		     dblspace_fileops.c-2.1.80 dblspace_fileops.c-2.3.99

dmsdos.o: $(DBL_OBJS)
	ld -r -o dmsdos.o $(DBL_OBJS)

dutil: dutil.c dmsdos.h dmsdos-config.h
	$(CC) $(DCFLAGS) -o dutil dutil.c

DAEMON_OBJS = daemon_actions.do dblspace_compr.do dstacker_compr.do \
	      dblspace_methsq.do

$(DAEMON_OBJS): dmsdos.h dmsdos-config.h

%.do: %.c ; $(CC) -o $@ $(DCFLAGS) -c $<

dmsdosd: $(DAEMON_OBJS)
	$(CC) -o dmsdosd $^

clean:
	rm -f *.o *.a *.i *.s *.lo *.do *.so *.so.* dutil dmsdosd dcread \
	      dmsdosfsck cvftest cvflist mcdmsdos

mrproper: clean
	rm -f core *~ depend
	rm -f .config* dmsdos-config.h .menuconfig*

install: all
	./check.sh
	install dcread $(EXEC_PREFIX)/dcread
	install dmsdosfsck $(EXEC_PREFIX)/dmsdosfsck
	install mcdmsdos $(EXEC_PREFIX)/mcdmsdos
	install cvflist $(EXEC_PREFIX)/cvflist
	install cvftest $(EXEC_PREFIX)/cvftest

install-min: min
	./check.sh
	install dmsdos.o $(MODULE_PREFIX)/dmsdos.o
	install dutil $(EXEC_PREFIX)/dutil

uninstall:
	rm -f $(MODULE_PREFIX)/dmsdos.o
	rm -f $(EXEC_PREFIX)/dutil
	rm -f $(EXEC_PREFIX)/dmsdosd
	rm -f $(EXEC_PREFIX)/dmsdosfsck
	rm -f $(EXEC_PREFIX)/mcdmsdos
	rm -f $(EXEC_PREFIX)/cvflist
	rm -f $(EXEC_PREFIX)/cvftest

messages:
	grep DMSDOS ../doc/messages.doc | sort -d -b -f > /tmp/messdoc
	./listmsg.sh -LOG > /tmp/messsrc
	diff -d -U 0 -w /tmp/messdoc /tmp/messsrc | grep DMSDOS

LIB_OBJS =  lib_interface.lo dblspace_interface.lo dblspace_dec.lo \
	    dblspace_compr.lo dblspace_methsq.lo dblspace_alloc.lo \
	    dblspace_chk.lo dblspace_tables.lo dstacker_compr.lo \
	    dstacker_dec.lo dstacker_alloc.lo

$(LIB_OBJS): dmsdos.h dmsdos-config.h lib_interface.h

ifndef LIB_SHARED

LIBDMSDOS = libdmsdos.a

%.lo: %.c ; $(CC) -o $@ $(LCFLAGS) -c $<

$(LIBDMSDOS): $(LIB_OBJS)
	ar rcs $(LIBDMSDOS) $^
	ranlib $(LIBDMSDOS)

else

%.lo: %.c ; $(CC) --shared -o $@ $(LCFLAGS) -c $<

LIBDMSDOS = libdmsdos.so.0.9.2

$(LIBDMSDOS): $(LIB_OBJS)
	gcc --shared -Xlinker --soname=$(LIBDMSDOS) -o $(LIBDMSDOS) $^
	ln -s $(LIBDMSDOS) libdmsdos.so

endif

dcread: dcread.c $(LIBDMSDOS) dmsdos.h dmsdos-config.h

mcdmsdos: mcdmsdos.c $(LIBDMSDOS) dmsdos.h dmsdos-config.h

dmsdosfsck: dmsdosfsck.c $(LIBDMSDOS) dmsdos.h dmsdos-config.h

cvftest: cvftest.c

cvflist: cvflist.c

-include depend
