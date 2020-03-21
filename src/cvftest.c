/*
cvftest.c

DMSDOS: doublespace/drivespace/stacker CVF identification tool.

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
#include <stdlib.h>

int main(int argc, char *argv[])
{
    unsigned char sb[512];
    int i;
    FILE *f;
    char *fn;

    if (argc<2||argc>3) {
usage:
        fprintf(stderr,"usage: %s filename [-v]\n",argv[0]);
        fprintf(stderr,"detect CVFs according to header\n");
        fprintf(stderr,"  -v: be verbose i.e. print result to stdout\n");
        fprintf(stderr,"  \"-\" as filename means read data from stdin\n");
        fprintf(stderr,"exit code: 0 = CVF detected, 1 = no CVF, >1 = some error occured\n");
        exit(20);
    }

    fn=argv[1];

    if (argc==3) {
        if (strcmp(argv[1],"-v")==0) { fn=argv[2]; }
        else if (strcmp(argv[2],"-v")!=0) { goto usage; }
    }

    if (strcmp(fn,"-")==0) { f=stdin; }
    else {
        f=fopen(fn,"rb");

        if (f==NULL) {
            perror("open failed");
            exit(4);
        }
    }

    for (i=0; i<512; ++i) { sb[i]=fgetc(f); }

    if (fgetc(f)==EOF) {
        if (ferror(f)) {
            perror("error reading file");
            exit(3);
        }

        if (argc==3) { goto nocvf; }

        return 1;
    }

    if (argc==2) {
        if (strncmp(sb+3,"MSDBL6.0",8)==0||strncmp(sb+3,"MSDSP6.0",8)==0
                ||strncmp(sb,"STACKER",7)==0) { return 0; }

        return 1;
    }

    if (strncmp(sb+3,"MSDBL6.0",8)==0||strncmp(sb+3,"MSDSP6.0",8)==0) {
        if (sb[51]==2&&sb[13]==16) { printf("drivespace CVF (version 2)\n"); }
        else if ((sb[51]==3||sb[51]==0)&&sb[13]==64) { printf("drivespace 3 CVF\n"); }
        else if (sb[51]<2&&sb[13]==16) { printf("doublespace CVF (version 1)\n"); }
        else { printf("unknown (new? damaged?) doublespace or drivespace CVF\n"); }

        return 0;
    } else if (strncmp(sb,"STACKER",7)==0) {
        int i;
        unsigned char b,c;
        unsigned char *p;
        int StacVersion;

        /* decode super block */
        for (i=0x30,p=sb+0x50,b=sb[0x4c]; i--; p++) {
            b=0xc4-b;
            b=b<0x80?b*2:b*2+1;
            b^=c=*p;
            *p=b;
            b=c;
        }

        if (sb[0x4e]!=0xa||sb[0x4f]!=0x1a) {
            printf("unknown (new? damaged?) stacker CVF\n");
        } else {
            StacVersion=sb[0x60];
            StacVersion&=0xff;
            StacVersion|=sb[0x61]<<8;
            StacVersion&=0xffff;

            if (StacVersion>=410) { printf("stacker version 4 CVF\n"); }
            else { printf("stacker version 3 CVF\n"); }
        }

        return 0;
    }

nocvf:
    printf("not a known CVF\n");
    return 1;
}
