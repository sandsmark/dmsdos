# If you want or need to change something herein, please look approx. 30 lines
# below for the 'User Config Section'

#
# Okay, that's it :)
########################################################################

########################################################################
# *** User Config Section ***
# Here you can look for settings to change if the code refuses to compile
# out of the box. There are so many different systems. So many versions
# of compilers. So many different bugs. :(

#
# CFLAGS for the dmsdos daemon
# note: some macro expansions require at least -O
#
# CFLAGS for the dmsdos library
# note: some macro expansions require at least -O
LCFLAGS= -O -ggdb -D__DMSDOS_LIB__ -DUSE_FLOCK -U__linux__

EXEC_PREFIX=/usr/bin
#
# Okay, that's the end of the User Config Section.
##########################################################################

CFLAGS+= -U__linux__

all: dcread dmsdosfsck mcdmsdos cvflist cvftest dutil

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
	$(CC) -w -E -MM dblspace*.c dstacker*.c > depend

depend:
	@touch depend

clean:
	rm -f *.o *.a *.i *.s *.lo *.do *.so *.so.* dutil dmsdosd dcread \
	      dmsdosfsck cvftest cvflist mcdmsdos

mrproper: clean
	rm -f core *~ depend
	rm -f .config* dmsdos-config.h .menuconfig*

install: all
	install dcread $(EXEC_PREFIX)/dcread
	install dmsdosfsck $(EXEC_PREFIX)/dmsdosfsck
	install mcdmsdos $(EXEC_PREFIX)/mcdmsdos
	install cvflist $(EXEC_PREFIX)/cvflist
	install cvftest $(EXEC_PREFIX)/cvftest

uninstall:
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
