/*
dblspace_buffer.c 

DMSDOS CVF-FAT module: low-level buffered read-write access functions

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

#ifndef __KERNEL__
#error This file needs __KERNEL__
#endif

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include "dmsdos.h"

/* This is just cut'n'paste from the fat fs original :) 
   We don't do a virtual sector translation here */

struct buffer_head *raw_bread (
	struct super_block *sb,
	int block)
{
	struct buffer_head *ret = NULL;

	/* Note that the blocksize is 512 or 1024, but the first read
	   is always of size 1024. Doing readahead may be counterproductive
	   or just plain wrong. */
	if (sb->s_blocksize == 512) {
		ret = bread (sb->s_dev,block,512);
	} else {
		struct buffer_head *real = bread (sb->s_dev,block>>1,1024);

		if (real != NULL){
			ret = (struct buffer_head *)
			  kmalloc (sizeof(struct buffer_head), GFP_KERNEL);
			if (ret != NULL) {
				/* #Specification: msdos / strategy / special device / dummy blocks
					Many special device (Scsi optical disk for one) use
					larger hardware sector size. This allows for higher
					capacity.

					Most of the time, the MsDOS file system that sit
					on this device is totally unaligned. It use logically
					512 bytes sector size, with logical sector starting
					in the middle of a hardware block. The bad news is
					that a hardware sector may hold data own by two
					different files. This means that the hardware sector
					must be read, patch and written almost all the time.

					Needless to say that it kills write performance
					on all OS.

					Internally the linux msdos fs is using 512 bytes
					logical sector. When accessing such a device, we
					allocate dummy buffer cache blocks, that we stuff
					with the information of a real one (1k large).

					This strategy is used to hide this difference to
					the core of the msdos fs. The slowdown is not
					hidden though!
				*/
				/*
					The memset is there only to catch errors. The msdos
					fs is only using b_data
				*/
				memset (ret,0,sizeof(*ret));
				ret->b_data = real->b_data;
				if (block & 1) ret->b_data += 512;
				ret->b_next = real;
			}else{
				brelse (real);
			}
		}
	}
	return ret;
}
struct buffer_head *raw_getblk (
	struct super_block *sb,
	int block)
{
	struct buffer_head *ret = NULL;
	if (sb->s_blocksize == 512){
		ret = getblk (sb->s_dev,block,512);
	}else{
		/* #Specification: msdos / special device / writing
			A write is always preceded by a read of the complete block
			(large hardware sector size). This defeat write performance.
			There is a possibility to optimize this when writing large
			chunk by making sure we are filling large block. Volunteer ?
		*/
		ret = raw_bread (sb,block);
	}
	return ret;
}

void raw_brelse (
	struct super_block *sb,
	struct buffer_head *bh)
{
	if (bh != NULL){
		if (sb->s_blocksize == 512){
			brelse (bh);
		}else{
			brelse (bh->b_next);
			/* We can free the dummy because a new one is allocated at
				each fat_getblk() and fat_bread().
			*/
			kfree (bh);
		}
	}
}
	
void raw_mark_buffer_dirty (
	struct super_block *sb,
	struct buffer_head *bh,
	int dirty_val)
{

#ifdef DBL_WRITEACCESS
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	mark_buffer_dirty (bh,dirty_val);
#else
printk(KERN_NOTICE "DMSDOS: write access not compiled in, ignored\n");
#endif
    
}

void raw_set_uptodate (
	struct super_block *sb,
	struct buffer_head *bh,
	int val)
{
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	mark_buffer_uptodate(bh, val);
}
int raw_is_uptodate (
	struct super_block *sb,
	struct buffer_head *bh)
{
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	return buffer_uptodate(bh);
}

/* we really need this for read-ahead */
void raw_ll_rw_block (
	struct super_block *sb,
	int opr,
	int nbreq,
	struct buffer_head *bh[32])
{
	if (sb->s_blocksize == 512){
		ll_rw_block(opr,nbreq,bh);
	}else{
		struct buffer_head *tmp[32];
		int i;
		for (i=0; i<nbreq; i++){
			tmp[i] = bh[i]->b_next;
		}
		ll_rw_block(opr,nbreq,tmp);
	}
}
