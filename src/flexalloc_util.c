// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_util.h"
#include <stdlib.h>

size_t
fla_strnlen(char *s, size_t maxlen)
{
  // otherwise provided by POSIX
  for (size_t off = 0; off < maxlen; off++, s++)
    if (*s == '\0')
      return off;
  return maxlen;
}

char *
fla_strndup(char const *s, size_t const len)
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
fla_strdup(char const *s)
{
  size_t len = strlen(s);
  return fla_strndup(s, len);
}

uint32_t
fla_nelems_max(uint64_t units_total, uint32_t elem_sz_nunit,
    uint32_t (*calc_md_size_nunits)(uint32_t nelems, va_list), ...)
{
  uint32_t nelems = units_total / elem_sz_nunit;
  uint32_t md_nunits;
  va_list ap;

  while(nelems)
  {
    va_start(ap, calc_md_size_nunits);
    md_nunits = calc_md_size_nunits(nelems, ap);
    va_end(ap);
    if(units_total - (nelems * elem_sz_nunit) >= md_nunits)
      break;
    nelems--;
  }
  return nelems;
}


