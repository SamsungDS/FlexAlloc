// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_util.h"

size_t
strnlen(char *s, size_t maxlen)
{
  // otherwise provided by POSIX
  for (size_t off = 0; off < maxlen; off++, s++)
    if (*s == '\0')
      return off;
  return maxlen;
}
