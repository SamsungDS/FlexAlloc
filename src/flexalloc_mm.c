// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>
#include <asm-generic/errno-base.h>
#include <libxnvme.h>
#include <libxnvmec.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "flexalloc.h"
#include "flexalloc_util.h"
#include "flexalloc_dp.h"
#include "flexalloc_hash.h"
#include "flexalloc_freelist.h"
#include "flexalloc_slabcache.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"
#include "flexalloc_ll.h"
#include "flexalloc_slabcache.h"
#include "flexalloc_shared.h"
#include "flexalloc_pool.h"
#include "flexalloc_dp.h"

static int
fla_dev_sanity_check(struct xnvme_dev const * dev, struct xnvme_dev const *md_dev)
{
  int err = 0;
  uint32_t mdts_nbytes;

  /*
   * xNVMe's linux backend can potentially fallback on an incorrect minimum data transfer
   * (mdts) value if application is not executed with admin privileges. Check here that we
   * have an mdts of FLA_MDTS_MIN_NBYTES bytes.
   */
  mdts_nbytes = fla_xne_dev_mdts_nbytes(dev);
  err |= mdts_nbytes <= FLA_MDTS_MIN_NBYTES;
  if (md_dev)
  {
    mdts_nbytes = fla_xne_dev_mdts_nbytes(md_dev);
    err |= mdts_nbytes <= FLA_MDTS_MIN_NBYTES;
  }
  FLA_ERR(err, "The minimum data transfer value of dev or md_dev reported to be less " \
          "than %d. This is most probably due to lack of administrative privileges. "\
          " you can solve this by running with sudo for example.", FLA_MDTS_MIN_NBYTES);

  if (md_dev)
  {
    err |= fla_xne_dev_lba_nbytes(md_dev) != fla_xne_dev_lba_nbytes(dev);
    FLA_ERR(err, "MD DEV LBA size != DEV LBA SIZE");
  }

  return err;
}

void
print_slab_sgmt(const struct flexalloc * fs, uint32_t from, uint32_t to)
{
  struct fla_slab_header * slab;

  fprintf(stderr, "%s\n", "--------------------------------");
  fprintf(stderr, "Slabs number : %"PRIu32"\n", *fs->slabs.fslab_num);
  fprintf(stderr, "Slabs head : %"PRIu32"\n", *fs->slabs.fslab_head);
  fprintf(stderr, "Slabs tail : %"PRIu32"\n", *fs->slabs.fslab_tail);
  fprintf(stderr, "Header ptr : %p\n", fs->slabs.headers);

  if (to == 0)
    to = fs->geo.nslabs;

  if (from >= to)
  {
    fprintf(stderr, "from (%"PRIu32") is greater than to (%"PRIu32")\n", from, to);
    return;
  }

  for(uint32_t i = from ; i < to ; ++i)
  {
    slab = fs->slabs.headers + i;
    fprintf(stderr, "slab number %"PRIu32", ", i);
    fprintf(stderr, "next : %"PRIu32", ", slab->next);
    fprintf(stderr, "prev : %"PRIu32"\n", slab->prev);
  }
  fprintf(stderr, "%s\n", "--------------------------------");

}

uint32_t
fla_slab_sgmt_calc(uint32_t nslabs, uint32_t lb_nbytes)
{
  /*
   * The int32_t free count, head and tail IDs are located at the last 96 bits of the slab_sgmt.
   * Leave room after the slab headers for whatever comes at the end.
   */
  const size_t needed_end_nbytes = sizeof(uint32_t)*3;
  const size_t slab_headers_nbytes = nslabs * sizeof(struct fla_slab_header);

  return FLA_CEIL_DIV(slab_headers_nbytes + needed_end_nbytes, lb_nbytes);
}

uint32_t
fla_slab_sgmt_calc_v(uint32_t nslabs, va_list ap)
{
  uint32_t lb_nbytes = va_arg(ap, uint32_t);
  return fla_slab_sgmt_calc(nslabs, lb_nbytes);
}

void
fla_geo_slab_sgmt_calc(uint32_t nslabs, uint32_t lb_nbytes,
                       struct fla_geo_slab_sgmt *geo)
{
  geo->slab_sgmt_nlb = fla_slab_sgmt_calc(nslabs, lb_nbytes);
}

static inline uint32_t
fla_geo_slab_sgmt_nblocks(const struct fla_geo_slab_sgmt *geo)
{
  return geo->slab_sgmt_nlb;
}

static inline uint32_t
fla_nslabs_max_mddev(uint64_t blocks, uint32_t slab_nlb, uint32_t lb_nbytes,
                     uint64_t md_blocks)
{
  uint32_t nslabs = blocks / slab_nlb;
  uint32_t md_nlb = fla_slab_sgmt_calc(nslabs, lb_nbytes);

  /* Make sure the meta data device has enough room to store slab MD */
  if (md_nlb <= md_blocks)
    return nslabs;
  else
    return 0;
}

/// total number of blocks in pool segment
static inline uint32_t
fla_geo_pool_sgmt_nblocks(const struct fla_geo_pool_sgmt *geo)
{
  return geo->freelist_nlb
         + geo->htbl_nlb
         + geo->entries_nlb;
}

void
fla_geo_from_super(struct xnvme_dev *dev, struct fla_super *super,
                   struct fla_geo *geo)
{
  /*
   * Re-caclculate geometry based on provided input parameters
   *
   * This routine re-calculates disk geometry based on the npools and nslabs
   * parameters which are read from flexalloc' super block.
   *
   */

  //...
  geo->lb_nbytes = fla_xne_dev_lba_nbytes(dev);
  geo->nlb = fla_xne_dev_tbytes(dev) / geo->lb_nbytes;

  geo->slab_nlb = super->slab_nlb;
  geo->npools = super->npools;
  geo->nslabs = super->nslabs;
  geo->md_nlb = super->md_nlb;
  fla_geo_pool_sgmt_calc(geo->npools, geo->lb_nbytes, &geo->pool_sgmt);
  fla_geo_slab_sgmt_calc(geo->nslabs, geo->lb_nbytes, &geo->slab_sgmt);
}

uint32_t
fla_geo_nblocks(const struct fla_geo *geo)
{
  return geo->md_nlb
         + fla_geo_pool_sgmt_nblocks(&geo->pool_sgmt)
         + fla_geo_slab_sgmt_nblocks(&geo->slab_sgmt);
}

uint64_t
fla_geo_nbytes(struct fla_geo *geo)
{
  return ((uint64_t)fla_geo_nblocks(geo)) * geo->lb_nbytes;
}


void
fla_geo_init(const struct xnvme_dev *dev, uint32_t npools, uint32_t slab_nlb,
             uint32_t lb_nbytes, struct fla_geo *geo)
{
  if (!dev)
    return;

  geo->lb_nbytes = lb_nbytes;
  geo->nlb = fla_xne_dev_tbytes(dev) / geo->lb_nbytes;

  geo->npools = npools;
  geo->slab_nlb = slab_nlb;

  geo->md_nlb = FLA_CEIL_DIV(sizeof(struct fla_super), lb_nbytes);
}

int
fla_mkfs_geo_calc(const struct xnvme_dev *dev, const struct xnvme_dev *md_dev,
                  uint32_t npools, uint32_t slab_nlb, struct fla_geo *geo)
{
  /*
   * Calculate geometry of disk format given the SLAB size (in number of logical blocks)
   * and optionally how many pools to support.
   *
   * If the number of pools is not supplied, assume (roughly) 1 pool per slab. This is a
   * very high number of pools, but the overhead should be miniscule.
   *
   * NOTE: this procedure is intended during creation of the system. When opening an existing
   *       system, read the super block and call the fla_geo_init procedure.
   */
  int err;
  uint32_t nslabs_approx;
  uint32_t lb_nbytes = fla_xne_dev_lba_nbytes(dev);
  struct fla_geo md_geo = {0};

  fla_geo_init(dev, npools, slab_nlb, lb_nbytes, geo);
  fla_geo_init(dev, npools, slab_nlb, lb_nbytes, &md_geo);

  err = fla_cs_geo_check(dev, geo);
  if (FLA_ERR(err, "fla_cs_geo_check()"))
    return err;

  // estimate how many slabs we can get before knowing the overhead of the pool metadata
  if (!md_dev)
  {
    nslabs_approx = fla_nelems_max(geo->nlb - geo->md_nlb, slab_nlb, fla_slab_sgmt_calc_v, lb_nbytes);
  }
  else
  {
    nslabs_approx = fla_nslabs_max_mddev(geo->nlb, slab_nlb, lb_nbytes, md_geo.nlb);
  }

  if (FLA_ERR(!nslabs_approx, "slab size too large - not enough space to allocate any slabs"))
  {
    return -1;
  }

  if (FLA_ERR(npools > nslabs_approx, "npools is too high"))
  {
    return -1;
  }
  else if (!npools)
  {
    // no desired number of pools specified, assume a max of 1 pool per slab (worst-case)
    geo->npools = nslabs_approx;
  }

  // calculate number of blocks required to support the pool entries
  fla_geo_pool_sgmt_calc(geo->npools, lb_nbytes, &geo->pool_sgmt);

  // calculate maximum number of slabs we can support given the logical blocks remaining
  if (!md_dev)
  {
    geo->nslabs = fla_nelems_max(
                    geo->nlb - geo->md_nlb - fla_geo_pool_sgmt_nblocks(&geo->pool_sgmt),
                    slab_nlb,
                    fla_slab_sgmt_calc_v, lb_nbytes);
  }
  else
  {
    geo->nslabs = fla_nslabs_max_mddev(geo->nlb, slab_nlb, lb_nbytes,
                                       md_geo.nlb - geo->md_nlb -
                                       fla_geo_pool_sgmt_nblocks(&geo->pool_sgmt));
  }

  if (FLA_ERR(!geo->nslabs, "slab size too large, not enough space to allocate any slabs"))
  {
    return -1;
  }

  // calculate number of blocks required to support `nslabs` slab header entries
  fla_geo_slab_sgmt_calc(geo->nslabs, lb_nbytes, &geo->slab_sgmt);

  if (FLA_ERR(npools > geo->nslabs,
              "Each pool will require 1+ slabs. Having more pools than slabs is invalid"))
  {
    return -1;
  }
  else if (geo->npools > geo->nslabs)
  {
    // if npools was not specified, yet we inferred more pools than we can allocate
    // slabs for, lower the number of pools accordingly.
    geo->npools = geo->nslabs;
    fla_geo_pool_sgmt_calc(geo->npools, lb_nbytes, &geo->pool_sgmt);
  }

  return 0;
}

/// calculate disk offset, in logical blocks, of the start of the pools segment
uint64_t
fla_geo_pool_sgmt_lb_off(struct fla_geo const *geo)
{
  return geo->md_nlb;
}

/// calculate disk offset in bytes of the start of the pools segment
uint64_t
fla_geo_pool_sgmt_off(struct fla_geo *geo)
{
  return geo->lb_nbytes * fla_geo_pool_sgmt_lb_off(geo);
}

/// calculate disk offset, in logical blocks, of the start of the slabs segment
uint64_t
fla_geo_slab_sgmt_lb_off(struct fla_geo const *geo)
{
  return fla_geo_pool_sgmt_lb_off(geo)
         + fla_geo_pool_sgmt_nblocks(&geo->pool_sgmt);
}

uint64_t
fla_geo_slabs_lb_off(struct fla_geo const *geo)
{
  return fla_geo_slab_sgmt_lb_off(geo) + fla_geo_slab_sgmt_nblocks(&geo->slab_sgmt);
}

uint64_t
fla_geo_slab_lb_off(struct flexalloc const *fs, uint32_t slab_id)
{
  uint64_t slabs_base = 0, slab_base;

  if (fs->dev.md_dev == NULL)
    slabs_base = fla_geo_slabs_lb_off(&fs->geo);

  //FIXME: We are missing the error handling here.
  int err = fs->fla_cs.fncs.slab_offset(fs, slab_id, slabs_base, &slab_base);
  FLA_ERR(err, "slab_offset()");

  return slab_base;
}

/// calculate disk offset in bytes of the start of the slabs segment
uint64_t
fla_geo_slab_sgmt_off(struct fla_geo *geo)
{
  return geo->lb_nbytes * fla_geo_slab_sgmt_lb_off(geo);
}

void
fla_mkfs_slab_sgmt_init(struct flexalloc *fs, struct fla_geo *geo)
{
  const uint64_t slab_sgmt_nbytes = geo->lb_nbytes * ((uint64_t)geo->slab_sgmt.slab_sgmt_nlb);
  const uint32_t last_slab_id = geo->nslabs - 1;
  const uint32_t first_slab_id = 0;
  struct fla_slab_header * curr_slab;

  memset(fs->slabs.headers, 0, slab_sgmt_nbytes);

  // tail points to the last 32 bits in slab_sgmt and is set to highest possible slab id
  fs->slabs.fslab_tail = (uint32_t*)((unsigned char*)fs->slabs.headers + slab_sgmt_nbytes - sizeof(
                                       uint32_t));
  *fs->slabs.fslab_tail = last_slab_id;

  // head points to the penultimate 32 bits in slab_sgmt and is set to lowest possible id
  fs->slabs.fslab_head = (uint32_t*)((unsigned char*)fs->slabs.headers + slab_sgmt_nbytes - (sizeof(
                                       uint32_t)*2));
  *fs->slabs.fslab_head = first_slab_id;

  fs->slabs.fslab_num = (uint32_t*)((unsigned char*)fs->slabs.headers + slab_sgmt_nbytes - (sizeof(
                                      uint32_t)*3));
  *fs->slabs.fslab_num = geo->nslabs;

  // Set first slab header
  curr_slab = fs->slabs.headers;
  curr_slab->next = first_slab_id + 1;
  curr_slab->prev = FLA_LINKED_LIST_NULL;

  //set last slab header
  curr_slab = fs->slabs.headers + last_slab_id;
  curr_slab->next = FLA_LINKED_LIST_NULL;
  curr_slab->prev = last_slab_id - 1;

  for(uint32_t curr_slab_id = 1 ; curr_slab_id < last_slab_id ; ++curr_slab_id)
  {
    curr_slab = fs->slabs.headers + curr_slab_id;
    curr_slab->next = curr_slab_id + 1;
    curr_slab->prev = curr_slab_id - 1;
  }

}

void
fla_mkfs_super_init(struct flexalloc *fs, struct fla_geo *geo)
{
  fs->super->magic = FLA_MAGIC;
  fs->super->fmt_version = FLA_FMT_VER;
  fs->super->md_nlb = geo->md_nlb;
  fs->super->npools = geo->npools;
  fs->super->slab_nlb = geo->slab_nlb;
  fs->super->nslabs = geo->nslabs;
}

int
fla_init(struct fla_geo *geo, struct xnvme_dev *dev, struct xnvme_dev *md_dev, void *fla_md_buf,
         struct flexalloc *fs)
{
  /*
   * Initializes flexalloc struct from knowledge of the disk geometry, and a buffer containing
   * the complete range of LB's containing the on-disk meta data.
   *
   * NOTE: flexalloc will be backed by data in the fla_md_buf buffer. This buffer should
   *       be an IO buffer such that it can be written to disk without having to copy
   *       the data.
   */
  uint8_t *buf_base = fla_md_buf;
  int err;

  fs->dev.dev = dev;
  fs->dev.md_dev = md_dev;
  fs->dev.lb_nbytes = fla_xne_dev_lba_nbytes(dev);

  fs->fs_buffer = fla_md_buf;
  fs->geo = *geo;

  // super block is at the head of the buffer
  fs->super = fla_md_buf;

  /*
   * Pool segment
   */
  err = fla_pool_init(fs, geo, buf_base + fla_geo_pool_sgmt_off(geo));
  if (FLA_ERR(err, "fla_pool_init()"))
    return err;

  /*
   * Slab segment
   */

  fs->slabs.headers = (struct fla_slab_header *) (buf_base + fla_geo_slab_sgmt_off(geo));
  fs->slabs.fslab_tail = (uint32_t*)((unsigned char *)fs->slabs.headers +
                                     (geo->slab_sgmt.slab_sgmt_nlb * geo->lb_nbytes) - sizeof(uint32_t));
  fs->slabs.fslab_head = (uint32_t*)((unsigned char*)fs->slabs.headers +
                                     (geo->slab_sgmt.slab_sgmt_nlb * geo->lb_nbytes) - (sizeof(uint32_t)*2));
  fs->slabs.fslab_num = (uint32_t*)((unsigned char*)fs->slabs.headers +
                                    (geo->slab_sgmt.slab_sgmt_nlb * geo->lb_nbytes) - (sizeof(uint32_t)*3));

  err = fla_init_dp(fs);
  if (err)
    return err;

  err = fla_init_cs(fs);
  if (FLA_ERR(err, "fla_init_cs()"))
    return err;

  return 0;
}

int
fla_mkfs(struct fla_mkfs_p *p)
{
  struct xnvme_dev *dev = NULL, *md_dev = NULL;
  struct flexalloc *fs;
  struct fla_geo geo;
  void *fla_md_buf;
  size_t fla_md_buf_len;
  struct xnvme_lba_range lba_range;
  struct fla_xne_io xne_io;
  int err;

  err = fla_xne_dev_open(p->open_opts.dev_uri, NULL, &dev);
  if (FLA_ERR(err, "failed to open device"))
    return -1;

  err = fla_xne_dev_mkfs_prepare(dev, p->open_opts.md_dev_uri, &md_dev);
  if (FLA_ERR(err, "fla_xne_dev_mkfs_prepare"))
    return err;

  err = fla_dev_sanity_check(dev, md_dev);
  if(FLA_ERR(err, "fla_dev_sanity_check()"))
    goto exit;


  if (md_dev)
  {
    err |= fla_xne_dev_lba_nbytes(md_dev) != fla_xne_dev_lba_nbytes(dev);
    if (err)
    {
      FLA_ERR(err, "MD DEV LBA size != DEV LBA SIZE");
      goto exit;
    }
  }

  err = fla_mkfs_geo_calc(dev, md_dev, p->npools, p->slab_nlb, &geo);
  if(FLA_ERR(err, "fla_mkfs_geo_calc()"))
    goto exit;

  if (!md_dev)
    md_dev = dev;

  fs = malloc(sizeof(struct flexalloc));
  if (FLA_ERR(!fs, "malloc()"))
  {
    err = -ENOMEM;
    goto exit;
  }

  memset(fs, 0, sizeof(struct flexalloc));
  fla_md_buf_len = fla_geo_nbytes(&geo);
  fla_md_buf = fla_xne_alloc_buf(md_dev, fla_md_buf_len);
  if (FLA_ERR(!fla_md_buf, "fla_xne_alloc_buf()"))
  {
    err = -ENOMEM;
    goto free_fs;
  }

  memset(fla_md_buf, 0, fla_md_buf_len);
  err = fla_init(&geo, dev, NULL, fla_md_buf, fs);
  if (FLA_ERR(err, "fla_init()"))
    goto free_md;

  // initialize pool
  fla_mkfs_pool_sgmt_init(fs, &geo);

  // initialize slab
  fla_mkfs_slab_sgmt_init(fs, &geo);

  // initialize super
  fla_mkfs_super_init(fs, &geo);

  xne_io.io_type = FLA_IO_MD_WRITE;
  xne_io.dev = md_dev;
  xne_io.buf = fla_md_buf;
  xne_io.prep_ctx = fs->fla_dp.fncs.prep_dp_ctx;
  xne_io.fla_dp = &fs->fla_dp;

  lba_range = fla_xne_lba_range_from_offset_nbytes(xne_io.dev, FLA_SUPER_SLBA, fla_md_buf_len);
  if(( err = FLA_ERR(lba_range.attr.is_valid != 1, "fla_xne_lba_range_from_offset_nbytes()")))
    goto free_md;
  xne_io.lba_range = &lba_range;

  err = fla_xne_sync_seq_w_xneio(&xne_io);
  if (FLA_ERR(err, "fla_xne_sync_seq_w_xneio()"))
    goto free_md;


free_md:
  fla_xne_free_buf(md_dev, fla_md_buf);

free_fs:
  fla_fs_free(fs);

exit:
  if (md_dev && md_dev != dev)
    fla_xne_dev_close(md_dev);
  fla_xne_dev_close(dev);
  return err;
}

int
fla_super_read(struct xnvme_dev *dev, size_t lb_nbytes, struct fla_super **super)
{
  int err = 0;

  // allocate read buffer and compute the LBA range
  *super = fla_xne_alloc_buf(dev, lb_nbytes);
  if ((err = FLA_ERR(!*super, "fla_xne_alloc_buf()")))
  {
    err = -ENOMEM;
    goto exit;
  }

  memset(*super, 0, lb_nbytes); // Keep valgrind happy
  struct xnvme_lba_range range;
  range = fla_xne_lba_range_from_offset_nbytes(dev, 0, lb_nbytes);
  if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;
  struct fla_xne_io xne_io = {.dev = dev, .buf = *super, .lba_range = &range, .fla_dp = NULL};

  err = fla_xne_sync_seq_r_xneio(&xne_io);
  if (FLA_ERR(err, "fla_xne_sync_seq_r_xneio()"))
    goto free_super;

  // TODO: Should this be placed in fla_dev_sanity_check
  if ((*super)->magic != FLA_MAGIC)
  {
    err = 22;
    FLA_ERR(err, "Invalid super block\n");
    goto free_super;
  }

  return 0;

free_super:
  fla_xne_free_buf(dev, *super);
exit:
  return err;
}

struct flexalloc *
fla_fs_alloc()
{
  struct flexalloc *fs = malloc(sizeof(struct flexalloc));
  if (FLA_ERR(!fs, "fla_fs_alloc malloc()"))
    return NULL;

  memset(fs, 0, sizeof(struct flexalloc));

  return fs;
}

void
fla_fs_free(struct flexalloc *fs)
{
  if (fs)
    free(fs);
}

int
fla_flush(struct flexalloc *fs)
{
  int err = 0;
  struct xnvme_dev *md_dev = fs->dev.md_dev;

  if (!fs || !(fs->state & FLA_STATE_OPEN))
    return 0;

  if (!md_dev)
    md_dev = fs->dev.dev;

  err = fla_slab_cache_flush(&fs->slab_cache);
  if (FLA_ERR(err, "fla_slab_cache_flush() - failed to flush one or more slab freelists"))
    goto exit;

  // We have to copy over the pool hash table's metadata before flushing
  fs->pools.htbl_hdr_buffer->len = fs->pools.htbl.len;

  struct xnvme_lba_range range;
  range = fla_xne_lba_range_from_slba_naddrs(md_dev, FLA_SUPER_SLBA, fla_geo_nblocks(&fs->geo));
  if ((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;

  struct fla_xne_io xne_io =
    { .dev = md_dev,
      .buf = fs->fs_buffer,
      .lba_range = &range,
      .fla_dp = &fs->fla_dp};
  err = fla_xne_sync_seq_w_xneio(&xne_io);
  if(FLA_ERR(err, "fla_xne_sync_seq_w_xneio()"))
    goto exit;

exit:
  return err;
}

void
fla_close_noflush(struct flexalloc *fs)
{
  if (!fs || !(fs->state & FLA_STATE_OPEN))
    return;

  fs->state &= ~FLA_STATE_OPEN;
  fs->fla_cs.fncs.fini_cs(fs, 0);
  fs->fla_dp.fncs.fini_dp(fs);
  fla_pool_fini(fs);
  fla_slab_cache_free(&fs->slab_cache);
  xnvme_dev_close(fs->dev.dev);
  free(fs->super);
  if (fs->dev.md_dev != fs->dev.dev)
    xnvme_dev_close(fs->dev.md_dev);
  free(fs->dev.dev_uri);
  if(fs->dev.md_dev_uri != NULL)
    free(fs->dev.md_dev_uri);
  fla_fs_free(fs);
}

int
fla_base_close(struct flexalloc *fs)
{
  int err = 0;

  if (!fs)
    return 0;

  err = fla_flush(fs);
  if(FLA_ERR(err, "fla_flush()"))
  {
    goto exit;
  }

  fla_close_noflush(fs);

exit:
  return err;
}

int
fla_base_sync(struct flexalloc *fs)
{
  return fla_flush(fs);
}

void
fla_print_geo(struct flexalloc *fs)
{
  struct fla_geo * geo = &(fs->geo);
  fprintf(stderr, "Flexalloc Geometry: \n");
  fprintf(stderr, "|  LBAs: %"PRIu64"\n", geo->nlb);
  fprintf(stderr, "|  LBA width: %"PRIu32"B\n", geo->lb_nbytes);
  fprintf(stderr, "|  md blocks: %"PRIu32"\n", geo->md_nlb);
  fprintf(stderr, "|  pool segment:\n");
  fprintf(stderr, "|    * pools: %"PRIu32"\n", geo->npools);
  fprintf(stderr, "|    * freelist blocks: %"PRIu32"\n", geo->pool_sgmt.freelist_nlb);
  fprintf(stderr, "|    * htbl blocks: %"PRIu32"\n", geo->pool_sgmt.htbl_nlb);
  fprintf(stderr, "|    * entry blocks: %"PRIu32"\n", geo->pool_sgmt.entries_nlb);
  fprintf(stderr, "|    * total blocks: %"PRIu32"\n", fla_geo_pool_sgmt_nblocks(&geo->pool_sgmt));
  fprintf(stderr, "|  slab segment:\n");
  fprintf(stderr, "|    * slabs: %"PRIu32"\n", geo->nslabs);
  fprintf(stderr, "|    * slab num LBAs: %"PRIu32"\n", geo->slab_nlb);
  fprintf(stderr, "|    * slab total blocks: %"PRIu32"\n", geo->slab_sgmt.slab_sgmt_nlb);
  fprintf(stderr, "\n");
}

void
fla_print_fs(struct flexalloc *fs)
{
  fla_print_geo(fs);
  fla_print_pool_entries(fs);
}

//TODO: We do not consider stripped objects here. We should do add to the pool strp indirection
uint32_t
fla_calc_objs_in_slab(struct flexalloc const * fs, uint32_t const obj_nlb)
{
  uint32_t obj_num;
  size_t unused_nlb, free_list_nlb;

  if (!fs->dev.md_dev)
  {
    for(obj_num = fs->geo.slab_nlb / obj_nlb ; obj_num > 0; --obj_num)
    {
      unused_nlb = (fs->geo.slab_nlb - (obj_num * obj_nlb));
      free_list_nlb = fla_slab_cache_flist_nlb(fs, obj_num);
      if(unused_nlb >= free_list_nlb)
        break;
    }
  }
  else   // For a zoned device we place free list nlb in the MD dev
    obj_num = fs->geo.slab_nlb / obj_nlb;

  return obj_num;
}

void
fla_base_pool_close(struct flexalloc *fs, struct fla_pool * handle)
{
  free(handle);
}

static int
fla_slab_next_available_obj(struct flexalloc * fs, struct fla_slab_header * slab,
                            struct fla_object * obj)
{
  int err = 0;
  uint32_t slab_id, num_objs;
  struct fla_pool_entry const * pool_entry;

  err = fla_slab_id(slab, fs, &slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
    return err;

  pool_entry = &fs->pools.entries[slab->pool];
  num_objs = (fs->pools.entrie_funcs + slab->pool)->fla_pool_num_fla_objs(pool_entry);

  err = fla_slab_cache_obj_alloc(&fs->slab_cache, slab_id, obj, num_objs);
  if(FLA_ERR(err, "fla_slab_cache_obj_alloc()"))
    return err;
  slab->refcount += num_objs;
  slab->nobj_since_trim += num_objs;

  return err;
}

int
fla_base_object_open(struct flexalloc * fs, struct fla_pool * pool_handle,
                     struct fla_object * obj)
{
  int err;
  struct fla_slab_header * slab;
  struct fla_pool_entry const * pool_entry;

  slab = fla_slab_header_ptr(obj->slab_id, fs);
  if((err = FLA_ERR(!slab, "fla_slab_header_ptr()")))
    goto exit;

  pool_entry = &fs->pools.entries[pool_handle->ndx];
  err = fla_slab_cache_elem_load(&fs->slab_cache, obj->slab_id, pool_entry->slab_nobj);
  if(err == FLA_SLAB_CACHE_INVALID_STATE)
    err = 0; //Ignore as it was already loaded.
  else if(FLA_ERR(err, "fla_slab_cache_elem_load()"))
    goto exit;

  //FIXME : make sure that the object is taken in the free list.

exit:
  return err;
}

int
fla_base_object_create(struct flexalloc * fs, struct fla_pool * pool_handle,
                       struct fla_object * obj)
{
  int err;
  struct fla_slab_header * slab;
  struct fla_pool_entry * pool_entry;
  uint32_t * from_head, * to_head, slab_id;

  pool_entry = &fs->pools.entries[pool_handle->ndx];
  err = fs->fla_dp.fncs.get_next_available_slab(fs, pool_handle, &slab);
  if(FLA_ERR(err, "get_next_available_slab()"))
  {
    goto exit;
  }

  err = fla_slab_id(slab, fs, &slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
  {
    goto exit;
  }

  err = fla_slab_cache_elem_load(&fs->slab_cache, slab_id, pool_entry->slab_nobj);
  if(err == FLA_SLAB_CACHE_INVALID_STATE)
    err = 0; //Ignore as it was already loaded.
  else if(FLA_ERR(err, "fla_slab_cache_elem_load()"))
  {
    FLA_DBG_EXEC(fla_print_fs(fs));
    goto exit;
  }

  from_head = fs->fla_dp.fncs.get_pool_slab_list_id(slab, &fs->pools);

  err = fla_slab_next_available_obj(fs, slab, obj);
  if(FLA_ERR(err, "fla_slab_next_available_obj()"))
  {
    goto exit;
  }

  to_head = fs->fla_dp.fncs.get_pool_slab_list_id(slab, &fs->pools);

  if(from_head != to_head)
  {
    // TODO: Add an assert to make sure that to_head != than from_head
    err = fla_hdll_remove(fs, slab, from_head);
    if(FLA_ERR(err, "fla_hdll_remove()"))
    {
      goto exit;
    }

    err = fla_hdll_prepend(fs, slab, to_head);
    if(FLA_ERR(err, "fla_hdll_prepend()"))
    {
      goto exit;
    }
  }

exit:
  return err;
}

uint64_t
fla_object_size_nbytes(struct flexalloc const * fs, struct fla_pool const * pool_handle)
{
  struct fla_pool_entry_fnc const * pool_entry_fnc = fs->pools.entrie_funcs + pool_handle->ndx;
  return pool_entry_fnc->fla_pool_obj_size_nbytes(fs, pool_handle);
}

uint64_t
fla_object_slba(struct flexalloc const * fs, struct fla_object const * obj,
                const struct fla_pool * pool_handle)
{
  const struct fla_pool_entry * pool_entry = &fs->pools.entries[pool_handle->ndx];
  return fla_geo_slab_lb_off(fs, obj->slab_id) + (pool_entry->obj_nlb * obj->entry_ndx);
}

uint64_t
fla_object_soffset(struct flexalloc const * fs, struct fla_object const * obj,
                   const struct fla_pool * pool_handle)
{
  return fla_object_slba(fs, obj, pool_handle) * fs->dev.lb_nbytes;
}

int
fla_base_object_destroy(struct flexalloc *fs, struct fla_pool * pool_handle,
                        struct fla_object * obj)
{
  int err = 0;
  struct fla_slab_header * slab;
  struct fla_pool_entry * pool_entry;
  uint32_t * from_head, * to_head;
  struct fla_pool_entry_fnc const * pool_entry_fnc;

  err = fs->fla_cs.fncs.object_destroy(fs, pool_handle, obj);
  if (FLA_ERR(err, "object_destroy()"))
    goto exit;

  slab = fla_slab_header_ptr(obj->slab_id, fs);
  if((err = FLA_ERR(!slab, "fla_slab_header_ptr()")))
    goto exit;

  pool_entry = &fs->pools.entries[pool_handle->ndx];
  from_head = fs->fla_dp.fncs.get_pool_slab_list_id(slab, &fs->pools);

  pool_entry_fnc = fs->pools.entrie_funcs + slab->pool;
  uint32_t num_fla_objs = pool_entry_fnc->fla_pool_num_fla_objs(pool_entry);

  err = fla_slab_cache_obj_free(&fs->slab_cache, obj, num_fla_objs);
  if(FLA_ERR(err, "fla_slab_cache_obj_free()"))
    goto exit;

  slab->refcount -= num_fla_objs;
  to_head = fs->fla_dp.fncs.get_pool_slab_list_id(slab, &fs->pools);

  err = fla_hdll_remove(fs, slab, from_head);
  if(FLA_ERR(err, "fla_hdll_remove()"))
    goto exit;

  err = fs->fla_dp.fncs.obj_destroy(fs, obj, pool_handle);
  if(FLA_ERR(err, "obj_destroy()"))
    goto exit;

  if (slab->refcount == 0)
  {
    err = fla_release_slab(fs, slab);
    if (FLA_ERR(err, "fla_release_slab()"))
      goto exit;

    err = fs->fla_dp.fncs.slab_format(fs, obj->slab_id);
    if (FLA_ERR(err, "slab_format()"))
      goto exit;

  } else {
    err = fla_hdll_prepend(fs, slab, to_head);
    if(FLA_ERR(err, "fla_hdll_prepend()"))
      goto exit;
  }

exit:
  return err;
}

int
fla_slab_range_check_id(const struct flexalloc * fs, const uint32_t s_id)
{
  return s_id > fs->geo.nslabs - 1;
}

struct fla_slab_header *
fla_slab_header_ptr(const uint32_t s_id, const struct flexalloc * fs)
{
  int err = 0;
  err = fla_slab_range_check_id(fs,s_id);
  if(FLA_ERR(err, "fla_slab_range_check_id()"))
  {
    return NULL;
  }

  return fs->slabs.headers + s_id;
}

int
fla_slab_id(const struct fla_slab_header * slab, const struct flexalloc * fs,
            uint32_t * slab_id)
{
  int err = 0;
  *slab_id = (uint32_t)(slab - fs->slabs.headers);

  err = fla_slab_range_check_id(fs, *slab_id);
  FLA_ERR(err, "fla_slab_range_check_id()");

  return err;
}

int
fla_acquire_slab(struct flexalloc *fs, const uint32_t obj_nlb,
                 struct fla_slab_header ** a_slab)
{
  int err = 0;

  if(*fs->slabs.fslab_num == 0)
  {
    // TODO : Define a proper error code for this.
    /* This error should not print anything as it is a valid state.  */
    err = FLA_ERR_ALL_SLABS_USED;
    goto exit;
  }

  err = fla_edll_remove_head(fs, fs->slabs.fslab_head, fs->slabs.fslab_tail, a_slab);
  if(FLA_ERR(err, "fla_edll_remove_head()"))
  {
    goto exit;
  }

  err = fla_format_slab(fs, *a_slab, obj_nlb);
  if(FLA_ERR(err, "fla_format_slab()"))
  {
    goto exit;
  }

  (*fs->slabs.fslab_num)--;

exit:
  return err;
}

int
fla_release_slab(struct flexalloc *fs, struct fla_slab_header * r_slab)
{
  int err = 0;
  uint32_t slab_id;

  if(FLA_ERR(r_slab->refcount, "fla_release_slab()"))
  {
    // TODO : Valid error when application tells us to release pool when there are still objects.
    // Define a proper error code.
    err = 1;
    goto exit;
  }

  err = fla_slab_id(r_slab, fs, &slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
  {
    goto exit;
  }

  fla_slab_cache_elem_drop(&fs->slab_cache, slab_id);

  err = fla_edll_add_tail(fs, fs->slabs.fslab_head, fs->slabs.fslab_tail, r_slab);
  if(FLA_ERR(err, "fla_edll_add_tail()"))
  {
    goto exit;
  }

  (*fs->slabs.fslab_num)++;

exit:
  return err;
}

int
fla_format_slab(struct flexalloc *fs, struct fla_slab_header * slab, uint32_t obj_nlb)
{
  int err = 0;
  uint32_t obj_num, slab_id;

  obj_num = fla_calc_objs_in_slab(fs, obj_nlb);
  if((err = FLA_ERR(obj_num < 1, "No objects can fit in the slab")))
    goto exit;

  slab->next = FLA_LINKED_LIST_NULL;
  slab->prev = FLA_LINKED_LIST_NULL;
  slab->refcount = 0;

  // FIXME: Do we need the pool ID here?
  slab->pool = 0;

  err = fla_slab_id(slab, fs, &slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
    goto exit;

  err = fs->fla_dp.fncs.slab_format(fs, slab_id);
  if (FLA_ERR(err, "slab_format()"))
    goto exit;

  err = fla_slab_cache_elem_init(&fs->slab_cache, slab_id, obj_num);
  if(FLA_ERR(err, "fla_slab_cache_elem_init()"))
    goto exit;

exit:
  return err;
}

uint64_t
fla_object_elba(struct flexalloc const * fs, struct fla_object const * obj,
                const struct fla_pool * pool_handle)
{
  const struct fla_pool_entry * pool_entry = &fs->pools.entries[pool_handle->ndx];
  const struct fla_pool_entry_fnc *pool_entry_fnc = &fs->pools.entrie_funcs[pool_handle->ndx];
  return fla_geo_slab_lb_off(fs, obj->slab_id)
         + pool_entry_fnc->get_slab_elba(pool_entry, obj->entry_ndx);
}

uint64_t
fla_object_eoffset(struct flexalloc const * fs, struct fla_object const * obj,
                   struct fla_pool const * pool_handle)
{
  return fla_object_elba(fs, obj, pool_handle) * fs->dev.lb_nbytes;
}

static int
fla_object_check_range(struct flexalloc const *fs, struct fla_object const *obj,
    struct fla_pool const *pool_handle, uint64_t const op_end_offset)
{
  int err;
  uint64_t obj_eoffset, slab_eoffset;

  obj_eoffset = fla_object_eoffset(fs, obj, pool_handle);
  slab_eoffset = (fla_geo_slab_lb_off(fs, obj->slab_id) + fs->geo.slab_nlb) * fs->geo.lb_nbytes;
  if((err = FLA_ERR(slab_eoffset < obj_eoffset, "Operation outside slab")))
    goto exit;

  if((err = FLA_ERR(obj_eoffset < op_end_offset, "Operation outside of an object")))
    goto exit;

exit:
  return err;
}

int
fla_object_read(const struct flexalloc * fs, struct fla_pool const * pool_handle,
                struct fla_object const * obj, void * buf, size_t r_offset, size_t r_len)
{
  int err;
  uint64_t obj_soffset, r_soffset;
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];

  obj_soffset = fla_object_soffset(fs, obj, pool_handle);
  r_soffset = obj_soffset + r_offset;

  err = fla_object_check_range(fs, obj, pool_handle, r_soffset + r_len);
  if (FLA_ERR(err, "fla_object_check_range()"))
    goto exit;

  struct fla_xne_io xne_io;
  xne_io.fla_dp = &fs->fla_dp;
  if (!(pool_entry->flags && FLA_POOL_ENTRY_STRP))
  {
    struct xnvme_lba_range range;
    range = fla_xne_lba_range_from_offset_nbytes(fs->dev.dev, r_soffset, r_len);
    if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
      goto exit;
    xne_io.dev = fs->dev.dev;
    xne_io.buf = buf;
    xne_io.lba_range = &range;

    err = fla_xne_sync_seq_r_xneio(&xne_io);
  }
  else
  {
    struct fla_pool_strp *strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
    struct fla_strp_params sp;
    xne_io.prep_ctx = fs->fla_dp.fncs.prep_dp_ctx;
    sp.strp_nobjs = strp_ops->strp_nobjs;
    sp.strp_chunk_nbytes = strp_ops->strp_nbytes;
    xne_io.io_type = FLA_IO_DATA_READ;
    xne_io.obj_handle = obj;
    xne_io.pool_handle = pool_handle;
    sp.faobj_nlbs = pool_entry->obj_nlb;
    sp.xfer_snbytes = r_offset;
    sp.xfer_nbytes = r_len;
    sp.strp_obj_tnbytes = (uint64_t)strp_ops->strp_nobjs * pool_entry->obj_nlb * fs->geo.lb_nbytes;
    sp.strp_obj_start_nbytes = obj_soffset;
    sp.dev_lba_nbytes = fs->geo.lb_nbytes;
    sp.write = false;
    xne_io.dev = fs->dev.dev;
    xne_io.buf = buf;
    xne_io.strp_params = &sp;
    err = fla_xne_async_strp_seq_xneio(&xne_io);
  }

  if(FLA_ERR(err, "fla_objec_read()"))
    goto exit;

exit:
  return err;
}

int
fla_object_write(struct flexalloc * fs, struct fla_pool const * pool_handle,
                 struct fla_object const * obj, void const * buf, size_t w_offset, size_t w_len)
{
  int err = 0;
  uint64_t obj_soffset, w_soffset;
  struct fla_pool_entry *pool_entry = &fs->pools.entries[pool_handle->ndx];
  struct fla_xne_io xne_io;

  obj_soffset = fla_object_soffset(fs, obj, pool_handle);
  w_soffset = obj_soffset + w_offset;

  err = fla_object_check_range(fs, obj, pool_handle, w_soffset + w_len);
  if (FLA_ERR(err, "fla_object_check_range()"))
    goto exit;

  xne_io.io_type = FLA_IO_DATA_WRITE;
  xne_io.dev = fs->dev.dev;
  xne_io.buf = (void*)buf;
  xne_io.prep_ctx = fs->fla_dp.fncs.prep_dp_ctx;
  xne_io.obj_handle = obj;
  xne_io.pool_handle = pool_handle;
  xne_io.fla_dp = &fs->fla_dp;
  if (!(pool_entry->flags && FLA_POOL_ENTRY_STRP))
  {
    struct xnvme_lba_range lba_range;
    lba_range = fla_xne_lba_range_from_offset_nbytes(xne_io.dev, w_soffset, w_len);
    if(( err = FLA_ERR(lba_range.attr.is_valid != 1, "fla_xne_lba_range_from_offset_nbytes()")))
      goto exit;
    xne_io.lba_range = &lba_range;

    err = fla_xne_sync_seq_w_xneio(&xne_io);
  }
  else
  {
    struct fla_pool_strp *strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
    struct fla_strp_params sp;
    sp.strp_nobjs = strp_ops->strp_nobjs;
    sp.strp_chunk_nbytes = strp_ops->strp_nbytes;
    sp.faobj_nlbs = pool_entry->obj_nlb;
    sp.xfer_snbytes = w_offset;
    sp.xfer_nbytes = w_len;
    sp.strp_obj_tnbytes = (uint64_t)strp_ops->strp_nobjs * pool_entry->obj_nlb * fs->geo.lb_nbytes;
    sp.strp_obj_start_nbytes = obj_soffset;
    sp.dev_lba_nbytes = fs->geo.lb_nbytes;
    sp.write = true;
    xne_io.strp_params = &sp;
    err = fla_xne_async_strp_seq_xneio(&xne_io);
  }

  if(FLA_ERR(err, "fla_object_write()"))
    goto exit;

exit:
  return err;
}

int
fla_object_unaligned_write(struct flexalloc * fs, struct fla_pool const * pool_handle,
                           struct fla_object const * obj, void const * w_buf, size_t obj_offset,
                           size_t len)
{
  int err = 0;
  void * bounce_buf, * buf;
  size_t bounce_buf_size,  aligned_sb, aligned_eb, orig_sb, orig_eb;

  orig_sb = (fla_object_slba(fs, obj, pool_handle) * fs->dev.lb_nbytes) + obj_offset;
  orig_eb = orig_sb + len;
  aligned_sb = (orig_sb / fs->dev.lb_nbytes) * fs->dev.lb_nbytes;
  aligned_eb = FLA_CEIL_DIV(orig_eb, fs->dev.lb_nbytes) * fs->dev.lb_nbytes;
  bounce_buf_size = aligned_eb - aligned_sb;

  err = fla_object_check_range(fs, obj, pool_handle, aligned_sb + bounce_buf_size);
  if (FLA_ERR(err, "fla_object_check_range()"))
    goto exit;

  bounce_buf = fla_xne_alloc_buf(fs->dev.dev, bounce_buf_size);
  if(FLA_ERR(!bounce_buf, "fla_buf_alloc()"))
  {
    err = -ENOENT;
    goto exit;
  }

  struct xnvme_lba_range range;

  if(aligned_sb < orig_sb)
  {
    range = fla_xne_lba_range_from_offset_nbytes(fs->dev.dev, aligned_sb, fs->dev.lb_nbytes);
    if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
      goto exit;
    struct fla_xne_io xne_io = {
      .dev = fs->dev.dev,
      .buf = bounce_buf,
      .lba_range = &range,
      .fla_dp = &fs->fla_dp};
    err = fla_xne_sync_seq_r_xneio(&xne_io);
    if(FLA_ERR(err, "fla_xne_sync_seq_r_xneio()"))
      goto free_bounce_buf;
  }

  if(aligned_eb > orig_eb)
  {
    range = fla_xne_lba_range_from_offset_nbytes(fs->dev.dev, aligned_eb - fs->dev.lb_nbytes,
            fs->dev.lb_nbytes);
    if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
      goto exit;
    buf = bounce_buf + bounce_buf_size - fs->dev.lb_nbytes;
    struct fla_xne_io xne_io = {
      .dev = fs->dev.dev,
      .buf = buf,
      .lba_range = &range,
      .fla_dp = &fs->fla_dp};

    err = fla_xne_sync_seq_r_xneio(&xne_io);
    if(FLA_ERR(err, "fla_xne_sync_seq_r_xneio()"))
      goto free_bounce_buf;
  }

  buf = bounce_buf + (orig_sb - aligned_sb);
  memcpy(buf, w_buf, len);

  struct xnvme_lba_range lba_range;
  struct fla_xne_io xne_io = {
    .io_type = FLA_IO_DATA_WRITE,
    .dev = fs->dev.dev,
    .buf = bounce_buf,
    .prep_ctx = fs->fla_dp.fncs.prep_dp_ctx,
    .obj_handle = obj,
    .pool_handle = pool_handle,
    .fla_dp = &fs->fla_dp};

  lba_range = fla_xne_lba_range_from_offset_nbytes(xne_io.dev, aligned_sb, bounce_buf_size);
  if(( err = FLA_ERR(lba_range.attr.is_valid != 1, "fla_xne_lba_range_from_offset_nbytes()")))
    goto free_bounce_buf;

  xne_io.lba_range = &lba_range;

  err = fla_xne_sync_seq_w_xneio(&xne_io);
  if(FLA_ERR(err, "fla_xne_sync_seq_w_xneio()"))
    goto free_bounce_buf;

free_bounce_buf:
  fla_xne_free_buf(fs->dev.dev, bounce_buf);

exit:
  return err;
}

int32_t
fla_fs_lb_nbytes(struct flexalloc const * const fs)
{
  return fs->dev.lb_nbytes;
}

struct fla_fns base_fns =
{
  .close = &fla_base_close,
  .sync = &fla_base_sync,
  .pool_open = &fla_base_pool_open,
  .pool_close = &fla_base_pool_close,
  .pool_create = &fla_base_pool_create,
  .pool_destroy = &fla_base_pool_destroy,
  .object_open = &fla_base_object_open,
  .object_create = &fla_base_object_create,
  .object_destroy = &fla_base_object_destroy,
  .pool_set_root_object = &fla_base_pool_set_root_object,
  .pool_get_root_object = &fla_base_pool_get_root_object,
};

int
fla_object_seal(struct flexalloc *fs, struct fla_pool const *pool_handle, struct fla_object *obj)
{
  return fs->fla_cs.fncs.object_seal(fs, pool_handle, obj);
}

int
fla_open(struct fla_open_opts *opts, struct flexalloc **fs)
{
  struct xnvme_dev *dev = NULL, *md_dev = NULL;
  void *fla_md_buf;
  size_t fla_md_buf_len;
  struct fla_geo geo;
  struct fla_super *super;
  int err = 0;;

  (*fs) = fla_fs_alloc();
  if (!(*fs))
  {
    err = -ENOMEM;
    goto exit;
  }

  err = fla_xne_dev_open(opts->dev_uri, opts->opts, &dev);
  if (FLA_ERR(err, "fla_xne_dev_open()"))
    goto free_fs;

  if (opts->md_dev_uri)
  {
    err = fla_xne_dev_open(opts->md_dev_uri, NULL, &md_dev);
    if (FLA_ERR(err, "fla_xne_dev_open()"))
      goto xnvme_dev_close;
  }
  else
    md_dev = dev;

  err = fla_dev_sanity_check(dev, md_dev);
  if(FLA_ERR(err, "fla_dev_sanity_check()"))
    goto xnvme_dev_close;

  err = fla_super_read(md_dev, fla_xne_dev_lba_nbytes(dev), &super);
  if (FLA_ERR(err, "fla_super_read"))
    goto xnvme_dev_close;

  // read disk geometry
  fla_geo_from_super(dev, super, &geo);

  // allocate 1 big buffer to hold all metadata not internal to a slab.
  fla_md_buf_len = fla_geo_nbytes(&geo);
  fla_md_buf = fla_xne_alloc_buf(md_dev, fla_md_buf_len);
  if (FLA_ERR(!fla_md_buf, "fla_xne_alloc_buf()"))
  {
    err = -ENOMEM;
    goto free_super;
  }

  memset(fla_md_buf, 0, fla_md_buf_len);

  struct xnvme_lba_range range;
  range = fla_xne_lba_range_from_offset_nbytes(md_dev, 0, fla_md_buf_len);
  if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;
  struct fla_xne_io xne_io = {.dev = md_dev, .buf = fla_md_buf, .lba_range = &range, .fla_dp = &(*fs)->fla_dp};

  err = fla_xne_sync_seq_r_xneio(&xne_io);
  if (FLA_ERR(err, "fla_xne_sync_seq_r_nbyte_nbytess()"))
    goto free_md;

  if (md_dev == dev)
    err = fla_init(&geo, dev, NULL, fla_md_buf, (*fs));
  else
    err = fla_init(&geo, dev, md_dev, fla_md_buf, (*fs));

  if (FLA_ERR(err, "fla_init()"))
    goto free_md;

  err = fla_slab_cache_init((*fs), &((*fs)->slab_cache));
  if (FLA_ERR(err, "fla_slab_cache_init()"))
    goto free_md;

  fla_xne_free_buf(dev, super);

  (*fs)->dev.dev_uri = fla_strdup(opts->dev_uri);
  if ((*fs)->dev.dev_uri == NULL)
  {
    err = -ENOMEM;
    goto free_md;
  }

  if(md_dev != dev)
  {
    (*fs)->dev.md_dev_uri = fla_strdup(opts->md_dev_uri);
    if ((*fs)->dev.md_dev_uri == NULL)
    {
      err = -ENOMEM;
      goto free_dev_uri;
    }
  }

  (*fs)->state |= FLA_STATE_OPEN;
  (*fs)->fns = base_fns;

  return 0;

free_dev_uri:
  free((*fs)->dev.dev_uri);
free_md:
  fla_xne_free_buf(md_dev, fla_md_buf);
free_super:
  fla_xne_free_buf(md_dev, super);
xnvme_dev_close:
  if (dev != md_dev)
    xnvme_dev_close(dev);

  xnvme_dev_close(md_dev);
free_fs:
  free(*fs);
exit:

  return err;
}

