/*
lib_interface.c

DMSDOS library: interface functions, hacks, dummies, and fakes.

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

#include<stdio.h>
#include<string.h>
#include<malloc.h>
#include<asm/unaligned.h>
#include<asm/types.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/file.h>
#include<sys/stat.h>
#include<mntent.h>

#define fat_boot_sector msdos_boot_sector

/* some interface hacks */
#include"lib_interface.h"
#undef MALLOC
#undef FREE
#undef CURRENT_TIME
#undef memcpy
#undef memset
#define MALLOC malloc
#define FREE free
#define kmalloc(x,y) malloc(x)
#define kfree free
#define CURRENT_TIME time(NULL)

#include"dmsdos.h"

#define CF_LE_W(v) (v)
#define CF_LE_L(v) (v)
#define CT_LE_W(v) (v)
#define CT_LE_L(v) (v)
#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

long int blk_size[1][1];

extern Acache mdfat[];
extern Acache dfat[];
extern Acache bitfat[];

/* hacks for the low-level interface */

#include <stdarg.h>
int printk(const char *fmt, ...)
{ va_list ap;
  char buf[500];
  char*p=buf;
  int i;

  va_start(ap, fmt);
  i=vsnprintf(buf, 500, fmt, ap);
  va_end(ap);

  if(p[0]=='<'&&p[1]>='0'&&p[1]<='7'&&p[2]=='>')p+=3;
  if(strncmp(p,"DMSDOS: ",8)==0)p+=8;
  fprintf(stderr,"libdmsdos: %s",p);

  return i;
}

void panic(const char *fmt, ...)
{ va_list ap;
  char buf[500];
  int i;

  va_start(ap, fmt);
  i=vsnprintf(buf, 500, fmt, ap);
  va_end(ap);

  fprintf(stderr,"libdmsdos panic: %s",buf);
  
  exit(1);
}

struct buffer_head* raw_bread(struct super_block*sb,int block)
{ struct buffer_head*bh;
  int fd=sb->s_dev;
  
  if(lseek(fd,block*512,SEEK_SET)<0)return NULL; 
  bh=malloc(sizeof(struct buffer_head));
  if(bh==NULL)return NULL;
    
  bh->b_data=malloc(512);
  if(bh->b_data==NULL)
  { free(bh);
    return NULL;
  }
  
  bh->b_blocknr=block;

  if(read(fd,bh->b_data,512)==512)return bh;
  
  free(bh->b_data);
  free(bh);
  
  return NULL;
}

struct buffer_head* raw_getblk(struct super_block*sb,int block)
{ struct buffer_head*bh;
  int fd=sb->s_dev;
  
  if(lseek(fd,block*512,SEEK_SET)<0)return NULL; 
  bh=malloc(sizeof(struct buffer_head));
  if(bh==NULL)return NULL;
    
  bh->b_data=malloc(512);
  if(bh->b_data==NULL)
  { free(bh);
    return NULL;
  }
  
  bh->b_blocknr=block;  
  return bh;
}

void raw_brelse(struct super_block*sb,struct buffer_head*bh)
{ if(bh==NULL)return;
  free(bh->b_data);
  free(bh);
}

void raw_set_uptodate(struct super_block*sb,struct buffer_head*bh,int v)
{ /* dummy */
}

void raw_mark_buffer_dirty(struct super_block*sb,struct buffer_head*bh,int dirty_val)
{ int fd=sb->s_dev;

  if(dirty_val==0)return;
  if(bh==NULL)return;
  
  if(lseek(fd,bh->b_blocknr*512,SEEK_SET)<0)
  { printf("can't seek block %ld\n",bh->b_blocknr);
    return;
  }

#ifdef DBL_WRITEACCESS  
  if(write(fd,bh->b_data,512)!=512)
    printf("writing block %ld failed\n",bh->b_blocknr);
#else
  fprintf(stderr,"DMSDOS: write access not compiled in, ignored\n");
#endif
}

void dblspace_reada(struct super_block*sb, int sector,int count)
{ /* dummy */
}

int try_daemon(struct super_block*sb,int clusternr, int length, int method)
{ return 0;
}

/*okay, first thing is setup super block*/
/* stolen from fatfs */
/* Read the super block of an MS-DOS FS. */

struct super_block *read_super(struct super_block *sb)
{
	struct buffer_head *bh;
	struct fat_boot_sector *b;
	int data_sectors,logical_sector_size,sector_mult,fat_clusters=0;
	int debug=0,error,fat=0;
	int blksize = 512;
	int i;
	char cvf_options[101]="bitfaterrs=nocheck";
	
	MSDOS_SB(sb)->cvf_format=NULL;
	MSDOS_SB(sb)->private_data=NULL;

	blksize = 512;

	bh = raw_bread(sb, 0);
	if (bh == NULL) {
		raw_brelse (sb, bh);
		sb->s_dev = 0;
		printk("FAT bread failed\n");
		return NULL;
	}
	b = (struct fat_boot_sector *) bh->b_data;
/*
 * The DOS3 partition size limit is *not* 32M as many people think.  
 * Instead, it is 64K sectors (with the usual sector size being
 * 512 bytes, leading to a 32M limit).
 * 
 * DOS 3 partition managers got around this problem by faking a 
 * larger sector size, ie treating multiple physical sectors as 
 * a single logical sector.
 * 
 * We can accommodate this scheme by adjusting our cluster size,
 * fat_start, and data_start by an appropriate value.
 *
 * (by Drew Eckhardt)
 */

#define ROUND_TO_MULTIPLE(n,m) ((n) && (m) ? (n)+(m)-1-((n)-1)%(m) : 0)
    /* don't divide by zero */

	logical_sector_size =
		CF_LE_W(get_unaligned((unsigned short *) &b->sector_size));
	sector_mult = logical_sector_size >> SECTOR_BITS;
	MSDOS_SB(sb)->cluster_size = b->cluster_size*sector_mult;
	MSDOS_SB(sb)->fats = b->fats;
	MSDOS_SB(sb)->fat_start = CF_LE_W(b->reserved)*sector_mult;
	MSDOS_SB(sb)->fat_length = CF_LE_W(b->fat_length)*sector_mult;
	MSDOS_SB(sb)->dir_start = (CF_LE_W(b->reserved)+b->fats*CF_LE_W(
	    b->fat_length))*sector_mult;
	MSDOS_SB(sb)->dir_entries =
		CF_LE_W(get_unaligned((unsigned short *) &b->dir_entries));
	MSDOS_SB(sb)->data_start = MSDOS_SB(sb)->dir_start+ROUND_TO_MULTIPLE((
	    MSDOS_SB(sb)->dir_entries << MSDOS_DIR_BITS) >> SECTOR_BITS,
	    sector_mult);
	data_sectors = CF_LE_W(get_unaligned((unsigned short *) &b->sectors));
	if (!data_sectors) {
		data_sectors = CF_LE_L(b->total_sect);
	}
	data_sectors = data_sectors * sector_mult - MSDOS_SB(sb)->data_start;
	error = !b->cluster_size || !sector_mult;
	if (!error) {
		MSDOS_SB(sb)->clusters = b->cluster_size ? data_sectors/
		    b->cluster_size/sector_mult : 0;
		MSDOS_SB(sb)->fat_bits = fat ? fat : MSDOS_SB(sb)->clusters >
		    MSDOS_FAT12 ? 16 : 12;
		fat_clusters = MSDOS_SB(sb)->fat_length*SECTOR_SIZE*8/
		    MSDOS_SB(sb)->fat_bits;
		/* this doesn't compile. I don't understand it either...
		error = !MSDOS_SB(sb)->fats || (MSDOS_SB(sb)->dir_entries &
		    (MSDOS_DPS-1)) || MSDOS_SB(sb)->clusters+2 > fat_clusters+
		    MSDOS_MAX_EXTRA || (logical_sector_size & (SECTOR_SIZE-1))
		    || !b->secs_track || !b->heads;
		*/
	}
	raw_brelse(sb, bh);
	/*
		This must be done after the brelse because the bh is a dummy
		allocated by fat_bread (see buffer.c)
	*/
	sb->s_blocksize = blksize;    /* Using this small block size solves */
				/* the misfit with buffer cache and cluster */
				/* because clusters (DOS) are often aligned */
				/* on odd sectors. */
	sb->s_blocksize_bits = blksize == 512 ? 9 : 10;
	i=-1;
#ifdef DMSDOS_CONFIG_DBL
	if(i<0)
	{ i=detect_dblspace(sb);
	  if(i>=0)i=mount_dblspace(sb,cvf_options);
	}
#endif
#ifdef DMSDOS_CONFIG_STAC
        if(i<0)
        { i=detect_stacker(sb);
          if(i>=0)i=mount_stacker(sb,cvf_options);
        }
#endif
        error=i;
	if (error || debug) {
		/* The MSDOS_CAN_BMAP is obsolete, but left just to remember */
		printk("MS-DOS FS Rel. 12 (hacked for libdmsdos), FAT %d\n",
		       MSDOS_SB(sb)->fat_bits);
		printk("[me=0x%x,cs=%d,#f=%d,fs=%d,fl=%d,ds=%d,de=%d,data=%d,"
		       "se=%d,ts=%ld,ls=%d]\n",b->media,MSDOS_SB(sb)->cluster_size,
		       MSDOS_SB(sb)->fats,MSDOS_SB(sb)->fat_start,MSDOS_SB(sb)->fat_length,
		       MSDOS_SB(sb)->dir_start,MSDOS_SB(sb)->dir_entries,
		       MSDOS_SB(sb)->data_start,
		       CF_LE_W(*(unsigned short *) &b->sectors),
		       (unsigned long)b->total_sect,logical_sector_size);
		printk ("Transaction block size = %d\n",blksize);
	}
	if(i<0) if (MSDOS_SB(sb)->clusters+2 > fat_clusters)
		MSDOS_SB(sb)->clusters = fat_clusters-2;
	if (error) {
			printk("Can't find a valid MSDOS CVF filesystem\n");
		if(MSDOS_SB(sb)->private_data)kfree(MSDOS_SB(sb)->private_data);
		MSDOS_SB(sb)->private_data=NULL;
		return NULL;
	}
	sb->s_magic = MSDOS_SUPER_MAGIC;
	/* set up enough so that it can read an inode */
	MSDOS_SB(sb)->free_clusters = -1; /* don't know yet */
	MSDOS_SB(sb)->fat_wait = NULL;
	MSDOS_SB(sb)->fat_lock = 0;
	MSDOS_SB(sb)->prev_free = 0;
	return sb;
}

void init_daemon(void)
{ /*dummy*/
}

void exit_daemon(void)
{ /*dummy*/
}

void clear_list_dev(struct super_block*sb)
{ /*dummy*/
}

void free_ccache_dev(struct super_block*sb)
{ /*dummy*/
}

void remove_from_daemon_list(struct super_block*sb,int clusternr)
{ /* dummy */
}

static int _wascalled=0;
void do_lib_init(void)
{ int i;

  if(_wascalled)return;
  _wascalled=1;
  
  /* first call of DMSDOS library, initialising variables */

  fprintf(stderr,"DMSDOS library version %d.%d.%d" DMSDOS_VLT
         " compiled " __DATE__ " " __TIME__ " with options:"
#ifndef DBL_WRITEACCESS
         " read-only"
#else
         " read-write"
#endif
#ifdef   USE_XMALLOC
         ", xmalloc"
#else
#ifdef   USE_VMALLOC
         ", vmalloc"
#else
         ", kmalloc"
#endif
#endif
#ifdef DMSDOS_USE_READPAGE
         ", readpage"
#endif
#ifdef   USE_READA_LIST
         ", reada list"
#endif
#ifdef INTERNAL_DAEMON
         ", internal daemon"
#endif
#ifdef DMSDOS_CONFIG_DBLSP_DRVSP
         ", doublespace/drivespace(<3)"
#endif
#ifdef DMSDOS_CONFIG_DRVSP3
         ", drivespace 3"
#endif
#ifdef DMSDOS_CONFIG_STAC3
         ", stacker 3"
#endif
#ifdef DMSDOS_CONFIG_STAC4
         ", stacker 4"
#endif
         "\n",
         DMSDOS_MAJOR,DMSDOS_MINOR,DMSDOS_ACT_REL);

  for(i=0;i<MDFATCACHESIZE;++i)
  {  mdfat[i].a_time=0;
     mdfat[i].a_acc=0;
     mdfat[i].a_buffer=NULL;
  }
  for(i=0;i<DFATCACHESIZE;++i)
  {  dfat[i].a_time=0;
     dfat[i].a_acc=0;
     dfat[i].a_buffer=NULL;
  }
  for(i=0;i<BITFATCACHESIZE;++i)
  {  bitfat[i].a_time=0;
     bitfat[i].a_acc=0;
     bitfat[i].a_buffer=NULL;
  }
}

struct super_block* open_cvf(char*filename,int rwflag)
{ struct super_block *sb;
  int fd;
  long int s;
  struct stat stat1;
  struct stat stat2;
  struct mntent*mn;
  FILE*mnt;
  
  do_lib_init();
 
 reopen: 
  fd=open(filename,rwflag?O_RDWR:O_RDONLY);
  if(fd<0)
  { perror("libdmsdos: unable to open CVF read-write");
    if(rwflag==0)return NULL;
    fprintf(stderr,"libdmsdos: trying again in read-only mode\n");
    rwflag=0;
    goto reopen;
  }
  
  if(fstat(fd,&stat1))perror("fstat (???)");/*must succeed*/
  mnt=fopen("/etc/mtab","r");
  if(mnt)
  { while((mn=getmntent(mnt))!=NULL)
    { /*printf("getmntent read %s\n",mn->mnt_fsname);*/
      if(stat(mn->mnt_fsname,&stat2))continue; /*this may happen e.g. for proc fs*/
      if(stat1.st_dev!=stat2.st_dev||stat1.st_ino!=stat2.st_ino)continue;
      if(!hasmntopt(mn,"ro"))
      { fprintf(stderr,"libdmsdos: CVF is currently mounted read-write, can't continue.\n");
        endmntent(mnt);
        close(fd);
        return NULL;
      }
      if(rwflag)
      { fprintf(stderr,"libdmsdos: CVF is currently mounted, cannot open read-write.\n");
        fprintf(stderr,"libdmsdos: trying again in read-only mode\n");
        endmntent(mnt);
        close(fd);
        rwflag=0;
        goto reopen;
      }
      fprintf(stderr,"libdmsdos: warning: CVF is currently mounted\n");
    }
    endmntent(mnt);
  }
  
  if(rwflag)
  { if(flock(fd,LOCK_EX|LOCK_NB))
    { perror("libdmsdos: unable to lock CVF exclusively");
      fprintf(stderr,"libdmsdos: trying again in read-only mode\n");
      rwflag=0;
      close(fd);
      goto reopen;
    }
  }
  else
  { if(flock(fd,LOCK_SH|LOCK_NB))
    { perror("libdmsdos: unable to lock CVF with shared flag");
      fprintf(stderr,"libdmsdos: probably some other process has opened the CVF read-write.\n");
      close(fd);
      return NULL;
    }
  }
  
  s=lseek(fd,0,SEEK_END);
  blk_size[0][0]=(s%1024)?(s/1024)+1:s/1024;
  sb=malloc(sizeof(struct super_block));
  if(sb==NULL)
  { fprintf(stderr,"malloc failed\n");
    flock(fd,LOCK_UN);
    close(fd);
    return NULL;
  }
  
  sb->s_flags=0;
  if(rwflag==0)sb->s_flags|=MS_RDONLY;
  sb->s_dev=fd;
   
  if(read_super(sb)==NULL)
  { flock(fd,LOCK_UN);
    close(fd);
    free(sb);
    return NULL;
  }
   
  return sb;  
}

void close_cvf(struct super_block*sb)
{ int fd=sb->s_dev;

  unmount_dblspace(sb); 
  flock(fd,LOCK_UN);
  close(fd);
  free(sb);
}
