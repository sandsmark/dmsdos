/*
dstacker_dec.c

DMSDOS CVF-FAT module: stacker decompression routines.

******************************************************************************
DMSDOS (compressed MSDOS filesystem support) for Linux
written 1995-1998 by Frank Gockel and Pavel Pisa

    (C) Copyright 1995-1998 by Frank Gockel
    (C) Copyright 1996-1998 by Pavel Pisa

Stacker decompression (based on sd4_cc package):

    (C) Copyright 1996 by Jaroslav Fojtik (stacker 3 decompression)

Some code of dmsdos has been copied from the msdos filesystem
so there are the following additional copyrights:

    (C) Copyright 1992,1993 by Werner Almesberger (msdos filesystem)
    (C) Copyright 1994,1995 by Jacques Gelinas (mmap code)
    (C) Copyright 1992-1995 by Linus Torvalds

DMSDOS was inspired by the THS filesystem (a simple doublespace
DS-0-2 compressed read-only filesystem) written 1994 by Thomas Scheuermann.

The DMSDOS code is distributed under the Gnu General Public Licence.
See file COPYING for details.
*****************************************************************************

*/

#include "dmsdos.h"

#ifdef __DMSDOS_LIB__
/* some interface hacks */
#include"lib_interface.h"
#include<malloc.h>
#include<string.h>
#include<errno.h>
#endif

#ifdef __GNUC__
#define INLINE static inline
#else
/* non-gnu compilers may not like inline */
#define INLINE static
#endif

#if defined(__GNUC__) && defined(__i386__) && defined(USE_ASM)
#define USE_GNU_ASM_i386

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

INLINE uint16_t swap_bytes_in_word(uint16_t x)
{
    __asm__("xchgb %b0,%h0"		/* swap bytes		*/
            : "=q"(x)
            :  "0"(x));
    return x;
}


#else

#define M_MOVSB(D,S,C) for(;(C);(C)--) *((uint8_t*)(D)++)=*((uint8_t*)(S)++)

INLINE uint16_t swap_bytes_in_word(uint16_t x)
{
    return ((x & 0x00ff) << 8) | ((x & 0xff00) >> 8);
}

#endif

#if !defined(cpu_to_le16)
/* for old kernel versions - works only on i386 */
#define le16_to_cpu(v) (v)
#define be16_to_cpu(v) (swap_bytes_in_word(v))
#endif

/***************************************************************************/
/***************************************************************************/
/********* begin code from sd3_bs0.c ***************************************/

/*#define INLINE inline*/

typedef struct {
    uint8_t *ptr;
    int x;
    int pos;
    int max_x;
} bitstreamC;

void InitBitStream(bitstreamC *b, void *k, int max_x)
{
    b->ptr = (uint8_t *)k;
    b->pos = 0x8;
    b->x = 0;
    b->max_x = max_x;
}


INLINE int Read9BitC(bitstreamC *b)
{
    unsigned int a;

    a = (unsigned) * (b->ptr++) << 8;
    a |= *b->ptr;
    a = (a >> (--b->pos));
    b->x++;

    if (b->pos == 0) {
        (b->ptr)++;
        b->x++;
        b->pos = 0x8;
    }

    return (a & 0x1FF);
}

INLINE int Read4BitC(bitstreamC *b)
{
    unsigned int a;

    if (b->pos <= 3) {
        b->pos += 4;
        a = (unsigned) * (b->ptr++) << 8;
        a |= *b->ptr;
        a = (a >> (b->pos));
        b->x++;
        return (a & 0xF);
    } else {
        if (b->pos == 4) {
            a = *(b->ptr);
            (b->ptr)++;
            b->x++;
            b->pos = 0x8;
            return (a & 0x0F);
        }

        b->pos -= 4;
        return ((*(b->ptr) >> (b->pos)) & 0x0F);
    }
}

INLINE int Read2BitC(bitstreamC *b)
{
    unsigned char a;



    if (b->pos <= 1) {
        a = *(b->ptr++) << 1;
        b->x++;
        b->pos = 0x7;

        if (*b->ptr >= 128) { a++; }

        return (a & 0x3);
    } else {
        if (b->pos == 2) {
            a = *(b->ptr);
            (b->ptr)++;
            b->x++;
            b->pos = 0x8;
            return (a & 0x03);
        }

        b->pos -= 2;
        return ((*(b->ptr) >> (b->pos)) & 0x03);
    }
}

int ReadBitC(bitstreamC *b)
{
    int a;

    a = (*(b->ptr) >> --(b->pos)) & 1;

    if (b->pos == 0) {
        (b->ptr)++;
        b->x++;
        b->pos = 8;
    }

    return (a);
}

/*---------------------------------------------------------*/

int ReadNC(bitstreamC *b)
{
    int repeater, rep;

    rep = repeater = Read2BitC(b);

    if (rep == 3) {
        rep = Read2BitC(b);
        repeater += rep;

        if (rep == 3) {
            rep = Read4BitC(b);
            repeater += rep;

            while (rep == 15) {
                if (b->x >= b->max_x) {
                    printk(KERN_ERR "DMSDOS: stac3_decomp: ReadNC error!\n");
                    return (0);
                };

                rep = Read4BitC(b);

                repeater += rep;
            }
        }
    }

    return (repeater);
}

#define __dcflDebugInfo 0x8000

INLINE uint8_t sd3_xorsum_D(uint8_t *data, int len)
{
    uint8_t sum = 0xFF;

    while (len--) { sum ^= *(data++); }

    return (sum);
};

int sd3_decomp(void *data, int CompSize, void *DecompData, int DecompSize,
               int Flags)
{
    bitstreamC bb;
    int DataSize = DecompSize;
    int token, repN;
    uint8_t *Decomp, *P;

    InitBitStream(&bb, data, CompSize);
    Decomp = (uint8_t *)DecompData;

    while (CompSize > bb.x + 2) {
        token = Read9BitC(&bb);

        if (DataSize <= 0) {
            if (token != 0x180) printk(KERN_INFO "DMSDOS: stac3_decomp: end token 0x%02X\n",
                                           (unsigned)token);

            break;
        };

        if (token >= 256) {
            token = token & 0xFF;

            if (token == 0x81) {
                repN = ReadNC(&bb) + 2;
#ifdef dcflDebugInfo
                printk(KERN_DEBUG "DMSDOS: stac3_decomp: Rep:(%dx) ", repN);
#endif

                if (DataSize < repN) {
                    repN = DataSize;
                    printk(KERN_ERR "DMSDOS: stac3_decomp: char repeat overrun! repN: %d\n", repN);
                    return (0);
                }

                memset((void *)Decomp, *(Decomp - 1), repN);
                Decomp += repN;
                DataSize -= repN;
                continue;
            }

            if (token >= 0x80) {
                token = token & 0x7F;

                if (!token) { break; }
            } else {
                if (token < 8) {
                    printk(KERN_ERR "DMSDOS: stac3_decomp: Unknown token %d on pos 0x%X->0x%X\n",
                           token, bb.ptr - (uint8_t *)data, Decomp - (uint8_t *)DecompData);
                    return (0);
                }

                token = 16 * token + Read4BitC(&bb);
            }

            repN = ReadNC(&bb) + 2;
#ifdef dcflDebugInfo
            printk(KERN_DEBUG "DMSDOS: stac3_decomp: Multi rep:(%dx %d) ", token, repN);
#endif

            if (DataSize < repN) {
                printk(KERN_ERR "DMSDOS: stac3_decomp: Multi rep overrun 0x%x at pos 0x%x->0x%x\n",
                       repN, bb.ptr - (uint8_t *)data, Decomp - (uint8_t *)DecompData);
                repN = DataSize;
                printk(KERN_ERR, "repN after: %d\n", repN);
                return (0);
            }

            /*   memmove(Decomp,Decomp-token,repN); Decomp+=repN; */
            DataSize -= repN;

            P = Decomp - token;

            /* this prevents segfaults in case of strange error */
            if (P < (uint8_t *)DecompData) {
                printk(KERN_ERR "DMSDOS: stac3_decomp: Illegal back pointer length 0x%x at pos 0x%x->0x%x\n",
                       token, bb.ptr - (uint8_t *)data, Decomp - (uint8_t *)DecompData);
                break;
            };

            while (repN--) { *(Decomp++) = *(P++); }

        } else {
            *Decomp = token; /*ReadnBitC(&bb,8);*/
            /*   printk(" %c",*Decomp,*Decomp);*/
            Decomp++;

            if (DataSize != 0) { DataSize--; }
        }
    }

    if (bb.pos != 8) {bb.x++; bb.ptr++;};

    if (CompSize > bb.x) {
        /* Check data xor sum */
        uint8_t sum;
        sum = sd3_xorsum_D((uint8_t *)DecompData, DecompSize - DataSize);

        if (sum^*bb.ptr) {
            printk(KERN_ERR "DMSDOS: stac3_decomp: xor sum error!\n");
            return (0);
        };
    };

    return (DecompSize - DataSize);
}


/**************** end code from sd3_bs0.c ********************************/

/*************************************************************************/
/*************************************************************************/
/*************** begin code from sd4_bs1.c *******************************/

typedef
struct {
    uint32_t buf;	/* bit buffer */
    int pb;	/* not read bits count in buffer */
    uint16_t *pd;	/* first not readed input data */
    uint16_t *pe;	/* after end of data */
} bits_t;

typedef
struct {
    uint8_t ch[0x400];	/* characters codes */
    uint8_t ln[0x400];	/* characters lens .. if >=0x80 controll */
    uint8_t ch1[0x200];	/* for codes vith more than bn bits */
    uint8_t ln1[0x200];
    int bn;		/* ch,ln array max convert bits, longer use ch1,cl1 */
    uint16_t cd_ln[16];	/* distribution of bits */
    uint16_t cd_ch[16];	/* distribution of codes codes */
} huf_t;

const unsigned sd4b_bmsk[] = {
    0x0, 0x1, 0x3, 0x7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF,
    0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF
};

#define RDN_G16(bits) \
   { \
    (bits).buf<<=16; \
    (bits).pb+=16; \
    if((bits).pd<(bits).pe) \
    { \
     (bits).buf|=le16_to_cpu(*((bits).pd++)); \
    }; \
   }

#define RDN_PR(i,bits,n,G16) \
   { \
    if((bits).pb<16) G16(bits); \
    i=(bits).buf>>((bits).pb-=(n)); \
   }

INLINE void sd4b_rdi(bits_t *pbits, void *pin, unsigned lin)
{
    pbits->pb = 0;
    pbits->pd = (uint16_t *)pin;
    pbits->pe = pbits->pd + ((lin + 1) >> 1);
};

INLINE unsigned sd4b_rdn(bits_t *pbits, int n)
{
    unsigned i;
    RDN_PR(i, *pbits, n, RDN_G16);
    i &= sd4b_bmsk[n];
    return i;
};

#define OUT_OVER 0x100

/* read and huffman decode of characters, stops on tokens or buffer ends */
INLINE unsigned sd4b_rdh(bits_t *pbits, const huf_t *phuf, uint8_t **pout, uint8_t *pend)
{

    unsigned ch;
    unsigned bmsk = sd4b_bmsk[phuf->bn];

    while (1) {
        while (1) {
            if (pbits->pb < 16) {
                RDN_G16(*pbits);
            }

            if (*pout >= pend) { return OUT_OVER; }

            ch = (pbits->buf >> (pbits->pb - phuf->bn))&bmsk;

            if ((pbits->pb -= phuf->ln[ch]) < 0) { break; }

            *((*pout)++) = phuf->ch[ch];

            if (pbits->pb >= 16) {
                if (*pout >= pend) { return OUT_OVER; }

                ch = (pbits->buf >> (pbits->pb - phuf->bn))&bmsk;

                if ((pbits->pb -= phuf->ln[ch]) < 0) { break; }

                *((*pout)++) = phuf->ch[ch];

                if (pbits->pb >= 16) {
                    if (*pout >= pend) { return OUT_OVER; }

                    ch = (pbits->buf >> (pbits->pb - phuf->bn))&bmsk;

                    if ((pbits->pb -= phuf->ln[ch]) < 0) { break; }

                    *((*pout)++) = phuf->ch[ch];
                };
            };
        };

        ch = phuf->ch[ch];

        pbits->pb += 0x40;

        if (ch) { return ch; }

        /* code longer than phuf->bn */
        if (pbits->pb < 16) { RDN_G16(*pbits); }

        ch = (pbits->buf >> (pbits->pb - 16)) & 0xFFFF;
        {
            int i;
            i = phuf->bn;

            do {
                i++;
            } while (phuf->cd_ch[i] <= (ch >> (16 - i)) && (i < 15));

            ch = (ch >> (16 - i)) - phuf->cd_ch[i] + phuf->cd_ln[i];
        };

        if ((pbits->pb -= phuf->ln1[ch]) < 0) {
            pbits->pb += 0x40;
            return phuf->ch1[ch];
        };

        *((*pout)++) = phuf->ch1[ch];
    };
};

INLINE int sd4b_rdhufi(huf_t *phuf, int m, int bn, uint8_t *ca)
{
    if (bn > 10) { bn = 10; }

    phuf->bn = bn;
    {
        int i;
        unsigned u, us, ut;
        memset(phuf->cd_ln, 0, sizeof(phuf->cd_ln));
        i = 0;

        while ((u = ca[i++]) < 16) { phuf->cd_ln[u]++; }

        memset(phuf->cd_ch, 0, sizeof(phuf->cd_ch));
        phuf->cd_ln[0] = 0;
        us = 0;
        ut = 0;

        for (i = 1; i < 16; i++) {
            u = phuf->cd_ln[i];
            phuf->cd_ln[i] = ut;
            phuf->cd_ch[i] = us;
            ut += u;
            us += u;
            us <<= 1;
        };

        /* if suceed us should be 0x10000 */
        if (us & 0xFFFF) { return (0); }
    };
    {
        int i, ln, ch, sh, cod;

        for (i = 0; (ln = ca[i]) < 16; i++) if (ln) {
                sh = (bn - ln);
                cod = (phuf->cd_ch[ln])++;

                if (i < m) { ch = i; }
                else {ch = i - m + 1; ln += 0x40;};

                if (sh > 0) {
                    memset(phuf->ch + (cod << sh), ch, 1 << sh);
                    memset(phuf->ln + (cod << sh), ln, 1 << sh);
                } else if (sh == 0) {
                    phuf->ch[cod] = ch;
                    phuf->ln[cod] = ln;
                } else {
                    cod >>= -sh;
                    phuf->ch[cod] = 0x00;
                    phuf->ln[cod] = 0x40;
                    cod = (phuf->cd_ln[ln & 0xF])++;
                    phuf->ch1[cod] = ch;
                    phuf->ln1[cod] = ln;
                };
            };

        /* if suceed ln should be 0xFF */
    };
    return (1);
};

#if 0
/* token decoding tables */
const unsigned int sd4b_prog_len[] = {   5,   7,   9,   11};
const unsigned int sd4b_prog_add[] = { 0x1, 0x21, 0xA1, 0x2A1};
const signed char sd4b_reps_div3[] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5,
                                      6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14,
                                      15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 20, 20, 20
                                     };
const signed char sd4b_reps_n[] = {3 - 1, 3 - 4, 3 - 7, 3 - 10, 3 - 13, 3 - 16, 3 - 19, 3 - 22,
                                   3 - 25, 3 - 28, 3 - 31, 3 - 34, 3 - 37, 3 - 40, 3 - 43, 3 - 46, 3 - 49, 3 - 52, 3 - 55, 3 - 58, 3 - 61
                                  };
const unsigned char sd4b_reps_b[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9};
const unsigned int  sd4b_reps_m[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
                                     192, 256, 384, 512, 768, 1024, 1536
                                    };
#endif
#if 1
extern const unsigned int sd4b_prog_len[];
extern const unsigned int sd4b_prog_add[];
extern const signed char sd4b_reps_div3[];
extern const signed char sd4b_reps_n[];
extern const unsigned char sd4b_reps_b[];
extern const unsigned int  sd4b_reps_m[];
#endif
#if 0
static const unsigned int sd4b_prog_len[] = {   5,   7,   9,   11};
static const unsigned int sd4b_prog_add[] = { 0x1, 0x21, 0xA1, 0x2A1};
static const signed char sd4b_reps_div3[] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5,
                                             6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14,
                                             15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 20, 20, 20
                                            };
static const signed char sd4b_reps_n[] = {3 - 1, 3 - 4, 3 - 7, 3 - 10, 3 - 13, 3 - 16, 3 - 19, 3 - 22,
                                          3 - 25, 3 - 28, 3 - 31, 3 - 34, 3 - 37, 3 - 40, 3 - 43, 3 - 46, 3 - 49, 3 - 52, 3 - 55, 3 - 58, 3 - 61
                                         };
static const unsigned char sd4b_reps_b[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9};
static const unsigned int  sd4b_reps_m[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
                                            192, 256, 384, 512, 768, 1024, 1536
                                           };
#endif

int sd4_decomp(void *pin, int lin, void *pout, int lout, int flg)
{
    bits_t bits;
    huf_t *huf;
    unsigned u;
    uint8_t len_150;

    sd4b_rdi(&bits, pin, lin);
    u = sd4b_rdn(&bits, 16);

    if (u != 0x81) {printk(KERN_ERR "DMSDOS: sd4_decomp: Magic = %X => error!\n", u); return 0;};

    huf = (huf_t *)MALLOC(sizeof(huf_t));

    if (!huf) {printk(KERN_ERR "DMSDOS: sd4_decomp: no memory!\n"); return 0;};

    {
        int i;
        int ie;
        int bmax1, bmax2;
        unsigned u;
        uint8_t ca[0x180];/* 12B4 */
        uint8_t *pca;
        uint8_t *pcae;

        memset(ca, 0, 22);
        i = sd4b_rdn(&bits, 3) + 1;
        ie = bmax2 = sd4b_rdn(&bits, 5);

        if (i > ie) {printk(KERN_ERR "DMSDOS: sd4_decomp: Table 1 error\n"); goto error;};

        ca[0] = bmax1 = sd4b_rdn(&bits, 4);

        while (1) {
            while (i <= ie) {
                u = sd4b_rdn(&bits, 4);
                ca[i++] = u;

                if (u > bmax1) { bmax1 = u; }
            };

            if (ie == 0x15) { break; }

            i = 0x10;
            ie = 0x15;
        };

        ca[22] = 0xFF;

        if (!sd4b_rdhufi(huf, 0x10, 7 < bmax1 ? 7 : bmax1, ca))
        {printk(KERN_ERR "DMSDOS: sd4_decomp: Table 1 consistency check !!!!\n"); goto error;};

        pca = ca;

        pcae = ca + 0x150;

        while ((u = sd4b_rdh(&bits, huf, &pca, pcae)) <= 6) {
            switch (u) {
                unsigned n;

            case 1:	/* 2 times zerro */
                pca[1] = pca[0] = 0;
                pca += 2;
                break;

            case 2:	/* 3 times zerro */
                pca[2] = pca[1] = pca[0] = 0;
                pca += 3;
                break;

            case 3:	/* zerro fill */
                n = 4 + (u = sd4b_rdn(&bits, 3));

                if (u == 7) do {u = sd4b_rdn(&bits, 7); n += u;}
                    while (u == 0x7F);

                if ((pca + n) > pcae) { n = pcae - pca; }

                memset(pca, 0, n);
                pca += n;
                break;

            case 4:	/* 2 times last char */
                pca[1] = pca[0] = *(pca - 1);
                pca += 2;
                break;

            case 5:	/* 3 times last char */
                u = *(pca - 1);

                if (pca < pcae) {pca[0] = pca[1] = pca[2] = u; pca += 3;};

                break;

            case 6:	/* repeat last chr */
                n = 4;

                do {u = sd4b_rdn(&bits, 3); n += u;}
                while (u == 7);

                if ((pca + n) > pcae) { n = pcae - pca; }

                memset(pca, *(pca - 1), n);
                pca += n;
                break;
            };
        };

        ca[0x150] = 0xFF;

        len_150 = ca[0x14F];

        if (!sd4b_rdhufi(huf, 0x100, bmax2, ca))
        {printk(KERN_ERR "DMSDOS: sd4_decomp: Table 2 consistency check !!!!\n"); goto error;};

    };

    {
        uint8_t *p, *r, *pe;
        p = (uint8_t *)pout;
        pe = p + lout;

        while ((u = sd4b_rdh(&bits, huf, &p, pe)) < 0x50) {
            {
                unsigned n, m;

                if (u < 0x40) {
                    m = sd4b_reps_div3[u];	/* short repeat tokens */
                    n = u + sd4b_reps_n[m];
                    u = sd4b_reps_b[m];
                    m = sd4b_reps_m[m];

                    if (u) { m += sd4b_rdn(&bits, u); }
                } else {
                    m = sd4b_rdn(&bits, 2);	/* Repeat n times last m characters */
                    m = sd4b_rdn(&bits, sd4b_prog_len[m]) + sd4b_prog_add[m];

                    if ((n = u - 0x40 + 6) == 0x15)
                        if ((n += sd4b_rdn(&bits, 4)) == 0x15 + 0xF)
                            if ((n += sd4b_rdn(&bits, 8)) == 0x15 + 0xF + 0xFF)
                                if ((n += sd4b_rdn(&bits, 12)) == 0x15 + 0xF + 0xFF + 0xFFF) {
                                    n += sd4b_rdn(&bits, 16);
                                }
                };

                if ((uint8_t *)pout + m > p)
                {m = p - (uint8_t *)pout; printk(KERN_ERR "DMSDOS: sd4_decomp: Under !!!\n");};

                if (p + n > pe)
                {n = pe - p; printk(KERN_ERR "DMSDOS: sd4_decomp: Over !!!!\n");};

                /*memcpy(p,p-m,n);p+=n;*/
                r = p - m;

                M_MOVSB(p, r, n); /* copy/repeat function */
            };
        };

        if ((u == OUT_OVER) && len_150) {
            int i;

            if ((i = sd4b_rdn(&bits, len_150)) == huf->cd_ch[len_150] - 1) { u = 0x50; }
            else { printk(KERN_ERR "DMSDOS: sd4_decomp: End read %X and should be %X\n", i, (int)huf->cd_ch[len_150] - 1); }
        };

        if (u == 0x50) {
            FREE(huf);
            return (p - (uint8_t *)pout);
        } else {printk(KERN_ERR "DMSDOS: sd4_decomp: Error end token %X\n", u);};
    };

error:
    FREE(huf);

    return 0;
};

/*************** end code from sd4_bs1.c *********************************/

int stac_decompress(unsigned char *buf_in, int len_in,
                    unsigned char *buf_out, int len_out)
{
    int alg_info;

    alg_info = le16_to_cpu(*(uint16_t *)buf_in);

    switch (alg_info) {
    case 0x0081:
        return (sd4_decomp(buf_in, len_in, buf_out, len_out, 0));

    case 0x5344:
        /* call DS decompression from dmsdos_dec */
        /* mde.size_hi_minus_1=(len_out-1)/SECTOR_SIZE; */
        /* return(dbl_decompress(buf_out,buf_in,&mde)); */
        return (ds_dec(buf_in, len_in, buf_out, len_out, 0x4000));

    default:
        return (sd3_decomp(buf_in, len_in, buf_out, len_out, 0));
    };
}

/* Specification:
   This function reads a stacker cluster into clusterd.
   It must take care of fragmentation and decompression.
   In case of failure it must return a negative error code,
   otherwise it returns number of used bytes in cluster.
*/
int stac_read_cluster(struct super_block *sb, unsigned char *clusterd,
                      int clusternr)
{
    int sect;
    int count, val, bytesperclust;
    struct buffer_head *bh;
    uint8_t *clusterk;
    Stac_cwalk cw;
    Dblsb *dblsb = MSDOS_SB(sb)->private_data;

    /* Prepare buffers for next read of cluster */
    if (clusterd == NULL) {
        if ((val = stac_cwalk_init(&cw, sb, clusternr, 0)) > 0) {
            while ((sect = stac_cwalk_sector(&cw)) > 0) {
                dblspace_reada(sb, sect, cw.flen + 1);
                cw.flen = 0;
            };
        };

        stac_cwalk_done(&cw);

        return 0;
    }

    /* Regular start of cluster read */

    val = stac_cwalk_init(&cw, sb, clusternr, 2);

    if (val < 0) {
        printk(KERN_ERR "DMSDOS: stac_read_cluster: alloc error in cluster %d\n",
               clusternr);
        return -EIO;
    };

    bytesperclust = dblsb->s_sectperclust * SECTOR_SIZE;

    if (val == 0) {
        memset(clusterd, 0, bytesperclust);
        /* I am not happy, that I cannot consider this as error (printk),
           but dblspace_getblk must fill rest of cluster and cannot
           call noread, some cases in dblspace_file_write are problematic too,
                                                            Pavel */
        LOG_CLUST("DMSDOS: stac_read_cluster: lost cluster (cluster %d)\n",
                  clusternr);
        return 0;
    }

    if (cw.compressed) {
        clusterk = (unsigned char *)MALLOC(cw.bytes_in_clust);

        if (clusterk == NULL) {
            printk(KERN_ERR "DMSDOS: stac_read_cluster: no memory!\n");
            stac_cwalk_done(&cw);
            return -EIO;
        }
    } else { clusterk = clusterd; }

    count = 0;

    while ((sect = stac_cwalk_sector(&cw)) > 0) {
        bh = raw_bread(sb, sect);

        if (bh == NULL) {
error1:

            if (cw.compressed) { FREE(clusterk); }

            stac_cwalk_done(&cw);
            return -EIO;
        }

        if (count + cw.bytes > cw.bytes_in_clust) {
            printk(KERN_ERR "DMSDOS: stac_read_cluster: internal cw error 1 cluster=%d\n",
                   clusternr);
            raw_brelse(sb, bh);
            goto error1;
        };

        memcpy(clusterk + count, bh->b_data + cw.offset, cw.bytes);

        count += cw.bytes;

        raw_brelse(sb, bh);
    };

    if (count != cw.bytes_in_clust) {
        printk(KERN_ERR "DMSDOS: stac_read_cluster: internal cw error 2 cluster=%d\n",
               clusternr);
        goto error1;
    };

    if (cw.compressed) {
        count = stac_decompress(clusterk, count, clusterd, bytesperclust);
        FREE(clusterk);

        if (!count) {
            printk(KERN_ERR "DMSDOS: stac_read_cluster: decompression error cluster=%d\n",
                   clusternr);
        };
    };

    stac_cwalk_done(&cw);

    if (count <= 0) { return -EIO; }

    if (bytesperclust - count > 0) { memset(clusterd + count, 0, bytesperclust - count); }

    return (count);
}
