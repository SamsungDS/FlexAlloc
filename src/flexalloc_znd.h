/**
 * flexalloc disk structures.
 *
 * Copyright (C) 2022 Adam Manzanares <a.manzanares@samsung.com>
 *
 * @file flexalloc_znd.h
 */
#ifndef __FLEXALLOC_ZND_H_
#define __FLEXALLOC_ZND_H_
#include <sys/queue.h>
#include <stdint.h>
#include "src/flexalloc.h"


void
fla_znd_zone_full(struct flexalloc *fs, uint32_t zone);

void
fla_znd_manage_zones_cleanup(struct flexalloc *fs);

void
fla_znd_manage_zones(struct flexalloc *fs, uint32_t zone);

#endif // __FLEXALLOC_ZND_H
