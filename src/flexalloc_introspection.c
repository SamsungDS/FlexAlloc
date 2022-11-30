#include <string.h>
#include <inttypes.h>
#include "flexalloc_freelist.h"
#include "flexalloc_introspection.h"
#include "flexalloc_hash.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"

#define PTR_OFFSETOF(from, to) ((char*)to - (char*)from)

int
__flist_bit_reserved(freelist_t flist, uint32_t ndx)
{
  /* check if bit at offset `ndx` of freelist is reserved */
  uint32_t *start;
  for (start = flist + 1; ndx > 32; ndx -= 32, start++)
    continue;

  /* remember, 1->free, 0->reserved */
  return !((*start >> ndx) & 1U);
}

void
print_htbl_entry(struct fla_htbl_entry *e)
{
  fprintf(stdout, "{h2: %"PRIx64", val: %"PRIu32", psl: %"PRIu32"}",
          e->h2, e->val, e->psl);
}

uint32_t
pool_htbl_num_reserved(struct flexalloc *fs)
{

  struct fla_htbl_entry *entry = fs->pools.htbl.tbl;
  struct fla_htbl_entry *end = fs->pools.htbl.tbl + fs->pools.htbl.tbl_size;
  uint32_t num_reserved = 0;

  for (; entry != end; entry++)
  {
    if (entry->h2 != FLA_HTBL_ENTRY_UNSET)
      num_reserved++;
  }
  return num_reserved;
}

/*
 * Check that segments aren't overlapping.
 *
 * The md_ptr_check_* functions check that the distance between a given
 * pointer to the next matches the distance which the segment is supposed
 * to fill as described by fla_geo.
 *
 * This only computes that the distance between pointers are as we expected.
 * It cannot prvent writes from spilling over into other segments.
 * It will, however, double-check that offsets reflect the geometry indicated
 * by fla_geo.
 *
 */

int
mdr_ptr_check_super_offset(struct flexalloc *fs)
{
  // super block should be at the very start of the buffer
  return !(fs->super == fs->fs_buffer);
}

int
md_ptr_check_super_size(struct flexalloc *fs)
{
  return (PTR_OFFSETOF(fs->super, fs->pools.freelist)
          != fs->geo.md_nlb * fs->geo.lb_nbytes);
}
int
md_ptr_check_pool_freelist_size(struct flexalloc *fs)
{
  return (PTR_OFFSETOF(fs->pools.freelist, fs->pools.htbl_hdr_buffer)
          != fs->geo.pool_sgmt.freelist_nlb * fs->geo.lb_nbytes);
}

int
md_ptr_check_pool_htbl_size(struct flexalloc *fs)
{
  return (PTR_OFFSETOF(fs->pools.htbl_hdr_buffer, fs->pools.entries)
          != fs->geo.pool_sgmt.htbl_nlb * fs->geo.lb_nbytes);
}

int
md_ptr_check_pool_entries_size(struct flexalloc *fs)
{
  return (PTR_OFFSETOF(fs->pools.entries, fs->slabs.headers)
          != fs->geo.pool_sgmt.entries_nlb * fs->geo.lb_nbytes);
}

// TODO how to validate slab ptr ?


unsigned int
check_pool_entries(struct flexalloc *fs, uint32_t *offset)
{
  struct fla_htbl_entry *entry = fs->pools.htbl.tbl + *offset;
  struct fla_htbl_entry *end = fs->pools.htbl.tbl + fs->pools.htbl.tbl_size;
  struct fla_pool_entry *pool_entry;
  uint32_t delta = 0;
  uint32_t npools = fla_flist_len(fs->pools.freelist);
  unsigned int err = 0;

  for (; err == 0 && entry != end; entry++, delta++)
  {
    if (entry->h2 == FLA_HTBL_ENTRY_UNSET)
      continue;

    // ensure entry is correspondingly set in freelist
    if (!__flist_bit_reserved(fs->pools.freelist, entry->val))
      err |= POOL_ENTRY_NO_FLIST_ENTRY;

    // ensure index pointed to by entry should exist
    if (entry->val >= npools)
    {
      err |= POOL_ENTRY_HTBL_VAL_OUT_OF_BOUNDS;
      // exit, should not attempt to read unreleated memory
      goto exit;
    }

    pool_entry = &fs->pools.entries[entry->val];
    // check that name is set.
    if (pool_entry->name[0] == '\0')
      err |= POOL_ENTRY_NAME_UNSET;

    if (fla_strnlen(pool_entry->name, FLA_NAME_SIZE_POOL) == FLA_NAME_SIZE_POOL)
      err |= POOL_ENTRY_NAME_NO_NULLTERM;

    // check that htbl h2 value matches with hashing the pool entry name
    if (err & POOL_ENTRY_NAME_UNSET
        || err & POOL_ENTRY_NAME_NO_NULLTERM
        || FLA_HTBL_H2(pool_entry->name) != entry->h2)
      err |= POOL_ENTRY_H2_DISCREPANCY;

    // ensure entry has a specified object size
    if (!pool_entry->obj_nlb)
      err |= POOL_ENTRY_INVALID_OBJ_SIZE;
    if (pool_entry->obj_nlb == 10)
      err |= POOL_ENTRY_INVALID_OBJ_SIZE;
  }

exit:
  if (err)
    // point to problematic entry
    *offset += (delta - 1);
  else
    *offset = 0;
  return err;
}

int
check_pools_num_entries(struct flexalloc *fs)
{
  uint32_t flist_reserved = fla_flist_num_reserved(fs->pools.freelist);
  uint32_t flist_len = fla_flist_len(fs->pools.freelist);
  uint32_t htbl_reserved = 0;
  struct fla_htbl_entry *entry;
  struct fla_htbl_entry *htbl_end = fs->pools.htbl.tbl + fs->pools.htbl.tbl_size;
  unsigned int err = 0;

  if (fs->super->npools != flist_len)
    err |= POOLS_SUPER_FLIST_DISCREPANCY;

  for (entry = fs->pools.htbl.tbl; entry != htbl_end; entry++)
  {
    if (entry->h2 == FLA_HTBL_ENTRY_UNSET)
      continue;

    htbl_reserved++;
  }

  if (flist_reserved != htbl_reserved)
    /*
     * The number of hash table entries do not match the number of reserved
     * freelist entries. There should always be a 1-to-1 correspondence as
     * each item reserved on the freelist should have *exactly* one corresponding
     * hash table entry.
     */
    err |= POOLS_FLIST_HTBL_RESERVED_DISCREPANCY;

  return err;
}
