/*
  gcc -DMAIN -O99 -Wall -pedantic mt19937.c -o mt19937 && ./mt19937
*/
/*
    Mersenne Twister: PseudoRandom Number Generator

    A C-program for MT19937: Integer version (1999/10/28)          

    random_uint32(mt_state* state) generates and returns one
    pseudorandom unsigned integer (32bit) which is uniformly
    distributed from 0 to 2^32 - 1.

    seed_initial_state(mt_state* state, uint32_t seed) sets initial
    values of the PRNG state of 624 words according to a given single
    32-bit seed word.

    set_initial_state_key(mt_state* state, char* key) sets the initial
    state according to the text of a key in a given string of chars.

    set_initial_state(mt_state* state, uint32_t* seed_array) sets the
    initial state to exactly the given seed_array.

    Before random_uint32() is called, an mt_state must be created and 
    seed_initial_state() or set_initial_state_key() or
    set_initial_state() must be called once.

    Coded by Takuji Nishimura, considering the suggestions by 
    Topher Cooper and Marc Rieffel in July-Aug. 1997.

    Refactoring modification for streamlined performance and
    better readability by Robert Bristow-Johnson  April 26th 2024

    This library is free software; you can redistribute it and/or   
    modify it under the terms of the GNU Library General Public 
    License as published by the Free Software Foundation; either 
    version 2 of the License, or (at your option) any later 
    version.
    This library is distributed in the hope that it will be useful, 
    but WITHOUT ANY WARRANTY; without even the implied warranty of 
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    See the GNU Library General Public License for more details. 
    You should have received a copy of the GNU Library General 
    Public License along with this library; if not, write to the 
    Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 
    02111-1307  USA

    Copyright (C) 1997, 1999 Makoto Matsumoto and Takuji Nishimura. 
    Any feedback is very welcome. For any question, comments, 
    see http://www.math.keio.ac.jp/matumoto/emt.html or email 
    matumoto@math.keio.ac.jp

    REFERENCE
    M. Matsumoto and T. Nishimura, 
    "Mersenne Twister: A 623-Dimensionally Equidistributed Uniform 
    Pseudo-Random Number Generator", 
    ACM Transactions on Modeling and Computer Simulation, 
    Vol. 8, No. 1, January 1998, pp 3--30.
*/

#include "mt19937.h"

// Period parameters 
#define N 624
#define M 397                       // not used directly in this version
#define K 227                       // redefined from N-M
#define MATRIX_A   0x9908b0dfUL     // constant vector A 
#define UPPER_MASK 0x80000000UL     // most significant w-r bits 
#define LOWER_MASK 0x7fffffffUL     // least significant r bits 

// Tempering parameters 
#define TEMPERING_MASK_B 0x9d2c5680UL
#define TEMPERING_MASK_C 0xefc60000UL
#define TEMPERING_SHIFT_U(x)  (x >> 11)
#define TEMPERING_SHIFT_S(x)  (x << 7)
#define TEMPERING_SHIFT_T(x)  (x << 15)
#define TEMPERING_SHIFT_L(x)  (x >> 18)

uint32_t random_uint32(mt_state* state)
{
    uint32_t* state_array = &(state->state_array[0]);
    int n = state->state_index;      // point to state N iterations ago 

    int k = n - (N-1);               // point to state N-1 iterations ago
    if (k < 0) k += N;               // modulo N circular indexing

    uint32_t x = (state_array[n] & UPPER_MASK) | (state_array[k] & LOWER_MASK);

    uint32_t xA = x >> 1;
    if (x & 0x00000001UL) xA ^= MATRIX_A;

    k = n - K;                       // point to state K iterations ago
    if (k < 0) k += N;               // modulo N circular indexing

    x = state_array[k] ^ xA;         // generate next value in the state
    state_array[n++] = x;            // update new state

    if (n >= N) n = 0;               // modulo N circular indexing
    state->state_index = n;          // 0 <= state_index <= N-1   always

    x ^= TEMPERING_SHIFT_U(x);
    x ^= TEMPERING_SHIFT_S(x) & TEMPERING_MASK_B;
    x ^= TEMPERING_SHIFT_T(x) & TEMPERING_MASK_C;
    x ^= TEMPERING_SHIFT_L(x);

    return x; 
}


/*
     Initializing the state_array from a seed 
*/
void seed_initial_state(mt_state* state, uint32_t seed) 
{
    uint32_t* state_array = &(state->state_array[0]);

    for (int n=0; n<N; n++)
    {
        seed = 1812433253UL * (seed ^ (seed >> 30)) + n;    // Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier.
        state_array[n] = seed;                              // suggested initial seed = 19650218UL 
    }

    state->state_index = 0;
}


/*
    Initializing the state_array from a text key.

    This is, hopefully, well scrambled but is a fully deterministic
    function of the key, which must be at least 8 characters.  If the
    key is less than 8 characters, the PRNG state is still adequately 
    initialized and the default seed is used.
*/
void set_initial_state_key(mt_state* state, char* key)  
{
    seed_initial_state(state, 19650218UL);

    uint32_t* state_array = &(state->state_array[0]);

    int offset = 0;
    int k = 0;
    while ((key[k] != '\0') && (k < 4*N))           // determine string length, stop at 4N
    {
        offset += (int)key[k++];                    // see how many times we circle around the ASCII table.
    }
 
    if (k >= 8)                                     // key must be at least 8 chars
    {
        offset &= 0x007f;
        offset += N-128;                            // N-128 <= offset <= N-1
        k -= 3;                                     // shorten reach by 3 chars

        int i = 0;
        for (int n=0; n<N; n++)
        {
            uint32_t key28bit = ( ((uint32_t)key[i+0] & 0x0000007fUL) << 21) 
                              | ( ((uint32_t)key[i+1] & 0x0000007fUL) << 14) 
                              | ( ((uint32_t)key[i+2] & 0x0000007fUL) << 7) 
                              | ( ((uint32_t)key[i+3] & 0x0000007fUL) );
            i += 4;
            if (i >= k) i -= k;                     // wrap i around
            
            state_array[n] += 1664525UL * (key28bit + n);

            int m = n - offset;
            if (m < 0) m += N;                      // modulo N

            state_array[n] ^= 1566083941UL * (state_array[m] ^ (state_array[m] >> 30));
        }
    }

    uint32_t alt_state_array[N];

    for (int n=0; n<N; n++)
    {
        alt_state_array[n] = random_uint32(state);      // scramble it up a bit with tempered random values
    }

    for (int n=0; n<N; n++)
    {
        state_array[n] = alt_state_array[(N-1)-n];      // swap with alternate state and reverse order
    }

    for (int n=0; n<2*N; n++)
    {
        random_uint32(state);     // this should scramble it up enough to dash any hope of reverse engineering the key
    }
}

/*
    Initialization by "seed_initial_state()" is an example. Theoretically, 
    there are 2^19937-1 possible states as an intial state. 
    This function allows to choose any of 2^19937-1 ones. 
    Essential bits in "seed_array[]" are the following 19937 bits: 
    (seed_array[0]&UPPER_MASK), seed_array[1], ..., seed_array[N-1]. 
    (seed_array[0]&LOWER_MASK) is not used in the state but it
    goes out (tempered) into the first output word. Theoretically, 
    (seed_array[0]&UPPER_MASK), seed_array[1], ..., seed_array[N-1] 
    can take any values except all zeros.

    The sizeof(seed_array[]) must be at least sizeof(state_array[]) = N = 624 
*/
void set_initial_state(mt_state* state, uint32_t* seed_array)
{
    uint32_t* state_array = &(state->state_array[0]);
 
    for (int n=0; n<N; n++)
    {
        state_array[n] = seed_array[n];
    }

    state->state_index = 0;
}

#ifdef MAIN
#include <stdio.h>
int main() {
  mt_state mt;
  seed_initial_state(&mt,1);
  for (int i = 0; i < 1000; ++i)
    printf("0x%08x\n",random_uint32(&mt));
  return 0;
}
#endif /* MAIN */
