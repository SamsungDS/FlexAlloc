#include "flexalloc_dp_fdp.h"
#include "flexalloc_util.h"
#include "flexalloc.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc_freelist.h"
#include "flexalloc_mm.h"
#include "flexalloc_ll.h"
#include <stdint.h>
#include <stdarg.h>

int
fla_fdp_get_pid_n(struct xnvme_dev * dev, uint32_t *pid, const int npid)
{
  /* while we wait for the FW to catch up*/
  for (int i = 0; i < npid ; ++i)
    *(pid + i) = 1;
  return 0;
//  int err;
//  uint32_t *pids;
//
//  pids = fla_xne_alloc_buf(dev, sizeof(uint32_t) * npid);
//  if (FLA_ERR(!pids, "fla_xne_alloc_buf()"))
//    return -errno;
//
//  err = fla_xne_get_usable_pids(dev, npid, &pids);
//  if (FLA_ERR(err, "fla_xne_get_usable_pids()"))
//    return err;
//
//  for (int i = 0; i < npid; ++i)
//    *(pid + i) = *(pids + i);
//
//  fla_xne_free_buf(dev, pids);
//  return 0;
}

static int
fla_fdp_onwrite_md_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  struct fla_dp_fdp* fla_dp_fdp = xne_io->fla_dp->fla_dp_fdp;
  ctx->cmd.nvm.cdw13.dspec = fla_dp_fdp->md_pid;
  return 0;
}

static int
fla_fdp_onwrite_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  uint32_t pid;
  int err = fla_fdp_get_pid_n(xne_io->dev, &pid, 1);
  if (FLA_ERR(err, "fla_fdp_get_pid_n()"))
    return err;
  ctx->cmd.nvm.cdw13.dspec = pid;
  ctx->cmd.nvm.dtype = 2;

  return 0;
}

static int
fla_fdp_get_id(const uint32_t ndx, va_list ag)
{
  uint32_t *pid = va_arg(ag, uint32_t*);
  uint32_t *fla_id = va_arg(ag, uint32_t*);
  struct fla_dp_fdp * fdp = va_arg(ag, struct fla_dp_fdp*);

  if(*fla_id == (fdp->pids + ndx)->fla_id)
  {
    *pid = (fdp->pids + ndx)->pid;
    return FLA_FLIST_SEARCH_RET_FOUND_STOP;
  }

  return FLA_FLIST_SEARCH_RET_CONTINUE;
}

static int
fla_fdp_cached_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  struct fla_dp_fdp* fdp = xne_io->fla_dp->fla_dp_fdp;
  struct fla_dp_fdp_pid_to_id *pid_to_id;
  uint32_t fla_id, pid, found = 0;
  int ret;

  switch (xne_io->io_type)
  {
  case FLA_IO_DATA_READ:
  case FLA_IO_MD_READ:
    return 0;
  case FLA_IO_MD_WRITE:
    return fla_fdp_onwrite_md_prep_ctx(xne_io, ctx);
  case FLA_IO_DATA_WRITE:
    switch (fdp->ctx_set)
    {
    case FLA_DP_FDP_ON_SLAB:
      fla_id = xne_io->obj_handle->slab_id;
      break;
    case FLA_DP_FDP_ON_POOL:
      fla_id = xne_io->pool_handle->ndx;
      break;
    case FLA_DP_FDP_ON_OBJECT:
      fla_id = xne_io->obj_handle->entry_ndx;
      break;
    case FLA_DP_FDP_ON_WRITE:
    /* ctx should be handled by fla_fdp_onwrite_prep_ctx */
    default:
      FLA_ERR(1, "fla_fdp_cached_prep_ctx()");
      return -EINVAL;
    }
  }

  ret = fla_flist_search_wfunc(fdp->free_pids, FLA_FLIST_SEARCH_FROM_START,
                               &found, fla_fdp_get_id, &pid, &fla_id, fdp);
  if (FLA_ERR(ret, "fla_flist_search_wfunc()"))
    return ret;

  switch (found)
  {
  case 0:
    ret = fla_flist_entries_alloc(fdp->free_pids, 1);
    if (FLA_ERR(ret < 0, "fla_fdp_cached_prep_ctx(), %d", ret))
      return -ENOSPC;

    pid_to_id = fdp->pids + ret;
    pid_to_id->fla_id = fla_id;

    ret = fla_fdp_get_pid_n(xne_io->dev, &pid_to_id->pid, 1);
    if (FLA_ERR(ret, "fla_fdp_get_pid_n()"))
      return ret;
    pid = pid_to_id->pid;

  case 1:
    ctx->cmd.nvm.cdw13.dspec = pid;
    break;

  default:
    FLA_ERR(true, "fla_fdp_cached_prep_ctx()");
    return -EINVAL;

  }

  ctx->cmd.nvm.dtype = 2;
  return 0;
}

int
fla_noop_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  return 0;
}

static void
fla_fdp_set_prep_ctx(struct flexalloc const *fs,
                     int (**prep_ctx)(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx))
{
  struct fla_dp_fdp* fla_dp_fdp = fs->fla_dp.fla_dp_fdp;

  switch (fla_dp_fdp->ctx_set)
  {
  case FLA_DP_FDP_ON_SLAB:
  case FLA_DP_FDP_ON_POOL:
  case FLA_DP_FDP_ON_OBJECT:
    *prep_ctx = fla_fdp_cached_prep_ctx;
    break;
  case FLA_DP_FDP_ON_WRITE:
    *prep_ctx = fla_fdp_onwrite_prep_ctx;
    break;
  default:
    *prep_ctx = fla_noop_prep_ctx;
  }
}

static uint16_t
fla_fdp_get_max_pids()
{
  return 10000;
}

static int
fla_fdp_init_pid_to_id(struct flexalloc const *fs)
{
  int err;
  struct fla_dp_fdp *fla_dp_fdp = fs->fla_dp.fla_dp_fdp;

  uint16_t pid_to_id_cache_size;
  switch (fla_dp_fdp->ctx_set)
  {
  case FLA_DP_FDP_ON_SLAB:
    pid_to_id_cache_size = fla_min(fs->geo.nslabs, fla_fdp_get_max_pids());
    break;
  case FLA_DP_FDP_ON_POOL:
    pid_to_id_cache_size = fla_min(fs->geo.npools, fla_fdp_get_max_pids());
    break;
  case FLA_DP_FDP_ON_OBJECT:
    pid_to_id_cache_size = fla_fdp_get_max_pids();
    break;
  case FLA_DP_FDP_ON_WRITE:
  /* Fall through: We look for a new pid every time we write */
  default:
    return 0;
  }

  fla_dp_fdp->pids
    = malloc(sizeof(struct fla_dp_fdp_pid_to_id) * pid_to_id_cache_size);
  if (FLA_ERR(!fs->fla_dp.fla_dp_fdp->pids, "malloc()"))
    return -ENOMEM;

  err = fla_flist_new(pid_to_id_cache_size, &fla_dp_fdp->free_pids);
  if (FLA_ERR(err, "fla_flist_new()"))
    return err;

  return 0;
}

static int
fla_fdp_init_md_pid(struct flexalloc const *fs)
{
  fs->fla_dp.fla_dp_fdp->md_pid = 0;
  return 0;
}

static uint32_t*
fla_dp_fdp_pool_slab_list_id(struct fla_slab_header const *slab,
                        struct fla_pools const *pools)
{
  struct fla_pool_entry * pool_entry = pools->entries + slab->pool;
  struct fla_dp_fdp_slab_list_ids * slab_list_ids = (struct fla_dp_fdp_slab_list_ids*)pool_entry;

  return slab->nobj_since_trim == 0 && slab->refcount == 0 ? &slab_list_ids->ready_slabs
    : slab->nobj_since_trim < pool_entry->slab_nobj ? &slab_list_ids->filling_slabs
    : &slab_list_ids->emptying_slabs;
}

static int
fla_dp_fdp_get_next_available_slab(struct flexalloc * fs, struct fla_pool * fla_pool,
                             struct fla_slab_header ** slab)
{
  int err, ret;
  struct fla_pool_entry * pool_entry = &fs->pools.entries[fla_pool->ndx];
  struct fla_dp_fdp_slab_list_ids * slab_list_ids = (struct fla_dp_fdp_slab_list_ids*)pool_entry;

  if(slab_list_ids->ready_slabs == FLA_LINKED_LIST_NULL)
  {
    if(slab_list_ids->filling_slabs == FLA_LINKED_LIST_NULL)
    {
      // ACQUIRE A NEW ONE
      err = fla_acquire_slab(fs, pool_entry->obj_nlb, slab);
      if(FLA_ERR(err, "fla_acquire_slab()"))
      {
        goto exit;
      }

      // Add to READY
      err = fla_hdll_prepend(fs, *slab, &slab_list_ids->ready_slabs);
      if(FLA_ERR(err, "fla_hdll_prepend()"))
      {
        goto release_slab;
      }

      // make sure we relate slab to pool
      (*slab)->pool = fla_pool->ndx;

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
      // TAKE FROM FILLING
      *slab = fla_slab_header_ptr(slab_list_ids->filling_slabs, fs);
      if((err = -FLA_ERR(!slab, "fla_slab_header_ptr()")))
      {
        goto exit;
      }
    }
  }
  else
  {
    // TAKE FROM READY
    *slab = fla_slab_header_ptr(slab_list_ids->ready_slabs, fs);
    if((err = -FLA_ERR(!slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
  }

exit:
  return err;
}

static int
fla_dp_fdp_slab_format(struct flexalloc * fs, uint32_t slab_id, struct fla_slab_header * slab)
{
  int err;
  uint32_t fla_id, pid, found = 0;
  err = fla_xne_dev_send_deallocate(fs->dev.dev, fla_geo_slab_lb_off(fs, slab_id),
                                    fs->super->slab_nlb);
  if (FLA_ERR(err, "fla_xne_dev_send_deallocate()"))
    goto exit;

  slab->nobj_since_trim = 0;

  fla_id = slab_id;
  err = fla_flist_search_wfunc(fs->fla_dp.fla_dp_fdp->free_pids, FLA_FLIST_SEARCH_FROM_START,
                             &found, fla_fdp_get_id, &pid, &fla_id, fs->fla_dp.fla_dp_fdp);
  if (FLA_ERR(err, "fla_flist_search_wfunc()"))
    return err;

  switch (found)
  {
  case 1:
    err = fla_flist_entry_free(fs->fla_dp.fla_dp_fdp->free_pids, fla_id);
    if (FLA_ERR(err, "fla_flist_entriy_free()"))
      goto exit;
    //fall through
  case 0:
    break;

  default:
    FLA_ERR(true, "fla_fdp_cached_prep_ctx()");
    return -EINVAL;
  }

exit:
  return err;
}

int
fla_dp_fdp_init(struct flexalloc *fs, uint64_t flags)
{
  int err;
  fs->fla_dp.dp_type = FLA_DP_FDP;
  fs->fla_dp.fla_dp_fdp = malloc(sizeof(struct fla_dp_fdp));
  if (FLA_ERR(!fs->fla_dp.fla_dp_fdp, "malloc()"))
    return -ENOMEM;

  fs->fla_dp.fla_dp_fdp->ctx_set = FLA_DP_FDP_ON_SLAB;
  fs->fla_dp.fncs.init_dp = fla_dp_fdp_init;
  fs->fla_dp.fncs.fini_dp = fla_dp_fdp_fini;
  fs->fla_dp.fncs.get_pool_slab_list_id = fla_dp_fdp_pool_slab_list_id;
  fs->fla_dp.fncs.get_next_available_slab = fla_dp_fdp_get_next_available_slab;
  fs->fla_dp.fncs.slab_format = fla_dp_fdp_slab_format;

  fla_fdp_set_prep_ctx(fs, &fs->fla_dp.fncs.prep_dp_ctx);

  if ((err = FLA_ERR(fla_fdp_init_md_pid(fs), "fla_fdp_init_md_pid()")))
    return err;

  if((err = FLA_ERR(fla_fdp_init_pid_to_id(fs), "fla_fdp_init_pid_to_id()")))
    return err;

  return 0;
}

int
fla_dp_fdp_fini(struct flexalloc *fs)
{
  free(fs->fla_dp.fla_dp_fdp);
  return 0;
}

