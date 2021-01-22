/**
 * flexalloc end-user API.
 *
 * Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 * Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>
 *
 * @file libflexalloc.h
 */
#ifndef __LIBFLEXALLOC_H_
#define __LIBFLEXALLOC_H_
#include <stdlib.h>
#include <stdint.h>
#include "flexalloc_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

struct flexalloc;

/**
 * Open flexalloc system
 * @param dev_uri path to device node, e.g. "/dev/nvme0"
 * @param flexalloc pointer to flexalloc system handle, if successful, otherwise uninitialized.
 *
 * @return On success 0 and *fs pointing to a flexalloc system handle. On error, non-zero
 * and *fs being uninitialized.
 */
int
fla_open(const char *dev_uri, struct flexalloc **fs);

/**
 * Open flexalloc with MD on a separate device
 * @param dev_uri path to device node that does not have MD, e.g. "/dev/nvme0n1"
 * @parm md_dev_uri path to device node where MD will be stored, e.g "/dev/nvme0n2"
 * @param flexalloc pointer to flexalloc system handle, if successful, otherwise uninitialized.
 *
 * @return On success 0 and *fs pointing to a flexalloc system handle. On error, non-zero
 * and *fs being uninitialized.
 */
int
fla_md_open(const char *dev_uri, const char *md_dev_uri, struct flexalloc **fs);

/**
 * Close flexalloc system.
 *
 * Flush meta data changes to disk and close flexalloc system for further access.
 *
 * @param fs flexalloc system handle
 *
 * @return On success 0 with *fs being freed and the device released.
 */
int
fla_close(struct flexalloc *fs);

/**
 * Flush flexalloc system state to disk.
 *
 * Flush meta data changes to disk.
 *
 * @param fs flexalloc system handle
 *
 * @return On success 0.
 */
int
fla_sync(struct flexalloc *fs);

/**
 * Create pool.
 *
 * Creates and allocates a new pool in memory using the given name as its
 * identifier. New pool is not created if it already exists with the same
 * object size. Objects from the pool will span `obj_nlb` logical blocks.
 * Will Not flush changes.
 *
 * NOTE: to acquire the handle for an existing pool, use fla_pool_open()
 *
 * @param fs flexalloc system handle
 * @param name Name of pool
 * @param name_len Length of name string
 * @param obj_nlb Size of objects to allocate from pool, in number of logical blocks.
 * @param pool Pool handle, is initialized if acquisition of pool went well.
 *
 * @return On success, 0 and *handle is initialized. On error, non-zero and *handle
 * is undefined.
 */
int
fla_pool_create(struct flexalloc *fs, const char *name, int name_len, uint32_t obj_nlb,
                struct fla_pool **pool);

/**
 * Destroy pool and its resources.
 *
 * Releases the pool and any slabs, and thereby objects, which it
 * manages from memroy. Will not flush changes.
 *
 * @param fs flexalloc system handle
 * @param pool Pool handle
 *
 * @return On success, 0, the pool has been released. On error, non-zero -
 * the pool has not been removed.
 */

int
fla_pool_destroy(struct flexalloc *fs, struct fla_pool * pool);

/**
 * Allocates a pool handle
 *
 * Open and allocate (malloc) pool handle of existing pool, if it exists.
 * Note, the pool must have been created earlier using fla_pool_create()
 *
 * @param fs flexalloc system handle
 * @param name name of pool to find
 * @param pool pool handle to initialize.
 *
 * @return On success, 0 and handle is initialized. On error, non-zero with
 * handle is undefined.
 */
int
fla_pool_open(struct flexalloc *fs, const char *name, struct fla_pool **pool);

/**
 * Closes the pool handle by freeing its memory.
 *
 * It will not close any of the related slabs or objects
 *
 * @param fs flexalloc system handle
 * @param pool Pool handle to free
 */
void
fla_pool_close(struct flexalloc *fs, struct fla_pool * pool);

/**
 * @brief return a free object related to a pool handle
 *
 * Acquires a slab if the pool is missing one.
 * Will adjust the pool empty, full and partial slab lists accordingly
 *
 * @param fs flexalloc system handle
 * @param pool Pool that will contain object
 * @param object Will point to created object
 * @return 0 on success. non zero otherwise
 */
int
fla_object_create(struct flexalloc * fs, struct fla_pool * pool,
                  struct fla_object * object);

/**
 * @brief Makes sure that object handle is valid
 *
 * Loads slab cache in case it has not been already loaded
 *
 * @param fs flexalloc system handle
 * @param pool Pool that contains object
 * @param object Object to check
 * @return 0 on success. non zero otherwise.
 */
int
fla_object_open(struct flexalloc * fs, struct fla_pool * pool,
                struct fla_object * object);

/**
 * @brief Makes an object available to the pool again
 *
 * Will not release an acquired slab by the pool
 *
 * @param fs flexalloc system handle
 * @param pool Pool containing object
 * @param object Pointer to object to be released
 * @return Zero on success. non zero otherwise
 */
int
fla_object_destroy(struct flexalloc *fs, struct fla_pool * pool,
                   struct fla_object * object);

/**
 * @brief Allocate an aligned (to underlying file system) buffer
 *
 * @param fs flexalloc system handle
 * @param nbytes Number of bytes to allocate
 */
void *
fla_buf_alloc(struct flexalloc const *fs, size_t nbytes);

/**
 * @brief Free allocated memory by fla_buff_alloc
 *
 * @param fs flexalloc system handle
 * @param buf Buffer to free
 */
void
fla_buf_free(struct flexalloc const * fs, void *buf);

/**
 * @brief Read len bytes into buf
 *
 * @param fs flexalloc system handle
 * @param pool Handle to the pool containing the obj
 * @param object Read from this object
 * @param buf Read into this buffer
 * @param offset Number of bytes from beginning of object where the read begins.
 * @param len Number of bytes to read
 * @return Zero on success. non zero otherwise
 */
int
fla_object_read(struct flexalloc const * fs, struct fla_pool const * pool,
                struct fla_object const * object, void * buf, size_t offset, size_t len);

/**
 * @brief Write len bytes from buf
 *
 * @param fs flexalloc system handle
 * @param pool Handle to the pool containing the obj
 * @param object Write from this object
 * @param buf Write from this buffer
 * @param offset Number of bytes from the beginning of the object where the write begins
 * @param len Number of bytes to write
 * @return Zero on success. non zero otherwise
 */
int
fla_object_write(struct flexalloc const * fs, struct fla_pool const * pool,
                 struct fla_object const * object, void const * buf, size_t offset, size_t len);

/**
 * @brief Same as fla_object_write but offset and len can be unaligned values
 *
 * @param fs flexalloc system handle
 * @param pool Handle to the pool containing the obj
 * @param object Write from this object
 * @param buf Write from this buffer
 * @param offset Number of bytes from beginning of object
 * @param len Number of bytes to write
 * @return Zero on success. non zero otherwise
 */
int
fla_object_unaligned_write(struct flexalloc const * fs,
                           struct fla_pool const * pool,
                           struct fla_object const * object, void const * buf, size_t offset,
                           size_t len);

/**
 * @brief Return the number of bytes in a block in fs
 *
 * @param fs flexalloc system handl
 * @return number of bytes in a block
 */
int32_t
fla_fs_lb_nbytes(struct flexalloc const * const fs);

/**
 * @brief Associate object with pool
 *
 * @param fs flexalloc system handle
 * @param pool flexalloc pool handle to associate obj with
 * @param object flexalloc object handle to associate with pool
 * @param force Force pool root to be updated to obj even if it is already set
 * @return Zero on success, non zero otherwise
 */
int
fla_pool_set_root_object(struct flexalloc const * const fs,
                         struct fla_pool const * pool,
                         struct fla_object const *object, fla_root_object_set_action act);

/**
 * @brief Get root object associated with pool
 *
 * @param fs flexalloc system handle
 * @param pool flexalloc pool handle to associate obj with
 * @param object flexalloc object handle associated with pool on success
 * @return Zero on success, non zero otherwise
 */
int
fla_pool_get_root_object(struct flexalloc const * const fs,
                         struct fla_pool const * pool,
                         struct fla_object *object);

#ifdef __cplusplus
}
#endif

#endif // __LIBFLEXALLOC_H_
