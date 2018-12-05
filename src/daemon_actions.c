/* 
daemon_actions.c

DMSDOS CVF-FAT module: external dmsdos daemon.

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
#include "dmsdos.h"
#include<sys/ioctl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<string.h>
#include<errno.h>
#include<signal.h>
#include<unistd.h>

/* default compression level minus 1 for dmsdosd */
int cfaktor=11;

int debug=0;

int exit_signaled=0;

#define CONTINUE 0
#define EXIT_AND_FINISH 1
#define EXIT_IMMEDIATELY 2

/* time to sleep between two idle calls (in seconds) 
   for slower systems you can use a higher value in order to reduce 
   unnecessary polling */
#define SLEEPTIME 30

#define PIDFILE "/var/run/dmsdosd.pid"

typedef struct
{ long val1;
  long val2;
  long val3;
  unsigned char data[32*1024];
} Cdata;

Cdata cdata;
Cdata ckdata;

void signal_handler(int a)
{ if(a==SIGINT)exit_signaled=EXIT_AND_FINISH;
  if(a==SIGTERM)exit_signaled=EXIT_IMMEDIATELY;
  /* reactivate again */
  signal(SIGINT,signal_handler);
  signal(SIGTERM,signal_handler);
  signal(SIGUSR1,signal_handler); /* this should just wake up */
}

void pidfile(void)
{ struct stat buf;
  int pid=0;
  char str[100];
  FILE*f;

  if(stat(PIDFILE,&buf)>=0)
  { if(debug)fprintf(stderr,"pidfile exists");
    f=fopen(PIDFILE,"r");
    if(f)
    { fscanf(f,"%d",&pid);
      fclose(f);
      sprintf(str,"/proc/%d/stat",pid);
      if(stat(str,&buf)>=0)
      { fprintf(stderr,"dmsdosd already running\n");
        exit(1);
      }
    }
    unlink(PIDFILE);
  }

  f=fopen(PIDFILE,"w");
  if(f==NULL)
  { fprintf(stderr,"cannot write pidfile %s\n",PIDFILE);
    exit(1);
  }
  fprintf(f,"%d\n",getpid());
  fclose(f);
}

#include <stdarg.h>
int printk(const char *fmt, ...)
{ va_list ap;
  char buf[500];
  char*p=buf;
  int i;

  va_start(ap, fmt);
  i=vsnprintf(buf, 500, fmt, ap);
  va_end(ap);

  if(p[0]=='<'&&p[1]>='0'&&p[1]<='7'&&p[2]=='>')p+=3;
  if(strncmp(p,"DMSDOS: ",8)==0)p+=8;
  
  fprintf(stderr,"dmsdosd: %s",p);

  return i;
}

int errm=0;

int get_and_compress_one(int fd)
{ int ret;
  int handle;
  int length;
  int size;
  int method;
  
  /* get cluster to compress */
  if(exit_signaled!=CONTINUE)return 2;
  if(debug)fprintf(stderr,"dmsdosd: Trying to read...\n");
  ret=ioctl(fd,DMSDOS_D_READ,&cdata);
  if(ret!=1)
  { if(debug)fprintf(stderr,"dmsdosd: nothing there - D_READ ioctl returned %d\n",ret);
    return ret;
  }
  
  handle=cdata.val1;
  length=cdata.val2;
  size=(length-1)/512+1;
  method=cdata.val3;
  if(debug)fprintf(stderr,"dmsdosd: compressing...\n");
  
  if(method==SD_3||method==SD_4)
  {
#ifdef DMSDOS_CONFIG_STAC
    ret=stac_compress(cdata.data,length,ckdata.data,
                     sizeof(ckdata.data),method,cfaktor);
#else
    if(errm==0)
    { errm=1;
      fprintf(stderr,"dmsdosd: stacker compression requested, but not compiled in!\n");
    }  
    ret=-1;
#endif
  }
  else
    ret=dbl_compress(ckdata.data,cdata.data,size,method,cfaktor)*512;
  
  if(debug)fprintf(stderr,"dmsdosd: compress %X from %d returned %d\n",
                  method,length,ret);
  if(ret<0)ret=0; /* compression failed */
  ckdata.val1=handle;
  ckdata.val2=ret;
  if(debug)fprintf(stderr,"dmsdosd: writing...\n");
  ioctl(fd,DMSDOS_D_WRITE,&ckdata);
  
  return 1; /* one cluster compressed */
}

void do_dmsdosd_actions(int fd)
{
  /* register dmsdosd */
  if(debug)fprintf(stderr,"dmsdosd: calling D_ASK...\n");
  if(ioctl(fd,DMSDOS_D_ASK,getpid()))
  { fprintf(stderr,"dmsdosd: can't get permissions (internal daemon running?)\n");
    return;
  }
  
  signal(SIGINT,signal_handler);
  signal(SIGTERM,signal_handler);
  signal(SIGUSR1,signal_handler); /* this should just wake up */

  do
  { while(get_and_compress_one(fd)==1);
    /* don't kill the system performance when nothing to compress */
    if(exit_signaled==CONTINUE)
    { if(debug)fprintf(stderr,"dmsdosd: sleeping...\n"); 
      sleep(SLEEPTIME);
      /* throw away long idle mdfat/dfat/bitfat sectors */
      ioctl(fd,DMSDOS_FREE_IDLE_CACHE,NULL);
    }
  }
  while(exit_signaled==CONTINUE);  
  
  if(debug)fprintf(stderr,"dmsdosd: calling D_EXIT...\n");
  ioctl(fd,DMSDOS_D_EXIT,NULL);
  
  if(exit_signaled==EXIT_AND_FINISH)
  { exit_signaled=CONTINUE;
    while(get_and_compress_one(fd)==1);
  }
  
}

int scan(char*arg)
{ int w;

  if(strncmp(arg,"0x",2)==0)sscanf(arg+2,"%x",&w);
  else sscanf(arg,"%d",&w);
  
  return w;
}

int main(int argc, char*argv[])
{ Dblsb dblsb;
  int fd;
  int ret;
  
  if(argc<2||argc>4)
  {
    fprintf(stderr,"DMSDOS daemon (C) 1996-1998 Frank Gockel, Pavel Pisa\n");
    fprintf(stderr,"compiled " __DATE__ " " __TIME__ " under dmsdos version %d.%d.%d%s\n\n",
           DMSDOS_MAJOR,DMSDOS_MINOR,DMSDOS_ACT_REL,DMSDOS_VLT);
    fprintf(stderr,"Usage: %s (directory)\n",argv[0]);
    fprintf(stderr,"       %s (directory) cf\n",argv[0]);
    fprintf(stderr,"       %s (directory) cf debug\n",argv[0]);
    return 1;
  }

  if(getuid())
  { fprintf(stderr,"Sorry, you must be root to run me.\n");
    exit(1);
  }
  
  if(argc==4)debug=1;
  
  fd=open(argv[1],O_RDONLY);
  if(fd<0)
  { perror("open");
    return 1;
  }
  
  if(argc==3)
  { cfaktor=scan(argv[2])-1;
    if(cfaktor<0||cfaktor>11)
    { fprintf(stderr,"cf parameter out of range\n");
      close(fd);
      return 1;
    }
  }

  /* this hack enables reverse version check */
  /* it must not be changed in order to recognize incompatible older versions */
  /* this also depends on s_dcluster being the first record in Dblsb */
  dblsb.s_dcluster=DMSDOS_VERSION;
  
  ret=ioctl(fd,DMSDOS_GET_DBLSB,&dblsb);
  if(ret<0)
  { printf("This is not a DMSDOS directory.\n");
    close(fd);
    return 1;
  }
  if(ret!=DMSDOS_VERSION)printf("You are running DMSDOS driver version %d.%d.%d.\n",(ret&0xff0000)>>16,
         (ret&0x00ff00)>>8,ret&0xff);
  /*printf("debug: ret=0x%08x\n",ret);*/
  if(ret!=DMSDOS_VERSION)printf("This program was compiled for DMSDOS version %d.%d.%d",
         (DMSDOS_VERSION&0xff0000)>>16,(DMSDOS_VERSION&0x00ff00)>>8,DMSDOS_VERSION&0xff);        
  if(ret&0x0f000000)
  { printf("\nSorry, this program is too old for the actual DMSDOS driver version.\n");
    close(fd);
    return 1;
  }
  if(ret<0x00000902)
  { printf("\nSorry, this program requires at least DMSDOS driver version 0.9.2.\n");
    close(fd);
    return 1;
  }
  if(ret!=DMSDOS_VERSION)printf(" but should still work.\n\n");


  if(!debug)
  { if(fork())exit(0);
  }

  pidfile();

  do_dmsdosd_actions(fd);
  
  close(fd);

  unlink(PIDFILE);

  return 0;
}
