#ifndef __FLEXALLOC_DP_NOOP_H
#define __FLEXALLOC_DP_NOOP_H

#include "flexalloc.h"

struct fla_dp_noop_slab_list_ids
{
  uint32_t empty_slabs;
  uint32_t full_slabs;
  uint32_t partial_slabs;
};

int fla_dp_noop_fini(struct flexalloc *fs);
int fla_dp_noop_init(struct flexalloc *fs, const uint64_t flags);
#endif // __FLEXALLOC_DP_NOOP_H
