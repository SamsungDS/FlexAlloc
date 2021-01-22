#ifndef __FLEXALLOC_CLI_COMMON_H_
#define __FLEXALLOC_CLI_COMMON_H_
#include <stddef.h>
#include <getopt.h>

struct cli_option
{
  struct option base;
  char *description;
  char *arg_ex;
};

void
print_options(struct cli_option *options);

#endif // __FLEXALLOC_CLI_COMMON_H_
