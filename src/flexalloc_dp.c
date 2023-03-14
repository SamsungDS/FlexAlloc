#include "flexalloc.h"
#include "flexalloc_util.h"
#include "flexalloc_dp_noop.h"
#include "flexalloc_util.h"
#include "flexalloc_dp.h"
#include "flexalloc_xnvme_env.h"

int
fla_init_dp(struct flexalloc *fs)
{
  int err = 0;
  err = fla_dp_type(fs, &fs->fla_dp.dp_type);
  if (FLA_ERR(err, "fla_dp_type()"))
    return err;

  switch (fs->fla_dp.dp_type)
  {
  case FLA_DP_FDP:
    err = fla_dp_fdp_init(fs, 0);
    break;
  case FLA_DP_DEFAULT:
    err = fla_dp_noop_init(fs, 0);
    FLA_ERR(err, "fla_dp_noop_init()");

    break;
  case FLA_DP_ZNS:
  default:
    err = 1;
    FLA_ERR(err, "Invalid data placement type.");
  }

  return err;
}

static bool
fla_dp_fdp_supported(struct xnvme_spec_idfy_ctrlr *idfy_ctrl)
{
  return idfy_ctrl->ctratt.flexible_data_placement;
}

//static bool
//fla_dp_fdp_enabled(uint32_t const dw0)
//{
//  return dw0 & (1 << 0);
//}

int
fla_dp_type(struct flexalloc *fs, enum fla_dp_t *dp_t)
{
  int err;
  struct xnvme_spec_idfy idfy_ctrl = {0};
//  const struct xnvme_spec_idfy_ns * idfy_ns = NULL;
//  uint32_t dw0;

  err = fla_xne_ctrl_idfy(fs->dev.dev, &idfy_ctrl);
  if (FLA_ERR(err, "fla_xne_ctrl_idfy()"))
    return err;

  if (fla_dp_fdp_supported(&idfy_ctrl.ctrlr))
  {
//    idfy_ns = xnvme_dev_get_ns(fs->dev.dev);
//    if ((err = FLA_ERR(idfy_ns == NULL, "xnvme_dev_get_ns_css()")))
//      return err;
//    err = fla_xne_feat_idfy(fs->dev.dev, idfy_ns->endgid, &dw0);
//    if (FLA_ERR(err, "fla_xne_feat_idfy()"))
//      return err;
//    if(fla_dp_fdp_enabled(dw0))
//    {
//      *dp_t = FLA_DP_FDP;
//      return 0;
//    }
    *dp_t = FLA_DP_FDP;
    return 0;
  }

  /* If no placement detected its default */
  *dp_t = FLA_DP_DEFAULT;
  return 0;
}
