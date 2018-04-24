/* 
dblspace_alloc.c

DMSDOS CVF-FAT module: memory and sector allocation functions

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

#ifdef __KERNEL__                    
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/msdos_fs.h>
#include <linux/string.h>
#include <linux/sched.h>
#endif

#include "dmsdos.h"

#ifdef __DMSDOS_LIB__
/* some interface hacks */
#include"lib_interface.h"
#include<malloc.h>
#include<errno.h>
#endif

extern unsigned long dmsdos_speedup;

#define NEAR_AREA 512
/* no change for doublespace, but fixes drivespace 3 problems (was 50) */
#define BIG_HOLE (dblsb->s_sectperclust*3+2) 

/* experimental new memory allocation routines */
#if defined(USE_XMALLOC) && !defined(__DMSDOS_LIB__)

int told=0;

#include<linux/mm.h>
#include<linux/malloc.h>

#ifndef MAX_KMALLOC_SIZE
#define MAX_KMALLOC_SIZE 128*1024
#endif

void* xmalloc(unsigned long size)
{ void* result;

  if(size<=MAX_KMALLOC_SIZE)
  { result=kmalloc(size,GFP_KERNEL);
    if(result)
    { /* the xfree routine recognizes kmalloc'd memory due to that it is
         usually not page-aligned --  we double-check here to be sure */
      if (  ( ((unsigned long)result) & (PAGE_SIZE-1) ) ==0   )
      { /* uhhh.... the trick is broken... 
           tell the user and go safe using vmalloc */
        kfree(result);
        if(told++)printk(KERN_ERR "DMSDOS: page-aligned memory returned by kmalloc - please disable XMALLOC\n"); 
      }
      else return result;
    }
  }
  result=vmalloc(size);
  /* again check alignment to be 100% sure */
  if ( ((unsigned long)result) & (PAGE_SIZE-1) )
    panic("DMSDOS: vmalloc returned unaligned memory - please disable XMALLOC\n"); 
  return result;
}

void xfree(void * data)
{ unsigned long address=(unsigned long)data;

  /* we rely on the fact that kmalloc'ed memory is unaligned but
     vmalloc'ed memory is page-aligned */
  /* we are causing SERIOUS kernel problems if the wrong routine is called -
     therefore xmalloc tests kmalloc's return address above */
  if(address&(PAGE_SIZE-1))kfree(data);
  else vfree(data);
}
#endif

#ifdef __DMSDOS_LIB__
/* we don't need locking in the library */
void lock_mdfat_alloc(Dblsb*dblsb) {}
void unlock_mdfat_alloc(Dblsb*dblsb) {}
#endif

#ifdef __KERNEL__
void lock_mdfat_alloc(Dblsb*dblsb) 
{ struct semaphore*sem;

  sem=dblsb->mdfat_alloc_semp;
  down(sem);
}
void unlock_mdfat_alloc(Dblsb*dblsb)
{ struct semaphore*sem;

  sem=dblsb->mdfat_alloc_semp;
  up(sem);
}
#endif

#ifdef DMSDOS_CONFIG_DBL
void u_free_cluster_sectors(struct super_block*sb, int clusternr,
                            unsigned long* undo_list)
{ 
  Mdfat_entry mde,dummy,newmde;
  int newval=0;
  int i;
  int sectors;
  int sectornr;
  int undo_pnt=0;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
  
  LOG_ALLOC("DMSDOS: free_cluster_sectors: freeing cluster %d\n",clusternr);

  /* read mdfat entry and clear */    
  newmde.sector_minus_1=0;
  newmde.size_lo_minus_1=0;
  newmde.size_hi_minus_1=0;
  newmde.flags=0;
  dbl_mdfat_value(sb,clusternr,NULL,&mde);
  dbl_mdfat_value(sb,clusternr,&newmde,&dummy);
  sectors=mde.size_lo_minus_1+1;
  sectornr=mde.sector_minus_1+1;
  if(mde.flags&2)
  { if(mde.unknown&2)
    { /* free fragmented cluster */
      struct buffer_head*bh;
      int fragcount;
      int fragpnt;
      int sec;
      int cnt;
      
      bh=raw_bread(sb,sectornr);
      if(bh==NULL)
      { printk(KERN_ERR "DMSDOS: free_cluster_sectors: fragmentation list unreadable in cluster %d\n",
               clusternr);
        goto nfree;
      }
      fragcount=bh->b_data[0];
      if(fragcount<=0||fragcount>dblsb->s_sectperclust||
         bh->b_data[1]!=0||bh->b_data[2]!=0||bh->b_data[3]!=0)
      { printk(KERN_ERR "DMSDOS: free_cluster_sectors: error in fragmentation list in cluster %d\n",
               clusternr);
        raw_brelse(sb,bh);
        goto nfree;
      }
      for(fragpnt=1;fragpnt<=fragcount;++fragpnt)
      { cnt=bh->b_data[fragpnt*4+3];
        cnt&=0xff;
        cnt/=4;
        cnt+=1;
        sec=bh->b_data[fragpnt*4];
        sec&=0xff;
        sec+=bh->b_data[fragpnt*4+1]<<8;
        sec&=0xffff;
        sec+=bh->b_data[fragpnt*4+2]<<16;
        sec&=0xffffff;
        sec+=1;
        
        if(fragpnt==1)
        { if(sec!=sectornr||cnt!=sectors)
          { printk(KERN_ERR "DMSDOS: free_cluster_sectors: first fragment wrong in cluster %d\n",
                   clusternr);
            raw_brelse(sb,bh);
            goto nfree;
          }
        }
        
        for(i=0;i<cnt;++i)
        { dbl_bitfat_value(sb,sec+i,&newval);
          if(undo_list)undo_list[undo_pnt++]=sec+i;
        }
      }
    }
    else
    { /* free sectors in bitfat */
     nfree:
      for(i=0;i<sectors;++i)
      { dbl_bitfat_value(sb,sectornr+i,&newval);
        if(undo_list)undo_list[undo_pnt++]=sectornr+i;
      }
    }
    
    dblsb->s_full=0;
  }
  else
  { LOG_CLUST("DMSDOS: stale MDFAT entry for cluster %d, zeroing.\n",
           clusternr);
  }
  
  if(undo_list)undo_list[undo_pnt]=0;
}

void free_cluster_sectors(struct super_block*sb, int clusternr)
{ lock_mdfat_alloc(MSDOS_SB(sb)->private_data);
  u_free_cluster_sectors(sb,clusternr,NULL);
  unlock_mdfat_alloc(MSDOS_SB(sb)->private_data);
}
#endif

/* for statistics - just for interest */
int nearfound=0;
int bigfound=0;
int exactfound=0;
int anyfound=0;
int notfound=0;
int fragfound=0;

/* this function must be called locked */
int find_free_bitfat(struct super_block*sb, int sectornr, int size)
{ int testsek;
  int i;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
  
  if(dblsb->s_free_sectors<0||dblsb->s_free_sectors>0x2000000)
  { printk(KERN_NOTICE "DMSDOS: find_free_bitfat: free sectors=%d, cannot believe this. Counting...\n",
           dblsb->s_free_sectors);
    check_free_sectors(sb);
    printk(KERN_NOTICE "DMSDOS: counted free sectors=%d\n",dblsb->s_free_sectors);
  }
  
  /* we needn't try in that case... */
  if(dblsb->s_free_sectors<size)
  { if(dblsb->s_full<2)printk(KERN_CRIT "DMSDOS: CVF full.\n");
    dblsb->s_full=2;
    return -ENOSPC;
  }

if(dmsdos_speedup&SP_FAST_BITFAT_ALLOC)
{ /* new strategy: find any fitting hole beginning with last result */

  testsek=dblsb->s_lastnear;
  if(testsek<dblsb->s_datastart||testsek>dblsb->s_dataend-size)
    testsek=dblsb->s_datastart;
    
  while(testsek<=dblsb->s_dataend-size)
  { if(dbl_bitfat_value(sb,testsek,NULL))
    { ++testsek;
      continue;
    }
    i=1;
    while(i<size&&dbl_bitfat_value(sb,testsek+i,NULL)==0)++i;
    if(i==size)
    { ++nearfound;
      dblsb->s_lastnear=testsek+size;
      dblsb->s_full=0;
      return testsek;
    }
    testsek+=i;
  }
  
  /* not found, continue */
  goto tryany;
  
} /* end of new strategy */
else
{ /* old strategy: do a search in near environment first */

  if(sectornr==0)sectornr=dblsb->s_lastnear;

  if(sectornr>=dblsb->s_datastart&&sectornr<=dblsb->s_dataend-size)
  { /* search exactly fitting hole near sectornr */
    testsek=sectornr;  
    while(testsek<sectornr+NEAR_AREA)
    { if(dbl_bitfat_value(sb,testsek,NULL))
      { ++testsek;
        continue;
      }
      i=1;
      while(i<=size&&dbl_bitfat_value(sb,testsek+i,NULL)==0)++i;
      if(i==size)
      { dblsb->s_full=0;
        ++nearfound;
        dblsb->s_lastnear=testsek; 
        return testsek;
      }
      testsek+=i;
    }
    testsek=sectornr;
    while(testsek>sectornr-NEAR_AREA)
    { if(dbl_bitfat_value(sb,testsek,NULL))
      { --testsek;
        continue;
      }
      i=1;
      while(i<=size&&dbl_bitfat_value(sb,testsek-i,NULL)==0)++i;
      if(i==size)
      { dblsb->s_full=0;
        ++nearfound;
        dblsb->s_lastnear=testsek-i+1;
        return testsek-i+1;
      }
      testsek-=i;
    }
  }
  /* not found, continue */
} /* end of old strategy */

  /* search for a big hole */
  if(dblsb->s_lastbig==-1)goto nobighole;
  
  testsek=dblsb->s_lastbig;
  if(testsek<dblsb->s_datastart||testsek+size>dblsb->s_dataend)
    testsek=dblsb->s_datastart;
    
  while(testsek<=dblsb->s_dataend-size)
  { if(dbl_bitfat_value(sb,testsek,NULL))
    { ++testsek;
      continue;
    }
    i=1;
    while(i<BIG_HOLE&&dbl_bitfat_value(sb,testsek+i,NULL)==0)++i;
    if(i==BIG_HOLE)
    { dblsb->s_full=0;
      ++bigfound;
      dblsb->s_lastbig=testsek;
      return testsek;
    }
    testsek+=i;
  }
  
  if(dblsb->s_lastbig==0)
     dblsb->s_lastbig=-1; /*there's no big hole any more*/
  else
     dblsb->s_lastbig=0; /* next time try from the beginning */

nobighole:

if((dmsdos_speedup&SP_NO_EXACT_SEARCH)==0)  
{
  /* search for an exactly fitting hole */
  /* hmmm... now the search code becomes awfully slow */
  testsek=dblsb->s_datastart;
  while(testsek<=dblsb->s_dataend-size)
  { if(dbl_bitfat_value(sb,testsek,NULL))
    { ++testsek;
      continue;
    }
    i=1;
    while(i<=size&&dbl_bitfat_value(sb,testsek+i,NULL)==0)++i;
    if(i==size)
    { dblsb->s_full=0;
      ++exactfound;
      return testsek;
    }
    testsek+=i;
  }
}
  
  if(dblsb->s_full==0)
  { printk(KERN_CRIT "DMSDOS: CVF almost full or highly fragmented at MDFAT level.\n");
    dblsb->s_full=1;
  }

tryany:
  /* last trial: search for any hole >= size */
  testsek=dblsb->s_datastart;
  while(testsek<=dblsb->s_dataend-size)
  { if(dbl_bitfat_value(sb,testsek,NULL))
    { ++testsek;
      continue;
    }
    i=1;
    while(i<size&&dbl_bitfat_value(sb,testsek+i,NULL)==0)++i;
    if(i==size)
    { ++anyfound;
      return testsek;
    }
    testsek+=i;
  }
  
  /* not found, means disk full or MDFAT too fragmented */
  ++notfound;
  
  if(dblsb->s_cvf_version==DRVSP3)
  { if(dblsb->s_full==0)
    { printk(KERN_CRIT "DMSDOS: CVF almost full or highly fragmented at MDFAT level.\n");
      dblsb->s_full=1;
    }
  }
  else /* this is for CVFs that cannot fragment cluster data */
  { if(dblsb->s_full<2)
      printk(KERN_CRIT "DMSDOS: CVF full or too fragmented at MDFAT level.\n");
    dblsb->s_full=2;
  }
  return 0;
}

void log_found_statistics()
{ printk(KERN_INFO "DMSDOS: free sector finding statistics:\n");
  printk(KERN_INFO "nearfound=%d bigfound=%d exactfound=%d anyfound=%d fragfound=%d notfound=%d\n",
         nearfound,bigfound,exactfound,anyfound,fragfound,notfound);
}

#ifdef DMSDOS_CONFIG_DRVSP3
int try_fragmented(struct super_block*sb,int near,int nr,
                   unsigned char*fraglist)
{ 
  int i;
  int sector=near;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
  int again=1;
  int frags;
  int cnt;

  /* if you never want dmsdos to write fragmented clusters as a last resort
     then uncomment the next return statement */
     
  /* return -ENOSPC; */

  if(dblsb->s_free_sectors<nr)
  { if(dblsb->s_full<2)printk(KERN_CRIT "DMSDOS: CVF full.\n");
    dblsb->s_full=2;
    return -ENOSPC;
  }

  printk(KERN_DEBUG "DMSDOS: trying to allocate fragmented space...\n");  
  LOG_ALLOC("DMSDOS: try_fragmented: start, near=%d nr=%d\n",near,nr);
  
  if(near==0)sector=dblsb->s_lastnear;
  
  if(sector<dblsb->s_datastart||sector>dblsb->s_dataend)
  { sector=dblsb->s_datastart;
    again=0;
  }
  
 retry:
  frags=0;
  fraglist[0]=0;
  fraglist[1]=0;
  fraglist[2]=0;
  fraglist[3]=0;
  cnt=nr;
  
  while(cnt>0&&sector<=dblsb->s_dataend)
  { if(dbl_bitfat_value(sb,sector,NULL))
    { ++sector;
        continue;
    }
    /* free sector found */
    i=1;
    while(dbl_bitfat_value(sb,sector+i,NULL)==0&&i<cnt)++i;
    /* i=number of free sectors :) */
    ++frags;
    fraglist[frags*4]=sector-1;
    fraglist[frags*4+1]=(sector-1)>>8;
    fraglist[frags*4+2]=(sector-1)>>16;
    fraglist[frags*4+3]=(sector-1)>>24;
    fraglist[frags*4+3]|=(i-1)<<2;
    fraglist[0]=frags;
    sector+=i+1;
    cnt-=i;
  }
  if(cnt>0&&again!=0)
  { sector=dblsb->s_datastart;
    again=0;
    goto retry;
  }
  
  /* now evaluate the result, check for strange things */
  if(cnt>0)
  { if(dblsb->s_full<2)
      printk(KERN_CRIT "DMSDOS: CVF full (cannot even allocate fragmented space)\n");
    dblsb->s_full=2;
    return -ENOSPC;
  }
  if(cnt<0)
  { printk(KERN_ERR "DMSDOS: try_fragmented: cnt<0 ? This is a bug.\n");
    return -EIO;
  }
  if(frags<2||frags>dblsb->s_sectperclust+1)
  { printk(KERN_ERR "DMSDOS: try_fragmented: frags=%d ? Cannot happen.\n",frags);
    return -EIO;
  }
  
  /* correct statistics */
  ++fragfound;--notfound; 
  dblsb->s_lastnear=sector;
  dblsb->s_full=1; /* uhh... 0 might be dangerous... */
  
  /* fraglist must be written to disk in *any* case in order to
     still represent a correct filesystem
     this is handled by dbl_write_cluster to prevent too much overhead */
     
  LOG_ALLOC("DMSDOS: try_fragmented: success, frags=%d\n",frags);
  return 0;
}
#endif /* DMSDOS_CONFIG_DRVSP3 */

#ifdef DMSDOS_CONFIG_DBL
/* replaces an existing cluster;
   this unusual function must be called before rewriting any file cluster;
   *** size must be known (encoded in mde) ***
   if fraglist!=NULL fragmented clusters are allowed for drivespace 3
   returns first sector nr
   changes mde and fraglist
*/

#define MAX_UNDO_LIST 70

int dbl_replace_existing_cluster(struct super_block*sb, int cluster, 
                             int near_sector,
                             Mdfat_entry*mde,
                             unsigned char*fraglist)
{ Mdfat_entry old_mde,dummy;
  int i;
  int newval;
  int sector;
  int old_sector;
  int old_size;
  int new_size;
  unsigned long undo_list[MAX_UNDO_LIST];
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;

  lock_mdfat_alloc(dblsb);

  LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster cluster=%d near_sector=%d\n",
         cluster,near_sector);
  dbl_mdfat_value(sb,cluster,NULL,&old_mde);
  old_size=old_mde.size_lo_minus_1+1;
  old_sector=old_mde.sector_minus_1+1;
  new_size=mde->size_lo_minus_1+1;
  mde->unknown=0; /* ensure fragmented bit is clear */ 
  if(old_mde.flags&2)
  {
    /* test whether same length (and not fragmented in drivespace 3) */
    if(old_size==new_size&&
       (dblsb->s_cvf_version!=DRVSP3||(old_mde.unknown&2)==0)
      )
    { LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: same length, ok\n");
      sector=old_sector;
      goto mdfat_update;
    }
    if(dblsb->s_cvf_version==DRVSP3&&(old_mde.unknown&2)!=0&&fraglist!=NULL)
    { /*old cluster is fragmented and new *is allowed* to be fragmentd */
      struct buffer_head*bh=raw_bread(sb,old_sector);
      if(bh)
      { int fragcount;
        int cnt;
        int sec;
        int fragpnt;
        int sects;
        int m_cnt=0;
        int m_sec=0;
        
        fragcount=bh->b_data[0];
        sects=0;
        if(fragcount<2||fragcount>dblsb->s_sectperclust+1||
           bh->b_data[1]!=0||bh->b_data[2]!=0||bh->b_data[3]!=0
          )
        { raw_brelse(sb,bh);
          goto check_failed;
        }
        
        for(fragpnt=1;fragpnt<=fragcount;++fragpnt)
        { cnt=bh->b_data[fragpnt*4+3];
          cnt&=0xff;
          cnt/=4;
          cnt+=1;
          sec=bh->b_data[fragpnt*4];
          sec&=0xff;
          sec+=bh->b_data[fragpnt*4+1]<<8;
          sec&=0xffff;
          sec+=bh->b_data[fragpnt*4+2]<<16;
          sec&=0xffffff;
          sec+=1;
        
          if(fragpnt==1)
          { m_cnt=cnt;
            m_sec=sec;
            if(sec!=old_mde.sector_minus_1+1||cnt!=old_mde.size_lo_minus_1+1)
            { printk(KERN_ERR "DMSDOS: dbl_replace_existing_cluster: checking old fraglist: first fragment wrong in cluster %d\n",
                     cluster);
              raw_brelse(sb,bh);
              goto check_failed;
            }
          }
          
          sects+=cnt;
        }
        raw_brelse(sb,bh);
        if(sects-1/*subtract space for fraglist*/==new_size)
        { /* we can reuse it */
          memcpy(fraglist,bh->b_data,4*(fragcount+1));
          mde->unknown|=2;
          mde->size_lo_minus_1=m_cnt-1;
          sector=m_sec;
          LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: same fragmented size, ok.\n");
          goto mdfat_update;
        }
       check_failed:
        /* fall through */ 
      }
    }
    /* different length, replace mdfat entry */
    newval=0;
    LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: freeing old sectors...\n");
    u_free_cluster_sectors(sb,cluster,undo_list);
    LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: freeing finished\n");
  }
  LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: call find_free_bitfat...\n");
  sector=find_free_bitfat(sb,near_sector,new_size);
  LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: find_free_bitfat returned %d\n",
         sector);
  if(sector<=0)
  { 
#ifdef DMSDOS_CONFIG_DRVSP3
    if(dblsb->s_cvf_version==DRVSP3&&fraglist!=NULL
       &&(dmsdos_speedup&SP_NO_FRAG_WRITE)==0)
    { i=try_fragmented(sb,near_sector,new_size+1,fraglist);/*yes one sector more*/
      if(i==0) /* success */
      { /* scan fraglist */
        int frags;
        int seccount;
        int usector;
        int j;
        
        frags=fraglist[0];
        for(i=1;i<=frags;++i)
        { seccount=fraglist[i*4+3];
          seccount&=0xff;
          seccount/=4;
          seccount+=1;
          usector=fraglist[i*4];
          usector&=0xff;
          usector+=fraglist[i*4+1]<<8;
          usector&=0xffff;
          usector+=fraglist[i*4+2]<<16;
          usector&=0xffffff;
          usector+=1;
          
          if(i==1) /* note values for mdfat */
          { mde->size_lo_minus_1=seccount-1;
            sector=usector;
          }
          
          /* allocate in bitfat */
          newval=1;
          for(j=0;j<seccount;++j)
          { /* check whether sectors are really free */
            if(dbl_bitfat_value(sb,usector+j,NULL))
            { printk(KERN_EMERG "DMSDOS: try_fragmented returned non-free sectors!\n");
              /* WARNING: bitfat is corrupt now */
              unlock_mdfat_alloc(dblsb);
              panic("DMSDOS: dbl_replace_existing_cluster: This is a bug - reboot and check filesystem\n");
              return -EIO;
            }
            dbl_bitfat_value(sb,usector+j,&newval);
          }
        }
        mde->unknown|=2; /* set fragmented bit */
        goto mdfat_update;
      }
      /* try_fragmented failed: fall through */
    }
#endif /* DMSDOS_CONFIG_DRVSP3 */
    if(old_mde.flags&2)
    { /* undo bitfat free */
      LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: undoing bitfat free...\n");
      newval=1;
      for(i=0;undo_list[i]!=0;++i)
               dbl_bitfat_value(sb,undo_list[i],&newval);
    }
    unlock_mdfat_alloc(dblsb);
    return -ENOSPC; /* disk full */
  }
  /* check whether really free (bug supposed in find_free_bitfat) */
  for(i=0;i<new_size;++i)
  { if(dbl_bitfat_value(sb,sector+i,NULL))
    { printk(KERN_EMERG "DMSDOS: find_free_bitfat returned sector %d size %d but they are not all free!\n",
             sector,new_size);
      unlock_mdfat_alloc(dblsb);
      panic("DMSDOS: dbl_replace_existing_cluster: This is a bug - reboot and check filesystem\n");
      return -EIO;
    }
  }
  newval=1;
  LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: allocating in bitfat...\n");
  for(i=0;i<new_size;++i)dbl_bitfat_value(sb,sector+i,&newval);
  
mdfat_update:
  mde->sector_minus_1=sector-1;
  mde->flags|=2;
  LOG_ALLOC("DMSDOS: dbl_replace_existing_cluster: writing mdfat...\n");
  dbl_mdfat_value(sb,cluster,mde,&dummy);
  unlock_mdfat_alloc(dblsb);
  return sector; /* okay */
}
#endif
