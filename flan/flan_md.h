#ifndef __LIBFLAN_MD_H
#define __LIBFLAN_MD_H

#include "flexalloc_hash.h"
#include "flexalloc_shared.h"
#include "flexalloc_freelist.h"
#define FLAN_MD_MAX_OPEN_ROOT_OBJECTS 2
struct flan_md_root_obj_buf
{
  void *buf;
  struct fla_object fla_obj;
  freelist_t freelist;
  struct fla_htbl *elem_htbl;
};

struct flan_md_ndx
{
  uint8_t root_offset;
  uint32_t elem_offset;
};

struct flan_md
{
  //pointer to the md in memory
  struct flan_md_root_obj_buf r_objs[FLAN_MD_MAX_OPEN_ROOT_OBJECTS];

  uint32_t (*elem_nbytes)();
  int (*init_root_obj_tracking)(struct flan_md_root_obj_buf *root_obj, bool from_zero);
};

int flan_md_init(struct flexalloc *fs, struct fla_pool *pool_handle, uint32_t elem_nbytes,
                 struct flan_md **md);
int flan_md_fini(struct flan_md *md);
int flan_md_map_obj();
int flan_md_unmap_obj();
int flan_md_find_obj();

#endif //__LIBFLAN_MD_H
