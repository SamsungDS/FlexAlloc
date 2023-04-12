#ifndef __FLEXALLOC_CS_H_
#define __FLEXALLOC_CS_H_
#include <libxnvme_geo.h>
#include <libxnvme_dev.h>
#include <stdint.h>
#include "flexalloc_shared.h"
struct flexalloc;
struct fla_geo;
struct fla_pool;
struct fla_object;

struct fla_cs_fncs
{
  int (*init_cs)(struct flexalloc *fs, const uint64_t flags);
  int (*fini_cs)(struct flexalloc *fs, const uint64_t flags);
  int (*check_pool)(struct flexalloc *fs, uint32_t const obj_nlb);
  int (*slab_offset)(struct flexalloc const *fs, uint32_t const slab_id,
                     uint64_t const slabs_base, uint64_t *slab_offset);
  int (*object_seal)(struct flexalloc *fs, struct fla_pool const *pool_handle,
                     struct fla_object *obj);
  int (*object_destroy)(struct flexalloc *fs, struct fla_pool const *pool_handle,
                        struct fla_object *obj);
  //int (*slab_trim)(struct flexalloc *fs, uint32_t const slab_id, struct fla_slab_header * h);
};

struct fla_cs
{
  enum fla_cs_t cs_t;
  union
  {
    struct fla_cs_cns   *fla_cs_cns;
    struct fla_cs_zns   *fla_cs_zns;
  };

  struct fla_cs_fncs fncs;
};

int
fla_init_cs(struct flexalloc *fs);

int
fla_cs_geo_check(struct xnvme_dev const *dev, struct fla_geo const *geo);

bool
fla_cs_is_type(struct flexalloc const *fs, enum fla_cs_t const cs_t);

#endif // __FLEXALLOC_CS_H_
