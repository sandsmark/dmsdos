/*
dutil.c

DMSDOS CVF-FAT module: external dmsdos utility.

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

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>

int scan(char *arg)
{
    int w;

    if (strncmp(arg, "0x", 2) == 0) { sscanf(arg + 2, "%x", &w); }
    else { sscanf(arg, "%d", &w); }

    return w;
}

void error(void)
{
    perror("ioctl failed");
    exit(1);
}

int main(int argc, char *argv[])
{
    Dblsb dblsb;
    int fd;
    int ret;
    unsigned long w[10];
    Dblstat dblstat;
    double ratio, dosratio;
    struct {
        unsigned long w;
        unsigned char data[512];
    } buffer;
    int i;
    char a[100];
    char b[100];
    char c[100];
    Mdfat_entry mde;

    if (argc < 2) {
        printf("DMSDOS utility (C) 1995-1998 Frank Gockel, Pavel Pisa\n");
        printf("compiled " __DATE__ " " __TIME__ " under dmsdos version %d.%d.%d%s\n\n",
               DMSDOS_MAJOR, DMSDOS_MINOR, DMSDOS_ACT_REL, DMSDOS_VLT);

        printf("Usage: %s (directory)\n", argv[0]);
        printf("       %s (directory) cluster (clusterno)\n", argv[0]);
        printf("       %s (directory) sector (sectorno) [(file)]\n", argv[0]);
        printf("       %s (directory) rcluster (clusterno) [(file)]\n", argv[0]);
        printf("       %s (directory) rrawcluster (clusterno) [(file)]\n", argv[0]);
        printf("       %s (directory) bitfat (sectorno)\n", argv[0]);
        printf("       %s (directory) setcomp (comp_option)\n", argv[0]);
        printf("       %s (directory) setcf (cf_option)\n", argv[0]);
        printf("       %s (directory) dumpcache\n", argv[0]);
        printf("       %s (directory) synccache [(allow_daemon)]\n", argv[0]);
        printf("       %s (directory) logstat\n", argv[0]);
        printf("       %s (directory) memory\n", argv[0]);
        printf("       %s (directory) checkfs [(repair)]\n", argv[0]);
        printf("       %s (directory) setloglevel (value)\n", argv[0]);
        printf("       %s (directory) setspeedup (value)\n", argv[0]);
        return 0;
    }

    fd = open(argv[1], O_RDONLY);

    if (fd < 0) {
        perror(argv[1]);
        return 2;
    }

    /* this hack enables reverse version check */
    /* it must not be changed in order to recognize incompatible older versions */
    /* this also depends on s_dcluster being the first record in Dblsb */
    dblsb.s_dcluster = DMSDOS_VERSION;

    ret = ioctl(fd, DMSDOS_GET_DBLSB, &dblsb);

    if (ret < 0) {
        printf("This is not a DMSDOS directory.\n");
        close(fd);
        return 2;
    }

    printf("You are running DMSDOS driver version %d.%d.%d.\n", (ret & 0xff0000) >> 16,
           (ret & 0x00ff00) >> 8, ret & 0xff);

    /*printf("debug: ret=0x%08x\n",ret);*/
    if (ret != DMSDOS_VERSION)printf("This utility was compiled for DMSDOS version %d.%d.%d",
                                         (DMSDOS_VERSION & 0xff0000) >> 16, (DMSDOS_VERSION & 0x00ff00) >> 8, DMSDOS_VERSION & 0xff);

    if (ret & 0x0f000000) {
        printf("\nSorry, this utility is too old for the actual DMSDOS driver version.\n");
        close(fd);
        return 2;
    }

    if (ret < 0x00000902) {
        printf("\nSorry, this utility requires at least DMSDOS driver version 0.9.2.\n");
        close(fd);
        return 2;
    }

    if (ret != DMSDOS_VERSION) { printf(" but should still work.\n\n"); }
    else { printf("\n"); }

    printf("Parameters of the CVF the directory specified belongs to:\n");
    printf("dcluster:    %5d  ", dblsb.s_dcluster);
    printf("mdfatstart:  %5d  ", dblsb.s_mdfatstart);
    printf("fatstart:    %5d  ", dblsb.s_fatstart);
    printf("rootdir:     %5d\n", dblsb.s_rootdir);
    printf("root_entries:%5d  ", dblsb.s_rootdirentries);
    printf("sectperclust:%5d  ", dblsb.s_sectperclust);
    printf("bootblock:   %5d  ", dblsb.s_bootblock);
    printf("16bitfat:    %5s\n", dblsb.s_16bitfat ? "yes" : "no");
    printf("datastart: %7d  ", dblsb.s_datastart);
    printf("dataend:   %7d  ", dblsb.s_dataend);
    printf("comp:   0x%08x  ", dblsb.s_comp);
    printf("cfaktor:   %7d\n", dblsb.s_cfaktor + 1);
    printf("max_cluster: %5d  ", dblsb.s_max_cluster);
    printf("max_cluster2:%5d  ", dblsb.s_max_cluster2);
    printf("cvf_version: %5d  ", dblsb.s_cvf_version);
    printf("free_sec:  %7d\n", dblsb.s_free_sectors);

    if (argc == 3) {
        if (strcmp(argv[2], "dumpcache") == 0) {
            if (ioctl(fd, DMSDOS_DUMPCACHE, w) < 0) { error(); }

            printf("Cache status written to syslog.\n");
            close(fd);
            return  0;
        }

        if (strcmp(argv[2], "logstat") == 0) {
            if (ioctl(fd, DMSDOS_LOG_STATISTICS, w) < 0) { error(); }

            printf("Statistics written to syslog.\n");
            close(fd);
            return 0;
        }

        if (strcmp(argv[2], "memory") == 0) {
            if (ioctl(fd, DMSDOS_REPORT_MEMORY, w) < 0) { error(); }

            printf("DMSDOS memory usage (in bytes):\n");
            printf("Cluster cache: %8ld            maximum: ", w[0]);

            if (w[1] > 0) { printf("%8ld (estimated)\n", w[1]); }
            else { printf("   -unknown- \n"); }

            printf("Buffer cache:  %8ld            maximum: %8ld\n", w[2], w[3]);
            return 0;
        }

    }

    if (argc == 3 || argc == 4) {
        if (strcmp(argv[2], "checkfs") == 0) {
            printf("Please wait while filesystem is checked...\n");
            w[0] = (argc == 4) ? scan(argv[3]) : 0;

            if (ioctl(fd, DMSDOS_SIMPLE_CHECK, w) < 0) { error(); }

            if (w[0] == 1 || w[0] == 2) { printf("Check aborted due to lack of kernel memory.\n"); }

            if (w[0] == 0) { printf("No filesystem error found.\n"); }

            if (w[0] == -1) { printf("Filesystem has serious errors: FAT level crosslink(s) found.\n"); }

            if (w[0] == -2) { printf("Filesystem has serious errors: MDFAT level crosslink(s) found.\n"); }

            if (w[0] == -3) { printf("Filesystem BITFAT mismatches MDFAT.\n"); }

            if (w[0] == -1 || w[0] == -2 || w[0] == -3) {
                if (ioctl(fd, DMSDOS_SET_COMP, READ_ONLY) < 0) { error(); }

                printf("The filesystem has been set to read-only mode.\n");
            }

            close(fd);
            return 0;
        }

        if (strcmp(argv[2], "synccache") == 0) {
            printf("Syncing cluster cache....be patient, this may take some time...\n");

            if (ioctl(fd, DMSDOS_SYNC_CCACHE, (argc == 4) ? scan(argv[3]) : 0) < 0) { error(); }

            printf("Cluster cache synced.\n");
            close(fd);
            return 0;
        }

    }

    if (argc < 4) {

        printf("\nPlease wait while filesystem is scanned...\n\n");

        if (ioctl(fd, DMSDOS_EXTRA_STATFS, &dblstat) < 0) { error(); }

        printf("free sectors:  %7ld     ", dblstat.free_sectors);
        printf("used sectors:  %7ld     ", dblstat.used_sectors);
        printf("all sectors:   %7ld\n", dblstat.free_sectors + dblstat.used_sectors);
        printf("max free hole: %7ld     ", dblstat.max_hole);
        i = (100 * (dblstat.free_sectors - dblstat.max_hole)) / dblstat.free_sectors;
        printf("fragmentation:%7d%%     ", i);
        i = (100 * dblstat.used_sectors) / (dblstat.free_sectors + dblstat.used_sectors);
        printf("capacity:     %7d%%\n", i);
        printf("free clusters: %7ld     ", dblstat.free_clusters);
        printf("used clusters: %7ld     ", dblstat.used_clusters);
        printf("all clusters:  %7ld\n", dblstat.free_clusters + dblstat.used_clusters
               + dblstat.lost_clusters);
        printf("compressed:    %7ld     ", dblstat.compressed_clusters);
        printf("uncompressed:  %7ld     ", dblstat.uncompressed_clusters);
        printf("lost clusters: %7ld\n", dblstat.lost_clusters);
        printf("cluster compression:   %5ld%%      ",
               (dblstat.compressed_clusters + dblstat.uncompressed_clusters == 0) ? 0 :
               (100 * dblstat.compressed_clusters) /
               (dblstat.compressed_clusters + dblstat.uncompressed_clusters)
              );

        if (dblstat.sectors_lo != 0) {
            ratio = ((double)dblstat.sectors_hi) / ((double)dblstat.sectors_lo);
        } else { ratio = 2.0; }

        if (dblstat.used_clusters != 0)
            dosratio = ((double)dblstat.used_clusters * dblsb.s_sectperclust) /
                       ((double)dblstat.used_sectors);
        else { dosratio = 2.0; }

        printf("compression ratio:  %5.2f : 1 / %5.2f : 1\n", ratio, dosratio);
        printf("space allocated by clusters (real allocated space):      %7ldKB\n",
               dblstat.sectors_lo / 2);
        printf("space allocated by clusters (space after decompression): %7ldKB\n",
               dblstat.sectors_hi / 2);
        printf("compressed free space (estimated free space):            %7dKB\n",
               ((int)(dblstat.free_sectors * ratio)) / 2);
        printf("uncompressed free space:                                 %7ldKB\n",
               dblstat.free_sectors / 2);
        printf("maximum free space due to cluster limit:                 %7ldKB\n",
               dblstat.free_clusters * dblsb.s_sectperclust / 2);

        if (dblstat.max_hole <= dblsb.s_sectperclust * 3 &&
                dblstat.free_sectors > dblsb.s_sectperclust * 3) {
            printf("Warning: This CVF should be defragmented at internal MDFAT level.\n");
        } else if (dblstat.free_sectors <= dblsb.s_sectperclust * 3 || dblsb.s_full == 2) {
            printf("Warning: This CVF is full. Do not write to it.\n");
        } else if (dblsb.s_full == 1) {
            printf("Warning: This CVF is almost full or highly fragmented at internal MDFAT level.\n");
        } else if (dblstat.free_clusters * dblsb.s_sectperclust < dblstat.free_sectors) {
            printf("Warning: You cannot use all free space of this CVF due to the cluster limit.\n");
            /*
            i=((int)(dblstat.free_sectors*ratio))/dblsb.s_sectperclust
              -dblstat.free_clusters+dblsb.s_max_cluster;
            if(i>dblsb.s_max_cluster2)i=dblsb.s_max_cluster2;
            if(i>dblsb.s_max_cluster)
            printf("         Enlarge the max_cluster value or the dos compression ratio.\n"
                   "         I recommend the following max_cluster value for this CVF: %d\n",i);
            */
            printf("         Adapt the compression ratio under Dos.\n");
        }

    }

    else if (strcmp(argv[2], "bitfat") == 0) {
        w[0] = scan(argv[3]);

        if (ioctl(fd, DMSDOS_READ_BITFAT, w) < 0) { error(); }

        if (w[1] == 0) { printf("\nbitfat: sector is free\n"); }

        if (((signed long)w[1]) > 0) { printf("\nbitfat: sector is allocated\n"); }

        if (((signed long)w[1]) < 0) { printf("\nbitfat: value out of range\n"); }
    }

    else if (strcmp(argv[2], "cluster") == 0) {
        w[0] = scan(argv[3]);
        w[1] = (unsigned long)(&mde);

        if (ioctl(fd, DMSDOS_READ_MDFAT, w) < 0) { error(); }

        printf("used:              %s\n", (mde.flags & 2) ? "yes" : "no");
        printf("compressed:        %s\n", (mde.flags & 1) ? "no" : "yes");
        printf("flags (raw):       0x%x\n", mde.flags);
        printf("size uncompressed: %d  compressed: %d\n",
               mde.size_hi_minus_1 + 1, mde.size_lo_minus_1 + 1);
        printf("first sector:      %ld\n", mde.sector_minus_1 + 1);
        printf("unknown bits:      %d\n", mde.unknown);

        if (ioctl(fd, DMSDOS_READ_DFAT, w) < 0) { error(); }

        printf("next cluster:      %ld\n", w[1]);
    }

    else if (strcmp(argv[2], "rrawcluster") == 0) {
        FILE *f = NULL;

        if (argc == 5) { f = fopen(argv[4], "wb"); }

        w[0] = scan(argv[3]);
        w[1] = (unsigned long)(&mde);

        if (ioctl(fd, DMSDOS_READ_MDFAT, w) < 0) { error(); }

        if (mde.flags & 2) {
            int s;

            for (s = 0; s < mde.size_lo_minus_1 + 1; ++s) {
                buffer.w = s + mde.sector_minus_1 + 1;

                if (ioctl(fd, DMSDOS_READ_BLOCK, &buffer) < 0) { error(); }

                for (i = 0; i < 512; ++i) {
                    if (f) { fputc(buffer.data[i], f); }

                    if (i % 16 == 0) {sprintf(a, "%3X : ", i + 512 * s); strcpy(b, "");}

                    sprintf(c, " %02X", buffer.data[i]);
                    strcat(a, c);

                    if (buffer.data[i] >= 32 && buffer.data[i] < 128) {
                        sprintf(c, "%c", buffer.data[i]);
                    } else { strcpy(c, "."); }

                    strcat(b, c);

                    if (i % 16 == 15) { printf("%s  %s\n", a, b); }
                }
            }
        } else { printf("unused cluster, contains no raw data.\n"); }

        if (f) { fclose(f); }
    }

    else if (strcmp(argv[2], "rcluster") == 0) {
        FILE *f = NULL;

        if (argc == 5) { f = fopen(argv[4], "wb"); }

        w[0] = scan(argv[3]);
        w[1] = (unsigned long)(&mde);

        if (ioctl(fd, DMSDOS_READ_MDFAT, w) < 0) { error(); }
        else {
            struct {
                unsigned long w;
                unsigned char data[1];
            } *buf = malloc(dblsb.s_sectperclust * 512 + 32);

            if (buf == NULL) {
                printf("Uhh... unable to get memory...\n");
                exit(3);
            }

            buf->w = w[0];

            if (ioctl(fd, DMSDOS_READ_CLUSTER, buf) < 0) { error(); }

            for (i = 0; i < 512 * dblsb.s_sectperclust; ++i) {
                if (f) { fputc(buf->data[i], f); }

                if (i % 16 == 0) {sprintf(a, "%3X : ", i); strcpy(b, "");}

                sprintf(c, " %02X", buf->data[i]);
                strcat(a, c);

                if (buf->data[i] >= 32 && buf->data[i] < 128) {
                    sprintf(c, "%c", buf->data[i]);
                } else { strcpy(c, "."); }

                strcat(b, c);

                if (i % 16 == 15) { printf("%s  %s\n", a, b); }
            }
        }

        if (f) { fclose(f); }
    }

    else if (strcmp(argv[2], "sector") == 0) {
        FILE *f = NULL;

        if (argc == 5) { f = fopen(argv[4], "wb"); }

        buffer.w = scan(argv[3]);

        if (ioctl(fd, DMSDOS_READ_BLOCK, &buffer) < 0) { error(); }

        for (i = 0; i < 512; ++i) {
            if (f) { fputc(buffer.data[i], f); }

            if (i % 16 == 0) {sprintf(a, "%3X : ", i); strcpy(b, "");}

            sprintf(c, " %02X", buffer.data[i]);
            strcat(a, c);

            if (buffer.data[i] >= 32 && buffer.data[i] < 128) {
                sprintf(c, "%c", buffer.data[i]);
            } else { strcpy(c, "."); }

            strcat(b, c);

            if (i % 16 == 15) { printf("%s  %s\n", a, b); }
        }

        if (f) { fclose(f); }
    }

    else if (strcmp(argv[2], "setcomp") == 0) {
        ret = 0;

        if (strcmp(argv[3], "ro") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, READ_ONLY); }
        else if (strcmp(argv[3], "no") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, UNCOMPRESSED); }
        else if (strcmp(argv[3], "guess") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, GUESS); }
        else if (strcmp(argv[3], "ds00") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, DS_0_0); }
        else if (strcmp(argv[3], "ds01") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, DS_0_1); }
        else if (strcmp(argv[3], "ds02") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, DS_0_2); }
        else if (strcmp(argv[3], "jm00") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, JM_0_0); }
        else if (strcmp(argv[3], "jm01") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, JM_0_1); }
        else if (strcmp(argv[3], "sq00") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, SQ_0_0); }
        else if (strcmp(argv[3], "sd4") == 0) { ret = ioctl(fd, DMSDOS_SET_COMP, SD_4); }
        else { printf("??? mode %s not recognized.\n", argv[3]); }

        if (ret < 0) { error(); }
    }

    else if (strcmp(argv[2], "setcf") == 0) {
        if (ioctl(fd, DMSDOS_SET_CF, scan(argv[3]) - 1) < 0) { error(); }
    }

    else if (strcmp(argv[2], "setmaxcluster") == 0) {
        /*if(ioctl(fd,DMSDOS_SET_MAXCLUSTER,scan(argv[3]))<0)error();*/
        printf("setmaxcluster is depreciated (sorry, it became too problematic).\n"
               "Please use the tools that came with your CVF package under Dos.\n");
    }

    else if (strcmp(argv[2], "setloglevel") == 0) {
        if (ioctl(fd, DMSDOS_SET_LOGLEVEL, scan(argv[3])) < 0) { error(); }
    }

    else if (strcmp(argv[2], "setspeedup") == 0) {
        if (ioctl(fd, DMSDOS_SET_SPEEDUP, scan(argv[3])) < 0) { error(); }
    }

    else { printf("??? syntax error in command line.\n"); }

    close(fd);
    return 0;
}
