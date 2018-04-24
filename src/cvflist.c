/*
cvflist.c

DMSDOS: utility that lists the names of CVFs inside a FAT12 or FAT16 MSDOS fs

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

#ifndef cpu_to_le16
/* works only for old kernels and little endian architecture */
#define cpu_to_le16(v) (v)
#define cpu_to_le32(v) (v)
#endif

#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

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

void raw_brelse(struct super_block*sb,struct buffer_head*bh)
{ if(bh==NULL)return;
  free(bh->b_data);
  free(bh);
}

int list_cvfs(struct super_block*sb)
{ int i,j,testvers;
  struct buffer_head* bh;
  struct msdos_dir_entry* data;
  char cvfname[20];
    
  /* scan the root directory for a CVF */
  
  for(i=0;i<MSDOS_SB(sb)->dir_entries/MSDOS_DPS;++i)
  { bh=raw_bread(sb,MSDOS_SB(sb)->dir_start+i);
    if(bh==NULL)
    {  fprintf(stderr,"unable to read msdos root directory\n");
       return -1;
    }
    data=(struct msdos_dir_entry*) bh->b_data;

    for(j=0;j<MSDOS_DPS;++j)
    { testvers=0;
      if(strncmp(data[j].name,"DRVSPACE",8)==0)testvers=1;
      if(strncmp(data[j].name,"DBLSPACE",8)==0)testvers=1;
      if(strncmp(data[j].name,"STACVOL ",8)==0)testvers=2;
      if(testvers)
      { if( ( data[j].name[8]>='0'&&data[j].name[8]<='9'
              &&data[j].name[9]>='0'&&data[j].name[9]<='9'
              &&data[j].name[10]>='0'&&data[j].name[10]<='9'
            ) | (testvers==2&&strncmp(data[j].name+8,"DSK",3)==0)
          )
        { /* it is a CVF */
          strncpy(cvfname,data[j].name,9-testvers);
          cvfname[9-testvers]='\0';
          strcat(cvfname,".");
          strncat(cvfname,data[j].ext,3);
          printf("%s\n",cvfname);
        }
      }
    }
    raw_brelse(sb,bh);
  }
  return 0;
}

/*okay, first thing is setup super block*/
/* stolen from fatfs */
/* Read the super block of an MS-DOS FS. */

int read_super(struct super_block *sb)
{
	struct buffer_head *bh;
	struct fat_boot_sector *b;
	int data_sectors,logical_sector_size,sector_mult,fat_clusters=0;
	int error,fat=0;
	int blksize = 512;
	
	MSDOS_SB(sb)->cvf_format=NULL;
	MSDOS_SB(sb)->private_data=NULL;

	blksize = 512;

	bh = raw_bread(sb, 0);
	if (bh == NULL) {
		raw_brelse (sb, bh);
		sb->s_dev = 0;
		fprintf(stderr,"cannot read file\n");
		return -1;
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
		cpu_to_le16(get_unaligned((unsigned short *) &b->sector_size));
	sector_mult = logical_sector_size >> SECTOR_BITS;
	MSDOS_SB(sb)->cluster_size = b->cluster_size*sector_mult;
	MSDOS_SB(sb)->fats = b->fats;
	MSDOS_SB(sb)->fat_start = cpu_to_le16(b->reserved)*sector_mult;
	MSDOS_SB(sb)->fat_length = cpu_to_le16(b->fat_length)*sector_mult;
	MSDOS_SB(sb)->dir_start = (cpu_to_le16(b->reserved)+b->fats*cpu_to_le16(
	    b->fat_length))*sector_mult;
	MSDOS_SB(sb)->dir_entries =
		cpu_to_le16(get_unaligned((unsigned short *) &b->dir_entries));
	MSDOS_SB(sb)->data_start = MSDOS_SB(sb)->dir_start+ROUND_TO_MULTIPLE((
	    MSDOS_SB(sb)->dir_entries << MSDOS_DIR_BITS) >> SECTOR_BITS,
	    sector_mult);
	data_sectors = cpu_to_le16(get_unaligned((unsigned short *) &b->sectors));
	if (!data_sectors) {
		data_sectors = cpu_to_le32(b->total_sect);
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
	
	if(error)goto c_err;

	/*
		This must be done after the brelse because the bh is a dummy
		allocated by fat_bread (see buffer.c)
	*/
	sb->s_blocksize = blksize;    /* Using this small block size solves */
				/* the misfit with buffer cache and cluster */
				/* because clusters (DOS) are often aligned */
				/* on odd sectors. */
	sb->s_blocksize_bits = blksize == 512 ? 9 : 10;
	
        return list_cvfs(sb);

       c_err:
        fprintf(stderr,"Not a FAT12 or FAT16 MSDOS filesystem.\n");
	return -1;
}

int main(int argc, char*argv[])
{ struct super_block *sb;
  int fd;
  int ret;
 
  if(argc!=2)
  { fprintf(stderr,"list names of CVFs in a FAT12 or FAT16 MSDOS fs.\n");
    fprintf(stderr,"usage: %s filename\n",argv[0]);
    return -1;
  }
  
  fd=open(argv[1],O_RDONLY);
  if(fd<0)
  { perror("open");
    return -1;
  }
  
  sb=malloc(sizeof(struct super_block));
  if(sb==NULL)
  { fprintf(stderr,"malloc failed\n");
    close(fd);
    return -1;
  }
  
  sb->s_flags=0;
  sb->s_flags|=MS_RDONLY;
  sb->s_dev=fd;
  sb->directlist=NULL;
  sb->directlen=NULL;
   
  ret=read_super(sb);
  close(fd);
  free(sb);
  return ret;
}

