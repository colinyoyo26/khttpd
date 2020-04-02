#include "bignum.h"

char *fib_sequence(long long k)
{
    unsigned long long mask = 1ull << (BITS - 1);
    unsigned int off = __builtin_clzll(k) + 1;
    mask >>= off;

    bn fcur, fnext, t1, t2;
    bn fcur_sqrt;
    bn_init(&fcur);
    bn_init(&fnext);
    bn_init(&t1);
    bn_init(&t2);
    bn_init(&fcur_sqrt);

    bn_assign(&fcur, 1);
    bn_assign(&fnext, 1);

    if (unlikely(k <= 1)) {
        bn_assign(&fcur, k);
        return bn_hex(&fcur);
    }

    for (; mask; mask >>= 1) {
        bn_sll(&t1, &fnext, 1);
        bn_sub(&t1, &t1, &fcur);
        bn_mul(&t1, &t1, &fcur);
        bn_mul(&fcur_sqrt, &fcur, &fcur);
        bn_mul(&t2, &fnext, &fnext);
        bn_add(&t2, &t2, &fcur_sqrt);
        bn_swap(&fcur, &t1);
        bn_swap(&fnext, &t2);
        if (k & mask) {
            bn_add(&t1, &fcur, &fnext);
            bn_swap(&fcur, &fnext);
            bn_swap(&fnext, &t1);
        }
    }

    char *buf = bn_hex(&fcur);

    bn_free(&fcur);
    bn_free(&fnext);
    bn_free(&t1);
    bn_free(&t2);
    bn_free(&fcur_sqrt);

    return buf;
}