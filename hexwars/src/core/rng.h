/* rng.h - 決定論的乱数 xorshift32（仕様書 1.2） */
#ifndef HW_RNG_H
#define HW_RNG_H

#include <stdint.h>

typedef struct { uint32_t s; } Rng;

void     rng_seed(Rng *r, uint32_t seed);
uint32_t rng_next(Rng *r);
/* [lo, hi] の一様整数 */
int      rng_range(Rng *r, int lo, int hi);

#endif
