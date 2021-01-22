// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include "libflexalloc.h"
#include "flexalloc_xnvme_env.h"
#include "flexalloc.h"

void *
fla_buf_alloc(struct flexalloc const *fs, size_t nbytes)
{
  return fla_xne_alloc_buf(fs->dev.dev, nbytes);
}

void
fla_buf_free(struct flexalloc const * fs, void *buf)
{
  fla_xne_free_buf(fs->dev.dev, buf);
}
