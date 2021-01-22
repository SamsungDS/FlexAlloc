// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>

#include "flexalloc_xnvme_env.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"
#include "flexalloc_cli_common.h"

#define FLA_MKFS_SHOW_USAGE 100

int fla_mkfs_parse_args(int, char**, struct fla_mkfs_p*);

static struct cli_option options[] =
{
  {
    .base = {"slba-nlb", required_argument, NULL, 's'},
    .description = "size of slab in logical blocks",
    .arg_ex = "NUM"
  },
  {
    .base = {"pools", required_argument, NULL, 'p'},
    .description = "number of pools",
    .arg_ex = "NUM"
  },
  {
    .base = {"help", no_argument, NULL, 'h'},
    .description = "display this help",
    .arg_ex = NULL
  },
  {
    .base = {"verbose", no_argument, NULL, 'v'},
    .description = "display additional information..!",
    .arg_ex = NULL
  },
  {
    .base = {NULL, 0, NULL, 0}
  }
};

void
fla_mkfs_help()
{
  fprintf(stdout, "Usage: mkfs.flexalloc [options] device\n\n");
  fprintf(stdout, "Initialize device for use with flexalloc\n\n");
  print_options(options);
}

int
fla_mkfs_parse_args(int argc, char ** argv, struct fla_mkfs_p * p)
{
  int err = 0;
  int c;
  int opt_idx = 0;
  // getopt_long requires an actual, contiguous array of `struct option` entries
  int n_opts = sizeof(options)/sizeof(struct cli_option);
  struct option long_options[n_opts];
  char *arg_end;
  long arg_long;

  for (int i=0; i<n_opts; i++)
  {
    memcpy(long_options+i, &options[i].base, sizeof(struct option));
  }

  while ((c = getopt_long(argc, argv, "vhs:p:", long_options, &opt_idx)) != -1)
  {
    switch (c)
    {
    case 'v':
      p->verbose = 1;
      break;
    case 'h':
      fla_mkfs_help();
      return FLA_MKFS_SHOW_USAGE;
    case 's':
      arg_long = strtol(optarg, &arg_end, 0);
      if ((arg_end == optarg) || (arg_long > INT_MAX) || (arg_long <= 0))

      {
        fprintf(stderr, "slab-nlb: invalid argument, '%s'\n", optarg);
        err |= -1;
      }
      p->slab_nlb = (int)arg_long;
      break;
    case 'p':
      arg_long = strtol(optarg, &arg_end, 0);
      if ((arg_end == optarg) || (arg_long > INT_MAX) || (arg_long <= 0))
      {
        fprintf(stderr, "pools: invalid argument, '%s'\n", optarg);
        err = -1;
      }
      p->npools = (int)arg_long;
      break;
    default:
      break;
    }
  }

  // expect 1 positional argument - the device to format
  if (optind == argc)
  {
    fprintf(stderr, "mkfs.flexalloc: not enough arguments\n");
    err = -1;
    goto exit;
  }
  p->dev_uri = argv[optind++];

  // TODO: ensure dev uri exists

  // further positional arguments must be a mistake, print them and exit
  if(optind < argc)
  {
    err = -1;
    fprintf(stderr, "One or more unrecognized arguments:\n");
    for(int i = optind ; i < argc; ++i)
    {
      fprintf(stderr, "  * %s\n", argv[i]);
    }
    fprintf(stderr, "\n");
    goto exit;
  }

  // Exit now if any invalid arguments were passed.
  // The user will receive one or more error messages indicating what to change.
  if (err)
    goto exit;

  // ensure we got told how large to make the slabs
  if (!p->slab_nlb)
  {
    fprintf(stderr,
            "slab-nlb is 0 and this is invalid, either you gave a non-integer value or forgot to set it.\n");
    err = -1;
    goto exit;
  }

exit:
  return err;
}

int
main(int argc, char ** argv)
{
  int err = 0;

  struct fla_mkfs_p mkfs_params =
  {
    .dev_uri = NULL,
    .slab_nlb = 0,
    .verbose = 0,
  };
  err = fla_mkfs_parse_args(argc, argv, &mkfs_params);
  if (err)
  {
    if (err == FLA_MKFS_SHOW_USAGE)
      err = 0;
    else
      fprintf(stderr, "Try 'mkfs.flexalloc --help' for more information.\n");

    goto exit;
  }

  fprintf(stderr, "Opts:\n");
  fprintf(stderr, "  dev_uri: %s\n", mkfs_params.dev_uri);
  fprintf(stderr, "  slab_nlb: %"PRIu32"\n", mkfs_params.slab_nlb);
  fprintf(stderr, "  verbose: %"PRIu8"\n", mkfs_params.verbose);
  fla_mkfs(&mkfs_params);

exit:
  exit(err);
}
