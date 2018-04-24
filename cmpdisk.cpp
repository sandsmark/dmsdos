
/* BC 3.1 source code for something similar to simple diff */

#include <string.h>
#include <dos.h>
#include <dir.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#define COMPAREBUFLEN (1024*16)
char comparebuf1[COMPAREBUFLEN];
char comparebuf2[COMPAREBUFLEN];

int TestAllFiles(char *path1,char *path2)
{
 int old_path_len1,old_path_len2;
 int done,file1,file2,i;
 struct ffblk s;
 int len1,len2;
 long int pos;

 old_path_len1=strlen(path1);
 old_path_len2=strlen(path2);
 strcat(path1,"*.*");
 done=findfirst(path1,&s,0x2f);
 path1[old_path_len1]=0;

 while(!done)
 {
  path1[old_path_len1]=0;
  path2[old_path_len2]=0;
  strcat(path1,s.ff_name);
  strcat(path2,s.ff_name);

  printf("File in progress %s",path1);

  if ((file1=open(path1, O_RDONLY | O_BINARY , S_IREAD))==-1)
   {printf("\nCannot open file: %s \n",path1);goto error;}
  else if ((file2=open(path2, O_RDONLY | O_BINARY , S_IREAD))==-1)
   {printf("\nCannot open file: %s \n",path2);close(file1);goto error;}
  else
  {
   len2=len1=COMPAREBUFLEN;pos=0;
   while((len1==COMPAREBUFLEN)&&(len2==COMPAREBUFLEN))
   {
    if((len1=read(file1,comparebuf1,COMPAREBUFLEN))==-1)
    {
     printf("\ndisk IO error!: cannot read file %s\n",path1);
     goto error_close;
    }
    if((len2=read(file2,comparebuf2,COMPAREBUFLEN))==-1)
    {
     printf("\ndisk IO error!: cannot read file %s\n",path2);
     goto error_close;
    };
    if(len1) if(memcmp(comparebuf1,comparebuf2,len1))
    {
     for(i=0;(i<len1)&&(*(comparebuf1+i)==*(comparebuf2+i));i++);
     printf("\nCompare Error %s on pos %X!\n",path1,pos+i);
     goto error_close;
    }
    pos+=len1;
   }
   if(len1!=len2)
   {
    printf("\nDifferent lengths of files %s and %s",path1,path2);
    error_close:
    close(file2);
    close(file1);
    error:
    if(getchar()=='q') return(1);
    break;
   }else{
    printf(" ... compared %ld bytes\n",pos);
    close(file2);
    close(file1);
   };
  }
  done=findnext(&s);
 }

 path1[old_path_len1]=0;
 path2[old_path_len2]=0;
 strcat(path1,"*.*");
 done=findfirst(path1,&s,FA_DIREC | 0x7);
 path1[old_path_len1]=0;


 while(!done)
 {
  if(*s.ff_name != '.')
  {
   if((s.ff_attrib & FA_DIREC)!=0)
   {
    strcat(path1,s.ff_name);
    strcat(path1,"\\");
    path1[old_path_len1+strlen(s.ff_name)+1]=0;
    strcat(path2,s.ff_name);
    strcat(path2,"\\");
    path1[old_path_len2+strlen(s.ff_name)+1]=0;
    TestAllFiles(path1,path2);
    path1[old_path_len1]=0;
    path2[old_path_len2]=0;
   }
  }
  done=findnext(&s);
 }
 path1[old_path_len1]=0;
 path2[old_path_len2]=0;

 return 0;
}

int main(int argc,char *argv[])
{
 if(argc<3)
 {
  printf("usage: cmpdisk <d>: <d>:\n");
  return(0);
 };

 {
  char path1[255],path2[255];
  strcpy(path1,argv[1]);
  strcpy(path2,argv[2]);
  TestAllFiles(path1,path2);
 };
 return(0);
};