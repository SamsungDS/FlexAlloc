// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "flexalloc_cli_common.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"
#include "flexalloc_freelist.h"
#include "flexalloc_hash.h"
#include "flexalloc_introspection.h"
#include "libflexalloc.h"

static struct cli_option options[] =
{
  {
    .base = {"help", no_argument, NULL, 'h'},
    .description = "display this help",
    .arg_ex = NULL
  },
  {
    .base = {NULL, 0, NULL, 0}
  }
};

void
fla_inspect_help()
{
  fprintf(stdout, "Usage: fla_inspect [options] device\n\n");
  fprintf(stdout, "Inspect flexalloc system state.\n\n");
  print_options(options);
}

// TODO *check* super. can have invalid values too
void
print_super(struct fla_super *super)
{
  fprintf(stdout, "Super Block:\n");
  fprintf(stdout, "magic : ");
  for (unsigned int i = 0; i < 4; i++)
  {
    uint8_t *ch = ((uint8_t *)&super->magic) + i;
    fprintf(stdout, "%#"PRIx8" ", *ch);
  }
  fprintf(stdout, "\n");
  fprintf(stdout, "nslabs: %"PRIu32"\n", super->nslabs);
  fprintf(stdout, "slab_nlb: %"PRIu32"\n", super->slab_nlb);
  fprintf(stdout, "npools: %"PRIu32"\n", super->npools);
  fprintf(stdout, "md_nlb: %"PRIu32"\n", super->md_nlb);
  fprintf(stdout, "fmt_version: %"PRIu8"\n", super->fmt_version);
}

int
validate_super(struct flexalloc *fs)
{
  char *b = (char*)&fs->super->magic;
  int err = 0;

  fprintf(stdout, "== Validating super block...\n");
  if ((err |= fs->super->nslabs == 0))
  {
    fprintf(stdout, "   * number of slabs reported as 0\n");
  }
  if ((err |= fs->super->npools > fs->super->nslabs))
  {
    fprintf(stdout, "   * system reports more pools than slabs\n");
  }
  if ((err |= fs->super->fmt_version == 0))
  {
    fprintf(stdout, "   * flexalloc format version unset\n");
  }

  if (b[0] != '!' || b[1] != 'F' || b[2] != 'S' || b[3] != '\0')
  {
    fprintf(stdout, "   * Invalid magic string: %c%c%c%c", b[0], b[1], b[2], b[3]);
  }

  return err;
}

int
validate_md_ptr_offsets(struct flexalloc *fs)
{
  int err = 0;
  fprintf(stdout, "== Validating meta-data pointer offsets...\n");
  if (mdr_ptr_check_super_offset(fs))
  {
    fprintf(stdout, "   * Super ptr offset does not match start of MD buffer\n");
    err = -1;
  }
  if (md_ptr_check_super_size(fs))
  {
    fprintf(stdout, "   * Super block size error\n");
    err = -1;
  }

  if (md_ptr_check_pool_freelist_size(fs))
  {
    fprintf(stdout, "   * Pool freelist size error\n");
    err = -1;
  }

  if (md_ptr_check_pool_htbl_size(fs))
  {
    fprintf(stdout, "   * Pool hash table size error\n");
    err = -1;
  }

  if (md_ptr_check_pool_entries_size(fs))
  {
    fprintf(stdout, "   * Pool entries size error\n");
    err = -1;
  }
  if (err)
  {
    fprintf(stdout, "\n");
    fprintf(stdout, "   One or more pointer offsets are incorrectly calculated,\n");
    fprintf(stdout, "   leading to one or more metadata elements to be incorrectly sized.\n");
  }
  return err;
}

int
validate_pool_num_entries(struct flexalloc *fs)
{
  int err = 0;
  fprintf(stdout, "== Validating number of pool entries...\n");
  if ((err = check_pools_num_entries(fs)))
  {
    if (err & POOLS_SUPER_FLIST_DISCREPANCY)
    {
      fprintf(stdout, "   * Super block and pool freelist disagree on the number of pools\n");
      fprintf(stdout, "      * Super block: %"PRIu32"\n", fs->super->npools);
      fprintf(stdout, "      * Freelist: %"PRIu32"\n", fla_flist_len(fs->pools.freelist));
    }
    if (err & POOLS_FLIST_HTBL_RESERVED_DISCREPANCY)
    {
      fprintf(stdout, "   * Freelist and hash table disagree on number of active pools\n");
      fprintf(stdout, "      * Freelist: %"PRIu32"\n", fla_flist_num_reserved(fs->pools.freelist));
      fprintf(stdout, "      * Hash table: %"PRIu32"\n", pool_htbl_num_reserved(fs));
    }
  }
  return err;
}

int
validate_pool_entries(struct flexalloc *fs)
{
  uint32_t offset = 0;
  char name_buffer[FLA_NAME_SIZE_POOL];
  int err = 0, fn_err;
  fprintf(stdout, "== Validating pool entries...\n");
  while ((fn_err = check_pool_entries(fs, &offset)))
  {
    struct fla_htbl_entry *entry = &fs->pools.htbl.tbl[offset];
    struct fla_pool_entry *pentry;

    fprintf(stdout, "* invalid pool entry!\n");
    fprintf(stdout, "   hash table index: %"PRIu32"\n", offset);
    fprintf(stdout, "   hash table entry: {h2: %"PRIx64", val: %"PRIu32", psl: %"PRIu16"}\n",
            entry->h2, entry->val, entry->psl);
    if (fn_err & POOL_ENTRY_HTBL_VAL_OUT_OF_BOUNDS)
    {
      fprintf(stdout, "   pool entry: --out of bounds--\n");
    }
    else
    {
      pentry = &fs->pools.entries[entry->val];
      // trust nothing in the invalid entry, copy and explicitly null terminate the name
      memcpy(name_buffer, pentry->name, FLA_NAME_SIZE_POOL);
      name_buffer[FLA_NAME_SIZE_POOL - 1] = '\0';
      fprintf(stdout, "   pool entry: {name: %s, obj_nlb: %"PRIu32", ", name_buffer, pentry->obj_nlb);
      fprintf(stdout, "slab list id 0: %"PRIu32", ", pentry->slab_list_id_0);
      fprintf(stdout, "slab list id 1: %"PRIu32", ", pentry->slab_list_id_1);
      fprintf(stdout, "slab list id 2: %"PRIu32"\n}", pentry->slab_list_id_2);
    }

    fprintf(stdout, "   errors:\n");
    if (fn_err & POOL_ENTRY_NO_FLIST_ENTRY)
      fprintf(stdout, "   * no flist entry\n");
    if (fn_err & POOL_ENTRY_HTBL_VAL_OUT_OF_BOUNDS)
      fprintf(stdout, "   * htbl entry `val` out of bounds\n");
    if (fn_err & POOL_ENTRY_NAME_UNSET)
      fprintf(stdout, "   * entry name is unset\n");
    if (fn_err & POOL_ENTRY_NAME_NO_NULLTERM)
      fprintf(stdout, "   * entry name is not null-terminated\n");
    if (fn_err & POOL_ENTRY_INVALID_OBJ_SIZE)
      fprintf(stdout, "   * invalid object size\n");

    err |= fn_err; // we only need to know there was an error.
    offset += 1; // advance to next entry
  }

  return err;
}

int
main(int argc, char **argv)
{
  int num_opts = sizeof(options)/sizeof(struct cli_option);
  struct option long_options[num_opts];
  int c;
  int opt_idx = 0;
  struct flexalloc *fs;
  char *dev_uri = NULL;
  int err = 0;
  struct fla_open_opts open_opts = {0};

  for (int i = 0; i < num_opts; i++)
  {
    memcpy(long_options+i, &options[i].base, sizeof(struct option));
  }

  while ((c = getopt_long(argc, argv, "h", long_options, &opt_idx)) != -1)
  {
    switch (c)
    {
    case 'h':
      fla_inspect_help();
      return 0;
    default:
      break;
    }
  }

  // expect 1 positional argument - the device
  if (optind == argc)
  {
    fprintf(stderr, "fla_inspect: not enough arguments\n");
    err = -1;
    goto exit;
  }

  open_opts.dev_uri = argv[optind++];

  fprintf(stdout, "opening flexalloc system on %s...\n", dev_uri);
  err = fla_open(&open_opts, &fs);
  if (err)
  {
    fprintf(stderr, "failed to open device '%s'\n", dev_uri);
    err = -1;
    goto exit;
  }

  validate_super(fs);
  validate_md_ptr_offsets(fs);
  validate_pool_num_entries(fs);
  validate_pool_entries(fs);

  fla_close_noflush(fs);

exit:
  exit(err);
}
