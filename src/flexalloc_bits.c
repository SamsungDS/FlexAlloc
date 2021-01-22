#include "flexalloc_bits.h"

uint32_t
ntz(uint32_t x)
{
  /*
   * Count number of trailing zero bits.
   * Aside from the easy case (x == 0, -> 32 'trailing' zeros), we count the
   * trailing bits in decreasing batch sizes (16, 8, 4, 2, 1). Note we shift out
   * matched sequence before proceeding to match on the smaller batch.
   * We first check the easy case (x == 0), then proceed to count.
   */
  uint32_t n;
  if (x == 0) return 32u;
  n = 1u;
  if ((x & 0x0000FFFF) == 0)
  {
    n += 16u;
    x >>= 16;
  }
  if ((x & 0x000000FF) == 0)
  {
    n += 8u;
    x >>= 8;
  }
  if ((x & 0x0000000F) == 0)
  {
    n += 4u;
    x >>= 4;
  }
  if ((x & 0x00000003) == 0)
  {
    n += 2u;
    x >>= 2;
  }
  // count last bit
  return n - (x & 1);
}

uint32_t
count_set_bits(uint32_t val)
{
  // Count number of set bits, following algorithm from 'Hacker's Delight' book.
  val = (val & 0x55555555) + ((val >> 1) & 0x55555555);
  val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
  val = (val & 0x0F0F0F0F) + ((val >> 4) & 0x0F0F0F0F);
  val = (val & 0x00FF00FF) + ((val >> 8) & 0x00FF00FF);
  return (val & 0x0000FFFF) + ((val >> 16) & 0x0000FFFF);
}
