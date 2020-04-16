/*
dblspace_compr.c

DMSDOS CVF-FAT module: [dbl|drv]space cluster write and compression routines.

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

#include <string.h>

#if defined(__DMSDOS_LIB__)
extern unsigned long dmsdos_speedup;
#endif

#ifdef __DMSDOS_DAEMON__
extern int cfaktor;
void panic(char *);
#include <malloc.h>
#include <asm/types.h>
#include <asm/byteorder.h>
#define MALLOC malloc
#define FREE   free
#define SECTOR_SIZE 512
#endif

#ifdef __DMSDOS_LIB__
/* some interface hacks */
#include "lib_interface.h"
#include <malloc.h>
#include <errno.h>
#endif

int c_maxtrycount[12] = { 1, 2, 3, 4, 6, 8, 10, 14, 18, 22, 28, 40};

#ifdef __GNUC__
#define INLINE static inline
#else
/* non-gnu compilers do not like inline */
#define INLINE static
#endif

#if !defined(cpu_to_le16)
/* for old kernel versions - works only on i386 */
#define cpu_to_le16(v) (v)
#endif


/* we always need at least DS compression */

/* for reading and writting from/to bitstream */
typedef
struct {
    uint32_t buf;	/* bit buffer */
    int pb;	/* already written bits to buf */
    uint16_t *pd;	/* data write pointer */
    uint16_t *pe;	/* after end of data */
} bits_t;

/* initializes writting to bitstream */
INLINE void dblb_wri(bits_t *pbits, void *pin, unsigned lin)
{
    pbits->buf = 0;
    pbits->pb = 0;
    pbits->pd = (uint16_t *)pin;
    pbits->pe = pbits->pd + ((lin + 1) >> 1);
}

/* writes n<=16 bits to bitstream *pbits */
INLINE void dblb_wrn(bits_t *pbits, int cod, int n)
{
    pbits->buf |= cod << pbits->pb;

    if ((pbits->pb += n) >= 16) {
        if (pbits->pd < pbits->pe) {
            *(pbits->pd++) = cpu_to_le16((uint16_t)pbits->buf);
        } else if (pbits->pd == pbits->pe) { pbits->pd++; } /* output overflow */

        pbits->buf >>= 16;
        pbits->pb -= 16;
    }
}

void write_byte(bits_t *pbits, int byte, int method)
{
    if (method != JM_0_0 && method != JM_0_1) {
        if (byte < 128) { dblb_wrn(pbits, 2, 2); }
        else { dblb_wrn(pbits, 1, 2); }
    } else { /* JM_0_0 */
        if (byte < 128) { dblb_wrn(pbits, 0, 1); }
        else { dblb_wrn(pbits, 3, 2); }
    }

    dblb_wrn(pbits, byte & 0x7F, 7);
}

void write_temp(bits_t *pbits,
                unsigned char *tempstr, int len, int diff, int method)
{
    if (len == 1) {
        write_byte(pbits, tempstr[0], method);
        return;
    }

    if (len == 2 && (method == JM_0_0 || method == JM_0_1)) {
        write_byte(pbits, tempstr[0], method);
        write_byte(pbits, tempstr[1], method);
        return;
    }

    if (method != JM_0_0 && method != JM_0_1) {
        ++len; /* corrects different counting scheme */

        if (diff < 64) { dblb_wrn(pbits, 0, 2); }
        else { dblb_wrn(pbits, 3, 2); }
    } else { /* JM_0_0 */
        dblb_wrn(pbits, 1, 2);
        dblb_wrn(pbits, (diff < 64) ? 0 : 1, 1);
    }

    if (diff < 64) { dblb_wrn(pbits, diff, 6); }
    else {
        if (diff < 320) {
            dblb_wrn(pbits, 0, 1);
            dblb_wrn(pbits, diff - 64, 8);
        } else {
            dblb_wrn(pbits, 1, 1);
            dblb_wrn(pbits, diff - 320, 12);
        }
    }

    /* okay, now encode len */
    if (len == 3) {
        dblb_wrn(pbits, 1, 1);
        return;
    }

    if (len < 6) {
        dblb_wrn(pbits, 1 << 1, 2);
        dblb_wrn(pbits, len - 4, 1);
        return;
    }

    if (len < 10) {
        dblb_wrn(pbits, 1 << 2, 3);
        dblb_wrn(pbits, len - 6, 2);
        return;
    }

    if (len < 18) {
        dblb_wrn(pbits, 1 << 3, 4);
        dblb_wrn(pbits, len - 10, 3);
        return;
    }

    if (len < 34) {
        dblb_wrn(pbits, 1 << 4, 5);
        dblb_wrn(pbits, len - 18, 4);
        return;
    }

    if (len < 66) {
        dblb_wrn(pbits, 1 << 5, 6);
        dblb_wrn(pbits, len - 34, 5);
        return;
    }

    if (len < 130) {
        dblb_wrn(pbits, 1 << 6, 7);
        dblb_wrn(pbits, len - 66, 6);
        return;
    }

    if (len < 258) {
        dblb_wrn(pbits, 1 << 7, 8);
        dblb_wrn(pbits, len - 130, 7);
        return;
    }

    dblb_wrn(pbits, 1 << 8, 9);
    dblb_wrn(pbits, len - 258, 8);
}

void write_marker(bits_t *pbits, int method)
{
    if (method == JM_0_0 || method == JM_0_1) { dblb_wrn(pbits, 13, 4); }
    else /* DS_0_x */ { dblb_wrn(pbits, 7, 3); }

    dblb_wrn(pbits, 0xFFF, 12);
}

typedef uint16_t hash_t;

/* hashing function is only 2 char because of DS min rep */
INLINE unsigned dbl_hash(uint8_t *p)
{
    return (((uint16_t)p[0] << 4) ^ ((uint16_t)p[1] << 0)) & 0x3FF;
};

/* adds new hash and returns previous occurence */
INLINE hash_t dbl_newhash(uint8_t *clusterd, int pos,
                          hash_t *hash_tab, hash_t *hash_hist, unsigned hash_mask)
{
    hash_t *hash_ptr;
    hash_t  hash_cur;
    hash_ptr = hash_tab + dbl_hash(&(clusterd[pos]));
    hash_cur = *hash_ptr;		/* find previous occurence of hash */
    *hash_ptr = pos;			/* store new occurence */
    *(hash_hist + (pos & hash_mask)) = hash_cur;
    /* store previous in history table */
    return (hash_cur);
};

/* compresses a doublespace/drivespace cluster
   gets uncompressed size (number of used sectors)
   returns compressed size (in number of used sectors) or -1 if failed
*/
int dbl_compress(uint8_t *clusterk, uint8_t *clusterd, int size,
                 int method, int cf)
{
    bits_t bits;
    int i;
    int pos;
    int mark_pos; /* position of next marker */
    hash_t *hash_tab;
    /* [0x400] pointers to last occurrence of same hash, index hash */
    hash_t *hash_hist;
    /* [0x800] previous occurences of hash, index actual pointer&hash_mask */
    unsigned hash_mask = 0x7FF; /* mask for index into hash_hist */
    int hash_cur;
    int max_hash = 0; /* GCC likes this */
    int match;
    int max_match;
    int try_cn;
    int try_count = c_maxtrycount[cf]; /* tunable parameter */

    switch (method) {
    case DS_0_0:
    case DS_0_1:
    case DS_0_2: /* handled together with JM_0_0 because similar */
    case JM_0_0:
    case JM_0_1:

        /* maximal compression */
        if (cf > 8) { hash_mask = 0xFFF; }

        /* Input size in bytes */
        size = size * SECTOR_SIZE;

        /* initialize bitstream */
        dblb_wri(&bits, clusterk, size - SECTOR_SIZE);

        /* put magic number */
        dblb_wrn(&bits, method, 32);

        hash_tab = MALLOC(0x400 * sizeof(hash_t));

        if (hash_tab == NULL) { return -1; }

        hash_hist = MALLOC((hash_mask + 1) * sizeof(hash_t));

        if (hash_hist == NULL) {FREE(hash_tab); return -1;}

        for (i = 0; i < 0x400; i++) { hash_tab[i] = 0xFFFF; }

        for (i = 0; i <= hash_mask; i++) { hash_hist[i] = 0xFFFF; }

        pos = mark_pos = 0;

        while (pos < size) {
            mark_pos += SECTOR_SIZE;

            while (pos < mark_pos) {
                if (bits.pd > bits.pe) { goto error; }	/* incompressible data */

                if (pos + 1 >= size) { goto single_char; }	/* cannot be hashed */

                hash_cur = dbl_newhash(clusterd, pos, hash_tab, hash_hist, hash_mask);

                if (hash_cur >= pos) { goto single_char; }

                try_cn = try_count;
                max_match = 1;       /* minimal match - 1 */

                do {                 /* longer offsets are not allowed */
                    if (pos - hash_cur >= 0x113F) { break; }

                    /* speedup heuristic : new tested hash occurence must be
                       at least one char longer */
                    if ((clusterd[hash_cur + max_match] == clusterd[pos + max_match]) &&
                            (clusterd[hash_cur + max_match - 1] == clusterd[pos + max_match - 1]) &&
                            (clusterd[hash_cur] == clusterd[pos]))
                        /* second chars are equal from hash function */
                    {
                        for (match = 0; match < mark_pos - pos; match++)
                            if (clusterd[hash_cur + match] != clusterd[pos + match]) { break; }

                        if (match > max_match) { /* find maximal hash */
                            max_hash = hash_cur;
                            max_match = match;

                            if (match == mark_pos - pos) { break; }
                        };
                    };

                    i = hash_cur;
                } while (--try_cn && ((hash_cur = hash_hist[i & hash_mask]) < i));

                if (max_match < 2) { goto single_char; }

                write_temp(&bits, &(clusterd[pos]), max_match, pos - max_hash, method);
                pos++;
                max_match--;
                i = max_match;

                if (pos + i + 1 >= size) { i = size - pos - 1; }

                max_match -= i;	/* last char cannot be hashed */

                while (i--) {
                    dbl_newhash(clusterd, pos++, hash_tab, hash_hist, hash_mask);
                }

                pos += max_match;
                continue;

single_char:
                write_byte(&bits, clusterd[pos++], method);

            }

            write_marker(&bits, method);
        }

        dblb_wrn(&bits, 0, 15);	/* flush last bits from bits.buf */

        if (bits.pd > bits.pe) { goto error; }

        FREE(hash_tab);
        FREE(hash_hist);
        return (((uint8_t *)bits.pd - (uint8_t *)clusterk) - 1) / 512 + 1;

error:
        FREE(hash_tab);
        FREE(hash_hist);
        return -1;

#ifdef DMSDOS_CONFIG_DRVSP3

    case SQ_0_0:
        size = size * SECTOR_SIZE;
        /* sq_comp(void* pin,int lin, void* pout, int lout, int flg) */
        i = sq_comp(clusterd, size, clusterk, size, cf);

        if ((i <= 0) || (i + SECTOR_SIZE > size)) { return -1; }

        return ((i - 1) / SECTOR_SIZE + 1);
#endif

    default:
        /* sorry, other compression methods currently not available */
        return -1;
    }

    return -1;
}

#if defined(__DMSDOS_LIB__)
#ifdef DMSDOS_CONFIG_DRVSP3
int write_fragmented(struct super_block *sb, unsigned char *fraglist,
                     unsigned char *clusterk, Mdfat_entry *mde, int ksize)
{
    int i, j;
    int frags;
    int seccount;
    int sector;
    int bytecount = ksize * SECTOR_SIZE;
    int koffset = SECTOR_SIZE;
    struct buffer_head *bh;
    int c;

    frags = fraglist[0];

    if ((mde->flags & 1) == 0) { koffset = 4 * (frags + 1); }

    LOG_CLUST("DMSDOS: writing fragmented cluster, frags=%d\n", frags);

    /* now we have all information and can write the cluster data */
    for (i = 1; i <= frags; ++i) {
        seccount = fraglist[i * 4 + 3];
        seccount &= 0xff;
        seccount /= 4;
        seccount += 1;
        sector = fraglist[i * 4];
        sector &= 0xff;
        sector += fraglist[i * 4 + 1] << 8;
        sector &= 0xffff;
        sector += fraglist[i * 4 + 2] << 16;
        sector &= 0xffffff;
        sector += 1;

        for (j = 0; j < seccount; ++j) {
            bh = raw_getblk(sb, sector + j);

            if (bh == NULL) {
                printk(KERN_ERR "DMSDOS: write_fragmented: raw_getblk sector %d failed\n",
                       sector + j);
                return -EIO;
            }

            if (i == 1 && j == 0) {
                /* need to copy fraglist first */
                memcpy(bh->b_data, fraglist, 4 * (frags + 1));

                if (koffset < SECTOR_SIZE) {
                    c = SECTOR_SIZE - koffset;
                    memcpy(bh->b_data, clusterk, c);
                    bytecount -= c;
                    clusterk += c;
                }
            } else {
                c = SECTOR_SIZE;

                if (c > bytecount) { c = bytecount; }

                memcpy(bh->b_data, clusterk, c);
                bytecount -= c;
                clusterk += c;
            }

            raw_set_uptodate(sb, bh, 1);
            raw_mark_buffer_dirty(sb, bh, 1);
            raw_brelse(sb, bh);
        }
    }

    return 0;
}
#endif

/* write a dmsdos cluster, compress before if possible;
   length is the number of used bytes, may be < SECTOR_SIZE*sectperclust only
   in the last cluster of a file;
   cluster must be allocated by allocate_cluster before if it is a new one;
   unable to write dir clusters;
   to avoid MDFAT level fragmentation, near_sector should be the sector no
   of the preceeding cluster;
   if ucflag==UC_UNCOMPR uncompressed write is forced.
   if ucflag<0 raw write is forced with compressed size -ucflag (in bytes).
   if ucflag==UC_TEST simulate write is done (checks for space or reserves
   space for the cluster in the filesystem but does not actually write it).
   if ucflag==UC_DIRECT write compressed but don't use daemon - this is to
   guarantee that the data are on the disk when the function exits.

   *********** This function is doublespace/drivespace specific ******
*/

#ifdef DMSDOS_CONFIG_DBL
int dbl_write_cluster(struct super_block *sb,
                      unsigned char *clusterd, int length, int clusternr,
                      int near_sector, int ucflag)
{
    int method;
    unsigned char *clusterk;
    int size;
    Mdfat_entry mde;
    int sector;
    int i;
    int res;
    int ksize;
    struct buffer_head *bh;
    unsigned char fraglist[66 * 4];
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    LOG_CLUST("DMSDOS dbl_write_cluster clusternr=%d length=%d near_sector=%d\n",
              clusternr, length, near_sector);

    /* are we deleting a cluster ? */
    if (clusterd == NULL || length == 0) {
        free_cluster_sectors(sb, clusternr);
        return 0;
    }

    if (ucflag == UC_TEST) {
        if (dblsb->s_full == 0 &&
                /* well, this is estimated */
                dblsb->s_sectperclust * CCACHESIZE + 100 < dblsb->s_free_sectors
           ) { return 0; }
        else { return -ENOSPC; }
    }

    /* guess compression method if not already known */
    if (dblsb->s_comp == GUESS) {
        printk(KERN_INFO "DMSDOS: write_cluster: guessing compression method...\n");
        {
            if (dblsb->s_cvf_version == DRVSP) {
                dblsb->s_comp = JM_0_0;
                /* no doubt here, there's only this one possible - so exit */
                /* goto guess_ok; Hmm ... really...???? better let it scan */
            }

            if (dblsb->s_cvf_version == DRVSP3) {
                dblsb->s_comp = SQ_0_0;
                /* that's only a default in case the scan routine finds nothing */
            }

            /* DBLSP: we know nothing, it can be DS_0_2 for dos 6.0 and DS_0_0
               for win95, let's scan it */
            for (i = 2; i <= dblsb->s_max_cluster; ++i) {
                dbl_mdfat_value(sb, i, NULL, &mde);

                /*if((mdfat&0xC0000000)==0x80000000)*/
                if (mde.flags == 2) {
                    bh = raw_bread(sb, mde.sector_minus_1 + 1);

                    if (bh != NULL) {
                        res = CHL(bh->b_data);
                        raw_brelse(sb, bh);

                        if (res == DS_0_0) {dblsb->s_comp = DS_0_0; goto guess_ok;}

                        if (res == DS_0_1) {dblsb->s_comp = DS_0_1; goto guess_ok;}

                        if (res == DS_0_2) {dblsb->s_comp = DS_0_2; goto guess_ok;}

                        if (res == JM_0_0) {dblsb->s_comp = JM_0_0; goto guess_ok;}

                        if (res == JM_0_1) {dblsb->s_comp = JM_0_1; goto guess_ok;}

                        if (res == SQ_0_0) {dblsb->s_comp = SQ_0_0; goto guess_ok;}
                    }
                }
            }

            if (dblsb->s_comp == GUESS) { /* still unknown ? */
                printk(KERN_WARNING "DMSDOS: could not guess compression method for CVF\n");
                dblsb->s_comp = UNCOMPRESSED;
            }
        }
guess_ok:
        printk(KERN_INFO "DMSDOS: write_cluster: guessed 0x%08x.\n", dblsb->s_comp);
        /* we do not need this any longer...
        #ifdef GUESS_HACK
            if(dblsb->s_comp==SQ_0_0)
            { dblsb->s_comp=JM_0_1;
              printk(KERN_WARNING "DMSDOS: guess_hack: guessed SQ-0-0 not supported, using JM-0-1 instead.\n");
            }
        #endif
        */
    }

    method = dblsb->s_comp; /* default compression method */

    size = (length - 1) / 512 + 1;

    if (size == 1) { method = UNCOMPRESSED; } /* it will not become smaller :) */

    if (ucflag < 0 || ucflag == UC_UNCOMPR) { method = UNCOMPRESSED; } /* no need to compress */

    LOG_CLUST("DMSDOS: write_cluster: ucflag=%d, method=0x%x\n", ucflag, method);

    if (method == UNCOMPRESSED) { /* includes RAW writes */
        clusterk = clusterd;

        if (ucflag < 0) {
            /* raw write of already compressed data (for dmsdosd/ioctl) */
            ksize = -ucflag / SECTOR_SIZE; /* must be n*512 for doublespace */
            mde.size_lo_minus_1 = ksize - 1;
            mde.size_hi_minus_1 = size - 1;
            mde.flags = 2;
        } else {
            /* normal uncompressed write */
            /*mdfat=0xC0000000|((size-1)<<26)|((size-1)<<22);*/
            mde.size_lo_minus_1 = size - 1;
            mde.size_hi_minus_1 = size - 1;
            mde.flags = 3;
            ksize = size;
        }
    } else {
        if ((ucflag == UC_DIRECT) ? 0 : try_daemon(sb, clusternr, length, method)) { goto wr_uc; }

        clusterk = (unsigned char *)MALLOC(size * SECTOR_SIZE);

        if (clusterk == NULL) {
            printk(KERN_WARNING "DMSDOS: write_cluster: no memory for compression, writing uncompressed!\n");
wr_uc:
            clusterk = clusterd;
            /*mdfat=0xC0000000|((size-1)<<26)|((size-1)<<22);*/
            mde.size_lo_minus_1 = size - 1;
            mde.size_hi_minus_1 = size - 1;
            mde.flags = 3;
            method = UNCOMPRESSED;
            ksize = size;
        } else {
            LOG_CLUST("DMSDOS: write_cluster: compressing...\n");
            ksize = dbl_compress(clusterk, clusterd, size, method, dblsb->s_cfaktor);
            LOG_CLUST("DMSDOS: write cluster: compressing finished\n");

            if (ksize < 0) {
                /* compression failed */
                FREE(clusterk);
                clusterk = clusterd;
                /*mdfat=0xC0000000|((size-1)<<26)|((size-1)<<22);*/
                mde.size_lo_minus_1 = size - 1;
                mde.size_hi_minus_1 = size - 1;
                mde.flags = 3;
                method = UNCOMPRESSED;
                ksize = size;
            } else {
                /*mdfat=0x80000000|((size-1)<<26)|((ksize-1)<<22);*/
                mde.size_lo_minus_1 = ksize - 1;
                mde.size_hi_minus_1 = size - 1;
                mde.flags = 2;
            }
        }
    }

    LOG_CLUST("DMSDOS: write_cluster: call dbl_replace_existing_cluster\n");
    sector = dbl_replace_existing_cluster(sb, clusternr, near_sector, &mde, fraglist);
    LOG_CLUST("DMSDOS: write_cluster: dbl_replace_existing_cluster returned %d\n",
              sector);

    if (sector < 0) { res = -ENOSPC; }
    else {
        res = 0;
        /* no SIMULATE write here, this is caught above */
#ifdef DMSDOS_CONFIG_DRVSP3

        if (mde.unknown & 2) { /* fragmented */
            res = write_fragmented(sb, fraglist, clusterk, &mde, ksize);
        } else
#endif
            for (i = 0; i < ksize; ++i) {
                bh = raw_getblk(sb, sector + i);

                if (bh == NULL) { res = -EIO; }
                else {
                    memcpy(bh->b_data, &(clusterk[SECTOR_SIZE * i]), SECTOR_SIZE);
                    raw_set_uptodate(sb, bh, 1);
                    raw_mark_buffer_dirty(sb, bh, 1);
                    raw_brelse(sb, bh);
                }
            }
    }

    if (method != UNCOMPRESSED) { FREE(clusterk); }

    return res;
}
#endif

#define CHECK_INTERVAL 1000
static int fsc_count = 0;
void check_free_sectors(struct super_block *sb)
{
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;
    int i;
    int c;

    if (fsc_count > CHECK_INTERVAL || dblsb->s_free_sectors < 0) {
        c = 0;
        LOG_ALLOC("DMSDOS: checking free sectors...\n");

        for (i = dblsb->s_datastart; i <= dblsb->s_dataend; ++i) {
            if (dbl_bitfat_value(sb, i, NULL) == 0) { ++c; }
        }

        LOG_ALLOC("DMSDOS: free sectors=%d\n", c);

        if (dblsb->s_free_sectors >= 0) {
            if (dblsb->s_free_sectors != c) {
                printk(KERN_WARNING "DMSDOS: check_free_sectors: wrong count %d corrected to %d\n",
                       dblsb->s_free_sectors, c);
            }
        }

        dblsb->s_free_sectors = c;
        fsc_count = 0;
    } else {
        ++fsc_count;
    }
}

/* write a dmsdos cluster, compress before if possible;
   length is the number of used bytes, may be < SECTOR_SIZE*sectperclust only
   in the last cluster of a file;
   cluster must be allocated by allocate_cluster before if it is a new one;
   unable to write dir clusters;
   to avoid MDFAT level fragmentation, near_sector should be the sector no
   of the preceeding cluster;
   if ucflag==1 uncompressed write is forced (only for umsdos --linux-.---)
   if ucflag<0 raw write is forced with compressed size -ucflag (in bytes)
   if ucflag==2 simulate write is done (checks for space or reserves space
   for the cluster in the filesystem but does not actually write it)
   length==0 or clusterd==NULL means remove the cluster
   if ucflag==3 perform like ucflag==0 but don't use the daemon

   *********** This function is a generic wrapper ******
*/
/* IMPORTANT: if calling ch related routines note that cluster is very
   likely locked when write is called - it should be locked to prevent
   modification while being written :)
*/

int dmsdos_write_cluster(struct super_block *sb,
                         unsigned char *clusterd, int length, int clusternr,
                         int near_sector, int ucflag)
{
    int ret;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    LOG_CLUST("DMSDOS: write_cluster clusternr=%d length=%d near_sector=%d\n",
              clusternr, length, near_sector);

    /* ensure the daemon doesn't use old data and overwrites our data again
       but don't do this when called by the daemon itself :-/ uuhhh deadlock */
    /* also don't do it for simulated writes - they change nothing.... */
    if (ucflag >= 0 && ucflag != 2) { remove_from_daemon_list(sb, clusternr); }

    /* check whether using the daemon is not desired due to speedup bits */
    if (ucflag == 0 && (dmsdos_speedup & SP_USE_DAEMON) == 0) { ucflag = 3; }

    check_free_sectors(sb);

    switch (dblsb->s_cvf_version) {
#ifdef DMSDOS_CONFIG_DBL

    case DBLSP:
    case DRVSP:
    case DRVSP3:
        ret = dbl_write_cluster(sb, clusterd, length, clusternr, near_sector,
                                ucflag);
        break;
#endif
#ifdef DMSDOS_CONFIG_STAC

    case STAC3:
    case STAC4:
        ret = stac_write_cluster(sb, clusterd, length, clusternr, near_sector,
                                 ucflag);
        break;
#endif

    default:
        printk(KERN_ERR "DMSDOS: write_cluster: illegal cvf_version flag!\n");
        ret = -EIO;
    }

    return ret;
}

#endif /* __DMSDOS_LIB__*/
