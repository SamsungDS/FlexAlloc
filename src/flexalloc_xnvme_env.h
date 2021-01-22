// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>

#ifndef __XNVME_ENV_H
#define __XNVME_ENV_H

#include <stdint.h>
#include <stdbool.h>
#include <libxnvme.h>
#include <libxnvme_lba.h>

/**
 * @brief Synchronous sequential write defined by lbs
 *
 * @param dev xnvme device
 * @param slba lba where to begin the write
 * @param naddrs Number of addresses to write
 * @param buf Buffer to write from
 * @return Zero on success. non-zero on error.
 */
int
fla_xne_sync_seq_w_naddrs(struct xnvme_dev * dev, const uint64_t slba, const uint64_t naddrs,
                          void const * buf);

int
/**
 * @brief Synchronous sequential write defined by bytes
 *
 * @param dev xnvme device
 * @param offset Byte offset where to begin the write
 * @param nbytes Number of bytes to write
 * @param buf Buffer to write from
 * @return Zero on success. Non-zero on error.
 */
fla_xne_sync_seq_w_nbytes(struct xnvme_dev * dev, const uint64_t offset, uint64_t nbytes,
                          void const * buf);

/**
 * @brief Write zeroes over range.
 *
 * Overwrites given logical block range with zeroes.
 * NOTE: subtracts 1 from lba_range->naddrs because xNVMe expects the
 * number of addresses value to be zero based.
 *
 * @param lba_range Xnvme structure defining the range of the write
 * @param xnvme_dev Xnvme device
 *
 * @return zero on success. non-zero on error
 */
int
fla_xne_write_zeroes(const struct xnvme_lba_range *lba_range, struct xnvme_dev *xne_hdl);

/**
 * @brief Synchronous sequential read from storage
 *
 * @param dev xnvme device
 * @param slba lba where to begin reading
 * @param naddrs Number of addresses to read
 * @param buf Buffer where to put the read bytes
 * @return Zero on success. non-zero on error
 */
int
fla_xne_sync_seq_r_naddrs(struct xnvme_dev * dev, const uint64_t slba, const uint64_t naddrs,
                          void * buf);

/**
 * @brief Synchronous sequential read defined by bytes
 *
 * @param dev xnvme device
 * @param offset Byte offset where to begin reading
 * @param nbytes Number of bytes to read
 * @param buf Buffere where to put the read bytes
 * @return ZEro on success. non-zero on error.
 */
int
fla_xne_sync_seq_r_nbytes(struct xnvme_dev * dev, const uint64_t offset, uint64_t nbytes,
                          void * buf);

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

int
fla_xne_dev_open(const char *dev_uri, struct xnvme_opts *opts, struct xnvme_dev **dev);

void
fla_xne_dev_close(struct xnvme_dev *dev);

int
fla_xne_dev_znd_reset(struct xnvme_dev *dev, uint64_t slba, bool all);

int
fla_xne_dev_mkfs_prepare(struct xnvme_dev *dev, char *md_dev_uri, struct xnvme_dev **md_dev);
/**
 * @brief Check if parameters are within range
 *
 * @param dev xnvme device
 * @return zero if everything is ok. Non-zero if there is a problem
 */
int
fla_xne_dev_sanity_check(struct xnvme_dev const * dev, struct xnvme_dev const * md_dev);

#endif /*__XNVME_ENV_H */
