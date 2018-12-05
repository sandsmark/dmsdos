/*
dblspace_fileops.c

DMSDOS CVF-FAT module: file operation routines.

******************************************************************************
DMSDOS (compressed MSDOS filesystem support) for Linux
written 1995-1998 by Frank Gockel and Pavel Pisa

    (C) Copyright 1995-1998 by Frank Gockel
    (C) Copyright 1996-1998 by Pavel Pisa

Some code of dmsdos has been copied from the msdos filesystem
so there are the following additional copyrights:

    (C) Copyright 1992,1993 by Werner Almesberger (msdos filesystem)
    (C) Copyright 1994,1995 by Jacques Gelinas (mmap code)
    (C) Copyright 1992-1995 by Linus Torvalds

DMSDOS was inspired by the THS filesystem (a simple doublespace
DS-0-2 compressed read-only filesystem) written 1994 by Thomas Scheuermann.

The DMSDOS code is distributed under the Gnu General Public Licence.
See file COPYING for details.
******************************************************************************

*/

/* UUHHHH .... too much has changed in 2.1.xx... we need a complete rewrite */

#include"dmsdos.h"

#ifdef __FOR_KERNEL_2_3_99
#include"dblspace_fileops.c-2.3.99"
#else
#ifdef __FOR_KERNEL_2_1_80
#include"dblspace_fileops.c-2.1.80"
#else
#include"dblspace_fileops.c-2.0.33"
#endif
#endif
