#include "crypto_aead256.h"
#include "verify_16.inc"
#include <dlfcn.h>
#include <stdio.h>
#include<string.h>
//#include "core_aes256encrypt.inc"
//#define AES(out,in,k) crypto_core_aes256encrypt(out,in,k,0)
extern int mode;
#define fbits(y)  ((y) & 0xc0)>>6
#define sbits(y)  ((y) & 0x30)>>4
#define ibits(y)  ((y) & 0x0f) 
#define fetch(mode)      slevel= sbits(mode);   num=ibits(mode); \
                         sprintf(buf,"enc_s%02d_n%02d",slevel,num);    \
                         function op = (function) dlsym(cx->handle, buf);  \
                         if (op == NULL) {                             \
                                           fprintf(stderr, "%s\n", dlerror());\
                                          return -1;\
                         }


static void store32(unsigned char *x,unsigned long long u)
{
  int i;
  for (i = 3;i >= 0;--i) { x[i] = u; u >>= 8; }
}

static void store64(unsigned char *x,unsigned long long u)
{
  int i;
  for (i = 7;i >= 0;--i) { x[i] = u; u >>= 8; }
}

/*
a = (a + x) * y in the finite field
16 bytes in a
xlen bytes in x; xlen <= 16; x is implicitly 0-padded
16 bytes in y
*/
  void addmul(unsigned char *a,
  const unsigned char *x,long long xlen,
  const unsigned char *y)
{
  int i;
  int j;
  unsigned char abits[128];
  unsigned char ybits[128];
  unsigned char prodbits[256];
  for (i = 0;i < xlen;++i) a[i] ^= x[i];
  for (i = 0;i < 128;++i) abits[i] = (a[i / 8] >> (7 - (i % 8))) & 1;
  for (i = 0;i < 128;++i) ybits[i] = (y[i / 8] >> (7 - (i % 8))) & 1;
  for (i = 0;i < 256;++i) prodbits[i] = 0;
  for (i = 0;i < 128;++i)
    for (j = 0;j < 128;++j)
      prodbits[i + j] ^= abits[i] & ybits[j];
  for (i = 127;i >= 0;--i) {
    prodbits[i] ^= prodbits[i + 128];
    prodbits[i + 1] ^= prodbits[i + 128];
    prodbits[i + 2] ^= prodbits[i + 128];
    prodbits[i + 7] ^= prodbits[i + 128];
    prodbits[i + 128] ^= prodbits[i + 128];
  }
  for (i = 0;i < 16;++i) a[i] = 0;
  for (i = 0;i < 128;++i) a[i / 8] |= (prodbits[i] << (7 - (i % 8)));
}

static unsigned char zero[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


int ed_init(EDcontext *cx,  const unsigned char *npub, const unsigned char *k )
{
int i;
  int lastmode;
  char buf[1024];
  int slevel ;
  int num ; 

   cx->handle = dlopen("./LIB/lib_enc.so", RTLD_NOW|RTLD_GLOBAL);
         if (cx->handle == NULL) {
         fprintf(stderr, "%s\n", dlerror());
         return 1;
         }

  typedef void (*function) ( unsigned char *, unsigned char *, unsigned char *);
  fetch(mode)
  for (i = 0;i < crypto_aead_KEYBYTES;++i) cx->kcopy[i] = k[i];
  op(zero,cx->kcopy, cx->H);
  for (i = 0;i < 12;++i) cx->J[i] = npub[i];
  cx->index = 1;
  store32(cx-> J + 12 ,1);
 
  op(cx->J, cx->kcopy,cx->T);  

  for (i = 0;i < 16;++i) cx->accum[i] = 0;
  for (i = 0;i < 16;++i) cx->paccum[i] = 0;
  store64(cx->finalblock,0);
  store64(cx->finalblock + 8,0);
  cx->mlen=0;
  cx->adlen=0;
  return 0;
}


int enc_update( EDcontext *cx, 
  unsigned char *c,unsigned long long *clen,
  const unsigned char *m,unsigned long long mlen
 // const unsigned char *ad,unsigned long long adlen,
 // const unsigned char *nsec,
 // const unsigned char *npub,
 // const unsigned char *k//, unsigned char mode//  void (*op)(unsigned char *, unsigned char *, unsigned char *)
)
{  
    unsigned char stream[16];
  int lastmode;
  char buf[1024];
  int slevel ;
  int num,i ; 
  typedef void (*function) ( unsigned char *, unsigned char *, unsigned char *);   
     
     fetch(mode)
     cx->mlen+=mlen;
     *clen=0;
    while (mlen > 0) {
    unsigned long long blocklen = 16;
    if (mlen < blocklen) blocklen = mlen;
    cx->index=cx->index+1;
    store32(cx->J + 12 ,cx->index);
 
    op(cx->J,cx->kcopy,stream);
    for (i = 0;i < blocklen;++i) c[i] = m[i] ^ stream[i];
    addmul(cx->accum,c,blocklen,cx->H);
           //printf("acc ");  for (i = 0;i < 16;++i)printf("%02x ", cx->accum[i]); printf("\n");
           //printf("ccc "); for (i = 0;i < blocklen;++i)printf("%02x ", c[i]); printf("\n");
    c += blocklen;*clen+=blocklen;
    m += blocklen;
    mlen -= blocklen;
  }

//   printf("acc ");  for (i = 0;i < 16;++i)printf("%02x ", cx->accum[i]); printf("\n");
  return 0;
}
int enc_final( EDcontext *cx, unsigned char *t)
{   
  int i;
  store64(cx-> finalblock + 8 , 8 * cx->mlen);
  //printf("finblk ");  for (i = 0;i < 16;++i)printf("%02x ", cx->finalblock[i]);printf("\n");
  //printf("acc ");  for (i = 0;i < 16;++i)printf("%02x ", cx->accum[i]); printf("\n");
  //printf("h ");  for (i = 0;i < 16;++i)printf("%02x ", cx->H[i]);   printf("\n");
   
  addmul(cx->accum,cx->finalblock,16,cx->H);
  for (i = 0;i < 16;++i) t[i] = cx->T[i] ^ cx->accum[i];
  return 0;
}

int dec_update( EDcontext *cx, 
  unsigned char *m,unsigned long long *mlen,
  const unsigned char *c,unsigned long long clen
 // const unsigned char *ad,unsigned long long adlen,
 // const unsigned char *nsec,
 // const unsigned char *npub,
 // const unsigned char *k//, unsigned char mode//  void (*op)(unsigned char *, unsigned char *, unsigned char *)
)
{  
    unsigned char stream[16];
   int lastmode;
  char buf[1024];
  int slevel ;
  int num ,i; 
  typedef void (*function) ( unsigned char *, unsigned char *, unsigned char *);   
  
     fetch(mode)
     cx->mlen+=clen;
    while (clen > 0) {
    unsigned long long blocklen = 16;
    if (clen < blocklen) blocklen = clen;
    cx->index=cx->index+1;
    store32(cx->  J + 12  ,cx->index);
 
    op(cx->J,cx->kcopy,stream);
    for (i = 0;i < blocklen;++i) m[i] = c[i] ^ stream[i];
    memcpy(cx->paccum, cx->accum,16);
    addmul(cx->accum,c,blocklen,cx->H);
       //printf("acc ");  for (i = 0;i < 16;++i)printf("%02x ", cx->accum[i]); printf("\n");
    c += blocklen;
    m += blocklen;
    clen -= blocklen;
  }


  return 0;
}

int dec_final( EDcontext *cx, unsigned char *t)
{   
  int i;    unsigned char ts[16];
  store64(cx-> finalblock + 8 , 8 * cx->mlen);
  //printf("finblk ");  for (i = 0;i < 16;++i)printf("%02x ", cx->finalblock[i]);printf("\n");
  // printf("acc ");  for (i = 0;i < 16;++i)printf("%02x ", cx->accum[i]); printf("\n");
  // printf("h ");  for (i = 0;i < 16;++i)printf("%02x ", cx->H[i]);   printf("\n");
  addmul(cx->accum,cx->finalblock,16,cx->H);
  for (i = 0;i < 16;++i) ts[i] = cx->T[i] ^ cx->accum[i];  
 // for (i = 0;i < 16;++i) printf("[%02x] ", ts[i]);printf("len=%d\n",cx->mlen);
  if (crypto_verify_16(t,ts) != 0) return -1;
  return 0;
}
 /*
int crypto_aead_encrypt(
  unsigned char *c,unsigned long long *clen,
  const unsigned char *m,unsigned long long mlen,
  const unsigned char *ad,unsigned long long adlen,
  const unsigned char *nsec,
  const unsigned char *npub,
  const unsigned char *k//, unsigned char mode//  void (*op)(unsigned char *, unsigned char *, unsigned char *)
)
{
  unsigned char kcopy[crypto_aead_KEYBYTES];
  unsigned char H[16];
  unsigned char J[16];
  unsigned char T[16];
  unsigned char accum[16];
  unsigned char stream[16];
  unsigned char finalblock[16];
  unsigned long long i;
  unsigned long long index;
 char buf[1024];
 int slevel; 
           
 int num;
 
 
void *handle;
        handle = dlopen("./LIB/lib_enc.so", RTLD_NOW|RTLD_GLOBAL);
         if (handle == NULL) {
         fprintf(stderr, "%s\n", dlerror());
         return 1;
         }

  typedef void (*function) ( unsigned char *, unsigned char *, unsigned char *);
  fetch(mode)
  
  printf("Using %s\n",buf); 
  for (i = 0;i < crypto_aead_KEYBYTES;++i) kcopy[i] = k[i];

  *clen = mlen + 16;
  store64(finalblock,8 * adlen);
  store64(finalblock + 8,8 * mlen);

  //AES(H,zero,kcopy);
  op(zero,kcopy, H); 
 
  for (i = 0;i < 12;++i) J[i] = npub[i];
  index = 1;
  store32(J + 12,index);
  //AES(T,J,kcopy);
  op(J, kcopy,T);  

  for (i = 0;i < 16;++i) accum[i] = 0;

  while (adlen > 0) {
    unsigned long long blocklen = 16;
    if (adlen < blocklen) blocklen = adlen;
    addmul(accum,ad,blocklen,H);
    ad += blocklen;
    adlen -= blocklen;
  }

  while (mlen > 0) {
    unsigned long long blocklen = 16;
    if (mlen < blocklen) blocklen = mlen;
    ++index;
    store32(J + 12,index);
    //AES(stream,J,kcopy);
    op(J,kcopy,stream);
    for (i = 0;i < blocklen;++i) c[i] = m[i] ^ stream[i];
    addmul(accum,c,blocklen,H);
    c += blocklen;
    m += blocklen;
    mlen -= blocklen;
  }

  addmul(accum,finalblock,16,H);
  for (i = 0;i < 16;++i) c[i] = T[i] ^ accum[i];
  return 0;
}




int crypto_aead_decrypt(
  unsigned char *m,unsigned long long *outputmlen,
  unsigned char *nsec,
  const unsigned char *c,unsigned long long clen,
  const unsigned char *ad,unsigned long long adlen,
  const unsigned char *npub,
  const unsigned char *k//, void (*op)(unsigned char *, unsigned char *, unsigned char *)
)
{
  unsigned char kcopy[crypto_aead_KEYBYTES];
  unsigned char H[16];
  unsigned char J[16];
  unsigned char T[16];
  unsigned char accum[16];
  unsigned char stream[16];
  unsigned char finalblock[16];
  unsigned long long mlen;
  unsigned long long origmlen;
  unsigned long long index;
  unsigned long long i;
  const unsigned char *origc;

  int lastmode;
  char buf[1024];
  int slevel ;
  int num ; 
 
  void *handle;
  handle = dlopen("./LIB/lib_enc.so", RTLD_NOW|RTLD_GLOBAL);
  if (handle == NULL) {
         fprintf(stderr, "%s\n", dlerror());
         return 1;
   }

  typedef void (*function) ( unsigned char *, unsigned char *, unsigned char *);
  fetch(mode)
  lastmode=mode;
  printf("Using %s\n",buf); 

  
  for (i = 0;i < crypto_aead_KEYBYTES;++i) kcopy[i] = k[i];

  if (clen < 16) return -1;
  mlen = clen - 16;

  store64(finalblock,8 * adlen);
  store64(finalblock + 8,8 * mlen);

  if(lastmode!=mode) {fetch(mode)
  printf("Switched to %s\n",buf);}
  op(zero,kcopy, H); lastmode=mode;
  
  for (i = 0;i < 12;++i) J[i] = npub[i];
  index = 1;
  store32(J + 12,index);

  if(lastmode!=mode) {fetch(mode)
  printf("Switched to %s\n",buf);}
  op(J,kcopy, T);lastmode=mode;

  for (i = 0;i < 16;++i) accum[i] = 0;

  while (adlen > 0) {
    unsigned long long blocklen = 16;
    if (adlen < blocklen) blocklen = adlen;
    addmul(accum,ad,blocklen,H);
    ad += blocklen;
    adlen -= blocklen;
  }

  origc = c;
  origmlen = mlen;
  while (mlen > 0) {
    unsigned long long blocklen = 16;
    if (mlen < blocklen) blocklen = mlen;
    addmul(accum,c,blocklen,H);
    c += blocklen;
    mlen -= blocklen;
  }

  addmul(accum,finalblock,16,H);
  for (i = 0;i < 16;++i) accum[i] ^= T[i];
  if (crypto_verify_16(accum,c) != 0) return -1;

  c = origc;
  mlen = origmlen;
  *outputmlen = mlen;

  while (mlen > 0) {
    unsigned long long blocklen = 16;
    if (mlen < blocklen) blocklen = mlen;
    ++index;
    store32(J + 12,index);
    if(lastmode!=mode) {fetch(mode)
    printf("Switched to %s\n",buf);}
    op(J,kcopy,stream);lastmode=mode;
    for (i = 0;i < blocklen;++i) m[i] = c[i] ^ stream[i];
    c += blocklen;
    m += blocklen;
    mlen -= blocklen;
  }

  return 0;
}*/
