@echo off
rem this file compiles dmsdos under win32. sorry I did not manage to get a
rem makefile work with nmake :( 
rem 
rem I had success with visual c++ 5.0 from microsoft

rem please adapt the include and library paths here if necessary
rem set CFLAGS=-I d:\programme\devstudio\vc\include -D__DMSDOS_LIB__ -c
rem set LFLAGS=/libpath:d:\programme\devstudio\vc\lib
set CFLAGS=-D__DMSDOS_LIB__ -DUSE_SOPEN -c
set LFLAGS=

rem check for existing configuration file
if exist dmsdos-config.h goto isthere
copy dmsdos-config.h.default dmsdos-config.h
:isthere

del *.obj

cl %CFLAGS% lib_interface.c
cl %CFLAGS% dblspace_interface.c
cl %CFLAGS% dblspace_dec.c
cl %CFLAGS% dblspace_compr.c
cl %CFLAGS% dblspace_methsq.c
cl %CFLAGS% dblspace_alloc.c
cl %CFLAGS% dblspace_chk.c
cl %CFLAGS% dblspace_tables.c
cl %CFLAGS% dstacker_compr.c
cl %CFLAGS% dstacker_dec.c
cl %CFLAGS% dstacker_alloc.c

rem dmsdos_library.lo: $(LIB_OBJS)
rem        ld -r -o dmsdos_library.lo $^
rem we don't want this here

rem libdmsdos.a: dmsdos_library.lo
rem        ar rcs libdmsdos.a dmsdos_library.lo

del libdmsdos.lib
lib /out:libdmsdos.lib lib_interface.obj dblspace_interface.obj dblspace_dec.obj dblspace_compr.obj dblspace_methsq.obj dblspace_alloc.obj dblspace_chk.obj dblspace_tables.obj dstacker_compr.obj dstacker_dec.obj dstacker_alloc.obj

rem
rem libdmsdos.so: dmsdos_library.lo
rem        ld -shared -o libdmsdos.so dmsdos_library.lo
rem

rem dcread: dcread.c libdmsdos.a dmsdos.h dmsdos-config.h
rem        $(CC) -Wall -ggdb -o dcread dcread.c -L. -ldmsdos
cl %CFLAGS% dcread.c 
link %LFLAGS% dcread.obj libdmsdos.lib

rem 
rem mcdmsdos: mcdmsdos.c libdmsdos.a dmsdos.h dmsdos-config.h
rem        $(CC) -Wall -ggdb -o mcdmsdos mcdmsdos.c -L. -ldmsdos

cl %CFLAGS% mcdmsdos.c 
link %LFLAGS% mcdmsdos.obj libdmsdos.lib

rem
rem dmsdosfsck: dmsdosfsck.c libdmsdos.a dmsdos.h dmsdos-config.h
rem        $(CC) -Wall -o dmsdosfsck dmsdosfsck.c -L. -ldmsdos

rem cl %CFLAGS% dmsdosfsck.c
rem link %LFLAGS% dmsdosfsck.obj libdmsdos.lib
rem this does not compile due to missing sleep ARGHH... use Unix, Win32 sucks.
