#ifndef _AES128_H_
#define _AES128_H_

#include <stdint.h>
#include <stddef.h>

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES encryption in CBC-mode of operation.
// CTR enables encryption in counter-mode.
// ECB enables the basic ECB 16-byte block algorithm. All can be enabled simultaneously.

// The #ifndef-guard allows it to be configured before #include'ing or at compile time.
 

#define AES_BLOCKLEN 16 // Block length in bytes - AES is 128b block only

 
    #define AES_KEYLEN 16   // Key length in bytes
    #define AES_keyExpSize 176
 
 
void aes128(unsigned char * , unsigned char * , unsigned char * );
#endif // _AES_H_
