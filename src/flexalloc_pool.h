/*
 * Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
 */

#ifndef __FLEXALLOC_POOL_H_
#define __FLEXALLOC_POOL_H_

#include <stdint.h>
#include "flexalloc_shared.h"

#define FLA_NAME_SIZE_POOL 112

struct fla_geo_pool_sgmt
{
  /// number of logical blocks for the freelist
  uint32_t freelist_nlb;
  /// number of logical blocks to contain the hash table
  uint32_t htbl_nlb;
  /// number of slots in the hash table
  uint32_t htbl_tbl_size;
  /// number of logical blocks to contain the pool entries
  uint32_t entries_nlb;
};

struct fla_pool_htbl_header
{
  /// number of slots in the hash table
  uint32_t size;
  /// number of elements presently inserted
  uint32_t len;
};

struct fla_pool_strp
{
  /// Num of objects to stripe across
  ///
  /// Pools may optionally hand out striped objects
  uint32_t strp_nobjs;

  /// Number of bytes of each stripe chunk
  ///
  /// When striped, each fla object is subdivided into stripe
  /// subsection or chunks.
  /// Must be set if we set strp_nobjs
  uint32_t strp_nbytes;
};

struct fla_pool_entry
{
  /// identifier for each slab depending on its "fullness"
  uint32_t empty_slabs;
  uint32_t full_slabs;
  uint32_t partial_slabs;

  /// Number of LBA's used for each object in the cache
  ///
  /// Note that all objects in a cache have the same size.
  /// Also note that this is rounded up to fit alignment requirements.
  uint32_t obj_nlb;

  /// Number of Objects that fit in each slab
  uint32_t slab_nobj;

  /// Root object that is optionally set
  ///
  /// Pools can have any valid flexalloc object set as a root object
  uint64_t root_obj_hndl;

  /// Num of objects to stripe across
  ///
  /// Pools may optionally hand out striped objects
  uint32_t strp_nobjs;

  /// Number of bytes of each stripe chunk
  ///
  /// When striped, each fla object is subdivided into stripe
  /// subsection or chunks.
  /// Must be set if we set strp_nobjs
  uint32_t strp_nbytes;

  uint64_t flags;
  uint64_t usable;

  /// Human-readable cache identifier
  ///
  /// The cache name is primarily used for debugging statistics
  /// and when creating the read-only file system view.
  // TODO get rid of 512B block assumption
  char name[FLA_NAME_SIZE_POOL]; // maximize use of 512B block while having entries aligned by 8B

  //TODO : add a struct nested inside fla_pool_entry which holds just the metadata
  //       (everything but the name and name length). This will allow us to ignore the
  //       name when we need to.
};

struct fla_pool_entry_fnc
{
  uint64_t (*get_slab_elba)(struct fla_pool_entry const * pool_entry,
                            uint32_t const obj_ndx);
};

/**
 * @brief set initial pool_entry values for new/reset entry.
 *
 * Initializes the pool_entry as when newly acquired by a freshly created pool.
 * Chiefly, the name and obj_nlb entries are set while other accounting structures
 * are initialized to their default starting values.
 *
 * @param pool_entry the pool entry itself, should point to an entry in fs->pools.entries
 * @param name name given to the pool
 * @param name_len length of the pool name (should match strlen(name))
 * @param obj_nlb describes object size in the form of number of logical blocks
 * @param slab_nobj describes the number of objects of this pool that fit in a slab
 */
void
fla_pool_entry_reset(struct fla_pool_entry *pool_entry, const char *name, int name_len,
                     uint32_t const obj_nlb, uint32_t const slab_nobj);

void
fla_geo_pool_sgmt_calc(uint32_t npools, uint32_t lb_nbytes,
                       struct fla_geo_pool_sgmt *geo);

struct fla_geo;

void
fla_mkfs_pool_sgmt_init(struct flexalloc *fs, struct fla_geo *geo);

int
fla_pool_init(struct flexalloc *fs, struct fla_geo *geo, uint8_t *pool_sgmt_base);

void
fla_print_pool_entries(struct flexalloc *fs);

int
fla_base_pool_open(struct flexalloc *fs, const char *name, struct fla_pool **handle);

int
fla_pool_release_all_slabs(struct flexalloc *fs, struct fla_pool_entry * pool_entry);

int
fla_base_pool_set_strp(struct flexalloc *fs, struct fla_pool *pool, uint32_t strp_nobjs,
                       uint32_t strp_nbytes);

int
fla_base_pool_create(struct flexalloc *fs, const char *name, int name_len, uint32_t obj_nlb,
                     struct fla_pool **handle);

int
fla_base_pool_destroy(struct flexalloc *fs, struct fla_pool * handle);

int
fla_base_pool_set_root_object(struct flexalloc const * const fs,
                              struct fla_pool const * pool_handle,
                              struct fla_object const *obj, fla_root_object_set_action act);

int
fla_base_pool_get_root_object(struct flexalloc const * const fs,
                              struct fla_pool const * pool_handle,
                              struct fla_object *obj);
#endif // __FLEXALLOC_POOL_H_
