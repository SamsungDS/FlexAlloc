// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>

#ifndef __XNVME_ENV_H
#define __XNVME_ENV_H

#include <stdint.h>
#include <stdbool.h>
#include <libxnvme.h>
#include <libxnvme_lba.h>

struct fla_strp_params
{
  /// Num of objects to stripe across
  uint32_t strp_nobjs;

  /// Number of bytes of each stripe chunk
  uint32_t strp_chunk_nbytes;

  /// Number of lbs in a non-striped object
  uint64_t faobj_nlbs;

  /// Start offset within object (slba == 0)
  uint64_t xfer_snbytes;

  /// Number of bytes to transfer
  uint64_t xfer_nbytes;

  /// Total bytes in a striped object
  uint64_t strp_obj_tnbytes;

  /// striped object offset within device
  uint64_t strp_obj_start_nbytes;

  /// the device logical block size
  uint32_t dev_lba_nbytes;

  /// Wether to write or not
  bool write;
};

enum fla_xne_io_type
{
  FLA_IO_DATA_WRITE = 0,
  FLA_IO_MD_WRITE,
  FLA_IO_MD_READ,
  FLA_IO_DATA_READ,
};

struct fla_xne_io
{
  enum fla_xne_io_type io_type;
  struct xnvme_dev *dev;
  void *buf;
  union
  {
    struct xnvme_lba_range * lba_range;
    struct fla_strp_params * strp_params;
  };

  int (*prep_ctx)(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx);
};

struct xnvme_lba_range
fla_xne_lba_range_from_offset_nbytes(struct xnvme_dev *dev, uint64_t offset, uint64_t nbytes);

/**
 * @brief Synchronous sequential write
 *
 * @param xne_io contains dev, slba, naddrs and buf
 * @return Zero on success. non-zero on error.
 */
int
fla_xne_sync_seq_w_xneio(struct fla_xne_io *xne_io);

/**
 * @brief asynchronous stripped sequential write
 *
 * @param xne_io contains dev, slba, naddrs and buf
 * @return Zero on success. non-zero on error.
 */
int
fla_xne_async_strp_seq_xneio(struct fla_xne_io *xne_io);

/**
 * @brief Synchronous sequential read from storage
 *
 * @param xne_io contains dev, slba, naddrs and buf
 * @return Zero on success. non-zero on error
 */
int
fla_xne_sync_seq_r_xneio(struct fla_xne_io *xne_io);

/**
 * @brief Allocate a buffer with xnvme allocate
 *
 * @param dev xnvme device
 * @param nbytes Number of bytes to allocate
 */
void *
fla_xne_alloc_buf(const struct xnvme_dev *dev, size_t nbytes);

/**
 * @brief Reallocate a buffer with xnvme reallocate
 *
 * @param dev xnvme device
 * @param buf Buffer to reallocate
 * @param nbytes Size of the allocated buffer in bytes
 */
void *
fla_xne_realloc_buf(const struct xnvme_dev *dev, void *buf,
                    size_t nbytes);
/**
 * @brief Free a buffer with xnvme free
 *
 * @param dev xnvme device
 * @param buf Buffer to free
 */
void
fla_xne_free_buf(const struct xnvme_dev * dev, void * buf);

uint64_t
fla_xne_dev_tbytes(const struct xnvme_dev * dev);

uint32_t
fla_xne_dev_lba_nbytes(const struct xnvme_dev * dev);

uint32_t
fla_xne_dev_znd_zones(const struct xnvme_dev *dev);

uint64_t
fla_xne_dev_znd_sect(const struct xnvme_dev *dev);

enum xnvme_geo_type
fla_xne_dev_type(const struct xnvme_dev *dev);

uint32_t
fla_xne_dev_mdts_nbytes(const struct xnvme_dev *dev);

int
fla_xne_dev_open(const char *dev_uri, struct xnvme_opts *opts, struct xnvme_dev **dev);

void
fla_xne_dev_close(struct xnvme_dev *dev);

int
fla_xne_dev_znd_send_mgmt(struct xnvme_dev *dev, uint64_t slba,
                          enum xnvme_spec_znd_cmd_mgmt_send_action act, bool);

uint32_t
fla_xne_dev_get_znd_mar(struct xnvme_dev *dev);

uint32_t
fla_xne_dev_get_znd_mor(struct xnvme_dev *dev);

int
fla_xne_dev_mkfs_prepare(struct xnvme_dev *dev, char const *md_dev_uri, struct xnvme_dev **md_dev);
/**
 * @brief Check if parameters are within range
 *
 * @param dev xnvme device
 * @return zero if everything is ok. Non-zero if there is a problem
 */
int
fla_xne_dev_sanity_check(struct xnvme_dev const * dev, struct xnvme_dev const * md_dev);

struct xnvme_lba_range
fla_xne_lba_range_from_slba_naddrs(struct xnvme_dev *dev, uint64_t slba, uint64_t naddrs);
#endif /*__XNVME_ENV_H */
