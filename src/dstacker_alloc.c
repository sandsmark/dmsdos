/*
dstacker_alloc.c

DMSDOS CVF-FAT module: stacker allocation routines.

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

/* some interface hacks */
#include"lib_interface.h"
#include<malloc.h>
#include<errno.h>

/* initializes Stac_cwalk structure, which can be used for sequential
   access to all sectors of cluster and when needed informs about
   all data areas in every sector. flg parameter speedups initialization
   when some informations are not necessarry
	flg = 0 .. only sector numbers
	flg = 1 .. sector numbers and data without ending suballocation
	flg = 2 .. sector numbers and exact data areas
	flg = 3 .. same as 2 but more checking
*/
int stac_cwalk_init(Stac_cwalk *cw, struct super_block *sb,
                    int clusternr, int flg)
{
    uint8_t *pp;
    unsigned u, v;
    int i;
    int last_sect;
    Mdfat_entry mde;
    struct buffer_head *bh;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;
    cw->finfo = NULL;
    cw->fbh = NULL;
    cw->sect = 0;
    cw->sb = sb;
    cw->clusternr = clusternr;
    /* -------------------------------------------- */
    dbl_mdfat_value(sb, clusternr, NULL, &mde);
    cw->start_sect = mde.sector_minus_1 + 1;
    cw->sect_cnt = cw->start_len = mde.size_lo_minus_1 + 1;
    cw->flags = mde.flags;

    if (!cw->start_sect) {cw->fcnt = 0; return 0;};

    /* -------------------------------------------- */
    cw->fcnt = 1;

    cw->flen = cw->sect_cnt = cw->start_len;

    cw->bytes_in_clust = cw->sect_cnt * SECTOR_SIZE;

    cw->offset = 0;

    cw->bytes = SECTOR_SIZE;

    cw->bytes_in_last = 0;

    last_sect = cw->start_sect + cw->sect_cnt - 1;

    /* -------------------------------------------- */
    u = (cw->flags & 0xe0) >> 5;

    if (cw->start_len == dblsb->s_sectperclust) { u |= 0x8; }

    switch (u) {
    case 0x0: /* ffff=000x, len<ClustSize, compressed, regular file/dir */
    case 0x2: /* ffff=010x, deleted 0 */
        cw->compressed = 1;
        break;

    case 0x1: /* ffff=001x, not compressed but not full size, regular file/dir */
    case 0x3: /* ffff=011x, deleted 1 */
    case 0x8: /* ffff=000x, len=ClustSize, not compressed, regular file/dir */
    case 0xa: /* ffff=010x, deleted 8 */
    case 0xc: /* ffff=100x, len=ClustSize, directory, never compressed */
    case 0xe: /* ffff=110x, deleted c */
        cw->compressed = 0;
        break;

    case 0x4: /* ffff=100x, len<ClustSize, fragmented, regular file/dir */
    case 0x6: /* ffff=110x, deleted 4 */
        bh = raw_bread(sb, cw->start_sect);

        if (bh == NULL) { return -1; }

        pp = bh->b_data;

        if (pp[0] != 0xed) { /* check fragment signature */
            printk(KERN_ERR "DMSDOS: stac_cwalk_init: fragment signature not found cluster=%d\n",
                   clusternr);
            raw_brelse(sb, bh);
            return -1;
        }

        cw->fcnt = pp[1] + 1; /* number of pieces */

        if ((cw->fcnt > dblsb->s_sectperclust) || (cw->fcnt * 4 > SECTOR_SIZE)) {
            printk(KERN_ERR "DMSDOS: stac_cwalk_init: too much fragmented cluster=%d!\n",
                   clusternr);
            raw_brelse(sb, bh);
            return -1;
        };

        cw->compressed = !(pp[2] & 0x80);

        v = cw->sect_cnt;

        cw->sect_cnt += (pp[2] & 0x3F) + 1;

        for (i = 1; i < cw->fcnt; ++i) {
            pp += 4;
            u = (pp[2] & 0xf) + ((pp[3] >> 2) & 0x30);
            v += u + 1;
        };

        last_sect = pp[0] + (pp[1] << 8) + ((pp[3] & 0x3f) << 16) + u; /* for suballocation tests */

        if (v != cw->sect_cnt) {
            printk(KERN_ERR "DMSDOS: stac_cwalk_init: sector count mismash fragmented cluster=%d!\n",
                   clusternr);
            raw_brelse(sb, bh);
            return -1;
        }

        cw->fbh = bh;
        cw->finfo = bh->b_data + 4;
        cw->offset = 4 * cw->fcnt;
        cw->bytes = SECTOR_SIZE - cw->offset;
        cw->bytes_in_clust = (cw->sect_cnt - 1) * SECTOR_SIZE + cw->bytes;
        break;

    case 0x5: /* ffff=101x, len<ClustSize, suballocated, regular file/dir(?) */
    case 0x7: /* ffff=111x, deleted 5 */
    case 0xd: /* ffff=101x, len=ClustSize, suballocated, regular file/dir(?) */
    case 0xf: /* ffff=111x, deleted d */
        if (flg == 0) {cw->bytes_in_clust = 0; cw->bytes = 0; return 1;};

        bh = raw_bread(sb, cw->start_sect);

        if (bh == NULL) { return -1; }

        pp = &(bh->b_data[SECTOR_SIZE - 2]);

        if (CHS(pp) != 0x1234) {
            printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation signature not found cluster=%d\n",
                   clusternr);
            raw_brelse(sb, bh);
            return -1;
        }

        if (cw->start_len == 1) {
            /* short suballocation */
            pp = &(bh->b_data[SECTOR_SIZE - 6]);
            cw->offset = CHS(pp) & (SECTOR_SIZE - 1);  /* begin of area */
            cw->compressed = !(CHS(pp) & 0x8000);

            if (CHS(pp) & 0x4000) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation not present, cluster %d\n",
                       clusternr);
                raw_brelse(sb, bh);
                return -1;
            };

            pp = &(bh->b_data[SECTOR_SIZE - 8]);

            cw->bytes = CHS(pp) & (SECTOR_SIZE - 1); /* end of area */

            /* some more paranoic checking of allocated space */
            if (cw->bytes && (cw->bytes <= SECTOR_SIZE - 8) && (cw->bytes > cw->offset)) {
                cw->bytes -= cw->offset;
            } else {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: count = %d < 0 in short subalocated\n", cw->bytes);
                printk(KERN_ERR "DMSDOS: cluster %d read error\n", clusternr);
                raw_brelse(sb, bh);
                return -1;
            }

            cw->bytes_in_clust = cw->bytes;
            last_sect = 0;
        } else {
            /* long suballocation */
            pp = &(bh->b_data[SECTOR_SIZE - 8]);
            cw->offset = CHS(pp) & (SECTOR_SIZE - 1);
            cw->compressed = !(CHS(pp) & 0x8000);

            if (CHS(pp) & 0x4000) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation not present, cluster %d\n",
                       clusternr);
                raw_brelse(sb, bh);
                return -1;
            };

            cw->bytes = SECTOR_SIZE - cw->offset - 8;

            cw->bytes_in_clust += cw->bytes - SECTOR_SIZE;

            if (cw->bytes < 0) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: count = %d < 0 in long subalocated\n", cw->bytes);
                printk(KERN_ERR "DMSDOS: cluster %d read error\n", clusternr);
                raw_brelse(sb, bh);
                return -1;
            }
        };

        raw_brelse(sb, bh);

        break;

    case 0x9: /* ffff=001x, contradiction ??? */
    case 0xb: /* ffff=011x, deleted 9 */
    default:
        printk(KERN_ERR "DMSDOS: stac_cwalk_init: unknown flags 0x%2x cluster %d\n",
               mde.flags, clusternr);
        return -1;
    };

    /* if flg>=2 we are checking subalocated end of cluster */
    /* because of we did not know if stacker can subalocate clusters
       which are not in order we must check nonlinear
       suballocation */
    /* text above is question but now I know that stacker is able to do everything
       nonlinear suballocation regulary exist */
    if (last_sect && (dblsb->s_cvf_version >= STAC4) && (flg >= 2)) {
        /* check for subalocated end of cluster */
        /* 1) check start of next cluster for linear subalocation */
        if (clusternr < dblsb->s_max_cluster) {
            dbl_mdfat_value(sb, clusternr + 1, NULL, &mde);
            i = (mde.sector_minus_1 + 1) == last_sect;

            if (i && ((mde.flags & 0xA0) != 0xA0)) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: wrong cluster types for subalocation, cluster %d\n",
                       clusternr);
                return -1;
            };
        } else { i = 0; }

        /* 2) check for nonlinear subalocation */
        if ((u = dbl_bitfat_value(sb, last_sect, NULL)) == 0) if (flg >= 3) { goto error1; }

        if ((u > 1) || (i)) {
            if ((u <= 1) && (flg >= 3)) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation error 1, cluster %d\n",
                       clusternr);
                return -1;
            };

            if (!i)LOG_ALLOC("DMSDOS: stac_cwalk_init: nonlinear suballocation, cluster %d\n",
                                 clusternr);

            /* now we know that our cluster is subalocated, we must find
               number of bytes in last sector of cluster */
            bh = raw_bread(sb, last_sect);

            if (bh == NULL) { return -1; }

            pp = &(bh->b_data[SECTOR_SIZE - 2]);

            if (CHS(pp) != 0x1234) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation error 2, cluster %d\n",
                       clusternr);
                raw_brelse(sb, bh);
                return -1;
            };

            pp = &(bh->b_data[SECTOR_SIZE - 6]); /* begin of short suball. clust */

            u = CHS(pp) & (SECTOR_SIZE - 1);

            pp = &(bh->b_data[SECTOR_SIZE - 8]); /* begin of long suball. clust */

            v = CHS(pp) & (SECTOR_SIZE - 1);

            raw_brelse(sb, bh);

            /* u contains number of bytes of our cluster in last_sect */
            if (v < u) {
                pp = &(bh->b_data[SECTOR_SIZE - 6]);
                u = CHS(pp);
                pp = &(bh->b_data[SECTOR_SIZE - 8]);
                v = CHS(pp);
                printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation error 3, cluster %d, zerro offset 0x%X 0x%X\n",
                       clusternr, u, v);
                return -1;
            };

            cw->bytes_in_last = u;

            if (cw->sect_cnt > 1) {
                cw->bytes_in_clust -= SECTOR_SIZE - u;
            } else if ((i = cw->bytes + cw->offset - cw->bytes_in_last) > 0) {
                if ((cw->bytes -= i) <= 0) {
                    printk(KERN_ERR "DMSDOS: stac_cwalk_init: suballocation error 4, cluster %d\n",
                           clusternr);
                    return -1;
                };

                cw->bytes_in_clust -= i;
            };

            cw->bytes_in_last |= 0x4000;
        };
    };

    if ((i = cw->bytes_in_clust - dblsb->s_sectperclust * SECTOR_SIZE) > 0) {
        cw->bytes_in_clust -= i;

        if (!cw->bytes_in_last) { cw->bytes_in_last = SECTOR_SIZE - i; }
        else if ((cw->bytes_in_last -= i) < 0x4000) {
error1:
            printk(KERN_ERR "DMSDOS: stac_cwalk_init: bad bytes_in_cluster %d\n",
                   clusternr);
            return -1;
        };
    };

    return cw->bytes_in_clust;
}

/* returns in order all sectors of cluster */
/* in Stac_cwalk updates fields sect, offset and bytes */
int stac_cwalk_sector(Stac_cwalk *cw)
{
    if (!cw->sect) {
        if (!cw->fcnt) { return 0; }

        cw->fcnt--;
        cw->flen--;
        cw->sect = cw->start_sect;
    } else {
        if (!cw->flen) {
            if (!cw->fcnt) { return 0; }

            cw->fcnt--;

            if (cw->finfo == NULL) {
                printk(KERN_ERR "DMSDOS: stac_cwalk_sector: finfo==NULL, cluster %d\n",
                       cw->clusternr);
                return 0;
            };

            cw->sect = cw->finfo[0] + (cw->finfo[1] << 8) + ((cw->finfo[3] & 0x3f) << 16);

            cw->flen = (cw->finfo[2] & 0xf) + ((cw->finfo[3] >> 2) & 0x30);

            cw->finfo += 4;
        } else {
            cw->sect++;
            cw->flen--;
        };

        cw->offset = 0;

        if (!cw->flen && !cw->fcnt && cw->bytes_in_last) {
            cw->bytes = cw->bytes_in_last & (SECTOR_SIZE - 1);
        } else { cw->bytes = SECTOR_SIZE; }
    };

    return cw->sect;
}

void stac_cwalk_done(Stac_cwalk *cw)
{
    if (cw->fbh != NULL) { raw_brelse(cw->sb, cw->fbh); }
}


void stac_special_free(struct super_block *sb, int clusternr)
{
    int val;
    int sect;
    Mdfat_entry newmde, dummy_mde;
    Stac_cwalk cw;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    val = stac_cwalk_init(&cw, sb, clusternr, 0);

    if (val <= 0) {
        if (val < 0) {
            printk(KERN_ERR "DMSDOS: stac_special_free: alloc error in cluster %d\n", clusternr);
        } else {
            LOG_CLUST("DMSDOS: stac_special_free: already free cluster %d\n", clusternr);
        }

        return;
    };

    newmde.sector_minus_1 = -1;

    newmde.size_lo_minus_1 = 0;

    newmde.size_hi_minus_1 = 0;

    newmde.flags = 0;

    dbl_mdfat_value(sb, clusternr, &newmde, &dummy_mde);

    if ((cw.flags & 0xA0) == 0xA0) {
        /* mark part of suballocated sector as free */
        struct buffer_head *bh;
        bh = raw_bread(sb, cw.start_sect);

        if (bh != NULL) {
            if (cw.start_len == 1) {
                bh->b_data[SECTOR_SIZE - 6 + 1] |= 0x40;
            } else {
                bh->b_data[SECTOR_SIZE - 8 + 1] |= 0x40;
            }

            raw_mark_buffer_dirty(sb, bh, 1);
            raw_brelse(sb, bh);
        }
    }

    /* free sectors from BITFAT */
    while ((sect = stac_cwalk_sector(&cw)) > 0) {
        val = dbl_bitfat_value(sb, sect, NULL);

        if (val > 0) {
            --val;
            dbl_bitfat_value(sb, sect, &val);
            dblsb->s_full = 0;
            /* adapt s_free_sectors, -1 unknown */
            /*if(val==0&&dblsb->s_free_sectors>=0) dblsb->s_free_sectors++;*/
            /* Hi Pavel,
               I have commented this out since free sector count is now
               maintained in dbl_bitfat_value.
            */
        } else { LOG_CLUST("DMSDOS: stac_special_free: sector not alocated\n"); }
    }

    stac_cwalk_done(&cw);

}

/* replaces an existing cluster for stacker;
   this unusual function must be called before rewriting any file cluster;
   *** size must be known (encoded in mde) ***
   it does nothing if called too often;
   returns first sector nr
*/

int stac_replace_existing_cluster(struct super_block *sb, int cluster,
                                  int near_sector,
                                  Mdfat_entry *mde)
{
    Mdfat_entry old_mde, new_mde, dummy;
    int i;
    int newval;
    int sector;
    int old_sector;
    int old_size;
    int new_size;

    LOG_ALLOC("DMSDOS: stac_replace_existing_cluster cluster=%d near_sector=%d\n",
              cluster, near_sector);
    dbl_mdfat_value(sb, cluster, NULL, &old_mde);
    old_size = old_mde.size_lo_minus_1 + 1;
    old_sector = old_mde.sector_minus_1 + 1;
    new_size = mde->size_lo_minus_1 + 1;

    LOG_ALLOC("Old size: %d, old sector: %d\n", old_size, old_sector);

    if (old_mde.flags & 2) {
        /* stacker routines always replace mdfat entry */
        newval = 0;
        LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: freeing old sectors...\n");
        stac_special_free(sb, cluster);
        LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: freeing finished\n");
    }

    LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: call find_free_bitfat...\n");
    sector = find_free_bitfat(sb, near_sector, new_size);
    LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: find_free_bitfat returned %d\n",
              sector);

    if (sector <= 0) {
        if (old_mde.flags & 2) {
            /* Stacker routines don't have an undo list for now.
               We cannot restore the state before. Sorry data are lost now. */
            new_mde.sector_minus_1 = 0;
            new_mde.size_lo_minus_1 = 0;
            new_mde.size_hi_minus_1 = 0;
            new_mde.flags = mde->flags = 0;
            LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: deleting mdfat entry...\n");
            dbl_mdfat_value(sb, cluster, &new_mde, &dummy);
        }

        return -ENOSPC; /* disk full */
    }

    /* check whether really free (bug supposed in find_free_bitfat) */
    for (i = 0; i < new_size; ++i) {
        if (dbl_bitfat_value(sb, sector + i, NULL)) {
            printk(KERN_EMERG "DMSDOS: find_free_bitfat returned sector %d size %d but they are not all free!\n",
                   sector, new_size);
            panic("DMSDOS: stac_replace_existing_cluster: This is a bug - reboot and check filesystem\n");
            return -EIO;
        }
    }

    newval = 1;
    LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: allocating in bitfat...\n");

    for (i = 0; i < new_size; ++i) { dbl_bitfat_value(sb, sector + i, &newval); }

    new_mde.sector_minus_1 = sector - 1;
    new_mde.size_lo_minus_1 = mde->size_lo_minus_1;
    new_mde.size_hi_minus_1 = mde->size_hi_minus_1;
    new_mde.flags = mde->flags | 2;
    LOG_ALLOC("DMSDOS: stac_replace_existing_cluster: writing mdfat...\n");
    dbl_mdfat_value(sb, cluster, &new_mde, &dummy);
    return sector; /* okay */
}
