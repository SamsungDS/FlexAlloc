#include "flexalloc_dp_noop.h"
#include "flexalloc_mm.h"
#include "flexalloc_ll.h"
#include "flexalloc_util.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc.h"

static uint32_t*
fla_dp_noop_pool_slab_list_id(struct fla_slab_header const *slab,
                        struct fla_pools const *pools)
{
  struct fla_pool_entry * pool_entry = pools->entries + slab->pool;
  struct fla_dp_noop_slab_list_ids * slab_list_ids = (struct fla_dp_noop_slab_list_ids*)pool_entry;

  return slab->refcount == 0 ? &slab_list_ids->empty_slabs
         : slab->refcount >= pool_entry->slab_nobj ? &slab_list_ids->full_slabs
         : &slab_list_ids->partial_slabs;
}

static int
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

static int
fla_dp_noop_slab_format(struct flexalloc * fs, uint32_t const slab_id, struct fla_slab_header * h)
{
  int err;
  err = fla_xne_dev_send_deallocate(fs->dev.dev, fla_geo_slab_lb_off(fs, slab_id),
                                    fs->super->slab_nlb);
  if (FLA_ERR(err, "fla_xne_dev_send_deallocate()"))
    return -1;
  return 0;
}

static int
fla_dp_noop_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  return 0;
}

int
fla_dp_noop_on_object_destroy(struct flexalloc * fs, struct fla_object * obj, struct fla_pool_entry * pool_entry)
{
  return 0;
}

int
fla_dp_noop_init(struct flexalloc *fs, const uint64_t flags)
{
  fs->fla_dp.fncs.init_dp = fla_dp_noop_init;
  fs->fla_dp.fncs.fini_dp = fla_dp_noop_fini;
  fs->fla_dp.fncs.prep_dp_ctx = fla_dp_noop_prep_ctx;
  fs->fla_dp.fncs.get_pool_slab_list_id = fla_dp_noop_pool_slab_list_id;
  fs->fla_dp.fncs.get_next_available_slab = fla_dp_noop_get_next_available_slab;
  fs->fla_dp.fncs.slab_format = fla_dp_noop_slab_format;
  fs->fla_dp.fncs.on_obj_destroy = fla_dp_noop_on_object_destroy;

  return 0;
}

int
fla_dp_noop_fini(struct flexalloc *fs)
{
  return 0;
}

