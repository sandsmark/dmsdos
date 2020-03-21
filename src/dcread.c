/*

dcread.c

DMSDOS: example program illustrating how to use the dmsdos library.

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


This is an example how to use the dmsdos library. This program displays
a cluster on the screen in one of several formats (hexdump, text, etc.).
It can also search a file through the directories.

For documentation about the dmsdos library see file libdmsdos.doc.

Warning: This utility is not perfect. It does not check file end properly.
It does not even distinguish between files and directories. It does not
support long file names. And the file name conversion to 8.3 name space is
far away from good. But example code never has to be perfect :)

There's also no documentation how to use this program except the usage
line. Example code never has documentation. Well, yes, you are expected
to read through the source code. :)

*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>

#include"dmsdos.h"
#include"lib_interface.h"

#define M_RAW 1
#define M_HEX 0
#define M_DIR 2
#define M_TXT 3
#define M_DISPLAYMASK 3
#define M_VERBOSE 16

/*this is not good - but currently we have only one CVF open at a time*/
struct super_block *sb;
Dblsb *dblsb;

int scan(char *text)
{
    int v=0;

    if (strncmp(text,"0x",2)==0||strncmp(text,"0X",2)==0) {
        sscanf(text+2,"%x",&v);
    } else {
        sscanf(text,"%d",&v);
    }

    return v;
}

unsigned char *get_root_dir(void)
{
    unsigned char *data;
    struct buffer_head *bh;
    int i;

    data=malloc(dblsb->s_rootdirentries*32);

    if (data==NULL) { return NULL; }

    for (i=0; i<dblsb->s_rootdirentries*32/512; ++i) {
        bh=raw_bread(sb,dblsb->s_rootdir+i);

        if (bh==NULL) {free(data); return NULL;}

        memcpy(data+i*512,bh->b_data,512);
        raw_brelse(sb,bh);
    }

    return data;
}

int display_cluster(int nr, int mode)
{
    unsigned char *data;
    int i,j;

    if (nr==0) {
        data=get_root_dir();

        if (data==NULL) { return -1; }

        i=dblsb->s_rootdirentries*32;
    } else {
        data=malloc(dblsb->s_sectperclust*512);

        if (data==NULL) { return -1; }

        i=dmsdos_read_cluster(sb,data,nr);

        if (i<0) {free(data); return -1;}
    }

    if (mode&M_VERBOSE) { fprintf(stderr,"cluster %d has length %d\n",nr,i); }

    switch (mode&M_DISPLAYMASK) {
    case M_RAW:
        for (j=0; j<dblsb->s_sectperclust*512; ++j) { printf("%c",data[j]); }

        break;

    case M_HEX:
        for (j=0; j<dblsb->s_sectperclust*512; j+=16) {
            char buf[100];
            char str[100];

            sprintf(str,"%04X:",j);

            for (i=0; i<16; ++i) {
                sprintf(buf," %02X",data[j+i]);
                strcat(str,buf);
            }

            strcat(str,"  ");

            for (i=0; i<16; ++i) {
                if (data[i+j]>=32&&data[i+j]<=126) { sprintf(buf,"%c",data[i+j]); }
                else { strcpy(buf,"."); }

                strcat(str,buf);
            }

            printf("%s\n",str);
        }

        break;

    case M_DIR:
        for (j=0; j<dblsb->s_sectperclust*512; j+=32) {
            unsigned char *pp;
            unsigned int x;

            if (data[j]==0) { break; }

            if (data[j]==0xe5) {printf("--DELETED--\n"); continue;}

            for (i=0; i<11; ++i) {
                if (i==8) { printf(" "); }

                printf("%c",data[j+i]);
            }

            printf("  ");

            if (data[j+11]&1) { printf("R"); }
            else { printf(" "); }

            if (data[j+11]&2) { printf("H"); }
            else { printf(" "); }

            if (data[j+11]&4) { printf("S"); }
            else { printf(" "); }

            if (data[j+11]&8) { printf("V"); }
            else { printf(" "); }

            if (data[j+11]&16) { printf("D"); }
            else { printf(" "); }

            if (data[j+11]&32) { printf("A"); }
            else { printf(" "); }

            if (data[j+11]&64) { printf("?"); }
            else { printf(" "); }

            if (data[j+11]&128) { printf("?"); }
            else { printf(" "); }

            pp=&(data[j+22]);
            x=CHS(pp);
            printf("  %02d:%02d:%02d",x>>11,(x>>5)&63,(x&31)<<1);

            pp=&(data[j+24]);
            x=CHS(pp);
            printf(" %02d.%02d.%04d",x&31,(x>>5)&15,(x>>9)+1980); /* y2k compliant :) */

            pp=&(data[j+26]);
            printf(" %5d",CHS(pp));

            pp=&(data[j+28]);
            printf(" %7lu\n",CHL(pp));
        }

        break;

    case M_TXT:
        i=0;

        for (j=0; j<dblsb->s_sectperclust*512; j++) {
            if (data[j]==10) {
                printf("\n");
                i=0;
                continue;
            }

            if (data[j]>=32&&data[j]<=126) { printf("%c",data[j]); }
            else { printf("."); }

            ++i;

            if (i==80&&j<511&&data[j+1]!=10) {
                printf("\n");
                i=0;
            }
        }

        if (i) { printf("\n"); }

        break;

    default:
        fprintf(stderr,"display mode not implemented\n");
        free(data);
        return -1;
    }

    free(data);
    return 0;
}

int display_chain(int start, int mode)
{
    int i,next;

    if (start==0) { return display_cluster(0,mode); }

    if (start==1||start<0||start>dblsb->s_max_cluster) { return -1; }

    do {
        next=dbl_fat_nextcluster(sb,start,NULL);

        if (next==0&&(mode&M_VERBOSE)!=0) {
            fprintf(stderr,"warning: cluster %d is marked as unused in FAT\n",start);
        }

        i=display_cluster(start,mode);

        if (i<0) { return i; }

        start=next;
    } while (next>1&&next<=dblsb->s_max_cluster);

    if (next>=0) {
        fprintf(stderr,"chain has no valid end in FAT\n");
        return -1;
    }

    return 0;
}

int scan_dir(char *entry,int start)
{
    char buf[]="           ";
    /*12345678EXT*/
    int i;
    int size;
    unsigned char *data;
    int next;

    if (strcmp(entry,".")==0) { return start; }
    else if (strcmp(entry,"..")==0) { strncpy(buf,"..",2); }
    else if (*entry=='.') { return -1; }
    else
        for (i=0; i<11; ++i) {
            if (*entry=='.'&&i<=7) {i=7; ++entry; continue;}

            if (*entry=='.'&&i==8) {i=7; ++entry; continue;}

            if (*entry=='.') { break; }

            if (*entry=='\0') { break; }

            buf[i]=toupper(*entry);
            ++entry;
        }

    do {
        printf("scan_dir: searching for %s in %d\n",buf,start);

        if (start==0) {
            data=get_root_dir();
            size=dblsb->s_rootdirentries;
            next=-1;
        } else {
            data=malloc(dblsb->s_sectperclust*512);

            if (data!=NULL) {
                i=dmsdos_read_cluster(sb,data,start);

                if (i<0) {free(data); data=NULL;}

                size=i/32;
                next=dbl_fat_nextcluster(sb,start,NULL);

                if (next==0)
                    fprintf(stderr,"warning: cluster %d is marked as unused in FAT\n",
                            next);
            }
        }

        if (data==NULL) { return -1; }

        for (i=0; i<size; ++i) {
            if (strncmp(&(data[i*32]),buf,11)==0) {
                unsigned char *pp;
                int cluster;

                pp=&(data[i*32+26]);
                cluster=CHS(pp);
                free(data);
                return cluster;
            }
        }

        free(data);
        start=next;
    } while (next>0&&next<=dblsb->s_max_cluster);

    return -1;
}

int scan_path(char *path,int start)
{
    int i;
    char *p;

    for (p=strtok(path,"/"); p; p=strtok(NULL,"/")) {
        i=scan_dir(p,start);

        if (i<0) {
            fprintf(stderr,"path component %s not found\n",p);
            return -1;
        }

        start=i;
    }

    return start;
}

int main(int argc, char *argv[])
{
    int mode=0;
    int cluster;
    int i;

    if (argc<3) {
        fprintf(stderr,"usage: dcread CVF cluster|/path/to/file [raw|dir|hex|txt]\n");
        return 1;
    }

    sb=open_cvf(argv[1],0/*read-only*/);

    if (sb==NULL) {
        printf("open CVF %s failed\n",argv[1]);
        return 2;
    }

    dblsb=MSDOS_SB(sb)->private_data;

    if (*(argv[2])=='/') {
        cluster=scan_path(argv[2]+1,0);

        if (cluster<0) {
            fprintf(stderr,"%s not found\n",argv[2]);
            return 1;
        }
    } else { cluster=scan(argv[2]); }

    if (argc==4) {
        if (strcmp(argv[3],"raw")==0) { mode=M_RAW; }
        else if (strcmp(argv[3],"dir")==0) { mode=M_DIR; }
        else if (strcmp(argv[3],"hex")==0) { mode=M_HEX; }
        else if (strcmp(argv[3],"txt")==0) { mode=M_TXT; }
        else {
            fprintf(stderr,"invalid argument %s\n",argv[3]);
            close_cvf(sb);
            return 1;
        }
    }

    i=display_chain(cluster,mode|M_VERBOSE);

    close_cvf(sb);

    return i;
}
