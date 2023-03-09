#ifndef __FLEXALLOC_FDP_H
#define __FLEXALLOC_FDP_H
#include <stdint.h>
#include "flexalloc_freelist.h"
#include "flexalloc_shared.h"
#include "flexalloc_xnvme_env.h"

struct fla_dp_fdp_slab_list_ids
{
  // slabs that are ready to be used from scratch
  uint32_t ready_slabs;
  //slabs that are ready to be used
  uint32_t filling_slabs;
  // slabs that are not necessarily full but need to empty
  // before being used.
  uint32_t emptying_slabs;
};

enum fla_dp_fdp_t
{
  FLA_DP_FDP_ON_SLAB,
  FLA_DP_FDP_ON_POOL,
  FLA_DP_FDP_ON_OBJECT,
  FLA_DP_FDP_ON_WRITE
};

struct fla_dp_fdp_pid_to_id
{
  uint32_t  pid;
  uint32_t  fla_id;
};

struct fla_dp_fdp
{
  enum fla_dp_fdp_t ctx_set;
  struct fla_dp_fdp_pid_to_id *pids;
  freelist_t free_pids;
  uint32_t md_pid;
};

int fla_dp_fdp_init(struct flexalloc *fs, const uint64_t flags);
int fla_dp_fdp_fini(struct flexalloc *fs);

#endif // __FLEXALLOC_FDP_H
