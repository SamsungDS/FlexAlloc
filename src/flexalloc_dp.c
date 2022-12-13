#include "flexalloc_dp.h"
#include "flexalloc.h"
#include "flexalloc_dp_noop.h"
#include "flexalloc_util.h"

int
fla_init_dp(struct flexalloc *fs)
{
  int err = 0;
  /*
   * Here we put logic to choose between ZNS, CNS and FDP. And
   * anythine else the world comes up with
   */
  err = fla_dp_type(fs, &fs->fla_dp.dp_type);
  switch (fs->fla_dp.dp_type)
  {
  case FLA_DP_FDP:
    //err = fla_fdp_init(fs, 0);
    break;
  case FLA_DP_ZNS:
  case FLA_DP_DEFAULT:
    err = fla_dp_noop_init(fs, 0);
    FLA_ERR(err, "fla_dp_noop_init()");

    break;
  default:
    err = 1;
    FLA_ERR(err, "Invalid data placement type.");
  }

  return err;

}

int
fla_dp_type(struct flexalloc *fs, enum fla_dp_t *dp_t)
{
  *dp_t = FLA_DP_DEFAULT;
  return 0;
}
