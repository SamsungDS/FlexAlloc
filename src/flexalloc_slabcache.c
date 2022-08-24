// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>

#include "flexalloc_slabcache.h"
#include "flexalloc_util.h"
#include "flexalloc.h"
#include "flexalloc_mm.h"
#include "flexalloc_xnvme_env.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t
fla_slab_cache_flist_nlb(struct flexalloc const * fs, uint32_t flist_len)
{
  return FLA_CEIL_DIV(fla_flist_size(flist_len), fs->geo.lb_nbytes);
}

/// number of bytes, in multiples of the logical block size, required to hold
/// a freelist of flist_len elements.
static size_t
cache_flist_size(struct fla_slab_flist_cache *cache, uint32_t flist_len)
{
  return fla_slab_cache_flist_nlb(cache->_fs, flist_len) * cache->_fs->geo.lb_nbytes;
}

static uint64_t
cache_entry_lb_slba(struct fla_slab_flist_cache const * cache, const uint32_t slab_id,
                    const uint32_t flist_nlb)
{
  // TODO: Remove the assumption that each flist for a zoned device is exactly one LBA
  // at some point we should set a minimal object size which will bound the flist size
  if (cache->_fs->dev.md_dev)
    return fla_geo_slabs_lb_off(&cache->_fs->geo) + slab_id;
  else
    return fla_geo_slab_lb_off(cache->_fs, slab_id) + cache->_fs->super->slab_nlb - flist_nlb;
}

int
fla_slab_cache_init(struct flexalloc *fs, struct fla_slab_flist_cache *cache)
{
  if (FLA_ERR(cache->_head != NULL,
              "fla_slab_cache_init() - _head ptr non-NULL, are we clobbering an existing cache?"))
    return -EINVAL;

  cache->_fs = fs;
  cache->_head = calloc(fs->super->nslabs, sizeof(struct fla_slab_flist_cache_elem));
  if (FLA_ERR(!cache->_head, "calloc() - failed to allocate slab flist cache"))
  {
    return -ENOMEM;
  }

  // zero out memory, effectively sets all cache entries to FLA_SLAB_CACHE_ELEM_STALE
  memset(cache->_head, 0, fs->super->nslabs * sizeof(struct fla_slab_flist_cache_elem));

  return 0;
}

void
fla_slab_cache_free(struct fla_slab_flist_cache *cache)
{
  struct fla_slab_flist_cache_elem *e, *end;
  if (cache->_head == NULL)
    return;

  // release IO-buffers for all entries
  e = cache->_head;
  end = cache->_head + cache->_fs->geo.nslabs;
  for (; e < end; e++)
  {
    if (!e->freelist)
      continue;

    fla_xne_free_buf(cache->_fs->dev.dev, e->freelist);
  }

  free(cache->_head);
  cache->_head = NULL;
}

// init entry, should be done when the slab is being acquired by a pool
int
fla_slab_cache_elem_init(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         uint32_t flist_len)
{
  struct fla_slab_flist_cache_elem *e;
  int err = 0;
  freelist_t flist_buf;

  e = &cache->_head[slab_id];
  if (e->state != FLA_SLAB_CACHE_ELEM_STALE)
    // do not attempt to initialize an already initialized cache entry
    return FLA_SLAB_CACHE_INVALID_STATE;

  flist_buf = fla_xne_alloc_buf(
                cache->_fs->dev.dev,
                cache_flist_size(cache, flist_len));

  if (FLA_ERR(!flist_buf,
              "fla_xne_alloc_buf() - failed to allocate slab flist IO-buffer"))
  {
    err = -ENOMEM;
    goto exit;
  }

  fla_flist_init(flist_buf, flist_len);
  e->freelist = flist_buf;
  e->state = FLA_SLAB_CACHE_ELEM_DIRTY;

exit:
  return err;
}

int
fla_slab_cache_elem_load(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         uint32_t flist_len)
{
  struct fla_slab_flist_cache_elem *e = &cache->_head[slab_id];
  freelist_t flist_buf;
  int err = 0;
  uint64_t slba;
  size_t flist_nlb;
  struct xnvme_dev *md_dev = cache->_fs->dev.md_dev;

  if (!md_dev)
    md_dev = cache->_fs->dev.dev;

  // do not clobber an entry - only overwrite an entry marked stale
  if (e->state != FLA_SLAB_CACHE_ELEM_STALE)
    return FLA_SLAB_CACHE_INVALID_STATE;

  flist_buf = fla_xne_alloc_buf( cache->_fs->dev.dev, cache_flist_size(cache, flist_len));
  if (FLA_ERR(!flist_buf,
              "fla_xne_alloc_buf() - failed to allocate slab flist IO-buffer"))
  {
    err = -ENOMEM;
    goto exit;
  }

  flist_nlb = fla_slab_cache_flist_nlb(cache->_fs, flist_len);
  if (cache->_fs->dev.md_dev)
  {
    if (flist_nlb > 1)
    {
      FLA_ERR_PRINTF("MD DEV AND FLIST NLB:%lu > 1, FIX ME", flist_nlb);
      goto free_io_buffer;
    }
  }

  slba = cache_entry_lb_slba(cache, slab_id, flist_nlb);
  err = fla_xne_sync_seq_r_naddrs(md_dev, slba, flist_nlb, flist_buf);
  if(FLA_ERR(err, "fla_xne_sync_seq_r_naddrs()"))
    goto free_io_buffer;

  // sanity-check - the caller should know the freelist length
  err = fla_flist_len(flist_buf) != flist_len;
  if (err)
  {
    FLA_ERR_PRINTF("error - expected freelist of length '%"PRIu32"', but entry reports '%"PRIu32"'",
                   flist_len, fla_flist_len(flist_buf));
    goto free_io_buffer;
  }

  e->freelist = flist_buf;
  e->state = FLA_SLAB_CACHE_ELEM_CLEAN;

  return 0; // success

free_io_buffer:
  fla_xne_free_buf(cache->_fs->dev.dev, flist_buf);
exit:
  return err;
}

int
fla_slab_cache_elem_flush(struct fla_slab_flist_cache *cache, uint32_t slab_id)
{
  int err = 0;
  struct fla_slab_flist_cache_elem *e;
  uint64_t slba;
  size_t flist_nlb;
  struct xnvme_dev *md_dev = cache->_fs->dev.dev;

  if (cache->_fs->dev.md_dev)
    md_dev = cache->_fs->dev.md_dev;

  e = &cache->_head[slab_id];
  if (e->state != FLA_SLAB_CACHE_ELEM_DIRTY)
    return 0;

  flist_nlb = fla_slab_cache_flist_nlb(cache->_fs, fla_flist_len(e->freelist));
  slba = cache_entry_lb_slba(cache, slab_id, flist_nlb);
  err = fla_xne_sync_seq_w_naddrs(md_dev, slba, flist_nlb, e->freelist);
  if(FLA_ERR(err, "fla_xne_sync_seq_w_naddrs()"))
    goto exit;

  e->state = FLA_SLAB_CACHE_ELEM_CLEAN;
exit:
  return err;
}

void
fla_slab_cache_elem_drop(struct fla_slab_flist_cache *cache, uint32_t slab_id)
{
  struct fla_slab_flist_cache_elem *e = &cache->_head[slab_id];
  e->state = FLA_SLAB_CACHE_ELEM_STALE;
  if (e->freelist)
  {
    fla_xne_free_buf(cache->_fs->dev.dev, e->freelist);
    e->freelist = NULL;
  }
}

int
fla_slab_cache_obj_alloc(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         struct fla_object *obj_id, uint32_t num_objs)
{
  struct fla_slab_flist_cache_elem *e = &cache->_head[slab_id];
  int err, entry_ndx;

  if (e->state == FLA_SLAB_CACHE_ELEM_STALE)
    return FLA_SLAB_CACHE_INVALID_STATE;

  entry_ndx = fla_flist_entries_alloc(e->freelist, num_objs);
  if ((err = FLA_ERR(entry_ndx < 0,
                     "fla_flist_entry_alloc() - failed to allocate an object in freelist")))
    return err;

  obj_id->slab_id = slab_id;
  obj_id->entry_ndx = entry_ndx;

  e->state = FLA_SLAB_CACHE_ELEM_DIRTY;
  return 0; // success
}

int
fla_slab_cache_obj_free(struct fla_slab_flist_cache *cache,
                        struct fla_object * obj_id, uint32_t num_objs)
{
  struct fla_slab_flist_cache_elem *e = &cache->_head[obj_id->slab_id];
  int err;

  if (e->state == FLA_SLAB_CACHE_ELEM_STALE)
    return FLA_SLAB_CACHE_INVALID_STATE;

  err = fla_flist_entries_free(e->freelist, obj_id->entry_ndx, num_objs);
  if (FLA_ERR(err, "fla_flist_entries_free() - failed to free object in freelist"))
    goto exit;

  e->state = FLA_SLAB_CACHE_ELEM_DIRTY;

exit:
  return err;
}

int
fla_slab_cache_flush(struct fla_slab_flist_cache *cache)
{
  uint32_t slab_id = 0;
  int err = 0;

  if (cache->_head == NULL)
    return 0;

  for (; slab_id < cache->_fs->geo.nslabs; slab_id++)
  {
    if (fla_slab_cache_elem_flush(cache, slab_id))
      err++;
  }

  return err;
}
