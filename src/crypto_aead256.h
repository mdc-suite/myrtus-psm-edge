#ifndef crypto_aead256_H
#define crypto_aead256_H



#define crypto_aead_encrypt crypto_aead_aes256gcmv1_ref_encrypt
#define crypto_aead_decrypt crypto_aead_aes256gcmv1_ref_decrypt
#define crypto_aead_KEYBYTES crypto_aead_aes256gcmv1_ref_KEYBYTES
#define crypto_aead_NSECBYTES crypto_aead_aes256gcmv1_ref_NSECBYTES
#define crypto_aead_NPUBBYTES crypto_aead_aes256gcmv1_ref_NPUBBYTES
#define crypto_aead_ABYTES crypto_aead_aes256gcmv1_ref_ABYTES
#define crypto_aead_NOOVERLAP crypto_aead_aes256gcmv1_NOOVERLAP
#define crypto_aead_PRIMITIVE "aes256gcmv1"

#define crypto_aead_aes256gcmv1_ref_KEYBYTES 32
#define crypto_aead_aes256gcmv1_ref_NSECBYTES 0
#define crypto_aead_aes256gcmv1_ref_NPUBBYTES 12
#define crypto_aead_aes256gcmv1_ref_ABYTES 16

  int crypto_aead_aes256gcmv1_ref_encrypt(unsigned char *,unsigned long long *,const unsigned char *,unsigned long long,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *,const unsigned char *);
  int crypto_aead_aes256gcmv1_ref_decrypt(unsigned char *,unsigned long long *,unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);

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
  unsigned long long mlen,adlen;
} EDcontext;
static void store32(unsigned char * ,unsigned long long  );
static void store64(unsigned char * ,unsigned long long  );
int ed_init(EDcontext * ,  const unsigned char * , const unsigned char *  );
int enc_update( EDcontext * , 
  unsigned char * ,unsigned long long *,
  const unsigned char * ,unsigned long long  
 
);
int enc_final( EDcontext * , unsigned char * );
int dec_update( EDcontext * , 
  unsigned char * ,unsigned long long * n,
  const unsigned char * ,unsigned long long  
 
);
int dec_final( EDcontext * , unsigned char * );
#endif
