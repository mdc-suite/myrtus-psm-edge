/* f8/aes256.c -- AES-256, single block, using the AES instructions of the host.
 *
 *   x86-64  : AES-NI (upstream implementation, unchanged)
 *   aarch64 : ARMv8 Crypto Extensions (LOGBOOK M-A9)
 *
 * al3monni mod: the ARM implementation, and the split below. The port used to
 * replace the AES-NI path, which cost the x86-64 build these two backends: a
 * walk there registered six of eight. Both paths now live side by side and the
 * preprocessor picks one, so every architecture registers all eight.
 *
 * Both produce identical ciphertext: the ARM side expands the key in scalar C
 * to the same schedule AES-NI's keygenassist produces.
 */
#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)

#include <wmmintrin.h>   /* AES-NI intrinsics */
#include <smmintrin.h>   /* SSE4.1 */
 
#include <wmmintrin.h> // Header for AES-NI intrinsics
#include <smmintrin.h> // Header for SSE4.1 (used for printing/manipulation)
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
static void aes256_load_keys(const uint8_t *enc_key, __m128i *round_keys) {

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
static void aes256_encrypt(const uint8_t *plaintext, const __m128i *round_keys, uint8_t *ciphertext) {
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

#elif defined(__aarch64__)

#include <arm_neon.h>    /* NEON + Crypto Extensions intrinsics */
#include <arm_neon.h>   // ARMv8 NEON + Crypto Extensions intrinsics

// ---- AES-256 key expansion (portable C, FIPS-197) ----
// Produces the same 240-byte (15 round key) schedule as the AES-NI version,
// so ciphertext is bit-identical. AES-256 (Nk=8) applies SubWord+RotWord+f8_rcon
// on words where i%8==0, and an EXTRA SubWord (no rotate) on words where
// i%8==4 -- that second case is the aes256-specific step.

static const uint8_t f8_sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static const uint8_t f8_rcon[8] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40};

// Expand 32-byte key into 240 bytes (15 round keys) for AES-256.
static void key_expansion_256(const uint8_t *key, uint8_t rk[240]) {
    for (int i = 0; i < 32; i++) rk[i] = key[i];
    int bytes = 32;      // start after the 8 initial words (Nk=8)
    int rconi = 1;
    while (bytes < 240) {
        uint8_t t[4];
        for (int i = 0; i < 4; i++) t[i] = rk[bytes - 4 + i];
        int word = bytes / 4;          // index of the word being generated
        if (word % 8 == 0) {
            // RotWord + SubWord + f8_rcon
            uint8_t tmp = t[0]; t[0]=t[1]; t[1]=t[2]; t[2]=t[3]; t[3]=tmp;
            for (int i = 0; i < 4; i++) t[i] = f8_sbox[t[i]];
            t[0] ^= f8_rcon[rconi++];
        } else if (word % 8 == 4) {
            // AES-256 extra step: SubWord only (no rotate, no f8_rcon)
            for (int i = 0; i < 4; i++) t[i] = f8_sbox[t[i]];
        }
        for (int i = 0; i < 4; i++) {
            rk[bytes] = rk[bytes - 32] ^ t[i];
            bytes++;
        }
    }
}

// ---- AES-256 encrypt using ARMv8 Crypto Extensions ----
// vaeseq_u8(state,key) = (state XOR key)->SubBytes->ShiftRows ; vaesmcq_u8 = MixColumns.
// Key is XORed at the START of vaese, so round_keys[i] feeds vaese and the
// final AddRoundKey is a bare veorq with round_keys[14].
void aes256(uint8_t *plaintext, uint8_t *key, uint8_t *ciphertext) {
    uint8_t rk[240];
    key_expansion_256(key, rk);

    uint8x16_t block = vld1q_u8(plaintext);

    // Rounds 0..12: AddRoundKey + SubBytes + ShiftRows + MixColumns
    for (int i = 0; i < 13; i++) {
        block = vaeseq_u8(block, vld1q_u8(rk + 16 * i));
        block = vaesmcq_u8(block);
    }
    // Round 13: AddRoundKey(rk_13) + SubBytes + ShiftRows (NO MixColumns)
    block = vaeseq_u8(block, vld1q_u8(rk + 16 * 13));
    // Final AddRoundKey with rk_14
    block = veorq_u8(block, vld1q_u8(rk + 16 * 14));

    vst1q_u8(ciphertext, block);
}

#else
#error "f8/aes256.c: this backend needs AES instructions (x86-64 AES-NI or ARMv8 Crypto Extensions)"
#endif
