/*
dblspace_tables.c

DMSDOS CVF-FAT module: [d|md|bit]fat access functions.

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

#include "dmsdos.h"

#ifdef __DMSDOS_LIB__
/* some interface hacks */
#include "lib_interface.h"
#include <malloc.h>
#include <time.h>
#include <errno.h>
#endif

#include <string.h>
#include <stdint.h>

Acache mdfat[MDFATCACHESIZE];
Acache dfat[DFATCACHESIZE];
Acache bitfat[BITFATCACHESIZE];

extern unsigned long int dmsdos_speedup;

#ifdef __DMSDOS_LIB__

/* we don't need locking in the library */
void lock_mdfat(void) {}
void unlock_mdfat(void) {}
void lock_dfat(void) {}
void unlock_dfat(void) {}
void lock_bitfat(void) {}
void unlock_bitfat(void) {}

#else

DECLARE_MUTEX(mdfat_sem);   /* Must be initialized to green light */
void lock_mdfat(void) {down(&mdfat_sem);}
void unlock_mdfat(void) {up(&mdfat_sem);}

DECLARE_MUTEX(dfat_sem);   /* Must be initialized to green light */
void lock_dfat(void) {down(&dfat_sem);}
void unlock_dfat(void) {up(&dfat_sem);}

DECLARE_MUTEX(bitfat_sem);   /* Must be initialized to green light */
void lock_bitfat(void) {down(&bitfat_sem);}
void unlock_bitfat(void) {up(&bitfat_sem);}

#endif /* else / __DMSDOS_LIB__ */

int acache_get(struct super_block *sb, Acache *acache, int area, int never,
               int cachesize)
{
    unsigned long min_time;
    unsigned int min_acc;
    int index;
    int i;

    LOG_ACACHE("DMSDOS: acache_get area=%d never=%d\n", area, never);

    min_time = acache[0].a_time;
    min_acc = acache[0].a_acc;
    index = 0;

    if (never == 0) {
        min_time = acache[1].a_time;
        min_acc = acache[1].a_acc;
        index = 1;
    }

    /* find area and dev in cache */
    for (i = 0; i < cachesize; ++i) {
        if ((acache[i].a_time < min_time ||
                (acache[i].a_time == min_time && acache[i].a_acc < min_acc)
            ) && never != i) {
            min_time = acache[i].a_time;
            min_acc = acache[i].a_acc;
            index = i;
        }

        if (acache[i].a_buffer != NULL && area == acache[i].a_area && sb->s_dev == acache[i].a_sb->s_dev) {
            /* found */
            if (acache[i].a_time == CURRENT_TIME) { ++acache[i].a_acc; }
            else {
                acache[i].a_time = CURRENT_TIME;
                acache[i].a_acc = 0;
            }

            index = i;
            return index;
        }
    }

    /* index = least recently used entry number */
    if (acache[index].a_buffer != NULL) {
        raw_brelse(acache[index].a_sb, acache[index].a_buffer);
    }

    LOG_ACACHE("DMSDOS: acache_get: reading area %d\n", area);

    if ((acache[index].a_buffer = raw_bread(sb, area)) == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read acache area=%d\n", area);
        return -EIO;
    }

    acache[index].a_area = area;
    acache[index].a_time = CURRENT_TIME;
    acache[index].a_acc = 0;
    acache[index].a_sb = sb;
    return index;
}

void u_dumpcache(Acache *c)
{
    printk(KERN_INFO "area=%d time=%ld acc=%d buffer=%p dev=0x%x\n", c->a_area, c->a_time,
           c->a_acc, c->a_buffer,
           /* check validity of sb before dereferencing to s_dev :) */
           (c->a_buffer != NULL && c->a_sb != NULL) ? c->a_sb->s_dev : 0);
}

void dumpcache(void)
{
    int i;

    printk(KERN_INFO "DMSDOS: mdfat cache:\n");

    for (i = 0; i < MDFATCACHESIZE; ++i) { u_dumpcache(&(mdfat[i])); }

    printk(KERN_INFO "DMSDOS: dfat cache:\n");

    for (i = 0; i < DFATCACHESIZE; ++i) { u_dumpcache(&(dfat[i])); }

    printk(KERN_INFO "DMSDOS: bitfat cache:\n");

    for (i = 0; i < BITFATCACHESIZE; ++i) { u_dumpcache(&(bitfat[i])); }
}

#ifndef __DMSDOS_LIB__
void get_memory_usage_acache(int *size, int *max)
{
    int i;
    int used = 0;

    for (i = 0; i < MDFATCACHESIZE; ++i)if (mdfat[i].a_buffer) { ++used; }

    for (i = 0; i < DFATCACHESIZE; ++i)if (dfat[i].a_buffer) { ++used; }

    for (i = 0; i < BITFATCACHESIZE; ++i)if (bitfat[i].a_buffer) { ++used; }

    if (size) { *size = used * SECTOR_SIZE; }

    if (max) { *max = (MDFATCACHESIZE + DFATCACHESIZE + BITFATCACHESIZE) * SECTOR_SIZE; }
}

void u_free_idle_cache(Acache *c)
{
    if (c->a_buffer != NULL && c->a_time - CURRENT_TIME > MAX_CACHE_TIME) {
        raw_brelse(c->a_sb, c->a_buffer);
        c->a_buffer = NULL;
        c->a_time = 0;
        c->a_acc = 0;
    }
}

void free_idle_cache(void)
{
    int i;

    lock_mdfat();

    for (i = 0; i < MDFATCACHESIZE; ++i) { u_free_idle_cache(&(mdfat[i])); }

    unlock_mdfat();
    lock_dfat();

    for (i = 0; i < DFATCACHESIZE; ++i) { u_free_idle_cache(&(dfat[i])); }

    unlock_dfat();
    lock_bitfat();

    for (i = 0; i < BITFATCACHESIZE; ++i) { u_free_idle_cache(&(bitfat[i])); }

    unlock_bitfat();

    /* handle cluster cache */
    free_idle_ccache();
}
#endif

int dbl_mdfat_value(struct super_block *sb, int clusternr,
                    Mdfat_entry *new,
                    Mdfat_entry *mde)
{
    int area;
    int pos;
    int offset;
    int merk_i;
#ifdef DMSDOS_CONFIG_STAC
    int i;
    int merk_i2;
    int nr_of_bytes;
#endif
#ifdef DMSDOS_CONFIG_DBL
    unsigned char *pp;
#endif
#ifdef DMSDOS_CONFIG_DBLSP_DRVSP
    int res;
#endif
#ifdef DMSDOS_CONFIG_STAC
    unsigned char mdfat_raw_field[5];
#endif
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    if (clusternr < 2 || clusternr > dblsb->s_max_cluster2) {
        printk(KERN_ERR "DMSDOS: illegal mdfat access (cluster=%d max_cluster2=%d)\n",
               clusternr, dblsb->s_max_cluster2);
        goto fake_mde;
    }

    switch (dblsb->s_cvf_version) {
#ifdef DMSDOS_CONFIG_STAC

    case STAC3:
    case STAC4:
        if (dblsb->s_16bitfat) { pos = clusternr * 4; }
        else { pos = clusternr * 3; }

        area = pos / SECTOR_SIZE;
        offset = pos % SECTOR_SIZE;
        area = (area / 6) * 9 + (area % 6) + 3 + dblsb->s_fatstart; /* yes!!! */
        lock_mdfat();
        merk_i = acache_get(sb, mdfat, area, -1, MDFATCACHESIZE);

        if (merk_i < 0) { goto mdfat_error; }

        nr_of_bytes = 3;

        if (dblsb->s_16bitfat) { nr_of_bytes = 4; }

        /* read 2nd sector if necessary */
        if (offset + nr_of_bytes - 1 > 511) {
            merk_i2 = acache_get(sb, mdfat, area + 1, merk_i, MDFATCACHESIZE);

            if (merk_i2 < 0) {++area; goto mdfat_error;}
        } else { merk_i2 = merk_i; }

        /* copy data in mdfat_raw_field (nr_of_bytes byte) */
        mdfat_raw_field[3] = 0; /* in case only 3 bytes to read */

        for (i = 0; i < nr_of_bytes; ++i) {
            if (i + offset < 512) {
                mdfat_raw_field[i] = mdfat[merk_i].a_buffer->b_data[i + offset];
            } else {
                mdfat_raw_field[i] = mdfat[merk_i2].a_buffer->b_data[i + offset - 512];
            }
        }

        /* setup mde */
        mde->sector_minus_1 = CHS(mdfat_raw_field) + ((mdfat_raw_field[3] & 0x3f) << 16) - 1;
        mde->unknown = (mdfat_raw_field[2]) & 0xf0; /* same as flags here */
        mde->size_lo_minus_1 = (mdfat_raw_field[2] & 0xf) + ((mdfat_raw_field[3] >> 2) & 0x30);
        mde->size_hi_minus_1 = mde->size_lo_minus_1; /* for compatibility */
        mde->flags = (mdfat_raw_field[2]) & 0xf0; /* unshifted like in sd4_c.cc */

        /* set used and compressed bits in flags (for compatibility) */
        /* Hi Pavel: Is there a bug here? sector seems to be sometimes 1 for
           empty stacker mdfat slots. I think it should be 0 ? This showed up in
           the new fs checking code as dead sectors. Hmm.... Should we tolerate
           0 and 1 here ? But then the stacker fs check complains later... */
        /* Hi Frank, they are normal deleted clusters, stored for DOS undel.
           They should be deleted automaticaly, when free space becomes low.
           At this moment is best to ignore them in your fat simple check,
           they are counted by stac simple check */
        if (mde->sector_minus_1 + 1) { mde->flags |= 2; } /*set 'used' flag*/

        switch (mde->flags & 0xa0) {
        case 0x80:
        case 0x00:
            if (mde->size_lo_minus_1 + 1 == dblsb->s_sectperclust) {
                mde->flags |= 1;
            }

            break;

        case 0x20:
            mde->flags |= 1;
            break;

        default:
            /* correct length for compatibility */
            mde->size_hi_minus_1 = dblsb->s_sectperclust - 1;
        }

        LOG_MDFAT("DMSDOS: dbl_mdfat_value: cluster %u\n",
                  (unsigned)clusternr);
        LOG_MDFAT("    sector %u len %u flags 0x%X raw 0x%02X 0x%02X 0x%02X 0x%02X\n",
                  (unsigned)(mde->sector_minus_1 + 1), (unsigned)(mde->size_lo_minus_1 + 1),
                  (unsigned)mde->flags,
                  (unsigned)(mdfat_raw_field[0]), (unsigned)(mdfat_raw_field[1]),
                  (unsigned)(mdfat_raw_field[2]), (unsigned)(mdfat_raw_field[3]));
        LOG_MDFAT("    pos %u area %u offset %u\n",
                  (unsigned)pos, (unsigned)area, (unsigned)offset);

        /* if new!=NULL, setup nr_of_bytes byte from new in mdfat_raw_field */
        if (new) {
            mdfat_raw_field[0] = (new->sector_minus_1 + 1);
            /* unknown bits ignored */
            mdfat_raw_field[1] = (new->sector_minus_1 + 1) >> 8;
            mdfat_raw_field[3] = ((new->sector_minus_1 + 1) >> 16) & 0x3f;
            mdfat_raw_field[3] |= (new->size_lo_minus_1 << 2) & 0xc0;
            mdfat_raw_field[2] = new->size_lo_minus_1 & 0x0f;
            mdfat_raw_field[2] |= new->flags & 0xf0; /* unshifted like in sd4_c.cc */

            /* write back mdfat_raw_entry */
            for (i = 0; i < nr_of_bytes; ++i) {
                if (i + offset < 512) {
                    mdfat[merk_i].a_buffer->b_data[i + offset] = mdfat_raw_field[i];
                } else {
                    mdfat[merk_i2].a_buffer->b_data[i + offset - 512] = mdfat_raw_field[i];
                }
            }

            /* mark buffer dirty (both if necessary) */
            raw_mark_buffer_dirty(sb, mdfat[merk_i].a_buffer, 1);

            if (merk_i != merk_i2) {
                raw_mark_buffer_dirty(sb, mdfat[merk_i2].a_buffer, 1);
            }

            /* write second mdfat if it exists */
            if (dblsb->s_2nd_fat_offset) {
                struct buffer_head *bh;

                bh = raw_getblk(sb,
                                mdfat[merk_i].a_area + dblsb->s_2nd_fat_offset);

                if (bh == NULL) {
                    printk(KERN_ERR "DMSDOS: unable to read second mdfat\n");
                    goto give_up;
                }

                memcpy(bh->b_data, mdfat[merk_i].a_buffer->b_data, SECTOR_SIZE);
                raw_set_uptodate(sb, bh, 1);
                raw_mark_buffer_dirty(sb, bh, 1);
                raw_brelse(sb, bh);

                if (merk_i != merk_i2) {
                    bh = raw_getblk(sb,
                                    mdfat[merk_i2].a_area + dblsb->s_2nd_fat_offset);

                    if (bh == NULL) {
                        printk(KERN_ERR "DMSDOS: unable to read second mdfat\n");
                        goto give_up;
                    }

                    memcpy(bh->b_data, mdfat[merk_i2].a_buffer->b_data, SECTOR_SIZE);
                    raw_set_uptodate(sb, bh, 1);
                    raw_mark_buffer_dirty(sb, bh, 1);
                    raw_brelse(sb, bh);
                }
            }

give_up:
            ;
        }

        unlock_mdfat();
        return 0;
#endif
#ifdef DMSDOS_CONFIG_DBLSP_DRVSP

    case DBLSP:
    case DRVSP:
        pos = (dblsb->s_dcluster + clusternr) * 4 + 512 * dblsb->s_mdfatstart;
        area = pos / SECTOR_SIZE;
        offset = (pos % SECTOR_SIZE);
        lock_mdfat();
        merk_i = acache_get(sb, mdfat, area, -1, MDFATCACHESIZE);

        if (merk_i < 0) { goto mdfat_error; }

        pp = &(mdfat[merk_i].a_buffer->b_data[offset]);
        res = CHL(pp);
        mde->sector_minus_1 = res & 0x1fffff;
        mde->unknown = (res & 0x00200000) >> 21;
        mde->size_lo_minus_1 = (res & 0x03c00000) >> 22;
        mde->size_hi_minus_1 = (res & 0x3c000000) >> 26;
        mde->flags = ((res & 0xC0000000) >> 30) & 3;

        if (new) {
            res = new->sector_minus_1;
            res &= 0x1fffff;
            /* unknown bit ??? don't know... set to zero */
            res |= new->size_lo_minus_1 << 22;
            res &= 0x03ffffff;
            res |= new->size_hi_minus_1 << 26;
            res &= 0x3fffffff;
            res |= new->flags << 30;
            pp[0] = res;
            pp[1] = res >> 8;
            pp[2] = res >> 16;
            pp[3] = res >> 24;
            raw_mark_buffer_dirty(sb, mdfat[merk_i].a_buffer, 1);
        }

        unlock_mdfat();
        return 0;
#endif
#ifdef DMSDOS_CONFIG_DRVSP3

    case DRVSP3:
        pos = (dblsb->s_dcluster + clusternr) * 5
              + ((dblsb->s_dcluster + clusternr) / 102) * 2
              + 512 * dblsb->s_mdfatstart;
        area = pos / SECTOR_SIZE;
        offset = (pos % SECTOR_SIZE);
        lock_mdfat();
        merk_i = acache_get(sb, mdfat, area, -1, MDFATCACHESIZE);

        if (merk_i < 0) { goto mdfat_error; }

        pp = &(mdfat[merk_i].a_buffer->b_data[offset]);

        /* setup mde */
        mde->sector_minus_1 = CHL(pp) & 0xffffff;
        mde->unknown = (CHL(pp) & 0x3000000) >> 24;
        mde->size_lo_minus_1 = (pp[3] >> 2) & 0x3f;
        mde->size_hi_minus_1 = pp[4] & 0x3f;
        mde->flags = (pp[4] >> 6) & 3;

        /* if new!=NULL, setup 5 byte from new in pp */
        if (new) {
            pp[0] = new->sector_minus_1/*&0xffffff ??? */;
            pp[1] = new->sector_minus_1 >> 8;
            pp[2] = new->sector_minus_1 >> 16;
            /*pp[3]=(new->sector_minus_1>>24)&3; ??? */
            pp[3] = new->unknown & 3; /* we need the fragmented bit here :) */
            pp[3] |= new->size_lo_minus_1 << 2;
            pp[4] = new->size_hi_minus_1 & 0x3f;
            pp[4] |= new->flags << 6;

            raw_mark_buffer_dirty(sb, mdfat[merk_i].a_buffer, 1);
        }

        unlock_mdfat();
        return 0;
#endif
    } /* end switch(dblsb->s_cvf_version) */

    printk(KERN_ERR "DMSDOS: dbl_mdfat_value: unknown version?? This is a bug.\n");
    goto fake_mde;

mdfat_error:
    unlock_mdfat();
    printk(KERN_ERR "DMSDOS: unable to read mdfat area %d for cluster %d\n", area,
           clusternr);

fake_mde:
    mde->sector_minus_1 = 0;
    mde->unknown = 0;
    mde->size_lo_minus_1 = 0;
    mde->size_hi_minus_1 = 0;
    mde->flags = 0;

    return -1;
}

int dbl_mdfat_cluster2sector(struct super_block *sb, int clusternr)
{
    Mdfat_entry mde;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    if (clusternr == 0) { return dblsb->s_rootdir; }

    if (dbl_mdfat_value(sb, clusternr, NULL, &mde) == 0) {
        return mde.sector_minus_1 + 1;
    }

    return -1;
}

int dbl_fat_nextcluster(struct super_block *sb, int clusternr, int *new)
{
    int area;
    int pos;
    int offset;
    int merk_i;
    int res;
    int newval;
    int merk_i2;
    int offset2;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    if (clusternr < 2 || clusternr > dblsb->s_max_cluster2) {
        printk(KERN_ERR "DMSDOS: illegal dfat access (cluster=%d max_cluster2=%d)\n",
               clusternr, dblsb->s_max_cluster2);
        return -1;
    }

    pos = dblsb->s_16bitfat ? clusternr << 1/**2*/ : (clusternr * 3)>>1/*/2*/;
    area = pos >> SECTOR_BITS/*pos/SECTOR_SIZE*/;
    offset = pos & (SECTOR_SIZE - 1)/*pos%SECTOR_SIZE*/;

    /* adapt area for stacker */
    if (dblsb->s_cvf_version >= STAC3) { area = (area / 3) * 9 + (area % 3); }

    area += dblsb->s_fatstart;

    lock_dfat();

    merk_i = acache_get(sb, dfat, area, -1, DFATCACHESIZE);

    if (merk_i < 0) {
dfat_error:
        unlock_dfat();
        printk(KERN_ERR "DMSDOS: unable to read dfat area %d for cluster %d\n", area,
               clusternr);
        return -1;
    }

    if (offset == 511) {
        /* overlap, must read following sector also */
        merk_i2 = acache_get(sb, dfat, area + 1, merk_i, DFATCACHESIZE);

        if (merk_i2 < 0) {++area; goto dfat_error;}

        offset2 = 0;
    } else {
        /* normal */
        merk_i2 = merk_i;
        offset2 = offset + 1;
    }

    LOG_DFAT("DMSDOS: FAT lookup: area=%d merk_i=%d merk_i2=%d offset=%d offset2=%d\n",
             area, merk_i, merk_i2, offset, offset2);
    LOG_DFAT("DMSDOS: FAT lookup: cluster=%d value(low=%d high=%d)\n",
             clusternr,
             dfat[merk_i].a_buffer->b_data[offset],
             dfat[merk_i2].a_buffer->b_data[offset2]);

    res = dfat[merk_i].a_buffer->b_data[offset];
    res &= 0xff; /* grmpf... sign problems !!!! this is brutal but it works */
    res |= (uint16_t)(dfat[merk_i2].a_buffer->b_data[offset2]) << 8;
    res &= 0xffff;

    if (new) {
        if (dblsb->s_16bitfat) { newval = *new & 0xFFFF; }
        else {
            if (clusternr & 1) { newval = (res & 0xF) | ((*new & 0xFFF) << 4); }
            else { newval = (res & 0xF000) | (*new & 0xFFF); }
        }

        dfat[merk_i].a_buffer->b_data[offset] = newval;
        dfat[merk_i2].a_buffer->b_data[offset2] = newval >> 8;
        raw_mark_buffer_dirty(sb, dfat[merk_i].a_buffer, 1);

        if (merk_i != merk_i2) {
            raw_mark_buffer_dirty(sb, dfat[merk_i2].a_buffer, 1);
        }

        /* write second fat if it exists */
        if (dblsb->s_2nd_fat_offset) {
            struct buffer_head *bh;

            bh = raw_getblk(sb,
                            dfat[merk_i].a_area + dblsb->s_2nd_fat_offset);

            if (bh == NULL) {
                printk(KERN_ERR "DMSDOS: unable to read second dfat\n");
                goto give_up;
            }

            memcpy(bh->b_data, dfat[merk_i].a_buffer->b_data, SECTOR_SIZE);
            raw_set_uptodate(sb, bh, 1);
            raw_mark_buffer_dirty(sb, bh, 1);
            raw_brelse(sb, bh);

            if (merk_i != merk_i2) {
                bh = raw_getblk(sb,
                                dfat[merk_i2].a_area + dblsb->s_2nd_fat_offset);

                if (bh == NULL) {
                    printk(KERN_ERR "DMSDOS: unable to read second dfat\n");
                    goto give_up;
                }

                memcpy(bh->b_data, dfat[merk_i2].a_buffer->b_data, SECTOR_SIZE);
                raw_set_uptodate(sb, bh, 1);
                raw_mark_buffer_dirty(sb, bh, 1);
                raw_brelse(sb, bh);
            }
        }

give_up:
        ;
    }

    unlock_dfat();

    if (dblsb->s_16bitfat) { return res >= 0xFFF7 ? -1 : res; }

    if (clusternr & 1) { res >>= 4; }
    else { res &= 0xfff; }

    return res >= 0xff7 ? -1 : res;
}

int dbl_bitfat_value(struct super_block *sb, int sectornr, int *new)
{
    int area;
    int pos;
    int offset;
    int merk_i;
#ifdef DMSDOS_CONFIG_DBL
    unsigned char *pp;
    int newval;
#endif
    int bitmask;
#ifdef DMSDOS_CONFIG_STAC
    int shiftval;
#endif
    int res;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    if (sectornr < dblsb->s_datastart) { return -1; }

    if (sectornr > dblsb->s_dataend) { return -1; }

    switch (dblsb->s_cvf_version) {
#ifdef DMSDOS_CONFIG_STAC3

    case STAC3:
        pos = ((sectornr - dblsb->s_datastart) >> 3);
        offset = pos & (SECTOR_SIZE - 1)/*pos%SECTOR_SIZE*/;
        area = (pos >> SECTOR_BITS)/*pos/SECTOR_SIZE*/ + dblsb->s_mdfatstart; /* misused */
        shiftval = ((sectornr - dblsb->s_datastart) & 7);
        bitmask = 0x1;
        lock_bitfat();
        merk_i = acache_get(sb, bitfat, area, -1, BITFATCACHESIZE);

        if (merk_i < 0) { goto bitfat_error; }

        res = (bitfat[merk_i].a_buffer->b_data[offset] >> shiftval)&bitmask;

        if (new) {
            if (dblsb->s_free_sectors >= 0 && res != 0 && *new == 0) { ++dblsb->s_free_sectors; }

            if (dblsb->s_free_sectors >= 0 && res == 0 && *new != 0) { --dblsb->s_free_sectors; }

            bitfat[merk_i].a_buffer->b_data[offset] &= ~(bitmask << shiftval);
            bitfat[merk_i].a_buffer->b_data[offset] |= (*new & bitmask) << shiftval;
            raw_mark_buffer_dirty(sb, bitfat[merk_i].a_buffer, 1);
        }

        unlock_bitfat();
        return res;
#endif
#ifdef DMSDOS_CONFIG_STAC4

    case STAC4:
        pos = ((sectornr - dblsb->s_datastart) >> 2);
        offset = pos & (SECTOR_SIZE - 1)/*pos%SECTOR_SIZE*/;
        area = (pos >> SECTOR_BITS)/*pos/SECTOR_SIZE*/ + dblsb->s_mdfatstart; /* misused */
        shiftval = ((sectornr - dblsb->s_datastart) & 3) << 1/**2*/;
        bitmask = 0x3;
        lock_bitfat();
        merk_i = acache_get(sb, bitfat, area, -1, BITFATCACHESIZE);

        if (merk_i < 0) { goto bitfat_error; }

        res = (bitfat[merk_i].a_buffer->b_data[offset] >> shiftval)&bitmask;

        if (new) {
            if (dblsb->s_free_sectors >= 0 && res != 0 && *new == 0) { ++dblsb->s_free_sectors; }

            if (dblsb->s_free_sectors >= 0 && res == 0 && *new != 0) { --dblsb->s_free_sectors; }

            bitfat[merk_i].a_buffer->b_data[offset] &= ~(bitmask << shiftval);
            bitfat[merk_i].a_buffer->b_data[offset] |= (*new & bitmask) << shiftval;
            raw_mark_buffer_dirty(sb, bitfat[merk_i].a_buffer, 1);
        }

        unlock_bitfat();
        return res;
#endif
#ifdef DMSDOS_CONFIG_DBL

    case DBLSP:
    case DRVSP:
    case DRVSP3:
        pos = SECTOR_SIZE + ((sectornr - dblsb->s_datastart) >> 4) * 2;
        area = pos >> SECTOR_BITS/*pos/SECTOR_SIZE*/;
        offset = pos & (SECTOR_SIZE - 1)/*(pos%SECTOR_SIZE)*/;
        bitmask = 0x8000 >> ((sectornr - dblsb->s_datastart) & 15);
        lock_bitfat();
        merk_i = acache_get(sb, bitfat, area, -1, BITFATCACHESIZE);

        if (merk_i < 0) { goto bitfat_error; }

        pp = &(bitfat[merk_i].a_buffer->b_data[offset]);
        res = CHS(pp);

        if (new) {
            newval = (*new) ? (res | bitmask) : (res & ~bitmask);
            pp[0] = newval;
            pp[1] = newval >> 8;
            raw_mark_buffer_dirty(sb, bitfat[merk_i].a_buffer, 1);
        }

        res = (res & bitmask) ? 1 : 0;

        if (new) {
            /* WARNING: variable res is different from above */
            if (dblsb->s_free_sectors >= 0 && res != 0 && *new == 0) { ++dblsb->s_free_sectors; }

            if (dblsb->s_free_sectors >= 0 && res == 0 && *new != 0) { --dblsb->s_free_sectors; }
        }

        unlock_bitfat();
        return res;
#endif
    }

    printk(KERN_ERR "DMSDOS: dbl_bitfat_value: version not found?? cannot happen\n");
    return -1;

bitfat_error:
    unlock_bitfat();
    printk(KERN_ERR "DMSDOS: unable to read bitfat area %d for sector %d\n", area,
           sectornr);
    return -1;
}

#ifndef __DMSDOS_LIB__
int dblspace_fat_access(struct super_block *sb, int clusternr, int newval)
{
    int cl;

    cl = dbl_fat_nextcluster(sb, clusternr, NULL);

    if (newval == -1) { return cl; }

    if (sb->s_flags & MS_RDONLY) {
        printk(KERN_ERR "DMSDOS: dblspace_fat_access: READ-ONLY filesystem\n");
        /* This is a bad hack in order to work around a problem with the
           FAT driver: The FAT driver assumes fat_access never fails. Thus
           returning -EROFS results in an endless loop (i.e. system hang)
           at least in fat_free. We return -1 here in order to simulate EOF
           which should break any loop in the FAT driver. */
        return /* -EROFS */ -1;
    }

    if (newval == 0) { delete_cache_cluster(sb, clusternr); }

    dbl_fat_nextcluster(sb, clusternr, &newval);

    if (cl < 0) { return -1; } /* see comment above -- just to be sure :) */

    /* if cl _is_ -1 (EOF) this is ok. */
    /* if it is a negative error it is replaced by EOF. */
    return cl;
}

int dblspace_bmap(struct inode *inode, int block)
{
#ifdef __FOR_KERNEL_2_3_99
    return dblspace_smap(inode, block);
#else
    printk(KERN_WARNING "DMSDOS: bmap called, unsupported!\n");
    return -EIO;
#endif
}

int dblspace_smap(struct inode *inode, int block)
{
    int cluster;
    int sect_offs;
    Dblsb *dblsb = MSDOS_SB(inode->i_sb)->private_data;

    cluster = block / dblsb->s_sectperclust;
    sect_offs = block % dblsb->s_sectperclust;

    LOG_FS("DMSDOS: smap called, block=%d cluster_offs=%d sector_offs=%d\n",
           block, cluster, sect_offs);

    if (inode->i_ino == MSDOS_ROOT_INO || (S_ISDIR(inode->i_mode) &&
                                           !MSDOS_I(inode)->i_start)) {
        if (block >= MSDOS_SB(inode->i_sb)->dir_entries >> MSDOS_DPS_BITS) {
            LOG_FS("DMSDOS: smap: root dir beyond end, returning 0\n");
            return 0;
        }

        LOG_FS("DMSDOS: smap: root dir, returning %d\n", block + FAKED_ROOT_DIR_OFFSET);
        return block + FAKED_ROOT_DIR_OFFSET;
    }

    if (!(cluster = get_cluster(inode, cluster))) {
        LOG_FS("DMSDOS: smap: get_cluster returned 0\n");
        return 0;
    }

    LOG_FS("DMSDOS: smap: returning vsector(cluster=%d sector=%d)\n",
           cluster, sect_offs);
    return ((cluster - 2) * dblsb->s_sectperclust) + sect_offs + FAKED_DATA_START_OFFSET;
}

/* clusternr is absolute, not relative to inode */
void dblspace_zero_new_cluster(struct inode *inode, int clusternr)
{
    /* we decide upon the inode whether this is a dir cluster */
    struct super_block *sb = inode->i_sb;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;
    Cluster_head *ch;
    int i;

    if (!S_ISDIR(inode->i_mode)) {
        /* we just throw the cluster away if it has an mdfat entry */
        delete_cache_cluster(sb, clusternr);
    } else {
        /* it may be in cache, so ... */
        ch = ch_read(sb, clusternr, C_NO_READ | C_KEEP_LOCK); /* I hope that noread is OK there, Pavel */

        if (ch == NULL) {
            printk(KERN_ERR "DMSDOS: zero_new_cluster: ch_noread failed???\n");
            return;
        }

        memset(ch->c_data, 0, dblsb->s_sectperclust * SECTOR_SIZE);

        if (DIR_MAY_BE_SHORT(dblsb)) { ch->c_length = SECTOR_SIZE; }
        else { ch->c_length = dblsb->s_sectperclust * SECTOR_SIZE; }

        /* ch_dirty_locked unlocks the cluster *after* write */
        i = ch_dirty_locked(ch, 0,
                            DIR_MAY_BE_COMPRESSED(dblsb) ? UC_NORMAL : UC_UNCOMPR);

        if (i < 0 && i != -EROFS) /* don't try until death on a read-only filesystem */
            ch_dirty_retry_until_success(ch, 0,
                                         DIR_MAY_BE_COMPRESSED(dblsb) ? UC_NORMAL : UC_UNCOMPR);

        ch_free(ch);
    }
}
#endif
