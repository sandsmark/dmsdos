/*
dstacker_compr.c

DMSDOS CVF-FAT module: stacker compression routines.

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
#include"lib_interface.h"
#include<malloc.h>
#include<string.h>
#include<errno.h>
#endif


#ifdef DMSDOS_CONFIG_STAC

#ifdef __DMSDOS_DAEMON__
#include<malloc.h>
#include<string.h>
#include<asm/unaligned.h>
#include<asm/types.h>
#include <asm/byteorder.h>
#define MALLOC malloc
#define FREE free
int printk(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#endif

#ifdef __GNUC__
#define INLINE static inline
#else
/* non-gnu compilers may not like inline */
#define INLINE static
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))


#if defined(__GNUC__) && defined(__i386__) && defined(USE_ASM)
#define USE_GNU_ASM_i386

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

INLINE __u16 swap_bytes_in_word(__u16 x)
{
    __asm__("xchgb %b0,%h0"         /* swap bytes           */
            : "=q" (x)
            :  "0" (x));
    return x;
}

#else

#define M_FIRSTDIFF(D,S,C) for(;(*(__u8*)(D)==*(__u8*)(S))&&(C);\
			   (__u8*)(D)++,(__u8*)(S)++,(C)--)

INLINE __u16 swap_bytes_in_word(__u16 x)
{
    return ((x & 0x00ff) << 8) | ((x & 0xff00) >> 8);
}

#endif

#if !defined(cpu_to_le16)
/* for old kernel versions - works only on i386 */
#define le16_to_cpu(v) (v)
#define cpu_to_le16(v) (v)
#define be16_to_cpu(v) (swap_bytes_in_word(v))
#define cpu_to_be16(v) (swap_bytes_in_word(v))
#endif

/* store and load __u16 in any byteorder on any */
/* address (odd or even).                       */
/* this is problematic on architectures,        */
/* which cannot do __u16 access to odd address. */
/* used for temporary storage of LZ intercode.  */
#define C_ST_u16(p,v) {put_unaligned(v,p);p=(__u16*)p+1;}
#define C_LD_u16(p,v) {v=get_unaligned(p);p=(__u16*)p+1;}

/* for reading and writting from/to bitstream */
typedef
struct {
    __u32 buf;   /* bit buffer */
    int pb;    /* not read bits count in buffer */
    __u16 *pd;   /* first not readed input data */
    __u16 *pe;   /* after end of data */
} bits_t;

/*************************************************************************/
/*************************************************************************/
/*************** begin code from sd4_bs1.c *******************************/

typedef unsigned int count_t;
typedef __u8 *hash_t;
typedef
struct {
    count_t cn;
    unsigned ch;
} ch_tab_t;

typedef
struct {
    __u16 cod[0x180];     /* characters codes */
    __u8  ln[0x180];      /* characters lens */
} huf_wr_t;

#ifdef MAX_COMP
#define MAX_HASH_HIST 0x1000
#else
#define MAX_HASH_HIST 0x800
#endif

#define TK_END   0x4F
#define TK_CHRS  0xF0
#define TKWR_CHRS(p,v) {if(v<15) *(p++)=TK_CHRS+(__u8)v;\
			else {*(p++)=TK_CHRS+15;C_ST_u16(p,v);};}

/* compression level table */
const unsigned comp_rat_tab[]= {
    /*0*/ 0x7F9,0x7F9,0x621,0x625,
    /*4*/ 0x665,0x669,0x6E9,0x6ED,
    /*8*/ 0x7D1,0x7D9,0x6E9,0x47D9,
    /*12*/0x46E9
};

/* token decoding tables */
const unsigned int sd4b_prog_len[]= {   5,   7,   9,   11};
const unsigned int sd4b_prog_add[]= { 0x1,0x21,0xA1,0x2A1};
const signed char sd4b_reps_div3[]= {0,0,0,0,1,1,1,2,2,2,3,3,3,4,4,4,5,5,5,
                                     6,6,6,7,7,7,8,8,8,9,9,9,10,10,10,11,11,11,12,12,12,13,13,13,14,14,14,
                                     15,15,15,16,16,16,17,17,17,18,18,18,19,19,19,20,20,20
                                    };
const signed char sd4b_reps_n[] = {3-1,3-4,3-7,3-10,3-13,3-16,3-19,3-22,
                                   3-25,3-28,3-31,3-34,3-37,3-40,3-43,3-46,3-49,3-52,3-55,3-58,3-61
                                  };
const unsigned char sd4b_reps_b[] = {0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9};
const unsigned int  sd4b_reps_m[] = {1,2,3,4,6,8,12,16,24,32,48,64,96,128,
                                     192,256,384,512,768,1024,1536
                                    };

#if 0

INLINE unsigned sd4_hash(__u8 *p)
{
    unsigned a;
    /*a=(p[0]>>1)^(p[1]<<7)^(p[1]>>4)^(p[2]<<2);*/
    a =p[1];
    a<<=5;
    a^=p[2];
    a<<=2+6;
    a^=p[1];
    a>>=5;
    a^=p[0];
    a>>=1;
    return a&0x3FF;
};

#else

INLINE unsigned sd4_hash(__u8 *p)
{
    return (((__u16)p[0]<<2)^((__u16)p[1]<<4)^(__u16)p[2])&0x3FF;
};

#endif

INLINE hash_t sd4_newhash(__u8 *p,hash_t *hash_tab,hash_t *hash_hist,unsigned hash_mask)
{
    hash_t *hash_ptr;
    hash_t  hash_cur;
    hash_ptr=hash_tab+sd4_hash(p);
    hash_cur=*hash_ptr;
    *hash_ptr=p;
    *(hash_hist+((unsigned)p&hash_mask))=hash_cur;
    return (hash_cur);
};

/* finds repetitions in *pin and writes intermediate code to *pout */
unsigned sd4_complz(void *pin,int lin,void *pout,int lout,int flg,count_t *ch_cn)
{
    void *work_mem;
    int try_count;           /* number of compares to find best match */
    int hash_skiped;         /* last bytes of repetition are hashed too */
    hash_t *hash_tab; /* pointers to last occurrence of same hash, index hash */
    hash_t *hash_hist;
    /* previous occurences of hash, index actual pointer&hash_mask */
    unsigned hash_mask=0x7FF;/* mask for index into hash_hist */
    __u8 *pi, *po, *pc, *pd, *pend, *poend;
    hash_t hash_cur;
    unsigned cn;
    unsigned max_match, match, token;
    int      try_cn;
    hash_t   max_hash=NULL;
#ifdef MAX_COMP
    int     delay_count=0;  /* test next # characters for better match */
    int     delay_cn;
    int     delay_best;
#endif

    try_count=2<<((flg>>2)&0x7); /* number of tested entries with same hash */
    hash_skiped=((flg>>5)+1)&0xF;/* when rep found last # bytes are procesed */

#ifdef MAX_COMP

    if (flg&0x4000) {
        hash_mask=MAX_HASH_HIST-1; /* maximal compression */
        delay_count=2;
        try_count*=4;
        hash_skiped*=4;
    };

#endif

    /* stack is small in kernel space, using MALLOC */
    work_mem=MALLOC((0x400+hash_mask+1)*sizeof(hash_t));

    if (work_mem==NULL) { return 0; }

    hash_tab =(hash_t *)work_mem;
    hash_hist=((hash_t *)work_mem)+0x400;

    pi=(__u8 *)pin;
    po=(__u8 *)pout;

    if (!lin) { goto return_error; }

    pend=pi+(lin-1);

    /*
    printk("There we are,work_mem=%X hash_hist=%X pin=%X lin=%X pend=%X pout=%X\n",
     work_mem,hash_hist,pin,lin,pend,pout);
    */

    if (lout<0x20) { goto return_error; } /* some minimal space for lz interform buffer */

    poend=po+(lout-0x20);

    for (cn=0; cn<0x400; cn++) { hash_tab[cn]=pend; }    /* none ocurence of hash */

    for (cn=0; cn<=hash_mask; cn++) { hash_hist[cn]=pend; } /* should not be be needed */

    pend--;                      /* last two bytes cannot be hashed */
    cn=0;

    while (pi<pend) {
        hash_cur=sd4_newhash(pi,hash_tab,hash_hist,hash_mask);

        /* goto single_char; */ /* to by pass LZ for tests */
        if (hash_cur>=pi) { goto single_char; }

        try_cn=try_count;
        max_match=2; /* minimal coded match-1 */

        do {
            if (pi-hash_cur>=0xAA0) { break; }  /* longer offsets are not allowed */

            if ((hash_cur[0]==pi[0])&&(hash_cur[1]==pi[1])&&
                    /* pi[2]=hash_cur[2] from hash function */
                    (hash_cur[max_match-1]==pi[max_match-1])&&
                    (hash_cur[max_match]==pi[max_match])) {
                match=pend-pi;           /* length of rest of data */
                pd=pi+2;
                pc=hash_cur+2;
                M_FIRSTDIFF(pd,pc,match);/* compare */
                match=pd-pi;             /* found match length */

                if ((match>max_match)&&((match>=6)||(pi-hash_cur<0x800))) {
                    max_hash=hash_cur;      /* find maximal hash */
                    max_match=match;

                    if (pd>pend+1) { break; }   /* match to end of block */
                };
            };

            pc=hash_cur;
        } while (--try_cn&&((hash_cur=hash_hist[(unsigned)pc&hash_mask])<pc));

        if (max_match<3) { goto single_char; }

#ifdef MAX_COMP
        /* tests if better matchs on next characters */
        delay_cn=0;

        if (delay_count) {
            delay_best=0;

            while ((delay_cn<delay_count)&&(pi+max_match<pend)&&
                    (max_match<0x100)) {
                pi++;
                delay_cn++;
                hash_cur=sd4_newhash(pi,hash_tab,hash_hist,hash_mask);
                try_cn=try_count;

                if (hash_cur<pi) do {
                        if (pi-hash_cur>=0xAA0) { break; } /* longer offsets are not allowed */

                        if ((hash_cur[0]==pi[0])&&(hash_cur[1]==pi[1])&&
                                /* pi[2]=hash_cur[2] from hash function */
                                (hash_cur[max_match-1]==pi[max_match-1])&&
                                (hash_cur[max_match]==pi[max_match])&&
                                (hash_cur!=max_hash+delay_cn-delay_best)) {
                            /* do not test actual max match */
                            match=pend-pi;           /* length of rest of data */
                            pd=pi+2;
                            pc=hash_cur+2;
                            M_FIRSTDIFF(pd,pc,match);/* compare */
                            match=pd-pi;             /* found match length */

                            if ((match>max_match+delay_cn)&&((match>=6)||(pi-hash_cur<0x800))) {
                                max_hash=hash_cur;
                                max_match=match; /* find maximal hash */
                                delay_best=delay_cn;

                                if (pd>pend+1) { break; }   /* match to end of block */
                            };
                        };

                        pc=hash_cur;
                    } while (--try_cn&&((hash_cur=hash_hist[(unsigned)pc&hash_mask])<pc));
            };

            pi-=delay_cn;

            delay_cn-=delay_best;

            while (delay_best) {
                delay_best--;
                ch_cn[*(pi++)]++;

                if (++cn>=0x8000u)
                {TKWR_CHRS(po,0x8000u); cn-=0x8000u; if (poend<po) goto return_error;};
            }
        };

#endif

        if (cn) { TKWR_CHRS(po,cn); }

        cn=pi-max_hash-1; /* history offset */
        pi+=max_match;    /* skip repeated bytes */

        if (max_match<6) {
            token=max_match-3;

            if (cn<3) {
                token+=cn+(cn<<1);
                *(po++)=token;
            } else {
                token=max_match-3+9;
                cn-=3;
                match=4;

                while (cn>=match) {token+=6; cn-=match; match<<=1;};

                match>>=1;

                if (cn>=match) {token+=3; cn-=match;};

                *(po++)=token;

                C_ST_u16(po,cn); /* history offset */
            };
        } else {
            if (max_match<21)
            {token=max_match-6+0x3F; *(po++)=token; C_ST_u16(po,cn);}
            else {
                token=0x4E;
                *(po++)=token;
                C_ST_u16(po,cn);  /* history offset */
                C_ST_u16(po,max_match); /* repeat count */
            };
        };

        ch_cn[token+0x100]++;

        if (hash_skiped&&(pi<pend)) {
#ifdef MAX_COMP
            max_match-=delay_cn;
#endif

            if (--max_match>hash_skiped) { max_match=hash_skiped; }

            pi-=max_match;

            while (max_match--) { sd4_newhash(pi++,hash_tab,hash_hist,hash_mask); }
        };

        if (poend<po) { goto return_error; }

        cn=0;
        continue;
single_char:
        ch_cn[*(pi++)]++;

        if (++cn>=0x8000u)
        {TKWR_CHRS(po,0x8000u); cn-=0x8000u; if (poend<po) goto return_error;};
    };

    pend+=2;

    while (pi!=pend) {ch_cn[*(pi++)]++; cn++;};

    if (cn) {
        if (cn>=0x8000u) {TKWR_CHRS(po,0x8000u); cn-=0x8000u;};

        TKWR_CHRS(po,cn);
    };

    ch_cn[TK_END+0x100]++;

    *po++=TK_END;

    FREE(work_mem);

    return (po-(__u8 *)pout);

return_error:
    FREE(work_mem);

    return (0);
};

INLINE void sd4b_wri(bits_t *pbits,void *pin,unsigned lin)
{
    pbits->buf=0;
    pbits->pb=32;
    pbits->pd=(__u16 *)pin;
    pbits->pe=pbits->pd+((lin+1)>>1);
};

INLINE void sd4b_wrn(bits_t *pbits,int cod, int n)
{
    pbits->pb-=n;
    pbits->buf|=(__u32)cod<<pbits->pb;

    if (pbits->pb<16) {
        *(pbits->pd++)=cpu_to_le16((__u16)(pbits->buf>>16));
        pbits->buf<<=16;
        pbits->pb+=16;
    };
};

INLINE int sd4b_wrhufi(huf_wr_t *phuf,count_t *ch_cn, unsigned *ch_blcn,int cod_num)
{
    unsigned i,u,t,blen;
    u=0;

    for (i=0; i<16; i++) {u<<=1; t=u; u+=ch_blcn[i]; ch_blcn[i]=t;};

    if (u!=0x8000u) { return (1); }

    for (i=0; i<cod_num; i++) {
        if ((blen=ch_cn[i])!=0) {
            phuf->cod[i]=ch_blcn[blen]++;
            phuf->ln[i]=blen;
        };
    };

    return (0);
};

INLINE void sd4b_wrh(bits_t *pbits,const huf_wr_t *phuf,const unsigned ch)
{
    sd4b_wrn(pbits,phuf->cod[ch],phuf->ln[ch]);
};

/*** Hacked generation of character codes ***/

void sd4_hsort1(ch_tab_t *ch_tab,int ch_num,int cl, ch_tab_t a)
{
    int ch;
    ch_tab_t b;
    ch_tab_t c;
    ch=cl*2;

    while (ch<ch_num) {
        b=ch_tab[ch-1];
        c=ch_tab[ch];

        if ((c.cn<b.cn)||((c.cn==b.cn)&&(c.ch<=b.ch))) {b=c; ch++;};

        if ((b.cn>a.cn)||((b.cn==a.cn)&&(b.ch>=a.ch))) {ch_tab[cl-1]=a; return;}

        ch_tab[cl-1]=b;
        cl=ch;
        ch*=2;
    };

    if (ch==ch_num) {
        b=ch_tab[ch-1];

        if ((b.cn<a.cn)||((b.cn==a.cn)&&(b.ch<a.ch)))
        {ch_tab[cl-1]=b; cl=ch; ch*=2;};
    };

    ch_tab[cl-1]=a;
};

int sd4_huffman(count_t *ch_cn,unsigned *ch_blcn,int cod_num,void *work_mem)
{
    ch_tab_t *ch_tab;  /* normaly 0x152 entries */
    int i,ch_num,cl;
    ch_tab_t a;
    ch_tab_t b;

    ch_tab=(ch_tab_t *)work_mem;
redo_reduced:
    ch_num=0;

    for (i=0; i<cod_num; i++) if (ch_cn[i])
        {ch_tab[ch_num].cn=ch_cn[i]; ch_tab[ch_num].ch=i|0x800; ch_num++;};

    ch_tab[ch_num].ch=0;

    if (ch_num==1) {
        ch_tab[ch_num]=ch_tab[ch_num-1];
        ch_tab[ch_num].ch&=0x801;
        ch_tab[ch_num].ch^=1;
        ch_num++;
    };

    cl=ch_num/2;

    while (cl>1) {
        sd4_hsort1(ch_tab,ch_num,cl,ch_tab[cl-1]);
        cl--;
    };

    cl=ch_num;

    a=ch_tab[0];

    while (cl>2) {
        sd4_hsort1(ch_tab,cl,1,a);
        b=ch_tab[0];
        a=ch_tab[--cl];
        ch_tab[cl].ch=b.ch;
        sd4_hsort1(ch_tab,cl,1,a);
        a=ch_tab[0];
        ch_tab[cl].cn=a.ch;

        if (a.ch<=b.ch) {a.ch=b.ch;};

        a.ch=(a.ch&0x7800)+cl+0x800;

        if (a.ch>=0x8000u) {
            printk("DMSDOS: sd4_huffman: Problems with number of bits\n");

            for (i=0; i<cod_num; i++) { ch_cn[i]=(ch_cn[i]+1)>>1; }

            goto redo_reduced;
        };

        a.ch+=0x8000u;

        a.cn+=b.cn;
    };

    ch_tab[1].cn=a.ch;

    {
        int st[0x20];
        int k=0,l=1,blen=0;

        memset(ch_blcn,0,sizeof(ch_blcn[0])*16);

        while (1) {
            do {
                k|=0x4000;

                do {
                    st[blen]=k;
                    blen++;
                    k=l&0x7FF;
                    l=ch_tab[k].ch&0x87FF;
                } while (l&0x8000);

                ch_cn[l]=blen;
                ch_blcn[blen]++;
                l=ch_tab[k].cn&0x87FF;
            } while (l&0x8000);

            do {
                ch_cn[l]=blen;
                ch_blcn[blen]++;

                do {
                    if (!--blen) { goto code_done; }

                    k=st[blen];
                } while (k&0x4000);

                l=ch_tab[k].cn&0x87FF;
            } while (!(l&0x8000));
        };

code_done:
        ;
    };

    return (0);
};

/*** Main compression routine ***/

int sd4_comp(void *pin,int lin, void *pout, int lout, int flg)
{
    count_t *ch_cn=NULL;
    char    *work_mem=NULL;
    unsigned lz_length;

    ch_cn=(count_t *)MALLOC(sizeof(count_t)*0x151);
    memset(ch_cn,0,sizeof(count_t)*0x151);

    if ((lz_length=sd4_complz(pin,lin,pout,lout,flg,ch_cn))==0) { goto return_error; }

    {
        unsigned ch_blcn[16];   /* bitlength couts for codes */
        int i,j;
        int bl_dat;
        unsigned *bl_buf;       /* prepared data of table 2 with tokens */
        count_t act_bl;
        count_t bl_cn[0x16];    /* occurecces of bit lens */
        unsigned bl_blcn[0x16]; /* bitlength couts for bit lens */
        int min_bl, max_bl;
        __u8 *lz_pos;
        __u8 *pdata;
        bits_t bits;
        unsigned token, u;
        huf_wr_t *huf;

        /* for converting local variables to allocated memory - kernel stack
           is too small */
#define SIZE_OF_bl_buf (sizeof(unsigned)*0x151)
#define SIZE_OF_huf    (((MAX(sizeof(huf_wr_t),sizeof(ch_tab_t)*0x152)+63)/64)*64)
        work_mem=(char *)MALLOC(SIZE_OF_huf+SIZE_OF_bl_buf);
        huf=(huf_wr_t *)work_mem;
        bl_buf=(unsigned *)(work_mem+SIZE_OF_huf);
        memset(bl_buf,0,SIZE_OF_bl_buf);

        /* ch_cn  .. ocurrences of codes */
        sd4_huffman(ch_cn,ch_blcn,0x150,work_mem);
        /* ch_cn  .. bit lengths of codes */
        /* ch_blcn .. counts of specific values of bit length */
        memset(bl_cn,0,sizeof(bl_cn));
        i=0;
        bl_dat=0;
        min_bl=8;
        max_bl=0;

        while (i<0x150) {
            /* case 0x10: 2 times zerro */
            /* case 0x11: 3 times zerro */
            /* case 0x12: zerro fill */
            /* case 0x13: 2 times last char */
            /* case 0x14: 3 times last char */
            /* case 0x15: repeat last chr */

            act_bl=ch_cn[i++];
            j=i;

            while ((i<0x150)&&(act_bl==ch_cn[i])) { i++; }

            j=i-j;

            if (!act_bl) {
                if (!j--) {bl_cn[act_bl]++; bl_buf[bl_dat++]=act_bl;}
                else if (!j--) {bl_cn[0x10]++; bl_buf[bl_dat++]=0x10;}
                else if (!j--) {bl_cn[0x11]++; bl_buf[bl_dat++]=0x11;}
                else {bl_cn[0x12]++; bl_buf[bl_dat++]=0x12; bl_buf[bl_dat++]=j;};
            } else {
                bl_cn[act_bl]++;
                bl_buf[bl_dat++]=act_bl;

                if (act_bl<min_bl) { min_bl=act_bl; }

                if (act_bl>max_bl) { max_bl=act_bl; }

                if (j--) {
                    if (!j--) {bl_cn[act_bl]++; bl_buf[bl_dat++]=act_bl;}
                    else if (!j--) {bl_cn[0x13]++; bl_buf[bl_dat++]=0x13;}
                    else if (!j--) {bl_cn[0x14]++; bl_buf[bl_dat++]=0x14;}
                    else {bl_cn[0x15]++; bl_buf[bl_dat++]=0x15; bl_buf[bl_dat++]=j;};
                };
            };
        };

        sd4_huffman(bl_cn,bl_blcn,0x16,work_mem);

        sd4b_wri(&bits,pout,lout-lz_length);

        lz_pos=(__u8 *)pout+lout-lz_length;

        memmove(lz_pos,pout,lz_length);

        /* write magic */
        sd4b_wrn(&bits,0x81,16);

        /* write table 1 */
        sd4b_wrn(&bits,min_bl-1,3);

        sd4b_wrn(&bits,max_bl,5);

        sd4b_wrn(&bits,bl_cn[0],4);

        for (i=min_bl; i<=max_bl; i++) { sd4b_wrn(&bits,bl_cn[i],4); }

        for (i=0x10; i<=0x15; i++) { sd4b_wrn(&bits,bl_cn[i],4); }

        /* write table 2 */
        if (sd4b_wrhufi(huf,bl_cn,bl_blcn,0x16))
        {printk("DMSDOS: sd4_comp: Hufman code leakage in table 1\n"); goto return_error;};

        for (i=0; i<bl_dat;) {
            sd4b_wrh(&bits,huf,j=bl_buf[i++]);

            if (j==0x12) {
                j=bl_buf[i++];

                if (j>=7) {
                    sd4b_wrn(&bits,7,3);
                    j-=7;

                    while (j>=0x7F) {sd4b_wrn(&bits,0x7F,7); j-=0x7F;};

                    sd4b_wrn(&bits,j,7);
                } else { sd4b_wrn(&bits,j,3); }
            } else if (j==0x15) {
                j=bl_buf[i++];

                while (j>=7) {sd4b_wrn(&bits,7,3); j-=7;};

                sd4b_wrn(&bits,j,3);
            };
        };

        /* write compressed data */
        {
            pdata=(__u8 *)pin;

            if (sd4b_wrhufi(huf,ch_cn,ch_blcn,0x150))
            {printk("DMSDOS: sd4_comp: Hufman code leakage in table 2\n"); goto return_error;};

            while (1) {
                /* check of LZ and huff contact in output buffer */
                if ((__u8 *)bits.pd+0x20>=lz_pos) { goto return_error; }

                token=*(lz_pos++);

                if (token>TK_CHRS) {
                    /* characters */
                    u=token-TK_CHRS;

                    if (u==15) {
                        C_LD_u16(lz_pos,u);

                        while (u--)
                            if ((__u8 *)bits.pd+1>=lz_pos) { goto return_error; }
                            else { sd4b_wrh(&bits,huf,*(pdata++)); }
                    } else while (u--) { sd4b_wrh(&bits,huf,*(pdata++)); }
                } else {
                    /* repetitions coded as tokens */
                    sd4b_wrh(&bits,huf,token+0x100);

                    if (token<0x3F) {
                        /* short repeat tokens */
                        token++;
                        u=sd4b_reps_div3[token];
                        pdata+=token+sd4b_reps_n[u];
                        i=sd4b_reps_b[u];

                        if (i) {
                            C_LD_u16(lz_pos,u);
                            sd4b_wrn(&bits,u,i);
                        };
                    } else if (token<TK_END) {
                        /* repeat n times last m characters */
                        C_LD_u16(lz_pos,u);
                        u++;  /* history offset */

                        if (u<0x21) { i=0; }
                        else if (u<0xA1) { i=1; }
                        else if (u<0x2A1) { i=2; }
                        else { i=3; }

                        sd4b_wrn(&bits,i,2);
                        sd4b_wrn(&bits,u-sd4b_prog_add[i],sd4b_prog_len[i]);

                        if (token==0x4E) {
                            /* repeat n>=21 */
                            C_LD_u16(lz_pos,u); /* repeat count */
                            pdata+=u;
                            u-=0x15;

                            if (u<0xF) { sd4b_wrn(&bits,u,4); }
                            else {
                                u-=0xF;
                                sd4b_wrn(&bits,0xF,4);

                                if (u<0xFF) { sd4b_wrn(&bits,u,8); }
                                else {
                                    u-=0xFF;
                                    sd4b_wrn(&bits,0xFF,8);

                                    if (u<0xFFF) { sd4b_wrn(&bits,u,12); }
                                    else {u-=0xFFF; sd4b_wrn(&bits,0xFFF,12); sd4b_wrn(&bits,u,16);};
                                };
                            };
                        } else { pdata+=token+6-0x3F; }
                    } else { break; }
                };
            };

            if ((token!=TK_END)||(pdata-(__u8 *)pin!=lin)) {
                printk("DMSDOS: sd4_comp: Compression ends with mismash\n");
                goto return_error;
            };
        };

        sd4b_wrn(&bits,0,16);

        FREE(ch_cn);

        FREE(work_mem);

        return ((__u8 *)bits.pd-(__u8 *)pout);
    };

return_error:
    if (ch_cn!=NULL) { FREE(ch_cn); }

    if (work_mem!=NULL) { FREE(work_mem); }

    return (0);
};

/*************** end code from sd4_bs1.c *********************************/

/*************************************************************************/
/*************************************************************************/
/*************** begin code from sd4_bs0.c *******************************/

INLINE void sd3b_wri(bits_t *pbits,void *pin,unsigned lin)
{
    pbits->buf=0;
    pbits->pb=32;
    pbits->pd=(__u16 *)pin;
    pbits->pe=pbits->pd+((lin+1)>>1);
};

INLINE void sd3b_wrn(bits_t *pbits,int cod, int n)
{
    pbits->pb-=n;
    pbits->buf|=(__u32)cod<<pbits->pb;

    if (pbits->pb<16) {
        *(pbits->pd++)=cpu_to_be16((__u16)(pbits->buf>>16));
        pbits->buf<<=16;
        pbits->pb+=16;
    };
};

INLINE __u8 sd3_xorsum(__u8 *data,int len)
{
    __u8 sum=0xFF;

    while (len--) { sum^=*(data++); }

    return (sum);
};

INLINE unsigned sd3_hash(__u8 *p)
{
    return (((__u16)p[0]<<4)^((__u16)p[1]<<0))&0x3FF;
};

INLINE hash_t sd3_newhash(__u8 *p,hash_t *hash_tab,hash_t *hash_hist,unsigned hash_mask)
{
    hash_t *hash_ptr;
    hash_t  hash_cur;
    hash_ptr=hash_tab+sd3_hash(p);
    hash_cur=*hash_ptr;
    *hash_ptr=p;
    *(hash_hist+((unsigned)p&hash_mask))=hash_cur;
    return (hash_cur);
};

int sd3_comp(void *pin,int lin, void *pout, int lout, int flg)
{
    bits_t bits;             /* output bitstream */
    int try_count;           /* number of compares to find best match */
    int hash_skiped;         /* last bytes of repetition are hashed too */
    hash_t *hash_tab;
    /* [0x400] pointers to last occurrence of same hash, index hash */
    hash_t *hash_hist;
    /* [0x800] previous occurences of hash, index actual pointer&hash_mask */
    unsigned hash_mask=0x7FF;/* mask for index into hash_hist */
    __u8 *pi, *pc, *pd, *pend;
    hash_t hash_cur;
    unsigned offs;
    unsigned max_match, match, rep;
    int      try_cn;
    hash_t   max_hash=NULL;

    try_count=2<<((flg>>2)&0x7); /* number of tested entries with same hash */
    hash_skiped=((flg>>5)+1)&0xF;/* when rep found last # bytes are procesed */

    pi=(__u8 *)pin;

    if (!lin) { return (0); }

    pend=pi+(lin-1);

    if (lout<0x20) { return (0); }

    sd3b_wri(&bits,pout,lout-0x10); /* initialize output bitstream */
    hash_tab=MALLOC(0x400*sizeof(hash_t));

    if (hash_tab==NULL) { return 0; }

    hash_hist=MALLOC(0x800*sizeof(hash_t));

    if (hash_hist==NULL) {FREE(hash_tab); return 0;}

    for (offs=0; offs<0x400; offs++) { hash_tab[offs]=pend; } /* none ocurence of hash */

    for (offs=0; offs<=hash_mask; offs++) { hash_hist[offs]=pend; } /* should not be needed */

    pend--;                      /* last two bytes cannot be hashed */

    while (pi<pend) {
        if (bits.pd>bits.pe) {
            /* aborting */
            FREE(hash_hist);
            FREE(hash_tab);
            return 0;
        };

        hash_cur=sd3_newhash(pi,hash_tab,hash_hist,hash_mask);

        if (hash_cur>=pi) { goto single_char; }

        try_cn=try_count;
        max_match=0;

        do {
            if (pi-hash_cur>=0x800) { break; }  /* longer offsets are not alloved */

            if (hash_cur[0]==pi[0]) { /* pi[1]=hash_cur[1] from hash function */
                match=pend-pi;           /* length of rest of data */
                pd=pi+2;
                pc=hash_cur+2;
                M_FIRSTDIFF(pd,pc,match);/* compare */
                match=pd-pi;             /* found match length */

                if (match>max_match)
                {max_hash=hash_cur; max_match=match;}; /* find maximal hash */
            };

            pc=hash_cur;
        } while (--try_cn&&((hash_cur=hash_hist[(unsigned)pc&hash_mask])<pc));

        if (max_match<2) { goto single_char; }

        offs=pi-max_hash;   /* history offset */
        pi+=max_match;      /* skip repeated bytes */

        if (offs<0x80) { sd3b_wrn(&bits,0x180+offs,9); }
        else {
            sd3b_wrn(&bits,0x100+offs/16,9);
            sd3b_wrn(&bits,offs%16,4);
        };

        rep=max_match-2;

        if (rep<3) { sd3b_wrn(&bits,rep,2); }
        else {
            rep-=3;
            sd3b_wrn(&bits,3,2);

            if (rep<3) { sd3b_wrn(&bits,rep,2); }
            else {
                rep-=3;
                sd3b_wrn(&bits,3,2);

                for (; rep>=15; rep-=15) { sd3b_wrn(&bits,15,4); }

                sd3b_wrn(&bits,rep,4);
            };
        };

        if (hash_skiped&&(pi<pend)) {
            if (--max_match>hash_skiped) { max_match=hash_skiped; }

            pi-=max_match;

            while (max_match--) { sd3_newhash(pi++,hash_tab,hash_hist,hash_mask); }
        };

        continue;

single_char:
        sd3b_wrn(&bits,*pi++,9);
    };

    pend+=2;

    while (pi!=pend) { sd3b_wrn(&bits,*pi++,9); }

    sd3b_wrn(&bits,0x180,9);
    bits.pb&=~7;
    sd3b_wrn(&bits,sd3_xorsum(pin,lin),8);

    sd3b_wrn(&bits,0,15);

    FREE(hash_hist);
    FREE(hash_tab);
    return ((__u8 *)bits.pd-(__u8 *)pout);

};

/*************** end code from sd4_bs0.c *********************************/


/* This function will be called by the dmsdos driver *and* by the daemon
   in order to compress stacker data (the daemon links the object file, too).
   Decision can be made with ifdef __KERNEL__ .

   Hi Frank,
	     I know, that it is different from doublespace, but I
   decide change this, because stacker 4 compression likes know
   exactly free space in output buffer. It uses this space for
   intermediate data representation ( which is always shorter
   then original data ), then moves it to end and writes new
   compressed data from begining (if write catch read compression
   returns false). Theoreticaly length -> length must sucees or
   all compression is useless, but who knows. And there is such
   lovely 32 kB buffer in daemon.

*/

#if 0
/* this confuses some compilers in the memset command below */
int stac_compress(void *pin,int lin, void *pout, int lout,
                  int method, int cfaktor)
#else
int stac_compress(unsigned char *pin,int lin, unsigned char *pout, int lout,
                  int method, int cfaktor)
#endif
{
    int ret=-1;
    int i;

    if (((i=lin%512)!=0)||!lin) { /* useless but stacker like it */
        memset(pin+lin,0,512-i);
        lin+=512-i;
    };

    if ((cfaktor<=0)||(cfaktor>12)) { cfaktor=11; }

    if (method==SD_4) { ret=sd4_comp(pin,lin,pout,lout,comp_rat_tab[cfaktor]); }
    else if (method==SD_3) { ret=sd3_comp(pin,lin,pout,lout,comp_rat_tab[cfaktor]); }

    if (ret>lin-512) { ret=0; } /* for now */

    return ret;
}

/* Specification:
   This function writes a stacker cluster.
   It must take care of clustermap and allocationmap manipulation
   including freeing the old sectors.
   It must not touch the FAT.
   It must take care of compression, if implemented.
   It must write uncompressed if ucflag==UC_UNCOMPR.
   Depending on the daemon implementation, it should be able to process
   raw writes (ucflag<0).
   near_sector may be ignored in case this is too difficult to implement.
   ucflag==UC_TEST means check for free space or reserve space for the
   cluster on the disk, but don't actually write the data.
   It is for write-back cluster caching. When a cluster is marked as dirty
   by calling the ch_dirty function, the cluster caching code calls the
   write_cluster function with the UC_TEST flag. The cluster
   write access is delayed only if the UC_TEST call succeeds (returns a
   value >=0). Otherwise the cluster caching code immediately falls back to
   write-through mode and calls the write function again.
   If the UC_TEST call succeeds, be prepared to be called again later
   at any time without the UC_TEST flag when the cluster caching code has
   decided to actually write the data back to disk.
*/

/* write a stacker cluster, compress before if possible;
   length is the number of used bytes, may be < SECTOR_SIZE*sectperclust only
   in the last cluster of a file;
   cluster must be allocated by allocate_cluster before if it is a new one;
   unable to write dir clusters;
   to avoid MDFAT level fragmentation, near_sector should be the sector no
   of the preceeding cluster;
   if ucflag==UC_UNCOMPR uncompressed write is forced.
   if ucflag==UC_TEST check for free space or reserve space on the
   disk but don't actually write the data.

   If ucflag<0 raw write is forced with compressed size -ucflag (in bytes),
   in that case parameter length is *uncompressed* size. This is the new
   dmsdosd/ioctl interface.

   If clusterd==NULL the cluster is to be removed instead of written. This
   is called by the rest of the dmsdos code when a file is deleted. So
   the stacker code is expected to free up mdfat/bitfat allocation for the
   cluster, but it must not touch the fat.

   if ucflag==UC_DIRECT do like ucflag==UC_NORMAL but don't use the daemon
   for compression.
   This should guarantee that the data are written when the function exits.
   It is unimportant whether compression fails or not - it's just important
   that the data use *least* disk space. This must not override the
   method=UNCOMPRESSED or compression level selected by user, though.
   It is intended for error recovery when the filesystem gets full - if
   we use the daemon, the uncompressed data might not fit to the disk while
   the compressed data may still do.
*/

#if defined(__DMSDOS_LIB__)

int stac_write_cluster(struct super_block *sb,
                       unsigned char *clusterd, int length, int clusternr,
                       int near_sector, int ucflag)
{
    int method;
    unsigned char *clusterk;
    int size;
    int sect,count;
    int i,val;
    int res;
    struct buffer_head *bh;
    int max_clen;
    Stac_cwalk cw;
    int cfaktor;
    Dblsb *dblsb=MSDOS_SB(sb)->private_data;

    /* check if we are deleting a cluster */
    if (clusterd==NULL||length==0) {
        lock_mdfat_alloc(dblsb);
        stac_special_free(sb,clusternr);
        unlock_mdfat_alloc(dblsb);
        return 0;
    }

    /* for now */
    /*if(ucflag==UC_TEST) return -EIO;*/
    if (ucflag==UC_TEST) {
        if ( dblsb->s_full==0 &&
                /* well, this is estimated */
                dblsb->s_sectperclust*CCACHESIZE+100<dblsb->s_free_sectors
           ) { return 0; }
        else { return -ENOSPC; }
    }

    if (dblsb->s_comp==GUESS) {
        if (dblsb->s_cvf_version==STAC3) {
            dblsb->s_comp=SD_3;
        } else {
            dblsb->s_comp=SD_4;
        }
    };

    method=dblsb->s_comp; /* default compression method */

    max_clen=dblsb->s_sectperclust*SECTOR_SIZE; /* maximal data size */

    if ( ( (dblsb->s_cvf_version==STAC3)&&(length<=SECTOR_SIZE) ) ||
            (ucflag==UC_UNCOMPR)
       ) { /* uncompressed forced or no need to compress */
        method=UNCOMPRESSED;
        clusterk=clusterd;
    } else if (ucflag<0) {
        /* raw compressed data from daemon */
        length=-ucflag;
        method=UNCOMPRESSED^1; /* not uncompressed */ /* is this correct ??? */
        /* Hi Pavel,
           Please check the code whether it works
           correctly for daemon writes. I think this may
           cause a FREE(data not to free) at the very
           end. I added a ucflag>=0 test there to avoid
           the problem.
         */
        clusterk=clusterd;
    } else if (method!=UNCOMPRESSED) {
        /* ucflag==3 test added - Frank */
        if ((ucflag==UC_DIRECT)?0:try_daemon(sb,clusternr,length,method)) { clusterk=NULL; }
        else if ((clusterk=(unsigned char *)MALLOC(max_clen))==NULL) {
            printk(KERN_WARNING "DMSDOS: stac_write_cluster: no memory for compression, writing uncompressed!\n");
        }

        if (clusterk==NULL) { method=UNCOMPRESSED; }
        else {
            /* We test possible compression */
            /* stacker needs length before compression to be
            multiple of SECTOR_SIZE */
            if (((i=length%SECTOR_SIZE)!=0)||!length) {
                memset(clusterd+length,0,SECTOR_SIZE-i);
                i=length+SECTOR_SIZE-i;
            } else { i=length; }

            cfaktor=dblsb->s_cfaktor;

            if ((cfaktor<=0)||(cfaktor>12)) { cfaktor=11; }

            if (method==SD_4) {
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing sd4...\n");
                i=sd4_comp(clusterd,i,clusterk,max_clen,comp_rat_tab[cfaktor]);
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing finished\n");
            } else if (method==SD_3) {
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing sd3...\n");
                i=sd3_comp(clusterd,i,clusterk,max_clen,comp_rat_tab[cfaktor]);
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing finished\n");
            } else if (method==DS_0_0) {
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing ds00...\n");
                i=dbl_compress(clusterk,clusterd,(i+511)/512,method,cfaktor)*512;
                LOG_CLUST("DMSDOS: stac_write_cluster: compressing finished\n");
            } else { i=0; }

            LOG_CLUST("DMSDOS: Cluster %i compressed from %i to %i\n",clusternr,length,i);

            if ((i<=0)||(i>=length)||(i>max_clen-SECTOR_SIZE)) {
                method=UNCOMPRESSED;
                FREE(clusterk);
            } else { length=i; }
        }
    }

    if (method==UNCOMPRESSED) { clusterk=clusterd; }

    /* Now we have data and must decide where to write them */
    val=stac_cwalk_init(&cw,sb,clusternr,3);

    if (val<0) {
        printk(KERN_ERR "DMSDOS: stac_write_cluster: alloc error in cluster %d\n",
               clusternr);
        res=-EIO;
        goto error_return;
    };

    /* decide if it is necessary to reallocate cluster */
    if ((val==0)||(cw.bytes_in_clust<length)||
            (cw.bytes_in_clust>=length+SECTOR_SIZE)||(cw.flags&0x40)||
            ((cw.compressed==0)!=(method==UNCOMPRESSED))) {
        /* It is necessary realocate space */
        /* this piece of code is dirty hack and must be rewriten */
        Mdfat_entry mde;

        stac_cwalk_done(&cw);

        size=(length+511)/512;

        if (!size) { size=1; }

        mde.size_lo_minus_1=size-1;
        mde.size_hi_minus_1=size-1;

        if (method==UNCOMPRESSED)
            if (size==dblsb->s_sectperclust) {
                mde.flags=2;
            } else {
                mde.flags=0x23;
            } else {
            mde.flags=2;
        }

        LOG_CLUST("DMSDOS: stac_write_cluster: Replace size %2i flg 0x%02X cluster %i\n",
                  size,mde.flags,clusternr);
        sect=stac_replace_existing_cluster(sb,clusternr,near_sector,&mde);
        LOG_CLUST("DMSDOS: stac_write_cluster: stac_replace_existing_cluster returned %d\n",
                  sect);

        if (sect<0) {res=-ENOSPC; goto error_return;};

        val=stac_cwalk_init(&cw,sb,clusternr,3);

        if ((val<0)||(length>cw.bytes_in_clust)) {
            printk(KERN_ERR "DMSDOS: stac_write_cluster: alloc error in cluster %d\n",
                   clusternr);
            res=-EIO;
            goto error_return;
        };
    }

    {
        res=0;
        count=0;

        while ((sect=stac_cwalk_sector(&cw))>0) {
            if (cw.bytes==SECTOR_SIZE) { bh=raw_getblk(sb,sect); }
            else { bh=raw_bread(sb,sect); }

            if (bh==NULL) { res=-EIO; }
            else {
                if (count+cw.bytes>cw.bytes_in_clust) {
                    printk(KERN_ERR "DMSDOS: stac_write_cluster: internal cw error 1 cluster=%d\n",
                           clusternr);
                    raw_brelse(sb,bh);
                    stac_cwalk_done(&cw);
                    goto error_return;
                };

                if (count+cw.bytes<=length) {
                    memcpy(bh->b_data+cw.offset,clusterk+count,cw.bytes);
                } else {
                    if ((i=length-count)>0) {
                        memcpy(bh->b_data+cw.offset,clusterk+count,i);
                        memset(bh->b_data+cw.offset+i,0,cw.bytes-i);
                    } else { memset(bh->b_data+cw.offset,0,cw.bytes); }
                };

                raw_set_uptodate(sb,bh,1);/*getblk needs this*/

                raw_mark_buffer_dirty(sb,bh,1);

                raw_brelse(sb,bh);
            };

            count+=cw.bytes;
        }
    }

    stac_cwalk_done(&cw);

    if ((count<length)||(count!=cw.bytes_in_clust))
        printk(KERN_ERR "DMSDOS: stac_write_cluster: internal cw error 2 cluster=%d\n",
               clusternr);

error_return:

    if (method!=UNCOMPRESSED&&ucflag>=0) { FREE(clusterk); }

    /* better not free the daemon raw data here - Frank */

    return res;
}

#endif /* __DMSDOS_LIB__*/

#endif /* DMSDOS_CONFIG_STAC */
