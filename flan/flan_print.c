#include <string.h>
#include <stdio.h>
#include "flan.h"
#include "libflexalloc.h"
#include "libxnvme.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"

#define POOL_NAME "TEST"

int main(int argc, char **argv)
{
  char *dev = NULL, *md_dev = NULL;
  struct flan_handle *flanh;
  unsigned int obj_sz = 187392 * 4096;
  int ret;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: flexalloc_print DEV [MD_DEV]");
    return 1;
  }

  dev = argv[1];
  if (argc > 2)
    md_dev = argv[2];

  struct fla_pool_create_arg pool_arg =
  {
    .flags = 0,
    .name = POOL_NAME,
    .name_len = strlen(POOL_NAME),
    .obj_nlb = 0, // will get set by flan_init
    .strp_nobjs = 0,
    .strp_nbytes = 0
  };

  if (md_dev)
    ret = flan_init(dev, md_dev, &pool_arg, obj_sz, &flanh);
  else
    ret = flan_init(dev, NULL, &pool_arg, obj_sz, &flanh);
  if (FLA_ERR(ret, "flan_init()"))
    goto exit;

  flan_md_print_md(flanh->md);

  // Close is called automatically with onexit

exit:
  return ret;
}
