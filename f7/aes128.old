#include <stdio.h>
#include <wmmintrin.h> // Header for AES-NI intrinsics
#include <smmintrin.h> // Header for SSE4.1 (used for printing/manipulation)
#include <stdint.h>

// Helper function to expand the 128-bit round key
static inline __m128i aes_128_key_expand(__m128i key, __m128i keygened) {
    keygened = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3, 3, 3, 3));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygened);
}

// Generates the 11 round keys required for AES-128
void aes128_load_keys(const uint8_t *enc_key, __m128i *round_keys) {
    round_keys[0] = _mm_loadu_si128((const __m128i*)enc_key);
    round_keys[1]  = aes_128_key_expand(round_keys[0],  _mm_aeskeygenassist_si128(round_keys[0],  0x01));
    round_keys[2]  = aes_128_key_expand(round_keys[1],  _mm_aeskeygenassist_si128(round_keys[1],  0x02));
    round_keys[3]  = aes_128_key_expand(round_keys[2],  _mm_aeskeygenassist_si128(round_keys[2],  0x04));
    round_keys[4]  = aes_128_key_expand(round_keys[3],  _mm_aeskeygenassist_si128(round_keys[3],  0x08));
    round_keys[5]  = aes_128_key_expand(round_keys[4],  _mm_aeskeygenassist_si128(round_keys[4],  0x10));
    round_keys[6]  = aes_128_key_expand(round_keys[5],  _mm_aeskeygenassist_si128(round_keys[5],  0x20));
    round_keys[7]  = aes_128_key_expand(round_keys[6],  _mm_aeskeygenassist_si128(round_keys[6],  0x40));
    round_keys[8]  = aes_128_key_expand(round_keys[7],  _mm_aeskeygenassist_si128(round_keys[7],  0x80));
    round_keys[9]  = aes_128_key_expand(round_keys[8],  _mm_aeskeygenassist_si128(round_keys[8],  0x1B));
    round_keys[10] = aes_128_key_expand(round_keys[9],  _mm_aeskeygenassist_si128(round_keys[9],  0x36));
}

// Encrypts a single 16-byte block
void aes128_encrypt(const uint8_t *plaintext, const __m128i *round_keys, uint8_t *ciphertext) {
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

    // 1 Final Round (Omits the MixColumns step automatically)
    block = _mm_aesenclast_si128(block, round_keys[10]);

    // Store the encrypted result
    _mm_storeu_si128((__m128i*)ciphertext, block);
}
void aes128(uint8_t * plaintext , uint8_t * key,uint8_t * ciphertext)
{    __m128i round_keys[11];
    aes128_load_keys(key, round_keys);    
    aes128_encrypt(plaintext, round_keys, ciphertext);
}

/*
int main() {
    uint8_t key[16]        = {0x84 , 0x81 , 0x85 , 0xdf , 0xa9 , 0x51 , 0xf1 , 0x1e , 0x13 , 0x97 , 0x24 , 0x8a , 0x6a , 0x69 , 0x8b , 0x17};
    uint8_t plaintext[16]  = {0xad , 0x40 , 0xa8 , 0x96 , 0xb1 , 0xc7 , 0xea , 0xa0 , 0x52 , 0xb1 , 0xa7 , 0x0b , 0xd6 , 0x45 , 0xdb , 0x66};
    uint8_t ciphertext[16] = {0};

    __m128i round_keys[11];

    aes128_load_keys(key, round_keys);
    aes128_encrypt(plaintext, round_keys, ciphertext);

    printf("Ciphertext: ");
    for(int i = 0; i < 16; i++) printf("%02x ", ciphertext[i]);
    printf("\n");

    return 0;
}*/
