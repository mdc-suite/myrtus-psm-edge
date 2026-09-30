#ifndef crypto_aead256_H
#define crypto_aead256_H

#define crypto_aead_KEYBYTES 32

  void addmul(unsigned char * ,
  const unsigned char * ,long long  ,
  const unsigned char * );
typedef struct
{
  unsigned char kcopy[crypto_aead_KEYBYTES];
  unsigned char H[16];
  unsigned char J[16];
  unsigned char T[16];
  unsigned char accum[16];
  unsigned char paccum[16];  
  unsigned char finalblock[16]; 
  void *handle;  
  unsigned long long index;
  unsigned long long mlen;
} EDcontext;
int ed_init(EDcontext * ,  const unsigned char * , const unsigned char *  );
int dec_update( EDcontext * , 
  unsigned char * ,
  const unsigned char * ,unsigned long long  
 
);
int dec_final( EDcontext * , unsigned char * );
#endif
