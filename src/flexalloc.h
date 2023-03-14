/**
 * flexalloc disk structures.
 *
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 * Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
 * Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>
 *
 * @file flexalloc.h
 */
#ifndef __FLEXALLOC_H_
#define __FLEXALLOC_H_
#include <stdint.h>
#include <libxnvme.h>
#include "flexalloc_shared.h"
#include "flexalloc_freelist.h"
#include "flexalloc_hash.h"
#include "flexalloc_pool.h"
#include "flexalloc_dp_fdp.h"
#include "flexalloc_dp.h"
#include "flexalloc_cs.h"

/// flexalloc device handle
struct fla_dev
{
  struct xnvme_dev *dev;
  char * dev_uri;
  struct xnvme_dev *md_dev;
  char * md_dev_uri;
  uint32_t lb_nbytes;
};

struct fla_slab_flist_cache
{
  /// flexalloc system handle
  struct flexalloc *_fs;
  /// Head of cache array, entry at offset n corresponds to slab with id n
  struct fla_slab_flist_cache_elem *_head;
};


struct fla_geo_slab_sgmt
{
  // # LBs for all slab segment
  uint32_t slab_sgmt_nlb;
};

/// Describes flexalloc disk geometry
struct fla_geo
{
  /// number of LBAs / disk size
  uint64_t nlb;
  /// LBA width, in bytes, read from disk
  uint32_t lb_nbytes;
  /// size of slabs in number of LBAs
  uint32_t slab_nlb;
  /// number of pools to reserve space for
  uint32_t npools;
  /// number of slabs
  uint32_t nslabs;

  /// number of blocks for fixed portion of metadata
  uint32_t md_nlb;
  /// blocks needed for pool segment
  struct fla_geo_pool_sgmt pool_sgmt;
  /// blocks needed for slab segment
  struct fla_geo_slab_sgmt slab_sgmt;
};

struct fla_pools
{
  /// pointer to pools freelist bit array
  freelist_t freelist;
  /// htbl data structure
  struct fla_htbl htbl;
  /// buffer into which to update the htbl data
  struct fla_pool_htbl_header *htbl_hdr_buffer;
  /// array of pool entries
  struct fla_pool_entry *entries;
  /// array of pool_entry functions in memeory
  struct fla_pool_entry_fnc *entrie_funcs;

};

struct fla_slabs
{
  struct fla_slab_header *headers;

  uint32_t *fslab_num;
  uint32_t *fslab_head;
  uint32_t *fslab_tail;
};

struct fla_dp_fncs
{
  int (*init_dp)(struct flexalloc *fs, uint64_t flags);
  int (*fini_dp)(struct flexalloc *fs);
  int (*prep_dp_ctx)(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx);
  uint32_t* (*get_pool_slab_list_id)(struct fla_slab_header const *slab,
                                    struct fla_pools const *pools);
  int (*get_next_available_slab)(struct flexalloc * fs, struct fla_pool_entry * pool_entry,
                                 struct fla_slab_header ** slab);
  int (*slab_format)(struct flexalloc * fs, uint32_t const slab_id, struct fla_slab_header * slab);
};

struct fla_dp
{
  enum fla_dp_t dp_type;
  union
  {
    struct fla_dp_fdp     *fla_dp_fdp;
    struct fla_dp_zns     *fla_dp_zns;
  };

  struct fla_dp_fncs fncs;
};

/// flexalloc handle
struct flexalloc
{
  struct fla_dev dev;
  unsigned int state;
  /// buffer holding all the disk-wide flexalloc metadata
  ///
  /// NOTE: allocated as an IO buffer.
  void *fs_buffer;

  struct fla_slab_flist_cache slab_cache;

  struct fla_geo geo;

  struct fla_super *super;
  struct fla_pools pools;
  struct fla_slabs slabs;
  struct fla_dp fla_dp;
  struct fla_cs fla_cs;

  struct fla_fns fns;

  /// pointer for the application to associate additional data
  void *user_data;
};

uint32_t
fla_calc_objs_in_slab(struct flexalloc const * fs, uint32_t const obj_nlb);
#endif // __FLEXALLOC_H_
