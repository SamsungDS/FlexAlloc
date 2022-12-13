#include "flan_md.h"
#include "flexalloc_util.h"
#include "libflexalloc.h"
#include "flexalloc_mm.h"
#include "flexalloc_freelist.h"
#include "flexalloc_hash.h"
#include <stdint.h>

static int
flan_md_root_object_create(struct flexalloc *fs, struct fla_pool *pool_handle,
                           struct flan_md const *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;

  uint64_t pool_obj_nbytes = fla_object_size_nbytes(fs, pool_handle);
  root_obj->buf = malloc(pool_obj_nbytes);
  if (FLA_ERR(!root_obj->buf, "malloc()"))
    return -EIO;

  memset(root_obj->buf, 0, pool_obj_nbytes);

  err = fla_object_create(fs, pool_handle, &root_obj->fla_obj);
  if (FLA_ERR(err, "fla_object_create()"))
    goto free_buf;

  err = fla_pool_set_root_object(fs, pool_handle, &root_obj->fla_obj, false);
  if (FLA_ERR(err, "fla_pool_set_root_object()"))
    goto obj_destroy;

  err = md->init_root_obj_tracking(root_obj, true);
  if (FLA_ERR(err, "init_root_obj_tracking()"))
    goto obj_destroy;

/*  uint32_t num_elem = pool_obj_nbytes / md->elem_nbytes();
  err = fla_flist_new(num_elem, &root_obj->freelist);
  if (FLA_ERR(err, "fla_flist_new()"))
    goto obj_destroy;

  err = htbl_new(num_elem, &root_obj->elem_htbl);
  if (FLA_ERR(err, "htbl_new()"))
    goto flist_destroy;*/

  return 0;

obj_destroy:
  err = fla_object_destroy(fs, pool_handle, &root_obj->fla_obj);
  FLA_ERR(err, "fla_object_destroy()");

free_buf:
  free(root_obj->buf);

  return err;
}

static int
flan_md_root_object_open(struct flexalloc *fs, struct fla_pool *pool_handle,
                         struct flan_md const *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;

  err = fla_pool_get_root_object(fs, pool_handle, &root_obj->fla_obj);
  if (err) // need to create root object
    return flan_md_root_object_create(fs, pool_handle, md, root_obj);

  uint64_t pool_obj_nbytes = fla_object_size_nbytes(fs, pool_handle);
  root_obj->buf = malloc(pool_obj_nbytes);
  if (FLA_ERR(!root_obj->buf, "malloc()"))
    return -EIO;

  err = fla_object_read(fs, pool_handle, &root_obj->fla_obj, root_obj->buf, 0, pool_obj_nbytes);
  if (FLA_ERR(err, "fla_object_read()"))
    goto free_buf;

  err = md->init_root_obj_tracking(root_obj, false);
  if (FLA_ERR(err, "init_root_obj_tracking()"))
    goto free_buf;

  /*uint32_t num_elem = pool_obj_nbytes / md->elem_nbytes();
  err = fla_flist_new(num_elem, &root_obj->freelist);
  if (FLA_ERR(err, "fla_flist_new()"))
    goto free_buf;

  for (uint32_t i = 0 ; i < num_elem ; ++i)
  {

  }*/

  return 0;

free_buf:
    free(root_obj->buf);

    return err;
}

static int
flan_md_init_root(struct flexalloc *fs, struct fla_pool *pool_handle,
                  struct flan_md *md)
{
  int err;

  /* Initialize the first root object */
  struct flan_md_root_obj_buf * first_root_obj = &md->r_objs[0];
  err = flan_md_root_object_open(fs, pool_handle, md, first_root_obj);
  if (FLA_ERR(err, "flan_md_root_object_open()"))
    return err;

  /* Initialize the rest of root objects to 0 */
  for (int i = 1 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    md->r_objs[i].buf = NULL;
    md->r_objs[i].fla_obj.entry_ndx = UINT32_MAX;
    md->r_objs[i].fla_obj.slab_id = UINT32_MAX;
  }

  return err;
}

int flan_md_init(struct flexalloc *fs, struct fla_pool *pool_handle, uint32_t elem_nbytes,
                 struct flan_md **md)
{
  int err = 0;
  *md = malloc(sizeof(struct flan_md));
  if ((err = FLA_ERR(!*md, "malloc()")))
    return err;

  (*md)->elem_nbytes = elem_nbytes;
  err = flan_md_init_root(fs, pool_handle, *md);
  if (FLA_ERR(err, "flan_md_init_root()"))
    goto free_md;

free_md:
  free(*md);

  return err;
}

int flan_md_fini(struct flan_md *md)
{
  free(md);
  return 0;
}
