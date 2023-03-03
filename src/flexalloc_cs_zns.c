#include "flexalloc_cs_zns.h"
#include "flexalloc_mm.h"

static int
fla_znd_manage_zones_object_finish(struct flexalloc *fs, struct fla_pool const *pool_handle,
                                   struct fla_object *obj)
{
  int err = 0;
  uint64_t obj_slba = fla_object_slba(fs, obj, pool_handle);
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  struct fla_pool_entry_fnc const * pool_entry_fnc = (fs->pools.entrie_funcs + pool_handle->ndx);
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  for (uint32_t fla_obj = 0; fla_obj < num_fla_objs; fla_obj++)
  {
    err |= fla_xne_dev_znd_send_mgmt(fs->dev.dev,
                                     obj_slba + (fs->fla_cs.fla_cs_zns->nzsect * fla_obj),
                                     XNVME_SPEC_ZND_CMD_MGMT_SEND_FINISH, false);
  }
  FLA_ERR(err, "fla_xne_dev_znd_send_mgmt_finish()");
  return err;
}

static int
fla_znd_manage_zones_object_reset(struct flexalloc *fs, struct fla_pool const *pool_handle,
                                  struct fla_object * obj)
{
  int err = 0;
  uint64_t obj_slba = fla_object_slba(fs, obj, pool_handle);
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  struct fla_pool_entry_fnc const * pool_entry_fnc = (fs->pools.entrie_funcs + pool_handle->ndx);
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  for (uint32_t fla_obj = 0; fla_obj < num_fla_objs; fla_obj++)
  {
    err |= fla_xne_dev_znd_send_mgmt(fs->dev.dev,
                                     obj_slba + (fs->fla_cs.fla_cs_zns->nzsect * fla_obj),
                                     XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET, false);
  }
  FLA_ERR(err, "fla_xne_dev_znd_send_mgmt_reset()");
  return err;
}

int
fla_cs_zns_slab_offset(struct flexalloc const *fs, uint32_t const slab_id,
                       uint64_t const slabs_base, uint64_t *slab_offset)
{
  int err = fla_cs_cns_slab_offset(fs, slab_id, slabs_base, slab_offset);
  if (FLA_ERR(err, "fls_cs_cns_slab_offset()"))
    return err;

  if (*slab_offset % fs->fla_cs.fla_cs_zns->nzsect)
    *slab_offset += (*slab_offset % fs->fla_cs.fla_cs_zns->nzsect);

  return 0;
}

int
fla_cs_zns_init(struct flexalloc *fs, uint64_t const flags)
{
  fs->fla_cs.fla_cs_zns = malloc(sizeof(struct fla_cs_zns));
  if (FLA_ERR(!fs->fla_cs.fla_cs_zns, "malloc()"))
    return -ENOMEM;
  fs->fla_cs.fla_cs_zns->nzones = fla_xne_dev_znd_zones(fs->dev.dev);
  fs->fla_cs.fla_cs_zns->nzsect = fla_xne_dev_znd_sect(fs->dev.dev);

  fs->fla_cs.fncs.init_cs = fla_cs_zns_init;
  fs->fla_cs.fncs.fini_cs = fla_cs_zns_fini;
  fs->fla_cs.fncs.check_pool = fla_cs_zns_pool_check;
  fs->fla_cs.fncs.slab_offset = fla_cs_zns_slab_offset;
  fs->fla_cs.fncs.object_seal = fla_cs_zns_object_seal;
  fs->fla_cs.fncs.object_destroy = fla_cs_zns_object_destroy;
  return 0;
}

int
fla_cs_zns_fini(struct flexalloc *fs, uint64_t const flags)
{
  free(fs->fla_cs.fla_cs_zns);
  return 0;
}

int
fla_cs_zns_pool_check(struct flexalloc *fs, uint32_t const obj_nlb)
{
  if (FLA_ERR(obj_nlb != fs->fla_cs.fla_cs_zns->nzsect,
              "object size (%"PRIu32") != formated zone size(%"PRIu64")",
              obj_nlb, fs->fla_cs.fla_cs_zns->nzsect))
    return 1;
  return 0;
}

int
fla_cs_zns_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle,
                       struct fla_object *obj)
{
  return fla_znd_manage_zones_object_finish(fs, pool_handle, obj);
}

int
fla_cs_zns_object_destroy(struct flexalloc *fs, struct fla_pool const *pool_handle,
                          struct fla_object *obj)
{
  return fla_znd_manage_zones_object_reset(fs, pool_handle, obj);
}
