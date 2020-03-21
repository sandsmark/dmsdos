/*
dblspace_methsq.c

DMSDOS CVF-FAT module: drivespace SQ compression/decompression routines.

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

#ifdef __DMSDOS_DAEMON__
#include<malloc.h>
#include<string.h>
#include<asm/unaligned.h>
#include<asm/types.h>
#include <asm/byteorder.h>
#define MALLOC malloc
#define FREE free
int printk(const char * fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern int debug;
#undef LOG_DECOMP
#define LOG_DECOMP if(debug)printk
#endif

#ifdef __DMSDOS_LIB__
/* some interface hacks */
#include"lib_interface.h"
#include<malloc.h>
#include<string.h>
#endif

#ifdef DMSDOS_CONFIG_DRVSP3

#ifdef __GNUC__
#define INLINE  static inline
#else
/* non-gnu compilers may not like inline */
#define INLINE static
#endif

/* store and load __u16 in any byteorder on any */
/* address (odd or even).                       */
/* this is problematic on architectures,        */
/* which cannot do __u16 access to odd address. */
/* used for temporary storage of LZ intercode.  */
#define C_ST_u16(p,v) {put_unaligned(v,p);p=(__u16*)p+1;}
#define C_LD_u16(p,v) {v=get_unaligned(p);p=(__u16*)p+1;}

/* high speed compare and move routines */
#if defined(__GNUC__) && defined(__i386__) && defined(USE_ASM)
#define USE_GNU_ASM_i386

#ifdef GAS_XLAT_BUG
#define XLAT "xlatl\n\t"
#else
#define XLAT "xlat\n\t"
#endif

/* copy block, overlaping part is replaced by repeat of previous part */
/* pointers and counter are modified to point after block */
#define M_MOVSB(D,S,C) \
__asm__ /*__volatile__*/(\
	"cld\n\t" \
	"rep\n\t" \
	"movsb\n" \
	:"=D" (D),"=S" (S),"=c" (C) \
	:"0" (D),"1" (S),"2" (C) \
	:"memory")

/* compare blocks, overlaping repeat test */
/* pointers and counter are modified to point after block */
/* D and S points to first diff adr */
#define M_FIRSTDIFF(D,S,C) \
__asm__ /*__volatile__*/(\
	"cld\n\t" \
	"rep\n\t" \
	"cmpsb\n\t" \
	"je 1f\n\t" \
	"dec %0\n\t" \
	"dec %1\n\t" \
	"1:\n" \
	:"=D" (D),"=S" (S),"=c" (C) \
	:"0" (D),"1" (S),"2" (C) \
	)


#else

#ifdef __GNUC__
/* non-gnu compilers may not like warning directive */
#warning USE_GNU_ASM_I386 not defined, using "C" equivalent
#endif

#define M_MOVSB(D,S,C) for(;(C);(C)--) *((__u8*)(D)++)=*((__u8*)(S)++)
#define M_FIRSTDIFF(D,S,C) for(;(*(__u8*)(D)==*(__u8*)(S))&&(C);\
			   (__u8*)(D)++,(__u8*)(S)++,(C)--)

#endif

#if !defined(cpu_to_le16)
    /* for old kernel versions - works only on i386 */
    #define le16_to_cpu(v) (v)
    #define cpu_to_le16(v) (v)
#endif

/*==============================================================*/
/* bitstream reading */

/* for reading and writting from/to bitstream */
typedef
 struct {
   __u32 buf;   /* bit buffer */
     int pb;    /* already readed bits from buf */
   __u16 *pd;   /* first not readed input data */
   __u16 *pe;   /* after end of data */
 } bits_t;

const unsigned sq_bmsk[]=
   {0x0,0x1,0x3,0x7,0xF,0x1F,0x3F,0x7F,0xFF,
    0x1FF,0x3FF,0x7FF,0xFFF,0x1FFF,0x3FFF,0x7FFF,0xFFFF};

/* read next 16 bits from input */
#define RDN_G16(bits) \
   { \
    (bits).buf>>=16; \
    (bits).pb-=16; \
    if((bits).pd<(bits).pe) \
    { \
     (bits).buf|=((__u32)(le16_to_cpu(*((bits).pd++))))<<16; \
    }; \
   }

/* prepares at least 16 bits for reading */
#define RDN_PR(bits,u) \
   { \
    if((bits).pb>=16) RDN_G16(bits); \
    u=(bits).buf>>(bits).pb; \
   }

/* initializes reading from bitstream */
INLINE void sq_rdi(bits_t *pbits,void *pin,unsigned lin)
{
  pbits->pb=32;
  pbits->pd=(__u16*)pin;
  pbits->pe=pbits->pd+((lin+1)>>1);
};

/* reads n<=16 bits from bitstream *pbits */
INLINE unsigned sq_rdn(bits_t *pbits,int n)
{
  unsigned u;
  RDN_PR(*pbits,u);
  pbits->pb+=n;
  u&=sq_bmsk[n];
  return u;
};

/*==============================================================*/
/* huffman decoding */

#define MAX_SPDA_BITS   10
#define MAX_SPDA_LEN    (1<<MAX_SPDA_BITS)
#define MAX_BITS        16
#define MAX_CODES       0x140
#define OUT_OVER        0x100

typedef
 struct {
  __s8 ln;                      /* character lens .. for tokens -0x40 */
  __u8 ch;                      /* character/token code */
 }huf_chln_t;

typedef
 struct {
  unsigned cd_ln[MAX_BITS+1];   /* distribution of bits */
  unsigned cd_ch[MAX_BITS+1];   /* distribution of codes codes */
  int  bn;                      /* chln array convert max bn bits codes */
  huf_chln_t chln1[MAX_CODES];  /* for codes with more than bn bits */
  huf_chln_t chln[0];           /* character codes decode array length SPDA_LEN */
 }huf_rd_t;

#define HUF_RD_SIZE(SPDA_LEN) \
	((sizeof(huf_rd_t)+SPDA_LEN*sizeof(huf_chln_t)+3)&~3)

const __u8 swap_bits_xlat[]=
	  {0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,
	   0x50,0xd0,0x30,0xb0,0x70,0xf0,0x08,0x88,0x48,0xc8,
	   0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,
	   0x78,0xf8,0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,
	   0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,0x0c,0x8c,
	   0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,
	   0x3c,0xbc,0x7c,0xfc,0x02,0x82,0x42,0xc2,0x22,0xa2,
	   0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
	   0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,
	   0x5a,0xda,0x3a,0xba,0x7a,0xfa,0x06,0x86,0x46,0xc6,
	   0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,
	   0x76,0xf6,0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,
	   0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,0x01,0x81,
	   0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,
	   0x31,0xb1,0x71,0xf1,0x09,0x89,0x49,0xc9,0x29,0xa9,
	   0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
	   0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,
	   0x55,0xd5,0x35,0xb5,0x75,0xf5,0x0d,0x8d,0x4d,0xcd,
	   0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,
	   0x7d,0xfd,0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,
	   0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,0x0b,0x8b,
	   0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,
	   0x3b,0xbb,0x7b,0xfb,0x07,0x87,0x47,0xc7,0x27,0xa7,
	   0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
	   0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,
	   0x5f,0xdf,0x3f,0xbf,0x7f,0xff};

/* swap 16 bits order */
INLINE unsigned swap_bits_order_16(unsigned d)
{ unsigned r;
  #ifdef USE_GNU_ASM_i386
    __asm__ (
	XLAT
	"xchgb	%%al,%%ah\n\t"
	XLAT
	:"=a"(r):"0"(d),"b"(swap_bits_xlat)); 
  #else
    r=((unsigned)swap_bits_xlat[(__u8)d])<<8;
    r|=swap_bits_xlat[(__u8)(d>>8)];
  #endif
  return r;
};

/* swap bit order */
INLINE unsigned swap_bits_order(unsigned d,int n)
{ unsigned r=0;
  while(n--) { r<<=1;r|=d&1;d>>=1;};
  return r;
};

/* initializes huffman conversion structure *phuf for m codes,
   *ca code and token bit lengths, ends with 0xFF, 
   bn predicated maximal bit length */
int sq_rdhufi(huf_rd_t *phuf,int m,int bn,__u8 *ca)
{
 if(bn>MAX_SPDA_BITS) bn=MAX_SPDA_BITS;
 phuf->bn=bn;
 {
  int i;
  unsigned u,us,ut;
  memset(phuf->cd_ln,0,sizeof(phuf->cd_ln));i=0;
  while((u=ca[i++])<=MAX_BITS) phuf->cd_ln[u]++;
  memset(phuf->cd_ch,0,sizeof(phuf->cd_ch));
  phuf->cd_ln[0]=0;us=0;ut=0;
  for(i=1;i<=MAX_BITS;i++)
  {
   u=phuf->cd_ln[i];phuf->cd_ln[i]=ut;
   phuf->cd_ch[i]=us;ut+=u;us+=u;us<<=1;
  };
  /* if suceed, codespace should be full else report error */ 
  if (us&((1<<MAX_BITS)-1)) 
   if(us!=1)return(0); /* exeption, zip 2.0 allows one code one bit long */
 };
 {
  int i,ln,l,ch,sh,cod;
  for(i=0;(l=ln=ca[i])<=MAX_BITS;i++) if(ln)
  {
   sh=(bn-ln);
   cod=(phuf->cd_ch[ln])++;
   cod=swap_bits_order_16(cod)>>(16-ln);
   if(i<m) ch=i; else {ch=i-m+1;ln-=0x40;};
   if (sh>0)
   {
    sh=1<<sh;
    l=1<<l;
    while(sh--)
    {
      phuf->chln[cod].ch=ch;
      phuf->chln[cod].ln=ln;
      cod+=l;
    };
   } else if (sh==0) {
    phuf->chln[cod].ch=ch;
    phuf->chln[cod].ln=ln;
   } else {
    cod&=sq_bmsk[bn];
    phuf->chln[cod].ch=0x00;
    phuf->chln[cod].ln=-0x40;
    cod=(phuf->cd_ln[l])++;
    phuf->chln1[cod].ch=ch;
    phuf->chln1[cod].ln=ln;
   };
  };
  /* if suceed ln should be 0xFF */
 };
 return(1);
};

/* read and huffman decode of characters, stops on tokens or buffer ends */
INLINE unsigned sq_rdh(bits_t *pbits,const huf_rd_t *phuf,__u8 **pout,__u8 *pend)
{
 unsigned ch;
 unsigned bmsk=sq_bmsk[phuf->bn];

 while(1)
 {while(1)
  {if(pbits->pb>=16)
    RDN_G16(*pbits);
   if (*pout>=pend) return OUT_OVER;
   ch=(pbits->buf>>pbits->pb)&bmsk;
   if((pbits->pb+=phuf->chln[ch].ln)<0) break;
   *((*pout)++)=phuf->chln[ch].ch;

   if(pbits->pb<16)
   {if (*pout>=pend) return OUT_OVER;
    ch=(pbits->buf>>pbits->pb)&bmsk;
    if((pbits->pb+=phuf->chln[ch].ln)<0) break;
    *((*pout)++)=phuf->chln[ch].ch;

    if(pbits->pb<16)
    {if (*pout>=pend) return OUT_OVER;
     ch=(pbits->buf>>pbits->pb)&bmsk;
     if((pbits->pb+=phuf->chln[ch].ln)<0) break;
     *((*pout)++)=phuf->chln[ch].ch;
    };
   };
  };

  ch=phuf->chln[ch].ch;
  pbits->pb+=0x40; if(ch--) return ch;
  /* code longer than phuf->bn */
  if(pbits->pb>=16) RDN_G16(*pbits);
  ch=swap_bits_order_16((__u16)(pbits->buf>>pbits->pb));
  {
   int i;
   i=phuf->bn;
   do
    i++;
   while((phuf->cd_ch[i]<=(ch>>(16-i)))&&(i<MAX_BITS));
   ch=((ch>>(16-i)))-phuf->cd_ch[i]+phuf->cd_ln[i];
  };
  if((pbits->pb+=phuf->chln1[ch].ln)<0) 
  {pbits->pb+=0x40;
   return phuf->chln1[ch].ch-1;
  };
  *((*pout)++)=phuf->chln1[ch].ch;
 };
};

/* read one huffman encoded value */
INLINE unsigned sq_rdh1(bits_t *pbits,const huf_rd_t *phuf)
{unsigned ch;
 if(pbits->pb>=16) RDN_G16(*pbits);
 ch=(pbits->buf>>pbits->pb)&sq_bmsk[phuf->bn];
 if((pbits->pb+=phuf->chln[ch].ln)>=0) return phuf->chln[ch].ch;
 ch=phuf->chln[ch].ch;
 pbits->pb+=0x40; if(ch) return ch+0x100-1;
 ch=swap_bits_order_16((__u16)(pbits->buf>>pbits->pb));
 {int i;
  i=phuf->bn;
  do
   i++;
  while((phuf->cd_ch[i]<=(ch>>(16-i)))&&(i<MAX_BITS));
  ch=((ch>>(16-i)))-phuf->cd_ch[i]+phuf->cd_ln[i];
 };
 if((pbits->pb+=phuf->chln1[ch].ln)>=0) return phuf->chln1[ch].ch;
 pbits->pb+=0x40;
 return phuf->chln1[ch].ch+0x100-1;
};

/*==============================================================*/
/* SQ decompression */

/* index conversion table for first bitlen table */
const int code_index_1[]={0x10,0x11,0x12,0x00,0x08,0x07,0x09,0x06,0x0A,0x05,
			  0x0B,0x04,0x0C,0x03,0x0D,0x02,0x0E,0x01,0x0F};

const unsigned sqt_repbas[]={
	0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0D,
	0x0F,0x11,0x13,0x17,0x1B,0x1F,0x23,0x2B,0x33,0x3B,
	0x43,0x53,0x63,0x73,0x83,0xA3,0xC3,0xE3,0x102,0x00,0x00};

const unsigned char sqt_repbln[]={
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,
	0x01,0x01,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,
	0x04,0x04,0x04,0x04,0x05,0x05,0x05,0x05,0x00,0x63,0x63};

const unsigned sqt_offbas[]={
	0x0001,0x0002,0x0003,0x0004,0x0005,0x0007,0x0009,0x000D,
	0x0011,0x0019,0x0021,0x0031,0x0041,0x0061,0x0081,0x00C1,
	0x0101,0x0181,0x0201,0x0301,0x0401,0x0601,0x0801,0x0C01,
	0x1001,0x1801,0x2001,0x3001,0x4001,0x6001,0x0000,0x0000};

const unsigned char sqt_offbln[]={
	0x00,0x00,0x00,0x00,0x01,0x01,0x02,0x02,
	0x03,0x03,0x04,0x04,0x05,0x05,0x06,0x06,
	0x07,0x07,0x08,0x08,0x09,0x09,0x0A,0x0A,
	0x0B,0x0B,0x0C,0x0C,0x0D,0x0D,0x00,0x00};

#if defined(__DMSDOS_LIB__)

int sq_dec(void* pin,int lin, void* pout, int lout, int flg)
{ 
  __u8 *p, *pend, *r;
  unsigned u, u1, repoffs, replen;
  int i,bn_max;
  bits_t bits;
  int final_flag;
  int count_1;
  int count_2;
  int count_3;
  int method;
  unsigned mask;
  __u8 *code_bln;       /* bitlengths of char, tokens and rep codes [0x150] */
  huf_rd_t *huf1,*huf2; /* tables for huffman decoding */
  char *work_mem;

  sq_rdi(&bits,pin,lin);
  p=(__u8*)pout;pend=p+lout;
  if((sq_rdn(&bits,8)!='S')||(sq_rdn(&bits,8)!='Q'))
  { printk(KERN_ERR "DMSDOS: Data are not SQ compressed\n");
    return(0);
  };
  u=sq_rdn(&bits,16);
  LOG_DECOMP("DMSDOS: sq_dec: version %X\n",u);
  /* allocating work memory */
  work_mem=(char*)MALLOC(0x150+HUF_RD_SIZE(MAX_SPDA_LEN)+HUF_RD_SIZE(256));
  if(!work_mem) 
    {printk(KERN_ERR "DMSDOS: sq_dec: out of memory!\n");return 0;};
  code_bln=work_mem;
  huf1=(huf_rd_t*)(work_mem+0x150);
  huf2=(huf_rd_t*)(work_mem+0x150+HUF_RD_SIZE(MAX_SPDA_LEN));
  do
  { final_flag=sq_rdn(&bits,1); LOG_DECOMP("DMSDOS: final_flag %d\n",final_flag);
    method=sq_rdn(&bits,2);     LOG_DECOMP("DMSDOS: method %d\n",method);
    switch(method)
    {
      case 0: 
	printk(KERN_NOTICE "DMSDOS: dec_sq: submethod not tested - raw read\n");
	/* go to byte boundary */
	/* read 16 bits - count of raw bytes */
	sq_rdn(&bits,(8-bits.pb)&7);
	replen=sq_rdn(&bits,16);
	if (replen+sq_rdn(&bits,16)!=0xFFFF) {FREE(work_mem);return 0;};
	r=(__u8*)bits.pd-(32-bits.pb)/8;
	if(r+replen>(__u8*)bits.pe) {FREE(work_mem);return 0;};
	if(p+replen>pend) {FREE(work_mem);return 0;};
	M_MOVSB(p,r,replen); /* copy/repeat function */
	if((unsigned)r&1) bits.pb=32;
	else {bits.pb=32+8;r--;};
#if 0        
	/* some compilers seem to be confused by this (???) */
	bits.pd=(typeof(bits.pd))r;
#else
	bits.pd=(__u16*)r;
#endif
	break;

      case 1: 
	printk(KERN_NOTICE "DMSDOS: sq_dec: submethod not tested - fixed huffman\n");
	/* 0x90*8 0x70*9 0x18*7 8*8 sqt_repbln sqt_repbas 0x101 0x120h */
	/* 0x1E*5 offset sqt_offbln sqt_offbas 0 0x1Eh */
	bn_max=9;
	count_1=0x120;
	count_2=0x20;
	i=0;
	while(i<0x90)  code_bln[i++]=8;
	while(i<0x100) code_bln[i++]=9;
	while(i<0x118) code_bln[i++]=7;
	while(i<0x120) code_bln[i++]=8;
	while(i<0x140) code_bln[i++]=5;
	goto case_1_cont;
      
      case 2:
	LOG_DECOMP("DMSDOS: sq_dec: submethod huffman\n");
	count_1=sq_rdn(&bits,5)+0x101;
	LOG_DECOMP("DMSDOS: count_1 %d\n",count_1);
	if(count_1>0x11E) 
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff count_1 too big\n");
	  FREE(work_mem);return(0);
	};
	count_2=sq_rdn(&bits,5)+1;
	LOG_DECOMP("DMSDOS: count_2 %d\n",count_2);
	if(count_2>0x1E) 
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff count_2 too big\n");
	  FREE(work_mem);return(0);
	};
	count_3=sq_rdn(&bits,4)+4;
	LOG_DECOMP("DMSDOS: count_3 %d\n",count_3);
	bn_max=0;
	for(i=0;i<count_3;i++)
	{ u=sq_rdn(&bits,3);
	  code_bln[code_index_1[i]]=u;
	  if(u>bn_max)bn_max=u;
	};
	while(i<19) code_bln[code_index_1[i++]]=0;code_bln[19]=0xFF;
	i=sq_rdhufi(huf1,19,bn_max,code_bln);
	if(!i)
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff error in helper table\n");
	  FREE(work_mem);return 0;
	};
	mask=sq_bmsk[huf1->bn]; u1=0; bn_max=0;
	for(i=0;i<count_1+count_2;)
	{ RDN_PR(bits,u);
	  bits.pb+=huf1->chln[u&mask].ln;
	  u=huf1->chln[u&mask].ch;
	  switch(u)
	  { case 16:            /* 3 to 6 repeats of last */
	      u=sq_rdn(&bits,2)+3;
	      while(u--) code_bln[i++]=u1;
	      break;
	    case 17:            /* 3 to 10 repeats of 0 */
	      u=sq_rdn(&bits,3)+3; u1=0;
	      while(u--) code_bln[i++]=u1;
	      break;
	    case 18:            /* 11 to 139 repeats of 0 */
	      u=sq_rdn(&bits,7)+11; u1=0;
	      while(u--) code_bln[i++]=u1;
	      break;
	    default:
	      code_bln[i++]=u;
	      u1=u;
	      if(u>bn_max) bn_max=u;
	  };
	};

      case_1_cont:
	/* code_bln+count_1     0x96  count_2 sqt_offbln sqt_offbas */
	code_bln[count_1+count_2]=0xFF;
	i=sq_rdhufi(huf2,0x100,8,code_bln+count_1);
	if(!i)
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff error in offset table\n");
	  FREE(work_mem);return 0;
	};
	
	/* code_bln             0x100 count_1 sqt_repbln sqt_repbas */
	code_bln[count_1]=0xFF;
	i=sq_rdhufi(huf1,0x100,bn_max,code_bln);
	if(!i)
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff error in char and len table\n");
	  FREE(work_mem);return 0;
	};
	
	while((u=sq_rdh(&bits,huf1,&p,pend))!=0)
	{ if(u==OUT_OVER){u=sq_rdh1(&bits,huf1)-0x100;break;};
	  u--;
	  replen=sqt_repbas[u]+sq_rdn(&bits,sqt_repbln[u]);
	  u=sq_rdh1(&bits,huf2);
	  repoffs=sqt_offbas[u]+sq_rdn(&bits,sqt_offbln[u]);
	  if(!repoffs)
	  { printk("DMSDOS: sq_dec: bad repoffs !!!\n\n");
	    FREE(work_mem);return(0);
	  };
	  if ((__u8*)pout+repoffs>p) 
	  { repoffs=p-(__u8*)pout;
	    printk(KERN_INFO "DMSDOS: sq_dec: huff offset UNDER\n");
	  };
	  if (p+replen>pend)
	  { replen=pend-p;
	    printk(KERN_INFO "DMSDOS: sq_dec: huff offset OVER\n");
	  };
	  r=p-repoffs; M_MOVSB(p,r,replen); /* copy/repeat function */
	};
      
	if(u)
	{ printk(KERN_INFO "DMSDOS: sq_dec: huff BAD last token %x\n",u);
	  FREE(work_mem);return 0;
	};
	break;
      
      case 3:
	printk(KERN_INFO "DMSDOS: sq_dec: unknown submethod - 3\n");
	FREE(work_mem);
	return(0);      
    };
  } while((!final_flag)&&(p<pend));
  FREE(work_mem);
  return(p-(__u8*)pout);
};

#endif /* __DMSDOS_LIB__ */

/*==============================================================*/
/* bitstream writting */

/* initializes writting to bitstream */
INLINE void sq_wri(bits_t *pbits,void *pin,unsigned lin)
{
 pbits->buf=0;
 pbits->pb=0;
 pbits->pd=(__u16*)pin;
 pbits->pe=pbits->pd+((lin+1)>>1);
};

/* writes n<=16 bits to bitstream *pbits */
INLINE void sq_wrn(bits_t *pbits,unsigned u, int n)
{
  pbits->buf|=(__u32)(u&sq_bmsk[n])<<pbits->pb;
  if((pbits->pb+=n)>=16)
  {
    if(pbits->pd<pbits->pe)
      *(pbits->pd++)=cpu_to_le16((__u16)pbits->buf);
    else if(pbits->pd==pbits->pe) pbits->pd++; /* output overflow */
    pbits->buf>>=16;
    pbits->pb-=16;
  }
}

/*==============================================================*/
/* huffman encoding */

typedef long int count_t;

typedef
 struct {
  count_t cn;
  unsigned ch;
 } ch_tab_t;

typedef
 struct {
  __u16 cod;    /* character code */
  __u16 ln;     /* character len */
 } huf_wr_t;

/*** Generation of character codes ***/

void sq_hsort1(ch_tab_t* ch_tab,int ch_num,int cl, ch_tab_t a)
{
 /* a           eax */
 /* cl          di  */
 int ch;     /* bp  */
 ch_tab_t b; /* ecx */
 ch_tab_t c; /* esi */
 ch=cl*2;
 while(ch<ch_num)
 {
  b=ch_tab[ch-1];c=ch_tab[ch];
  if((c.cn<b.cn)||((c.cn==b.cn)&&(c.ch<=b.ch))) {b=c;ch++;};
  if((b.cn>a.cn)||((b.cn==a.cn)&&(b.ch>=a.ch))) {ch_tab[cl-1]=a;return;}
  ch_tab[cl-1]=b;cl=ch;ch*=2;
 };
 if(ch==ch_num)
 {
  b=ch_tab[ch-1];
  if((b.cn<a.cn)||((b.cn==a.cn)&&(b.ch<a.ch)))
   {ch_tab[cl-1]=b;cl=ch;ch*=2;};
 };
 ch_tab[cl-1]=a;
};

int sq_huffman(count_t* ch_cn,__u8* ch_blen,unsigned* ch_blcn,int cod_num,ch_tab_t *ch_tab)
{
 int i,ch_num,cl;
 ch_tab_t a;
 ch_tab_t b;

 redo_reduced:
 ch_num=0;
 for(i=0;i<cod_num;i++) if(ch_cn[i])
  {ch_tab[ch_num].cn=ch_cn[i];ch_tab[ch_num].ch=i|0x800;ch_num++;};
 ch_tab[ch_num].ch=0;
 if(ch_num==0) 
 {
  ch_tab[0].ch=0x800;
  ch_tab[0].cn=1;
  ch_num++;
 }
 if(ch_num==1)
 {
  ch_tab[ch_num]=ch_tab[ch_num-1];
  ch_tab[ch_num].ch&=0x801;
  ch_tab[ch_num].ch^=1;ch_num++;
 };
 cl=ch_num/2;
 while(cl>1)
 {
  sq_hsort1(ch_tab,ch_num,cl,ch_tab[cl-1]);
  cl--;
 };

 cl=ch_num; a=ch_tab[0];
 while(cl>2)
 {
  sq_hsort1(ch_tab,cl,1,a);
  b=ch_tab[0];
  a=ch_tab[--cl];
  ch_tab[cl].ch=b.ch;
  sq_hsort1(ch_tab,cl,1,a);
  a=ch_tab[0];
  ch_tab[cl].cn=a.ch;
  if(a.ch<=b.ch) {a.ch=b.ch;};
  a.ch=(a.ch&0x7800)+cl+0x800;
  if(a.ch>=0x8000u)
  {
   printk("DMSDOS: sq_huffman: Problems with number of bits\n");
   for(i=0;i<cod_num;i++) ch_cn[i]=(ch_cn[i]+1)>>1;
   goto redo_reduced;
  };
  a.ch+=0x8000u;
  a.cn+=b.cn;
 };
 ch_tab[1].cn=a.ch;

 {
  int st[MAX_BITS+2];
  int k=0,l=1,blen=0;

  memset(ch_blcn,0,sizeof(ch_blcn[0])*(MAX_BITS+1));
  memset(ch_blen,0,sizeof(ch_blen[0])*cod_num);
  while(1)
  {
   do
   {
    k|=0x4000;
    do
    {
     st[blen]=k;
     blen++;
     k=l&0x7FF;
     l=ch_tab[k].ch&0x87FF;
    }while(l&0x8000);
    ch_blen[l]=blen;
    ch_blcn[blen]++;
    l=ch_tab[k].cn&0x87FF;
   }while(l&0x8000);
   do
   {
    ch_blen[l]=blen;
    ch_blcn[blen]++;
    do
    {
     if(!--blen) goto code_done;
     k=st[blen];
    }while(k&0x4000);
    l=ch_tab[k].cn&0x87FF;
   }while(!(l&0x8000));
  };
  code_done:;
 };
 return(0);
};

INLINE int sq_wrhufi(huf_wr_t *phuf, __u8* ch_blen, 
		     unsigned* ch_blencn,int cod_num)
{
 unsigned i,u,t,blen;
 u=0;
 for(i=0;i<=MAX_BITS;i++) {u<<=1;t=u;u+=ch_blencn[i];ch_blencn[i]=t;};
 if(u!=1u<<MAX_BITS) return(1);
 for(i=0;i<cod_num;i++)
 {
  if((blen=ch_blen[i])!=0)
  {
   phuf[i].cod=swap_bits_order_16(ch_blencn[blen]++)>>(16-blen);
   phuf[i].ln=blen;
  };
 };
 return(0);
};

INLINE void sq_wrh(bits_t *pbits,const huf_wr_t *phuf,const unsigned ch)
{
 sq_wrn(pbits,phuf[ch].cod,phuf[ch].ln);
};


/*==============================================================*/
/* SQ compression */

typedef __u8* hash_t;

#define TK_END   0x00
#define TK_CHRS  0xF0
#define TKWR_CHRS(p,v) {if(v<15) *(p++)=TK_CHRS+(__u8)v;\
			else {*(p++)=TK_CHRS+15;C_ST_u16(p,v);};}

#define HASH_TAB_ENT    (1<<10)
#define HASH_HIST_ENT   (1<<12)
#define MAX_OFFS        32768
#define MAX_OFFS_BLN8   1024
#define MIN_REP         3
#define MAX_REP         258

/* definition of data hash function, it can use max 3 chars */
INLINE unsigned sq_hash(__u8 *p)
{
 return (((__u16)p[0]<<2)^((__u16)p[1]<<4)^(__u16)p[2])&(HASH_TAB_ENT-1);
};

/* store hash of chars at *p in hash_tab, previous occurence is stored */
/* in hash_hist, which is indexed by next hash positions */
/* returns previous occurence of same hash as is hash of chars at *p  */
INLINE hash_t sq_newhash(__u8 *p,hash_t* hash_tab,hash_t* hash_hist,unsigned hist_mask)
{
 hash_t* hash_ptr;
 hash_t  hash_cur;
 hash_ptr=hash_tab+sq_hash(p);
 hash_cur=*hash_ptr;
 *hash_ptr=p;
 *(hash_hist+((unsigned)p&hist_mask))=hash_cur;
 return(hash_cur);
};

/* binary seeking of nearest less or equal base value */
INLINE unsigned find_token(int token,const unsigned *tab_val,int tab_len)
{
 int half;
 int beg=0;
 do
 {
  half=tab_len>>1;
  if(tab_val[beg+half]>token) tab_len=half;
  else {beg+=half;tab_len-=half;};
 }
 while(tab_len>1);
 return beg;
};

/* finds repetitions in *pin and writes intermediate code to *pout */
unsigned sq_complz(void* pin,int lin,void* pout,int lout,int flg,
		count_t* ch_cn, count_t* offs_cn, void *work_mem)
{
 int try_count;         /* number of compares to find best match */
 int hash_skiped;       /* last bytes of repetition are hashed too */
 hash_t *hash_tab;      /* [HASH_TAB_ENT] */
	/* pointers to last occurrence of same hash, index hash */
 hash_t *hash_hist;     /* [HASH_HIST_ENT] */
	/* previous occurences of hash, index actual pointer&hist_mask */ 
 unsigned hist_mask=(HASH_HIST_ENT-1); /* mask for index into hash_hist */
 __u8 *pi, *po, *pc, *pd, *pend, *poend;
 hash_t hash_cur;
 unsigned cn;
 unsigned max_match, match, token;
 int      try_cn;
 hash_t   max_hash=NULL;

 int     delay_count=0;  /* test next # characters for better match */
 int     delay_cn;
 int     delay_best;
 
 hash_tab/*[HASH_TAB_ENT]*/=(hash_t*)work_mem;
 hash_hist/*[HASH_HIST_ENT]*/=(hash_t*)work_mem+HASH_TAB_ENT;

 try_count=2<<((flg>>2)&0x7); /* number of tested entries with same hash */
 hash_skiped=((flg>>5)+1)&0xF;/* when rep found last # bytes are procesed */

 if(flg&0x4000)
 { 
  /* maximal compression */
  delay_count=2;
  try_count*=4;
  hash_skiped*=4;
 };

 pi=(__u8*)pin;
 po=(__u8*)pout;
 if(!lin) return(0);
 pend=pi+(lin-1);
 if(lout<0x20) return(0);     /* some minimal space for lz interform buffer */
 poend=po+(lout-0x20);
 for(cn=0;cn<HASH_TAB_ENT;cn++) hash_tab[cn]=pend; /* none ocurence of hash */
 for(cn=0;cn<=hist_mask;cn++) hash_hist[cn]=pend;  /* should not be needed */
 pend--;                      /* last two bytes cannot be hashed */
 cn=0;
 while(pi<pend)
 {
  hash_cur=sq_newhash(pi,hash_tab,hash_hist,hist_mask);
  /* goto single_char; */ /* to by pass LZ for tests */
  if(hash_cur>=pi) goto single_char;
   try_cn=try_count;
   max_match=MIN_REP-1;
   do{
    if(pi-hash_cur>MAX_OFFS) break;   /* longer offsets are not allowed */
    if((hash_cur[max_match]==pi[max_match])&&
       (hash_cur[max_match-1]==pi[max_match-1])&&
       (hash_cur[0]==pi[0])&&(hash_cur[1]==pi[1]))
       /* pi[2]=hash_cur[2] from hash function */
    {
     match=pend-pi;             /* length of rest of data */
     if(match>MAX_REP-2) match=MAX_REP-2;
     pd=pi+2;
     pc=hash_cur+2;
     M_FIRSTDIFF(pd,pc,match);  /* compare */
     match=pd-pi;               /* found match length */
     if((match>max_match)&&((match>3)||(pi-hash_cur<=MAX_OFFS_BLN8)))
     {
      max_hash=hash_cur;        /* found maximal hash */
      max_match=match;
      if(match==MAX_REP)break;  /* longer match cannot be encoded */
      if(pd>pend+1)break;       /* match to end of block */
     };
    };
    pc=hash_cur;
   }while(--try_cn&&((hash_cur=hash_hist[(unsigned)pc&hist_mask])<pc));
   if(max_match<MIN_REP) goto single_char;
 
   /* tests if better matchs on next characters */
   delay_cn=0;
   if(delay_count)
   { 
    delay_best=0;
    while((delay_cn<delay_count)&&(pi+max_match<pend)&&
	  (max_match<0x100))
    {
     pi++;delay_cn++;
     hash_cur=sq_newhash(pi,hash_tab,hash_hist,hist_mask);
     try_cn=try_count;
     if (hash_cur<pi) do
     {
      if(pi-hash_cur>MAX_OFFS) break;  /* longer offsets are not allowed */
      if((hash_cur[max_match]==pi[max_match])&&
	 (hash_cur[max_match-1]==pi[max_match-1])&&
	 (hash_cur[0]==pi[0])&&(hash_cur[1]==pi[1])&&
	 /* pi[2]=hash_cur[2] from hash function */
	 (hash_cur!=max_hash+delay_cn-delay_best))
      {  /* do not test actual max match */
       match=pend-pi;           /* length of rest of data */
       if(match>MAX_REP-2) match=MAX_REP-2;
       pd=pi+2;
       pc=hash_cur+2;
       M_FIRSTDIFF(pd,pc,match);/* compare */
       match=pd-pi;             /* found match length */
       if((match>max_match+delay_cn)&&((match>3)||(pi-hash_cur<=MAX_OFFS_BLN8)))
       {
	max_hash=hash_cur;max_match=match; /* find maximal hash */
	delay_best=delay_cn;
	if(match==MAX_REP)break;/* longer match cannot be encoded */
	if(pd>pend+1)break;     /* match to end of block */
       };
      };
      pc=hash_cur;
     }while(--try_cn&&((hash_cur=hash_hist[(unsigned)pc&hist_mask])<pc)); 
    };
    if(delay_best) 
      LOG_DECOMP("DMSDOS: sq_complz: Delayed match %i is better\n",delay_best);
    pi-=delay_cn;
    delay_cn-=delay_best;
    while(delay_best)
    {
     delay_best--;
     ch_cn[*(pi++)]++;
     if(++cn>=0x8000u)
      {TKWR_CHRS(po,0x8000u);cn-=0x8000u;if(poend<po) return(0);};
    } 
   };

   if(cn) TKWR_CHRS(po,cn);
   cn=pi-max_hash;   /* history offset */
   pi+=max_match;    /* skip repeated bytes */

   /* store information about match len into *po */
   token=find_token(max_match,sqt_repbas,29);   /* for max match */
   ch_cn[token+0x101]++;
   *po++=token+1;
   if(sqt_repbln[token]) *po++=max_match-sqt_repbas[token];
   /* store information about match offset into *po */
   token=find_token(cn,sqt_offbas,30);  /* for history offset */
   offs_cn[token]++;
   *po++=token;
   if(sqt_offbln[token])
   {
    if(sqt_offbln[token]<=8) 
     *po++=cn-sqt_offbas[token];
    else
     C_ST_u16(po,cn-sqt_offbas[token]);
   };
   if(hash_skiped&&(pi<pend))
   {
    max_match-=delay_cn;
    if(--max_match>hash_skiped) max_match=hash_skiped;
    pi-=max_match;
    while(max_match--) sq_newhash(pi++,hash_tab,hash_hist,hist_mask);
   };
   if(poend<po) return(0);
   cn=0;
   continue;
  single_char:
   ch_cn[*(pi++)]++;
   if(++cn>=0x8000u)
    {TKWR_CHRS(po,0x8000u);cn-=0x8000u;if(poend<po) return(0);};
 };

 pend+=2;
 while(pi!=pend) {ch_cn[*(pi++)]++;cn++;};
 if(cn)
 {
  if(cn>=0x8000u) {TKWR_CHRS(po,0x8000u);cn-=0x8000u;};
  TKWR_CHRS(po,cn);
 };

 ch_cn[TK_END+0x100]++;
 *po++=TK_END;
 return(po-(__u8*)pout);
};

/*** Main compression routine ***/

__u16 sq_comp_rat_tab[]=
  {0x7F9,0x7F9,0x621,0x625,
   0x665,0x669,0x6E9,0x6ED,
   0x7D1,0x7D9,0x6E9,0x47D9,
   0x46E9};                     /* compression ratio to seek lengths */

typedef
 struct{
  count_t ch_cn[0x120];         /* counts of characters and rep length codes */
  count_t offs_cn[0x20];        /* counts of offset codes */
  union {
   struct {
    __u8 ch_blen[0x120+0x20];   /* bitlengths of character codes and tokens */
    __u8 code_buf[0x120+0x20];  /* precompressed decompression table */
    ch_tab_t ch_tab[0x120+0x20];/* temporrary table for huffman */
    huf_wr_t ch_huf[0x120];     /* character and token encoding table */
    huf_wr_t offs_huf[0x20];    /* repeat encoding table */
   } a;
   hash_t lz_tabs[HASH_TAB_ENT+HASH_HIST_ENT]; 
  } a;
 }sq_comp_work_t;

int sq_comp(void* pin,int lin, void* pout, int lout, int flg)
{
 count_t *ch_cn;        /* [0x120] counts of characters and rep length codes */
 count_t *offs_cn;      /* [0x20] counts of offset codes */
 unsigned lz_length;    /* length of intermediate reprezentation */
 __u8* lz_pos;          /* possition of intermediate data in pout */

 __u8 *ch_blen;         /* [0x120] bitlengths of character codes and tokens */
 __u8 *offs_blen;       /* [0x20] bitlengths of ofset codes are stored in */
			/* end of ch_blen */
 unsigned ch_blcn[MAX_BITS+1]; /* counts of bitlengths of chars */
 unsigned offs_blcn[MAX_BITS+1]; /* counts of bitlengths of offs */
 huf_wr_t *ch_huf;      /* [0x120] character and token encoding table */
 huf_wr_t *offs_huf;    /* [0x20] repeat encoding table */

  ch_tab_t *ch_tab;     /* [0x120+0x20] temporrary table for huffman */
 __u8 *code_buf;        /* [0x120+0x20] precompressed decompression table */
 count_t code_cn[0x20];
 __u8 code_blen[0x20];
 unsigned code_blcn[MAX_BITS+1];
 unsigned code_buf_len;
 sq_comp_work_t *work_mem=NULL;

 int  count_1, count_2, count_3; 
 bits_t bits;
 int  i;


 /* allocating work memory */
 work_mem=(sq_comp_work_t*)MALLOC(sizeof(sq_comp_work_t));
 if(!work_mem) 
 { printk("DMSDOS: sq_comp: Not enough memory\n");
   return 0;
 };

 ch_cn=work_mem->ch_cn;
 offs_cn=work_mem->offs_cn;
 ch_blen=work_mem->a.a.ch_blen;
 code_buf=work_mem->a.a.code_buf;
 ch_tab=work_mem->a.a.ch_tab;
 ch_huf=work_mem->a.a.ch_huf;
 offs_huf=work_mem->a.a.offs_huf;
 memset(ch_cn,0,sizeof(work_mem->ch_cn));
 memset(offs_cn,0,sizeof(work_mem->offs_cn));

 /* find repetitions in input data block */
 lz_length=sq_complz(pin,lin,pout,lout,sq_comp_rat_tab[flg&0xf],
		     ch_cn,offs_cn,work_mem->a.lz_tabs);
 LOG_DECOMP("DMSDOS: sq_comp: lz_length %d\n",lz_length);
 if(lz_length==0) {FREE(work_mem);return(0);};

 /* move intermediate data to end of output buffer */
 lz_pos=(__u8*)pout+lout-lz_length;
 memmove(lz_pos,pout,lz_length);

 {
  count_1=0x11E;
  while(!ch_cn[count_1-1])count_1--;
  count_2=0x1E;
  while(!offs_cn[count_2-1]&&(count_2>2))count_2--;
  /* offset bitlengths are stored exactly after count_1 character bitlengths */
  offs_blen=ch_blen+count_1;
  sq_huffman(ch_cn,ch_blen,ch_blcn,count_1,ch_tab);
  sq_huffman(offs_cn,offs_blen,offs_blcn,count_2,ch_tab);

  LOG_DECOMP("DMSDOS: sq_comp: count_1 %d\n",count_1);
  LOG_DECOMP("DMSDOS: sq_comp: count_2 %d\n",count_2);
 }

 {
  __u8 *pi=ch_blen;
  __u8 *pe=ch_blen+count_1+count_2;
  __u8 *po=code_buf;
  int code, rep;

  for(code=19;code--;) code_cn[code]=0;
  code=0;
  while(pi<pe)
  {  
   if(*pi==0)
   {
    code=0;
    rep=1; pi++;
    while((pi<pe)&&(rep<138)&&(*pi==0)) {pi++;rep++;}
    if (rep<=2) {code_cn[0]+=rep; while(rep--) *po++=0;}
    /* code 17 - 3 to 10 repeats of 0   - (3)+3  */
    else if(rep<=10) {code_cn[17]++;*po++=17;*po++=rep-3;}
    /* code 18 - 11 to 139 repeats of 0 - (7)+11 */
    else {code_cn[18]++;*po++=18;*po++=rep-11;}
    continue;
   }
   if(*pi==code)
   {
    rep=1; pi++;
    while((pi<pe)&&(rep<6)&&(*pi==code)) {pi++;rep++;}
    if (rep<=2) {code_cn[code]+=rep; while(rep--) *po++=code;}
    /* code 16 - 3 to 6 repeats of last - (2)+3  */
    else {code_cn[16]++;*po++=16;*po++=rep-3;}
    continue;
   }
   code=*pi++;
   *po++=code;
   code_cn[code]++;
  };
  code_buf_len=po-code_buf;

  do{
   sq_huffman(code_cn,code_blen,code_blcn,19,ch_tab);
   code=1;
   /* not elegant way to limit helper table blen by 7 */
   for(i=0;i<19;i++) if(code_blen[i]>7) code=0;
   if(code) break;
   for(i=0;i<19;i++) code_cn[i]=(code_cn[i]+1)>>1;   
  }while(1);

  count_3=19;
  while(!code_blen[code_index_1[count_3-1]]) count_3--;

  LOG_DECOMP("DMSDOS: sq_comp: count_3 %d\n",count_3);
 }

 /* prepare output bitstream for writting */
 sq_wri(&bits,pout,lout);

 sq_wrn(&bits,'S',8);
 sq_wrn(&bits,'Q',8);
 sq_wrn(&bits,0,16); 
 sq_wrn(&bits,1,1);     /* final flag */
 sq_wrn(&bits,2,2);     /* huffman */
 sq_wrn(&bits,count_1-0x101,5);
 sq_wrn(&bits,count_2-1,5);
 sq_wrn(&bits,count_3-4,4);
 for(i=0;i<count_3;i++) sq_wrn(&bits,code_blen[code_index_1[i]],3);

 { /* compressed code table write */ 
  __u8 *pi=code_buf;
  __u8 *pe=code_buf+code_buf_len;
  int code;
  
  if(sq_wrhufi(ch_huf,code_blen,code_blcn,19))
  { printk("DMSDOS: sq_comp: Huffman code leakage in table 1\n");
    FREE(work_mem);return(0);
  };
  while(pi<pe)
  {
   sq_wrh(&bits,ch_huf,code=*pi++);
   switch(code)
   {
    case 16: sq_wrn(&bits,*pi++,2); break;
    case 17: sq_wrn(&bits,*pi++,3); break;
    case 18: sq_wrn(&bits,*pi++,7); break;
    default: ;
   }
  }
 }

 { /* real data write */
  int cod;
  int len;
  __u8 *pi=(__u8*)pin;

  if(sq_wrhufi(ch_huf,ch_blen,ch_blcn,count_1))
  { printk("DMSDOS: sq_comp: Huffman code leakage in table 2\n");
    FREE(work_mem);return(0);
  };
  if(sq_wrhufi(offs_huf,offs_blen,offs_blcn,count_2))
  { printk("DMSDOS: sq_comp: Huffman code leakage in table 3\n");
    FREE(work_mem);return(0);
  };

  while((cod=*(lz_pos++))!=TK_END)
  {
   if((__u8*)bits.pd+0x20>=lz_pos)
   {
    LOG_DECOMP("DMSDOS: sq_comp: Data overwrites intermediate code\n");
    FREE(work_mem);return 0;
   };
   if(cod>=TK_CHRS)
   { /* characters */
    len=cod-TK_CHRS;
    if(len==15) C_LD_u16(lz_pos,len);
    while(len--) sq_wrh(&bits,ch_huf,*(pi++));
   }else{ /* tokens */
    sq_wrh(&bits,ch_huf,cod+0x100);
    cod--;
    len=sqt_repbas[cod];
    if(sqt_repbln[cod]) 
    { 
     sq_wrn(&bits,*lz_pos,sqt_repbln[cod]);
     len+=*(lz_pos++);
    };
    pi+=len;
    cod=*(lz_pos++);
    sq_wrh(&bits,offs_huf,cod);
    if(sqt_offbln[cod])
    {
     if(sqt_offbln[cod]<=8) sq_wrn(&bits,*(lz_pos++),sqt_offbln[cod]);
     else { C_LD_u16(lz_pos,len);sq_wrn(&bits,len,sqt_offbln[cod]);};
    };
   };
  }
  sq_wrh(&bits,ch_huf,TK_END+0x100);
  sq_wrn(&bits,0,16);
  if(pi-(__u8*)pin!=lin) 
  {
   printk("DMSDOS: sq_comp: ERROR: Processed only %d bytes !!!!!!\n",pi-(__u8*)pin);
   FREE(work_mem);return 0;
  };
  FREE(work_mem);
  return((__u8*)bits.pd-(__u8*)pout);
 };
 FREE(work_mem);return 0;
};

#endif /* DMSDOS_CONFIG_DRVSP3 */
