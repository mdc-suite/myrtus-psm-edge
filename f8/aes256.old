 
#include <wmmintrin.h> // Header for AES-NI intrinsics
#include <smmintrin.h> // Header for SSE4.1 (used for printing/manipulation)
#include <stdint.h>
#include <immintrin.h>
// Helper function to expand the 128-bit round key
 
#define KEY_EXP_ASSIST_1(r1, r2, r4) \
    r4 = _mm_shuffle_epi32(r4, _MM_SHUFFLE(3, 3, 3, 3)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, r4);

#define KEY_EXP_ASSIST_2(r1, r2, r3) \
    r3 = _mm_shuffle_epi32(r3, _MM_SHUFFLE(2, 2, 2, 2)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4)); \
    r1 = _mm_xor_si128(r1, r3);
// Generates the 11 round keys required for AES-128
void aes256_load_keys(const uint8_t *enc_key, __m128i *round_keys) {

    __m128i r1, r2, r4;

    // Load the 256-bit user key into two 128-bit registers
    r1 = _mm_loadu_si128((const __m128i*)enc_key);
    r2 = _mm_loadu_si128((const __m128i*)(enc_key + 16));

    round_keys[0] = r1;
    round_keys[1] = r2;
    r4 = _mm_aeskeygenassist_si128(r2, 0x01);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[2] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[3] = r2;
    r4 = _mm_aeskeygenassist_si128(r2, 0x02);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[4] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[5] = r2;

    // Round 6 & 7
    r4 = _mm_aeskeygenassist_si128(r2, 0x04);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[6] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[7] = r2;

    // Round 8 & 9
    r4 = _mm_aeskeygenassist_si128(r2, 0x08);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[8] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[9] = r2;

    // Round 10 & 11
    r4 = _mm_aeskeygenassist_si128(r2, 0x10);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[10] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[11] = r2;

    // Round 12 & 13
    r4 = _mm_aeskeygenassist_si128(r2, 0x20);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[12] = r1;
    r4 = _mm_aeskeygenassist_si128(r1, 0x00);     KEY_EXP_ASSIST_2(r2, r1, r4);     round_keys[13] = r2;

    // Final Round key generation
    r4 = _mm_aeskeygenassist_si128(r2, 0x40);     KEY_EXP_ASSIST_1(r1, r2, r4);     round_keys[14] = r1; 
}

// Encrypts a single 16-byte block
void aes256_encrypt(const uint8_t *plaintext, const __m128i *round_keys, uint8_t *ciphertext) {
    // Load plaintext into a 128-bit register
    __m128i block = _mm_loadu_si128((const __m128i*)plaintext);

    // Initial Round (XOR with the original key)
    block = _mm_xor_si128(block, round_keys[0]);

    // 9 Standard Rounds
    block = _mm_aesenc_si128(block, round_keys[1]);
    block = _mm_aesenc_si128(block, round_keys[2]);
    block = _mm_aesenc_si128(block, round_keys[3]);
    block = _mm_aesenc_si128(block, round_keys[4]);
    block = _mm_aesenc_si128(block, round_keys[5]);
    block = _mm_aesenc_si128(block, round_keys[6]);
    block = _mm_aesenc_si128(block, round_keys[7]);
    block = _mm_aesenc_si128(block, round_keys[8]);
    block = _mm_aesenc_si128(block, round_keys[9]);
    block = _mm_aesenc_si128(block, round_keys[10]);
    block = _mm_aesenc_si128(block, round_keys[11]);
    block = _mm_aesenc_si128(block, round_keys[12]);
    block = _mm_aesenc_si128(block, round_keys[13]);
    // 1 Final Round (Omits the MixColumns step automatically)
    block = _mm_aesenclast_si128(block, round_keys[14]);

    // Store the encrypted result
    _mm_storeu_si128((__m128i*)ciphertext, block);
}


void aes256(uint8_t * plaintext , uint8_t * key,uint8_t * ciphertext)
{    __m128i round_keys[15];
    aes256_load_keys(key, round_keys);    
    aes256_encrypt(plaintext, round_keys, ciphertext);
}
/*
int main() {
    uint8_t key[32]        = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81, 0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7, 0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4};
    uint8_t plaintext[16]  = {0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96, 0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A };
    uint8_t ciphertext[16] = {0};
    uint8_t rk[16] = {0};
    __m128i round_keys[15];

    aes256_load_keys(key, round_keys);
 /*   for(int i =0;i<=14;i++){
    _mm_storeu_si128((__m128i*)rk, round_keys[i]);
    for(int j = 0; j < 16; j++) printf("%02x ", rk[j]);
  printf("\n");
    }*//*
    aes256_encrypt(plaintext, round_keys, ciphertext);

    printf("Ciphertext: ");
    for(int i = 0; i < 16; i++) printf("%02x ", ciphertext[i]);
    printf("\n");

    return 0;
}*/
