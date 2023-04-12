#include "flexalloc_cs_cns.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"

int
fla_cs_cns_pool_check(struct flexalloc *fs, uint32_t const obj_nlb)
{
  return 0;
}

int
fla_cs_cns_slab_offset(struct flexalloc const *fs, uint32_t const slab_id,
                       uint64_t const slabs_base, uint64_t *slab_offset)
{
  *slab_offset = slabs_base + (slab_id * fs->geo.slab_nlb);
  return 0;
}

int
fla_cs_cns_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle,
                       struct fla_object *obj)
{
  return 0;
}

int
fla_cs_cns_object_destroy(struct flexalloc *fs, struct fla_pool const *pool_handle,
                          struct fla_object *obj)
{
  return 0;
}

int
fla_cs_cns_fini(struct flexalloc *fs, const uint64_t flags)
{
  return 0;
}

static int
fla_cs_cns_slab_trim(struct flexalloc *fs, uint32_t const slab_id)
{
  int err;
  err = fla_xne_dev_send_deallocate(fs->dev.dev, fla_geo_slab_lb_off(fs, slab_id),
                                    fs->super->slab_nlb);
  if (FLA_ERR(err, "fla_xne_dev_send_deallocate()"))
    return -1;
  return 0;
}

int
fla_cs_cns_init(struct flexalloc *fs, const uint64_t flags)
{
  fs->fla_cs.fncs.init_cs = fla_cs_cns_init;
  fs->fla_cs.fncs.fini_cs = fla_cs_cns_fini;
  fs->fla_cs.fncs.check_pool = fla_cs_cns_pool_check;
  fs->fla_cs.fncs.slab_offset = fla_cs_cns_slab_offset;
  fs->fla_cs.fncs.object_seal = fla_cs_cns_object_seal;
  fs->fla_cs.fncs.object_destroy = fla_cs_cns_object_destroy;
  fs->fla_cs.fncs.slab_trim = fla_cs_cns_slab_trim;
  return 0;
}


