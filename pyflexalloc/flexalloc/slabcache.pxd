"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
from flexalloc.freelist cimport freelist_t


cdef extern from "flexalloc_slabcache.h" nogil:
    cdef enum fla_slab_flist_elem_state:
        FA_SLAB_CACHE_ELEM_STALE = 0
        FA_SLAB_CACHE_ELEM_DIRTY = 1
        FA_SLAB_CACHE_ELEM_CLEAN = 2


    cdef struct fla_slab_flist_cache_elem:
        freelist_t freelist
        fla_slab_flist_elem_state state
