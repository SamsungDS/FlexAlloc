// Copyright (C) 2022 Adam Manzanares <a.manzanares@samsung.com>
#include "flexalloc_znd.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"

// Full zones are implicitly placed into the closed state so no need to manage anymore
void
fla_znd_zone_full(struct flexalloc *fs, uint32_t zone)
{
  struct fla_zs_entry *z_entry;

  TAILQ_FOREACH(z_entry, &fs->zs_thead, entries)
  {
    if (z_entry->zone_number == zone)
      break;
  }

  // We should probably warn here
  if (!z_entry)
    return;

  TAILQ_REMOVE(&fs->zs_thead, z_entry, entries);
  fs->zs_size--;
}

void
fla_znd_manage_zones_cleanup(struct flexalloc *fs)
{
  struct fla_zs_entry *z_entry;

  while ((z_entry = TAILQ_FIRST(&fs->zs_thead)))
  {
    TAILQ_REMOVE(&fs->zs_thead, z_entry, entries);
    free(z_entry);
  }
}

void
fla_znd_manage_zones(struct flexalloc *fs, uint32_t zone)
{

  struct fla_zs_entry *z_entry;
  uint64_t zlba;
  int ret;

  TAILQ_FOREACH(z_entry, &fs->zs_thead, entries)
  {
    if (z_entry->zone_number == zone)
      break;
  }

  if (z_entry)
  {
    // We found the entry so remove and reinsert at head
    TAILQ_REMOVE(&fs->zs_thead, z_entry, entries);
    TAILQ_INSERT_HEAD(&fs->zs_thead, z_entry, entries);
    return;
  }

  // We are under the number of zones limit
  if (fs->zs_size < fla_xne_dev_get_znd_mor(fs->dev.dev))
  {
    z_entry = malloc(sizeof(struct fla_zs_entry));
    assert(z_entry);
    z_entry->zone_number = zone;
    TAILQ_INSERT_HEAD(&fs->zs_thead, z_entry, entries);
    fs->zs_size++;
  }
  else // We have to close a zone
  {
    z_entry = TAILQ_LAST(&fs->zs_thead, zs_thead);
    assert(z_entry);
    zlba = z_entry->zone_number * fs->geo.nzsect;
    ret = fla_xne_dev_znd_send_mgmt(fs->dev.dev, zlba, XNVME_SPEC_ZND_CMD_MGMT_SEND_CLOSE, false);
    if (ret)
      FLA_ERR_PRINTF("Error trying to close zone at:%lu\n", zlba);

    z_entry->zone_number = zone;
    TAILQ_REMOVE(&fs->zs_thead, z_entry, entries);
    TAILQ_INSERT_HEAD(&fs->zs_thead, z_entry, entries);
  }
}

bool
fla_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle, struct fla_object *obj)
{

  uint64_t obj_slba = fla_object_slba(fs, obj, pool_handle);
  uint32_t obj_zn = obj_slba / fs->geo.nzsect;
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  uint32_t strps = pool_entry->strp_nobjs;

  for (uint32_t strp = 0; strp < strps; strp++)
  {
    int err = fla_xne_dev_znd_send_mgmt(fs->dev.dev, obj_slba + (fs->geo.nzsect * strp),
                                        XNVME_SPEC_ZND_CMD_MGMT_SEND_FINISH, false);
    if (FLA_ERR(err, "fla_xne_dev_znd_send_mgmt_finish()"))
      return false;
  }

  // Update me for striping
  fla_znd_zone_full(fs, obj_zn);
  return true;
}

