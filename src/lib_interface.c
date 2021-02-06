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

#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

/* cut-over cluster counts for FAT12 and FAT16 */
#define FAT12_THRESHOLD  4085
#define FAT16_THRESHOLD 65525

/* Unaligned fields must first be accessed byte-wise */
#define GET_UNALIGNED_W(f)			\
    ( (uint16_t)f[0] | ((uint16_t)f[1]<<8) )

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
    char *p = buf;
    int i;

    va_start(ap, fmt);
    i = vsprintf(buf, fmt, ap);
    va_end(ap);

    if (p[0] == '<' && p[1] >= '0' && p[1] <= '7' && p[2] == '>') { p += 3; }

    if (strncmp(p, "DMSDOS: ", 8) == 0) { p += 8; }

    fprintf(stderr, "libdmsdos: %s", p);

    return i;
}

void panic(const char *fmt, ...)
{
    va_list ap;
    char buf[500];
    int i;

    va_start(ap, fmt);
    i = vsprintf(buf, fmt, ap);
    va_end(ap);

    fprintf(stderr, "libdmsdos panic: %s", buf);

    exit(1);
}

int translate_direct(struct super_block *sb, int block)
{
    int i;

    if (block >= sb->directsize) {
        printk("DMSDOS: access beyond end of CVF in direct mode (wanted=%d limit=%d)\n",
               block, sb->directsize - 1);
        return 0;
    }

    /* calculate physical sector */
    i = 0;

    do {
        block -= sb->directlen[i];
        ++i;
    } while (block >= 0 && i < MAXFRAGMENT);

    --i;
    block += sb->directlen[i] + sb->directlist[i];
    return block;
}

struct buffer_head *raw_bread(struct super_block *sb, int block)
{
    struct buffer_head *bh;
    int fd = sb->s_dev;

    if (sb->directlist) {
        block = translate_direct(sb, block);

        if (!block) {
            printk("raw_bread: translate_direct failed\n");
            return NULL;
        }
    }

    if (lseek(fd, block * 512, SEEK_SET) < 0) {
        printk("raw_bread: lseek block %d failed: %s\n", block, strerror(errno));
        return NULL;
    }

    bh = malloc(sizeof(struct buffer_head));

    if (bh == NULL) {
        printk("raw_bread: malloc(%d) failed\n", sizeof(struct buffer_head));
        return NULL;
    }

    bh->b_data = malloc(512);

    if (bh->b_data == NULL) {
        free(bh);
        printk("raw_bread: malloc(512) failed\n");
        return NULL;
    }

    bh->b_blocknr = block;

    if (read(fd, bh->b_data, 512) >= 0) { return bh; }

    printk("raw_bread: read failed: %s\n", strerror(errno));
    free(bh->b_data);
    free(bh);

    return NULL;
}

struct buffer_head *raw_getblk(struct super_block *sb, int block)
{
    struct buffer_head *bh;
    int fd = sb->s_dev;

    if (sb->directlist) {
        block = translate_direct(sb, block);

        if (!block) { return NULL; }
    }

    if (lseek(fd, block * 512, SEEK_SET) < 0) {
        printk("raw_getblk: lseek block %d failed: %s\n", block, strerror(errno));
        return NULL;
    }

    bh = malloc(sizeof(struct buffer_head));

    if (bh == NULL) { return NULL; }

    bh->b_data = malloc(512);

    if (bh->b_data == NULL) {
        free(bh);
        return NULL;
    }

    bh->b_blocknr = block;
    return bh;
}

void raw_brelse(struct super_block *sb, struct buffer_head *bh)
{
    if (bh == NULL) { return; }

    free(bh->b_data);
    free(bh);
}

void raw_set_uptodate(struct super_block *sb, struct buffer_head *bh, int v)
{
    /* dummy */
}

void raw_mark_buffer_dirty(struct super_block *sb, struct buffer_head *bh, int dirty_val)
{
    int fd = sb->s_dev;

    if (dirty_val == 0) { return; }

    if (bh == NULL) { return; }

#ifdef DBL_WRITEACCESS

    if (lseek(fd, bh->b_blocknr * 512, SEEK_SET) < 0) {
        printk("can't seek block %ld: %s\n", bh->b_blocknr, strerror(errno));
        return;
    }

    if (write(fd, bh->b_data, 512) < 0) {
        printk("writing block %ld failed: %s\n", bh->b_blocknr, strerror(errno));
    }

#else
    printk("DMSDOS: write access not compiled in, ignored\n");
#endif
}

void dblspace_reada(struct super_block *sb, int sector, int count)
{
    /* dummy */
}

int try_daemon(struct super_block *sb, int clusternr, int length, int method)
{
    return 0;
}

typedef struct _change {
    void *data;
    off_t pos;
    int size;
    struct _change *next;
} CHANGE;

static CHANGE *changes, *last;

static int fd_;

static int min(int a, int b)
{
    return a < b ? a : b;
}

void fs_read(off_t pos, int size, void *data)
{
    CHANGE *walk;
    int got;

    if (lseek(fd_, pos, 0) != pos)
        panic("Seek to %lld", (long long)pos);
    if ((got = read(fd_, data, size)) < 0)
        panic("Read %d bytes at %lld", size, (long long)pos);
    if (got != size)
        panic("Got %d bytes instead of %d at %lld", got, size, (long long)pos);
    for (walk = changes; walk; walk = walk->next) {
        if (walk->pos < pos + size && walk->pos + walk->size > pos) {
            if (walk->pos < pos)
                memcpy(data, (char *)walk->data + pos - walk->pos,
                        min(size, walk->size - pos + walk->pos));
            else
                memcpy((char *)data + walk->pos - pos, walk->data,
                        min(walk->size, size + pos - walk->pos));
        }
    }
}


#define FAT_STATE_DIRTY 0x01
static void check_fat_state_bit(DOS_FS * fs, void *b)
{
    if (fs->fat_bits == 32) {
        struct boot_sector *b32 = b;

        if (b32->reserved3 & FAT_STATE_DIRTY) {
            printf("0x41: ");
            //if (print_fat_dirty_state() == 1) {
            //    b32->reserved3 &= ~FAT_STATE_DIRTY;
            //    fs_write(0, sizeof(*b32), b32);
            //}
        }
    } else {
        struct boot_sector_16 *b16 = b;

        if (b16->reserved2 & FAT_STATE_DIRTY) {
            printf("0x25: ");
            //if (print_fat_dirty_state() == 1) {
            //    b16->reserved2 &= ~FAT_STATE_DIRTY;
            //    fs_write(0, sizeof(*b16), b16);
            //}
        }
    }
}

static void read_fsinfo(DOS_FS * fs, struct boot_sector *b, unsigned int lss)
{
    struct info_sector i;

    if (!b->info_sector) {
        printf("No FSINFO sector\n");
        //if (get_choice(2, "  Not automatically creating it.",
        //            2,
        //            1, "Create one",
        //            2, "Do without FSINFO") == 1) {
        //    /* search for a free reserved sector (not boot sector and not
        //     * backup boot sector) */
        //    uint32_t s;
        //    for (s = 1; s < le16toh(b->reserved); ++s)
        //        if (s != le16toh(b->backup_boot))
        //            break;
        //    if (s > 0 && s < le16toh(b->reserved)) {
        //        init_fsinfo(&i);
        //        fs_write((off_t)s * lss, sizeof(i), &i);
        //        b->info_sector = htole16(s);
        //        fs_write(offsetof(struct boot_sector, info_sector),
        //                sizeof(b->info_sector), &b->info_sector);
        //        if (fs->backupboot_start)
        //            fs_write(fs->backupboot_start +
        //                    offsetof(struct boot_sector, info_sector),
        //                    sizeof(b->info_sector), &b->info_sector);
        //    } else {
        //        printf("No free reserved sector found -- "
        //                "no space for FSINFO sector!\n");
        //        return;
        //    }
        //} else
            return;
    }

    fs->fsinfo_start = le16toh(b->info_sector) * lss;
    fs_read(fs->fsinfo_start, sizeof(i), &i);

    if (i.magic != htole32(0x41615252) ||
            i.signature != htole32(0x61417272) || i.boot_sign != htole32(0xaa550000)) {
        printf("FSINFO sector has bad magic number(s):\n");
        if (i.magic != htole32(0x41615252))
            printf("  Offset %llu: 0x%08x != expected 0x%08x\n",
                    (unsigned long long)offsetof(struct info_sector, magic),
                    le32toh(i.magic), 0x41615252);
        if (i.signature != htole32(0x61417272))
            printf("  Offset %llu: 0x%08x != expected 0x%08x\n",
                    (unsigned long long)offsetof(struct info_sector, signature),
                    le32toh(i.signature), 0x61417272);
        if (i.boot_sign != htole32(0xaa550000))
            printf("  Offset %llu: 0x%08x != expected 0x%08x\n",
                    (unsigned long long)offsetof(struct info_sector, boot_sign),
                    le32toh(i.boot_sign), 0xaa550000);
        //if (get_choice(1, "  Auto-correcting it.",
        //            2,
        //            1, "Correct",
        //            2, "Don't correct (FSINFO invalid then)") == 1) {
        //    init_fsinfo(&i);
        //    fs_write(fs->fsinfo_start, sizeof(i), &i);
        //} else
            fs->fsinfo_start = 0;
    }

    if (fs->fsinfo_start)
        fs->free_clusters = le32toh(i.free_clusters);
}


static void check_backup_boot(DOS_FS * fs_, struct boot_sector *b, unsigned int lss)
{
#if 0
    struct boot_sector b2;

    if (!fs->backupboot_start) {
        printf("There is no backup boot sector.\n");
        if (le16toh(b->reserved) < 3) {
            printf("And there is no space for creating one!\n");
            return;
        }
        //if (get_choice(1, "  Auto-creating backup boot block.",
        //	       2,
        //	       1, "Create one",
        //	       2, "Do without a backup") == 1) {
        //    unsigned int bbs;
        //    /* The usual place for the backup boot sector is sector 6. Choose
        //     * that or the last reserved sector. */
        //    if (le16toh(b->reserved) >= 7 && le16toh(b->info_sector) != 6)
        //	bbs = 6;
        //    else {
        //	bbs = le16toh(b->reserved) - 1;
        //	if (bbs == le16toh(b->info_sector))
        //	    --bbs;	/* this is never 0, as we checked reserved >= 3! */
        //    }
        //    fs->backupboot_start = bbs * lss;
        //    b->backup_boot = htole16(bbs);
        //    fs_write(fs->backupboot_start, sizeof(*b), b);
        //    fs_write(offsetof(struct boot_sector, backup_boot),
        //	     sizeof(b->backup_boot), &b->backup_boot);
        //    printf("Created backup of boot sector in sector %d\n", bbs);
        //    return;
        //} else
        return;
        //}

        //fs_read(fs->backupboot_start, sizeof(b2), &b2);
        //if (memcmp(b, &b2, sizeof(b2)) != 0) {
        //    /* there are any differences */
        //    uint8_t *p, *q;
        //    int i, pos, first = 1;
        //    char buf[32];

        //    printf("There are differences between boot sector and its backup.\n");
        //    printf("This is mostly harmless. Differences: (offset:original/backup)\n  ");
        //    pos = 2;
        //    for (p = (uint8_t *) b, q = (uint8_t *) & b2, i = 0; i < sizeof(b2);
        //            ++p, ++q, ++i) {
        //        if (*p != *q) {
        //            sprintf(buf, "%s%u:%02x/%02x", first ? "" : ", ",
        //                    (unsigned)(p - (uint8_t *) b), *p, *q);
        //            if (pos + strlen(buf) > 78)
        //                printf("\n  "), pos = 2;
        //            printf("%s", buf);
        //            pos += strlen(buf);
        //            first = 0;
        //        }
        //    }
        //    printf("\n");

        //    //switch (get_choice(3, "  Not automatically fixing this.",
        //    //            3,
        //    //            1, "Copy original to backup",
        //    //            2, "Copy backup to original",
        //    //            3, "No action")) {
        //    //    case 1:
        //    //        fs_write(fs->backupboot_start, sizeof(*b), b);
        //    //        break;
        //    //    case 2:
        //    //        fs_write(0, sizeof(b2), &b2);
        //    //        break;
        //    //    default:
        //    //        break;
        //    //}
        //}
#endif
}

int host_fat_lookup(struct super_block *sb, int nr)
{
    struct buffer_head *bh, *bh2;
    unsigned char *p_first, *p_last;
    int first, last, next, b;

    if ((unsigned)(nr - 2) >= MSDOS_SB(sb)->clusters) {
        return 0;
    }

    if (MSDOS_SB(sb)->fat_bits == 16) {
        first = last = nr * 2;
    } else {
        first = nr * 3 / 2;
        last = first + 1;
    }

    b = MSDOS_SB(sb)->fat_start + (first >> SECTOR_BITS);

    if (!(bh = raw_bread(sb, b))) {
        printk("DMSDOS: bread in host_fat_access failed\n");
        return 0;
    }

    if ((first >> SECTOR_BITS) == (last >> SECTOR_BITS)) {
        bh2 = bh;
    } else {
        if (!(bh2 = raw_bread(sb, b + 1))) {
            raw_brelse(sb, bh);
            printk("DMSDOS: 2nd bread in host_fat_lookup failed\n");
            return 0;
        }
    }

    if (MSDOS_SB(sb)->fat_bits == 16) {
        p_first = p_last = NULL; /* GCC needs that stuff */
        next = cpu_to_le16(((unsigned short *) bh->b_data)[(first &
                           (SECTOR_SIZE - 1)) >> 1]);

        if (next >= 0xfff7) { next = -1; }
    } else {
        p_first = &((unsigned char *) bh->b_data)[first & (SECTOR_SIZE - 1)];
        p_last = &((unsigned char *) bh2->b_data)[(first + 1) &
                             (SECTOR_SIZE - 1)];

        if (nr & 1) { next = ((*p_first >> 4) | (*p_last << 4)) & 0xfff; }
        else { next = (*p_first + (*p_last << 8)) & 0xfff; }

        if (next >= 0xff7) { next = -1; }
    }

    raw_brelse(sb, bh);

    if (bh != bh2) {
        raw_brelse(sb, bh2);
    }

    return next;
}

int dos_cluster2sector(struct super_block *sb, int clusternr)
{
    return (clusternr - 2) * MSDOS_SB(sb)->cluster_size + MSDOS_SB(sb)->data_start;
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

    directlist = malloc(sizeof(unsigned long) * (MAXFRAGMENT + 1));

    if (directlist == NULL) {
        printk("DMSDOS: out of memory (directlist)\n");
        return -1;
    }

    directlen = malloc(sizeof(unsigned long) * (MAXFRAGMENT + 1));

    if (directlen == NULL) {
        printk("DMSDOS: out of memory (directlen)\n");
        free(directlist);
        return -1;
    }

    fragmentzaehler = 0;

    folge_cluster = startcluster;

    do {
        clusterzaehler = 0;
        directlist[fragmentzaehler] = folge_cluster;

        do {
            akt_cluster = folge_cluster;
            folge_cluster = host_fat_lookup(sb, akt_cluster);
            ++clusterzaehler;
        } while (folge_cluster == akt_cluster + 1);

        directlen[fragmentzaehler] = clusterzaehler;
        LOG_REST("DMSDOS: firstclust=%d anz=%d\n",
                 directlist[fragmentzaehler],
                 directlen[fragmentzaehler]);

        ++fragmentzaehler;
    } while (folge_cluster > 0 && fragmentzaehler < MAXFRAGMENT);

    if (fragmentzaehler == MAXFRAGMENT && folge_cluster > 0) {
        /* zu fragmentiert, raus */
        free(directlist);
        free(directlen);
        printk("DMSDOS: CVF too fragmented, not mounted.\n");
        printk("Increase MAXFRAGMENT in lib_interface.h and recompile.\n");
        return -1;
    }

    printk("DMSDOS: CVF has %d fragment(s)\n", fragmentzaehler);

    /* convert cluster-oriented numbers into sector-oriented ones */
    for (i = 0; i < fragmentzaehler; ++i) {
        /*printk("DMSDOS: umrechnen 1\n");*/
        directlist[i] = dos_cluster2sector(sb, directlist[i]);
        /*printk("DMSDOS: umrechnen 2\n");*/
        directlen[i] *= MSDOS_SB(sb)->cluster_size;
        /*printk("DMSDOS: umrechnen 3\n");*/
    }

    /* hang in */
    sb->directlist = directlist;
    sb->directlen = directlen;

    return 0;
}

int setup_translation(struct super_block *sb, char *ext)
{
    int i, j, testvers;
    struct buffer_head *bh;
    struct DIR_ENT *data;
    char cvfname[20];

    /* scan the root directory for a CVF */

    printf("%hu / %lu loops, offset %p\n", MSDOS_SB(sb)->dir_entries, MSDOS_DPS, MSDOS_SB(sb));

    for (i = 0; i < MSDOS_SB(sb)->dir_entries / MSDOS_DPS; ++i) {
        bh = raw_bread(sb, MSDOS_SB(sb)->dir_start + i);

        if (bh == NULL) {
            printk("DMSDOS: unable to read msdos root directory\n");
            return -1;
        }

        data = (struct DIR_ENT *) bh->b_data;

        for (j = 0; j < MSDOS_DPS; ++j) {
            testvers = 0;

            if (strncmp(data[j].name, "DRVSPACE", 8) == 0) { testvers = 1; }

            if (strncmp(data[j].name, "DBLSPACE", 8) == 0) { testvers = 1; }

            if (strncmp(data[j].name, "STACVOL ", 8) == 0) { testvers = 2; }

            if (testvers) {
                if ((data[j].ext[0] >= '0' && data[j].ext[0] <= '9'
                        && data[j].ext[1] >= '0' && data[j].ext[1] <= '9'
                        && data[j].ext[2] >= '0' && data[j].ext[2] <= '9'
                    ) | (testvers == 2 && strncmp(data[j].name + 8, "DSK", 3) == 0)
                   ) {
                    /* it is a CVF */
                    strncpy(cvfname, data[j].name, 9 - testvers);
                    cvfname[9 - testvers] = '\0';
                    strcat(cvfname, ".");
                    strncat(cvfname, data[j].ext, 3);
                    printk("DMSDOS: CVF %s in root directory found.\n", cvfname);

                    if (ext) {
                        if (strncmp(ext, data[j].ext, 3) != 0) { continue; }
                    }

                    if (setup_fragment(sb, data[j].start) == 0) {
                        sb->directsize = data[j].size / SECTOR_SIZE;
                        blk_size[0][0] = (data[j].size % 1024) ? (data[j].size / 1024) + 1 :
                                         data[j].size / 1024;
                        raw_brelse(sb, bh);
                        printk("DMSDOS: using CVF %s.\n", cvfname);
                        return 0;
                    }
                }
            }
        }

        raw_brelse(sb, bh);
    }

    return -1;
}


static struct {
    uint8_t media;
    const char *descr;
} mediabytes[] = {
    {
    0xf0, "5.25\" or 3.5\" HD floppy"}, {
    0xf8, "hard disk"}, {
    0xf9, "3,5\" 720k floppy 2s/80tr/9sec or "
	    "5.25\" 1.2M floppy 2s/80tr/15sec"}, {
    0xfa, "5.25\" 320k floppy 1s/80tr/8sec"}, {
    0xfb, "3.5\" 640k floppy 2s/80tr/8sec"}, {
    0xfc, "5.25\" 180k floppy 1s/40tr/9sec"}, {
    0xfd, "5.25\" 360k floppy 2s/40tr/9sec"}, {
    0xfe, "5.25\" 160k floppy 1s/40tr/8sec"}, {
0xff, "5.25\" 320k floppy 2s/40tr/8sec"},};
static const char *get_media_descr(unsigned char media)
{
    int i;

    for (i = 0; i < sizeof(mediabytes) / sizeof(*mediabytes); ++i) {
	if (mediabytes[i].media == media)
	    return (mediabytes[i].descr);
    }
    return ("undefined");
}

static void dump_boot(DOS_FS * fs, struct boot_sector *b, unsigned lss)
{
    unsigned short sectors;

    printf("Boot sector contents:\n");
    if (1) { //!atari_format) {
        char id[9];
        strncpy(id, (const char *)b->system_id, 8);
        id[8] = 0;
        printf("System ID \"%s\"\n", id);
    } else {
        /* On Atari, a 24 bit serial number is stored at offset 8 of the boot
         * sector */
        printf("Serial number 0x%x\n",
                b->system_id[5] | (b->system_id[6] << 8) | (b->
                    system_id[7] << 16));
    }
    printf("Media byte 0x%02x (%s)\n", b->media, get_media_descr(b->media));
    printf("%10d bytes per logical sector\n", GET_UNALIGNED_W(b->sector_size));
    printf("%10d bytes per cluster\n", fs->cluster_size);
    printf("%10d reserved sector%s\n", le16toh(b->reserved),
            le16toh(b->reserved) == 1 ? "" : "s");
    printf("First FAT starts at byte %llu (sector %llu)\n",
            (unsigned long long)fs->fat_start,
            (unsigned long long)fs->fat_start / lss);
    printf("%10d FATs, %d bit entries\n", b->fats, fs->fat_bits);
    printf("%10lld bytes per FAT (= %llu sectors)\n", (long long)fs->fat_size,
            (long long)fs->fat_size / lss);
    if (!fs->root_cluster) {
        printf("Root directory starts at byte %llu (sector %llu)\n",
                (unsigned long long)fs->root_start,
                (unsigned long long)fs->root_start / lss);
        printf("%10d root directory entries\n", fs->root_entries);
    } else {
        printf("Root directory start at cluster %lu (arbitrary size)\n",
                (unsigned long)fs->root_cluster);
    }
    printf("Data area starts at byte %llu (sector %llu)\n",
            (unsigned long long)fs->data_start,
            (unsigned long long)fs->data_start / lss);
    printf("%10lu data clusters (%llu bytes)\n",
            (unsigned long)fs->data_clusters,
            (unsigned long long)fs->data_clusters * fs->cluster_size);
    printf("%u sectors/track, %u heads\n", le16toh(b->secs_track),
            le16toh(b->heads));
    printf("%10u hidden sectors\n",// atari_format ?
            /* On Atari, the hidden field is only 16 bit wide and unused */
            /*(((unsigned char *)&b->hidden)[0] |
             ((unsigned char *)&b->hidden)[1] << 8) :*/ le32toh(b->hidden));
    sectors = GET_UNALIGNED_W(b->sectors);
    printf("%10u sectors total\n", sectors ? sectors : le32toh(b->total_sect));
}


/*okay, first thing is setup super block*/
/* stolen from fatfs */
/* Read the super block of an MS-DOS FS. */


struct super_block *read_super(struct super_block *sb, char *ext)
{
    DOS_FS *fs = calloc(1, sizeof(DOS_FS));
    fd_ = sb->s_dev;

    struct buffer_head *bh;
    struct boot_sector *b;
    int data_sectors, logical_sector_size, sector_mult, fat_clusters = 0;
    int debug = 0, error, fat = 0;
    int blksize = 512;
    int i = -1;
    int mt = 0;
    char cvf_options[101] = "bitfaterrs=nocheck";

    MSDOS_SB(sb)->cvf_format = NULL;
    MSDOS_SB(sb)->private_data = NULL;

retry:
    blksize = 512;

    bh = raw_bread(sb, 0);

    if (bh == NULL) {
        raw_brelse(sb, bh);
        sb->s_dev = 0;
        printk("FAT bread failed\n");
        return NULL;
    }

    b = (struct boot_sector *) bh->b_data;
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
    MSDOS_SB(sb)->cluster_size = b->cluster_size * sector_mult;
    MSDOS_SB(sb)->fats = b->fats;
    MSDOS_SB(sb)->fat_start = cpu_to_le16(b->reserved) * sector_mult;
    MSDOS_SB(sb)->fat_length = cpu_to_le16(b->fat_length) * sector_mult;
    MSDOS_SB(sb)->dir_start = (cpu_to_le16(b->reserved) + b->fats * cpu_to_le16(
                                   b->fat_length)) * sector_mult;
    MSDOS_SB(sb)->dir_entries =
        cpu_to_le16(get_unaligned((unsigned short *) &b->dir_entries));
    MSDOS_SB(sb)->data_start = MSDOS_SB(sb)->dir_start + ROUND_TO_MULTIPLE((
                                   MSDOS_SB(sb)->dir_entries << MSDOS_DIR_BITS) >> SECTOR_BITS,
                               sector_mult);
    data_sectors = cpu_to_le16(get_unaligned((unsigned short *) &b->sectors));

    if (!data_sectors) {
        data_sectors = cpu_to_le32(b->total_sect);
    }

    data_sectors = data_sectors * sector_mult - MSDOS_SB(sb)->data_start;
    error = !b->cluster_size || !sector_mult;

    if (!error) {
        MSDOS_SB(sb)->clusters = b->cluster_size ? data_sectors /
                                 b->cluster_size / sector_mult : 0;
        MSDOS_SB(sb)->fat_bits = fat ? fat : MSDOS_SB(sb)->clusters >
                                 MSDOS_FAT12 ? 16 : 12;
        fat_clusters = MSDOS_SB(sb)->fat_length * SECTOR_SIZE * 8 /
                       MSDOS_SB(sb)->fat_bits;
        /* this doesn't compile. I don't understand it either...
        error = !MSDOS_SB(sb)->fats || (MSDOS_SB(sb)->dir_entries &
            (MSDOS_DPS-1)) || MSDOS_SB(sb)->clusters+2 > fat_clusters+
            MSDOS_MAX_EXTRA || (logical_sector_size & (SECTOR_SIZE-1))
            || !b->secs_track || !b->heads;
        */
    }

    raw_brelse(sb, bh);

    fs->cluster_size = b->cluster_size * logical_sector_size;
    fs->nfats = b->fats;
    fs->fat_start = (off_t)le16toh(b->reserved) * logical_sector_size;
    long long fat_length = le16toh(b->fat_length) ? le16toh(b->fat_length) : le32toh(b->fat32_length);
    fs->root_start = (le16toh(b->reserved) + b->fats * fat_length) * logical_sector_size;
    fs->root_entries = GET_UNALIGNED_W(b->dir_entries);
    long long position = (long long)fs->root_start + ROUND_TO_MULTIPLE(fs->root_entries << MSDOS_DIR_BITS, logical_sector_size);
    fs->data_start = position;

    unsigned sectors = GET_UNALIGNED_W(b->sectors);
    unsigned total_sectors = sectors ? sectors : le32toh(b->total_sect);
    position = (long long)total_sectors * logical_sector_size - fs->data_start;
    long long data_size = position;
    fs->data_clusters = data_size / fs->cluster_size;
    fs->root_cluster = 0;       /* indicates standard, pre-FAT32 root dir */
    fs->fsinfo_start = 0;       /* no FSINFO structure */
    fs->free_clusters = -1;     /* unknown */

    if (!b->fat_length && b->fat32_length) {
        fs->fat_bits = 32;
        fs->root_cluster = le32toh(b->root_cluster);
        if (!fs->root_cluster && fs->root_entries)
            /* M$ hasn't specified this, but it looks reasonable: If
             * root_cluster is 0 but there is a separate root dir
             * (root_entries != 0), we handle the root dir the old way. Give a
             * warning, but convertig to a root dir in a cluster chain seems
             * to complex for now... */
            printf("Warning: FAT32 root dir not in cluster chain! "
                    "Compatibility mode...\n");
        else if (!fs->root_cluster && !fs->root_entries)
            panic("No root directory!");
        else if (fs->root_cluster && fs->root_entries)
            printf("Warning: FAT32 root dir is in a cluster chain, but "
                    "a separate root dir\n"
                    "  area is defined. Cannot fix this easily.\n");
        if (fs->data_clusters < FAT16_THRESHOLD)
            printf("Warning: Filesystem is FAT32 according to fat_length "
                    "and fat32_length fields,\n"
                    "  but has only %lu clusters, less than the required "
                    "minimum of %d.\n"
                    "  This may lead to problems on some systems.\n",
                    (unsigned long)fs->data_clusters, FAT16_THRESHOLD);

        check_fat_state_bit(fs, b);
        fs->backupboot_start = le16toh(b->backup_boot) * logical_sector_size;
        check_backup_boot(fs, b, logical_sector_size);

        read_fsinfo(fs, b, logical_sector_size);
    } else {
        /* On real MS-DOS, a 16 bit FAT is used whenever there would be too
         * much clusers otherwise. */
        fs->fat_bits = (fs->data_clusters >= FAT12_THRESHOLD) ? 16 : 12;
        if (fs->data_clusters >= FAT16_THRESHOLD)
            panic("Too many clusters (%lu) for FAT16 filesystem.",
                    (unsigned long)fs->data_clusters);
        check_fat_state_bit(fs, &b);
    }
        dump_boot(fs, b, logical_sector_size);

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
    i = 0;

//#ifdef DMSDOS_CONFIG_DBL

    if (i == 0) {
        i = detect_dblspace(sb);

        if (i > 0) {mt++; i = mount_dblspace(sb, cvf_options);}
    }

//#endif
//#ifdef DMSDOS_CONFIG_STAC

    if (i == 0) {
        i = detect_stacker(sb);

        if (i > 0) {mt++; i = mount_stacker(sb, cvf_options);}
    }

//#endif

    if (mt == 0) {
        /* looks like a real msdos filesystem */
        printk("DMSDOS: trying to find CVF inside host MSDOS filesystem...\n");
        i = setup_translation(sb, ext);
        ++mt;

        if (i == 0) { goto retry; }
    }

    error = i;
c_err:

    if (error || debug) {
        /* The MSDOS_CAN_BMAP is obsolete, but left just to remember */
        printk("MS-DOS FS Rel. 12 (hacked for libdmsdos), FAT %d\n",
               MSDOS_SB(sb)->fat_bits);
        printk("[me=0x%x,cs=%d,#f=%d,fs=%d,fl=%d,ds=%d,de=%d,data=%d,"
               "se=%d,ts=%ld,ls=%d]\n", b->media, MSDOS_SB(sb)->cluster_size,
               MSDOS_SB(sb)->fats, MSDOS_SB(sb)->fat_start, MSDOS_SB(sb)->fat_length,
               MSDOS_SB(sb)->dir_start, MSDOS_SB(sb)->dir_entries,
               MSDOS_SB(sb)->data_start,
               cpu_to_le16(*(unsigned short *) &b->sectors),
               (unsigned long)b->total_sect, logical_sector_size);
        printk("Transaction block size = %d\n", blksize);
    }

    if (!error && i < 0) if (MSDOS_SB(sb)->clusters + 2 > fat_clusters) {
            MSDOS_SB(sb)->clusters = fat_clusters - 2;
        }

    if (error) {
        printk("Can't find a valid MSDOS CVF filesystem (error: %d)\n", error);

        if (MSDOS_SB(sb)->private_data) { kfree(MSDOS_SB(sb)->private_data); }

        MSDOS_SB(sb)->private_data = NULL;
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

void remove_from_daemon_list(struct super_block *sb, int clusternr)
{
    /* dummy */
}

static int _wascalled = 0;
void do_lib_init(void)
{
    int i;

    if (_wascalled) { return; }

    _wascalled = 1;

    /* first call of DMSDOS library, initialising variables */

    printk("DMSDOS library version %d.%d.%d" DMSDOS_VLT
           " compiled " __DATE__ " " __TIME__ " with options:"
#ifndef DBL_WRITEACCESS
           " read-only"
#else
           " read-write"
#endif
           ", doublespace/drivespace(<3)"
           ", drivespace 3"
           ", stacker 3"
           ", stacker 4"
           "\n",
           DMSDOS_MAJOR, DMSDOS_MINOR, DMSDOS_ACT_REL);

    for (i = 0; i < MDFATCACHESIZE; ++i) {
        mdfat[i].a_time = 0;
        mdfat[i].a_acc = 0;
        mdfat[i].a_buffer = NULL;
    }

    for (i = 0; i < DFATCACHESIZE; ++i) {
        dfat[i].a_time = 0;
        dfat[i].a_acc = 0;
        dfat[i].a_buffer = NULL;
    }

    for (i = 0; i < BITFATCACHESIZE; ++i) {
        bitfat[i].a_time = 0;
        bitfat[i].a_acc = 0;
        bitfat[i].a_buffer = NULL;
    }
}

struct super_block *open_cvf(char *filename, int rwflag)
{
    struct super_block *sb;
    int fd;
    long int s;
    char *ext = NULL;

    do_lib_init();

    ext = strrchr(filename, ':');

    if (ext) {
        if (strlen(ext) == 4) {
            *ext = '\0';
            ++ext;
        } else {
            ext = NULL;
        }
    }

reopen:
#ifndef USE_SOPEN
    fd = open(filename, rwflag ? O_RDWR : O_RDONLY);

    if (fd < 0) {
        printk("unable to open CVF read-write: %s\n", strerror(errno));

        if (rwflag == 0) { return NULL; }

        printk("trying again in read-only mode\n");
        rwflag = 0;
        goto reopen;
    }

#ifdef USE_FLOCK

    if (rwflag) {
        if (flock(fd, LOCK_EX | LOCK_NB)) {
            printk("unable to lock CVF exclusively: %s", strerror(errno));
            printk("trying again in read-only mode\n");
            rwflag = 0;
            close(fd);
            goto reopen;
        }
    } else {
        if (flock(fd, LOCK_SH | LOCK_NB)) {
            printk("unable to lock CVF with shared flag: %s", strerror(errno));
            printk("probably some other process has opened the CVF read-write.\n");
            close(fd);
            return NULL;
        }
    }

#endif /* USE_FLOCK */
#else
    /* open with win32 locking */
    fd = sopen(filename, rwflag ? O_RDWR : O_RDONLY, rwflag ? SH_DENYRW : SH_DENYWR);

    if (fd < 0) {
        printk("unable to open CVF read-write: %s\n", strerror(errno));

        if (rwflag == 0) { return NULL; }

        printk("trying again in read-only mode\n");
        rwflag = 0;
        goto reopen;
    }

#endif

    s = lseek(fd, 0, SEEK_END);
    blk_size[0][0] = (s % 1024) ? (s / 1024) + 1 : s / 1024;
    sb = malloc(sizeof(struct super_block));

    if (sb == NULL) {
        printk("malloc failed\n");
#ifdef USE_FLOCK
        flock(fd, LOCK_UN);
#endif
        close(fd);
        return NULL;
    }

    sb->s_dev = fd;
    sb->s_flags = 0;

    if (rwflag == 0) { sb->s_flags |= MS_RDONLY; }

    sb->directlist = NULL;
    sb->directlen = NULL;

    if (read_super(sb, ext) == NULL) {
#ifdef USE_FLOCK
        flock(fd, LOCK_UN);
#endif
        close(fd);
        free(sb);
        return NULL;
    }

    return sb;
}

void close_cvf(struct super_block *sb)
{
    int fd = sb->s_dev;

    unmount_dblspace(sb);
#ifdef USE_FLOCK
    flock(fd, LOCK_UN);
#endif
    close(fd);

    if (sb->directlist) { free(sb->directlist); }

    if (sb->directlen) { free(sb->directlen); }

    free(sb);
}
