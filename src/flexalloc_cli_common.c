#include <stdio.h>
#include "flexalloc_cli_common.h"

#define ARG_BUFLEN 384

static void
fmt_arg(char *buf, size_t buflen, struct cli_option *o)
{
  if (o->base.has_arg)
  {
    sprintf(buf, "--%s=%s", o->base.name, o->arg_ex);
  }
  else
  {
    sprintf(buf, "--%s", o->base.name);
  }
}

void
print_options(struct cli_option *options)
{
  char buf[ARG_BUFLEN];
  int longest_arg = 0;
  struct cli_option *o;

  fprintf(stdout, "Options:\n");

  for(o = options; o->base.name; o++)
  {
    char *end = buf;
    int arg_len;

    fmt_arg(buf, ARG_BUFLEN, o);
    while (*end != '\0')
      end++;
    arg_len = end - buf;

    if (arg_len > longest_arg)
      longest_arg = arg_len;
  }

  for(o = options; o->base.name; o++)
  {
    fmt_arg(buf, ARG_BUFLEN, o);
    fprintf(stdout, " -%c, %-*s\t%s\n", o->base.val, longest_arg, buf, o->description);
  }
}
