/*
lib_interface.h

DMSDOS library: headers for interface functions, hacks, dummies, and fakes.

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

/* These are copied from the kernel include files in order to avoid
   including those files. They are not 100% identical to the kernel types.
   Most of them needn't be the same as in the kernel.
   This has been done for libc6 support.
*/

/* machine and system dependent hacks */

#include <stdint.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define cpu_to_le16(v) (v)
#define cpu_to_le32(v) (v)
#define cpu_to_be16(v) (swap_bytes_in_word(v))

#define be16_to_cpu(v) (swap_bytes_in_word(v))
#define le16_to_cpu(v) (v)

#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))
#define C_ST_u16(p,v) {put_unaligned(v,(uint16_t*)p);p=(uint8_t*)((uint16_t*)p+1);}

#define get_unaligned(ptr) (*(ptr))
#define C_LD_u16(p,v) {v=get_unaligned((uint16_t*)p);p=(uint8_t*)((uint16_t*)p+1);}

#else
#error "Please implement the macros for big endian"
#endif // __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

typedef uint8_t uint8_t;
typedef int8_t int8_t;
typedef uint16_t uint16_t;
typedef int16_t int16_t;
typedef uint32_t uint32_t;
typedef int32_t int32_t;

int printk(const char *fmt, ...);
void panic(const char *fmt, ...);

#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT

#define SECTOR_SIZE 512
#define SECTOR_BITS 9
#define MSDOS_DPB       (MSDOS_DPS) /* dir entries per block */
#define MSDOS_DPB_BITS  4 /* log2(MSDOS_DPB) */
#define MSDOS_DPS       (SECTOR_SIZE/sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS  4 /* log2(MSDOS_DPS) */
#define MSDOS_DIR_BITS  5 /* log2(sizeof(struct msdos_dir_entry)) */

#define MSDOS_SUPER_MAGIC 0x4d44 /* MD */

#define MSDOS_MAX_EXTRA 3 /* tolerate up to that number of clusters which are
                             inaccessible because the FAT is too short */

#define KERN_EMERG      "<0>"   /* system is unusable                   */
#define KERN_ALERT      "<1>"   /* action must be taken immediately     */
#define KERN_CRIT       "<2>"   /* critical conditions                  */
#define KERN_ERR        "<3>"   /* error conditions                     */
#define KERN_WARNING    "<4>"   /* warning conditions                   */
#define KERN_NOTICE     "<5>"   /* normal but significant condition     */
#define KERN_INFO       "<6>"   /* informational                        */
#define KERN_DEBUG      "<7>"   /* debug-level messages                 */

struct buffer_head {
    unsigned long b_blocknr;        /* block number */
    char *b_data;                   /* pointer to data block */
};

#define MS_RDONLY   1      /* Mount read-only */
#define MSDOS_SB(s) (&((s)->u.msdos_sb))

struct msdos_dir_entry {
    int8_t    name[8],ext[3]; /* name and extension */
    uint8_t    attr;           /* attribute bits */
    uint8_t    lcase;          /* Case for base and extension */
    uint8_t    ctime_ms;       /* Creation time, milliseconds */
    uint16_t   ctime;          /* Creation time */
    uint16_t   cdate;          /* Creation date */
    uint16_t   adate;          /* Last access date */
    uint16_t   starthi;        /* High 16 bits of cluster in FAT32 */
    uint16_t   time,date,start;/* time, date and first cluster */
    uint32_t   size;           /* file size (in bytes) */
};

struct msdos_sb_info {
    unsigned short cluster_size; /* sectors/cluster */
    unsigned char fats,fat_bits; /* number of FATs, FAT bits (12 or 16) */
    unsigned short fat_start,fat_length; /* FAT start & length (sec.) */
    unsigned short dir_start,dir_entries; /* root dir start & entries */
    unsigned short data_start;   /* first data sector */
    unsigned long clusters;      /* number of clusters */
    unsigned long root_cluster;  /* first cluster of the root directory */
    unsigned long fsinfo_offset; /* FAT32 fsinfo offset from start of disk */
    void *fat_wait;
    int fat_lock;
    int prev_free;               /* previously returned free cluster number */
    int free_clusters;           /* -1 if undefined */
    /*struct fat_mount_options options;*/
    struct nls_table *nls_disk;  /* Codepage used on disk */
    struct nls_table *nls_io;    /* Charset used for input and display */
    struct cvf_format *cvf_format;
    void *private_data;
};

struct super_block {
    int s_dev;
    unsigned long s_blocksize;
    unsigned char s_blocksize_bits;
    unsigned long s_flags;
    unsigned long s_magic;
    unsigned long *directlist;
    unsigned long *directlen;
    unsigned long directsize;
    union {
        struct msdos_sb_info msdos_sb;
    } u;

};

struct fat_boot_sector {
    uint8_t    ignored[3];     /* Boot strap short or near jump */
    uint8_t    system_id[8];   /* Name - can be used to special case
                                   partition manager volumes */
    uint8_t    sector_size[2]; /* bytes per logical sector */
    uint8_t    cluster_size;   /* sectors/cluster */
    uint16_t   reserved;       /* reserved sectors */
    uint8_t    fats;           /* number of FATs */
    uint8_t    dir_entries[2]; /* root directory entries */
    uint8_t    sectors[2];     /* number of sectors */
    uint8_t    media;          /* media code (unused) */
    uint16_t   fat_length;     /* sectors/FAT */
    uint16_t   secs_track;     /* sectors per track */
    uint16_t   heads;          /* number of heads */
    uint32_t   hidden;         /* hidden sectors (unused) */
    uint32_t   total_sect;     /* number of sectors (if sectors == 0) */

    /* The following fields are only used by FAT32 */
    uint32_t   fat32_length;   /* sectors/FAT */
    uint16_t   flags;          /* bit 8: fat mirroring, low 4: active fat */
    uint8_t    version[2];     /* major, minor filesystem version */
    uint32_t   root_cluster;   /* first cluster in root directory */
    uint16_t   info_sector;    /* filesystem info sector */
    uint16_t   backup_boot;    /* backup boot sector */
    uint8_t    reserved2[12];  /* Unused */
    uint8_t drive_number;      /* Logical Drive Number */
    uint8_t reserved3;         /* Unused */

    uint8_t extended_sig;      /* Extended Signature (0x29) */
    uint32_t serial;           /* Serial number */
    uint8_t label[11];         /* FS label */
    uint8_t fs_type[8];        /* FS Type */

    /* fill up to 512 bytes */
    uint8_t junk[422];
} __attribute__ ((packed));

#define MALLOC malloc
#define FREE free
#define kmalloc(x,y) malloc(x)
#define kfree free
#define CURRENT_TIME time(NULL)
#define vmalloc malloc
#define vfree free

#define MAXFRAGMENT 300
