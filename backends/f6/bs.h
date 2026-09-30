#ifndef _BS_H_
#define _BS_H_

#include <stdint.h>

#define BLOCK_SIZE          128
#define KEY_SCHEDULE_SIZE   240
#define WORD_SIZE           64
#define BS_BLOCK_SIZE       (BLOCK_SIZE * WORD_SIZE / 8)
#define WORDS_PER_BLOCK     (BLOCK_SIZE / WORD_SIZE)

#if (WORD_SIZE==64)
    typedef uint64_t    word_t;
    #define ONE         1ULL
    #define MUL_SHIFT   6
    #define WFMT        "lx"
    #define WPAD        "016"
    #define __builtin_bswap_wordsize(x) __builtin_bswap64(x)
#elif (WORD_SIZE==32)
    typedef uint32_t    word_t;
    #define ONE         1UL
    #define MUL_SHIFT   5
    #define WFMT        "x"
    #define WPAD        "08"
    #define __builtin_bswap_wordsize(x) __builtin_bswap32(x)
#elif (WORD_SIZE==16)
    typedef uint16_t    word_t;
    #define ONE         1
    #define MUL_SHIFT   4
    #define WFMT        "hx"
    #define WPAD        "04"
    #define __builtin_bswap_wordsize(x) __builtin_bswap16(x)
#elif (WORD_SIZE==8)
    typedef uint8_t     word_t;
    #define ONE         1
    #define MUL_SHIFT   3
    #define WFMT        "hhx"
    #define WPAD        "02"
    #define __builtin_bswap_wordsize(x) (x)
#else
#error "invalid word size"
#endif

void bs_transposef6(word_t * blocks);
void bs_transpose_revf6(word_t * blocks);
void bs_transpose_dstf6(word_t * transpose, word_t * blocks);

void bs_sboxf6(word_t U[8]);
void bs_sbox_revf6(word_t U[8]);

void bs_shiftrowsf6(word_t * B);
void bs_shiftrows_revf6(word_t * B);

void bs_mixcolumnsf6(word_t * B);
void bs_mixcolumns_revf6(word_t * B);

void bs_shiftmixf6(word_t * B);

void bs_addroundkeyf6(word_t * B, word_t * rk);
void bs_apply_sboxf6(word_t * input);
void bs_apply_sbox_revf6(word_t * input);


void expand_keyf6(unsigned char *in);
void bs_expand_keyf6(word_t (* rk)[BLOCK_SIZE], uint8_t * key);

void bs_cipherf6(word_t state[BLOCK_SIZE], word_t (* rk)[BLOCK_SIZE]);
void bs_cipher_revf6(word_t state[BLOCK_SIZE], word_t (* rk)[BLOCK_SIZE]);

#endif
