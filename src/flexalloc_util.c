// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_util.h"
#include <stdlib.h>

size_t
strnlen(char *s, size_t maxlen)
{
  // otherwise provided by POSIX
  for (size_t off = 0; off < maxlen; off++, s++)
    if (*s == '\0')
      return off;
  return maxlen;
}

char *
strndup(char const *s, size_t const len)
{
  char *new_s = malloc(len + 1);
  if (new_s == NULL)
  {
    return NULL;
  }

  memcpy(new_s, s, len);
  new_s[len] = '\0';

  return new_s;
}

char *
strdup(char const *s)
{
  size_t len = strlen(s);
  return strndup(s, len);
}

