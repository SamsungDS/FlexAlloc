// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
#include "flexalloc_pool.h"
#include "flexalloc_ll.h"
#include "flexalloc_util.h"
#include "flexalloc_shared.h"

void
fla_geo_pool_sgmt_calc(uint32_t npools, uint32_t lb_nbytes,
                       struct fla_geo_pool_sgmt *geo)
{
  /*
   * Infer the number of blocks used for the various parts of the pool segment
   * provided the number of pools to support and the logical block size.
   *
   * The pool segment is laid out in the following sections:
   * * freelist, 1 bit per pool, packed
   * * htbl - ~8B header and 2 16B entries per pool, packed
   * * entries - 1 ~144B entry per pool, packed
   *
   * Each section is rounded up to a multiple of logical blocks.
   */
  geo->freelist_nlb = FLA_CEIL_DIV(fla_flist_size(npools), lb_nbytes);
  /*
   * Allocate 2 table entries per intended entry
   *
   * Hash table algorithms degrade as the table is filled as the further the
   * hash- and compression functions deviate from the ideal of perfect distribution, the
   * more collisions will be encountered.
   * Over-provisioning by a factor of 3/2-2 is a common practice to alleviate this.
   */
  geo->htbl_tbl_size = npools * 2;
  geo->htbl_nlb = FLA_CEIL_DIV(
                    sizeof(struct fla_pool_htbl_header)
                    + geo->htbl_tbl_size * sizeof(struct fla_htbl_entry), lb_nbytes);
  geo->entries_nlb = FLA_CEIL_DIV(npools * sizeof(struct fla_pool_entry), lb_nbytes);
}

void
fla_mkfs_pool_sgmt_init(struct flexalloc *fs, struct fla_geo *geo)
{
  // initialize freelist.
  fla_flist_init(fs->pools.freelist, geo->npools);

  // initialize hash table header
  // this stores the size of the table and the number of elements
  fs->pools.htbl_hdr_buffer->len = 0;
  fs->pools.htbl_hdr_buffer->size = geo->pool_sgmt.htbl_tbl_size;

  // initialize hash table entries
  fla_htbl_entries_init(fs->pools.htbl.tbl, fs->pools.htbl_hdr_buffer->size);

  // initialize the entries themselves
  memset(fs->pools.entries, 0, (geo->lb_nbytes * geo->pool_sgmt.entries_nlb));
}

static uint64_t
get_slab_elba_default(struct fla_pool_entry const * pool_entry,
                      uint32_t const obj_ndx)
{
  return pool_entry->obj_nlb * (obj_ndx + 1);
}

static uint64_t
get_slab_elba_strp(struct fla_pool_entry const * pool_entry,
                   uint32_t const obj_ndx)
{
  struct fla_pool_strp *strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
  return pool_entry->obj_nlb * (obj_ndx + strp_ops->strp_nobjs);
}

static int
fla_pool_entry_reset_default(struct fla_pool_entry *pool_entry,
                             struct fla_pool_create_arg const *arg,
                             uint32_t const slab_nobj)
{
  memcpy(pool_entry->name, arg->name, arg->name_len);
  pool_entry->obj_nlb = arg->obj_nlb;
  pool_entry->slab_nobj = slab_nobj;
  pool_entry->empty_slabs = FLA_LINKED_LIST_NULL;
  pool_entry->full_slabs = FLA_LINKED_LIST_NULL;
  pool_entry->partial_slabs = FLA_LINKED_LIST_NULL;
  pool_entry->root_obj_hndl = FLA_ROOT_OBJ_NONE;
  return 0;
}

static int
fla_pool_entry_reset_strp(struct fla_pool_entry *pool_entry,
                          struct fla_pool_create_arg const *arg,
                          uint32_t const slab_nobj)
{
  struct fla_pool_strp * strp_ops;

  int err = fla_pool_entry_reset_default(pool_entry, arg, slab_nobj);
  if (FLA_ERR(err, "fla_pool_entry_reset_default()"))
    return err;

  strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
  strp_ops->strp_nobjs = arg->strp_nobjs;
  strp_ops->strp_nbytes = arg->strp_nbytes;
  return 0;
}

uint32_t
fla_pool_num_fla_objs_strp(struct fla_pool_entry const * pool_entry)
{
  struct fla_pool_strp *strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
  return strp_ops->strp_nobjs;
}

uint32_t
fla_pool_num_fla_objs_default(struct fla_pool_entry const * pool_entry)
{
  return 1;
}

static uint64_t
fla_pool_obj_size_nbytes_default(struct flexalloc const *fs, struct fla_pool const *ph)
{
  const struct fla_pool_entry * pool_entry = &fs->pools.entries[ph->ndx];
  return pool_entry->obj_nlb * (uint64_t)fs->geo.lb_nbytes;
}

static uint64_t
fla_pool_obj_size_nbytes_strp(struct flexalloc const *fs, struct fla_pool const *ph)
{
  const struct fla_pool_entry * pool_entry = &fs->pools.entries[ph->ndx];
  struct fla_pool_strp *strp_ops = (struct fla_pool_strp*)&pool_entry->usable;
  return pool_entry->obj_nlb * fs->geo.lb_nbytes * (uint64_t)strp_ops->strp_nobjs;
}


static int
fla_pool_initialize_entrie_func_(struct fla_pools *pools, const uint32_t ndx)
{
  if ((pools->entries + ndx)->flags & FLA_POOL_ENTRY_STRP)
  {
    (pools->entrie_funcs + ndx)->get_slab_elba = get_slab_elba_strp;
    (pools->entrie_funcs + ndx)->fla_pool_entry_reset = fla_pool_entry_reset_strp;
    (pools->entrie_funcs + ndx)->fla_pool_num_fla_objs = fla_pool_num_fla_objs_strp;
    (pools->entrie_funcs + ndx)->fla_pool_obj_size_nbytes = fla_pool_obj_size_nbytes_strp;

  }
  else
  {
    (pools->entrie_funcs + ndx)->get_slab_elba = get_slab_elba_default;
    (pools->entrie_funcs + ndx)->fla_pool_entry_reset = fla_pool_entry_reset_default;
    (pools->entrie_funcs + ndx)->fla_pool_num_fla_objs = fla_pool_num_fla_objs_default;
    (pools->entrie_funcs + ndx)->fla_pool_obj_size_nbytes = fla_pool_obj_size_nbytes_default;
  }

  return 0;
}

int
fla_pool_initialize_entrie_func(const uint32_t ndx, va_list ag)
{
  int err;
  struct fla_pools *pools = va_arg(ag, struct fla_pools*);
  err = fla_pool_initialize_entrie_func_(pools, ndx);
  if (FLA_ERR(err, "fla_pool_initialize_entrie_func()"))
    return FLA_FLIST_SEARCH_RET_ERR;
  return FLA_FLIST_SEARCH_RET_FOUND_CONTINUE;
}

int
fla_pool_init(struct flexalloc *fs, struct fla_geo *geo, uint8_t *pool_sgmt_base)
{
  int ret;
  uint32_t found = 0;
  fs->pools.freelist = (freelist_t)(pool_sgmt_base);
  fs->pools.htbl_hdr_buffer = (struct fla_pool_htbl_header *)
                              (pool_sgmt_base
                               + (geo->lb_nbytes * geo->pool_sgmt.freelist_nlb));

  fs->pools.htbl.len = fs->pools.htbl_hdr_buffer->len;
  fs->pools.htbl.tbl_size = fs->pools.htbl_hdr_buffer->size;

  fs->pools.htbl.tbl = (struct fla_htbl_entry *)(fs->pools.htbl_hdr_buffer + 1);

  fs->pools.entries =(struct fla_pool_entry *)
                     (pool_sgmt_base
                      + (geo->lb_nbytes
                         * (geo->pool_sgmt.freelist_nlb
                            + geo->pool_sgmt.htbl_nlb)));
  fs->pools.entrie_funcs = malloc(sizeof(struct fla_pool_entry_fnc)*geo->npools);
  if (FLA_ERR(fs->pools.entrie_funcs == NULL, "malloc()"))
    return -ENOMEM;

  ret = fla_flist_search_wfunc(fs->pools.freelist, FLA_FLIST_SEARCH_FROM_START,
                               &found, fla_pool_initialize_entrie_func, &fs->pools);

  return ret;
}

void
fla_pool_fini(struct flexalloc *fs)
{
  free(fs->pools.entrie_funcs);
}

void
fla_print_pool_entries(struct flexalloc *fs)
{
  int err = 0;
  struct fla_slab_header * curr_slab;
  uint32_t tmp;
  struct fla_pool_entry * pool_entry;
  uint64_t allocated_nbytes = 0;
  uint32_t slab_heads[3];
  char * slab_head_names[3] = {"Empty Slabs List", "Full Slabs List", "Partial Slabs List"};

  for (uint32_t npool = 0 ; npool < fs->geo.npools ; ++npool)
  {
    pool_entry = &fs->pools.entries[npool];

    slab_heads[0] = pool_entry->empty_slabs;
    slab_heads[1] = pool_entry->full_slabs;
    slab_heads[2] = pool_entry->partial_slabs;
    if(pool_entry->obj_nlb == 0 && pool_entry->slab_nobj == 0)
      continue; //as this pool has not been initialized

    fprintf(stderr, "Pool Entry %"PRIu32"(%p)\n", npool, pool_entry);
    fprintf(stderr, "|  flags: \n");
    fprintf(stderr, "|  `-> Stripped: %d\n", (int)(pool_entry->flags & FLA_POOL_ENTRY_STRP));
    fprintf(stderr, "|  non-stripped obj_nlb : %"PRIu32"\n", pool_entry->obj_nlb);
    fprintf(stderr, "|  root_obj_hndl : \n");
    fprintf(stderr, "|  `-> slab_id : %"PRIu32"\n",
        ((struct fla_object*)(&pool_entry->root_obj_hndl))->slab_id);
    fprintf(stderr, "|  `-> entry_ndx %"PRIu32"\n",
        ((struct fla_object*)(&pool_entry->root_obj_hndl))->entry_ndx);
    fprintf(stderr, "|  PoolName : %s\n", pool_entry->name);
    fprintf(stderr, "|  Max Number of Objects In Slab %"PRIu32"\n", pool_entry->slab_nobj);
    fprintf(stderr, "|  Slabs:\n");
    for(size_t i = 0 ; i < 3 ; ++i)
    {
      fprintf(stderr, "|    Head : %d, offset %ld, name : %s\n", slab_heads[i], i, slab_head_names[i]);
      tmp = slab_heads[i];
      for(uint32_t j = 0 ; j < fs->geo.nslabs && tmp != FLA_LINKED_LIST_NULL; ++j)
      {
        curr_slab = fla_slab_header_ptr(tmp, fs);
        if((err = FLA_ERR(!curr_slab, "fla_slab_header_ptr()")))
          return;

        fprintf(stderr, "|    -->next : %"PRIu32", prev : %"PRIu32", refcount : %"PRIu32", num_obj_since_trim : %"PRIu32"\n",
                curr_slab->next, curr_slab->prev, curr_slab->refcount, curr_slab->nobj_since_trim);
        tmp = curr_slab->next;
        allocated_nbytes += (curr_slab->refcount * (uint64_t)pool_entry->obj_nlb * fs->geo.lb_nbytes);
      }
    }
  }

  fprintf(stderr, "Size Counter\n");
  fprintf(stderr, "|  Allocatable size : %"PRIu64"\n",
      fs->geo.nslabs * (uint64_t)fs->geo.slab_nlb *fs->geo.lb_nbytes);
  fprintf(stderr, "|  Allocated size : %"PRIu64"\n", allocated_nbytes);
}

int
fla_base_pool_open(struct flexalloc *fs, const char *name, struct fla_pool **handle)
{
  struct fla_htbl_entry *htbl_entry;

  htbl_entry = htbl_lookup(&fs->pools.htbl, name);
  if (!htbl_entry) // TODO: Find error code for this valid error
    return -1;

  (*handle) = malloc(sizeof(struct fla_pool));
  if (FLA_ERR(!(*handle), "malloc()"))
    return -ENOMEM;

  (*handle)->h2 = htbl_entry->h2;
  (*handle)->ndx = htbl_entry->val;
  return 0;
}

int
fla_pool_release_all_slabs(struct flexalloc *fs, struct fla_pool_entry * pool_entry)
{
  int err = 0;
  uint32_t * heads[3] = {&pool_entry->empty_slabs, &pool_entry->full_slabs, &pool_entry->partial_slabs};
  for(size_t i = 0 ; i < 3 ; ++i)
  {
    err = fla_hdll_remove_all(fs, heads[i], fla_release_slab);
    if(FLA_ERR(err, "fla_hdll_remove_all()"))
    {
      goto exit;
    }
  }

exit:
  return err;
}

int
fla_base_pool_create(struct flexalloc *fs, struct fla_pool_create_arg const *arg,
                     struct fla_pool **handle)
{
  int err;
  struct fla_pool_entry *pool_entry;
  struct fla_pool_entry_fnc *pool_func;
  int entry_ndx = 0;
  uint32_t slab_nobj;

  // Return pool if it exists
  err = fla_base_pool_open(fs, arg->name, handle);
  if(!err)
  {
    pool_entry = &fs->pools.entries[(*handle)->ndx];
    if(pool_entry->obj_nlb != arg->obj_nlb)
    {
      err = -EINVAL;
      goto free_handle;
    }
    return 0;
  }

  if ((err = FLA_ERR(arg->name_len >= FLA_NAME_SIZE_POOL, "pool name too long")))
    goto exit;

  slab_nobj = fla_calc_objs_in_slab(fs, arg->obj_nlb);
  if((err = FLA_ERR(slab_nobj < 1, "Object size is incompatible with slab size.")))
    goto exit;

  err = fs->fla_cs.fncs.check_pool(fs, arg->obj_nlb);
  if (FLA_ERR(err, "check_pool()"))
    goto exit;

  (*handle) = malloc(sizeof(struct fla_pool));
  if (FLA_ERR(!(*handle), "malloc()"))
  {
    err = -ENOMEM;
    goto exit;
  }

  entry_ndx = fla_flist_entries_alloc(fs->pools.freelist, 1);
  if ((err = FLA_ERR(entry_ndx < 0, "failed to allocate pool entry")))
    goto free_handle;
  pool_entry = &fs->pools.entries[entry_ndx];

  pool_entry->flags = arg->flags;
  err = fla_pool_initialize_entrie_func_(&fs->pools, entry_ndx);
  if (FLA_ERR(err, "fla_pool_initialize_entrie_func_()"))
    goto free_freelist_entry;
  pool_func = (fs->pools.entrie_funcs + entry_ndx);

  err = htbl_insert(&fs->pools.htbl, arg->name, entry_ndx);
  if (FLA_ERR(err, "failed to create pool entry in hash table"))
    goto free_freelist_entry;

  pool_func->fla_pool_entry_reset(pool_entry, arg, slab_nobj);

  (*handle)->ndx = entry_ndx;
  (*handle)->h2 = FLA_HTBL_H2(arg->name);

  return 0;

free_freelist_entry:
  fla_flist_entries_free(fs->pools.freelist, entry_ndx, 1);

free_handle:
  free(*handle);

exit:
  return err;
}

int
fla_base_pool_destroy(struct flexalloc *fs, struct fla_pool * handle)
{
  struct fla_pool_entry *pool_entry;
  struct fla_htbl_entry *htbl_entry;
  int err = 0;
  if ((err = FLA_ERR(handle->ndx > fs->super->npools, "invalid pool id, out of range")))
    goto exit;

  pool_entry = &fs->pools.entries[handle->ndx];
  htbl_entry = htbl_lookup(&fs->pools.htbl, pool_entry->name);
  if ((err = FLA_ERR(!htbl_entry, "failed to find pool entry in hash table")))
    goto exit;

  /*
   * Name given by pool entry pointed to by handle->ndx resolves to a different
   * h2 (secondary hash) value than indicated by handle.
   * This means the handle is stale/invalid - the entry is either unused or used
   * for some other entry.
   */
  if ((err = FLA_ERR(htbl_entry->h2 != handle->h2,
                     "stale/invalid pool handle - resolved to an unused/differently named pool")))
    goto exit;

  if ((err = FLA_ERR(htbl_entry->val != handle->ndx,
                     "stale/invalid pool handle - corresponding hash table entry points elsewhere")))
    goto exit;


  err = fla_pool_release_all_slabs(fs, pool_entry);
  if(FLA_ERR(err, "fla_pool_release_all_slabs()"))
    goto exit;

  err = fla_flist_entries_free(fs->pools.freelist, handle->ndx, 1);
  if (FLA_ERR(err,
              "could not clear pool freelist entry - probably inconsistency in the metadat"))
    /*
     * We would normally undo our actions to abort cleanly.
     * However, this should only happen in case the pool_id given is within
     * the range of pools as specified in the super block, but that the freelist
     * somehow has fewer entries anyway (inconsistency).
     */
    goto exit;

  // remove hash table entry, note the freelist entry is the canonical entry.
  htbl_remove_key(&fs->pools.htbl, pool_entry->name);

  free(handle);

  // TODO: release slabs controlled by pool

exit:
  return err;
}

struct fla_object*
fla_pool_lookup_root_object(struct flexalloc const * const fs,
                            struct fla_pool const *pool_handle)
{
  struct fla_pool_entry *pool_entry;

  pool_entry = &fs->pools.entries[pool_handle->ndx];
  return (struct fla_object *)&pool_entry->root_obj_hndl;
}

int
fla_base_pool_set_root_object(struct flexalloc const * const fs,
                              struct fla_pool const * pool_handle,
                              struct fla_object const *obj, fla_root_object_set_action act)
{
  struct fla_object *pool_root;
  int ret = 0;

  // Lookup pool entry and check if root is already set
  pool_root = fla_pool_lookup_root_object(fs, pool_handle);
  if ((*(uint64_t *)pool_root != FLA_ROOT_OBJ_NONE) && !(act & ROOT_OBJ_SET_FORCE))
  {
    ret = EINVAL;
    FLA_ERR(ret, "Pool has root object set and force is false");
    goto out;
  }

  // Set the pool root to be the object root provided if clear is not set
  if (!(act & ROOT_OBJ_SET_CLEAR))
    *pool_root = *obj;
  else
    *(uint64_t *)pool_root = FLA_ROOT_OBJ_NONE;

out:
  return ret;
}

int
fla_base_pool_get_root_object(struct flexalloc const * const fs,
                              struct fla_pool const * pool_handle,
                              struct fla_object *obj)
{
  struct fla_object *pool_root;
  int ret = 0;

  // Lookup pool entry and check if it is not set
  pool_root = fla_pool_lookup_root_object(fs, pool_handle);
  if (*(uint64_t *)pool_root == FLA_ROOT_OBJ_NONE)
  {
    ret = EINVAL;
    goto out;
  }

  // Set obj to the pool root
  *obj = *pool_root;

out:
  return ret;

}

uint32_t
fla_pool_obj_nlb(struct flexalloc const * const fs, struct fla_pool const *pool_handle)
{
  struct fla_pool_entry * pool_entry;

  pool_entry = &fs->pools.entries[pool_handle->ndx];
  return pool_entry->obj_nlb;
}
