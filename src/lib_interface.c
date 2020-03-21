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

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#ifdef USE_FLOCK
#include <unistd.h>
#include <sys/file.h>
#endif
#ifdef USE_SOPEN
#include <share.h>
#endif
#include <fcntl.h>
#include <errno.h>

#define fat_boot_sector msdos_boot_sector

/* some interface hacks */
#include "lib_interface.h"
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

#ifndef cpu_to_le16
/* works only for old kernels and little endian architecture */
#define cpu_to_le16(v) (v)
#define cpu_to_le32(v) (v)
#endif

#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

long int blk_size[1][1];

extern Acache mdfat[];
extern Acache dfat[];
extern Acache bitfat[];

/* hacks for the low-level interface */

#include <stdarg.h>
int printk(const char *fmt, ...)
{
    va_list ap;
    char buf[500];
    char *p=buf;
    int i;

    va_start(ap, fmt);
    i=vsprintf(buf,fmt,ap);
    va_end(ap);

    if (p[0]=='<'&&p[1]>='0'&&p[1]<='7'&&p[2]=='>') { p+=3; }

    if (strncmp(p,"DMSDOS: ",8)==0) { p+=8; }

    fprintf(stderr,"libdmsdos: %s",p);

    return i;
}

void panic(const char *fmt, ...)
{
    va_list ap;
    char buf[500];
    int i;

    va_start(ap, fmt);
    i=vsprintf(buf,fmt,ap);
    va_end(ap);

    fprintf(stderr,"libdmsdos panic: %s",buf);

    exit(1);
}

int translate_direct(struct super_block *sb,int block)
{
    int i;

    if (block>=sb->directsize) {
        printk("DMSDOS: access beyond end of CVF in direct mode (wanted=%d limit=%d)\n",
               block,sb->directsize-1);
        return 0;
    }

    /* calculate physical sector */
    i=0;

    do {
        block-=sb->directlen[i];
        ++i;
    } while (block>=0&&i<MAXFRAGMENT);

    --i;
    block+=sb->directlen[i]+sb->directlist[i];
    return block;
}

struct buffer_head *raw_bread(struct super_block *sb,int block)
{
    struct buffer_head *bh;
    int fd=sb->s_dev;

    if (sb->directlist) {
        block=translate_direct(sb,block);

        if (!block) {
            printk("raw_bread: translate_direct failed\n");
            return NULL;
        }
    }

    if (lseek(fd,block*512,SEEK_SET)<0) {
        printk("raw_bread: lseek block %d failed: %s\n",block,strerror(errno));
        return NULL;
    }

    bh=malloc(sizeof(struct buffer_head));

    if (bh==NULL) {
        printk("raw_bread: malloc(%d) failed\n",sizeof(struct buffer_head));
        return NULL;
    }

    bh->b_data=malloc(512);

    if (bh->b_data==NULL) {
        free(bh);
        printk("raw_bread: malloc(512) failed\n");
        return NULL;
    }

    bh->b_blocknr=block;

    if (read(fd,bh->b_data,512)>=0) { return bh; }

    printk("raw_bread: read failed: %s\n",strerror(errno));
    free(bh->b_data);
    free(bh);

    return NULL;
}

struct buffer_head *raw_getblk(struct super_block *sb,int block)
{
    struct buffer_head *bh;
    int fd=sb->s_dev;

    if (sb->directlist) {
        block=translate_direct(sb,block);

        if (!block) { return NULL; }
    }

    if (lseek(fd,block*512,SEEK_SET)<0) {
        printk("raw_getblk: lseek block %d failed: %s\n",block,strerror(errno));
        return NULL;
    }

    bh=malloc(sizeof(struct buffer_head));

    if (bh==NULL) { return NULL; }

    bh->b_data=malloc(512);

    if (bh->b_data==NULL) {
        free(bh);
        return NULL;
    }

    bh->b_blocknr=block;
    return bh;
}

void raw_brelse(struct super_block *sb,struct buffer_head *bh)
{
    if (bh==NULL) { return; }

    free(bh->b_data);
    free(bh);
}

void raw_set_uptodate(struct super_block *sb,struct buffer_head *bh,int v)
{
    /* dummy */
}

void raw_mark_buffer_dirty(struct super_block *sb,struct buffer_head *bh,int dirty_val)
{
    int fd=sb->s_dev;

    if (dirty_val==0) { return; }

    if (bh==NULL) { return; }

#ifdef DBL_WRITEACCESS

    if (lseek(fd,bh->b_blocknr*512,SEEK_SET)<0) {
        printk("can't seek block %ld: %s\n",bh->b_blocknr,strerror(errno));
        return;
    }

    if (write(fd,bh->b_data,512)<0) {
        printk("writing block %ld failed: %s\n",bh->b_blocknr,strerror(errno));
    }

#else
    printk("DMSDOS: write access not compiled in, ignored\n");
#endif
}

void dblspace_reada(struct super_block *sb, int sector,int count)
{
    /* dummy */
}

int try_daemon(struct super_block *sb,int clusternr, int length, int method)
{
    return 0;
}

int host_fat_lookup(struct super_block *sb,int nr)
{
    struct buffer_head *bh,*bh2;
    unsigned char *p_first,*p_last;
    int first,last,next,b;

    if ((unsigned) (nr-2) >= MSDOS_SB(sb)->clusters) {
        return 0;
    }

    if (MSDOS_SB(sb)->fat_bits == 16) {
        first = last = nr*2;
    } else {
        first = nr*3/2;
        last = first+1;
    }

    b = MSDOS_SB(sb)->fat_start + (first >> SECTOR_BITS);

    if (!(bh = raw_bread(sb, b))) {
        printk("DMSDOS: bread in host_fat_access failed\n");
        return 0;
    }

    if ((first >> SECTOR_BITS) == (last >> SECTOR_BITS)) {
        bh2 = bh;
    } else {
        if (!(bh2 = raw_bread(sb, b+1))) {
            raw_brelse(sb, bh);
            printk("DMSDOS: 2nd bread in host_fat_lookup failed\n");
            return 0;
        }
    }

    if (MSDOS_SB(sb)->fat_bits == 16) {
        p_first = p_last = NULL; /* GCC needs that stuff */
        next = cpu_to_le16(((unsigned short *) bh->b_data)[(first &
                           (SECTOR_SIZE-1)) >> 1]);

        if (next >= 0xfff7) { next = -1; }
    } else {
        p_first = &((unsigned char *) bh->b_data)[first & (SECTOR_SIZE-1)];
        p_last = &((unsigned char *) bh2->b_data)[(first+1) &
                           (SECTOR_SIZE-1)];

        if (nr & 1) { next = ((*p_first >> 4) | (*p_last << 4)) & 0xfff; }
        else { next = (*p_first+(*p_last << 8)) & 0xfff; }

        if (next >= 0xff7) { next = -1; }
    }

    raw_brelse(sb, bh);

    if (bh != bh2) {
        raw_brelse(sb, bh2);
    }

    return next;
}

int dos_cluster2sector(struct super_block *sb,int clusternr)
{
    return (clusternr-2)*MSDOS_SB(sb)->cluster_size+MSDOS_SB(sb)->data_start;
}

int setup_fragment(struct super_block *sb, int startcluster)
{
    int fragmentzaehler;
    int clusterzaehler;
    int akt_cluster;
    int folge_cluster;
    int i;
    unsigned long *directlist;
    unsigned long *directlen;

    LOG_REST("DMSDOS: setup_fragment\n");

    directlist=malloc(sizeof(unsigned long)*(MAXFRAGMENT+1));

    if (directlist==NULL) {
        printk("DMSDOS: out of memory (directlist)\n");
        return -1;
    }

    directlen=malloc(sizeof(unsigned long)*(MAXFRAGMENT+1));

    if (directlen==NULL) {
        printk("DMSDOS: out of memory (directlen)\n");
        free(directlist);
        return -1;
    }

    fragmentzaehler=0;

    folge_cluster=startcluster;

    do {
        clusterzaehler=0;
        directlist[fragmentzaehler]=folge_cluster;

        do {
            akt_cluster=folge_cluster;
            folge_cluster=host_fat_lookup(sb,akt_cluster);
            ++clusterzaehler;
        } while (folge_cluster==akt_cluster+1);

        directlen[fragmentzaehler]=clusterzaehler;
        LOG_REST("DMSDOS: firstclust=%d anz=%d\n",
                 directlist[fragmentzaehler],
                 directlen[fragmentzaehler]);

        ++fragmentzaehler;
    } while (folge_cluster>0&&fragmentzaehler<MAXFRAGMENT);

    if (fragmentzaehler==MAXFRAGMENT&&folge_cluster>0) {
        /* zu fragmentiert, raus */
        free(directlist);
        free(directlen);
        printk("DMSDOS: CVF too fragmented, not mounted.\n");
        printk("Increase MAXFRAGMENT in lib_interface.h and recompile.\n");
        return -1;
    }

    printk("DMSDOS: CVF has %d fragment(s)\n",fragmentzaehler);

    /* convert cluster-oriented numbers into sector-oriented ones */
    for (i=0; i<fragmentzaehler; ++i) {
        /*printk("DMSDOS: umrechnen 1\n");*/
        directlist[i]=dos_cluster2sector(sb,directlist[i]);
        /*printk("DMSDOS: umrechnen 2\n");*/
        directlen[i]*=MSDOS_SB(sb)->cluster_size;
        /*printk("DMSDOS: umrechnen 3\n");*/
    }

    /* hang in */
    sb->directlist=directlist;
    sb->directlen=directlen;

    return 0;
}

int setup_translation(struct super_block *sb,char *ext)
{
    int i,j,testvers;
    struct buffer_head *bh;
    struct msdos_dir_entry *data;
    char cvfname[20];

    /* scan the root directory for a CVF */

    printf("%d / %d loops, offset %x\n", MSDOS_SB(sb)->dir_entries, MSDOS_DPS, MSDOS_SB(sb));

    for (i=0; i<MSDOS_SB(sb)->dir_entries/MSDOS_DPS; ++i) {
        bh=raw_bread(sb,MSDOS_SB(sb)->dir_start+i);

        if (bh==NULL) {
            printk("DMSDOS: unable to read msdos root directory\n");
            return -1;
        }

        data=(struct msdos_dir_entry *) bh->b_data;

        for (j=0; j<MSDOS_DPS; ++j) {
            testvers=0;

            if (strncmp(data[j].name,"DRVSPACE",8)==0) { testvers=1; }

            if (strncmp(data[j].name,"DBLSPACE",8)==0) { testvers=1; }

            if (strncmp(data[j].name,"STACVOL ",8)==0) { testvers=2; }

            if (testvers) {
                if ( ( data[j].name[8]>='0'&&data[j].name[8]<='9'
                        &&data[j].name[9]>='0'&&data[j].name[9]<='9'
                        &&data[j].name[10]>='0'&&data[j].name[10]<='9'
                     ) | (testvers==2&&strncmp(data[j].name+8,"DSK",3)==0)
                   ) {
                    /* it is a CVF */
                    strncpy(cvfname,data[j].name,9-testvers);
                    cvfname[9-testvers]='\0';
                    strcat(cvfname,".");
                    strncat(cvfname,data[j].ext,3);
                    printk("DMSDOS: CVF %s in root directory found.\n",cvfname);

                    if (ext) {
                        if (strncmp(ext,data[j].ext,3)!=0) { continue; }
                    }

                    if (setup_fragment(sb,data[j].start)==0) {
                        sb->directsize=data[j].size/SECTOR_SIZE;
                        blk_size[0][0]=(data[j].size%1024)?(data[j].size/1024)+1:
                                       data[j].size/1024;
                        raw_brelse(sb,bh);
                        printk("DMSDOS: using CVF %s.\n",cvfname);
                        return 0;
                    }
                }
            }
        }

        raw_brelse(sb,bh);
    }

    return -1;
}

/*okay, first thing is setup super block*/
/* stolen from fatfs */
/* Read the super block of an MS-DOS FS. */

struct super_block *read_super(struct super_block *sb,char *ext)
{
    struct buffer_head *bh;
    struct fat_boot_sector *b;
    int data_sectors,logical_sector_size,sector_mult,fat_clusters=0;
    int debug=0,error,fat=0;
    int blksize = 512;
    int i=-1;
    int mt=0;
    char cvf_options[101]="bitfaterrs=nocheck";

    MSDOS_SB(sb)->cvf_format=NULL;
    MSDOS_SB(sb)->private_data=NULL;

retry:
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

    if (error) { goto c_err; }

    /*
    	This must be done after the brelse because the bh is a dummy
    	allocated by fat_bread (see buffer.c)
    */
    sb->s_blocksize = blksize;    /* Using this small block size solves */
    /* the misfit with buffer cache and cluster */
    /* because clusters (DOS) are often aligned */
    /* on odd sectors. */
    sb->s_blocksize_bits = blksize == 512 ? 9 : 10;
    i=0;

#ifdef DMSDOS_CONFIG_DBL

    if (i==0) {
        i=detect_dblspace(sb);

        if (i>0) {mt++; i=mount_dblspace(sb,cvf_options);}
    }

#endif
#ifdef DMSDOS_CONFIG_STAC

    if (i==0) {
        i=detect_stacker(sb);

        if (i>0) {mt++; i=mount_stacker(sb,cvf_options);}
    }

#endif

    if (mt==0) {
        /* looks like a real msdos filesystem */
        printk("DMSDOS: trying to find CVF inside host MSDOS filesystem...\n");
        i=setup_translation(sb,ext);
        ++mt;

        if (i==0) { goto retry; }
    }

    error=i;
c_err:

    if (error || debug) {
        /* The MSDOS_CAN_BMAP is obsolete, but left just to remember */
        printk("MS-DOS FS Rel. 12 (hacked for libdmsdos), FAT %d\n",
               MSDOS_SB(sb)->fat_bits);
        printk("[me=0x%x,cs=%d,#f=%d,fs=%d,fl=%d,ds=%d,de=%d,data=%d,"
               "se=%d,ts=%ld,ls=%d]\n",b->media,MSDOS_SB(sb)->cluster_size,
               MSDOS_SB(sb)->fats,MSDOS_SB(sb)->fat_start,MSDOS_SB(sb)->fat_length,
               MSDOS_SB(sb)->dir_start,MSDOS_SB(sb)->dir_entries,
               MSDOS_SB(sb)->data_start,
               cpu_to_le16(*(unsigned short *) &b->sectors),
               (unsigned long)b->total_sect,logical_sector_size);
        printk ("Transaction block size = %d\n",blksize);
    }

    if (!error&&i<0) if (MSDOS_SB(sb)->clusters+2 > fat_clusters) {
            MSDOS_SB(sb)->clusters = fat_clusters-2;
        }

    if (error) {
        printk("Can't find a valid MSDOS CVF filesystem (error: %d)\n", error);

        if (MSDOS_SB(sb)->private_data) { kfree(MSDOS_SB(sb)->private_data); }

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
{
    /*dummy*/
}

void exit_daemon(void)
{
    /*dummy*/
}

void clear_list_dev(struct super_block *sb)
{
    /*dummy*/
}

void free_ccache_dev(struct super_block *sb)
{
    /*dummy*/
}

void remove_from_daemon_list(struct super_block *sb,int clusternr)
{
    /* dummy */
}

static int _wascalled=0;
void do_lib_init(void)
{
    int i;

    if (_wascalled) { return; }

    _wascalled=1;

    /* first call of DMSDOS library, initialising variables */

    printk("DMSDOS library version %d.%d.%d" DMSDOS_VLT
           " compiled " __DATE__ " " __TIME__ " with options:"
#ifndef DBL_WRITEACCESS
           " read-only"
#else
           " read-write"
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

    for (i=0; i<MDFATCACHESIZE; ++i) {
        mdfat[i].a_time=0;
        mdfat[i].a_acc=0;
        mdfat[i].a_buffer=NULL;
    }

    for (i=0; i<DFATCACHESIZE; ++i) {
        dfat[i].a_time=0;
        dfat[i].a_acc=0;
        dfat[i].a_buffer=NULL;
    }

    for (i=0; i<BITFATCACHESIZE; ++i) {
        bitfat[i].a_time=0;
        bitfat[i].a_acc=0;
        bitfat[i].a_buffer=NULL;
    }
}

struct super_block *open_cvf(char *filename,int rwflag)
{
    struct super_block *sb;
    int fd;
    long int s;
    char *ext=NULL;

    do_lib_init();

    ext=strrchr(filename,':');

    if (ext) {
        if (strlen(ext)==4) {
            *ext='\0';
            ++ext;
        } else {
            ext=NULL;
        }
    }

reopen:
#ifndef USE_SOPEN
    fd=open(filename,rwflag?O_RDWR:O_RDONLY);

    if (fd<0) {
        printk("unable to open CVF read-write: %s\n",strerror(errno));

        if (rwflag==0) { return NULL; }

        printk("trying again in read-only mode\n");
        rwflag=0;
        goto reopen;
    }

#ifdef USE_FLOCK

    if (rwflag) {
        if (flock(fd,LOCK_EX|LOCK_NB)) {
            printk("unable to lock CVF exclusively: %s",strerror(errno));
            printk("trying again in read-only mode\n");
            rwflag=0;
            close(fd);
            goto reopen;
        }
    } else {
        if (flock(fd,LOCK_SH|LOCK_NB)) {
            printk("unable to lock CVF with shared flag: %s",strerror(errno));
            printk("probably some other process has opened the CVF read-write.\n");
            close(fd);
            return NULL;
        }
    }

#endif /* USE_FLOCK */
#else
    /* open with win32 locking */
    fd=sopen(filename,rwflag?O_RDWR:O_RDONLY,rwflag?SH_DENYRW:SH_DENYWR);

    if (fd<0) {
        printk("unable to open CVF read-write: %s\n",strerror(errno));

        if (rwflag==0) { return NULL; }

        printk("trying again in read-only mode\n");
        rwflag=0;
        goto reopen;
    }

#endif

    s=lseek(fd,0,SEEK_END);
    blk_size[0][0]=(s%1024)?(s/1024)+1:s/1024;
    sb=malloc(sizeof(struct super_block));

    if (sb==NULL) {
        printk("malloc failed\n");
#ifdef USE_FLOCK
        flock(fd,LOCK_UN);
#endif
        close(fd);
        return NULL;
    }

    sb->s_dev=fd;
    sb->s_flags=0;

    if (rwflag==0) { sb->s_flags|=MS_RDONLY; }

    sb->directlist=NULL;
    sb->directlen=NULL;

    if (read_super(sb,ext)==NULL) {
#ifdef USE_FLOCK
        flock(fd,LOCK_UN);
#endif
        close(fd);
        free(sb);
        return NULL;
    }

    return sb;
}

void close_cvf(struct super_block *sb)
{
    int fd=sb->s_dev;

    unmount_dblspace(sb);
#ifdef USE_FLOCK
    flock(fd,LOCK_UN);
#endif
    close(fd);

    if (sb->directlist) { free(sb->directlist); }

    if (sb->directlen) { free(sb->directlen); }

    free(sb);
}
