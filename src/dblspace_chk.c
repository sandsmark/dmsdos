/*
dblspace_chk.c

DMSDOS CVF-FAT module: bitfat check routines.

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
#endif

#include <string.h>

#define MAXMSG 20

extern unsigned long loglevel;
extern unsigned long dmsdos_speedup;
extern int daemon_present;

#ifdef DMSDOS_CONFIG_STAC
/* reads stacker BITFAT sumary informations */
__u8 *stac_bitfat_sumary(struct super_block *sb,
                         struct buffer_head **pbh)
{
    int pos,sector;
    struct buffer_head *bh;
    int bitfat2;
    Dblsb *dblsb=MSDOS_SB(sb)->private_data;

    bitfat2=dblsb->s_cvf_version>STAC3;
    pos=dblsb->s_dataend-dblsb->s_datastart; /* number of data sectors */
    pos=(pos+(bitfat2?4:8))>>(bitfat2?2:3);
    pos=(pos+0xF)&~0xF;
    sector=pos/SECTOR_SIZE+dblsb->s_mdfatstart; /* here it's AMAP start !!! */
    *pbh=bh=raw_bread(sb,sector);

    if (bh==NULL) { return (NULL); }

    return (bh->b_data+pos%SECTOR_SIZE);
};

/* sets bitfat state, 0=query only, 1=clean, 2=dirty, 3=bad, 11=force clean */
int stac_bitfat_state(struct super_block *sb,int new_state)
{
    int old_state;
    __u8 *pp;
    struct buffer_head *bh;
    static __u8 bitfat_up_to_date_fl[4]= {0xAA,0xBB,0xAA,0xAA};
    static __u8 bitfat_changing_fl[4]= {0xAA,0xBB,0x55,0x55};
    static __u8 bitfat_bad_fl[4]= {0xAA,0xBB,0x00,0x00};
    Dblsb *dblsb=MSDOS_SB(sb)->private_data;

    if (dblsb->s_cvf_version<STAC3) { return 0; }

    if ((pp=stac_bitfat_sumary(sb,&bh))==NULL) {
        printk(KERN_ERR "DMSDOS: read BITFAT state error\n");
        return -2;
    };

    if (!memcmp(pp,bitfat_up_to_date_fl,sizeof(bitfat_up_to_date_fl))) {
        old_state=1;
    } else if (!memcmp(pp,bitfat_changing_fl,sizeof(bitfat_changing_fl))) {
        old_state=2;
    } else { old_state=3; }

    if (new_state&&(dblsb->s_comp!=READ_ONLY)
            &&((old_state!=3)||(new_state&0xF0))) {
        if ((new_state&0xF)==1) {
            memcpy(pp,bitfat_up_to_date_fl,sizeof(bitfat_up_to_date_fl));
        } else if ((new_state&0xF)==2) {
            memcpy(pp,bitfat_changing_fl,sizeof(bitfat_changing_fl));
        } else { memcpy(pp,bitfat_bad_fl,sizeof(bitfat_bad_fl)); }

        raw_mark_buffer_dirty(sb,bh,1);
    };

    raw_brelse(sb,bh);

    return old_state;
};

/* prepared for repair of BITFAT */
int stac_simple_check(struct super_block *sb, int repair)
{
    unsigned char *sect_array;
    int clust,i,j,val,sect;
    int non_lin_alloc;
    Stac_cwalk cw;
    struct buffer_head *bh;
    __u8 *pp;
    int free_sects;
    int deleted_clusts;
    int bitfat_dirty;
    static __u8 inc_tab[4]= {1,4,16,64};
    Dblsb *dblsb=MSDOS_SB(sb)->private_data;

#define set_bits(field,nr,val) field[nr>>2]|=val<<((nr&3)<<1)
#define get_bits(field,nr) ((field[nr>>2]>>((nr&3)<<1))&3)
#define inc_bits(field,nr) field[nr>>2]+=inc_tab[nr&3]

    bitfat_dirty=0;

    if (dblsb->s_comp==READ_ONLY) { repair=0; }

    /* check bitfat mount id */

    {

        if ((pp=stac_bitfat_sumary(sb,&bh))==NULL) {
            printk(KERN_ERR "DMSDOS: simple_check: read BITFAT sumary error\n");
            return -2;
        };

        if ((i=stac_bitfat_state(sb,0))!=1) {
            if (i>2) {
                printk(KERN_WARNING "DMSDOS: simple_check: BITFAT abnormal state: ");

                for (i=0; i<16; i++) { printk(" %02X",(int)pp[i]); }

                printk("\n");
            } else { printk(KERN_NOTICE "DMSDOS: simple_check: BITFAT mounted/dirty\n"); }

            if (repair) {
                printk(KERN_INFO "DMSDOS: Updating BITFAT\n");
                stac_bitfat_state(sb,0x12);
                bitfat_dirty=1;
            };
        };

        printk(KERN_INFO "DMSDOS: Sumary: info1 = %d\n",(int)CHL((pp+4)));

        printk(KERN_INFO "DMSDOS: Sumary: info2 = %d\n",(int)(CHL((pp+8)))-(0xF<<28));

        raw_brelse(sb,bh);
    };

    /* check mdfat */

    val=dblsb->s_dataend/4 + 1;
    sect_array=(unsigned char *)vmalloc(val);

    if (sect_array==NULL) {
        printk(KERN_WARNING "DMSDOS: simple_check: MDFAT+BITFAT test skipped (no memory)\n");
        return 2;
    }

    for (i=0; i<val; ++i) { sect_array[i]=0; }

    deleted_clusts=0;
    non_lin_alloc=0;

    for (clust=2; clust<=dblsb->s_max_cluster2; ++clust) {
        i=stac_cwalk_init(&cw,sb,clust,0);

        if (i>0) {
            if (cw.flags&0x40) { deleted_clusts++; }

            while ((sect=stac_cwalk_sector(&cw))>0) {
                if (sect>dblsb->s_dataend||sect<dblsb->s_datastart) {
                    printk(KERN_ERR "DMSDOS: MDFAT entry invalid (cluster %d, sect %d)\n",
                           clust,sect);
mde_sect_error:
                    stac_cwalk_done(&cw);
                    vfree(sect_array);
                    return -2;
                }

                val=get_bits(sect_array,sect);

                if (val) {
                    if (dblsb->s_cvf_version==STAC3||
                            (((cw.flags&0xA0)!=0xA0)&&(cw.flen||cw.fcnt))) {
                        printk(KERN_ERR "DMSDOS: MDFAT crosslink detected (cluster %d)\n",
                               clust);
                        goto mde_sect_error;
                    };

                    if (((cw.flags&0xA0)!=0xA0)&&!non_lin_alloc) {
                        non_lin_alloc++;
                        printk(KERN_NOTICE "DMSDOS: Interesting MDFAT non-lin subalocation (cluster %d)\n",
                               clust);
                    };
                };

                inc_bits(sect_array,sect);
            };

            stac_cwalk_done(&cw);
        } else if (i<0) {
            printk(KERN_ERR "DMSDOS: MDFAT bad allocation (cluster %d)\n",
                   clust);
            vfree(sect_array);
            return -2;
        };
    };

    /* check bitfat */

    j=0; /* count BITFAT mismatches */

    free_sects=0;

    for (i=dblsb->s_datastart; i<=dblsb->s_dataend; ++i) {
        if ((val=get_bits(sect_array,i))==0) { free_sects++; }

        if (dbl_bitfat_value(sb,i,NULL)!=val) {
            if (repair) {
                if (!j) {
                    /* repair of first mitchmatch in BITFAT */
                    printk(KERN_INFO "DMSDOS: Updating BITFAT.\n");

                    if (stac_bitfat_state(sb,0x12)<=0) {
                        printk(KERN_ERR "DMSDOS: simple_check: BITFAT state error\n");
                        vfree(sect_array);
                        return -3;
                    };

                    /* mark bitfat as dirty */
                    bitfat_dirty=1;
                };

                dbl_bitfat_value(sb,i,&val);

                j++;
            } else {
                printk(KERN_ERR "DMSDOS: BITFAT mismatches MDFAT (sector %d is %d and should be %d)\n",
                       i,dbl_bitfat_value(sb,i,NULL),(unsigned)get_bits(sect_array,i));
                j++;

                if (j==MAXMSG) {
                    vfree(sect_array);
                    printk(KERN_ERR "DMSDOS: Too many BITFAT mismatches in CVF, check aborted.\n");
                    return -3;
                }
            }
        }
    }

    if (bitfat_dirty) {
        printk(KERN_INFO "DMSDOS: Updating BITFAT finished\n");
        stac_bitfat_state(sb,2);
    };

    if ((dblsb->s_free_sectors!=-1)&&
            (dblsb->s_free_sectors!=free_sects)) {
        printk(KERN_INFO "DMSDOS: adapting free sectors count\n");
    }

    dblsb->s_free_sectors=free_sects;

    printk(KERN_INFO "DMSDOS: Sumary: Free sectors = %d\n",free_sects);
    printk(KERN_INFO "DMSDOS: Sumary: Deleted clusters = %d\n",deleted_clusts);

    vfree(sect_array);

    if (j!=0&&repair==0) { return -3; }

    return 0;
}
#endif

/* simple fs check routines (DON'T mount obviously damaged filesystems rw)
*/
int simple_check(struct super_block *sb,int repair)
{
    /* unsigned char field[512*1024]; grr... panics (stack overflow) */
    unsigned char *field;
    unsigned char bits[8]= {1,2,4,8,16,32,64,128};
    int i,val;
    Mdfat_entry mde,dummy;
    Dblsb *dblsb=MSDOS_SB(sb)->private_data;
    int mdfat_dead_msg_count=0;
    int maxmsg=0;
    int errorcode=0;
#ifdef DMSDOS_CONFIG_DBL
    int free_sects;
    int j;
#endif

    /* can't repair ro filesystems */
    if (dblsb->s_comp==READ_ONLY) { repair=0; }

#define setbit(nr) field[nr/8]|=bits[nr%8]
#define getbit(nr) (field[nr/8]&bits[nr%8])

    /* check fat */

    /* get memory for field */
    val=dblsb->s_max_cluster2/8 + 1;
    field=(unsigned char *)vmalloc(val);

    if (field==NULL) {
        printk(KERN_WARNING "DMSDOS: simple_check aborted (no memory)\n");
        return 1;
    }

    for (i=0; i<val; ++i) { field[i]=0; }

    for (i=2; i<=dblsb->s_max_cluster2&&maxmsg<=MAXMSG; ++i) {
        val=dbl_fat_nextcluster(sb,i,NULL);
        dbl_mdfat_value(sb,i,NULL,&mde);

        if (val!=0&&val!=-1) {
            if (getbit(val)) {
                printk(KERN_ERR "DMSDOS: FAT crosslink or loop in CVF detected (cluster %d), giving up.\n",
                       i);
                ++maxmsg;
                repair=0; /* unable to fix this - refuse to do further repair */
                errorcode=-1;
                break;  /* we cannot continue here */
            }

            setbit(val);
        }

        if (val==0&&(mde.flags&2)!=0) {
            if (repair==0) {
                if (dblsb->s_cvf_version<STAC3) {
                    printk(KERN_ERR "DMSDOS: MDFAT-level dead sectors found in CVF (cluster %d)\n",
                           i);
                    ++maxmsg;
                    errorcode=-2;
                }
            } else {
                if (mdfat_dead_msg_count++==0) { /* print message only once */
                    if (dblsb->s_cvf_version<STAC3) {
                        printk(KERN_NOTICE "DMSDOS: MDFAT-level dead sectors found, removing...\n");
                    } else {
                        printk(KERN_INFO "DMSDOS: Deleted clusters found, removing...\n");
                    }
                }

                mde.flags=0;
                mde.size_lo_minus_1=0;
                mde.size_hi_minus_1=0;
                mde.sector_minus_1=(dblsb->s_cvf_version<STAC3)?0:-1;
                dbl_mdfat_value(sb,i,&mde,&dummy);
            }
        }
    }

    vfree(field);

    if (maxmsg>MAXMSG) {
        printk(KERN_ERR "DMSDOS: giving up after %d errors. There may be more errors.\n",
               maxmsg);
    }

    if (errorcode) {
        printk(KERN_ERR "DMSDOS: part 1 of filesystem check failed, aborting.\n");
        return errorcode;
    }

#ifdef DMSDOS_CONFIG_STAC

    if (dblsb->s_cvf_version>=STAC3) {
        return stac_simple_check(sb,repair);
    }

#endif

#ifdef DMSDOS_CONFIG_DBL
    /* check mdfat */

    val=dblsb->s_dataend/8 + 1;
    field=(unsigned char *)vmalloc(val);

    if (field==NULL) {
        printk(KERN_WARNING "DMSDOS: simple_check: MDFAT+BITFAT test skipped (no memory)\n");
        return 2;
    }

    for (i=0; i<val; ++i) { field[i]=0; }

    for (i=2; i<=dblsb->s_max_cluster2&&maxmsg<=MAXMSG; ++i) {
        dbl_mdfat_value(sb,i,NULL,&mde);

        if (mde.flags&2) { /* 'used' bit set */
            val=mde.sector_minus_1;

            if (val+mde.size_lo_minus_1>=dblsb->s_dataend||
                    val+1<dblsb->s_datastart) {
                printk(KERN_ERR "DMSDOS: MDFAT entry invalid in CVF (cluster %d)\n",
                       i);
                ++maxmsg;
                repair=0; /* refuse to repair */
                errorcode=-2;
            } else
                for (j=0; j<=mde.size_lo_minus_1; ++j) {
                    ++val;

                    if (getbit(val)) {
                        printk(KERN_ERR "DMSDOS: MDFAT crosslink in CVF detected (cluster %d)\n",
                               i);
                        ++maxmsg;
                        repair=0;
                        errorcode=-2;
                    }

                    setbit(val);
                }

#ifdef DMSDOS_CONFIG_DRVSP3

            /* check fragmented clusters */
            if (mde.unknown&2) {
                /* cluster is fragmented */
                int fragcount;
                int fragpnt;
                int sector_minus_1;
                int seccount_minus_1;
                struct buffer_head *bh;

                bh=raw_bread(sb,mde.sector_minus_1+1);

                if (bh==NULL) {
                    printk(KERN_ERR "DMSDOS: unable to read fragmentation list of cluster %d.\n",
                           i);
                    ++maxmsg;
                    repair=0;
                    errorcode=-2;
                } else {
                    fragcount=bh->b_data[0];

                    if (bh->b_data[1]!=0||bh->b_data[2]!=0||bh->b_data[3]!=0
                            ||fragcount<=0||fragcount>dblsb->s_sectperclust) {
                        printk(KERN_ERR "DMSDOS: illegal fragcount in cluster %d\n",i);
                        ++maxmsg;
                        repair=0;
                        errorcode=-2;
                        raw_brelse(sb,bh);
                    } else { /* read list and scan all fragments */
                        for (fragpnt=1; fragpnt<=fragcount; ++fragpnt) {
                            sector_minus_1=bh->b_data[fragpnt*4+0];
                            sector_minus_1&=0xff;
                            sector_minus_1+=bh->b_data[fragpnt*4+1]<<8;
                            sector_minus_1&=0xffff;
                            sector_minus_1+=bh->b_data[fragpnt*4+2]<<16;
                            sector_minus_1&=0xffffff;
                            seccount_minus_1=bh->b_data[fragpnt*4+3];
                            seccount_minus_1&=0xff;
                            seccount_minus_1/=4;

                            /* test range */
                            val=sector_minus_1;

                            if (val+seccount_minus_1>=dblsb->s_dataend||
                                    val+1<dblsb->s_datastart) {
                                printk(KERN_ERR "DMSDOS: MDFAT entry invalid in CVF (fragmented cluster %d fragpnt %d)\n",
                                       i,fragpnt);
                                ++maxmsg;
                                repair=0; /* refuse to repair */
                                errorcode=-2;
                                break;
                            }

                            if (fragpnt==1) {
                                /* first is the sector itself */
                                if (sector_minus_1!=mde.sector_minus_1
                                        ||seccount_minus_1!=mde.size_lo_minus_1) {
                                    printk(KERN_ERR "DMSDOS: fraglist!=mde cluster %d sector %d!=%ld or count %d!=%d\n",
                                           i,sector_minus_1+1,mde.sector_minus_1+1,
                                           seccount_minus_1+1,mde.size_lo_minus_1);
                                    ++maxmsg;
                                    repair=0;
                                    errorcode=-2;
                                }
                            } else for (j=0; j<=seccount_minus_1; ++j) {
                                    ++val;

                                    if (getbit(val)) {
                                        printk(KERN_ERR "DMSDOS: MDFAT crosslink in CVF detected (cluster %d)\n",
                                               i);
                                        ++maxmsg;
                                        repair=0;
                                        errorcode=-2;
                                    }

                                    setbit(val);
                                }
                        }

                        raw_brelse(sb,bh);
                    }
                }
            } /* end check fragmented cluster */

#endif

        }

        /* Hmmm... this doesn't seem to be an error... dos tolerates this...
            else / 'used' bit NOT set /
            { if(mde.sector_minus_1!=0||mde.size_lo_minus_1!=0||mde.size_hi_minus_1!=0)
              { printk(KERN_NOTICE "DMSDOS: non-zero unused(?) MDFAT entry in cluster %d\n",
                       i);
                ++maxmsg;
                repair=0;
                errorcode=-2;
              }
            }
        */
        /* Hmmm... unknown bits seem to contain nothing useful but are sometimes set...
            if(mde.unknown) / treat unknown cases as error unless we know it better /
            { printk(KERN_NOTICE "DMSDOS: unknown bits set to %d in cluster %d\n",
                     mde.unknown,i);
              ++maxmsg;
              repair=0;
              errorcode=-2;
            }
        */
    }

    if (maxmsg>MAXMSG) {
        printk(KERN_ERR "DMSDOS: giving up after %d errors. There may be more errors.\n",
               maxmsg);
    }

    if (errorcode) {
        vfree(field);
        printk(KERN_ERR "DMSDOS: part 2 of filesystem check failed, aborting.\n");
        return errorcode;
    }

    /* check bitfat */

    /* dataend-1 problem corrected above - dmsdos doesn't touch the
       last sector now because it seems to be reserved... */

    j=0; /* count BITFAT mismatches */
    free_sects=0; /* count free sectors */

    for (i=dblsb->s_datastart; i<=dblsb->s_dataend; ++i) {
        val=dbl_bitfat_value(sb,i,NULL);

        if (val==0) { ++free_sects; }

        if (val!=(getbit(i)?1:0)) {
            if (repair) {
                int newval;

                if (j==0) { printk(KERN_NOTICE "DMSDOS: BITFAT mismatches MDFAT, repairing...\n"); }

                newval=(getbit(i)?1:0);
                dbl_bitfat_value(sb,i,&newval);
                ++j;
            } else {
                printk(KERN_ERR "DMSDOS: BITFAT mismatches MDFAT (sector %d)\n",
                       i);
                ++j;

                if (j==MAXMSG) {
                    vfree(field);
                    printk(KERN_ERR "DMSDOS: Too many BITFAT mismatches, check aborted.\n");
                    return -3;
                }
            }

        }
    }

    vfree(field);

    if (j!=0&&repair==0) { return -3; }

    dblsb->s_free_sectors=free_sects;
    printk(KERN_INFO "DMSDOS: free sectors=%d\n",dblsb->s_free_sectors);
#endif

    return 0;
}

