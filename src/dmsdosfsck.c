/*

dmsdosfsck.c

DMSDOS: filesystem check utility for CVFs.

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
#include<stdlib.h>
#include<string.h>
#include<ctype.h>
#include<unistd.h>
#include"dmsdos.h"
#include"lib_interface.h"

int check_dir(int parent, int clusternr);

/*this is not good - but currently we have only one CVF open at a time*/
static struct super_block *sb;
static Dblsb *dblsb;

typedef struct {
    int referrenced_from;
    int start;
} Fatdata;

static Fatdata fat[65536];

#define FAT_EOF -1

static int seenlist[65536];
static int repair_automatically = 0;
static int repair_interactively = 0;
static int verbose = 0;
static int listfiles = 0;

#define vprintf if(verbose)printf
#define lprintf if(listfiles)printf

static int repair(char *text)
{
    int c;

    if (repair_automatically) { return 1; }

    if (repair_interactively == 0) { return 0; }

    fflush(stdin);
    printf("%s", text);
    fflush(stdout);
    c = fgetc(stdin);
    fflush(stdin);

    if (c == 'y' || c == 'Y') { return 1; }

    return 0;
}

static int check_fat_loop(int begin)
{
    int seen = 0;
    int next;
    int i;
    int newval;

    while (begin >= 2 && begin <= dblsb->s_max_cluster) {
        /* add to seenlist */
        seenlist[seen] = begin;
        ++seen;

        if (seen > 65535) { return -1; } /* cannot happen */

        next = dbl_fat_nextcluster(sb, begin, NULL);

        /* check whether it was already seen */
        for (i = 0; i < seen; ++i) {
            if (seenlist[i] == next) {
                /* here begins a fat loop */
                printf("FAT loop at cluster %d found.\n", begin);

                if (repair("Break it?") == 0) { return 1; }

                newval = FAT_EOF;
                dbl_fat_nextcluster(sb, begin, &newval);
                return 0;
            }
        }

        begin = next;
    }

    return 0;
}

static int check_fat()
{
    int i;
    int val;
    int errors = 0;
    int newval = 0;

    for (i = 0; i < 65536; ++i) {
        fat[i].referrenced_from = 0;
        fat[i].start = 0;
    }

    for (i = 2; i <= dblsb->s_max_cluster; ++i) {
        vprintf("Checking cluster %d...\n", i);

        val = dbl_fat_nextcluster(sb, i, NULL);

        if (val) {
            if (val == 1) {
                printf("cluster %d: invalid fat entry\n", i);

                if (repair("Correct it?") == 0) { ++errors; }
                else {
                    newval = FAT_EOF;
                    dbl_fat_nextcluster(sb, i, &newval);
                }
            }

            if (check_fat_loop(i)) {
                printf("Unresolved FAT loop in filesystem. Can't continue, sorry.\n");
                close_cvf(sb);
                exit(4);
            }
        }

        if (val >= 2 && val <= dblsb->s_max_cluster) {
            if (fat[val].referrenced_from) {
                /* this is a crosslink */
                printf("cluster %d is crosslinked with %d to %d\n",
                       i, fat[val].referrenced_from, val);

                if (repair("Break it?") == 0) { ++errors; }
                else {
                    newval = FAT_EOF;
                    dbl_fat_nextcluster(sb, i, &newval);
                    dbl_fat_nextcluster(sb, fat[val].referrenced_from, &newval);
                    fat[val].referrenced_from = 0;
                }
            }

            fat[val].referrenced_from = i;
        }
    }

    return errors;
}

static int check_chains()
{
    int i;
    int val;
    int errors = 0;
    int newval = 0;
    int next;

    for (i = 2; i <= dblsb->s_max_cluster; ++i) {
        val = dbl_fat_nextcluster(sb, i, NULL);

        if (val >= 2 && val <= dblsb->s_max_cluster && fat[i].referrenced_from == 0) {
            /* this is the start of a chain */
            vprintf("checking chain beginning at cluster %d...\n", i);

rchain:
            next = dbl_fat_nextcluster(sb, val, NULL);

            if (next == FAT_EOF) { continue; }

            if (next == 0 || next > dblsb->s_max_cluster) {
                vprintf("chain breaks unexpectedly.\n");

                if (repair("Set proper end?") == 0) { ++errors; }
                else {
                    newval = FAT_EOF;
                    dbl_fat_nextcluster(sb, val, &newval);
                }
            } else {
                val = next;
                goto rchain;
            }

        }
    }

    return 0;
}

struct nametest {
    unsigned char name[12];
    struct nametest *next;
};

static int add_name(struct nametest **namelist, unsigned char *text)
{
    struct nametest *akt = *namelist;

    while (akt) {
        if (strncmp(akt->name, text, 11) == 0) { return -1; }

        akt = akt->next;
    }

    akt = malloc(sizeof(struct nametest));

    if (akt == NULL) { return -2; }

    strncpy(akt->name, text, 11);
    akt->next = *namelist;
    *namelist = akt;

    return 0;
}

void free_namelist(struct nametest **namelist)
{
    struct nametest *akt = *namelist;
    struct nametest *merk;

    while (akt) {
        merk = akt->next;
        free(akt);
        akt = merk;
    }

    *namelist = NULL;
}


int check_char(unsigned char c, int noprint)
{
    if (c == 0x5) { c = 0xE5; } /* M$ hack for languages where E5 is a valid char */

    if (c < 32 || strchr("+\\?*<>|\"=,;", c) != NULL) {
        if (!noprint) { lprintf("?"); }

        return 1;
    }

    if (!noprint) {
        if (c < 127) {
            lprintf("%c", c);
            return 0;
        }

        // TODO: proper codepage support, this is Code page 865 (DOS Nordic)
        switch (c) {
        case 0x92:
            lprintf("Æ");
            break;

        case 0x9c:
            lprintf("£");
            break;

        case 0x9d:
            lprintf("Ø");
            break;

        //case 0xff:
        //    lprintf(" ");
        //    break;
        default:
            lprintf(" 0x%x ", c);
            break;
        }
    }

    return 0;
}

int check_direntry(int dirstartclust, unsigned char *data, int *need_write,
                   struct nametest **namelist)
{
    int i;
    unsigned int x;
    unsigned char *pp;
    unsigned long size;
    int cluster, prevcluster, newval;
    unsigned long fatsize;
    int clustersize;
    int invchar;

    *need_write = 0;

    if (data[0] == 0 || data[0] == 0xE5) { return 0; }

    if (data[11] & 8) { return 0; } /* ignore V entries */

    invchar = 0;

    for (i = 0; i < 11; ++i) {
        if (i == 8) { lprintf(" "); }

        invchar += check_char(data[i], 0);
    }

    if (listfiles) {
        printf("  ");

        if (data[11] & 1) { printf("R"); }
        else { printf(" "); }

        if (data[11] & 2) { printf("H"); }
        else { printf(" "); }

        if (data[11] & 4) { printf("S"); }
        else { printf(" "); }

        if (data[11] & 8) { printf("V"); }
        else { printf(" "); }

        if (data[11] & 16) { printf("D"); }
        else { printf(" "); }

        if (data[11] & 32) { printf("A"); }
        else { printf(" "); }

        if (data[11] & 64) { printf("?"); }
        else { printf(" "); }

        if (data[11] & 128) { printf("?"); }
        else { printf(" "); }

        pp = &(data[22]);
        x = CHS(pp);
        printf("  %02d:%02d:%02d", x >> 11, (x >> 5) & 63, (x & 31) << 1);

        pp = &(data[24]);
        x = CHS(pp);
        printf(" %02d.%02d.%4d", x & 31, (x >> 5) & 15, (x >> 9) + 1980); /* y2k compliant :) */
    }

    pp = &(data[26]);
    cluster = CHS(pp);
    lprintf(" %5d", cluster);

    pp = &(data[28]);
    size = CHL(pp);
    lprintf(" %7lu ", size);

    if (invchar) {
        printf("name has invalid chars, \n");

        if (repair("Replace them?") != 0) {
            for (i = 0; i < 11; ++i) {
                if (check_char(data[i], 1)) {
                    data[i] = '~';
                    *need_write = 1;
                }
            }
        }
    }

    if (add_name(namelist, data)) {
        puts("ERR: duplicate filename");

        if (repair("Rename?") != 0) {
            char teststr[10];
            int n = 1;

            i = 7;

            while (i >= 0) {
                if (data[i] == ' ') { data[i] = '~'; }
                else { break; }

                --i;
            }

            do {
                sprintf(teststr, "~%d", n++);

                for (i = 0; i < strlen(teststr); ++i) { data[i + 8 - strlen(teststr)] = teststr[i]; }
            } while (add_name(namelist, data));

            *need_write = 1;
        }
    }

    if (cluster < 0 || cluster == 1 || cluster > dblsb->s_max_cluster) {
        printf("ERR: Dir at invalid cluster %d, max is %d\n", cluster, dblsb->s_max_cluster);

        if (repair("Truncate?") == 0) { return -1; }

        data[26] = 0;
        data[27] = 0;
        cluster = 0;
        *need_write = 1;
    } else {
        if (cluster) {
            if (fat[cluster].referrenced_from != 0 || fat[cluster].start != 0) {
                printf("first cluster crosslink\n");

                if (repair("Truncate?") == 0) { return -1; }

                data[26] = 0;
                data[27] = 0;
                cluster = 0;
                *need_write = 1;
            } else { fat[cluster].start = 1; }
        }
    }

    if (data[11] & 16) { /* dir */
        if (cluster == 0) {
            printf("clusternr invalid for subdir\n");
            goto irrepdir;
        }

        lprintf("OK\n");
        lprintf("Descending directory...\n");
        i = check_dir(dirstartclust, cluster);
        lprintf("Ascending...\n");

        if (i >= 0) {
            if (size) {
                printf("directory entry has size !=0\n");

                if (repair("Correct this?") == 0) { ++i; }
                else {
                    data[28] = 0;
                    data[29] = 0;
                    data[30] = 0;
                    data[31] = 0;
                    size = 0;
                    *need_write = 1;
                }
            }

            return i;
        }

irrepdir:
        printf("directory is irreparably damaged: '");

        for (i = 0; i < 11; ++i) {
            if (i == 8) { printf(" "); }

            printf("%c", data[i]);
        }

        printf("'\n");

        if (repair("Convert to file?") == 0) { return -1; }

        data[11] &= ~16;
        *need_write = 1;
        /* fall through */
    }

    if (cluster == 0) {
        if (size == 0) {
            lprintf("OK, empty file at cluster 0\n");
            return 0;
        }

        printf("wrong size\n");

        if (repair("Correct this?") == 0) { return -1; }
        else {
            data[28] = 0;
            data[29] = 0;
            data[30] = 0;
            data[31] = 0;
            size = 0;
            *need_write = 1;
        }

        return 0;
    }

    clustersize = dblsb->s_sectperclust * SECTOR_SIZE;

    fatsize = 0;
    prevcluster = 0;

    while (cluster > 1 && cluster <= dblsb->s_max_cluster) {
        prevcluster = cluster;
        cluster = dbl_fat_nextcluster(sb, cluster, NULL);
        fatsize += clustersize;
    }

    if (cluster == 0) {
        printf("WARN: file at cluster 0\n");
        cluster = prevcluster;
        fatsize -= clustersize;

        if (repair("Correct this?") == 0) { return -1; }

        newval = FAT_EOF;
        dbl_fat_nextcluster(sb, cluster, &newval);
        printf("WARN: new val at %d, cluster now at %d\n", newval, cluster);
    }

    if (cluster == 1 || cluster > dblsb->s_max_cluster) {
        printf("ERR: File at invalid cluster %d, max is %d\n", cluster, dblsb->s_max_cluster);
        return -1;
    }

    if (size == fatsize) {
        lprintf("OK\n");
        return 0;
    }

    if (size / clustersize == (fatsize - 1) / clustersize) {
        lprintf("OK\n");
        return 0;
    }

    printf("ERR: File size %lu (%lu clusters), actual %lu (%lu clusters)\n", size, size/clustersize, fatsize, (fatsize - 1) / clustersize);

    if (repair("Recalculate file size?") == 0) { return -1; }

    data[28] = fatsize;
    data[29] = fatsize >> 8;
    data[30] = fatsize >> 16;
    data[31] = fatsize >> 24;
    *need_write = 1;
    return 0;
}

int check_root_dir(void)
{
    int i, j, errors, r;
    struct buffer_head *bh;
    int need_write = 0;
    struct nametest *namelist = NULL;

    errors = 0;

    for (i = 0; i < dblsb->s_rootdirentries / 16; ++i) {
        bh = raw_bread(sb, dblsb->s_rootdir + i);

        if (bh == NULL) { return -1; }

        for (j = 0; j < 16; ++j) {
            r = check_direntry(0, &(bh->b_data[j * 32]), &need_write, &namelist);

            if (r) { ++errors; }
            else if (need_write) { raw_mark_buffer_dirty(sb, bh, 1); }
        }

        raw_brelse(sb, bh);
    }

    free_namelist(&namelist);
    return errors;
}

int check_dir(int parent, int clusternr)
{
    unsigned char *data;
    int j, errors, r;
    int next;
    int start = 2;
    int dirstartclust = clusternr;
    int need_write = 0;
    int len;
    struct nametest *namelist = NULL;

    errors = 0;
    data = malloc(dblsb->s_sectperclust * SECTOR_SIZE);

    if (data == NULL) { return -1; }

    vprintf("checking directory at start cluster %d...\n", clusternr);

    next = dbl_fat_nextcluster(sb, clusternr, NULL);

    if (next == 0) {
        printf("warning: dir cluster %d is marked free\n", clusternr);
    }

    while (clusternr > 0) {
        len = dmsdos_read_cluster(sb, data, clusternr);

        if (len < 0) {free(data); return -1;}

        if (start) {
            unsigned char *pp;

            /* check .       12345678EXT */
            if (strncmp(data, ".          ", 11) != 0) {
                printf("first entry is not '.'\n");
                ++errors;
            }

            pp = &(data[26]);

            if (CHS(pp) != clusternr) {
                printf("self cluster nr is wrong in '.'\n");
                ++errors;
            }

            /* check ..      12345678EXT */
            if (strncmp(data + 32, "..         ", 11) != 0) {
                printf("second entry is not '..'\n");
                ++errors;
            }

            pp = &(data[26 + 32]);

            if (CHS(pp) != parent) {
                printf("parent cluster nr is wrong in '..'\n");
                ++errors;
            }

            if (errors) {
                printf("This doesn't look like a directory, skipped\n");
                free(data);
                return -1;
            }
        }

        for (j = start; j < dblsb->s_sectperclust * 16; ++j) {
            r = check_direntry(dirstartclust, &(data[j * 32]), &need_write, &namelist);

            if (r) { ++errors; }
            else if (need_write) { dmsdos_write_cluster(sb, data, len, clusternr, 0, 1); }
        }

        start = 0;
        next = dbl_fat_nextcluster(sb, clusternr, NULL);

        if (next == 1 || next > dblsb->s_max_cluster) {
            printf("directory ends with fat alloc error\n");
            ++errors;
            break;
        }

        clusternr = next;
    }

    free(data);
    free_namelist(&namelist);
    return errors;
}

static int check_unused_chains()
{
    int i;
    int val;
    int errors = 0;

    for (i = 2; i <= dblsb->s_max_cluster; ++i) {
        val = dbl_fat_nextcluster(sb, i, NULL);

        if (val >= 2 && val <= dblsb->s_max_cluster && fat[i].referrenced_from == 0
                && fat[i].start == 0) {
            vprintf("chain beginning with cluster %d is unused.\n", i);
            /* if(repair("Delete it?")==0)++errors;
               else
               { free_chain(sb,i);
               }
            */
            ++errors;
        }
    }

    return errors;
}

int main(int argc, char *argv[])
{
    int errors = 0;
    int i;
    char *filename = NULL;

    fprintf(stderr, "dmsdosfsck 0.0.2 ALPHA TEST (be extremely careful with repairs)\n");

    if (argc == 1) {
usage:
        fprintf(stderr, "usage: dmsdosfsck [ -aflrtvVw ] [ -d path -d ... ] [ -u path -u ... ] device\n"
                "-a          automatically repair the file system\n"
                "-d path (*) drop that file\n"
                "-f      (*) salvage unused chains to files\n"
                "-l          list path names\n"
                "-r          interactively repair the file system\n"
                "-t      (*) test for bad clusters\n"
                "-u path (*) try to undelete that (non-directory) file\n"
                "-v          verbose mode\n"
                "-V      (*) perform a verification pass\n"
                "-w      (*) write changes to disk immediately\n"
                "(*) not yet implemented but option accepted for dosfsck compatibility\n");
        exit(16);
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-a") == 0) { repair_automatically = 1; }
        else if (strcmp(argv[i], "-r") == 0) { repair_interactively = 1; }
        else if (strcmp(argv[i], "-l") == 0) { listfiles = 1; }
        else if (strcmp(argv[i], "-v") == 0) { verbose = 1; }
        else if (argv[i][0] != '-') { filename = argv[i]; }
    }

    if (filename == NULL) { goto usage; }

    if (repair_automatically != 0 || repair_interactively != 0) {
        printf("\n\nWARNING: repair functions are incomplete. Interrupt within 5 seconds to abort.\7\n\n\n");
        sleep(5);
    }

    sb = open_cvf(filename, repair_automatically | repair_interactively);

    if (sb == NULL) {
        fprintf(stderr, "open_cvf %s failed - maybe this isn't a CVF.\n", filename);
        exit(16);
    }

    dblsb = MSDOS_SB(sb)->private_data;

    printf("pass 1: checking FAT...\n");

    i = check_fat();

    if (i) {
        puts("Filesystem has fatal FAT errors");
        puts("Cannot continue, sorry.\n");
        puts("Don't mount this filesystem, it may crash or hang the FAT driver.");
        close_cvf(sb);
        exit(4);
    }

    printf("pass 2: checking cluster chains...\n");

    i = check_chains();

    if (i) {
        printf("filesystem has errors in cluster chains\n");
        ++errors;
    }

    puts("pass 3: calling dmsdos simple_check...");

    i = simple_check(sb, 0);

    if (i == -1) { /* there were fatal FAT errors detected */
        puts("Filesystem still has fatal FAT errors");
//        printf("CANNOT HAPPEN. THIS IS A BUG.\n");
//        close_cvf(sb);
//        abort();
    }

    if (i) {
        printf("filesystem has low-level structure errors\n");

        if (repair("Try to fix them?") == 0) { ++errors; }
        else {
            i = simple_check(sb, 1);

            if (i) {
                printf("couldn't fix all low-level structure errors\n");
                ++errors;
            }
        }
    }

    printf("pass 4: checking directories...\n");

    if (check_root_dir()) {
        printf("filesystem has msdos-level structure errors\n");
        ++errors;
    }

    printf("pass 5: checking for unused chains...\n");

    i = check_unused_chains();

    if (i) {
        printf("filesystem has unused cluster chains\n");
        ++errors;
    }

    close_cvf(sb);

    if (errors == 0) {
        printf("filesystem has no errors\n");
    }

    return (errors == 0) ? 0 : 4;
}
