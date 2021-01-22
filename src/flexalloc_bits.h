#ifndef __FLEXALLOC_BITS_H_
#define __FLEXALLOC_BITS_H_

#include <stdint.h>

/**
 * Count number of trailing zero bits.
 *
 * Given a 4B/32b value, count the number of trailing zero bits before
 * encountering the first 1-bit.
 *
 * Examples:
 * - ntz(0) == 32
 * - ntz(~0) == 0
 * - ntz(4) == 2
 */
uint32_t
ntz(uint32_t x);

/**
 * Count number of set bits.
 *
 * Given a 4B/32b value, count the number of set bits.
 *
 * Examples:
 * - count_set_bits(0) == 0
 * - count_set_bits(~0) == 32
 * - count_set_bits(7) == 3
 */
uint32_t
count_set_bits(uint32_t val);

#endif // __FLEXALLOC_BITS_H_
