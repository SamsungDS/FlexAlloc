"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
PXD
"""
from libc.stdint cimport uint64_t, uint32_t, uint8_t

FA_NAME_SIZE = 128
FA_NAME_SIZE_POOL = 128

cdef extern from "flexalloc_mm.h" nogil:
    cdef struct fla_mkfs_p:
        char *dev_uri
        uint32_t slab_nlb
        uint32_t npools
        uint8_t verbose

    # TODO: fla_obj_list_item

    cdef struct fla_obj_meta:
        char name[128]


    cdef struct fla_slab_header:
        uint64_t pool
        uint32_t prev
        uint32_t next
        uint32_t refcount
        uint32_t maxcount


    cdef struct fla_pool_htbl_header:
        uint32_t size
        uint32_t len


    cdef struct fla_pool_entry:
        uint32_t empty_slabs
        uint32_t full_slabs
        uint32_t partial_slabs

        uint32_t obj_nlb
        char name[128]

    cdef struct fla_super:
        uint64_t magic
        uint32_t nslabs
        uint32_t slab_nlb
        uint32_t npools
        uint32_t md_nlb
        uint8_t fmt_version

    int fla_mkfs(fla_mkfs_p *p)


cdef class MkfsParams:
    cdef fla_mkfs_p *data
    cdef bytes dev_uri
    cdef bint owner

    @staticmethod
    cdef public MkfsParams from_ptr(fla_mkfs_p *ptr)
