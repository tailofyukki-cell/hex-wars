#include "rng.h"

void rng_seed(Rng *r, uint32_t seed)
{
    r->s = seed ? seed : 0x9E3779B9u;
}

uint32_t rng_next(Rng *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

int rng_range(Rng *r, int lo, int hi)
{
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(rng_next(r) % span);
}
