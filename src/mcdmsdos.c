/*

mcdmsdos.c

DMSDOS: external filesystem interface for Midnight Commander

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
#include<assert.h>

#include"dmsdos.h"
#include"lib_interface.h"

#define M_LIST 1
#define M_OUT 2
#define M_IN 3

/*this is not good - but currently we have only one CVF open at a time*/
static struct super_block *sb;
static Dblsb *dblsb;

int scan(char *text)
{
    int v = 0;

    if (strncmp(text, "0x", 2) == 0 || strncmp(text, "0X", 2) == 0) {
        sscanf(text + 2, "%x", &v);
    } else {
        sscanf(text, "%d", &v);
    }

    return v;
}

unsigned char *get_root_dir(void)
{
    unsigned char *data;
    struct buffer_head *bh;
    int i;

    data = malloc(dblsb->s_rootdirentries * 32);

    if (data == NULL) { return NULL; }

    for (i = 0; i < dblsb->s_rootdirentries * 32 / 512; ++i) {
        bh = raw_bread(sb, dblsb->s_rootdir + i);

        if (bh == NULL) {free(data); return NULL;}

        memcpy(data + i * 512, bh->b_data, 512);
        raw_brelse(sb, bh);
    }

    return data;
}

int copy_cluster_out(int nr, int len, FILE *f)
{
    unsigned char *data;
    int i, j;

    if (nr == 0) {
        data = get_root_dir();

        if (data == NULL) { return -1; }

        i = dblsb->s_rootdirentries * 32;
    } else {
        data = malloc(dblsb->s_sectperclust * 512);

        if (data == NULL) { return -1; }

        i = dmsdos_read_cluster(sb, data, nr);

        if (i < 0) {free(data); return -1;}
    }

    if (len <= 0 || len > i) { len = i; }

    for (j = 0; j < len; ++j) { fputc(data[j], f); }

    free(data);
    return ferror(f);
}

int handle_dir_chain(int start, int rek, char *prefix);

static void print_filename(const char *filename)
{
    for (int i = 0; i < strlen(filename); ++i) {
        if (filename[i] >= 32 && filename[i] < 127) {
            printf("%c", filename[i]);
            continue;
        }

        // TODO: proper codepage support, this is Code page 865 (DOS Nordic)
        switch ((uint8_t)filename[i]) {
        case 0x92:
            printf("æ");
            break;

        case 0x9c:
            printf("£");
            break;

        case 0x9d:
            printf("ø");
            break;

        //case 0xff:
        //    lprintf(" ");
        //    break;
        default:
            printf(" 0x%x ", filename[i]);
            break;
        }
    }
}

#define ENTRY_READ_ONLY 0x1
#define ENTRY_HIDDEN 0x2
#define ENTRY_SYSTEM 0x3
#define ENTRY_VOLUME_LABEL 0x8
#define ENTRY_SUB_DIR 0x10
#define ENTRY_ARCHIVE 0x20
#define ENTRY_DEVICE 0x40
#define ENTRY_RESERVED 0x80

int display_dir_cluster(int nr, int rek, char *prefix)
{
    unsigned char *data;
    int i, j;

    printf("display_dir_cluster called with nr=%d rek=%d prefix=%s\n",
           nr, rek, prefix);

    if (nr == 0) {
        data = get_root_dir();

        if (data == NULL) { return -1; }

        i = dblsb->s_rootdirentries * 32;
    } else {
        data = malloc(dblsb->s_sectperclust * 512);

        if (data == NULL) { return -1; }

        i = dmsdos_read_cluster(sb, data, nr);

        if (i < 0) {free(data); return -1;}
    }

    j = 0;

    while (data[j] == 0 && j < i) { j++; }

    for (; j < dblsb->s_sectperclust * 512; j += 32) {
        unsigned char *pp;
        unsigned int x;
        char filename[15] = "";
        int nstart;
        long size;
        char datestr[16][4] = {"?00", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
                               "?13", "?14", "?15"
                              };

        if (data[j] == 0) { continue; }

        if (data[j] == 0xe5) { continue; }

        const uint8_t entry_flags = data[j + 11];

        if (entry_flags & ENTRY_VOLUME_LABEL) { continue; }

        if (data[j] == '.') { continue; }

        for (i = 0; i < 11; ++i) {
            if (i == 8 && data[j + i] != ' ') { strcat(filename, "."); }

            if (data[j + i] != ' ') { strncat(filename, &(data[j + i]), 1); }
        }

        for (i = 0; i < strlen(filename); ++i) {
            if (filename[i] >= 32 && filename[i] < 127) {
                filename[i] = tolower(filename[i]);
                continue;
            }
        }

        if (entry_flags & 16) { printf("dr"); }
        else { printf("-r"); }

        if (entry_flags & ENTRY_READ_ONLY) { printf("-"); }
        else { printf("w"); }

        printf("xr-xr-x 1 0 0"); /* bogus values :) */

        printf("  ");

        if (entry_flags & ENTRY_READ_ONLY) { printf("R"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_HIDDEN) { printf("H"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_SYSTEM) { printf("S"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_VOLUME_LABEL) { printf("V"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_SUB_DIR) { printf("D"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_ARCHIVE) { printf("A"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_DEVICE) { printf("?"); }
        else { printf(" "); }

        if (entry_flags & ENTRY_RESERVED) { printf("?"); }
        else { printf(" "); }

        pp = &(data[j + 28]);
        size = CHL(pp);
        printf(" %7lu", size);

        pp = &(data[j + 24]);
        x = CHS(pp);
        int shortyear = ((x >> 9) + 80);

        if (shortyear > 99) {
            shortyear -= 99;
        }

        printf(" %02d.%02d.%02d", x & 31, (x >> 5) & 15, shortyear);
        printf(" %s", datestr[(x >> 5) & 15]);
        printf(" %02d", x & 31);
        printf(" %04d", (x >> 9) + 1980); /* y2k compliant :) */

        pp = &(data[j + 22]);
        x = CHS(pp);
        /*printf("  %02d:%02d:%02d",x>>11,(x>>5)&63,(x&31)<<1);
        printf(" %02d:%02d",x>>11,(x>>5)&63);*/

        pp = &(data[j + 26]);
        nstart = CHS(pp);
        /*printf(" %5d",nstart); */

        printf(" %s", prefix);
        print_filename(filename);
        printf("\n");

        if ((entry_flags & ENTRY_SUB_DIR) != 0 && rek != 0 && filename[0] != '.') {
            char *nprefix;
            nprefix = malloc(strlen(prefix) + 20);

            if (nprefix == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(3);
            }

            strcpy(nprefix, prefix);
            strcat(nprefix, filename);
            strcat(nprefix, "/");
            handle_dir_chain(nstart, rek, nprefix);
            free(nprefix);
        }

    }

    free(data);
    return 0;
}

int handle_dir_chain(int start, int rek, char *prefix)
{
    int i, next;

    if (start == 0) { return display_dir_cluster(0, rek, prefix); }

    if (start == 1 || start < 0 || start > dblsb->s_max_cluster) { return -1; }

    do {
        next = dbl_fat_nextcluster(sb, start, NULL);
        i = display_dir_cluster(start, rek, prefix);

        if (i < 0) { return i; }

        start = next;
    } while (next > 1 && next <= dblsb->s_max_cluster);

    if (next >= 0) {
        return -1;
    }

    return 0;
}

int handle_file_chain(int start, int len, FILE *f)
{
    int i, next;

    if (start == 0) { return -1; } /* never a file :) */

    if (start == 1 || start < 0 || start > dblsb->s_max_cluster) { return -1; }

    do {
        next = dbl_fat_nextcluster(sb, start, NULL);
        i = copy_cluster_out(start, len, f);

        if (i < 0) { return i; }

        len -= dblsb->s_sectperclust * 512;

        if (len <= 0) { break; }

        start = next;
    } while (next > 1 && next <= dblsb->s_max_cluster);

    if (next >= 0) {
        return -1;
    }

    return 0;
}

int scan_dir(char *entry, int start, int *len)
{
    char buf[] = "           ";
    /*12345678EXT*/
    int i;
    int size = -1;
    unsigned char *data;
    int next = -1;

    if (strcmp(entry, ".") == 0) { return start; }
    else if (strcmp(entry, "..") == 0) { strncpy(buf, "..", 2); }
    else if (*entry == '.') { return -1; }
    else
        for (i = 0; i < 11; ++i) {
            if (*entry == '.' && i <= 7) {i = 7; ++entry; continue;}

            if (*entry == '.' && i == 8) {i = 7; ++entry; continue;}

            if (*entry == '.') { break; }

            if (*entry == '\0') { break; }

            buf[i] = toupper(*entry);
            ++entry;
        }

    do {
        /*printf("scan_dir: searching for %s in %d\n",buf,start);*/

        if (start == 0) {
            data = get_root_dir();
            size = dblsb->s_rootdirentries;
            next = -1;
        } else {
            data = malloc(dblsb->s_sectperclust * 512);

            if (data != NULL) {
                i = dmsdos_read_cluster(sb, data, start);

                if (i < 0) {free(data); data = NULL;}

                size = i / 32;
                next = dbl_fat_nextcluster(sb, start, NULL);

                if (next == 0)
                    fprintf(stderr, "warning: cluster %d is marked as unused in FAT\n",
                            next);
            }
        }

        if (data == NULL) { return -1; }

        assert(size != -1);
        for (i = 0; i < size; ++i) {
            if (strncmp(&(data[i * 32]), buf, 11) == 0) {
                unsigned char *pp;
                int cluster;

                pp = &(data[i * 32 + 26]);
                cluster = CHS(pp);
                pp = &(data[i * 32 + 28]);

                if (len) { *len = CHL(pp); }

                free(data);
                return cluster;
            }
        }

        free(data);
        start = next;
    } while (next > 0 && next <= dblsb->s_max_cluster);

    return -1;
}

int scan_path(char *path, int start, int *len)
{
    int i;
    char *p;

    for (p = strtok(path, "/"); p; p = strtok(NULL, "/")) {
        i = scan_dir(p, start, len);

        if (i < 0) {
            fprintf(stderr, "path component %s not found\n", p);
            return -1;
        }

        start = i;
    }

    return start;
}

int main(int argc, char *argv[])
{
    int mode = 0;
    int cluster;
    int i;
    char *p;
    int len;
    FILE *f;

    fprintf(stderr, "mcdmsdos version 0.2.0 (for libdmsdos 0.9.x and newer)\n");

    if (argc < 2) {
        fprintf(stderr, "\nusage: mcdmsdos <mc-extfs-command> ...\n");
        fprintf(stderr, "where <mc-extfs-command> can be:\n");
        fprintf(stderr, "        list <CVF>\n");
        fprintf(stderr, "        copyout <CVF> <path/name_in_CVF> <outfile>\n");
        fprintf(stderr, "        copyin <CVF> <path/name_in_CVF> <infile> [*]\n");
        fprintf(stderr, "      [*] currently not implemented\n");
        return 1;
    }

    /* check syntax */
    if (strcmp(argv[1], "list") == 0) {
        mode = M_LIST;

        if (argc != 3) {
            fprintf(stderr, "wrong number of arguments\n");
            return 1;
        }
    } else if (strcmp(argv[1], "copyout") == 0) {
        mode = M_OUT;

        if (argc != 5) {
            fprintf(stderr, "wrong number of arguments\n");
            return 1;
        }
    } else if (strcmp(argv[1], "copyin") == 0) {
        mode = M_IN;

        if (argc != 5) {
            fprintf(stderr, "wrong number of arguments\n");
            return 1;
        }

        fprintf(stderr, "copyin command is not implemented\n");
        return -2;
    } else {
        fprintf(stderr, "unknown command\n");
        return -1;
    }

    sb = open_cvf(argv[2], 0/*read-only*/);

    if (sb == NULL) {
        printf("open CVF %s failed\n", argv[1]);
        return 2;
    }

    dblsb = MSDOS_SB(sb)->private_data;


    if (mode == M_LIST) {
        i = handle_dir_chain(0, 1, "");
    }

    else if (mode == M_OUT) {
        p = malloc(strlen(argv[3]) + 1);
        strcpy(p, argv[3]);
#ifdef _WIN32

        /* convert to Unix style path */
        for (i = 0; i < strlen(p); ++i) {if (p[i] == '\\')p[i] = '/';}

#endif

        if (*p == '/') { ++p; }

        cluster = scan_path(p, 0, &len);

        if (cluster < 0) {
            fprintf(stderr, "%s not found\n", argv[3]);
            return 1;
        }

        f = fopen(argv[4], "wb");

        if (f == NULL) {
            perror("open write failed");
            i = -1;
        } else {
            i = handle_file_chain(cluster, len, f);
            fclose(f);
        }
    }

    close_cvf(sb);

    return i;
}
