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
int fla_dp_noop_get_next_available_slab(struct flexalloc * fs, struct fla_pool * fla_pool,
                             struct fla_slab_header ** slab);
uint32_t* fla_dp_noop_pool_slab_list_id(struct fla_slab_header const *slab,
                        struct fla_pools const *pools);
#endif // __FLEXALLOC_DP_NOOP_H
