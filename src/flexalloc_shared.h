#ifndef FLEXALLOC_SHARED_H_
#define FLEXALLOC_SHARED_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLA_ERR_ERROR 1001
#define FLA_ERR_ALL_SLABS_USED 2001

struct flexalloc;

struct fla_pool;
struct flexalloc;

/// flexalloc object handle
///
/// The object handle is created from the slab id and the index of the object entry
/// within the slab.
struct fla_object
{
  /// ID of the parent slab (0..nslab-1)
  uint32_t slab_id;
  /// offset of object within the slab
  uint32_t entry_ndx;
};

typedef enum
{
  ROOT_OBJ_SET_DEF = 0,
  ROOT_OBJ_SET_FORCE = 1 << 0,
  ROOT_OBJ_SET_CLEAR = 1 << 1
} fla_root_object_set_action;

/**
 * @brief Return the number of bytes in a block in fs
 *
 * @param fs flexalloc system handl
 * @return number of bytes in a block
 */
int32_t
fla_fs_lb_nbytes(struct flexalloc const * const fs);

struct fla_fns
{
  int (*close)(struct flexalloc *fs);
  int (*sync)(struct flexalloc *fs);
  int (*pool_open)(struct flexalloc *fs, const char *name, struct fla_pool **pool);
  void (*pool_close)(struct flexalloc *fs, struct fla_pool *pool);
  int (*pool_create)(struct flexalloc *fs, const char *name, int name_len, uint32_t obj_nlb,
                     struct fla_pool **pool);
  int (*pool_destroy)(struct flexalloc *fs, struct fla_pool *pool);
  int (*object_open)(struct flexalloc *fs, struct fla_pool *pool, struct fla_object *object);
  int (*object_create)(struct flexalloc *fs, struct fla_pool *pool, struct fla_object *object);
  int (*object_destroy)(struct flexalloc *fs, struct fla_pool *pool, struct fla_object *object);
  int (*pool_set_root_object)(struct flexalloc const * const fs, struct fla_pool const * pool,
                              struct fla_object const *object, fla_root_object_set_action act);
  int (*pool_get_root_object)(struct flexalloc const * const fs, struct fla_pool const * pool,
                              struct fla_object *object);
};

#ifdef __cplusplus
}
#endif

#endif // FLEXALLOC_SHARED_H_
