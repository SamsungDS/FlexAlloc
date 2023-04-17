// Copyright (C) 2021 Adam Manzanares <a.manzanares@samsung.com>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <libgen.h>
#include <pthread.h>

#include "flan.h"
#include "flan_md.h"
#include "flexalloc_cs.h"
#include "flexalloc_util.h"
#include "libflexalloc.h"

unsigned int num_objects;
unsigned int flan_obj_sz;
struct flan_handle *root;
static pthread_mutex_t flan_mutex;

struct flan_ohandle flan_otable[FLAN_MAX_OPEN_OBJECTS];
char * flan_pool_names[FLAN_POOL_LAST] =
{ "FLAN_POOL_SHORT", "FLAN_POOL_MEDIUM", "FLAN_POOL_LONG",
  "FLAN_POOL_EXTREME", "FLAN_POOL_NONE"};


void flan_cleanup(void)
{
  flan_close(root);
}

static uint32_t
flan_elem_nbytes()
{
  return sizeof(struct flan_oinfo);
}

static bool
flan_has_key(char const *key, void const *ptr)
{
  struct flan_oinfo const *oinfo = ptr;
  if (!oinfo)
    return 0;

  if (!strncmp(key, oinfo->name, FLAN_OBJ_NAME_LEN_MAX))
    return 1;
  return 0;
}

int flan_init(const char *dev_uri, const char *mddev_uri, struct fla_pool_create_arg *pool_arg,
              uint64_t objsz, struct flan_handle **flanh)
{
  int ret = 0;
  uint32_t obj_nlb;
  struct fla_open_opts open_opts = {0};
  struct xnvme_opts x_opts = xnvme_opts_default();

  pthread_mutex_init(&flan_mutex, NULL);
  *flanh = malloc(sizeof(struct flan_handle));
  memset(*flanh, 0, sizeof(struct flan_handle));
  memset(flan_otable, 0, sizeof(struct flan_ohandle) * FLAN_MAX_OPEN_OBJECTS);

  open_opts.dev_uri = dev_uri;
  open_opts.opts = &x_opts;
  open_opts.opts->async = "io_uring_cmd";
  if (mddev_uri)
    open_opts.md_dev_uri = mddev_uri;

  ret = fla_open(&open_opts, &((*flanh)->fs));
  if (ret)
  {
    printf("Error fla opening dev:%s,md_dev:%s\n", dev_uri, mddev_uri);
    goto out_free;
  }

  (*flanh)->is_zns = fla_cs_is_type((*flanh)->fs, FLA_CS_ZNS);

  if (objsz > FLAN_APPEND_SIZE)
    (*flanh)->append_sz = FLAN_APPEND_SIZE;
  else
    (*flanh)->append_sz = objsz;

  if (objsz % (*flanh)->append_sz > 0)
  {
    objsz = ((objsz / (*flanh)->append_sz) * (*flanh)->append_sz) + (*flanh)->append_sz;
  }

  if ((ret = FLA_ERR(objsz % fla_fs_lb_nbytes((*flanh)->fs) != 0,
        "Flan object size %"PRIu64" not a multiple of lb_nbytes %"PRIu32"",
        objsz, fla_fs_lb_nbytes((*flanh)->fs))))
      goto out_free;

  obj_nlb = objsz / fla_fs_lb_nbytes((*flanh)->fs);

  // Create a pool for all the possible flan pool types
  for (int p_offset = 0; p_offset < FLAN_POOL_LAST; ++p_offset)
  {
    ret = fla_pool_open((*flanh)->fs, flan_pool_names[p_offset], &((*flanh)->p_ptrs[p_offset]));
    if (ret == -1)
    {
      pool_arg->obj_nlb = obj_nlb;
      pool_arg->name = flan_pool_names[p_offset];
      pool_arg->name_len = fla_strnlen(pool_arg->name, FLAN_OBJ_NAME_LEN_MAX);
      ret = fla_pool_create((*flanh)->fs, pool_arg, &((*flanh)->p_ptrs[p_offset]));
    }

    if (ret)
    {
      printf("Error opening fla pool:%s\n", flan_pool_names[p_offset]);
      goto out_free;
    }
  }

  flan_obj_sz = obj_nlb * fla_fs_lb_nbytes((*flanh)->fs);
  if (pool_arg->flags && FLA_POOL_ENTRY_STRP)
    flan_obj_sz *= pool_arg->strp_nobjs;

  ret = flan_md_init((*flanh)->fs, (*flanh)->p_ptrs[FLAN_POOL_NONE],
                       flan_elem_nbytes, flan_has_key, &(*flanh)->md);
  open_opts.opts->sync = "io_uring";
  if (FLA_ERR(ret, "flan_md_init()"))
    goto out_free;

  root = *flanh;
  atexit(flan_cleanup);
  return ret;

out_free:
  free(*flanh);

  return ret;
}

static enum flan_pool_t
flan_get_pool_type_flags(struct flan_handle *flanh, int flags)
{
  for (int p_offset = 0; p_offset < FLAN_POOL_LAST; ++p_offset)
  {
    if (flags & (1<<(3 + p_offset)))
      return p_offset;
  }
  return FLAN_POOL_NONE;
}

static struct fla_pool*
flan_get_pool_handle_oinfo(struct flan_handle *flanh, struct flan_oinfo * oinfo)
{
  return flanh->p_ptrs[oinfo->pool_type];
}

static int
flan_object_create(const char *name, struct flan_handle *flanh, struct flan_oinfo **oinfo, int flags)
{
  int ret;
  unsigned int namelen = strlen(name);
  struct fla_object fla_obj;
  struct fla_pool * ph;

  if (namelen > FLAN_OBJ_NAME_LEN_MAX)
  {
    printf("flan obj create Name:%s longer than max len of:%d\n", name,  FLAN_OBJ_NAME_LEN_MAX);
    return -EINVAL;
  }

  enum flan_pool_t pool_type = flan_get_pool_type_flags(flanh, flags);
  if (FLA_ERR(pool_type < 0, "flan_get_pool_type_flags()"))
    return -EIO;

  ph = flanh->p_ptrs[pool_type];

  ret = fla_object_create(flanh->fs, ph, &fla_obj);
  if (FLA_ERR(ret, "fla_object_create()"))
    return ret;

  ret = flan_md_map_new_elem(flanh->md, name, (void**)oinfo);
  if (FLA_ERR(ret, "flan_md_map_new_elem()"))
    return ret;

  memcpy((*oinfo)->name, name, namelen + 1);
  (*oinfo)->size = 0;
  (*oinfo)->fla_oh[0] = fla_obj;
  (*oinfo)->fla_oh[1].entry_ndx = UINT32_MAX;
  (*oinfo)->fla_oh[1].slab_id = UINT32_MAX;
  (*oinfo)->pool_type = pool_type;

  flanh->is_dirty = true;
  num_objects++;

  return ret;
}

struct flan_oinfo
*flan_find_oinfo(struct flan_handle *flanh, const char *name, uint32_t * cur)
{
  int err;
  struct flan_oinfo *oinfo;

  char * base_name = basename((char *)name);
  err = flan_md_find(flanh->md, base_name, (void**)&oinfo);
  if (FLA_ERR(err, "flan_md_find()"))
    return NULL;

  return oinfo;
}

uint64_t flan_otable_search(const char *name, uint64_t *ff)
{
  bool oh_set = false;
  uint64_t oh_num;

  // Seach through the open objects table
  for (oh_num = 0; oh_num < FLAN_MAX_OPEN_OBJECTS; oh_num++)
  {
    // Blank entry
    if (flan_otable[oh_num].oinfo == NULL)
    {
      if (!oh_set)
      {
        if (ff)
          *ff = oh_num;

        oh_set = true;
      }

      continue;
    }

    // Entry with a matching name return the entry
    if (!strncmp(name, flan_otable[oh_num].oinfo->name, FLAN_OBJ_NAME_LEN_MAX))
      return oh_num;
  }

  if (!oh_set)
    *ff = FLAN_MAX_OPEN_OBJECTS;

  return oh_num;
}

enum flan_mnulti_object_action_t
{
  FLAN_MULTI_OBJ_ACTION_WRITE,
  FLAN_MULTI_OBJ_ACTION_WRITE_UNALIGNED,
  FLAN_MULTI_OBJ_ACTION_READ,
};

static int
flan_multi_object_action(struct flan_handle *flanh,
    struct flan_oinfo *oinfo, void * buf, size_t r_offset, size_t r_len,
    enum flan_mnulti_object_action_t at)
{
  int err;
  struct fla_object *fla_obj_tmp;
  const struct flexalloc * fs = flanh->fs;
  struct fla_pool const * ph = flan_get_pool_handle_oinfo(flanh, oinfo);
  uint64_t obj_nbytes = fla_object_size_nbytes(fs, ph);
  uint64_t r_len_toread = r_len;

  uint64_t loop_r_len = 0, loop_r_offset;
  uint64_t r_len_end_obj;
  void * loop_buf = buf;

  loop_r_offset = r_offset % obj_nbytes;
  for (int obj_offset = r_offset / obj_nbytes; r_len_toread > 0 ;
      r_len_toread -= loop_r_len)
  {
    if ((err = FLA_ERR(obj_offset > 1, "fla_multi_object_action()")))
      goto exit;

    fla_obj_tmp = &oinfo->fla_oh[obj_offset];
    r_len_end_obj = obj_nbytes - loop_r_offset;
    loop_r_len = fla_min(r_len_toread, r_len_end_obj);

    switch (at) {
    case FLAN_MULTI_OBJ_ACTION_WRITE:
      err = fla_object_write((struct flexalloc*)fs, ph, fla_obj_tmp, loop_buf, loop_r_offset, loop_r_len);
      break;
    case FLAN_MULTI_OBJ_ACTION_WRITE_UNALIGNED:
      err = fla_object_unaligned_write((struct flexalloc*)fs, ph, fla_obj_tmp, loop_buf, loop_r_offset, loop_r_len);
      break;
    case FLAN_MULTI_OBJ_ACTION_READ:
      err = fla_object_read(fs, ph, fla_obj_tmp, loop_buf, loop_r_offset, loop_r_len);
      break;
    default:
      err = -EIO;
      break;
    }
    if (FLA_ERR(err, "fla_object_action(), error: %d", err))
      goto exit;
    obj_offset++;
    loop_r_offset = 0;
    loop_buf += loop_r_len;
    if ((err = FLA_ERR(loop_r_len > r_len_toread, "Error in multi object action")))
      goto exit;
  }

exit:
  return err;
}

static void
flan_otable_free_append_buf(struct flan_handle const * flanh, uint64_t oh)
{
  if (flan_otable[oh].append_buf)
  {
    fla_buf_free(flanh->fs, flan_otable[oh].append_buf);
    if (flan_otable[oh].append_buf == flan_otable[oh].read_buf)
      flan_otable[oh].read_buf = NULL;
    flan_otable[oh].append_buf = NULL;
  }
}

int flan_object_open(const char *name, struct flan_handle *flanh, uint64_t *oh, int flags)
{
  struct flan_oinfo *oinfo;
  int ret = 0;
  uint64_t oh_num;
  uint64_t ff_oh = FLAN_MAX_OPEN_OBJECTS;
  uint32_t bs = flanh->append_sz;
  char * base_name = basename((char *)name);

  oh_num = flan_otable_search(base_name, &ff_oh);

  // The object is already open increase ref count and return object handle
  if (oh_num < FLAN_MAX_OPEN_OBJECTS)
  {
    *oh = oh_num;
    flan_otable[oh_num].use_count++;
    return 0;
  }

  if (ff_oh == FLAN_MAX_OPEN_OBJECTS)
  {
    printf("Open objects hit limit of %d\n", FLAN_MAX_OPEN_OBJECTS);
    return -EINVAL;
  }

  // Search through all the on disk MD
  ret = flan_md_find(flanh->md, base_name, (void**)&oinfo);
  if (FLA_ERR(ret, "flan_md_find()"))
    return ret;

  if (!oinfo && (flags & FLAN_OPEN_FLAG_CREATE))
  {
    ret = flan_object_create(base_name, flanh, &oinfo, flags);
    if (ret)
      return ret;
  }

  if (oinfo == NULL)
    return -EINVAL;

  flan_otable[ff_oh].oinfo = oinfo;
  if (flags & FLAN_OPEN_FLAG_WRITE)
    flan_otable[ff_oh].append_off = flan_otable[ff_oh].oinfo->size; // Verify zero on new entry
  else if (flags & FLAN_OPEN_FLAG_READ)
    flan_otable[ff_oh].read_buf_off = UINT64_MAX;

  flan_otable[ff_oh].use_count++;
  flan_otable[ff_oh].append_buf = fla_buf_alloc(flanh->fs, flanh->append_sz);
  if (!flan_otable[ff_oh].append_buf)
  {
    printf("Object open unable to allocate append buf\n");
    return -EINVAL;
  }

  if (flags & FLAN_OPEN_FLAG_READ)
    flan_otable[ff_oh].read_buf = flan_otable[ff_oh].append_buf;

  flan_otable[ff_oh].o_flags = flags;

  memset(flan_otable[ff_oh].append_buf, 0, bs);

  // Read the data into the append buffer
  if (flan_otable[ff_oh].append_off < flan_obj_sz && flags & FLAN_OPEN_FLAG_WRITE)
  {
    ret = flan_multi_object_action(flanh, oinfo, flan_otable[ff_oh].append_buf,
                    (flan_otable[ff_oh].append_off / bs) * bs, bs, FLAN_MULTI_OBJ_ACTION_READ);
    if (FLA_ERR(ret, "flan_multi_object_action()"))
    {

      flan_otable_free_append_buf(flanh, ff_oh);
      return ret;
    }
  }

  // Freeze a zns file that has been previously appended
  if (flan_otable[ff_oh].append_off % flanh->append_sz && flanh->is_zns)
    flan_otable[ff_oh].frozen = true;

  *oh = ff_oh;
  return ret;
}

static int
flan_multi_object_destroy(struct flan_handle * flanh, struct flan_oinfo *oinfo)
{
  int err = 0;
  struct flexalloc * fs = flanh->fs;
  struct fla_pool * ph = flan_get_pool_handle_oinfo(flanh, oinfo);

  for(int i = 0; i < FLAN_MAX_FLA_OBJ_IN_OINFO; ++i)
  {
    if (oinfo->fla_oh[i].entry_ndx != UINT32_MAX && oinfo->fla_oh[i].slab_id != UINT32_MAX)
    {
      err = fla_object_destroy(fs, ph, &oinfo->fla_oh[i]);
      oinfo->fla_oh[i].slab_id = UINT32_MAX;
      oinfo->fla_oh[i].entry_ndx = UINT32_MAX;
    }
  }

  return err;
}

int flan_object_delete(const char *name, struct flan_handle *flanh)
{
  int err;
  char *base_name = basename((char *)name);
  uint64_t oh = flan_otable_search(base_name, NULL);
  struct flan_oinfo *oinfo = NULL;

  if (oh < FLAN_MAX_OPEN_OBJECTS)
    oinfo = flan_otable[oh].oinfo;

  if (!oinfo)
    return -EINVAL;

  // Invalidate any open handles
  if (oh != FLAN_MAX_OPEN_OBJECTS)
  {

    flan_otable_free_append_buf(flanh, oh);
    err = flan_multi_object_destroy(flanh, oinfo);
    if (FLA_ERR(err, "fla_object_destroy()"))
    {
      printf("Error while destroying object in fla %d\n", err);
      return err;
    }
    memset(&flan_otable[oh], 0, sizeof(struct flan_ohandle) - sizeof(uint32_t));
  }

  err = flan_md_umap_elem(flanh->md, base_name);
  if (err == -1 || FLA_ERR(err, "flan_md_umap_elem()"))
    return err;

  memset(oinfo->name, 0, FLAN_OBJ_NAME_LEN_MAX);
  oinfo->size = 0;
  num_objects--;

  flanh->is_dirty = true;

  return 0;
}

void flan_otable_close(struct flan_handle *flanh)
{
  uint64_t oh_num;

  for (oh_num = 0; oh_num < FLAN_MAX_OPEN_OBJECTS; oh_num++)
  {
    if(flan_otable[oh_num].oinfo != NULL)
      flan_object_close(oh_num, flanh);
  }
}

void flan_close(struct flan_handle *flanh)
{
  int err;
  if (!flanh)
    return;

  flan_otable_close(flanh);

  err = flan_md_fini(flanh->md);
  FLA_ERR(err, "flan_md_fini() : Unhandled error on closing md");

  for (int p_offset = 0; p_offset < FLAN_POOL_LAST; ++p_offset)
    fla_pool_close(flanh->fs, flanh->p_ptrs[p_offset]);

  fla_close(flanh->fs);
  free(flanh);
  root = NULL;
}

static int
flan_object_unaligned_read(uint64_t oh,  struct flan_oinfo *oinfo, void *buf,
                           size_t offset, size_t len, struct flan_handle *flanh)
{
  void *al_buf;
  size_t al_offset, al_len, tail_len = 0, tail_start = 0;
  int ret = 0;
  uint32_t bs = flanh->append_sz;

  al_offset = offset - (offset % bs);
  al_len = len + (bs - (len % bs));
  if (al_offset + al_len < offset + len)
    al_len += bs;

  al_buf = flan_buf_alloc(al_len, flanh);
  if (!al_buf)
    return -ENOMEM;

  if (al_offset + al_len > flan_otable[oh].append_off && flanh->is_zns)
  {
    al_len -= bs;
    if (al_len)
      tail_len = (offset + len) - (al_offset + al_len);
    else
      tail_len = len;
  }

  if (al_len)
    ret = flan_multi_object_action(flanh, oinfo, al_buf, al_offset,
        al_len, FLAN_MULTI_OBJ_ACTION_READ);

  if (ret)
  {
    printf("Whoops unaligned object read went wrong\n");
    goto out_free;
  }

  if (al_offset / bs == flan_otable[oh].append_off / bs)
    tail_start = offset % bs;

  if (tail_len)
  {
    if (al_len)
      memcpy(buf, al_buf + (offset % bs), al_len - (offset % bs));

    if (al_len)
      memcpy(buf + al_len - (offset % bs), flan_otable[oh].append_buf + tail_start, tail_len);
    else
      memcpy(buf, flan_otable[oh].append_buf + tail_start, tail_len);
  }
  else
  {
    memcpy(buf, al_buf + (offset % bs), len);
  }

out_free:
  fla_buf_free(flanh->fs, al_buf);
  return ret;
}

static ssize_t
flan_object_read_r(uint64_t oh, void *buf, size_t offset, size_t len,
                   struct flan_handle *flanh, struct flan_oinfo *oinfo)
{
  int err;
  uint64_t *rb_off = &flan_otable[oh].read_buf_off;
  char *rb = flan_otable[oh].read_buf;
  char *bufpos = buf;
  size_t from_buffer = 0, toRead = len;

  if (offset + len > flan_otable[oh].oinfo->size)
  {
    fprintf(stderr, "Huston we have a problem offset %"PRIu64" len %"PRIu64" size %"PRIu64"\n",
        offset, len, flan_otable[oh].oinfo->size);
  }
  // Read in the correct block if the starting address does not fall within the buffer
  if (offset < *rb_off || offset >= (*rb_off + flanh->append_sz))
  {
    *rb_off = (offset / flanh->append_sz) * flanh->append_sz;
    err = flan_multi_object_action(flanh, oinfo, rb, *rb_off,
        flanh->append_sz, FLAN_MULTI_OBJ_ACTION_READ);
    if (err)
      FLA_ERR(err, "flan_object_read_r() uncaught error "
          "offset %"PRIu64" len %"PRIu64" size %"PRIu64" "
          "rb_off %"PRIu64" append_sz %"PRIu64"\n",
          offset, len, flan_otable[oh].oinfo->size, *rb_off, flanh->append_sz);
  }

  // Read completely contained in buffer
  if (offset >= *rb_off && offset + len <= *rb_off + flanh->append_sz)
  {
    memcpy(bufpos, rb + (offset % flanh->append_sz), len);
    return len;
  }

  from_buffer = flanh->append_sz - (offset % flanh->append_sz);
  memcpy(bufpos, rb + offset % flanh->append_sz, from_buffer);

  toRead -= from_buffer;
  bufpos += from_buffer;
  offset += from_buffer;

  // TODO set the rb_off if it hasn't been initialized
  while (toRead > flanh->append_sz)
  {

    *rb_off += flanh->append_sz;
    err = flan_multi_object_action(flanh, oinfo, rb, *rb_off,
        flanh->append_sz, FLAN_MULTI_OBJ_ACTION_READ);
    if (err)
      FLA_ERR(err, "flan_object_read_r() uncaught error");
    memcpy(bufpos, rb, flanh->append_sz);
    toRead -= flanh->append_sz;
    bufpos += flanh->append_sz;
    offset += flanh->append_sz;
  }

  err = flan_multi_object_action(flanh, oinfo, rb, *rb_off + flanh->append_sz,
      flanh->append_sz, FLAN_MULTI_OBJ_ACTION_READ);
  if (err)
    FLA_ERR(err, "flan_object_read_r() uncaught error");
  *rb_off += flanh->append_sz;
  memcpy(bufpos, rb + offset % flanh->append_sz, toRead);

  return len;
}

static ssize_t
flan_object_read_rw(uint64_t oh, void *buf, size_t offset, size_t len,
                    struct flan_handle *flanh, struct flan_oinfo *oinfo)
{
  uint32_t bs = flanh->append_sz;
  int ret;

  // Cant read past end of file
  if (offset + len > oinfo->size)
  {
    len = oinfo->size - offset;
  }

  // Aligned start and end go straight to the underlying storage if buffer is aligned
  if (!(offset % bs) && !(len % bs))
  {
    if (!((uintptr_t)buf % bs))
    {
      ret =  flan_multi_object_action(flanh, oinfo, buf, offset,
          len, FLAN_MULTI_OBJ_ACTION_READ);
      if (ret)
        return ret;

      return len;
    }
  }

  ret = flan_object_unaligned_read(oh, oinfo, buf, offset, len, flanh);
  if (ret)
    return ret;

  return len;
}

ssize_t flan_object_read(uint64_t oh, void *buf, size_t offset, size_t len,
                           struct flan_handle *flanh)
{
  struct flan_oinfo *oinfo = flan_otable[oh].oinfo;

  // Cant read past end of file
  if (offset + len > oinfo->size)
  {
    if(offset > oinfo->size)
      fprintf(stderr, "we have a problem\n");

    len = oinfo->size - offset;
  }

  if (flan_otable[oh].o_flags & FLAN_OPEN_FLAG_WRITE)
    return flan_object_read_rw(oh, buf, offset, len, flanh, oinfo);

  return flan_object_read_r(oh, buf, offset, len, flanh, oinfo);
}

int flan_conv_object_write(struct flan_oinfo *oinfo, void *buf, size_t offset,
    size_t len, struct flan_handle *flanh)
{
  int ret;

  if (len % flan_dev_bs(flanh) || offset % flan_dev_bs(flanh))
    ret = flan_multi_object_action(flanh, oinfo, buf, offset,
        len, FLAN_MULTI_OBJ_ACTION_WRITE_UNALIGNED);
  else
    ret = flan_multi_object_action(flanh, oinfo, buf, offset,
        len, FLAN_MULTI_OBJ_ACTION_WRITE);

  return ret;
}

// This is currently only going to support append, force this later
int flan_zns_object_write(struct flan_oinfo *oinfo, void *buf, size_t offset,
    size_t len, uint64_t oh, struct flan_handle *flanh)
{
  size_t al_start = 0, al_end = 0, al_len = 0, tail_len = 0;
  int ret = 0;
  char *bufpos = (char *)buf;
  char *al_buf;
  uint32_t bs = flanh->append_sz;

  // Unaligned start offset
  if (offset % bs)
    al_start = offset + (bs - (offset % bs));
  else
    al_start = offset;

  // Unaligned tail
  if ((offset + len) % bs)
    al_end = ((offset + len) / bs) * bs;
  else
    al_end = offset + len;

  al_len = al_end - al_start;
  tail_len = len - ((al_start - offset) + al_len);

  // We fit into the buffer so just fill buffer up
  if (offset + len < al_start)
  {
    memcpy(flan_otable[oh].append_buf + (offset % bs), buf, len);
    flan_otable[oh].append_off += len;
    return ret;
  }

  size_t buf_offset = (offset / bs) * bs;
  // Going to cross block boundary, copy up to the block boundary
  if (al_start != offset)
  {
    memcpy(flan_otable[oh].append_buf + (offset % bs), buf, al_start - offset);
    ret = flan_multi_object_action(flanh, oinfo, flan_otable[oh].append_buf,
        buf_offset, bs, FLAN_MULTI_OBJ_ACTION_WRITE);
    if (FLA_ERR(ret, "Error writing append buffer, error: %d\n", ret))
        return ret;
  }
  // Write out all of the aligned data
  bufpos += al_start - offset;
  if (al_len) {
    al_buf = flan_buf_alloc(al_len, flanh);
    memcpy(al_buf, bufpos, al_len);
    ret = flan_multi_object_action(flanh, oinfo, al_buf, al_start,
        al_len, FLAN_MULTI_OBJ_ACTION_WRITE);
    free(al_buf);
  }
  if(FLA_ERR(ret, "ZNS write of the aligned portion of append data fails, error: %d\n", ret))
    return ret;

  bufpos += al_len;
  // Copy the unaligned tail to the buffer
  memcpy(flan_otable[oh].append_buf, bufpos, tail_len);
  flan_otable[oh].append_off += len;

  return ret;
}

static uint32_t
flan_object_oinfo_num_active_objs(struct flan_oinfo *oinfo)
{
  uint32_t count = 0;

  for (int i = 0 ; i < FLAN_MAX_FLA_OBJ_IN_OINFO ; ++i)
  {
    if (oinfo->fla_oh[i].entry_ndx == UINT32_MAX ||
        oinfo->fla_oh[i].slab_id == UINT32_MAX)
      break;
    count++;
  }

  return count;
}

int
flan_object_write(uint64_t oh, void *buf, size_t offset, size_t len,
                        struct flan_handle *flanh)
{
  struct flan_oinfo *oinfo = flan_otable[oh].oinfo;
  struct fla_pool * ph = flan_get_pool_handle_oinfo(flanh, oinfo);
  uint32_t num_active = flan_object_oinfo_num_active_objs(oinfo);
  int ret = 0;

  if (offset + len
      > fla_object_size_nbytes(flanh->fs, ph) * num_active)
  {
    if ((ret = FLA_ERR(num_active == FLAN_MAX_FLA_OBJ_IN_OINFO,
                       "flan_object_write_(): exceeded FLAN_MAX_FLA_OBJ_IN_OINFO")))
      return ret;
    ret = fla_object_create(flanh->fs, ph, &oinfo->fla_oh[num_active]);
    if (FLA_ERR(ret, "fla_object_create()"))
      return ret;
  }

  if (flanh->is_zns)
    ret = flan_zns_object_write(oinfo, buf, offset, len, oh, flanh);
  else
    ret = flan_conv_object_write(oinfo, buf, offset, len, flanh);

  if(FLA_ERR(ret, "flan_object_write fla object write fails : %d\n", ret))
    return -EIO;

  if (offset + len > oinfo->size)
    oinfo->size = offset + len;

  return ret;
}

void *flan_buf_alloc(size_t nbytes, struct flan_handle *flanh)
{
  return fla_buf_alloc(flanh->fs, nbytes);
}

uint32_t flan_dev_bs(struct flan_handle *flanh)
{
  return fla_fs_lb_nbytes(flanh->fs);
}

/* You can only rename open objects */
int flan_object_rename(const char *oldname, const char *newname, struct flan_handle *flanh)
{
  int err;
  struct flan_oinfo *oinfo;
  char * base_newname = basename((char*)newname);
  char * base_oldname = basename((char*)oldname);

  unsigned int namelen = strlen(base_newname);
  if (namelen > FLAN_OBJ_NAME_LEN_MAX)
    return FLA_ERR(-1, "flan_object_rename()");

  uint64_t old_oh = flan_otable_search(base_oldname, NULL);
  if (FLA_ERR(old_oh == FLAN_MAX_OPEN_OBJECTS, "flan_otable_search()"))
    return -EIO;

  uint32_t old_new_oh_usecount = 0;
  uint64_t new_oh = flan_otable_search(base_newname, NULL);
  if (new_oh != FLAN_MAX_OPEN_OBJECTS)
  { // we forget about new existing new_on except for the count
    old_new_oh_usecount = flan_otable[new_oh].use_count;
    flan_otable_free_append_buf(flanh, new_oh);
    memset(&flan_otable[new_oh], 0, sizeof(struct flan_ohandle));
  }

  err = flan_md_rmap_elem(flanh->md, base_oldname, base_newname, (void**)&oinfo);
  if (FLA_ERR(err, "flan_md_rmap_elem()"))
    return -EIO;

  if (FLA_ERR(oinfo != flan_otable[old_oh].oinfo, "flan_object_rename() : oinfo mismatch"))
    return -EIO;

  if (oinfo->fla_oh[0].entry_ndx == UINT32_MAX && oinfo->fla_oh[0].slab_id == UINT32_MAX
      && oinfo->fla_oh[1].entry_ndx == UINT32_MAX && oinfo->fla_oh[1].slab_id == UINT32_MAX)
    return FLA_ERR(-EIO, "flan object without a fla object");

  memset(oinfo->name, 0, FLAN_OBJ_NAME_LEN_MAX);
  memcpy(oinfo->name, base_newname, namelen + 1);

  flan_otable[old_oh].use_count += old_new_oh_usecount;

  return 0;
}

static int
flan_multi_object_seal(struct flan_handle * flanh,
                struct flan_oinfo *oinfo)
{
  int err;
  struct flexalloc * fs = flanh->fs;
  struct fla_pool * ph = flan_get_pool_handle_oinfo(flanh, oinfo);

  for(int i = 0; i < FLAN_MAX_FLA_OBJ_IN_OINFO; ++i)
  {
    if (oinfo->fla_oh[i].entry_ndx != UINT32_MAX && oinfo->fla_oh[i].slab_id != UINT32_MAX)
    {
      err = fla_object_seal(fs, ph, &oinfo->fla_oh[i]);
      //oinfo->fla_oh[i].slab_id = UINT32_MAX;
      //oinfo->fla_oh[i].entry_ndx = UINT32_MAX;
    }
  }

  return err;

}

// TODO assumes this won't fail, revisit me
int flan_object_close(uint64_t oh, struct flan_handle *flanh)
{
  size_t append_off = flan_otable[oh].append_off;
  uint32_t bs = flanh->append_sz;
  int ret = 0;

  /* File is no longer open */
  if (flan_otable[oh].oinfo == NULL)
    return 0;

  flan_otable[oh].use_count--;
  // Use count drops to zero lets free
  if (flan_otable[oh].use_count < 1)
  {
    if (append_off % bs && !flan_otable[oh].frozen && flanh->is_zns)
    {
      // Append the last block
      ret = flan_multi_object_action(flanh, flan_otable[oh].oinfo, flan_otable[oh].append_buf,
                             (append_off / bs) * bs, bs, FLAN_MULTI_OBJ_ACTION_WRITE);
      if (FLA_ERR(ret, "Error writing last block, corruption abound\n"))
        return ret;
    }

    // Seal the object
    if (append_off)
      flan_multi_object_seal(flanh, flan_otable[oh].oinfo);

    fla_buf_free(flanh->fs, flan_otable[oh].append_buf);
    memset(&flan_otable[oh], 0, sizeof(struct flan_ohandle));
  }

  return 0;
}

int flan_sync(struct flan_handle *flanh)
{
  return fla_sync(flanh->fs);
}

bool flan_is_zns(struct flan_handle *flanh)
{
  return flanh->is_zns;
}
