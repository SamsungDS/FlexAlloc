"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
from libc.stdint cimport uint64_t, uint32_t
from flexalloc.xnvme_env cimport xnvme_dev
from flexalloc.xnvme_env cimport xnvme_opts
from flexalloc.hash cimport fla_htbl
from flexalloc.freelist cimport freelist_t
from flexalloc.slabcache cimport fla_slab_flist_cache_elem
from flexalloc.mm cimport (
    fla_slab_header,
    fla_pool_entry,
    fla_super,
    fla_pool_htbl_header
)


cdef extern from "flexalloc.h" nogil:
    cdef struct flexalloc

    cdef struct fla_object:
        uint32_t slab_id
        uint32_t entry_ndx


    cdef struct fla_dev:
        xnvme_dev *dev
        uint32_t lb_nbytes


    cdef struct fla_slab_flist_cache:
        flexalloc *_fs
        fla_slab_flist_cache_elem *_head


    cdef struct fla_geo_pool_sgmt:
        uint32_t freelist_nlb
        uint32_t htbl_nlb
        uint32_t htbl_tbl_size
        uint32_t entries_nlb


    cdef struct fla_geo_slab_sgmt:
        uint32_t slab_sgmt_nlb


    cdef struct fla_geo:
        uint64_t nlb
        uint32_t lb_nbytes
        uint32_t slab_nlb
        uint32_t npools
        uint32_t nslabs

        uint32_t md_nlb
        fla_geo_pool_sgmt pool_sgmt
        fla_geo_slab_sgmt slab_sgmt

    cdef struct fla_pools:
        freelist_t freelist
        fla_htbl htbl
        fla_pool_htbl_header *htbl_hdr_buffer
        fla_pool_entry *entries


    cdef struct fla_slabs:
        fla_slab_header *headers

        uint32_t *fslab_num
        uint32_t fslab_head
        uint32_t fslab_tail

    cdef struct flexalloc:
        fla_dev dev
        unsigned int state
        void *fs_buffer

        fla_slab_flist_cache slab_cache
        fla_geo geo

        fla_super *super
        fla_pools pools
        fla_slabs slabs

    cdef struct fla_pool:
        uint64_t h2
        uint32_t ndx

    cdef struct fla_open_opts:
      char *dev_uri
      char *md_dev_uri
      xnvme_opts *opts


cdef class FlexAlloc:
    cdef flexalloc *data
    cdef bint owner

    @staticmethod
    cdef public FlexAlloc from_ptr(flexalloc *ptr)
    cpdef public void close(self)


cdef class ObjectHandle:
    cdef fla_object *data
    cdef bint owner


cdef class PoolHandle:
    cdef fla_pool *data
    cdef bint owner

    @staticmethod
    cdef public PoolHandle from_ptr(fla_pool *ptr)
