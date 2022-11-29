#ifndef __FLEXALLOC_DP_H
#define __FLEXALLOC_DP_H
#include "flexalloc_xnvme_env.h"

struct flexalloc;

enum fla_dp_t
{
  FLA_DP_FDP,
  FLA_DP_ZNS,
  FLA_DP_DEFAULT
};

int fla_dp_type(struct flexalloc *fs, enum fla_dp_t *dp_t);
int fla_init_dp(struct flexalloc *fs);

#endif // __FLEXALLOC_DP_H
