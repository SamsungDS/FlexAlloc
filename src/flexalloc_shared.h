#ifndef FLEXALLOC_SHARED_H_
#define FLEXALLOC_SHARED_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLA_ERR_ERROR 1001

struct fla_pool;

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

#ifdef __cplusplus
}
#endif

#endif // FLEXALLOC_SHARED_H_
