from libc.stdint cimport uint32_t


cdef extern from "flexalloc_freelist.h" nogil:
    ctypedef uint32_t * freelist_t
