#ifndef __FLEXALLOC_DP_NOOP_H
#define __FLEXALLOC_DP_NOOP_H

#include "flexalloc.h"
#include "flexalloc_xnvme_env.h"

int
fla_dp_noop_prep_ctx(struct fla_xne_io *xne_io, struct xnvme_cmd_ctx *ctx)
{
  return 0;
}

int
fla_dp_noop_fini(struct flexalloc *fs)
{
  return 0;
}

int
fla_dp_noop_init(struct flexalloc *fs, const uint64_t flags)
{
  fs->fla_dp.fncs.init_dp = fla_dp_noop_init;
  fs->fla_dp.fncs.fini_dp = fla_dp_noop_fini;
  fs->fla_dp.fncs.prep_dp_ctx = fla_dp_noop_prep_ctx;

  return 0;
}
#endif // __FLEXALLOC_DP_NOOP_H
