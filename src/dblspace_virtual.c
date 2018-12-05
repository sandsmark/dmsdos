/*
dblspace_virtual.c

DMSDOS CVF-FAT module: virtual buffer routines.

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

#ifndef __KERNEL__
#error This file needs __KERNEL__
#endif

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/semaphore.h>
#include "dmsdos.h"

/* Here we do a virtual sector translation */

#define FAKED_BH_MAGIC 0xfff0
/* i hope this never occurs in kernel buffers... */

extern unsigned long int dmsdos_speedup;

/* cluster caching ... */
Cluster_head ccache[CCACHESIZE];

DECLARE_MUTEX(ccache_sem);   /* Must be initialized to green light */
void lock_ccache(void) {down(&ccache_sem);}
void unlock_ccache(void) {up(&ccache_sem);}

DECLARE_WAIT_QUEUE_HEAD(fullwait);

int help_ccache=0;

/* must be called ccache locked */
/* function does not deal with locking */
Cluster_head* find_in_ccache(struct super_block*sb,
                             int clusternr,Cluster_head**lastfree,
                             Cluster_head**oldest)
{ int i;
  int lastfree_n=-1;
  int oldest_n=-1;
  unsigned int oldest_time=CURRENT_TIME;
  
  for(i=0;i<CCACHESIZE;++i)
  { if(ccache[i].c_flags==C_FREE)lastfree_n=i;
    else 
    { if(ccache[i].c_time<oldest_time&&ccache[i].c_count==0)
      { oldest_n=i;
        oldest_time=ccache[i].c_time;
      }
      if(ccache[i].c_clusternr==clusternr&&ccache[i].c_sb->s_dev==sb->s_dev)
      { /* found */
        return &(ccache[i]);
      }
    }
  }
  if(lastfree_n>=0&&lastfree!=NULL)*lastfree=&(ccache[lastfree_n]);
  /* uhhh, deadlock possible here... we should always have an oldest when
     there's nothing free any more... */
  /* use help_ccache to avoid always choosing the same cluster */
  if(lastfree_n<0&&oldest_n<0&&oldest!=NULL)
  { /* search more carefully */
    /* candidates are all those with count==0 */
    for(i=0;i<CCACHESIZE;++i)
    { ++help_ccache; if(help_ccache>=CCACHESIZE)help_ccache=0;
      if(ccache[help_ccache].c_count==0)
      { oldest_n=help_ccache;
        break;
      }
    }
  }
  if(oldest_n>=0&&oldest!=NULL)*oldest=&(ccache[oldest_n]);
  return NULL;
}

int count_error_clusters(void)
{ int i;
  int errs=0;
  
  for(i=0;i<CCACHESIZE;++i)
  { if(ccache[i].c_flags==C_DIRTY_NORMAL||ccache[i].c_flags==C_DIRTY_UNCOMPR)
      if(ccache[i].c_errors)++errs;
  }
  if(errs)printk("DMSDOS: %d error clusters waiting in cache\n",errs);
  return errs;
}

/* this is called if a cluster write fails too often */
/* the cluster is expected to be locked */
#define MAX_ERRORS 5
#define MAX_ERROR_CLUSTERS ((CCACHESIZE/10)+1)
void handle_error_cluster(Cluster_head*ch)
{ Dblsb*dblsb;

  if(ch->c_errors>MAX_ERRORS||count_error_clusters()>MAX_ERROR_CLUSTERS)
  { /* get rid of the cluster to prevent dmsdos crash due to endless loops */
    /* this shouldn't hurt much since the filesystem is already damaged */
    printk(KERN_CRIT "DMSDOS: dirty cluster %d on dev 0x%x removed, data are lost\n",
           ch->c_clusternr,ch->c_sb->s_dev);
    /*dmsdos_write_cluster(sb,NULL,0,clusternr,0,0); better not do this*/
    ch->c_flags=C_DELETED;
    ch->c_clusternr=-1;
    if((ch->c_sb->s_flags&MS_RDONLY)==0)
    { printk(KERN_CRIT "DMSDOS: filesystem on dev 0x%x probably damaged, set to READ-ONLY mode\n",
             ch->c_sb->s_dev);
      ch->c_sb->s_flags|=MS_RDONLY;
      dblsb=MSDOS_SB(ch->c_sb)->private_data;
      dblsb->s_comp=READ_ONLY;
    }
  }
  else
  { printk(KERN_CRIT "DMSDOS: cannot write dirty cluster %d on dev 0x%x c_error=%d, trying again later\n",
           ch->c_clusternr,ch->c_sb->s_dev,ch->c_errors);
  }
}

void lock_ch(Cluster_head*ch)
{ if(ch->c_count==0)panic("DMSDOS: lock_ch: count=0! This is a bug.\n");
  down(&(ch->c_sem));
}

void unlock_ch(Cluster_head*ch){up(&(ch->c_sem));}

void dump_ccache(void)
{ int i;

  printk(KERN_INFO "DMSDOS: ccache contents:\n");
  lock_ccache();
  for(i=0;i<CCACHESIZE;++i)
  { printk(KERN_INFO "slot %d: time=0x%x flags=",i,ccache[i].c_time);
    switch(ccache[i].c_flags)
    { case C_FREE: printk("FREE"); break;
      case C_VALID: printk("VALID"); break;
      case C_INVALID: printk("INVALID"); break;
      case C_DIRTY_NORMAL: printk("DIRTY_NORMAL"); break;
      case C_DIRTY_UNCOMPR: printk("DIRTY_UNCOMPR"); break;
      case C_DELETED: printk("DELETED"); break;
      case C_NOT_MALLOCD: printk("NOT_MALLOCD"); break;
      default: printk("unknown(?)");
    }
    printk(" count=%d length=%d clusternr=%d dev=0x%x errors=%d\n",
           ccache[i].c_count,ccache[i].c_length,ccache[i].c_clusternr,
           /* note that c_sb of a free slot may point to anywhere, so... */
           ccache[i].c_flags!=C_FREE?ccache[i].c_sb->s_dev:0,
           ccache[i].c_errors);
  }
  unlock_ccache();
}

/* get a (probably empty) ccache slot for cluster */
/* this function does not malloc memory or read the data */
/* the returned cluster is locked */
#define MAX_RETRIES 100 /* should just break the loop in case of ... */
Cluster_head* get_ch(struct super_block*sb,int clusternr)
{ Cluster_head*free=NULL;
  Cluster_head*oldest=NULL;
  Cluster_head*actual;
  int count_retries=0;
  struct super_block*sb2;
  
 retry:
  if(count_retries++>MAX_RETRIES)
  { printk(KERN_WARNING "DMSDOS: get_ch: max_retries reached, breaking loop. This may be a bug.\n");
    return NULL; 
  }
  
  lock_ccache();
  
  actual=find_in_ccache(sb,clusternr,&free,&oldest);
  
  /* the simplest case: we have found it in cache */
  if(actual)
  { ++(actual->c_count);
    actual->c_time=CURRENT_TIME;
    unlock_ccache();
    lock_ch(actual);
    if(actual->c_sb!=sb||actual->c_clusternr!=clusternr)
    { /* looks like some other process was faster and discarded it :( */
      printk(KERN_INFO "DMSDOS: get_ch: actual looks modified ARGHHH, retrying\n");
      unlock_ch(actual);
      lock_ccache();
      --(actual->c_count);
      unlock_ccache();
      goto retry;
    }
    return actual;
  }
  
  /* we have not found it, instead we found a free slot */
  if(free)
  { if(free->c_count!=0)
    { printk(KERN_WARNING "DMSDOS: get_ch: free->c_count!=0\n");
    }
    free->c_count=1;
    free->c_flags=C_NOT_MALLOCD;
    free->c_sb=sb;
    free->c_clusternr=clusternr;
    free->c_time=CURRENT_TIME;
    free->c_errors=0;
    unlock_ccache();
    lock_ch(free);
    if(free->c_sb!=sb||free->c_clusternr!=clusternr)
    { /* looks like some other process was faster and discarded it :( */
      printk(KERN_INFO "DMSDOS: get_ch: free looks modified ARGHHH, retrying\n");
      unlock_ch(free);
      lock_ccache();
      --(free->c_count);
      unlock_ccache();
      goto retry;
    }
    return free;
  }
  
  /* now the most complex case: we must discard one cluster */
  if(oldest)
  { if(oldest->c_count!=0)
    { printk(KERN_WARNING "DMSDOS: get_ch: oldest->c_count!=0\n");
    }
    oldest->c_count=1;
    /* is this the time touch really necessary here ? */
    /*oldest->c_time=CURRENT_TIME;*/
    unlock_ccache();
    lock_ch(oldest);
    switch(oldest->c_flags)
    { case C_DIRTY_NORMAL:
      case C_DIRTY_UNCOMPR:
        sb2=oldest->c_sb;
        if(dmsdos_write_cluster(sb2,oldest->c_data,oldest->c_length,
                  oldest->c_clusternr,0,
                  oldest->c_flags==C_DIRTY_NORMAL?UC_NORMAL:UC_UNCOMPR)<0)
          { /* write failed */
            oldest->c_time=CURRENT_TIME;
            ++(oldest->c_errors);
            handle_error_cluster(oldest);
            break;
          }
        /* successfully written */
        oldest->c_flags=C_VALID;
        oldest->c_errors=0;
        /* fall through */
      case C_VALID:
      case C_INVALID:
        if(oldest->c_count>1)break; /* we cannot do anything here */
        /* we are the only process using it */
        FREE(oldest->c_data);
        oldest->c_flags=C_NOT_MALLOCD;
        /* fall through */
      case C_NOT_MALLOCD:
        if(oldest->c_count>1)break;
        /* now we have a free slot */
        oldest->c_sb=sb;
        oldest->c_clusternr=clusternr;
        oldest->c_time=CURRENT_TIME;
        oldest->c_errors=0;
        return oldest;
    }
    unlock_ch(oldest);
    lock_ccache();
    --(oldest->c_count);
    unlock_ccache();
    /* anything may have happened meanwhile */
    goto retry;
  }
  
  /* the last case: not found, no one free and no one found to discard */
  /* all are in use - we must wait */
  unlock_ccache();
  LOG_CCACHE("DMSDOS: cluster cache full, waiting...\n");
  /* or may it be better to abort here ? */
  sleep_on(&fullwait);
  goto retry;
  
  return NULL;
}

/* this routine *must* return a cluster with its slack zerod out if the
   cluster doesn't have maximum length and flag C_NO_READ is unset */
Cluster_head* ch_read(struct super_block*sb,int clusternr,int flag)
{ Cluster_head*ch;
  Dblsb*dblsb;
  int i;

  LOG_CCACHE("DMSDOS: ch_read cluster %d\n",clusternr);
  ch=get_ch(sb,clusternr);
  if(ch==NULL)return NULL;
  /* get_ch returns a locked cluster slot */
  LOG_CCACHE("DMSDOS: ch_read: got count=%d\n",ch->c_count);
  dblsb=MSDOS_SB(ch->c_sb)->private_data;
  
  /* check whether we need to get memory */
  if(ch->c_flags==C_NOT_MALLOCD)
  { ch->c_data=MALLOC(dblsb->s_sectperclust*SECTOR_SIZE);
    if(ch->c_data==NULL)
    { printk(KERN_ERR "DMSDOS: ch_read: no memory!\n");
      unlock_ch(ch);
      ch_free(ch);
      return NULL;
    }
    else ch->c_flags=C_INVALID;
  }
  
  /* check whether we need to read the data from disk */
  if(ch->c_flags==C_INVALID)
  { ch->c_length=0;
    if((flag&C_NO_READ)==0)
    { i=dmsdos_read_cluster(sb,ch->c_data,clusternr);
      if(i<0)
      { printk(KERN_ERR "DMSDOS: ch_read: read_cluster failed\n");
        unlock_ch(ch);
        ch_free(ch);
        return NULL;
      }
      ch->c_length=i;
    }
    ch->c_flags=C_VALID;
  }
  
  /* check whether the lock is to be kept */
  if((flag&C_KEEP_LOCK)==0)unlock_ch(ch);
  
  LOG_CCACHE("DMSDOS: ch_read finished.\n");
  return ch;
}

int ch_dirty_locked(Cluster_head*ch, int near, int ucflag)
{    /* Hmm...
        We must ensure that we can safely delay writing of the cluster
        before. This is done by calling the write_cluster function with
        the SIMULATE flag. If there's a problem then it is expected to
        return an error code (return value<0). In that case we fall back to
        the old write-through caching mechanism.
     */
     /* IMPORTANT: This function expects that the cluster has already 
        been locked. If not, use ch_dirty intead. 
     */

  int i;
  struct super_block*sb=ch->c_sb;
  
  if(sb->s_flags&MS_RDONLY)
  { printk(KERN_ERR "DMSDOS: ch_dirty(_locked): READ-ONLY filesystem\n");
    unlock_ch(ch);
    return -EROFS; /*this might be bad, but...*/
  }
            
  LOG_CCACHE("DMSDOS: ch_dirty_locked cluster %d flags %d ucflag %d\n",
                ch->c_clusternr,ch->c_flags,ucflag);

  /* we expect the cluster to be already locked */
  /* this is not checked here */

  /* do not write free or deleted clusters or other junk */
  if(ch->c_flags==C_FREE||ch->c_flags==C_DELETED||ch->c_flags==C_INVALID
     ||ch->c_flags==C_NOT_MALLOCD)
  { unlock_ch(ch);
    return 0;
  }
     
  if(dmsdos_speedup&SP_USE_WRITE_BACK)
  {
     /* call write_cluster with SIMULATE flag (ucflag=UC_TEST) first */
     i=dmsdos_write_cluster(sb,ch->c_data,ch->c_length,ch->c_clusternr,
                                 near,UC_TEST);
     if(i>=0)
     { /* success - we can safely delay writing this cluster */
       ch->c_flags=ucflag!=UC_NORMAL?C_DIRTY_UNCOMPR:C_DIRTY_NORMAL;
       unlock_ch(ch);
       return i;
     }
     /* otherwise fall back to the default write-through code */
  }
     
  i=dmsdos_write_cluster(sb,ch->c_data,ch->c_length,ch->c_clusternr,
                                      near,ucflag);
  /* the cluster is *not* marked dirty since failure is *reported* to the
     calling process, which can call ch_dirty_retry_until_success if the
     data really *must* be written */
  /* so we also don't increment the error counter */
  unlock_ch(ch);
  return i;
}

int ch_dirty(Cluster_head*ch, int near, int ucflag)
{ /* same as ch_dirty_locked, but cluster is expected not to be locked */
  /* IMPORTANT: The cluster must not be locked by calling process as this 
     will cause a deadlock. If you have just locked the cluster, use 
     ch_dirty_locked instead. 
  */
  
  LOG_CCACHE("DMSDOS: ch_dirty cluster %d flags %d ucflag %d\n",
                ch->c_clusternr,ch->c_flags,ucflag);
                
  lock_ch(ch);
  /* ch_dirty_locked unlocks the cluster */
  return ch_dirty_locked(ch,near,ucflag);
}

/* This routine is called if extremely important data (directory structure)
   really must be written when normal ch_dirty already fails. However, there's
   no guarantee that the data can really be written some time later. This
   function is meant as a last resort. Code will discard the data later if too
   many errors occur and set the filesystem to read-only mode. */
void ch_dirty_retry_until_success(Cluster_head*ch, int near, int ucflag)
{ lock_ch(ch);
  if(ch->c_flags!=C_FREE&&ch->c_flags!=C_DELETED&&ch->c_flags!=C_INVALID
     &&ch->c_flags!=C_NOT_MALLOCD)
       ch->c_flags=ucflag!=UC_NORMAL?C_DIRTY_UNCOMPR:C_DIRTY_NORMAL;
  unlock_ch(ch);
}

void ch_free(Cluster_head*ch)
{ lock_ccache();
  --(ch->c_count);
  LOG_CCACHE("DMSDOS: ch_free cluster %d count_after_free %d\n",
             ch->c_clusternr,ch->c_count);
  if(ch->c_count==0)
  { /* throw away unused deleted clusters immediately */
    if(ch->c_flags==C_DELETED)
    { FREE(ch->c_data);
      ch->c_flags=C_FREE;
    }
    /* throw away unused and not malloc'd clusters also */
    if(ch->c_flags==C_NOT_MALLOCD)ch->c_flags=C_FREE;
    
    unlock_ccache();
    wake_up(&fullwait);
    return;
  }
  unlock_ccache();
}

void ccache_init()
{ int i;

  for(i=0;i<CCACHESIZE;++i)
  { ccache[i].c_flags=C_FREE;
    ccache[i].c_time=0;
    ccache[i].c_count=0;
    init_MUTEX(&ccache[i].c_sem);
    ccache[i].c_errors=0;
  }
}

/* for unmount */
/* we assume that the clusters of the device to be unmounted are
   - not in use (count==0) and
   - not locked
   either of them would mean that an inode of the filesystem is currently
   in use and thus the filesystem driver shouldn't allow unmount...
   ...violation will cause segfaults in a lot of other places...
*/
void free_ccache_dev(struct super_block*sb)
{ int i;

 retry:
  lock_ccache();
  for(i=0;i<CCACHESIZE;++i)
  { if(ccache[i].c_flags!=C_FREE&&ccache[i].c_sb->s_dev==sb->s_dev)
    { /* write it before if it is dirty... */
      if(ccache[i].c_flags==C_DIRTY_NORMAL||ccache[i].c_flags==C_DIRTY_UNCOMPR)
      { ++(ccache[i].c_count);
        unlock_ccache();
        lock_ch(&(ccache[i]));
        if(ccache[i].c_flags==C_DIRTY_NORMAL||
           ccache[i].c_flags==C_DIRTY_UNCOMPR)
        { /* we do not check for failure since we cannot do anything for it
            at unmount time (except panic, but this would be worse) */
          dmsdos_write_cluster(ccache[i].c_sb,ccache[i].c_data,
            ccache[i].c_length,ccache[i].c_clusternr,0,
            ccache[i].c_flags==C_DIRTY_NORMAL?UMOUNT_UCFLAG:UC_UNCOMPR);
          ccache[i].c_flags=C_VALID;
        }
        unlock_ch(&(ccache[i]));
        lock_ccache();
        --(ccache[i].c_count);
        unlock_ccache();
        
        /* I'm not sure whether we can be sure here, so just the safe way */
        goto retry;
      }

      if(ccache[i].c_count)printk(KERN_ERR "DMSDOS: free_ccache_dev: oh oh, freeing busy cluster...\n");
      if(ccache[i].c_flags!=C_NOT_MALLOCD)FREE(ccache[i].c_data);
      ccache[i].c_flags=C_FREE;
      wake_up(&fullwait);
    }
  }
  unlock_ccache();
}

/* for memory management */
/* We simply ignore clusters that are currently in use
   (count!=0). This should be safe now. */
void free_idle_ccache(void)
{ int i;

 retry:
  lock_ccache();
  for(i=0;i<CCACHESIZE;++i)
  { if(ccache[i].c_flags!=C_FREE&&ccache[i].c_count==0) /*those with count=0
                                                         are never locked */
    { /* hmm... would be a good idea to make it depend on free system
         memory whether to discard which cached clusters... and how many...
         any ideas? */
      if(CURRENT_TIME-ccache[i].c_time>MAX_CCACHE_TIME)
      { /* throw away */
        /* but write it before if it is dirty... */
        if(ccache[i].c_flags==C_DIRTY_NORMAL||ccache[i].c_flags==C_DIRTY_UNCOMPR)
        { ++(ccache[i].c_count);
          unlock_ccache();
          lock_ch(&(ccache[i]));
          if(ccache[i].c_flags==C_DIRTY_NORMAL||
             ccache[i].c_flags==C_DIRTY_UNCOMPR)
          { if(dmsdos_write_cluster(ccache[i].c_sb,ccache[i].c_data,
               ccache[i].c_length,ccache[i].c_clusternr,0,
               ccache[i].c_flags==C_DIRTY_NORMAL?UC_NORMAL:UC_UNCOMPR)>=0
              )
            { ccache[i].c_flags=C_VALID;
              ccache[i].c_errors=0;
            }
            else /* failed */
            { ccache[i].c_time=CURRENT_TIME; /* so we don't catch it again
                                                in retry */
              ++(ccache[i].c_errors);
              handle_error_cluster(&(ccache[i]));
            }
          }
          unlock_ch(&(ccache[i]));
          lock_ccache();
          --(ccache[i].c_count);
          unlock_ccache();
       
          /* I'm not sure whether we must restart here */   
          goto retry;
        }
        if(ccache[i].c_flags!=C_NOT_MALLOCD)FREE(ccache[i].c_data);
        ccache[i].c_flags=C_FREE;
        wake_up(&fullwait);
      }
    }
  }
  unlock_ccache();

  /* wake up forgotten fullwaits - for safety */
  wake_up(&fullwait);
}

/* for cluster cache syncing - writes all currently unused dirty clusters */
void sync_cluster_cache(int allow_daemon)
{ int i;
  struct super_block*sb;

  lock_ccache();
  
  for(i=0;i<CCACHESIZE;++i)
  { if(ccache[i].c_flags==C_DIRTY_NORMAL||ccache[i].c_flags==C_DIRTY_UNCOMPR)
    { sb=ccache[i].c_sb;
      ++(ccache[i].c_count);
      unlock_ccache();
      lock_ch(&(ccache[i]));
      if(ccache[i].c_flags==C_DIRTY_NORMAL
         ||ccache[i].c_flags==C_DIRTY_UNCOMPR)
      { if(dmsdos_write_cluster(sb,ccache[i].c_data,ccache[i].c_length,
              ccache[i].c_clusternr,0,
              ccache[i].c_flags==C_DIRTY_UNCOMPR?UC_UNCOMPR:
                  (allow_daemon?UC_NORMAL:UC_DIRECT)
                               ) >=0
          )
        { ccache[i].c_flags=C_VALID;
          ccache[i].c_errors=0;
        }
        else /* we cannot do much :( */
        { ++(ccache[i].c_errors);
          handle_error_cluster(&(ccache[i]));
        }
      }
      unlock_ch(&(ccache[i]));
      lock_ccache();
      --(ccache[i].c_count);
      
      /* we must not retry here since this causes an endless loop
         in case of a write error...
      unlock_ccache();      
      goto retry;
      */
    }
  }
      
  unlock_ccache();
}

/* cluster delete - deletes this cluster from the cache and cancels ch_dirty
   -- called when cluster is deleted in FAT */
void delete_cache_cluster(struct super_block*sb, int clusternr)
{ Cluster_head*ch;
  
  /* gets always a locked cluster and doesn't force read from disk */
  ch=get_ch(sb,clusternr);
  
  /* it is really necessary that get_ch never fails, but .... */
  if(ch==NULL)
  { printk(KERN_WARNING "DMSDOS: delete_cache_cluster: get_ch returned NULL\n");
    /* we can't do anything here, so just assume Murphy is not there :) */
    /* well of course, failed get_ch means the cluster is surely not in the
       cache */
    dmsdos_write_cluster(sb,NULL,0,clusternr,0,0);
    return;
  }

  /* inform the cluster handling routines so they can delete pointers in fs */
  dmsdos_write_cluster(sb,NULL,0,clusternr,0,0);
  
  /* if someone is using the cluster except us he has bad luck */
  if(ch->c_flags!=C_NOT_MALLOCD)ch->c_flags=C_DELETED;
  ch->c_clusternr=-1; /* :) */
  
  unlock_ch(ch);
  ch_free(ch);
}

/* for daemon write */
int daemon_write_cluster(struct super_block*sb,unsigned char*data,
                         int len, int clusternr, int rawlen)
{ Cluster_head*ch;
  int i;

  /* gets always a locked cluster slot and doesn't force read from disk */
  ch=get_ch(sb,clusternr);
  if(ch==NULL)
  { /* sorry */
    printk(KERN_ERR "DMSDOS: daemon_write_cluster: ch==NULL\n");
    return -EIO;
  }

  i=dmsdos_write_cluster(sb,data,len,clusternr,0,-rawlen);
  unlock_ch(ch);
  ch_free(ch);
  
  return i;
}

void log_ccache_statistics()
{ int j;
  int free;
  int valid;
  int invalid;
  int dirty_n;
  int dirty_u;
  int del;
  int n_mallocd;
  
  lock_ccache();
  
  printk(KERN_INFO "DMSDOS: ccache statistics:\n");
  
  free=0;
  valid=0;
  invalid=0;
  dirty_n=0;
  dirty_u=0;
  del=0;
  n_mallocd=0;
  
  for(j=0;j<CCACHESIZE;++j)
  { switch(ccache[j].c_flags)
    { case C_FREE: ++free; break;
      case C_VALID: ++valid; break;
      case C_INVALID: ++invalid; break;
      case C_DIRTY_NORMAL: ++dirty_n; break;
      case C_DIRTY_UNCOMPR: ++dirty_u; break;
      case C_DELETED: ++del; break;
      case C_NOT_MALLOCD: ++n_mallocd; break;
      default: printk(KERN_ERR "DMSDOS: log_ccache_statistics: cannot happen.\n");
    }
  }
  
  printk(KERN_INFO "sum: free=%d valid=%d invalid=%d dirty_n=%d dirty_u=%d del=%d n_mallocd=%d\n",
         free,valid,invalid,dirty_n,dirty_u,del,n_mallocd);
         
  unlock_ccache();
}

void get_memory_usage_ccache(int*used, int*max)
{ int i;
  int size=0;
  int usedcount=0;
  Dblsb*dblsb;

  for(i=0;i<CCACHESIZE;++i)
  { switch(ccache[i].c_flags)
    { case C_VALID:
      case C_DIRTY_NORMAL:
      case C_DIRTY_UNCOMPR:
        dblsb=MSDOS_SB(ccache[i].c_sb)->private_data;
        size+=dblsb->s_sectperclust*SECTOR_SIZE;
        usedcount++;
    }
  }
  if(used)*used=size;
  if(max)
  { /* unknown due to possibly different cluster sizes */
    /* we estimate :) */
    if(usedcount==0)*max=0; /* nothing in use, we can't estimate :( */
    else *max=(CCACHESIZE*size)/usedcount;
  }
}

/**********************************************************************/


struct buffer_head* dblspace_bread(struct super_block*sb,int vsector)
{ int dbl_clust;
  int sect_offs;
  struct buffer_head*bh;
  Cluster_head*ch;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
  
  if(vsector>=FAKED_ROOT_DIR_OFFSET&&
     vsector<FAKED_ROOT_DIR_OFFSET+dblsb->s_rootdirentries/16)
  { LOG_LLRW("DMSDOS: read_virtual_sector: rootdir sector_offset %d\n",
           vsector-FAKED_ROOT_DIR_OFFSET);
    return raw_bread(sb,dblsb->s_rootdir+vsector-FAKED_ROOT_DIR_OFFSET);
  }

  if(vsector<FAKED_DATA_START_OFFSET)  
  { err:
    printk(KERN_ERR "DMSDOS: illegal virtual sector %d, can't map to real sector\n",
           vsector);
    *(int*)0=0;
    return NULL;
  }
  dbl_clust=((vsector-FAKED_DATA_START_OFFSET)/dblsb->s_sectperclust)+2;
  if(dbl_clust>dblsb->s_max_cluster)goto err;
  sect_offs=(vsector-FAKED_DATA_START_OFFSET)%dblsb->s_sectperclust;
  
  ch=ch_read(sb,dbl_clust,0); /*we need to read and shouldn't need a lock*/
  if(ch==NULL)
  { printk(KERN_ERR "DMSDOS: read_virtual_sector: read_cluster failed!\n");
    return NULL;
  }
  /* now setup a fake buffer_head */
  bh=MALLOC(sizeof(struct buffer_head));
  if(bh==NULL)
  { printk(KERN_ERR "DMSDOS: read_virtual_sector: no memory!\n");
    /*FREE(clusterd);*/
    ch_free(ch);
    return NULL;
  }
  bh->b_state=FAKED_BH_MAGIC;

  /* we must share the data */
  bh->b_data=&(ch->c_data[sect_offs*SECTOR_SIZE]);
  /* the cluster is *not* free yet, so DON'T call ch_free here */
  /* instead copy a pointer to the cluster_head somewhere so we can free it 
     later */
  bh->b_next=(struct buffer_head*)ch; /*the cast avoids compiler warning */
  return bh;
}

struct buffer_head *dblspace_getblk (
	struct super_block *sb,
	int block)
{       
        struct buffer_head*ret;
	ret=dblspace_bread(sb,block);
	if(ret==NULL)return ret;
	return ret;
}

void dblspace_brelse(struct super_block* sb,struct buffer_head*bh)
{ if(bh)
  { if(bh->b_state==FAKED_BH_MAGIC)
    { /* we free the cluster instead */
      /* a pointer to ch was saved in b_next */
      /* uh, we must check the size first.... uh no this is for bh_dirty.. */
      Cluster_head*ch=(Cluster_head*)bh->b_next;
      /*if((unsigned long)bh->b_data-(unsigned long)ch->c_data+SECTOR_SIZE>ch->c_length)
          ch->c_length=(unsigned long)bh->b_data-(unsigned long)ch->c_data+SECTOR_SIZE;*/
      ch_free(ch);
      FREE(bh);
      return;
    }
  }
  raw_brelse(sb,bh);
}


void dblspace_mark_buffer_dirty(struct super_block*sb,struct buffer_head*bh,
                                int dirty_val)
{ int i;

  if(dirty_val==0)return;

  if(sb->s_flags&MS_RDONLY)
  { printk(KERN_ERR "DMSDOS: dblspace_mark_buffer_dirty: READ-ONLY filesystem\n");
    return;
  }

  if(bh)
  { if(bh->b_state==FAKED_BH_MAGIC)
    { 
      /* a copy of ch was saved in b_next */
      Cluster_head*ch=(Cluster_head*)bh->b_next;
      struct super_block*sb=ch->c_sb;
      Dblsb*dblsb=MSDOS_SB(sb)->private_data;
      
      /* ensure cluster is marked large enough to hold the virtual sector */
      if((unsigned long)bh->b_data-(unsigned long)ch->c_data+SECTOR_SIZE>
                                                               ch->c_length)
          ch->c_length=(unsigned long)bh->b_data-(unsigned long)ch->c_data+
                                                                 SECTOR_SIZE;

      /* the virtual sector code is usually called by directory handling 
         routines, so we check whether we may compress a directory
         -- file_write calles ch_dirty directly */
      i=ch_dirty(ch,0,DIR_MAY_BE_COMPRESSED(dblsb)?UC_NORMAL:UC_UNCOMPR);
      if(i<0&&i!=-EROFS) /*don't try until death on a read-only filesystem*/
      {
      /* now we have a serious problem here...
         ch_dirty failed... we are in danger of losing the data....
      */
         printk(KERN_CRIT "DMSDOS: Dirty virtual sector cannot be written - FILESYSTEM DAMAGE POSSIBLE! Trying to delay write.\n");
         ch_dirty_retry_until_success(ch,0,
                          DIR_MAY_BE_COMPRESSED(dblsb)?UC_NORMAL:UC_UNCOMPR);
      }
      return; 
    }
  }

  raw_mark_buffer_dirty(sb,bh,1);
}

void dblspace_set_uptodate (
	struct super_block *sb,
	struct buffer_head *bh,
	int val)
{       
        if(bh->b_state==FAKED_BH_MAGIC)return; 
        raw_set_uptodate(sb,bh,val);
}

int dblspace_is_uptodate (
	struct super_block *sb,
	struct buffer_head *bh)
{
        if(bh->b_state==FAKED_BH_MAGIC)return 1;        
	return raw_is_uptodate(sb,bh);
}

void dblspace_ll_rw_block (
	struct super_block *sb,
	int opr,
	int nbreq,
	struct buffer_head *bh[32])
{
        /* we just cannot do low-level access here */
        /* this might be wrong... */
        /* as far as I know, only the read_ahead code in fat/file.c uses 
           this... well we do our own read-ahead in dmsdos, so what... */ 
        return;
}

#ifdef USE_READA_LIST

DECLARE_MUTEX(reada_sem);   /* Must be initialized to green light */
void lock_reada(void) {down(&reada_sem);}
void unlock_reada(void) {up(&reada_sem);}
struct
{ struct buffer_head*bh;
  struct super_block*sb;
} reada_list[READA_LIST_SIZE];
int ra_pos=0;

void init_reada_list(void)
{ int i;

  for(i=0;i<READA_LIST_SIZE;++i)
  { reada_list[i].bh=NULL;
    reada_list[i].sb=NULL;
  }
}

void kill_reada_list_dev(int dev)
{ int i;

  lock_reada();

  for(i=0;i<READA_LIST_SIZE;++i)
  { if(reada_list[i].bh)
    { if(reada_list[i].sb->s_dev==dev)
      { raw_brelse(reada_list[i].sb,reada_list[i].bh);
        reada_list[i].bh=NULL;
        reada_list[i].sb=NULL;
      }
    }
  }

  unlock_reada();
}

void stack_reada(struct super_block*sb,struct buffer_head*bh)
{
  lock_reada();

  if(reada_list[ra_pos].bh)
                   raw_brelse(reada_list[ra_pos].sb,reada_list[ra_pos].bh);

  reada_list[ra_pos].bh=bh;
  reada_list[ra_pos].sb=sb;

  ++ra_pos;
  if(ra_pos>=READA_LIST_SIZE)ra_pos=0;

  unlock_reada();
}
#endif /*USE_READA_LIST*/

void dblspace_reada(struct super_block*sb, int sector,int count)
{ 
  struct buffer_head*bhlist[MAX_READA];
  int i;
  struct buffer_head**bhlist2=bhlist;

  if(count>MAX_READA)count=MAX_READA;

  i=0;
  while(i<count)
  { 
    bhlist[i]=raw_getblk(sb,sector+i); /* get without read */
    if(bhlist[i]==NULL)break; /* failed, give up */
    if(raw_is_uptodate(sb,bhlist[i]))
    { raw_brelse(sb,bhlist[i]); /* according to breada the buffer must be
                                   freed here */
      break; /* has already been read, abort */
    }
    ++i;
  }

  count=i;

  if(count==0)return;

  /* uhh.. there's a hard limit of 32 in the fat filesystem... */
  if(count/32)
  { for(i=0;i<count/32;++i)
    { raw_ll_rw_block(sb,READA,32,bhlist2);
      bhlist2+=32;
    }
  }
  if(count%32)raw_ll_rw_block(sb,READA,count%32,bhlist2);
          /* place read command in list, but don't wait for it to finish */

  for(i=0;i<count;++i)
#ifdef USE_READA_LIST
            stack_reada(sb,bhlist[i]);
#else
            /* not a good idea - brelse calls wait_on_buffer....... */
            raw_brelse(sb,bhlist[i]);
#endif
}
