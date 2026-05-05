#ifndef MT19937_H
#define MT19937_H

#include <stdint.h>

typedef struct
{
    uint32_t state_array[624];      // the array for the state vector 
    int state_index;                // index into state vector array
} mt_state;

uint32_t random_uint32(mt_state* state);
void seed_initial_state(mt_state* state, uint32_t seed);
void set_initial_state_key(mt_state* state, char* key);
void set_initial_state(mt_state* state, uint32_t* seed_array);

#endif
