#ifndef FLEXALLOC_SHARED_H_
#define FLEXALLOC_SHARED_H_
#include <stdint.h>
#include <libxnvme.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLA_ERR_ERROR 1001

struct fla_pool;
struct flexalloc;

/// flexalloc open options
///
/// Minimally the dev_uri needs to be set
/// If the md_dev is set than flexalloc md will be stored on this device
/// The xnvme open options are optionally set at open time as well
struct fla_open_opts
{
  const char *dev_uri;
  const char *md_dev_uri;
  struct xnvme_opts opts;
};

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

#ifdef __cplusplus
}
#endif

#endif // FLEXALLOC_SHARED_H_
