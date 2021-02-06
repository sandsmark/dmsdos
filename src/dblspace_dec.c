/*
dblspace_dec.c

DMSDOS CVF-FAT module: [dbl|drv]space cluster read and decompression routines.

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
#include<string.h>
#include<errno.h>

#ifdef __GNUC__
#define INLINE static inline
#else
/* non-gnu compilers may not like inline */
#define INLINE static
#endif

/* we always need DS decompression */

#if defined(__GNUC__) && defined(__i386__) && defined(USE_ASM)
#define USE_GNU_ASM_i386

/* copy block, overlaping part is replaced by repeat of previous part */
/* pointers and counter are modified to point after block */
#define M_MOVSB(D,S,C) \
__asm__ /*__volatile__*/(\
	"cld\n\t" \
	"rep\n\t" \
	"movsb\n" \
	:"=D" (D),"=S" (S),"=c" (C) \
	:"0" (D),"1" (S),"2" (C) \
	:"memory")


#else

#define M_MOVSB(D,S,C) for(;(C);(C)--) *((uint8_t*)(D)++)=*((uint8_t*)(S)++)

#endif

#if !defined(le16_to_cpu)
/* for old kernel versions - works only on i386 */
#define le16_to_cpu(v) (v)
#endif

/* for reading and writting from/to bitstream */
typedef
struct {
    uint32_t buf;	/* bit buffer */
    int pb;	/* already readed bits from buf */
    uint16_t *pd;	/* first not readed input data */
    uint16_t *pe;	/* after end of data */
} bits_t;

static const unsigned dblb_bmsk[] = {
    0x0, 0x1, 0x3, 0x7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF,
    0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF
};

/* read next 16 bits from input */
#define RDN_G16(bits) \
   { \
    (bits).buf>>=16; \
    (bits).pb-=16; \
    if((bits).pd<(bits).pe) \
    { \
     (bits).buf|=((uint32_t)(le16_to_cpu(*((bits).pd++))))<<16; \
    }; \
   }

/* prepares at least 16 bits for reading */
#define RDN_PR(bits,u) \
   { \
    if((bits).pb>=16) RDN_G16(bits); \
    u=(bits).buf>>(bits).pb; \
   }

/* initializes reading from bitstream */
INLINE void dblb_rdi(bits_t *pbits, void *pin, unsigned lin)
{
    pbits->pb = 32;
    pbits->pd = (uint16_t *)pin;
    pbits->pe = pbits->pd + ((lin + 1) >> 1);
}

/* reads n<=16 bits from bitstream *pbits */
INLINE unsigned dblb_rdn(bits_t *pbits, int n)
{
    unsigned u;
    RDN_PR(*pbits, u);
    pbits->pb += n;
    u &= dblb_bmsk[n];
    return u;
}

INLINE int dblb_rdoffs(bits_t *pbits)
{
    unsigned u;
    RDN_PR(*pbits, u);

    switch (u & 3) {
    case 0:
    case 2:
        pbits->pb += 1 + 6;
        return 63 & (u >> 1);

    case 1:
        pbits->pb += 2 + 8;
        return (255 & (u >> 2)) + 64;
    }

    pbits->pb += 2 + 12;
    return (4095 & (u >> 2)) + 320;
}

INLINE int dblb_rdlen(bits_t *pbits)
{
    unsigned u;
    RDN_PR(*pbits, u);

    switch (u & 15) {
    case  1:
    case  3:
    case  5:
    case  7:
    case  9:
    case 11:
    case 13:
    case 15:
        pbits->pb++;
        return 3;

    case  2:
    case  6:
    case 10:
    case 14:
        pbits->pb += 2 + 1;
        return (1 & (u >> 2)) + 4;

    case  4:
    case 12:
        pbits->pb += 3 + 2;
        return (3 & (u >> 3)) + 6;

    case  8:
        pbits->pb += 4 + 3;
        return (7 & (u >> 4)) + 10;

    case  0:
        ;
    }

    switch ((u >> 4) & 15) {
    case  1:
    case  3:
    case  5:
    case  7:
    case  9:
    case 11:
    case 13:
    case 15:
        pbits->pb += 5 + 4;
        return (15 & (u >> 5)) + 18;

    case  2:
    case  6:
    case 10:
    case 14:
        pbits->pb += 6 + 5;
        return (31 & (u >> 6)) + 34;

    case  4:
    case 12:
        pbits->pb += 7 + 6;
        return (63 & (u >> 7)) + 66;

    case  8:
        pbits->pb += 8 + 7;
        return (127 & (u >> 8)) + 130;

    case  0:
        ;
    }

    pbits->pb += 9;

    if (u & 256) { return dblb_rdn(pbits, 8) + 258; }

    return -1;
}

INLINE int dblb_decrep(bits_t *pbits, uint8_t **p, void *pout, uint8_t *pend,
                       int repoffs, int k, int flg)
{
    int replen;
    uint8_t *r;

    if (repoffs == 0) {LOG_DECOMP("DMSDOS: decrb: zero offset ?\n"); return -2;}

    if (repoffs == 0x113f) {
        int pos = *p - (uint8_t *)pout;
        LOG_DECOMP("DMSDOS: decrb: 0x113f sync found.\n");

        if ((pos % 512) && !(flg & 0x4000)) {
            LOG_DECOMP("DMSDOS: decrb: sync at decompressed pos %d ?\n", pos);
            return -2;
        }

        return 0;
    }

    replen = dblb_rdlen(pbits) + k;

    if (replen <= 0)
    {LOG_DECOMP("DMSDOS: decrb: illegal count ?\n"); return -2;}

    if ((uint8_t *)pout + repoffs > *p)
    {LOG_DECOMP("DMSDOS: decrb: of>pos ?\n"); return -2;}

    if (*p + replen > pend)
    {LOG_DECOMP("DMSDOS: decrb: output overfill ?\n"); return -2;}

    r = *p - repoffs;
    M_MOVSB(*p, r, replen);
    return 0;
}

/* DS decompression */
/* flg=0x4000 is used, when called from stacker_dec.c, because of
   stacker does not store original cluster size and it can mean,
   that last cluster in file can be ended by garbage */
int ds_dec(void *pin, int lin, void *pout, int lout, int flg)
{
    uint8_t *p, *pend;
    unsigned u, repoffs;
    int r;
    bits_t bits;

    dblb_rdi(&bits, pin, lin);
    p = (uint8_t *)pout;
    pend = p + lout;

    if ((dblb_rdn(&bits, 16)) != 0x5344) { return -1; }

    u = dblb_rdn(&bits, 16);
    LOG_DECOMP("DMSDOS: DS decompression version %d\n", u);

    do {
        r = 0;
        RDN_PR(bits, u);

        switch (u & 3) {
        case 0:
            bits.pb += 2 + 6;
            repoffs = (u >> 2) & 63;
            r = dblb_decrep(&bits, &p, pout, pend, repoffs, -1, flg);
            break;

        case 1:
            bits.pb += 2 + 7;
            *(p++) = (u >> 2) | 128;
            break;

        case 2:
            bits.pb += 2 + 7;
            *(p++) = (u >> 2) & 127;
            break;

        case 3:
            if (u & 4) {  bits.pb += 3 + 12; repoffs = ((u >> 3) & 4095) + 320; }
            else  {  bits.pb += 3 + 8;  repoffs = ((u >> 3) & 255) + 64; };

            r = dblb_decrep(&bits, &p, pout, pend, repoffs, -1, flg);

            break;
        }
    } while ((r == 0) && (p < pend));

    if (r < 0) { return r; }

    if (!(flg & 0x4000)) {
        u = dblb_rdn(&bits, 3);

        if (u == 7) { u = dblb_rdn(&bits, 12) + 320; }

        if (u != 0x113f) {
            LOG_DECOMP("DMSDOS: decrb: final sync not found?\n");
            return -2;
        }
    }

    return p - (uint8_t *)pout;
}

/* JM decompression */
int jm_dec(void *pin, int lin, void *pout, int lout, int flg)
{
    uint8_t *p, *pend;
    unsigned u, repoffs;
    int r;
    bits_t bits;

    dblb_rdi(&bits, pin, lin);
    p = (uint8_t *)pout;
    pend = p + lout;

    if ((dblb_rdn(&bits, 16)) != 0x4D4A) { return -1; }

    u = dblb_rdn(&bits, 16);
    LOG_DECOMP("DMSDOS: JM decompression version %d\n", u);

    do {
        r = 0;
        RDN_PR(bits, u);

        switch (u & 3) {
        case 0:
        case 2:
            bits.pb += 8;
            *(p++) = (u >> 1) & 127;
            break;

        case 1:
            bits.pb += 2;
            repoffs = dblb_rdoffs(&bits);
            r = dblb_decrep(&bits, &p, pout, pend, repoffs, 0, flg);
            break;

        case 3:
            bits.pb += 9;
            *(p++) = ((u >> 2) & 127) | 128;
            break;
        }
    } while ((r == 0) && (p < pend));

    if (r < 0) { return r; }

    if (!(flg & 0x4000)) {
        u = dblb_rdn(&bits, 2);

        if (u == 1) { u = dblb_rdoffs(&bits); }

        if (u != 0x113f) {
            LOG_DECOMP("DMSDOS: decrb: final sync not found?\n");
            return -2;
        }
    }

    return p - (uint8_t *)pout;
}


/* decompress a compressed doublespace/drivespace cluster clusterk to clusterd
*/
int dbl_decompress(unsigned char *clusterd, unsigned char *clusterk,
                   Mdfat_entry *mde)
{
    int sekcount;
    int r, lin, lout;

    sekcount = mde->size_hi_minus_1 + 1;
    lin = (mde->size_lo_minus_1 + 1) * SECTOR_SIZE;
    lout = (mde->size_hi_minus_1 + 1) * SECTOR_SIZE;

    switch (clusterk[0] + ((int)clusterk[1] << 8) +
            ((int)clusterk[2] << 16) + ((int)clusterk[3] << 24)) {
    case DS_0_0:
    case DS_0_1:
    case DS_0_2:
        LOG_DECOMP("DMSDOS: decompressing DS-0-x\n");
        r = ds_dec(clusterk, lin, clusterd, lout, 0);

        if (r <= 0) {
            printk(KERN_ERR "DMSDOS: error in DS-0-x compressed data.\n");
            return -2;
        }

        LOG_DECOMP("DMSDOS: decompress finished.\n");
        return 0;

    case JM_0_0:
    case JM_0_1:
        LOG_DECOMP("DMSDOS: decompressing JM-0-x\n");
        r = jm_dec(clusterk, lin, clusterd, lout, 0);

        if (r <= 0) {
            printk(KERN_ERR "DMSDOS: error in JM-0-x compressed data.\n");
            return -2;
        }

        LOG_DECOMP("DMSDOS: decompress finished.\n");
        return 0;

//#ifdef DMSDOS_CONFIG_DRVSP3

    case SQ_0_0:
        LOG_DECOMP("DMSDOS: decompressing SQ-0-0\n");
        r = sq_dec(clusterk, lin, clusterd, lout, 0);

        if (r <= 0) {
            printk(KERN_ERR "DMSDOS: SQ-0-0 decompression failed.\n");
            return -1;
        }

        LOG_DECOMP("DMSDOS: decompress finished.\n");
        return 0;
//#endif

    default:
        printk(KERN_ERR "DMSDOS: compression method not recognized.\n");
        return -1;

    } /* end switch */

    return 0;
}

//#ifdef DMSDOS_CONFIG_DRVSP3
/* read the fragments of a fragmented cluster and assemble them */
/* warning: this is guessed from low level viewing drivespace 3 disks
   and may be awfully wrong... we'll see... */
int read_fragments(struct super_block *sb, Mdfat_entry *mde, unsigned char *data)
{
    struct buffer_head *bh;
    struct buffer_head *bh2;
    int fragcount;
    int fragpnt;
    int offset;
    int sector;
    int seccount;
    int membytes;
    int safety_counter;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    /* read first sector */
    sector = mde->sector_minus_1 + 1;
    bh = raw_bread(sb, sector);

    if (bh == NULL) { return -EIO; }

    fragcount = bh->b_data[0];

    if (bh->b_data[1] != 0 || bh->b_data[2] != 0 || bh->b_data[3] != 0 || fragcount <= 0 ||
            fragcount > dblsb->s_sectperclust) {
        printk(KERN_ERR "DMSDOS: read_fragments: cluster does not look fragmented!\n");
        raw_brelse(sb, bh);
        return -EIO;
    }

    membytes = dblsb->s_sectperclust * SECTOR_SIZE;

    if (mde->flags & 1) {
        offset = 0;
        safety_counter = 0;
    } else {
        offset = (fragcount + 1) * 4;
        /* copy the rest of the sector */
        memcpy(data, &(bh->b_data[offset]), SECTOR_SIZE - offset);
        data += (SECTOR_SIZE - offset);
        safety_counter = SECTOR_SIZE - offset;
    }

    ++sector;
    seccount = mde->size_lo_minus_1;
    fragpnt = 1;

    while (fragpnt <= fragcount) {
        if (fragpnt > 1) {
            /* read next fragment pointers */
            seccount = bh->b_data[fragpnt * 4 + 3];
            seccount &= 0xff;
            seccount /= 4;
            seccount += 1;
            sector = bh->b_data[fragpnt * 4];
            sector &= 0xff;
            sector += bh->b_data[fragpnt * 4 + 1] << 8;
            sector &= 0xffff;
            sector += bh->b_data[fragpnt * 4 + 2] << 16;
            sector &= 0xffffff;
            sector += 1;
        }

        while (seccount) {
            bh2 = raw_bread(sb, sector);

            if (bh2 == NULL) {raw_brelse(sb, bh); return -EIO;}

            /*printk(KERN_DEBUG "DMSDOS: read_fragments: data=0x%p safety_counter=0x%x sector=%d\n",
                   data,safety_counter,sector);*/
            if (safety_counter + SECTOR_SIZE > membytes) {
                int maxbytes = membytes - safety_counter;

                if (maxbytes <= 0) {
                    printk(KERN_WARNING "DMSDOS: read_fragments: safety_counter exceeds membytes!\n");
                    raw_brelse(sb, bh2);
                    raw_brelse(sb, bh);
                    return -EIO;
                }

                printk(KERN_DEBUG "DMSDOS: read_fragments: size limit reached.\n");
                memcpy(data, bh2->b_data, maxbytes);
                raw_brelse(sb, bh2);
                raw_brelse(sb, bh);
                return membytes;
            } else { memcpy(data, bh2->b_data, SECTOR_SIZE); }

            raw_brelse(sb, bh2);
            data += SECTOR_SIZE;
            safety_counter += SECTOR_SIZE;
            ++sector;
            --seccount;
        }

        ++fragpnt;
    }

    raw_brelse(sb, bh);

    return safety_counter;
}
//#endif

//#ifdef DMSDOS_CONFIG_DBL
/* read a complete file cluster and decompress it if necessary;
   this function is unable to read cluster 0 (CVF root directory) */
/* returns cluster length in bytes or error (<0) */
/* this function is specific to doublespace/drivespace */
int dbl_read_cluster(struct super_block *sb,
                     unsigned char *clusterd, int clusternr)
{
    Mdfat_entry mde;
    unsigned char *clusterk;
    int nr_of_sectors;
    int i;
    struct buffer_head *bh;
    int membytes;
    int sector;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    LOG_CLUST("DMSDOS: dbl_read_cluster %d\n", clusternr);

    dbl_mdfat_value(sb, clusternr, NULL, &mde);

    if ((mde.flags & 2) == 0) {
        /* hmm, cluster is unused (it's a lost or ghost cluster)
           and contains undefined data, but it *is* readable */
        /* oh no, it contains ZEROD data per definition...
           this is really important */
        if (clusterd) { /*clusterd==NULL means read_ahead - don't do anything*/
            memset(clusterd, 0, dblsb->s_sectperclust * SECTOR_SIZE);
        }

        LOG_CLUST("DMSDOS: lost cluster %d detected\n", clusternr);
        return 0; /* yes, has length zero */
    }

    sector = mde.sector_minus_1 + 1;
    nr_of_sectors = mde.size_lo_minus_1 + 1; /* real sectors on disk */

    if (nr_of_sectors > dblsb->s_sectperclust) {
        printk(KERN_WARNING "DMSDOS: read_cluster: mdfat sectors > sectperclust, cutting\n");
        nr_of_sectors = dblsb->s_sectperclust;
    }

    if (clusterd == NULL) {
        /* read-ahead */
        dblspace_reada(sb, sector, nr_of_sectors);
        return 0;
    }

//#ifdef DMSDOS_CONFIG_DRVSP3

    if (mde.unknown & 2) {
        /* we suppose this bit indicates a fragmented cluster */
        /* this is *not sure* and may be awfully wrong - reports
           whether success or not are welcome
        */

        LOG_CLUST("DMSDOS: cluster %d has unknown bit #1 set. Assuming fragmented cluster.\n",
                  clusternr);

        if (mde.flags & 1) { /* not compressed */
            LOG_CLUST("DMSDOS: uncompressed fragmented cluster\n");
            i = read_fragments(sb, &mde, clusterd);

            if (i < 0) {
                printk(KERN_ERR "DMSDOS: read_fragments failed!\n");
                return i;
            }
        } else {
            LOG_CLUST("DMSDOS: compressed fragmented cluster\n");
            membytes = SECTOR_SIZE * dblsb->s_sectperclust;

            clusterk = (unsigned char *)MALLOC(membytes);

            if (clusterk == NULL) {
                printk(KERN_ERR "DMSDOS: no memory for decompression!\n");
                return -2;
            }

            /* returns length in bytes */
            i = read_fragments(sb, &mde, clusterk);

            if (i < 0) {
                printk(KERN_ERR "DMSDOS: read_fragments failed!\n");
                return i;
            }

            /* correct wrong size_lo information (sq_dec needs it) */
            if (i > 0) { mde.size_lo_minus_1 = (i - 1) / SECTOR_SIZE; }

            i = dbl_decompress(clusterd, clusterk, &mde);

            FREE(clusterk);

            if (i) {
                printk(KERN_ERR "DMSDOS: decompression of cluster %d in CVF failed.\n",
                       clusternr);
                return i;
            }

        }

        /* the slack must be zerod out */
        if (mde.size_hi_minus_1 + 1 < dblsb->s_sectperclust) {
            memset(clusterd + (mde.size_hi_minus_1 + 1)*SECTOR_SIZE, 0,
                   (dblsb->s_sectperclust - mde.size_hi_minus_1 - 1)*
                   SECTOR_SIZE);
        }

        return (mde.size_hi_minus_1 + 1) * SECTOR_SIZE;

    } /* end of read routine for fragmented cluster */

//#endif

    if (mde.flags & 1) {
        /* cluster is not compressed */
        for (i = 0; i < nr_of_sectors; ++i) {
            bh = raw_bread(sb, sector + i);

            if (bh == NULL) { return -EIO; }

            memcpy(&clusterd[i * SECTOR_SIZE], bh->b_data, SECTOR_SIZE);
            raw_brelse(sb, bh);
        }
    } else {
        /* cluster is compressed */

        membytes = SECTOR_SIZE * nr_of_sectors;

        clusterk = (unsigned char *)MALLOC(membytes);

        if (clusterk == NULL) {
            printk(KERN_ERR "DMSDOS: no memory for decompression!\n");
            return -2;
        }

        for (i = 0; i < nr_of_sectors; ++i) {
            bh = raw_bread(sb, sector + i);

            if (bh == NULL) {
                FREE(clusterk);
                return -EIO;
            }

            memcpy(&clusterk[i * SECTOR_SIZE], bh->b_data, SECTOR_SIZE);
            raw_brelse(sb, bh);
        }

        i = dbl_decompress(clusterd, clusterk, &mde);

        FREE(clusterk);

        if (i) {
            printk(KERN_ERR "DMSDOS: decompression of cluster %d in CVF failed.\n",
                   clusternr);
            return i;
        }

    }

    /* the slack must be zerod out */
    if (mde.size_hi_minus_1 + 1 < dblsb->s_sectperclust) {
        memset(clusterd + (mde.size_hi_minus_1 + 1)*SECTOR_SIZE, 0,
               (dblsb->s_sectperclust - mde.size_hi_minus_1 - 1)*
               SECTOR_SIZE);
    }

    return (mde.size_hi_minus_1 + 1) * SECTOR_SIZE;
}
//#endif

/* read a complete file cluster and decompress it if necessary;
   it must be able to read directories
   this function is unable to read cluster 0 (CVF root directory) */
/* returns cluster length in bytes or error (<0) */
/* this function is a generic wrapper */
int dmsdos_read_cluster(struct super_block *sb,
                        unsigned char *clusterd, int clusternr)
{
    int ret;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    LOG_CLUST("DMSDOS: read_cluster %d\n", clusternr);

    switch (dblsb->s_cvf_version) {
    case DBLSP:
    case DRVSP:
    case DRVSP3:
        ret = dbl_read_cluster(sb, clusterd, clusternr);
        break;
    case STAC3:
    case STAC4:
        ret = stac_read_cluster(sb, clusterd, clusternr);
        break;

    default:
        printk(KERN_ERR "DMSDOS: read_cluster: illegal cvf version flag!\n");
        ret = -EIO;
    }

    return ret;
}
