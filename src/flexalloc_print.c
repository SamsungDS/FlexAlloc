#include <string.h>
#include <stdio.h>
#include "libflexalloc.h"
#include "libxnvme.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"

int main(int argc, char **argv)
{
  char *dev = NULL, *md_dev = NULL;
  struct fla_open_opts open_opts = {0};
  struct flexalloc *fs;
  struct xnvme_opts x_opts = xnvme_opts_default();
  int ret;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: flexalloc_print DEV [MD_DEV]");
    return 1;
  }

  dev = argv[1];
  if (argc > 2)
    md_dev = argv[2];

  open_opts.dev_uri = dev;
  open_opts.md_dev_uri = md_dev;
  open_opts.opts = &x_opts;
  open_opts.opts->async = "io_uring_cmd";
  ret = fla_open(&open_opts, &fs);
  if (FLA_ERR(ret, "fla_open()"))
    return ret;

  fla_print_fs(fs);

  ret = fla_close(fs);
  if (FLA_ERR(ret, "fla_close()"))
      return ret;

  return ret;
}
