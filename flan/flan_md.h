#ifndef __LIBFLAN_MD_H
#define __LIBFLAN_MD_H

#include "flexalloc_shared.h"
#define FLAN_MD_MAX_OPEN_ROOT_OBJECTS 2
#define FLAN_MD_ROOT_END_SIGNATURE 0xABCDFDCB
struct flan_md_root_obj_buf_end
{
  struct fla_object fla_next_root;

  /* Write pointer in buffer. Should reflect last written to storage */
  uint64_t write_offset_nbytes;

  /*
   * This struct needs to devide 512 (common small block size)
   * This padding is used to make the struct 256 bytes
   */
  uint32_t padding128;

  /* sizeof struct flan_md_root_obj_buf_end */
  uint32_t end_struct_nbytes;

  /* size if bytes of the flist */
  uint32_t flist_nbytes;

  /*
   * Must be at the end
   * This is used to know where to continue writing when we
   * sync with storage.
   */
  uint32_t signature;
};

struct flan_md_root_obj_buf
{
  void *buf;
  uint64_t buf_nbytes;
  struct fla_object fla_obj;

  /* In mem and storage */
  uint32_t * freelist;

  /* dirty elements */
  uint32_t * dirties;
  struct fla_htbl *elem_htbl;
  enum {
    ACTIVE,
    INACTIVE} state;

  /* In mem and storage */
  struct flan_md_root_obj_buf_end *end;
};

struct flan_md_ndx
{
  uint8_t root_offset;
  uint32_t elem_offset;
};

struct flan_md
{
  struct flexalloc *fs;
  struct fla_pool *ph;
  //pointer to the md in memory
  struct flan_md_root_obj_buf r_objs[FLAN_MD_MAX_OPEN_ROOT_OBJECTS];

  uint32_t (*elem_nbytes)();
  bool (*has_key)(char const *key, void const *ptr);
};

int flan_md_init(struct flexalloc *fs, struct fla_pool *ph, uint32_t(*elem_nbytes)(),
    bool (*has_key)(char const *key, void const *ptr),
    struct flan_md **md);
int flan_md_fini(struct flan_md *md);
int flan_md_find(struct flan_md *md, const char *key, void ** elem);
int flan_md_map_new_elem(struct flan_md *md, const char *key,  void **elem);
int flan_md_umap_elem(struct flan_md *md, const char *key);
int flan_md_mod_dirty(struct flan_md *md, const char *key, void *elem, bool val);
int flan_md_rmap_elem(struct flan_md *md, const char *curr_key, const char *new_key,
    void** rmapped_elem);

#endif //__LIBFLAN_MD_H
