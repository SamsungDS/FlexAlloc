#ifndef __FLEXALLOC_CS_ZNS_H
#define __FLEXALLOC_CS_ZNS_H

#include "flexalloc.h"
#include "flexalloc_util.h"
#include "flexalloc_cs_cns.h"

struct fla_cs_zns
{
  /// Number of zones
  uint32_t nzones;
  /// Number of sectors in zone
  uint64_t nzsect;
};

int fla_cs_zns_init(struct flexalloc *fs, uint64_t const flags);
int fla_cs_zns_fini(struct flexalloc *fs, uint64_t const flags);
int fla_cs_zns_pool_check(struct flexalloc *fs, uint32_t const obj_nlb);
int fla_cs_zns_slab_offset(struct flexalloc const *fs, uint32_t const slab_id,
                           uint64_t const slabs_base, uint64_t *slab_offset);
int fla_cs_zns_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle,
                           struct fla_object *obj);
int fla_cs_zns_object_destroy(struct flexalloc *fs, struct fla_pool const *pool_handle,
                              struct fla_object *obj);

#endif // __FLEXALLOC_CS_ZNS_H
