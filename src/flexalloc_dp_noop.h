#ifndef __FLEXALLOC_DP_NOOP_H
#define __FLEXALLOC_DP_NOOP_H

#include "flexalloc_mm.h"
#include "flexalloc_ll.h"
#include "flexalloc.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc_util.h"

struct fla_dp_noop_slab_list_ids
{
  uint32_t empty_slabs;
  uint32_t full_slabs;
  uint32_t partial_slabs;
};

int
fla_dp_noop_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  return 0;
}

int
fla_dp_noop_fini(struct flexalloc *fs)
{
  return 0;
}

static uint32_t*
fla_dp_noop_pool_slab_list_id(struct fla_slab_header const *slab,
                        struct fla_pools const *pools)
{
  struct fla_pool_entry * pool_entry = pools->entries + slab->pool;
  struct fla_dp_noop_slab_list_ids * slab_list_ids = (struct fla_dp_noop_slab_list_ids*)pool_entry;
  struct fla_pool_entry_fnc const * pool_entry_fnc = pools->entrie_funcs + slab->pool;
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  return slab->refcount == 0 ? &slab_list_ids->empty_slabs
         : slab->refcount + num_fla_objs > pool_entry->slab_nobj ? &slab_list_ids->full_slabs
         : &slab_list_ids->partial_slabs;
}

int
fla_dp_noop_get_next_available_slab(struct flexalloc * fs, struct fla_pool_entry * pool_entry,
                             struct fla_slab_header ** slab)
{
  int err, ret;
  struct fla_dp_noop_slab_list_ids * slab_list_ids = (struct fla_dp_noop_slab_list_ids*)pool_entry;

  if(slab_list_ids->partial_slabs == FLA_LINKED_LIST_NULL)
  {
    if(slab_list_ids->empty_slabs == FLA_LINKED_LIST_NULL)
    {
      // ACQUIRE A NEW ONE
      err = fla_acquire_slab(fs, pool_entry->obj_nlb, slab);
      if(FLA_ERR(err, "fla_acquire_slab()"))
      {
        goto exit;
      }

      // Add to empty
      err = fla_hdll_prepend(fs, *slab, &slab_list_ids->empty_slabs);
      if(FLA_ERR(err, "fla_hdll_prepend()"))
      {
        goto release_slab;
      }

      goto exit;
release_slab:
      ret = fla_release_slab(fs, *slab);
      if(FLA_ERR(ret, "fla_release_slab()"))
      {
        goto exit;
      }
    }
    else
    {
      // TAKE FROM EMPTY
      *slab = fla_slab_header_ptr(slab_list_ids->empty_slabs, fs);
      if((err = -FLA_ERR(!slab, "fla_slab_header_ptr()")))
      {
        goto exit;
      }
    }
  }
  else
  {
    // TAKE FROM PARTIAL
    *slab = fla_slab_header_ptr(slab_list_ids->partial_slabs, fs);
    if((err = -FLA_ERR(!slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
  }

exit:
  return err;
}

int
fla_dp_noop_init(struct flexalloc *fs, const uint64_t flags)
{
  fs->fla_dp.fncs.init_dp = fla_dp_noop_init;
  fs->fla_dp.fncs.fini_dp = fla_dp_noop_fini;
  fs->fla_dp.fncs.prep_dp_ctx = fla_dp_noop_prep_ctx;
  fs->fla_dp.fncs.get_pool_slab_list_id = fla_dp_noop_pool_slab_list_id;
  fs->fla_dp.fncs.get_next_available_slab = fla_dp_noop_get_next_available_slab;

  return 0;
}
#endif // __FLEXALLOC_DP_NOOP_H
