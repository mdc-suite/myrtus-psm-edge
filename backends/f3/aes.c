
#include <string.h>
#include "aes.h"
#include "bs.h"
#include "utils.h"

void aes_ecb_encrypt(uint8_t * inputb , uint8_t * key,uint8_t * outputb  )
{
    word_t input_space[BLOCK_SIZE];
    word_t rk[11][BLOCK_SIZE];
    size_t size=16;
    memset(outputb,0,size);
    word_t * state = (word_t *)outputb;

    bs_expand_key(rk, key);

    while (size > 0)
    {
        if (size < BS_BLOCK_SIZE)
        {
            memset(input_space,0,BS_BLOCK_SIZE);
            memmove(input_space, inputb, size);
            bs_cipher(input_space,rk);
            memmove(outputb, input_space, size);
            size = 0;
            state += size;
        }
        else
        {
            memmove(state,inputb,BS_BLOCK_SIZE);
            bs_cipher(state,rk);
            size -= BS_BLOCK_SIZE;
            state += BS_BLOCK_SIZE;
        }

    }
}
 
