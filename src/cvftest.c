#include<stdio.h>
#include<string.h>

int main(int argc, char*argv[])
{ unsigned char sb[512];
  int i;
  FILE*f;
  
  if(argc!=2)
  { fprintf(stderr,"usage: %s filename\n",argv[0]);
    fprintf(stderr,"detect CVFs according to header\n");
    fprintf(stderr,"exit code: 0 = CVF detected, 1 = no CVF, >1 = some error occured\n"); 
    exit(20);
  }
  
  if(strcmp(argv[1],"-")==0)f=stdin;
  else 
  { f=fopen(argv[1],"rb");  
    if(f==NULL)
    { perror("open failed");
      exit(4);
    }
  }
  
  for(i=0;i<512;++i)sb[i]=fgetc(f);
  
  if(fgetc(f)==EOF)
  { perror("error reading file");
    exit(3);
  }
    
  if(strncmp(sb+3,"MSDBL6.0",8)==0||strncmp(sb+3,"MSDSP6.0",8)==0
     ||strncmp(sb,"STACKER",7)==0)return 0;
     
  return 1;
}
