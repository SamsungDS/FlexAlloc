"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
from libc.stdint cimport uint64_t, uint32_t
from libc.stdlib cimport malloc, free

cdef extern from "flexalloc_hash.h" nogil:
    uint64_t fla_hash_djb2(char *key)
    uint64_t fla_hash_sdbm(char *key)
    uint64_t fla_mad_compression(uint64_t hash, uint64_t a, uint64_t b, uint64_t n)

    uint64_t FA_HTBL_H1(char *key)
    uint64_t FA_HTBL_H2(char *key)
    uint64_t FA_HTBL_COMPRESS(uint64_t hval, uint64_t tbl_size)

    int htbl_new(unsigned int tbl_size, fla_htbl **htbl)
    int htbl_init(fla_htbl *htbl, unsigned int tbl_size)
    void htbl_free(fla_htbl *htbl)
    int htbl_insert(fla_htbl *htbl, char *key, uint32_t val)
    fla_htbl_entry *htbl_lookup(fla_htbl *htbl, char *key)
    void htbl_remove(fla_htbl *htbl, char *key)


cdef class HashtableEntry:

    def __init__(self):
        print("HashtableEntry.__init__")
        cdef fla_htbl_entry *data = <fla_htbl_entry *>malloc(sizeof(fla_htbl_entry))
        if not data:
            raise MemoryError("failed to allocate entry")
        self.data = data
        self.owner = True

    @staticmethod
    cdef public HashtableEntry from_ptr(fla_htbl_entry *entry):
        cdef HashtableEntry inst = HashtableEntry.__new__(HashtableEntry)
        inst.owner = False
        inst.data = entry
        return inst

    def __repr__(self):
        if self.data == NULL:
            return "HashtableEntry<data: NULL>"
        return "HashtableEntry<h2: {}, val: {}, psl: {}>".format(self.data.h2, self.data.val, self.data.psl)

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            free(self.data)
            self.data = NULL

    @property
    def h2(self):
        return self.data.h2

    @property
    def val(self):
        return self.data.val

    @property
    def psl(self):
        return self.data.psl


cdef class Hashtable:

    def __init__(self, table_size):
        cdef fla_htbl *data
        if htbl_new(table_size, &data):
            raise MemoryError("failed to allocate table")
        self.data = data
        self.owner = True

    @staticmethod
    cdef public Hashtable from_ptr(fla_htbl *htbl):
        cdef Hashtable inst = Hashtable.__new__(Hashtable)
        inst.data = htbl
        inst.owner = False
        return inst

    def __repr__(self):
        if self.data == NULL:
            return "Hashtable<data: NULL>"
        else:
            return "Hashtable<tbl_size: {}, len: {}>".format(self.data.tbl_size, self.data.len)

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            htbl_free(self.data)
            self.data = NULL
        return

    @property
    def tbl_size(self):
        return self.data.tbl_size

    @property
    def len(self):
        return self.data.len

    def insert(self, str key, val):
        py_str = key.encode("ascii")
        cdef char *c_str = py_str
        return htbl_insert(self.data, c_str, val)

    def lookup(self, str key):
        py_str = key.encode("ascii")
        cdef char *c_str = py_str
        cdef fla_htbl_entry *entry = htbl_lookup(self.data, c_str)
        if entry != NULL:
            return HashtableEntry.from_ptr(entry)
        return None

    def remove(self, str key):
        py_str = key.encode("ascii")
        cdef char *c_str = py_str
        return htbl_remove(self.data, c_str)
