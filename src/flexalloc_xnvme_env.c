// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>

#include <libxnvme.h>
#include <libxnvme_dev.h>
#include <libxnvme_pp.h>
#include <libxnvme_lba.h>
#include <libxnvme_nvm.h>
#include <libxnvme_znd.h>
#include <libxnvmec.h>

#include <errno.h>
#include <stdint.h>
#include "flexalloc_xnvme_env.h"
#include "flexalloc_util.h"

struct fla_async_cb_args
{
  uint32_t ecount;
  uint32_t completed;
  uint32_t submitted;
};

static void
fla_async_cb(struct xnvme_cmd_ctx *ctx, void *cb_arg)
{
  struct fla_async_cb_args *cb_args = cb_arg;

  cb_args->completed++;

  if (xnvme_cmd_ctx_cpl_status(ctx))
  {
    xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
    cb_args->ecount++;
  }

  xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
}

uint32_t
fla_xne_calc_mdts_naddrs(const struct xnvme_dev * dev)
{
  const struct xnvme_geo *geo = xnvme_dev_get_geo(dev);
  return fla_min(geo->mdts_nbytes / geo->lba_nbytes, UINT16_MAX);
}

struct xnvme_lba_range
fla_xne_lba_range_from_slba_naddrs(struct xnvme_dev *dev, uint64_t slba, uint64_t naddrs)
{
  return xnvme_lba_range_from_slba_naddrs(dev, slba, naddrs);

}

struct xnvme_lba_range
fla_xne_lba_range_from_offset_nbytes(struct xnvme_dev *dev, uint64_t offset, uint64_t nbytes)
{
  return xnvme_lba_range_from_offset_nbytes(dev, offset, nbytes);
}

int
fla_xne_dev_mkfs_prepare(struct xnvme_dev *dev, char *md_dev_uri, struct xnvme_dev **md_dev)
{
  int err = 0;

  if (md_dev_uri)
  {
    if (fla_xne_dev_type(dev) == XNVME_GEO_ZONED)
    {
      err = fla_xne_dev_znd_send_mgmt(dev, 0, XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET, true);
      if (FLA_ERR(err, "flexalloc_xne_dev_znd_reset()"))
        return err;
    }

    err = fla_xne_dev_open(md_dev_uri, NULL, md_dev);
    if (FLA_ERR(err,"failed to open md dev\n"))
      return -1;
  }

  return err;
}

int
fla_xne_dev_znd_send_mgmt(struct xnvme_dev *dev, uint64_t slba,
                          enum xnvme_spec_znd_cmd_mgmt_send_action act, bool all)
{
  int err = 0;
  uint32_t nsid;
  nsid = xnvme_dev_get_nsid(dev);
  struct xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);

  if (fla_xne_dev_type(dev) != XNVME_GEO_ZONED)
  {
    FLA_ERR_PRINT("Attempting zoned reset on a non zoned namespace\n");
    return -EINVAL;
  }

  err = xnvme_znd_mgmt_send(&ctx, nsid, slba, all, act, 0, NULL);
  if (err || xnvme_cmd_ctx_cpl_status(&ctx))
  {
    xnvmec_perr("xnvme_nvme_znd_mgmt_send", err);
    xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
    err = err ? err : -EIO;
  }

  return err;
}

uint32_t
fla_xne_dev_get_znd_mar(struct xnvme_dev *dev)
{
  const struct xnvme_spec_znd_idfy_ns *zns = (void *)xnvme_dev_get_ns_css(dev);
  return zns->mar;
}

uint32_t
fla_xne_dev_get_znd_mor(struct xnvme_dev *dev)
{
  const struct xnvme_spec_znd_idfy_ns *zns = (void *)xnvme_dev_get_ns_css(dev);
  return zns->mor;
}

int
fla_xne_sync_seq_w(const struct xnvme_lba_range * lba_range,
                   struct xnvme_dev * xne_hdl, const void * buf)
{
  int err = 0;
  uint32_t nsid;
  const char * wbuf = buf;
  struct xnvme_cmd_ctx ctx;

  nsid = xnvme_dev_get_nsid(xne_hdl);
  ctx = xnvme_cmd_ctx_from_dev(xne_hdl);

#ifdef FLA_XNVME_IGNORE_MDTS
  err = xnvme_nvm_write(&ctx, nsid, lba_range->slba, lba_range->naddrs - 1, wbuf, NULL);
  if (err || xnvme_cmd_ctx_cpl_status(&ctx))
  {
    xnvmec_perr("xnvme_nvm_write()", err);
    xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
    err = err ? err : -EIO;
    goto exit;
  }

#else
  uint32_t nlb, mdts_naddrs;
  const struct xnvme_geo * geo;
  mdts_naddrs = fla_xne_calc_mdts_naddrs(xne_hdl);
  geo = xnvme_dev_get_geo(xne_hdl);

  for(uint64_t slba = lba_range->slba; slba <= lba_range->elba; slba += mdts_naddrs)
  {
    /* mdts_naddrs -1 because it is not a zero based value */
    nlb = XNVME_MIN(lba_range->elba - slba, mdts_naddrs - 1);

    err = xnvme_nvm_write(&ctx, nsid, slba, nlb, wbuf, NULL);
    if (err || xnvme_cmd_ctx_cpl_status(&ctx))
    {
      xnvmec_perr("xnvme_nvm_write()", err);
      xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
      err = err ? err : -EIO;
      goto exit;
    }

    wbuf += (nlb+1)*geo->lba_nbytes;
  }
#endif //FLA_XNVME_IGNORE_MDTS

exit:
  return err;
}

int
fla_xne_sync_seq_w_naddrs(struct xnvme_dev * dev, const uint64_t slba, const uint64_t naddrs,
                          void const * buf)
{
  int err;
  struct xnvme_lba_range range;

  range = fla_xne_lba_range_from_slba_naddrs(dev, slba, naddrs);
  if ((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;

  err = fla_xne_sync_seq_w(&range, dev, buf);
  if (FLA_ERR(err, "fla_xne_sync_seq_w()"))
    goto exit;

exit:
  return err;
}

// Read or write out a larger chunk striped across multiple objects
int
fla_xne_sync_strp_seq_x(struct xnvme_dev *dev, const uint64_t offset, uint64_t nbytes,
                        void const *buf, struct fla_sync_strp_params const * const sp, bool write)
{
  int err;
  uint32_t nsid = xnvme_dev_get_nsid(dev);
  struct xnvme_queue *queue = NULL;
  struct fla_async_cb_args cb_args = { 0 };
  const struct xnvme_geo *geo = xnvme_dev_get_geo(dev);
  uint32_t strp_blks = (sp->strp_nbytes / geo->lba_nbytes) - 1;
  uint64_t bytes_to_xfer = nbytes, strp_offset = offset, strp_to_xfer;
  char *strp_buf = (char *)buf;

  if (write)
  {
    if (FLA_ERR(nbytes % (sp->strp_nobjs * sp->strp_nbytes) > 0,
                "Striped write must be a multiple of sp->strp_nbytes and nbytes")
        || FLA_ERR(nbytes/sp->strp_nobjs < sp->strp_nbytes,
                   "Num bytes not large enough for sp->strp_nbytes * sp->strp_nobjs"))
    {
      err = -1;
      goto exit;
    }
  }

  // Make sure offset aligns on strp boundary
  if (offset % sp->strp_nbytes)
  {
    err = -1;
    FLA_ERR(err, "Striped write offset must be aligned to strp sz");
    goto exit;
  }

  err = xnvme_queue_init(dev, sp->strp_nobjs, 0, &queue);
  if (FLA_ERR(err, "xnvme_queue_init"))
    goto exit;

  xnvme_queue_set_cb(queue, fla_async_cb, &cb_args);

  while (bytes_to_xfer > 0)
  {
    strp_to_xfer = bytes_to_xfer / sp->strp_nbytes;
    for (uint32_t strp=0; strp < strp_to_xfer;)
    {
      struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
      uint64_t slba = (strp_offset/geo->lba_nbytes) + (strp * sp->obj_len);
submit:
      if (write)
        err = xnvme_nvm_write(ctx, nsid, slba, strp_blks, strp_buf + (strp * sp->strp_nbytes),
                              NULL);
      else
        err = xnvme_nvm_read(ctx, nsid, slba, strp_blks, strp_buf + (strp * sp->strp_nbytes),
                             NULL);

      switch (err)
      {
      case 0:
        cb_args.submitted +=1;
        goto next;

      case -EBUSY:
      case -EAGAIN:
        xnvme_queue_poke(queue, 0);
        goto submit;

      default:
        FLA_ERR(1, "Async submission error\n");
        goto exit;
      }

next:
      ++strp;
    }

    err = xnvme_queue_wait(queue);
    bytes_to_xfer -=  sp->strp_nbytes * strp_to_xfer;
    strp_buf += sp->strp_nbytes * sp->strp_nobjs;
    strp_offset += sp->strp_nbytes;
    if (FLA_ERR(cb_args.ecount, "Error completing async IO\n"))
      goto exit;

  }

exit:
  if (queue)
  {
    int err_exit = xnvme_queue_term(queue);
    FLA_ERR(err_exit, "xnvme_queue_term");
  }

  return err < 0 ? err : 0;
}

int
fla_xne_sync_seq_w_nbytes(struct xnvme_dev * dev, const uint64_t offset, uint64_t nbytes,
                          void const * buf)
{
  int err;
  struct xnvme_lba_range range;

  range = fla_xne_lba_range_from_offset_nbytes(dev, offset, nbytes);
  if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_offset_nbytes()")))
    goto exit;

  err = fla_xne_sync_seq_w(&range, dev, buf);
  if (FLA_ERR(err, "fla_xne_sync_seq_w()"))
    goto exit;

exit:
  return err;
}


int
fla_xne_sync_seq_r(const struct xnvme_lba_range * lba_range,
                   struct xnvme_dev * xne_hdl, void * buf)
{
  int err = 0;
  uint32_t nsid;
  char * rbuf = buf;
  struct xnvme_cmd_ctx ctx;

  nsid = xnvme_dev_get_nsid(xne_hdl);
  ctx = xnvme_cmd_ctx_from_dev(xne_hdl);

#ifdef FLA_XNVME_IGNORE_MDTS
  err = xnvme_nvm_read(&ctx, nsid, lba_range->slba, lba_range->naddrs - 1, rbuf, NULL);
  if (err || xnvme_cmd_ctx_cpl_status(&ctx))
  {
    xnvmec_perr("xnvme_nvm_read()", err);
    xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
    err = err ? err : -EIO;
    goto exit;
  }
#else
  uint32_t nlb, mdts_naddrs;
  const struct xnvme_geo * geo;
  mdts_naddrs = fla_xne_calc_mdts_naddrs(xne_hdl);
  geo = xnvme_dev_get_geo(xne_hdl);
  for(uint64_t slba = lba_range->slba; slba <= lba_range->elba; slba += mdts_naddrs)
  {
    /* mdts_naddrs -1 because it is not a zero based value */
    nlb = XNVME_MIN(lba_range->elba - slba, mdts_naddrs - 1);

    err = xnvme_nvm_read(&ctx, nsid, slba, nlb, rbuf, NULL);
    if (err || xnvme_cmd_ctx_cpl_status(&ctx))
    {
      xnvmec_perr("xnvme_nvm_read()", err);
      xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
      err = err ? err : -EIO;
      goto exit;
    }
    rbuf += (nlb+1)*geo->lba_nbytes;
  }
#endif //FLA_XNVME_IGNORE_MDTS

exit:
  return err;
}

int
fla_xne_sync_seq_r_naddrs(struct xnvme_dev * dev, const uint64_t slba, const uint64_t naddrs,
                          void * buf)
{
  int err;
  struct xnvme_lba_range range;

  range = fla_xne_lba_range_from_slba_naddrs(dev, slba, naddrs);
  if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;

  err = fla_xne_sync_seq_r(&range, dev, buf);
  if(FLA_ERR(err, "fla_xne_sync_seq_w()"))
    goto exit;

exit:
  return err;
}

int
fla_xne_sync_seq_r_nbytes(struct xnvme_dev * dev, const uint64_t offset, uint64_t nbytes,
                          void * buf)
{
  int err;
  struct xnvme_lba_range range;

  range = fla_xne_lba_range_from_offset_nbytes(dev, offset, nbytes);
  if((err = FLA_ERR(range.attr.is_valid != 1, "fla_xne_lba_range_from_slba_naddrs()")))
    goto exit;

  err = fla_xne_sync_seq_r(&range, dev, buf);
  if(FLA_ERR(err, "fla_xne_sync_seq_w()"))
    goto exit;

exit:
  return err;
}

int
fla_xne_write_zeroes(const struct xnvme_lba_range *lba_range,
                     struct xnvme_dev *xne_hdl)
{
  struct xnvme_cmd_ctx ctx;
  uint32_t nsid;
  int err = 0;

  nsid = xnvme_dev_get_nsid(xne_hdl);
  ctx = xnvme_cmd_ctx_from_dev(xne_hdl);

#ifdef FLA_XNVME_IGNORE_MDTS
  err = xnvme_nvm_write_zeroes(&ctx, nsid, lba_range->slba, lba_range->naddrs - 1);
  if (err || xnvme_cmd_ctx_cpl_status(&ctx))
  {
    xnvmec_perr("xnvme_nvm_write_zeroes()", err);
    xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
    err = err ? err : -EIO;
    goto exit;
  }
#else
  uint32_t nlb, mdts_naddrs;
  mdts_naddrs = fla_xne_calc_mdts_naddrs(xne_hdl);

  for (uint64_t slba = lba_range->slba; slba <= lba_range->elba; slba += mdts_naddrs)
  {
    /* mdtsnaddrs -1 because it is not a zero-based value */
    nlb = XNVME_MIN(lba_range->elba - slba, mdts_naddrs - 1);

    err = xnvme_nvm_write_zeroes(&ctx, nsid, slba, nlb);
    if (err || xnvme_cmd_ctx_cpl_status(&ctx))
    {
      xnvmec_perr("xnvme_nvm_write_zeroes()", err);
      xnvme_cmd_ctx_pr(&ctx, XNVME_PR_DEF);
      err = err ? err : -EIO;
      goto exit;
    }
  }
#endif //FLA_XNVME_IGNORE_MDTS

exit:
  return err;
}

void *
fla_xne_alloc_buf(const struct xnvme_dev *dev, size_t nbytes)
{
  return xnvme_buf_alloc(dev, nbytes);
}

void *
fla_xne_realloc_buf(const struct xnvme_dev *dev, void *buf,
                    size_t nbytes)
{
  return xnvme_buf_realloc(dev, buf, nbytes);
}

void
fla_xne_free_buf(const struct xnvme_dev * dev, void * buf)
{
  xnvme_buf_free(dev, buf);
}

uint64_t
fla_xne_dev_tbytes(const struct xnvme_dev * dev)
{
  const struct xnvme_geo *geo = xnvme_dev_get_geo(dev);
  return geo->tbytes;
}

uint32_t
fla_xne_dev_lba_nbytes(const struct xnvme_dev * dev)
{
  struct xnvme_geo const * geo = xnvme_dev_get_geo(dev);
  return geo->lba_nbytes;
}

uint32_t
fla_xne_dev_znd_zones(const struct xnvme_dev *dev)
{
  struct xnvme_geo const *geo = xnvme_dev_get_geo(dev);
  return geo->nzone;
}

uint64_t
fla_xne_dev_znd_sect(const struct xnvme_dev *dev)
{
  struct xnvme_geo const *geo = xnvme_dev_get_geo(dev);
  return geo->nsect;
}

enum xnvme_geo_type
fla_xne_dev_type(const struct xnvme_dev *dev)
{
  struct xnvme_geo const *geo = xnvme_dev_get_geo(dev);
  return geo->type;
}

int
fla_xne_dev_open(const char *dev_uri, struct xnvme_opts *opts, struct xnvme_dev **dev)
{
  struct xnvme_opts default_opts;
  int err = 0;

  if (opts == NULL)
  {
    default_opts = xnvme_opts_default();
    opts = &default_opts;
    opts->direct = 1;
  }

  *dev = xnvme_dev_open(dev_uri, opts);
  if (FLA_ERR(!(*dev), "xnvme_dev_open()"))
  {
    err = 1001;
    return err;
  }
  return err;
}

void
fla_xne_dev_close(struct xnvme_dev *dev)
{
  xnvme_dev_close(dev);
}

int
fla_xne_dev_sanity_check(struct xnvme_dev const * dev, struct xnvme_dev const *md_dev)
{
  int err = 0;
  struct xnvme_geo const * geo = xnvme_dev_get_geo(dev);
  struct xnvme_geo const *md_geo = NULL;

  if (md_dev)
    md_geo = xnvme_dev_get_geo(md_dev);
  /*
   * xNVMe's linux backend can potentially fallback on an incorrect minimum data transfer
   * (mdts) value if application is not executed with admin privileges. Check here that we
   * have an mdts of 512 bytes.
   */
  err |= geo->mdts_nbytes <= 512; /* TODO get rid of these magic values */
  if (md_dev)
    err |= md_geo->mdts_nbytes <= 512;

  FLA_ERR(err, "The minimum data transfer value of dev or md_dev reported to be less than 512."
          \
          " This is most probably due to lack of administrative privileges. you can solve " \
          " this by running with sudo for example.");

  return err;
}
