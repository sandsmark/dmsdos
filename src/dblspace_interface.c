/*
dblspace_interface.c

DMSDOS CVF-FAT module: high-level interface functions.

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

#include"dmsdos.h"

/* some interface hacks */
# include"lib_interface.h"
# include<string.h>
# include<malloc.h>
# define MAJOR(x) 0
# define MINOR(x) 0

extern Acache mdfat[];
extern Acache dfat[];
extern Acache bitfat[];

unsigned long loglevel = DEFAULT_LOGLEVEL;
unsigned long dmsdos_speedup = DEFAULT_SPEEDUP;

/* evaluate numbers from options */
char *read_number(char *p, unsigned long *n, int *error)
{
    *n = 0;
    *error = -1;

    if (*p == 'b' || *p == 'B' || *p == '%') {
        /*binary*/
        ++p;

        while (*p == '0' || *p == '1') {
            (*n) *= 2;

            if (*p == '1') { ++(*n); }

            ++p;
            *error = 0;
        }
    } else if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) {
        /*hexadecimal*/
        p += 2;

        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            (*n) *= 16;
            (*n) += ((*p <= '9') ? (*p) : (*p) - 'a' + 10) & 0xf;
            ++p;
            *error = 0;
        }
    } else if (*p == '0' || *p == 'O' || *p == 'o') {
        /*octal*/
        ++p;

        while (*p >= '0' && *p <= '8') {
            (*n) *= 8;
            (*n) += (*p) - '0';
            ++p;
            *error = 0;
        }
    } else {
        /*decimal*/
        while (*p >= '0' && *p <= '9') {
            (*n) *= 10;
            (*n) += (*p) - '0';
            ++p;
            *error = 0;
        }
    }

    LOG_REST("DMSDOS: read_number: n=%lu=0x%lx error=%d\n", *n, *n, *error);
    return p;
}

/* evaluates a single option (needn't be '\0' terminated) */
int evaluate_option(char *option, Dblsb *dblsb, int *repair)
{
    int ret = 0;

    LOG_REST("DMSDOS: evaluate option: %s\n", option);

    if (strncmp(option, "comp=", 5) == 0 || strncmp(option, "comp:", 5) == 0) {
        if (strncmp(option + 5, "no", 2) == 0) { dblsb->s_comp = UNCOMPRESSED; }
        /*else if(strncmp(option+5,"ro",2)==0)*comp=READ_ONLY;*/
        else if (strncmp(option + 5, "ds00", 4) == 0) { dblsb->s_comp = DS_0_0; }
        else if (strncmp(option + 5, "ds01", 4) == 0) { dblsb->s_comp = DS_0_1; }
        else if (strncmp(option + 5, "ds02", 4) == 0) { dblsb->s_comp = DS_0_2; }
        else if (strncmp(option + 5, "jm00", 4) == 0) { dblsb->s_comp = JM_0_0; }
        else if (strncmp(option + 5, "jm01", 4) == 0) { dblsb->s_comp = JM_0_1; }
        else if (strncmp(option + 5, "sq00", 4) == 0) { dblsb->s_comp = SQ_0_0; }
        else if (strncmp(option + 5, "sd3", 3) == 0) { dblsb->s_comp = SD_3; }
        else if (strncmp(option + 5, "sd4", 3) == 0) { dblsb->s_comp = SD_4; }
        else if (strncmp(option + 5, "guess", 5) == 0) { dblsb->s_comp = GUESS; }
        else { ret = -1; }
    } else if (strncmp(option, "cf=", 3) == 0 || strncmp(option, "cf:", 3) == 0) {
        if (option[3] == '1' && option[4] >= '0' && option[4] <= '2') {
            dblsb->s_cfaktor = option[4] - '0' + 9;
        } else if (option[3] >= '1' && option[3] <= '9') {
            dblsb->s_cfaktor = option[3] - '0' - 1;
        } else { ret = -1; }
    } else if (strncmp(option, "loglevel=", 9) == 0 || strncmp(option, "loglevel:", 9) == 0) {
        /* must be decimal or hexadecimal (0x preceeded) number */
        read_number(option + 9, &loglevel, &ret);

        if (ret >= 0) {
            LOG_REST("DMSDOS: evaluate_option: loglevel set to 0x%lx.\n", loglevel);
        }
    } else if (strncmp(option, "speedup=", 8) == 0 || strncmp(option, "speedup:", 8) == 0) {
        /* must be decimal or hexadecimal (0x preceeded) number */
        read_number(option + 8, &dmsdos_speedup, &ret);

        if (ret >= 0) {
            LOG_REST("DMSDOS: evaluate_option: speedup set to 0x%lx.\n", dmsdos_speedup);
        }
    } else if (strncmp(option, "bitfaterrs=", 11) == 0 || strncmp(option, "bitfaterrs:", 11) == 0) {
        if (strncmp(option + 11, "repair", 6) == 0) { *repair = 1; }
        else if (strncmp(option + 11, "ignore", 6) == 0) { *repair = 2; }
        else if (strncmp(option + 11, "setro", 5) == 0) { *repair = 0; }
        else if (strncmp(option + 11, "nocheck", 7) == 0) { *repair = -1; }
        else { ret = -1; }
    } else {
        printk(KERN_ERR "DMSDOS: unknown option %s, rejected\n", option);
        ret = -1;
    }

    return ret;
}

int parse_dmsdos_options(char *options, Dblsb *dblsb, int *repair)
{
    if (options == NULL) { return 0; }

    while (*options) {
        if (evaluate_option(options, dblsb, repair) < 0) { return -1; }

        while (*options != '\0' && *options != '.' && *options != '+') { ++options; }

        while (*options == '.' || *options == '+') { ++options; }
    }

    return 0;
}

int ilog2(int arg)
{
    /* integer log2 */
    int i = 0;

    if (arg <= 0) { return -1; }

    while (arg >>= 1) { ++i; }

    return i;
}

//#ifdef DMSDOS_CONFIG_DBL
int detect_dblspace(struct super_block *sb)
{
    struct buffer_head *bh;

    MOD_INC_USE_COUNT;
    bh = raw_bread(sb, 0);

    if (bh == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read super block\n");
        MOD_DEC_USE_COUNT;
        return 0;
    }

    if (strncmp(bh->b_data + 3, "MSDBL6.0", 8) == 0
            || strncmp(bh->b_data + 3, "MSDSP6.0", 8) == 0) {
        raw_brelse(sb, bh);
        MOD_DEC_USE_COUNT;
        return 1;
    }

    raw_brelse(sb, bh);
    MOD_DEC_USE_COUNT;
    return 0;
}

/* setup fresh dblsb structure */
Dblsb *malloc_dblsb(void)
{
    Dblsb *dblsb;

    dblsb = kmalloc(sizeof(Dblsb), GFP_KERNEL);

    if (dblsb == NULL) { return NULL; }

    dblsb->mdfat_alloc_semp = NULL;

    return dblsb;
}

/* ensure all memory is released */
void free_dblsb(Dblsb *dblsb)
{
    if (dblsb == NULL) { return; }

    if (dblsb->mdfat_alloc_semp) {
        kfree(dblsb->mdfat_alloc_semp);
        dblsb->mdfat_alloc_semp = NULL;
    }

    kfree(dblsb);
}

int mount_dblspace(struct super_block *sb, char *options)
{
    struct buffer_head *bh;
    struct buffer_head *bh2;
    int i, mdfatb, fatb;
    unsigned int version_flag;
    unsigned char *pp;
    Dblsb *dblsb;
    int repair = 0;
    int mdrc, m_sector = 0;

    MOD_INC_USE_COUNT;
    LOG_REST("DMSDOS: dblspace/drvspace module mounting...\n");

    dblsb = malloc_dblsb();

    if (dblsb == NULL) {
        printk(KERN_ERR "DMSDOS: mount_dblspace: out of memory\n");
        MOD_DEC_USE_COUNT;
        return -1;
    }

    MSDOS_SB(sb)->private_data = dblsb;


    dblsb->s_comp = GUESS;
    dblsb->s_cfaktor = DEFAULT_CF;

    if (parse_dmsdos_options(options, dblsb, &repair)) {
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    dblsb->s_dataend = blk_size[MAJOR(sb->s_dev)][MINOR(sb->s_dev)] * 2;

    bh = raw_bread(sb, 0);

    if (bh == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read super block\n");
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    if (strncmp(bh->b_data + 3, "MSDBL6.0", 8) && strncmp(bh->b_data + 3, "MSDSP6.0", 8)) {
        printk(KERN_ERR "DMSDOS: MSDBL/MSDSP signature not found, CVF skipped\n");
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    if (sb->s_flags & MS_RDONLY) { dblsb->s_comp = READ_ONLY; }

    printk(KERN_INFO "DMSDOS: mounting CVF on device 0x%x %s...\n",
           sb->s_dev,
           dblsb->s_comp == READ_ONLY ? "read-only" : "read-write");

    /* dblspace correction was relocated. Pavel */
    dblsb->s_dataend -= 1;

    pp = &(bh->b_data[45]);
    dblsb->s_dcluster = CHS(pp);

    if (dblsb->s_dcluster & 0x8000) { dblsb->s_dcluster |= 0xffff0000; }

    pp = &(bh->b_data[36]);
    dblsb->s_mdfatstart = CHS(pp) + 1;
    pp = &(bh->b_data[17]);
    dblsb->s_rootdirentries = CHS(pp);
    dblsb->s_sectperclust = ((unsigned long)(bh->b_data[13]));
    dblsb->s_spc_bits = ilog2(dblsb->s_sectperclust);
    pp = &(bh->b_data[39]);
    i = CHS(pp); /*i=res0*/
    dblsb->s_bootblock = i;
    pp = &(bh->b_data[14]);
    dblsb->s_fatstart = i + CHS(pp);
    pp = &(bh->b_data[41]);
    dblsb->s_rootdir = i + CHS(pp);
    pp = &(bh->b_data[43]);
    dblsb->s_datastart = i + CHS(pp) + 2;
    dblsb->s_2nd_fat_offset = 0; /* dblsp doesn't have a second fat */
    dblsb->s_cvf_version = DBLSP;
    version_flag = bh->b_data[51];

    if (version_flag == 2) { dblsb->s_cvf_version = DRVSP; }

    if (version_flag == 3 || dblsb->s_sectperclust > 16) { dblsb->s_cvf_version = DRVSP3; }

    if (version_flag > 3)printk(KERN_WARNING "DMSDOS: strange version flag %d, assuming 0.\n",
                                    version_flag);

    bh2 = raw_bread(sb, dblsb->s_bootblock);

    if (bh2 == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read emulated boot block\n");
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    pp = &(bh2->b_data[57]);

    if (CHL(pp) == 0x20203631) { dblsb->s_16bitfat = 1; }
    else if (CHL(pp) == 0x20203231) { dblsb->s_16bitfat = 0; }
    else if (CHL(pp) == 0x20203233) {
        printk(KERN_ERR "DMSDOS: CVF has FAT32 signature, not mounted. Please report this.\n");
        raw_brelse(sb, bh2);
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    } else {
        unsigned long bitSizeSig = CHL(pp);
        pp = &(bh->b_data[62]);
        dblsb->s_16bitfat = (CHS(pp) > 32) ? 1 : 0;
        printk(KERN_WARNING "DMSDOS: FAT bit size (0x%lx) not recognized, guessed %d bit because %d is more than 32\n",
                bitSizeSig,
               CHS(pp) > 32 ? 16 : 12,
               CHS(pp)
               );
    }

    raw_brelse(sb, bh2);

    /* try to verify correct end of CVF */
    mdrc = 0;

    for (i = -1; i <= 1; ++i) {
        bh2 = raw_bread(sb, dblsb->s_dataend + i);

        if (bh2 == NULL) {
            LOG_REST("DMSDOS: MDR test breaks at i=%d\n", i);
            break;
        }

        if (strcmp(bh2->b_data, "MDR") == 0) {
            ++mdrc;
            m_sector = dblsb->s_dataend + i;
            LOG_REST("DMSDOS: MDR signature found at sector %d\n", m_sector);
        }

        raw_brelse(sb, bh2);
    }

    if (mdrc != 1)
        printk(KERN_WARNING "DMSDOS: could not find MDR signature or found more than one, mdrc=%d (ignored)\n",
               mdrc);
    else {
        if (dblsb->s_dataend != m_sector - 1) {
            LOG_REST("DMSDOS: dataend corrected due to MDR signature old=%d new=%d\n",
                     dblsb->s_dataend, m_sector - 1);
            dblsb->s_dataend = m_sector - 1;
        }
    }

    dblsb->s_full = 0;

    /* calculate maximum cluster nr (fixes lost cluster messages) */
    mdfatb = (dblsb->s_bootblock - dblsb->s_mdfatstart);
    mdfatb *= ((dblsb->s_sectperclust > 16) ? 102 : 128);
    mdfatb -= dblsb->s_dcluster;
    fatb = 512 * (dblsb->s_rootdir - dblsb->s_fatstart);

    if (dblsb->s_16bitfat) { fatb /= 2; }
    else { fatb = (2 * fatb) / 3; }

    dblsb->s_max_cluster = ((mdfatb < fatb) ? mdfatb : fatb) - 1;

    if (dblsb->s_16bitfat) {
        if (dblsb->s_max_cluster > 0xFFF6) { dblsb->s_max_cluster = 0xFFF6; }
    } else {
        if (dblsb->s_max_cluster > 0xFF6) { dblsb->s_max_cluster = 0xFF6; }
    }

    /* adapt max_cluster according to dos' limits */
    dblsb->s_max_cluster2 = dblsb->s_max_cluster;
    pp = &(bh->b_data[32]);
    i = CHL(pp);
    pp = &(bh->b_data[22]);
    i -= CHS(pp);
    pp = &(bh->b_data[14]);
    i -= CHS(pp);
    i -= dblsb->s_rootdirentries >> 4;
    /*i=(i>>4)+1;*/
    i = (i / dblsb->s_sectperclust) + 1;

    if (i <= dblsb->s_max_cluster) {
        dblsb->s_max_cluster = i;
    } else {
        printk(KERN_WARNING "DMSDOS: dos max_cluster=%d too large, cutting to %d.\n",
               i, dblsb->s_max_cluster);
    }

    LOG_REST("DMSDOS: dcluster=%d\n", dblsb->s_dcluster);
    LOG_REST("DMSDOS: mdfatstart=%d\n", dblsb->s_mdfatstart);
    LOG_REST("DMSDOS: rootdirentries=%d\n", dblsb->s_rootdirentries);
    LOG_REST("DMSDOS: sectperclust=%d\n", dblsb->s_sectperclust);
    LOG_REST("DMSDOS: fatstart=%d\n", dblsb->s_fatstart);
    LOG_REST("DMSDOS: rootdir=%d\n", dblsb->s_rootdir);
    LOG_REST("DMSDOS: %d bit FAT\n", dblsb->s_16bitfat ? 16 : 12);

    dblsb->s_lastnear = 0;
    dblsb->s_lastbig = 0;
    dblsb->s_free_sectors = -1; /* -1 means unknown */

    /* error test (counts sectors) */
    if (repair != -1) { /* repair==-1 means do not even check */
        i = simple_check(sb, repair & 1);

        if (i == -1 || i == -2) {
            printk(KERN_WARNING "DMSDOS: CVF has serious errors or compatibility problems, setting to read-only.\n");
            dblsb->s_comp = READ_ONLY;
        }

        if (i == -3) {
            if (repair & 2) {
                printk(KERN_WARNING "DMSDOS: CVF has bitfat mismatches, ignored.\n");
            } else {
                printk(KERN_WARNING "DMSDOS: CVF has bitfat mismatches, setting to read-only.\n");
                dblsb->s_comp = READ_ONLY;
            }
        }
    }

    /* if still unknown then count now */
    if (dblsb->s_free_sectors < 0) { check_free_sectors(sb); }

    /* print doublespace version */
    if (dblsb->s_cvf_version == DBLSP && dblsb->s_sectperclust == 16) {
        printk(KERN_INFO "DMSDOS: CVF is in doublespace format (version 1).\n");
    } else if (dblsb->s_cvf_version == DRVSP && dblsb->s_sectperclust == 16) {
        printk(KERN_INFO "DMSDOS: CVF is in drivespace format (version 2).\n");
    } else if (dblsb->s_cvf_version == DRVSP3 && dblsb->s_sectperclust == 64) {
        printk(KERN_INFO "DMSDOS: CVF is in drivespace 3 format.\n");
    } else {
        printk(KERN_INFO "DMSDOS: CVF is in unknown (new?) format, please report.\n");
        printk(KERN_INFO "DMSDOS: version_flag=%d sectperclust=%d\n", version_flag,
               dblsb->s_sectperclust);
        printk(KERN_NOTICE "DMSDOS: CVF set to read-only.\n");
        dblsb->s_comp = READ_ONLY;
    }

    raw_brelse(sb, bh);

    /* set some msdos fs important stuff */
    MSDOS_SB(sb)->dir_start = FAKED_ROOT_DIR_OFFSET;
    MSDOS_SB(sb)->dir_entries = dblsb->s_rootdirentries;
    MSDOS_SB(sb)->data_start = FAKED_DATA_START_OFFSET; /*begin of virtual cluster 2*/
    MSDOS_SB(sb)->clusters = dblsb->s_max_cluster;

    if (MSDOS_SB(sb)->fat_bits != (dblsb->s_16bitfat ? 16 : 12)) {
        LOG_REST("DMSDOS: fat bit size mismatch in fat driver, trying to correct\n");
        MSDOS_SB(sb)->fat_bits = dblsb->s_16bitfat ? 16 : 12;
    }

    MSDOS_SB(sb)->cluster_size = dblsb->s_sectperclust;
#ifdef HAS_SB_CLUSTER_BITS

    for (MSDOS_SB(sb)->cluster_bits = 0;
            (1 << MSDOS_SB(sb)->cluster_bits) < MSDOS_SB(sb)->cluster_size;) {
        MSDOS_SB(sb)->cluster_bits++;
    }

    MSDOS_SB(sb)->cluster_bits += SECTOR_BITS;
#endif

    /* these *must* always match */
    if (dblsb->s_comp == READ_ONLY) { sb->s_flags |= MS_RDONLY; }

    return 0;
}
//#endif

int unmount_dblspace(struct super_block *sb)
{
    int j;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    LOG_REST("DMSDOS: CVF on device 0x%x unmounted.\n", sb->s_dev);

    /* mark stacker bitfat as up to date and unmounted */
    if (dblsb->s_cvf_version >= STAC3) {
        stac_bitfat_state(sb, 1);
    }

    /* kill buffers used by unmounted cvf */
    for (j = 0; j < MDFATCACHESIZE; ++j) {
        if (mdfat[j].a_buffer != NULL) {
            if (mdfat[j].a_sb->s_dev == sb->s_dev) {
                raw_brelse(sb, mdfat[j].a_buffer);
                mdfat[j].a_buffer = NULL;
            }

            mdfat[j].a_time = 0;
            mdfat[j].a_acc = 0;
        }
    }

    for (j = 0; j < DFATCACHESIZE; ++j) {
        if (dfat[j].a_buffer != NULL) {
            if (dfat[j].a_sb->s_dev == sb->s_dev) {
                raw_brelse(sb, dfat[j].a_buffer);
                dfat[j].a_buffer = NULL;
            }

            dfat[j].a_time = 0;
            dfat[j].a_acc = 0;
        }
    }

    for (j = 0; j < BITFATCACHESIZE; ++j) {
        if (bitfat[j].a_buffer != NULL) {
            if (bitfat[j].a_sb->s_dev == sb->s_dev) {
                raw_brelse(sb, bitfat[j].a_buffer);
                bitfat[j].a_buffer = NULL;
            }

            bitfat[j].a_time = 0;
            bitfat[j].a_acc = 0;
        }
    }

    /*kfree(MSDOS_SB(sb)->private_data);*/
    free_dblsb(dblsb);
    MSDOS_SB(sb)->private_data = NULL;
    /*MSDOS_SB(sb)->cvf_format=NULL;*/ /*this causes a segfault in
                                         dec_cvf_format_use_count_by_version*/
    MOD_DEC_USE_COUNT;
    return 0;
}

int detect_stacker(struct super_block *sb)
{
    struct buffer_head *bh;

    MOD_INC_USE_COUNT;
    bh = raw_bread(sb, 0);

    if (bh == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read super block\n");
        MOD_DEC_USE_COUNT;
        return 0;
    }

    if (strncmp(bh->b_data, "STACKER", 7) == 0) {
        raw_brelse(sb, bh);
        MOD_DEC_USE_COUNT;
        return 1;
    }

    raw_brelse(sb, bh);
    MOD_DEC_USE_COUNT;
    return 0;
}

int mount_stacker(struct super_block *sb, char *options)
{
    struct buffer_head *bh;
    struct buffer_head *bh2;
    int i;
    unsigned char *pp, *p;
    unsigned char buf[512];
    unsigned char b, c;
    int SectSize, ClustSects, ClustSize, ReservSects, FATCnt;
    int RootDirEnt, TotalSects, FATSize, HidenSects, FirstRootSect;
    int FirstDataSect, FirstDataSect2, FAT12, FirstFATSect;
    int StacVersion;
    /* parameters of virtual DOS drive */
    int BB_FirstDataSect, BB_ClustCnt, BB_SectSize, BB_TotalSects;
    Dblsb *dblsb;
    int repair = 0;

    MOD_INC_USE_COUNT;
    LOG_REST("DMSDOS: stacker 3/4 module mounting...\n");

    dblsb = malloc_dblsb();

    if (dblsb == NULL) {
        printk(KERN_ERR "DMSDOS: mount_stacker: out of memory\n");
        MOD_DEC_USE_COUNT;
        return -1;
    }

    MSDOS_SB(sb)->private_data = dblsb;


    dblsb->s_comp = GUESS;
    dblsb->s_cfaktor = DEFAULT_CF;

    if (parse_dmsdos_options(options, dblsb, &repair)) {
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    dblsb->s_dataend = blk_size[MAJOR(sb->s_dev)][MINOR(sb->s_dev)] * 2;

    LOG_REST("DMSDOS: reading super block...\n");
    bh = raw_bread(sb, 0);

    if (bh == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read super block of CVF\n");
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    LOG_REST("DMSDOS: super block read finished\n");
    pp = &(bh->b_data[0]);

    if (strncmp(pp, "STACKER", 7) != 0) {
        printk(KERN_ERR "DMSDOS: STACKER signature not found\n");
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    /* copy block (must not directly modify kernel buffer!!!) */
    memcpy(buf, bh->b_data, SECTOR_SIZE);

    /* decode super block */
    for (i = 0x30, p = buf + 0x50, b = buf[0x4c]; i--; p++) {
        b = 0xc4 - b;
        b = b < 0x80 ? b * 2 : b * 2 + 1;
        b ^= c = *p;
        *p = b;
        b = c;
    }

    if (buf[0x4e] != 0xa || buf[0x4f] != 0x1a) {
        printk(KERN_ERR "DMSDOS: Stacker 0x1A0A signature not found\n");
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    if (sb->s_flags & MS_RDONLY) { dblsb->s_comp = READ_ONLY; }

    printk(KERN_NOTICE "DMSDOS: mounting CVF on device 0x%x %s...\n",
           sb->s_dev,
           dblsb->s_comp == READ_ONLY ? "read-only" : "read-write");

    /* extract important info */
    pp = &(buf[0x6C]);
    TotalSects = CHL(pp);
    pp = &(buf[0x70]);
    dblsb->s_bootblock = CHS(pp);
    pp = &(buf[0x74]);
    dblsb->s_mdfatstart = CHS(pp); /* here it's AMAP start !!! */
    pp = &(buf[0x76]);
    FirstFATSect = dblsb->s_fatstart = CHS(pp);
    pp = &(buf[0x7a]);
    FirstDataSect2 = dblsb->s_datastart = CHS(pp);
    pp = &(buf[0x60]);
    StacVersion = CHS(pp);

    if (StacVersion >= 410) { dblsb->s_cvf_version = STAC4; }
    else { dblsb->s_cvf_version = STAC3; }

    /* if(buf[0x64]==9)dblsb->s_cvf_version=STAC4;
    else dblsb->s_cvf_version=STAC3; */

    /* now we need the boot block */
    bh2 = raw_bread(sb, dblsb->s_bootblock);

    if (bh2 == NULL) {
        printk(KERN_ERR "DMSDOS: unable to read emulated boot block of CVF\n");
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    /* read values */
    dblsb->s_sectperclust = bh2->b_data[0xd];
    dblsb->s_spc_bits = ilog2(dblsb->s_sectperclust);
    pp = &(bh2->b_data[0x11]);
    dblsb->s_rootdirentries = CHS(pp);

    pp = &(buf[0x62]);
    SectSize = CHS(pp);
    pp = &(bh2->b_data[0xB]);
    BB_SectSize = CHS(pp);

    if (SectSize != SECTOR_SIZE || BB_SectSize != SECTOR_SIZE) {
        printk(KERN_WARNING "DMSDOS: Stacker sector size not 512 bytes, hmm...\n");
    }

    ClustSects = bh2->b_data[0xD];
    ClustSize = ClustSects * SectSize;
    pp = &(bh2->b_data[0xE]);
    ReservSects = CHS(pp);
    FATCnt = bh2->b_data[0x10];
    pp = &(bh2->b_data[0x11]);
    RootDirEnt = CHS(pp);
    pp = &(bh2->b_data[0x13]);
    BB_TotalSects = CHS(pp);

    printf("Cluster size: %d\n", ClustSize);

    if (!BB_TotalSects)
    { pp = &(bh2->b_data[0x20]); BB_TotalSects = CHL(pp);};

    pp = &(bh2->b_data[0x16]);

    FATSize = CHS(pp);

    pp = &(bh2->b_data[0x1B]);

    HidenSects = CHS(pp);

    printf("Hidden sectors: %d\n", HidenSects);

    if (BB_SectSize != SectSize) { printk(KERN_WARNING "DMSDOS: Inconsistent sector length\n"); }

    FirstRootSect = FirstFATSect + 3 * FATCnt * FATSize;

    dblsb->s_2nd_fat_offset = 3 * (FATCnt - 1) * FATSize;

    /* Number of sectors in root directory */
    FirstDataSect = ((long)RootDirEnt * 0x20 + SectSize - 1) / SectSize;
    /* Emulated data start sector for DOS */
    BB_FirstDataSect = FirstDataSect + FATCnt * FATSize + ReservSects;
    /* ??? +HidenSects; */
    /* Real data start sector */
    FirstDataSect += FirstRootSect;
    /* Counting BB_ClustCnt from emulated boot block */
    BB_ClustCnt = (BB_TotalSects - BB_FirstDataSect) / ClustSects;

    if (BB_ClustCnt >= 0xFED) { FAT12 = 0; }
    else { FAT12 = 1; }

    if (BB_ClustCnt < 2 || BB_ClustCnt > 0xfff7) {
        printk(KERN_ERR "DMSDOS: BB_ClustCnt=0x%x impossible (FAT32?)\n", BB_ClustCnt);
        raw_brelse(sb, bh2);
        raw_brelse(sb, bh);
        free_dblsb(dblsb);
        MSDOS_SB(sb)->private_data = NULL;
        MOD_DEC_USE_COUNT;
        return -1;
    }

    if (FirstDataSect2 != FirstDataSect) {
        printk(KERN_WARNING "DMSDOS: Inconsistent first data sector number. Mounting READ ONLY.\n");
        printk(KERN_WARNING "In header found %u but computed %u\n", (unsigned)FirstDataSect2, (unsigned)FirstDataSect);
        dblsb->s_comp = READ_ONLY;
    }

    LOG_REST("DMSDOS: Stac version %u start of FAT %u, root %u, data %u; FATSize %u; FATCnt %u; clusts %u; sects %u\n",
             (unsigned)StacVersion, (unsigned)FirstFATSect, (unsigned)FirstRootSect,
             (unsigned)FirstDataSect, (unsigned)FATSize, (unsigned)FATCnt,
             (unsigned)BB_ClustCnt, (unsigned)BB_TotalSects);

    /* try dos standard method to detect fat bit size - does not work */
    /* pp=&(bh2->b_data[57]); */
    /* if(CHL(pp)==0x20203631)dblsb->s_16bitfat=1; */
    /* else if(CHL(pp)==0x20203231)dblsb->s_16bitfat=0; else */

    /* used only stacker method for fat entry size now */
    dblsb->s_16bitfat = FAT12 ? 0 : 1;
    LOG_REST("DMSDOS: FAT bit size of CVF is %d bit\n",
             (FAT12) ? 12 : 16);

    /* check if clusters fits in FAT */
    if (BB_ClustCnt + 2 > (FAT12 ? (SECTOR_SIZE * FATSize * 2) / 3 : (SECTOR_SIZE * FATSize) / 2)) {
        printk(KERN_WARNING "DMSDOS: FAT size does not match cluster count. Mounting READ ONLY.\n");
        dblsb->s_comp = READ_ONLY;
    }

    /* check size of physical media against stacvol parameters */
    if ((TotalSects <= 0) || (TotalSects - 1) > dblsb->s_dataend) {
        printk(KERN_WARNING "DMSDOS: CVF is shorter about %d sectors. Mounting READ ONLY.\n",
               (int)TotalSects - 1 - dblsb->s_dataend);
        dblsb->s_comp = READ_ONLY;
    } else if ((TotalSects - 1) < dblsb->s_dataend) {
        printk(KERN_INFO "DMSDOS: CVF end padding %d sectors.\n",
               (int)dblsb->s_dataend - TotalSects + 1);
        dblsb->s_dataend = TotalSects - 1;
    }

    raw_brelse(sb, bh2);
    dblsb->s_full = 0;
    raw_brelse(sb, bh);

    dblsb->s_rootdir = FirstRootSect;
    dblsb->s_max_cluster = dblsb->s_max_cluster2 = BB_ClustCnt + 1;

    LOG_REST("DMSDOS: mdfatstart=%d\n", dblsb->s_mdfatstart);
    LOG_REST("DMSDOS: rootdirentries=%d\n", dblsb->s_rootdirentries);
    LOG_REST("DMSDOS: sectperclust=%d\n", dblsb->s_sectperclust);
    LOG_REST("DMSDOS: fatstart=%d\n", dblsb->s_fatstart);
    LOG_REST("DMSDOS: rootdir=%d\n", dblsb->s_rootdir);
    LOG_REST("DMSDOS: %d bit FAT\n", dblsb->s_16bitfat ? 16 : 12);

    /* allocation informations */
    dblsb->s_lastnear = 0;
    dblsb->s_lastbig = 0;
    dblsb->s_free_sectors = -1; /* -1 means unknown */

    /* set some msdos fs important stuff */
    MSDOS_SB(sb)->dir_start = FAKED_ROOT_DIR_OFFSET;
    MSDOS_SB(sb)->dir_entries = dblsb->s_rootdirentries;
    MSDOS_SB(sb)->data_start = FAKED_DATA_START_OFFSET; /*begin of virtual cluster 2*/
    MSDOS_SB(sb)->clusters = BB_ClustCnt;

    if (MSDOS_SB(sb)->fat_bits != (dblsb->s_16bitfat ? 16 : 12)) {
        LOG_REST("DMSDOS: fat bit size mismatch in fat driver, trying to correct\n");
        MSDOS_SB(sb)->fat_bits = dblsb->s_16bitfat ? 16 : 12;
    }

    MSDOS_SB(sb)->cluster_size = dblsb->s_sectperclust;
#ifdef HAS_SB_CLUSTER_BITS

    for (MSDOS_SB(sb)->cluster_bits = 0;
            (1 << MSDOS_SB(sb)->cluster_bits) < MSDOS_SB(sb)->cluster_size;) {
        MSDOS_SB(sb)->cluster_bits++;
    }

    MSDOS_SB(sb)->cluster_bits += SECTOR_BITS;
#endif

    /* error test */
    if (repair != -1) { /* repair==-1 means do not even check */
        i = simple_check(sb, repair & 1);

        if (i == -1 || i == -2) {
            printk(KERN_WARNING "DMSDOS: CVF has serious errors or compatibility problems, setting to read-only.\n");
            dblsb->s_comp = READ_ONLY;
        }

        if (i == -3) {
            if (repair & 2) {
                printk(KERN_WARNING "DMSDOS: CVF has bitfat mismatches, ignored.\n");
            } else {
                printk(KERN_WARNING "DMSDOS: CVF has bitfat mismatches, setting to read-only.\n");
                dblsb->s_comp = READ_ONLY;
            }
        }
    }

    /* print stacker version */
    if (dblsb->s_cvf_version == STAC3) {
        printk(KERN_NOTICE "DMSDOS: CVF is in stacker 3 format.\n");
    } else if (dblsb->s_cvf_version == STAC4) {
        printk(KERN_NOTICE "DMSDOS: CVF is in stacker 4 format.\n");
    }

    /* if still unknown then count now */
    if (dblsb->s_free_sectors < 0) { check_free_sectors(sb); }

    /* these *must* always match */
    if (dblsb->s_comp == READ_ONLY) { sb->s_flags |= MS_RDONLY; }

    /* mark stacker bitfat as mounted and changing */
    /* if not regulary unmounted, it must be repaired before */
    /* next write access */
    if ((sb->s_flags & MS_RDONLY) == 0) { stac_bitfat_state(sb, 2); }

    return 0;
}
//#endif

#ifdef DMSDOS_USE_READPAGE
#define READPAGE dblspace_readpage
#define MMAP NULL
#define RMFLAG CVF_USE_READPAGE
#else
#define READPAGE NULL
#define MMAP dblspace_mmap
#define RMFLAG 0
#endif

static char seq[] = "000000";

int log_prseq(void)
{
    int i;

    i = 5;

    while (i >= 0) {
        ++seq[i];

        if (seq[i] <= '9') { break; }

        seq[i] = '0';
        --i;
    }

    printk(seq);

    return 1;
}
