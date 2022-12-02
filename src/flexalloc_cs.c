#include "flexalloc_cs.h"
#include "flexalloc_util.h"
#include "flexalloc_cs_cns.h"
#include "flexalloc_cs_zns.h"
#include "flexalloc.h"
#include <stdint.h>

int
fla_cs_type(struct xnvme_dev const *dev, enum fla_cs_t *cs_t)
{
  struct xnvme_geo const *geo = xnvme_dev_get_geo(dev);
  switch (geo->type)
  {
  case XNVME_GEO_ZONED:
    *cs_t = FLA_CS_ZNS;
    return 0;
  case XNVME_GEO_CONVENTIONAL:
    *cs_t = FLA_CS_CNS;
    return 0;
  default:
    FLA_ERR(1, "Unsuported Command Set %d\n", geo->type);
    return 1;
  }
}

int
fla_init_cs(struct flexalloc *fs)
{
  int err = fla_cs_type(fs->dev.dev, &fs->fla_cs.cs_t);
  if (FLA_ERR(err, "fla_cs_type()"))
    return err;

  switch (fs->fla_cs.cs_t)
  {
  case FLA_CS_CNS:
    err = fla_cs_cns_init(fs, 0);
    FLA_ERR(err, "fla_cs_cns_init()");
    break;
  case FLA_CS_ZNS:
    err = fla_cs_zns_init(fs, 0);
    FLA_ERR(err, "fla_cs_zns_init()");
    break;
  default:
    err = 1;
    FLA_ERR(err, "Unsuported Command set %d\n", fs->fla_cs.cs_t);
  }

  return err;
}

int
fla_cs_geo_check(struct xnvme_dev const *dev, struct fla_geo const *geo)
{
  uint64_t nzsect = fla_xne_dev_znd_sect(dev);

  enum xnvme_geo_type geo_type = fla_xne_dev_type(dev);
  if (geo_type == XNVME_GEO_ZONED && geo->slab_nlb % nzsect)
  {
    FLA_ERR_PRINTF("Slab size :%"PRIu32" not multiple of zone sz:%"PRIu64"\n",
                   geo->slab_nlb, nzsect);
    return -1;
  }

  return 0;
}

bool
fla_cs_is_type(struct flexalloc const *fs, enum fla_cs_t const cs_t)
{
  return fs->fla_cs.cs_t == cs_t;
}
