#ifndef __FLEXALLOC_CS_CNS_H
#define __FLEXALLOC_CS_CNS_H

#include "flexalloc.h"
struct fla_cs_cns
{
  int dummy;
};

int fla_cs_cns_pool_check(struct flexalloc *fs, uint32_t const obj_nlb);
int fla_cs_cns_slab_offset(struct flexalloc const *fs, uint32_t const slab_id,
                           uint64_t const slabs_base, uint64_t *slab_offset);
int fla_cs_cns_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle,
                           struct fla_object *obj);
int fla_cs_cns_object_destroy(struct flexalloc *fs, struct fla_pool const *pool_handle,
                              struct fla_object *obj);
int fla_cs_cns_init(struct flexalloc *fs, const uint64_t flags);
int fla_cs_cns_fini(struct flexalloc *fs, const uint64_t flags);

#endif // __FLEXALLOC_CS_CNS_H

