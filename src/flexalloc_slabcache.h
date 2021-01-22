/**
 * flexalloc Slab Freelist Cache
 *
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 *
 * Slab freelist cache
 *
 * Each slab must maintain its own object allocation freelist. The exact size
 * needed for the freelist depends on the number of objects that can fit into the
 * slab, which in turn is determined by the pool object size.
 * For this reason, the per-slab object freelists cannot be determined ahead of
 * time, nor laid out contiguously on disk without over-provisioning space.
 * Hence each slab maintains its own freelist, subtracting from the total space
 * available in the slab.
 *
 * As with other metadata, these freelists may require frequent checks and
 * changes as objects are allocated or freed. To limit disk I/O overhead, the
 * freelists will be cached in memory and flushed to disk as appropriate.
 *
 * @file flexalloc_slabcache.h
 */
#ifndef __FLEXALLOC_SLABCACHE_H_
#define __FLEXALLOC_SLABCACHE_H_
#include "flexalloc.h"

/// State of slab freelist cache element
enum fla_slab_flist_elem_state
{
  /// A stale entry is one which is either not initialized or whose entry has
  /// been explicitly invalidated as part of the slab being released from the
  /// pool.
  FLA_SLAB_CACHE_ELEM_STALE = 0,
  /// An entry is dirty if it contains changes which have not been written
  /// back to the disk.
  FLA_SLAB_CACHE_ELEM_DIRTY = 1,
  /// An entry is clean if its contents are the same as on disk
  FLA_SLAB_CACHE_ELEM_CLEAN = 2,
};

struct fla_slab_flist_cache_elem
{
  /// Pointer to a IO-buffer containing the slab freelist, if initialized.
  freelist_t freelist;
  /// Track the cache entry state. Will initially be stale until the entry
  /// is either initialized from scratch when a pool acquires the slab or
  /// from being loaded from disk.
  enum fla_slab_flist_elem_state state;
};

#define FLA_SLAB_CACHE_INVALID_STATE 5001

/**
 * Determine number of logical blocks needed for a freelist with flist_len entries.
 *
 * Determine number of logical blocks needed for a freelist of the specified size.
 * Do not attempt to determine this number otherwise.
 *
 * @param fs flexalloc system handle
 * @param flist_len number of entries to have in freelist
 *
 * @return number of logical blocks required to contain the freelist
 */
size_t
fla_slab_cache_flist_nlb(struct flexalloc *fs, uint32_t flist_len);

/**
 * Initialize new slab freelist cache instance.
 *
 * Initializes the cache and its elements. Note that the cache is tied to a
 * particular flexalloc system instance due to the particulars of allocating IO-buffers
 * for holding the freelists.
 *
 * @param fs flexalloc system handle
 * @param cache an uninitialized cache struct
 * @return On success 0 and cache being initialized. On error, non-zero and
 * cache is in an undefined state.
 */
int
fla_slab_cache_init(struct flexalloc *fs, struct fla_slab_flist_cache *cache);

/**
 * Free slab freelist cache memory.
 *
 * Frees all memory associated the slab freelist and any IO-buffers.
 *
 * NOTE: does *not* flush IO-buffers, any changes to the slab freelists not yet
 * committed to disk will be lost!
 *
 * @param cache slab freelist cache.
 */
void
fla_slab_cache_free(struct fla_slab_flist_cache *cache);

/**
 * Initialize cache entry.
 *
 * Initialize cache entry for the slab from scratch with an empty freelist whose
 * size is inferred from the size of the objects (obj_nlb) to be served from the
 * slab.
 *
 * NOTE: incurs no disk I/O, the element may be explicitly flushed to disk using
 * fla_slab_cache_elem_flush() if desired.
 *
 * @param cache slab freelist cache
 * @param slab_id id of the slab whose freelist to initialize
 * @param flist_len intended length of freelist
 *
 * @return On success 0 with the cache entry initialized and marked dirty.
 * Non-zero otherwise with the cache entry unchanged.
 * NOTE: FLA_SLAB_CACHE_INVALID_STATE is returned if the element is already
 * initialized. Elements must be explicitly dropped, using
 * fla_slab_cache_elem_drop() before being (re-)initialized.
 * The element should be dropped when the slab is released from a pool back to
 * the system.
 */
int
fla_slab_cache_elem_init(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         uint32_t flist_len);

/**
 * Load cache entry freelist from disk.
 *
 * Load existing slab freelist from disk. Pool management code should call this
 * when loading in a slab which already exists on disk.
 *
 * @param cache slab freelist cache
 * @param slab_id id of the slab whose contents to load
 * @param flist_len number of entries in freelist
 *
 * @return On success 0 and the cache entry is populated, non-zero otherwise.
 * NOTE: FLA_SLAB_CACHE_INVALID_STATE is returned if the element is already
 * initialized. Elements must be explicitly dropped, using
 * fla_slab_cache_elem_drop() before being (re-)initialized.
 * The element should be dropped when the slab is released from a pool back to
 * the system.
 */
int
fla_slab_cache_elem_load(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         uint32_t flist_len);

/**
 * Flush cache entry to disk.
 *
 * Flush the slab freelist to disk and mark the cache entry as clean. This can be
 * used explicitly to ensure object (de-)allocations are durable.
 *
 * @param cache slab freelist cache
 * @param slab_id id of slab whose freelist should be persisted
 *
 * @return On success 0 with the cache element marked as clean. On error, non-zero
 * with the cache element state unchanged.
 */
int
fla_slab_cache_elem_flush(struct fla_slab_flist_cache *cache, uint32_t slab_id);

/**
 * Mark cache entry data as invalid
 *
 * Marks cache entry data as invalid, freeing the associated freelist IO buffer, if any.
 * This should be called by the pool when releasing a given slab.
 *
 * @param cache slab freelist cache
 * @param slab_id id of the slab whose cache entry to invalidate/drop
 */
void
fla_slab_cache_elem_drop(struct fla_slab_flist_cache *cache, uint32_t slab_id);

/**
 * Allocate an object from the slab.
 *
 * Allocates an object from the slab by finding and reserving an entry from
 * the freelist.
 *
 * @param cache slab freelist cache
 * @param slab_id id of the slab to reserve an entry from
 * @param obj_id pointer to a object id struct - to be populated if operation is successful
 *
 * @return On success 0, with obj_id set to uniquely identify the reserved object.
 * Non-zero return values indicate an error. FLA_SLAB_CACHE_INVALID_STATE means
 * the operation failed because the cache entry is not initialized.
 *
 */
int
fla_slab_cache_obj_alloc(struct fla_slab_flist_cache *cache, uint32_t slab_id,
                         struct fla_object *obj_id);

/**
 * Release object entry reservation.
 *
 * Call to release a reserved object by marking the freelist entry as available.
 * This should be done when an object is discarded.
 *
 * @param cache slab freelist cache
 * @param obj_id object id, uniquely identifying the object and its parent slab
 *
 * @return On success 0, otherwise an error occurred and the entry was not freed.
 */
int
fla_slab_cache_obj_free(struct fla_slab_flist_cache *cache,
                        struct fla_object * obj_id);

/**
 * Flush all dirty cache entries to disk.
 *
 * @param cache slab freelist cache
 * @return On success 0, On error, the number of dirty cache entries which could
 * not be flushed to disk.
 */
int
fla_slab_cache_flush(struct fla_slab_flist_cache *cache);
#endif // __FLEXALLOC_SLABCACHE_H_
