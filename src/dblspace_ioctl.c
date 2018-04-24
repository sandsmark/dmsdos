/*
dblspace_ioctl.c

DMSDOS CVF-FAT module: ioctl functions (interface for external utilities).

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

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/mm.h>
#include <asm/semaphore.h>
#include "dmsdos.h"

#ifdef INTERNAL_DAEMON
int idmsdosd(void*);
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#endif

extern unsigned long dmsdos_speedup;

int daemon_present=0;
int must_maintain_list=0;
int listcount=0;
Rwlist rwlist[LISTSIZE];
int rlist=0;
int wlist=0;

#ifdef INTERNAL_DAEMON
struct wait_queue * daemon_wait = NULL;
struct wait_queue * daemon_exit_wait=NULL;
int daemon_go_home=0;
int internal_daemon_counter=0;
#else
int daemon_pid;
int daemon_task_nr;
#endif

void dumpcache(void);
void dump_ccache(void);

struct semaphore listaccess_sem=MUTEX;   /* Must be initialized to green light */
void lock_listaccess(void) {down(&listaccess_sem);}
void unlock_listaccess(void) {up(&listaccess_sem);}

void log_statistics(void)
{ log_list_statistics();
  log_ccache_statistics();
  log_found_statistics();
  /*log_other_statistics();*/
}

#undef DEPRECATED_CODE
#ifdef DEPRECATED_CODE
int set_maxcluster(struct super_block*sb,int data)
{ struct buffer_head*bh,*bh2;
  int i;
  unsigned char*pp;
  unsigned long int sectors;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;

  if(dblsb->s_cvf_version>=DRVSP3)
  { printk(KERN_ERR "DMSDOS: set_maxcluster refused - CVF is not in doublespace or drivespace<=2 format.\n");
    return -EINVAL;
  }
  if(data<2||data>dblsb->s_max_cluster2)return -EINVAL;
  /* check if higher clusters are unused */
  for(i=dblsb->s_max_cluster2;i>data;--i)
  { if(dbl_fat_nextcluster(sb,i,NULL))
    { printk(KERN_ERR "DMSDOS: set_maxcluster %d refused: cluster %d in use\n",
             data,i);
      return -EINVAL;
    }
  }
  
  bh=raw_bread(sb,0);
  if(bh==NULL)return -EIO;
  bh2=raw_bread(sb,dblsb->s_bootblock);
  if(bh2==NULL)
  { raw_brelse(sb,bh);
    return -EIO;
  }
  
  /* calculate number of sectors */
  /*sectors=(data-1)<<4;*/
  sectors=(data-1)*dblsb->s_sectperclust;
  sectors+=dblsb->s_rootdirentries>>4;
  pp=&(bh->b_data[14]);
  sectors+=CHS(pp);
  pp=&(bh->b_data[22]);
  sectors+=CHS(pp);
  bh->b_data[32]=sectors;
  bh->b_data[33]=sectors>>8;
  bh->b_data[34]=sectors>>16;
  bh->b_data[35]=sectors>>24;
  sectors+=CHS(pp);
  bh2->b_data[32]=sectors;
  bh2->b_data[33]=sectors>>8;
  bh2->b_data[34]=sectors>>16;
  bh2->b_data[35]=sectors>>24;
  raw_mark_buffer_dirty(sb,bh,1);
  raw_mark_buffer_dirty(sb,bh2,1);
  raw_brelse(sb,bh);
  raw_brelse(sb,bh2);
  
  dblsb->s_max_cluster=data;
  MSDOS_SB(sb)->clusters=data;
  MSDOS_SB(sb)->free_clusters=-1;
      
  return 0;
}
#endif

void dmsdos_extra_statfs(struct super_block*sb, Dblstat*dblstat)
{ int sector,size,cluster;
  Mdfat_entry mde;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
    
      dblstat->free_sectors=0;
      dblstat->max_hole=0;  
      for(sector=dblsb->s_datastart;sector<dblsb->s_dataend;
          ++sector)
      { if(dbl_bitfat_value(sb,sector,NULL)==0)
        { ++(dblstat->free_sectors);
          size=1;
          while(dbl_bitfat_value(sb,sector+size,NULL)==0)++size;
          if(size>dblstat->max_hole)dblstat->max_hole=size;
          sector+=size-1;
          dblstat->free_sectors+=size-1;
        }
      }
      dblstat->used_sectors=dblsb->s_dataend+1-dblsb->s_datastart
                   -dblstat->free_sectors;
      dblstat->sectors_lo=0;
      dblstat->sectors_hi=0;
      dblstat->compressed_clusters=0;
      dblstat->uncompressed_clusters=0;
      dblstat->free_clusters=0;
      dblstat->used_clusters=0;
      dblstat->lost_clusters=0;
      for(cluster=2;cluster<=dblsb->s_max_cluster;++cluster)
      { if(dbl_fat_nextcluster(sb,cluster,NULL)!=0)
        { dbl_mdfat_value(sb,cluster,NULL,&mde);
          if(mde.flags&2)
          { ++(dblstat->used_clusters);
            if(mde.flags&1)++(dblstat->uncompressed_clusters);
            else ++(dblstat->compressed_clusters);
            dblstat->sectors_hi+=mde.size_hi_minus_1+1;
            dblstat->sectors_lo+=mde.size_lo_minus_1+1;
          }
          else ++(dblstat->lost_clusters);
        }
        else ++(dblstat->free_clusters);
      }
}
    
int dmsdos_ioctl_dir(
	struct inode *dir,
	struct file *filp,
	unsigned int cmd,
	unsigned long data)
{ unsigned char* idata;
  struct buffer_head*bh;
  int newval;
  int val;
  int cluster;
  int sector;
  Dblstat dblstat;
  Mdfat_entry mde,dummy;
  unsigned char *clusterd;
  int membytes;
  struct super_block*sb=dir->i_sb;
  Dblsb*dblsb=MSDOS_SB(sb)->private_data;
  long lval;
#ifndef INTERNAL_DAEMON
  int length;
  int i;
  int plist;
  /*int sectors;*/
  int rawlength;
#endif

  idata=(unsigned char*)data;

  switch(cmd)	
  { case DMSDOS_GET_DBLSB:
      /* check dutil version number */
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      /*if(get_fs_long(idata)<DMSDOS_LOWEST_COMPATIBLE_VERSION)*/
      memcpy_fromfs(&lval,idata,sizeof(long));
      if(lval<DMSDOS_LOWEST_COMPATIBLE_VERSION)
        return DMSDOS_VERSION|0x0f000000;
      if(verify_area(VERIFY_WRITE, idata, sizeof(Dblsb)))return -EFAULT;
      memcpy_tofs(idata,(unsigned char*)dblsb,sizeof(Dblsb));
      return DMSDOS_VERSION;
    case DMSDOS_EXTRA_STATFS:
      if(verify_area(VERIFY_WRITE, idata, sizeof(Dblstat)))return -EFAULT;
      dmsdos_extra_statfs(dir->i_sb,&dblstat);
      memcpy_tofs(idata,&dblstat,sizeof(Dblstat));
      return 0;
    case DMSDOS_READ_BLOCK:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      /*sector=get_fs_long(idata);*/
      memcpy_fromfs(&sector,idata,sizeof(long));
      if(sector<0||sector>dblsb->s_dataend)return -EINVAL;
      bh=raw_bread(dir->i_sb,sector);
      if(bh==NULL)return -EIO;
      if(verify_area(VERIFY_WRITE, idata+sizeof(long),SECTOR_SIZE))return -EFAULT;
      memcpy_tofs(idata+sizeof(long),bh->b_data,SECTOR_SIZE);
      raw_brelse(dir->i_sb,bh);
      return 0;
    case DMSDOS_WRITE_BLOCK:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, sizeof(long)+SECTOR_SIZE))return -EFAULT;
      /*sector=get_fs_long(idata);*/
      memcpy_fromfs(&sector,idata,sizeof(long));
      if(sector<0||sector>dblsb->s_dataend)return -EINVAL;
      bh=raw_bread(dir->i_sb,sector);
      if(bh==NULL)return -EIO;
      memcpy_fromfs(bh->b_data,idata+sizeof(long),SECTOR_SIZE);
      raw_mark_buffer_dirty(dir->i_sb,bh,1);
      raw_brelse(dir->i_sb,bh);
      return 0;
    case DMSDOS_READ_DIRENTRY:
    case DMSDOS_WRITE_DIRENTRY:
      printk(KERN_WARNING "DMSDOS: READ/WRITE DIRENTRY ioctl has gone\n");
      return -EINVAL;
    case DMSDOS_READ_BITFAT:
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      /*sector=get_fs_long(idata);*/
      memcpy_fromfs(&sector,idata,sizeof(long));
      if(sector<0||sector>dblsb->s_dataend)return -EINVAL;
      val=dbl_bitfat_value(sb,sector,NULL);
      if(verify_area(VERIFY_WRITE, idata+sizeof(long),sizeof(long)))return -EFAULT;
      /*put_fs_long(val,idata+sizeof(long));*/
      memcpy_tofs(idata+sizeof(long),&val,sizeof(long));
      return 0;
    case DMSDOS_WRITE_BITFAT:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, 2*sizeof(long)))return -EFAULT;
      /*sector=get_fs_long(idata);*/
      memcpy_fromfs(&sector,idata,sizeof(long));
      if(sector<0||sector>dblsb->s_dataend)return -EINVAL;
      /*newval=get_fs_long(idata+sizeof(long));*/
      memcpy_fromfs(&newval,idata+sizeof(long),sizeof(long));
      dbl_bitfat_value(sb,sector,&newval);
      return 0;
    case DMSDOS_READ_MDFAT:
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      /*cluster=get_fs_long(idata);*/
      memcpy_fromfs(&cluster,idata,sizeof(long));
      if(cluster>dblsb->s_max_cluster)return -EINVAL;
      dbl_mdfat_value(sb,cluster,NULL,&mde);
      if(verify_area(VERIFY_WRITE,idata+sizeof(long),sizeof(Mdfat_entry)))return -EFAULT;
      memcpy_fromfs(&lval,idata+sizeof(long),sizeof(long));
      memcpy_tofs((Mdfat_entry*)lval,&mde,sizeof(Mdfat_entry));
      return 0;
    case DMSDOS_WRITE_MDFAT:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, sizeof(long)+sizeof(Mdfat_entry)))return -EFAULT;
      /*cluster=get_fs_long(idata);*/
      memcpy_fromfs(&cluster,idata,sizeof(long));
      if(cluster>dblsb->s_max_cluster)return -EINVAL;
      memcpy_fromfs(&lval,idata+sizeof(long),sizeof(long));
      memcpy_fromfs(&mde,(Mdfat_entry*)lval,
                    sizeof(Mdfat_entry));
      dbl_mdfat_value(sb,cluster,&mde,&dummy);
      return 0;
    case DMSDOS_READ_DFAT:
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      /*cluster=get_fs_long(idata);*/
      memcpy_fromfs(&cluster,idata,sizeof(long));
      if(cluster>dblsb->s_max_cluster)return -EINVAL;
      val=dbl_fat_nextcluster(sb,cluster,NULL);
      if(verify_area(VERIFY_WRITE, idata+sizeof(long),sizeof(long)))return -EFAULT;      
      /*put_fs_long(val,idata+sizeof(long));*/
      memcpy_tofs(idata+sizeof(long),&val,sizeof(long));
      return 0;
    case DMSDOS_WRITE_DFAT:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, 2*sizeof(long)))return -EFAULT;
      /*cluster=get_fs_long(idata);*/
      memcpy_fromfs(&cluster,idata,sizeof(long));
      if(cluster>dblsb->s_max_cluster)return -EINVAL;
      /*newval=get_fs_long(idata+sizeof(long));*/
      memcpy_fromfs(&newval,idata+sizeof(long),sizeof(long));
      dbl_fat_nextcluster(sb,cluster,&newval);
      return 0;
    case DMSDOS_READ_CLUSTER:
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;      
      /*cluster=get_fs_long(idata);*/
      memcpy_fromfs(&cluster,idata,sizeof(long));
      if(cluster < 0 || cluster>dblsb->s_max_cluster)
        return -EINVAL;
      membytes=SECTOR_SIZE*dblsb->s_sectperclust;
      if(verify_area(VERIFY_WRITE, idata+sizeof(long),membytes))return -EFAULT;
      if((clusterd=(unsigned char*)MALLOC(membytes))==NULL)
      { printk(KERN_ERR "DMSDOS: ioctl: read_cluster: no memory!\n");
        return -EIO;
      }
      val=dmsdos_read_cluster(dir->i_sb,clusterd,cluster);
      if (val >= 0)
        memcpy_tofs(idata+sizeof(long), clusterd, membytes);
      FREE(clusterd);
      return val;
    case DMSDOS_SET_COMP:
      if(current->euid!=0)return -EPERM;
      if(dblsb->s_comp!=READ_ONLY&&data==READ_ONLY)sync_cluster_cache(0);
      dblsb->s_comp=data;
      if(data==READ_ONLY)sb->s_flags |= MS_RDONLY;
      else sb->s_flags&=~MS_RDONLY;
      return 0;
    case DMSDOS_SET_CF:
      if(current->euid!=0)return -EPERM;
      if(data>=12u)return -EINVAL;
      dblsb->s_cfaktor=data;
      return 0;
    case DMSDOS_SIMPLE_CHECK:
      if(verify_area(VERIFY_READ, idata, sizeof(long)))return -EFAULT;
      memcpy_fromfs(&lval,idata,sizeof(long));      
      val=simple_check(dir->i_sb,lval);
      if(verify_area(VERIFY_WRITE, idata, sizeof(long)))return -EFAULT;
      /*put_fs_long(val,idata);*/
      memcpy_tofs(idata,&val,sizeof(long));
      return 0;
    case DMSDOS_DUMPCACHE:
      dumpcache();
      dump_ccache();
      return 0;
    case DMSDOS_D_ASK:
 #ifdef INTERNAL_DAEMON
      return -EINVAL;
 #else
      if(current->euid!=0)return -EPERM;
      /* dmsdosd says it is present and ready */
      if(current->pid!=data)
      { printk(KERN_ERR "DMSDOS: daemon is lying about its pid\n");
        return -EINVAL;
      }
      daemon_present=1;
      daemon_pid=current->pid;
      LOG_DAEMON("DMSDOS: D_ASK\n");
      return 0;
 #endif
    case DMSDOS_D_READ:
 #ifdef INTERNAL_DAEMON
      return -EINVAL;
 #else
      if(current->euid!=0)return -EPERM;
      lock_listaccess();
      /*search next valid entry*/
      for(i=LISTSIZE;i>0;--i)
      { if(rwlist[rlist].flag==D_VALID)
        { /* check in mdfat that cluster is actually used */
          dbl_mdfat_value(rwlist[rlist].sb,rwlist[rlist].clusternr,
                          NULL,&mde);
          if((mde.flags&3)==3)goto vr_found; /* used and uncompressed */
          rwlist[rlist].flag=D_EMPTY;  /* remove - it's garbage */
          --listcount;
          LOG_DAEMON("DMSDOS: D_READ: removing garbage entry cluster=%d\n",
                 rwlist[rlist].clusternr);
        }
        /*rlist=(rlist+1)&(LISTSIZE-1);*/
        rlist++;if(rlist>=LISTSIZE)rlist=0;
      }
      unlock_listaccess();
      return 0;
   vr_found:
      cluster=rwlist[rlist].clusternr;
      /* yes we change sb here */
      sb=rwlist[rlist].sb;
      dblsb=MSDOS_SB(sb)->private_data;
      length=rwlist[rlist].length;
      clusterd=MALLOC(dblsb->s_sectperclust*SECTOR_SIZE);
      if(clusterd==NULL)
      { printk(KERN_ERR "DMSDOS: ioctl: D_READ: no memory!\n");
        unlock_listaccess();
        return 0; 
      }
      if((i=dmsdos_read_cluster(sb,clusterd,cluster))<0)
      { printk(KERN_ERR "DMSDOS: ioctl: D_READ: read_cluster failed!\n");
        rwlist[rlist].flag=D_EMPTY;
        --listcount;
        unlock_listaccess();
        FREE(clusterd);
        return 0;   
      }
      if(length<0)length=i; /* if invalid length, use the read value */
      rwlist[rlist].flag=D_IN_D_ACTION;
      if(verify_area(VERIFY_WRITE, idata, 3*sizeof(long)+length))
      { unlock_listaccess();
        FREE(clusterd);
        return -EFAULT;
      }
      /*put_fs_long(rlist,idata);*/
      memcpy_tofs(idata,&rlist,sizeof(long));
      /*put_fs_long(length,idata+sizeof(long));*/
      memcpy_tofs(idata+sizeof(long),&length,sizeof(long));
      /*put_fs_long(rwlist[rlist].method,idata+2*sizeof(long));*/
      memcpy_tofs(idata+2*sizeof(long),&(rwlist[rlist].method),sizeof(long));
      memcpy_tofs(idata+3*sizeof(long),clusterd,length);
      unlock_listaccess();
      FREE(clusterd);
      return 1;
#endif
    case DMSDOS_D_WRITE:
#ifdef INTERNAL_DAEMON
      return -EINVAL;
#else
      if(current->euid!=0)return -EPERM;
      if(verify_area(VERIFY_READ,idata,3*sizeof(long)))return -EFAULT;             
      lock_listaccess();
      /*plist=get_fs_long(idata);*/
      memcpy_fromfs(&plist,idata,sizeof(long));
      if(rwlist[plist].flag==D_OVERWRITTEN){rwlist[plist].flag=D_EMPTY;--listcount;}
      if(rwlist[plist].flag!=D_IN_D_ACTION)
      { LOG_DAEMON("DMSDOS: D_WRITE: Entry not in action, flag=%d cluster=%d\n",
               rwlist[plist].flag,rwlist[plist].clusternr);
        unlock_listaccess();
        return 0;
      }
      
      /* will be freed in any case later, so: */
      --listcount;
      
      /*rawlength=get_fs_long(idata+sizeof(long));*/
      memcpy_fromfs(&rawlength,idata+sizeof(long),sizeof(long));
      if(rawlength==0)
      { /* data were uncompressible */
        rwlist[plist].flag=D_EMPTY;
        unlock_listaccess();
        return 0;
      }
      /* check that cluster is used */
      dbl_mdfat_value(rwlist[plist].sb,rwlist[plist].clusternr,
                      NULL,&mde);
      if((mde.flags&3)!=3)
      { rwlist[plist].flag=D_EMPTY;  /* remove - it's garbage */
        LOG_DAEMON("DMSDOS: D_WRITE: removing garbage entry cluster=%d\n",
               rwlist[plist].clusternr);
        unlock_listaccess();
        return 0;
      }
      if(verify_area(VERIFY_READ,idata+3*sizeof(long),rawlength))
      { rwlist[plist].flag=D_EMPTY;
        unlock_listaccess();
        return -EFAULT;
      }
      clusterd=MALLOC(rawlength);
      if(clusterd==NULL)
      { printk(KERN_ERR "DMSDOS: ioctl: D_WRITE: no memory!\n");
        rwlist[plist].flag=D_EMPTY;
        unlock_listaccess();
        return 0;
      }
      length=rwlist[plist].length;
      cluster=rwlist[plist].clusternr;
      sb=rwlist[plist].sb;
      memcpy_fromfs(clusterd,idata+3*sizeof(long),rawlength);
      rwlist[plist].flag=D_EMPTY;
      unlock_listaccess();
      /* this may call try_daemon via ccache code (freeing a ccache slot) */
      daemon_write_cluster(sb,clusterd,length,
                           cluster,
                           rawlength);
      FREE(clusterd);
      return 0;
#endif
    case DMSDOS_D_EXIT:
#ifdef INTERNAL_DAEMON
      return -EINVAL;
#else
      if(current->euid!=0)return -EPERM;
      /* dmsdosd is saying good-bye */
      daemon_present=0;
      LOG_DAEMON("DMSDOS: D_EXIT\n");
      return 0;
#endif
    case DMSDOS_MOVEBACK:
      printk(KERN_WARNING "DMSDOS: MOVEBACK ioctl has gone\n");
      return -EINVAL;
    case DMSDOS_SET_MAXCLUSTER:
      /*if(current->euid!=0)return -EPERM;
      return set_maxcluster(dir->i_sb,data);*/
      printk(KERN_WARNING "DMSDOS: SETMAXCLUSTER ioctl has gone.\n");
      return -EINVAL;
    case DMSDOS_FREE_IDLE_CACHE:
      if(current->euid!=0)return -EPERM;
      free_idle_cache();
      return 0;
    case DMSDOS_SET_LOGLEVEL:
      if(current->euid!=0)return -EPERM;
      loglevel=data;
      printk(KERN_INFO "DMSDOS: ioctl: loglevel set to 0x%lx.\n",loglevel);
      return 0;
    case DMSDOS_SET_SPEEDUP:
      if(current->euid!=0)return -EPERM;
      dmsdos_speedup=data;
      printk(KERN_INFO "DMSDOS: ioctl: speedup set to 0x%lx.\n",dmsdos_speedup);
      return 0;
    case DMSDOS_SYNC_CCACHE:
      sync_cluster_cache(data);
      return 0;
    case DMSDOS_LOG_STATISTICS:
      if(current->euid!=0)return -EPERM;
      log_statistics();
      return 0;
    case DMSDOS_RECOMPRESS:
      printk(KERN_WARNING "DMSDOS: RECOMPRESS ioctl has gone\n");
      return -EINVAL;
    case DMSDOS_REPORT_MEMORY:
      { Memuse memuse;
        if(verify_area(VERIFY_WRITE,idata,sizeof(Memuse)))return -EFAULT;
        get_memory_usage_acache(&(memuse.acachebytes),&(memuse.max_acachebytes));
        get_memory_usage_ccache(&(memuse.ccachebytes),&(memuse.max_ccachebytes));
        memcpy_tofs(idata,&memuse,sizeof(Memuse));
        return 0;   
      }
    default:
      return -EINVAL;
  }	
	
}

void remove_from_daemon_list(struct super_block*sb,int clusternr)
{ int i;

  if(must_maintain_list==0)return;

  lock_listaccess();
  
  /* check list for existing entry and mark it as overwritten */
  for(i=0;i<LISTSIZE;++i)
  { if(rwlist[i].flag!=D_EMPTY)
    if(rwlist[i].clusternr==clusternr&&rwlist[i].sb->s_dev==sb->s_dev)
    {
      if(rwlist[i].flag==D_IN_D_ACTION){rwlist[i].flag=D_OVERWRITTEN;break;}
      if(rwlist[i].flag==D_VALID){rwlist[i].flag=D_EMPTY;--listcount;break;}
    }
  }
  
  if(daemon_present==0)
  { if(listcount==0)must_maintain_list=0;
  }
    
  unlock_listaccess();
}

int lastawake=0;
int try_daemon(struct super_block*sb,int clusternr, int length, int method)
{ int i;

  if(daemon_present==0&&must_maintain_list==0)return 0;
  
  lock_listaccess();
  
  must_maintain_list=1;
  
  /* check list for existing entry and mark it as overwritten */
/* no longer necessary here......
  for(i=0;i<LISTSIZE;++i)
  { if(rwlist[i].clusternr==clusternr&&rwlist[i].sb->s_dev==sb->s_dev)
    {
      if(rwlist[i].flag==D_IN_D_ACTION){rwlist[i].flag=D_OVERWRITTEN;break;}
      if(rwlist[i].flag==D_VALID){rwlist[i].flag=D_EMPTY;--listcount;break;}
    }
  }
*/
  
  if(daemon_present==0||listcount==LISTSIZE)
  {
    if(listcount==0)must_maintain_list=0;
    unlock_listaccess();
    return 0;
  }
 
  /* find empty slot in list */
  for(i=LISTSIZE;i>0;--i)
  { if(rwlist[wlist].flag==D_EMPTY) goto w_found;
    /*wlist=(wlist+1)&(LISTSIZE-1);*/
    wlist++;if(wlist>=LISTSIZE)wlist=0;
  }
  /*this shouldn't happen - otherwise there's a count error */
  printk(KERN_WARNING "DMSDOS: try_daemon: no empty slot found, listcount corrected.\n");
  listcount=LISTSIZE;
  unlock_listaccess();
  return 0;
  
 w_found:
  ++listcount;
  rwlist[wlist].clusternr=clusternr;
  rwlist[wlist].sb=sb;
  rwlist[wlist].length=length;
  rwlist[wlist].method=method;
  rwlist[wlist].flag=D_VALID;
  unlock_listaccess();
  
  /* check whether or not to awake the daemon -
     strategy is this:
      * don't awake it in periods below 5 secs
      * don't awake it for just a little data
  */
  if(lastawake+5>CURRENT_TIME||listcount<LISTSIZE/2)return 1;
  lastawake=CURRENT_TIME;
#ifdef INTERNAL_DAEMON
  wake_up(&daemon_wait);
#else
  i=kill_proc(daemon_pid,SIGUSR1,1);
  if(i<0)
  { printk(KERN_WARNING "DMSDOS: try_daemon: kill_proc daemon_pid=%d failed with error code %d, assuming daemon has died\n",
           daemon_pid,-i);
    daemon_present=0;
  }
#endif
  return 1;
}

void clear_list_dev(struct super_block*sb)
{ int i;

  lock_listaccess();
  for(i=0;i<LISTSIZE;++i)
  { if(rwlist[i].flag!=D_EMPTY)
    { if(rwlist[i].sb)printk(KERN_ERR "DMSDOS: clear_list_dev: Uhh, sb==NULL ...\n");
      else
      if(rwlist[i].sb->s_dev==sb->s_dev)
      { rwlist[i].flag=D_EMPTY;
        --listcount;
      }
    }
  }
  unlock_listaccess();
}

void init_daemon(void)
{ int i;

  if(daemon_present)
  { printk(KERN_INFO "DMSDOS: init_daemon: daemon already present\n");
#ifdef INTERNAL_DAEMON
    ++internal_daemon_counter;
#endif
    return;
  }
  lock_listaccess();
  LOG_REST("DMSDOS: clearing rwlist...\n");
  for(i=0;i<LISTSIZE;++i)rwlist[i].flag=D_EMPTY;
  listcount=0;
  unlock_listaccess();

#ifdef INTERNAL_DAEMON
  /*daemon_present=1...this is maintained by idmsdosd itself*/;
  internal_daemon_counter=1;
  /* fire up internal daemon */
  printk(KERN_NOTICE "DMSDOS: starting internal daemon...\n");
  daemon_go_home=0;
  kernel_thread(idmsdosd,NULL,0);
#endif
}

void log_list_statistics()
{ int j;
  int empty;
  int valid;
  int in_action;
  int overwritten;
  
  lock_listaccess();
  
  printk(KERN_INFO "DMSDOS: list statistics:\n");
  printk(KERN_INFO "daemon_present=%d must_maintain_list=%d listcount=%d\n",
         daemon_present,must_maintain_list,listcount);
  empty=0;
  valid=0;
  in_action=0;
  overwritten=0;
    
  for(j=0;j<LISTSIZE;++j)
      { switch(rwlist[j].flag)
        { case D_EMPTY: ++empty; break;
          case D_VALID: ++valid; break;
          case D_IN_D_ACTION: ++in_action; break;
          case D_OVERWRITTEN: ++overwritten; break;
          default: printk(KERN_ERR "DMSDOS: log_list_statistics: cannot happen.\n");
        }
      }
  printk(KERN_INFO "sum: empty=%d valid=%d in_action=%d overwritten=%d\n",
         empty,valid,in_action,overwritten);
  
  unlock_listaccess();
}

#ifdef INTERNAL_DAEMON

int internal_d_read(int*val1, int*val2, int*val3,
                    unsigned char*clusterd)
{ int i;
  int cluster;
  Mdfat_entry mde;
  int length;
  int method;
  struct super_block*sb=NULL;

      lock_listaccess();
      /*search next valid entry*/
      for(i=LISTSIZE;i>0;--i)
      { if(rwlist[rlist].flag==D_VALID)
        { /* check in mdfat that cluster is actually used */
          sb=rwlist[rlist].sb;
          if(sb)
          { dbl_mdfat_value(sb,rwlist[rlist].clusternr,
                          NULL,&mde);
            if((mde.flags&3)==3)goto vr_found; /* used and uncompressed */
          }
          rwlist[rlist].flag=D_EMPTY;  /* remove - it's garbage */
          --listcount;
          LOG_DAEMON("DMSDOS: D_READ: removing garbage entry cluster=%d\n",
                 rwlist[rlist].clusternr);
        }
        /*rlist=(rlist+1)&(LISTSIZE-1);*/
        rlist++;if(rlist>=LISTSIZE)rlist=0;
      }
      unlock_listaccess();
      return 0;
   vr_found:
      cluster=rwlist[rlist].clusternr;
      sb=rwlist[rlist].sb;
      length=rwlist[rlist].length;
      method=rwlist[rlist].method;
      if((i=dmsdos_read_cluster(sb,clusterd,cluster))<0)
      { printk(KERN_ERR "DMSDOS: ioctl: D_READ: read_cluster failed!\n");
        rwlist[rlist].flag=D_EMPTY;
        --listcount;
        unlock_listaccess();
        return 0;   
      }
      if(length<0)length=i; /* if invalid length, use read value */
      rwlist[rlist].flag=D_IN_D_ACTION;
      *val1=rlist; /*put_fs_long(rlist,idata);*/
      *val2=length; /*put_fs_long(length,idata+sizeof(long));*/
      *val3=method; /*put_fs_long(rwlist[rlist].method,idata+2*sizeof(long));*/
      unlock_listaccess();
      return 1;
}

int internal_d_write(int plist, int rawlength, unsigned char*clusterd)
{ Mdfat_entry mde;
  int length;
  int cluster;
  struct super_block*sb=NULL;

      lock_listaccess();
      if(rwlist[plist].flag==D_OVERWRITTEN){rwlist[plist].flag=D_EMPTY;--listcount;}
      if(rwlist[plist].flag!=D_IN_D_ACTION)
      { LOG_DAEMON("DMSDOS: D_WRITE: Entry not in action, flag=%d cluster=%d\n",
               rwlist[plist].flag,rwlist[plist].clusternr);
        unlock_listaccess();
        return 0;
      }
      
      /* will be freed in any case later, so: */
      --listcount;
      
      if(rawlength==0)
      { /* data were uncompressible */
        rwlist[plist].flag=D_EMPTY;
        unlock_listaccess();
        return 0;
      }
      /* check that cluster is used */
      sb=rwlist[plist].sb;
      if(sb==NULL)goto shitt;
      dbl_mdfat_value(sb,rwlist[plist].clusternr,
                      NULL,&mde);
      if((mde.flags&3)!=3)
      { shitt:
        rwlist[plist].flag=D_EMPTY;  /* remove - it's garbage */
        LOG_DAEMON("DMSDOS: D_WRITE: removing garbage entry cluster=%d\n",
               rwlist[plist].clusternr);
        unlock_listaccess();
        return 0;
      }
      length=rwlist[plist].length;
      cluster=rwlist[plist].clusternr;
      rwlist[plist].flag=D_EMPTY;
      unlock_listaccess();
      /* this may call try_daemon via ccache code (freeing a ccache slot) */
      daemon_write_cluster(sb,clusterd,length,cluster,
                           rawlength);
      return 0;
}

typedef struct
{ long val1;
  long val2;
  long val3;
  unsigned char data[32*1024];
} Cdata;

/* we need the memory always - there's only one process using it - idmsdosd */
Cdata cdata;
Cdata ckdata;

int get_and_compress_one(void)
{ int ret;
  int handle;
  int length;
  int size;
  int method;
  
  /* get cluster to compress */
  LOG_DAEMON("idmsdosd: Trying to read...\n");
  ret=internal_d_read(&handle,&length,&method,cdata.data);
  if(ret!=1)
  { LOG_DAEMON("idmsdosd: nothing there - D_READ ioctl returned %d\n",ret);
    return ret;
  }
  
  size=(length-1)/512+1;
  LOG_DAEMON("idmsdosd: compressing...\n");
  ret=
#ifdef DMSDOS_CONFIG_STAC
      (method==SD_3||method==SD_4) ?
       stac_compress(cdata.data,length,ckdata.data,
                     sizeof(ckdata.data),method,11) :
#endif
       dbl_compress(ckdata.data,cdata.data,size,method,11)*512;
  LOG_DAEMON("idmsdosd: compress %X from %d returned %d\n",
                  method,length,ret);
  if(ret<0)ret=0; /* compression failed */
  LOG_DAEMON("idmsdosd: writing...\n");
  internal_d_write(handle,ret,ckdata.data);
  
  return 1; /* one cluster compressed */
}

struct timer_list idmsdosd_timer;

static void idmsdosd_timer_function(unsigned long data)
{ 
  /* do something */
  /*printk(KERN_DEBUG "DMSDOS: idmsdosd_timer_function: doing something :)\n");*/
    
  /* wake up daemon */
  wake_up(&daemon_wait);

  del_timer(&idmsdosd_timer);
  idmsdosd_timer.expires=jiffies + (IDMSDOSD_TIME * HZ);
  add_timer(&idmsdosd_timer);
}

int idmsdosd(void*dummy)
{      
        /* throw away some things from the calling process */
        /* otherwise the root fs cannot be unmounted on reboot ... urgh*/
	exit_files(current);
	exit_fs(current);
	exit_sighand(current);
        exit_mm(current);
        
        /*
         *      We have a bare-bones task_struct, and really should fill
         *      in a few more things so "top" and /proc/2/{exe,root,cwd}
         *      display semi-sane things. Not real crucial though...
         */
        current->session = 1;
        current->pgrp = 1;
        sprintf(current->comm, "dmsdosd");

#undef NEED_LOCK_KERNEL
       /* do we really need this ??? The code was copied from kflushd. */
#ifdef NEED_LOCK_KERNEL
        /*
         *      As a kernel thread we want to tamper with system buffers
         *      and other internals and thus be subject to the SMP locking
         *      rules. (On a uniprocessor box this does nothing).
         */
#ifdef __SMP__
        lock_kernel();
        syscall_count++;
#endif
#endif

  daemon_present=1;
  
  init_timer(&idmsdosd_timer);
  idmsdosd_timer.function=idmsdosd_timer_function;
  idmsdosd_timer.expires=jiffies + (IDMSDOSD_TIME * HZ);
  add_timer(&idmsdosd_timer);
  
  for(;;)
  { while(get_and_compress_one()==1);
    /* don't kill the system performance when nothing to compress */
    { LOG_DAEMON("idmsdosd: sleeping...\n"); 
      interruptible_sleep_on(&daemon_wait);
      if(daemon_go_home)break;
      /* throw away long idle mdfat/dfat/bitfat sectors and clusters */
      free_idle_cache();
    }
  }
  
  del_timer(&idmsdosd_timer);
  daemon_present=0;
  
  LOG_DAEMON("idmsdosd: exiting...\n");
  wake_up(&daemon_exit_wait);
  return 0;
}

void remove_internal_daemon(void)
{ if(daemon_present)
   { printk(KERN_NOTICE "DMSDOS: killing internal daemon...\n");
     daemon_go_home=1;
     wake_up(&daemon_wait);
     interruptible_sleep_on(&daemon_exit_wait);
     /* this seems to work - don't ask me why :) */
   }
}

#endif

void exit_daemon(void)
{
#ifdef INTERNAL_DAEMON
   --internal_daemon_counter;
   if(internal_daemon_counter<0)
   { printk(KERN_WARNING "DMSDOS: exit_daemon: counter<0 ???\n");
     internal_daemon_counter=0;
   }
   if(internal_daemon_counter==0)remove_internal_daemon();
#endif
}

void force_exit_daemon(void)
{
#ifdef INTERNAL_DAEMON
   internal_daemon_counter=0;
   remove_internal_daemon();
#endif
}
