/**
 * flexalloc disk structures.
 *
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 *
 * @file flexalloc_introspection.h
 */
#ifndef __FLEXALLOC_INTROSPECTION_H_
#define __FLEXALLOC_INTROSPECTION_H_
#include <inttypes.h>
#include "flexalloc_mm.h"

#define POOLS_SUPER_FLIST_DISCREPANCY 1U
#define POOLS_FLIST_HTBL_RESERVED_DISCREPANCY (1U << 1U)

#define POOL_ENTRY_NO_FLIST_ENTRY 1U
#define POOL_ENTRY_HTBL_VAL_OUT_OF_BOUNDS (1U << 1U)
#define POOL_ENTRY_NAME_UNSET (1U << 2U)
#define POOL_ENTRY_NAME_NO_NULLTERM (1U << 3U)
#define POOL_ENTRY_H2_DISCREPANCY (1U << 4U)
#define POOL_ENTRY_INVALID_OBJ_SIZE (1U << 5U)

uint32_t
pool_htbl_num_reserved(struct flexalloc *fs);

unsigned int
check_pool_entries(struct flexalloc *fs, uint32_t *offset);

int
check_pools_num_entries(struct flexalloc *fs);

int
mdr_ptr_check_super_offset(struct flexalloc *fs);

int
md_ptr_check_super_size(struct flexalloc *fs);

int
md_ptr_check_pool_freelist_size(struct flexalloc *fs);

int
md_ptr_check_pool_htbl_size(struct flexalloc *fs);

int
md_ptr_check_pool_entries_size(struct flexalloc *fs);
#endif // __FLEXALLOC_INTROSPECTION_H_
