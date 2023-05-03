#include "flan_md.h"
#include "flan.h"
#include "flexalloc_util.h"
#include "libflexalloc.h"
#include "flexalloc_mm.h"
#include "flexalloc_freelist.h"
#include "flexalloc_hash.h"
#include <stdint.h>

static uint32_t
flan_md_calc_end_struct_nbytes()
{
  return sizeof(struct flan_md_root_obj_buf_end);
}

static uint32_t
flan_md_calc_end_obj_size_nbytes(uint32_t num_elems)
{
  return flan_md_calc_end_struct_nbytes() + fla_flist_size(num_elems);
}

static uint32_t
flan_md_calc_end_obj_size_nbytes_v(uint32_t nelems, va_list ap)
{
  return flan_md_calc_end_obj_size_nbytes(nelems);
}

static int
flan_md_get_usable_root_nelems(struct flan_md const *md, uint32_t *nelems)
{
  uint64_t pool_obj_nbytes = fla_object_size_nbytes(md->fs, md->ph);

  uint32_t md_elems_in_fla_obj = fla_nelems_max(pool_obj_nbytes, md->elem_nbytes(),
      flan_md_calc_end_obj_size_nbytes_v);

  if (pool_obj_nbytes < flan_md_calc_end_obj_size_nbytes(md_elems_in_fla_obj))
  {
    FLA_ERR(true, "flan_md_get_usable_root_nelems()");
    return -1;
  }

  *nelems = md_elems_in_fla_obj;

  return 0;
}

static int
flan_md_get_root_end(struct flan_md const *md, struct flan_md_root_obj_buf *root_obj,
    struct flan_md_root_obj_buf_end **root_end)
{
  int err = 0;
  if (root_obj->buf_nbytes < sizeof(struct flan_md_root_obj_buf_end))
  {
    FLA_ERR(true, "flan_md_get_root_end()");
    return -1;
  }

  *root_end = root_obj->buf + root_obj->buf_nbytes - sizeof(struct flan_md_root_obj_buf_end);

  return err;
}

static int
flan_md_reset_root_end(struct flan_md const *md, struct flan_md_root_obj_buf_end *root_end,
    uint32_t num_elems)
{
  if (!root_end)
    return -1;
  root_end->fla_next_root.entry_ndx = 0xFFFFFFFF;
  root_end->fla_next_root.slab_id = 0xFFFFFFFF;
  root_end->end_struct_nbytes = flan_md_calc_end_struct_nbytes();
  root_end->write_offset_nbytes = 0;
  root_end->padding128 = 0;
  root_end->flist_nbytes = fla_flist_size(num_elems);
  root_end->signature = FLAN_MD_ROOT_END_SIGNATURE;

  return 0;
}

static int
flan_md_root_object_create(struct flan_md const *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;
  uint32_t num_elems = 0;
  void *buf_end;

  root_obj->buf_nbytes = fla_object_size_nbytes(md->fs, md->ph);

  root_obj->buf = fla_buf_alloc(md->fs, root_obj->buf_nbytes);
  if (FLA_ERR(!root_obj->buf, "fla_buf_alloc()"))
    return -EIO;
  memset(root_obj->buf, 0, root_obj->buf_nbytes);

  err = fla_object_create(md->fs, md->ph, &root_obj->fla_obj);
  if (FLA_ERR(err, "fla_object_create()"))
    goto free_buf;

//  // This will zero out the root object
//  err = fla_object_write(md->fs, md->ph, &root_obj->fla_obj, root_obj->buf, 0, root_obj->buf_nbytes);
//  if (FLA_ERR(err, "fla_object_write()"))
//    goto obj_destroy;

  err = fla_pool_set_root_object(md->fs, md->ph, &root_obj->fla_obj, false);
  if (FLA_ERR(err, "fla_pool_set_root_object()"))
    goto obj_destroy;

  err = flan_md_get_usable_root_nelems(md, &num_elems);
  if(FLA_ERR(err, "flan_md_get_usable_root_nelems()"))
    goto obj_destroy;

  err = flan_md_get_root_end(md, root_obj, &root_obj->end);
  if (FLA_ERR(err, "flan_md_get_root_end()"))
    goto obj_destroy;

  err = flan_md_reset_root_end(md, root_obj->end, num_elems);
  if (FLA_ERR(err, "flan_md_reset_root_end()"))
    goto obj_destroy;

  buf_end = (char*)root_obj->buf + root_obj->buf_nbytes;
  uint32_t end_sz_nbytes = flan_md_calc_end_obj_size_nbytes(num_elems);
  root_obj->freelist = buf_end - end_sz_nbytes;
  fla_flist_init(root_obj->freelist, num_elems);

  err = htbl_new(num_elems, &root_obj->elem_htbl);
  if (FLA_ERR(err, "htbl_new()"))
    goto obj_destroy;

  err = fla_flist_new(num_elems, &root_obj->dirties);
  if (FLA_ERR(err, "fla_flist_new()"))
    goto htbl_destroy;

  return 0;

htbl_destroy:
  htbl_free(root_obj->elem_htbl);

obj_destroy:
  err = fla_object_destroy(md->fs, md->ph, &root_obj->fla_obj);
  FLA_ERR(err, "fla_object_destroy()");

free_buf:
  fla_buf_free(md->fs, root_obj->buf);

  return err;
}

static int
flan_md_print_root_obj_elem(const uint32_t ndx, va_list ag)
{
  void * root_buf = va_arg(ag, void*);
  struct flan_oinfo *tmp;

  tmp = (((struct flan_oinfo*)root_buf) + ndx);
  fprintf(stderr, "`-> Object (%s)\n", tmp->name);
  fprintf(stderr, "    Size : (%"PRIu64")\n", tmp->size);
  fprintf(stderr, "    ndx : (%"PRIu32")\n" , ndx);

  return FLA_FLIST_SEARCH_RET_FOUND_CONTINUE;
}

static void
flan_md_print_root_obj(struct flan_md_root_obj_buf const * root_obj)
{
  fprintf(stderr, "Root Object\n");
  fprintf(stderr, "`-> buf ptr : %p\n", root_obj->buf);
  fprintf(stderr, "`-> buf_nbytes : %"PRIu64"\n", root_obj->buf_nbytes);
  fprintf(stderr, "`-> FlexAlloc Object:\n");
  fprintf(stderr, "   `-> entry_ndx: %"PRIu32"\n", root_obj->fla_obj.entry_ndx);
  fprintf(stderr, "   `-> slab_id: %"PRIu32"\n", root_obj->fla_obj.slab_id);
  fprintf(stderr, "`-> freelist:\n");
  fprintf(stderr, "   `-> ptr: %p\n", root_obj->freelist);
  if(root_obj->freelist) {
    fprintf(stderr, "   `-> len: %"PRIu32"\n", fla_flist_len(root_obj->freelist));
    fprintf(stderr, "   `-> reserved: %"PRIu32"\n", fla_flist_num_reserved(root_obj->freelist));
  }
  fprintf(stderr, "`-> dirties:\n");
  fprintf(stderr, "   `-> ptr: %p\n", root_obj->dirties);
  if(root_obj->dirties) {
    fprintf(stderr, "   `-> len: %"PRIu32"\n", fla_flist_len(root_obj->dirties));
    fprintf(stderr, "   `-> reserved: %"PRIu32"\n", fla_flist_num_reserved(root_obj->dirties));
  }
  fprintf(stderr, "`-> htbl ptr %p\n", root_obj->elem_htbl);
  fprintf(stderr, "`-> state %d\n", root_obj->state);
  fprintf(stderr, "`-> end:\n");
  fprintf(stderr, "   `-> end ptr : %p\n", root_obj->end);
  if (root_obj->end) {
    fprintf(stderr, "   `-> fla_next_root:\n");
    fprintf(stderr, "      `-> entry_ndx: %"PRIu32"\n", root_obj->end->fla_next_root.entry_ndx);
    fprintf(stderr, "      `-> slab_id: %"PRIu32"\n", root_obj->end->fla_next_root.slab_id);
    fprintf(stderr, "   `-> write_offset_nbytes: %"PRIu64"\n", root_obj->end->write_offset_nbytes);
    fprintf(stderr, "   `-> padding128 : %"PRIu32"\n", root_obj->end->padding128);
    fprintf(stderr, "   `-> end_struct_nbytes : %"PRIu32"\n", root_obj->end->end_struct_nbytes);
    fprintf(stderr, "   `-> flist_nbytes : %"PRIu32"\n", root_obj->end->flist_nbytes);
    fprintf(stderr, "   `-> signature : %x\n", root_obj->end->signature);
  }

  uint32_t found = 0;
  fprintf(stderr, "Elements in Root\n");
  int err = fla_flist_search_wfunc(root_obj->freelist, FLA_FLIST_SEARCH_FROM_START, &found,
      flan_md_print_root_obj_elem, root_obj->buf);
  if (err)
    fprintf(stderr, "Found an error (%d) during freelist search\n", err);

 
}

void
flan_md_print_md(struct flan_md const * md)
{
  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    struct flan_md_root_obj_buf const *root_obj = &md->r_objs[i];
    if (root_obj->state == INACTIVE)
      continue;

    flan_md_print_root_obj(root_obj);
  }
}

//only called from flan_md_write_root_obj
static int
flan_md_forward_dirty_elements(const uint32_t src_ndx, va_list ag)
{
  int err = 0, isalloc;
  struct flan_md_root_obj_buf * root_obj = va_arg(ag, struct flan_md_root_obj_buf*);
  struct flan_md * md = va_arg(ag, struct flan_md*);

  uint64_t dest_ndx = root_obj->end->write_offset_nbytes / md->elem_nbytes();
  if (root_obj->end->write_offset_nbytes % md->elem_nbytes() != 0)
    dest_ndx++; // make sure we start after the original write_offset_nbytes

  void *src = root_obj->buf + (md->elem_nbytes() * src_ndx);
  void *dest = root_obj->buf + (dest_ndx * md->elem_nbytes());

  // We are lucky as we don't have to copy.
  if (src == dest)
    goto set_write_offset_remove_from_dirties;

  isalloc = fla_flist_entry_isalloc(root_obj->freelist, dest_ndx);
  if (FLA_ERR(isalloc < 0 || isalloc > 1, "fla_flist_entry_isalloc()"))
    return FLA_FLIST_SEARCH_RET_ERR;

  // find the next free ndx after dest_ndx
  if(isalloc)
  {
    for (dest_ndx+=1; dest_ndx < fla_flist_len(root_obj->freelist); ++ dest_ndx)
    {
      isalloc = fla_flist_entry_isalloc(root_obj->freelist, dest_ndx);
      if (FLA_ERR(isalloc < 0 || isalloc > 1, "fla_flist_entry_isalloc()"))
        return FLA_FLIST_SEARCH_RET_ERR;
      if (!isalloc)
      {
        dest = root_obj->buf + (dest_ndx * md->elem_nbytes());
        break;
      }
    }
  }

  err = fla_flist_entry_unfree(root_obj->freelist, dest_ndx);
  if (FLA_ERR(err, "fla_flist_entry_unfree()"))
    return FLA_FLIST_SEARCH_RET_ERR;

  memcpy(dest, src, md->elem_nbytes());

  err = fla_flist_entry_free(root_obj->freelist, src_ndx);
  if (FLA_ERR(err, "fla_flist_entry_free()"))
    return FLA_FLIST_SEARCH_RET_ERR;

set_write_offset_remove_from_dirties:
  if ((dest_ndx + 1) * md->elem_nbytes() > root_obj->end->write_offset_nbytes)
    root_obj->end->write_offset_nbytes = (dest_ndx + 1) * md->elem_nbytes();

  err = fla_flist_entry_free(root_obj->dirties, src_ndx);
  if (FLA_ERR(err, "fla_flist_entry_free()"))
    return FLA_FLIST_SEARCH_RET_ERR;

  return FLA_FLIST_SEARCH_RET_FOUND_CONTINUE;
}

// This can only be called when closing flan. It will not update the hashes
// It will leave the fs in an undefined state
static int
flan_md_write_root_obj(struct flan_md *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;
  uint32_t forwarded;
  uint64_t src_write_nbytes = root_obj->end->write_offset_nbytes;
  uint32_t num_elems = 0, end_obj_nbytes;

  if (fla_flist_num_reserved(root_obj->dirties) <= 0)
    return 0; /* no need to update a clean cache */

  err = flan_md_get_usable_root_nelems(md, &num_elems);
  if(FLA_ERR(err, "flan_md_get_usable_root_nelems()"))
    return err;

  err = fla_flist_search_wfunc(root_obj->dirties, FLA_FLIST_SEARCH_FROM_START, &forwarded,
      flan_md_forward_dirty_elements, root_obj, md);
  if (FLA_ERR(err, "fla_flist_search_wfunc()"))
    return err;

  /*
   * We need to find the start offset of the end struct in such a way that
   * it ends alinged to an lba.
   */
  end_obj_nbytes = flan_md_calc_end_obj_size_nbytes(num_elems);
  uint64_t dest_write_nbytes = FLA_CEIL_DIV(
      root_obj->end->write_offset_nbytes + end_obj_nbytes,
      md->fs->geo.lb_nbytes) * md->fs->geo.lb_nbytes;

  uint64_t src_write_len_nbytes = dest_write_nbytes - src_write_nbytes;
  void *src_end_ptr = root_obj->buf + root_obj->buf_nbytes - end_obj_nbytes;
  void *dst_end_ptr = root_obj->buf + dest_write_nbytes - end_obj_nbytes;
  root_obj->end->write_offset_nbytes = src_write_nbytes + src_write_len_nbytes;
  memcpy(dst_end_ptr, src_end_ptr, end_obj_nbytes);

  err = fla_object_write(md->fs, md->ph, &root_obj->fla_obj, root_obj->buf + src_write_nbytes,
      src_write_nbytes, src_write_len_nbytes);
  if (FLA_ERR(err, "fla_object_write() src_nbytes : %"PRIu64", len %"PRIu64"",
        src_write_nbytes, dest_write_nbytes))
    return err;

  return err;
}

static int
flan_md_read_root_obj(struct flan_md const *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;
  struct flan_md_root_obj_buf_end *end;
  uint64_t end_struct_nbytes = flan_md_calc_end_struct_nbytes();

  err = fla_object_read(md->fs, md->ph, &root_obj->fla_obj, root_obj->buf, 0, root_obj->buf_nbytes);
  if (FLA_ERR(err, "fla_object_read()"))
    return err;

  /*
   * search for the end of the md end struct from the end of the buffer
   * assume that we can search sizeof(struct flan_md_root_obj_buf_end) bytes at a time
   */
  void* buf_end = (char*)root_obj->buf + root_obj->buf_nbytes;
  for (end = (buf_end - end_struct_nbytes) ; (void*)end > root_obj->buf; --end)
    if (end->signature == FLAN_MD_ROOT_END_SIGNATURE)
      break;

  if ((err = FLA_ERR((void*)end <= root_obj->buf, "flan_md_read_root_obj()")))
    return err;

  if ((err = FLA_ERR(end_struct_nbytes != end->end_struct_nbytes, "flan_md_read_root_obj()")))
    return err;

  uint32_t end_sz_nbytes = end_struct_nbytes + end->flist_nbytes;
  memcpy(buf_end - end_sz_nbytes, (void*)end - end->flist_nbytes, end_sz_nbytes);

  root_obj->end = (buf_end - end_struct_nbytes);
  root_obj->freelist = fla_flist_load(buf_end - end_sz_nbytes);

  return 0;
}

static int
flan_md_root_object_open(struct flan_md const *md, struct flan_md_root_obj_buf *root_obj)
{
  int err;

  err = fla_pool_get_root_object(md->fs, md->ph, &root_obj->fla_obj);
  if (err) // need to create root object
    return flan_md_root_object_create(md, root_obj);

  root_obj->buf_nbytes = fla_object_size_nbytes(md->fs, md->ph);
  root_obj->buf = fla_buf_alloc(md->fs, root_obj->buf_nbytes);
  if (FLA_ERR(!root_obj->buf, "fla_buf_alloc()"))
    return -EIO;
  memset(root_obj->buf, 0, root_obj->buf_nbytes);

  err = flan_md_read_root_obj(md, root_obj);
  if (FLA_ERR(err, "flan_md_read_root_obj()"))
    goto free_buf;

  uint32_t num_elems = fla_flist_len(root_obj->freelist);
  err = htbl_new(num_elems, &root_obj->elem_htbl);
  if (FLA_ERR(err, "htbl_new()"))
    goto free_buf;

  err = fla_flist_new(num_elems, &root_obj->dirties);
  if (FLA_ERR(err, "fla_flist_new()"))
    goto free_htbl;

  return 0;

free_htbl:
  htbl_free(root_obj->elem_htbl);

free_buf:
  fla_buf_free(md->fs, root_obj->buf);

  return err;
}


static void
flan_md_reset_root_obj(struct flan_md_root_obj_buf * root_obj)
{
  root_obj->buf = NULL;
  root_obj->buf_nbytes = 0;
  root_obj->fla_obj.entry_ndx = UINT32_MAX;
  root_obj->fla_obj.slab_id = UINT32_MAX;
  root_obj->state = INACTIVE;
  root_obj->freelist = NULL;
  root_obj->dirties = NULL;
  root_obj->end = NULL;
}

static int
flan_md_init_root(struct flan_md *md)
{
  int err;

  /* Initialize the first root object */
  struct flan_md_root_obj_buf * first_root_obj = &md->r_objs[0];
  err = flan_md_root_object_open(md, first_root_obj);
  if (FLA_ERR(err, "flan_md_root_object_open()"))
    return err;
  first_root_obj->state = ACTIVE;

  /* Initialize the rest of root objects to 0 */
  for (int i = 1 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
    flan_md_reset_root_obj(&md->r_objs[i]);

  return err;
}

int flan_md_init(struct flexalloc *fs, struct fla_pool *ph,
    uint32_t(*elem_nbytes)(),
    bool (*has_key)(char const *key, void const *ptr),
    struct flan_md **md)
{
  int err = 0;
  *md = fla_buf_alloc(fs, sizeof(struct flan_md));
  if ((err = FLA_ERR(!*md, "malloc()")))
    return err;
  (*md)->fs = fs;
  (*md)->ph = ph;
  (*md)->elem_nbytes = elem_nbytes;
  (*md)->has_key = has_key;

  err = flan_md_init_root(*md);
  if (FLA_ERR(err, "flan_md_init_root()"))
    goto free_md;

  return 0;

free_md:
  fla_buf_free(fs, *md);

  return err;
}

int flan_md_fini(struct flan_md *md)
{
  struct flexalloc * fs = md->fs;
  int err = 0;
  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    struct flan_md_root_obj_buf *root_obj = &md->r_objs[i];
    if (root_obj->state == INACTIVE)
      continue;

    err |= flan_md_write_root_obj(md, root_obj);
    if(FLA_ERR(err, "flan_md_write()"))
      continue;

    htbl_free(root_obj->elem_htbl);
    fla_flist_free(root_obj->dirties);

    memset(root_obj->buf, 0, root_obj->buf_nbytes);
    memset(root_obj->end, 0, sizeof(struct flan_md_root_obj_buf_end));
    fla_buf_free(fs, root_obj->buf);
  }

  if (FLA_ERR(err, "flan_md_fini()"))
    return err;

  memset(md, 0, sizeof(struct flan_md));
  fla_buf_free(fs, md);
  return 0;
}

static int
flan_md_get_usable_root_ndx(struct flan_md *md, struct flan_md_root_obj_buf **root_buf, int * root_ndx)
{
  int err, free_ndx;
  struct flan_md_root_obj_buf *root_obj;
  struct flan_md_root_obj_buf_end *prev_root_obj_end = NULL;
  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS; ++ i)
  {
    root_obj = &md->r_objs[i];

    if (root_obj->state == INACTIVE)
    {
      /* There should be at least one ACTIVE root at this point*/
      if (FLA_ERR(i < 1, "flan_md_get_usable_root()"))
        return -1;

      err = flan_md_get_root_end(md, root_obj, &prev_root_obj_end);
      if (FLA_ERR(err, "flan_md_get_root_end()"))
        return err;;

      err = flan_md_root_object_create(md, root_obj);
      if (FLA_ERR(err, "flan_md_root_object_create()"))
        return err;

      prev_root_obj_end->fla_next_root = root_obj->fla_obj;
    }

    free_ndx = fla_flist_entries_alloc(root_obj->freelist, 1);
    if (free_ndx == -1)
      continue;
    break;
  }

  if (free_ndx == -1)
  {
    *root_buf = NULL;
    return -EIO;
  }

  *root_buf = root_obj;
  *root_ndx = free_ndx;
  return 0;
}

int flan_md_map_new_elem(struct flan_md *md, const char *key,  void **elem)
{
  int err;
  struct flan_md_root_obj_buf *root_obj = NULL;
  int root_ndx = -1;
  err =  flan_md_get_usable_root_ndx(md, &root_obj, &root_ndx);
  if (FLA_ERR(err, "flan_md_get_usable_root_ndx()"))
    return err;

  err = htbl_insert(root_obj->elem_htbl, key, root_ndx);
  if (FLA_ERR(err, "htbl_insert()"))
    return err;

  err = fla_flist_entry_unfree(root_obj->dirties, root_ndx);
  if (FLA_ERR(err, "fla_flist_entry_unfree()"))
    return err;

  *elem = root_obj->buf + (md->elem_nbytes() * root_ndx);

  return 0;
}

static int
flan_md_umap_(struct flan_md_root_obj_buf * root_obj,
    struct fla_htbl_entry *htbl_entry, uint32_t const ndx)
{
  int err;
  err = fla_flist_entry_free(root_obj->freelist, ndx);
  if (FLA_ERR(err, "fla_flist_entry_free()"))
    return err;

  err = fla_flist_entry_free(root_obj->dirties, ndx);
  if (FLA_ERR(err, "fla_flist_entry_free()"))
    return err;

  htbl_remove_entry(root_obj->elem_htbl, htbl_entry);
  return 0;
}

int flan_md_umap_elem(struct flan_md *md, const char *key)
{
  int err;
  struct flan_md_root_obj_buf * root_obj;

  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    root_obj = &md->r_objs[i];
    if (root_obj->state == INACTIVE)
      continue;

    struct fla_htbl *htbl = root_obj->elem_htbl;
    struct fla_htbl_entry *htbl_entry = htbl_lookup(htbl, key);
    if (!htbl_entry)
      continue;

    err = flan_md_umap_(root_obj, htbl_entry, htbl_entry->val);
    if (FLA_ERR(err, "fla_md_umap_elem()"))
      return err;

    return 0;
  }

  return -1;
}

int flan_md_mod_dirty(struct flan_md *md, const char *key, void *elem, bool val)
{
  struct flan_md_root_obj_buf * root_obj;
  uint32_t ndx;
  int err = 0;
  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    root_obj = &md->r_objs[i];
    if (root_obj->state == INACTIVE)
      continue;

    if(elem < root_obj->buf || elem > root_obj->buf + root_obj->buf_nbytes)
      continue;

    ndx = elem - root_obj->buf;

    if (val)
    {
      err = fla_flist_entry_unfree(root_obj->dirties, ndx);
      if (FLA_ERR(err, "fla_flist_entry_unfree()"))
        return err;
      goto out;
    } else {
      err = fla_flist_entry_free(root_obj->dirties, ndx);
      if (FLA_ERR(err, "fla_flist_entry_free()"))
        return err;
      goto out;
    }
  }

  err = FLA_ERR(-EIO, "flan_md_mod_dirty(): elem not found");

out:
  return err;
}

static int
flan_md_add_elem_from_freelist_to_hash(const uint32_t ndx, va_list ag)
{
  int err;
  struct flan_md_root_obj_buf * root_obj = va_arg(ag, struct flan_md_root_obj_buf*);
  struct flan_md * md = va_arg(ag, struct flan_md*);
  const char *key = va_arg(ag, char*);
  uint32_t *found_ndx = va_arg(ag, uint32_t*);
  if (md->has_key(key, root_obj->buf + (ndx * md->elem_nbytes())))
  {
    *found_ndx = ndx;
    err = htbl_insert(root_obj->elem_htbl, key, ndx);
    if (FLA_ERR(err, "htbl_insert()"))
      return FLA_FLIST_SEARCH_RET_ERR;
    return FLA_FLIST_SEARCH_RET_FOUND_STOP;
  }
  return FLA_FLIST_SEARCH_RET_CONTINUE;
}

static int
flan_md_find_elem_hash(struct flan_md *md, const char *key,
    struct flan_md_root_obj_buf **root_obj,
    struct fla_htbl_entry **htbl_entry,
    uint32_t *ndx, void **elem)
{
  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    (*root_obj) = &md->r_objs[i];
    if ((*root_obj)->state == INACTIVE)
      continue;

    struct fla_htbl *htbl = (*root_obj)->elem_htbl;
    *htbl_entry = htbl_lookup(htbl, key);
    if (!(*htbl_entry))
      continue;

    *ndx = (*htbl_entry)->val;
    goto exit;
  }

  *elem = NULL;
  *root_obj = NULL;
  *htbl_entry = NULL;
  *ndx = UINT32_MAX;
  return 0;

exit:
  *elem = (*root_obj)->buf + (md->elem_nbytes() * (*ndx));
  return 0;
}

static int
flan_md_find_elem_freelist(struct flan_md *md, const char *key,
    struct flan_md_root_obj_buf **root_obj,
    struct fla_htbl_entry **htbl_entry,
    uint32_t *ndx, void **elem)
{
  int err;
  uint32_t found = 0;

  for (uint32_t i = 0 ; i < FLAN_MD_MAX_OPEN_ROOT_OBJECTS ; ++i)
  {
    (*root_obj) = &md->r_objs[i];
    if ((*root_obj)->state == INACTIVE)
      continue;

    found = 0;
    err = fla_flist_search_wfunc((*root_obj)->freelist, FLA_FLIST_SEARCH_FROM_START,
        &found, flan_md_add_elem_from_freelist_to_hash, (*root_obj), md, key, ndx);
    if (FLA_ERR(err, "fla_flist_search_wfunc()"))
      return err;
    if (!found)
      continue;

    goto exit;
  }

  *elem = NULL;
  *root_obj = NULL;
  *htbl_entry = NULL;
  *ndx = UINT32_MAX;
  return 0;

exit:
  *elem = (*root_obj)->buf + (md->elem_nbytes() * (*ndx));
  *htbl_entry = htbl_lookup((*root_obj)->elem_htbl, key);

  if (FLA_ERR(!(*htbl_entry), "htbl_lookup()"))
    return -EIO;

  return 0;
}

static int
flan_md_find_(struct flan_md *md, const char *key,
    struct flan_md_root_obj_buf **root_obj,
    struct fla_htbl_entry **htbl_entry,
    uint32_t *ndx, void **elem)
{
  int err;
  err = flan_md_find_elem_hash(md, key, root_obj, htbl_entry, ndx, elem);
  if (FLA_ERR(err, "flan_md_find_elem_hash()"))
    return err;

  if (*elem)
    return 0;

  /*
   * Its not on any of the active hash tables. We are forced to do
   * a "thorough" search on the free lists and add it to the hash
   * if we find it.
   */
  err = flan_md_find_elem_freelist(md, key, root_obj, htbl_entry, ndx, elem);
  if (FLA_ERR(err, "flan_md_find_elem_freelist()"))
    return err;

  return 0;
}

int flan_md_rmap_elem(struct flan_md *md, const char *curr_key, const char *new_key,
    void** rmapped_elem)
{
  int err;
  uint32_t curr_ndx, new_ndx;
  struct fla_htbl_entry *curr_htbl_entry, *new_htbl_entry;
  struct flan_md_root_obj_buf *curr_root_obj, *new_root_obj;
  void *curr_elem = NULL, *new_elem = NULL;

  err = flan_md_find_(md, new_key, &new_root_obj, &new_htbl_entry, &new_ndx, &new_elem);
  if (FLA_ERR(err, "flan_md_find_()"))
    return err;

  if(new_elem) {
    err = flan_md_umap_(new_root_obj, new_htbl_entry, new_ndx);
    if (FLA_ERR(err, "flan_md_umap_()"))
      return err;

    memset(new_elem, 0, md->elem_nbytes());
  }

  err = flan_md_find_(md, curr_key, &curr_root_obj, &curr_htbl_entry, &curr_ndx, &curr_elem);
  if (FLA_ERR(err, "flan_md_find_()"))
    return err;

  /* Hash of new_key needs to point to new offset */
  htbl_remove_entry(curr_root_obj->elem_htbl, curr_htbl_entry);
  err = htbl_insert(curr_root_obj->elem_htbl, new_key, curr_ndx);
  if (FLA_ERR(err, "htbl_insert()"))
    return err;

  *rmapped_elem = curr_elem;

  return 0;
}

int flan_md_find(struct flan_md *md, const char *key, void **elem)
{
  int err;
  uint32_t found_ndx;
  struct fla_htbl_entry *htbl_entry;
  struct flan_md_root_obj_buf *root_obj;

  err = flan_md_find_(md, key, &root_obj, &htbl_entry, &found_ndx, elem);
  if (FLA_ERR(err, "flan_md_find_()"))
    return err;

  if (*elem)
    return 0;

  *elem = NULL;
  return 0;
}

