from libc.stdint cimport uint64_t, uint16_t


cdef extern from "flexalloc_hash.h" nogil:
    cdef struct fla_htbl_entry:
        uint64_t h2
        uint64_t val
        uint16_t psl


    cdef struct fla_htbl:
        fla_htbl_entry *tbl
        unsigned int tbl_size
        unsigned int len

        unsigned int stat_insert_calls
        unsigned int stat_insert_failed
        unsigned int stat_insert_tries


cdef class HashtableEntry:
    cdef fla_htbl_entry *data
    cdef bint owner

    @staticmethod
    cdef public HashtableEntry from_ptr(fla_htbl_entry * entry)


cdef class Hashtable:
    cdef fla_htbl *data
    cdef bint owner

    @staticmethod
    cdef public Hashtable from_ptr(fla_htbl *htbl)
