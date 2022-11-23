// Copyright (C) 2022 Adam Manzanares <a.manzanares@samsung.com>
#include "flexalloc_znd.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"

int
fla_znd_manage_zones_object_finish(struct flexalloc *fs, struct fla_pool const *pool_handle,
                                   struct fla_object *obj)
{
  int err = 0;
  uint64_t obj_slba = fla_object_slba(fs, obj, pool_handle);
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  struct fla_pool_entry_fnc const * pool_entry_fnc = (fs->pools.entrie_funcs + pool_handle->ndx);
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  for (uint32_t strp = 0; strp < num_fla_objs; strp++)
  {
    err |= fla_xne_dev_znd_send_mgmt(fs->dev.dev, obj_slba + (fs->geo.nzsect * strp),
                                     XNVME_SPEC_ZND_CMD_MGMT_SEND_FINISH, false);
  }
  FLA_ERR(err, "fla_xne_dev_znd_send_mgmt_finish()");
  return err;
}

int
fla_znd_manage_zones_object_reset(struct flexalloc *fs, struct fla_pool const *pool_handle,
                                  struct fla_object * obj)
{
  int err = 0;
  uint64_t obj_slba = fla_object_slba(fs, obj, pool_handle);
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  struct fla_pool_entry_fnc const * pool_entry_fnc = (fs->pools.entrie_funcs + pool_handle->ndx);
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  for (uint32_t strp = 0; strp < num_fla_objs; strp++)
  {
    err |= fla_xne_dev_znd_send_mgmt(fs->dev.dev, obj_slba + (fs->geo.nzsect * strp),
                                     XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET, false);
  }
  FLA_ERR(err, "fla_xne_dev_znd_send_mgmt_reset()");
  return err;
}

