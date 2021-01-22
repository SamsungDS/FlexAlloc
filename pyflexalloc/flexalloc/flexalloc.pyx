from libc.stdlib cimport malloc, free


cdef extern from "libflexalloc.h" nogil:
    int fla_open(char * dev_uri, flexalloc ** fs)
    int fla_close(flexalloc * fs)
    int fla_sync(flexalloc *fs)


cdef class FlexAlloc:
    def __init__(self, dev_uri: str):
        self._dev_uri_py_str = dev_uri.encode("ascii")
        cdef char *c_str = self._dev_uri_py_str
        self.data = NULL
        if fla_open(c_str, &self.data):
            raise MemoryError("failed to open FlexAlloc system")
        self.owner = True

    @staticmethod
    cdef public FlexAlloc from_ptr(flexalloc *fs):
        cdef FlexAlloc inst = FlexAlloc.__new__(FlexAlloc)
        inst.data = fs
        inst.owner = False
        return inst

    def close(self):
        if self.data == NULL:
            return  # already closed
        print("FlexAlloc.close()")
        if fla_close(self.data):
            raise RuntimeError("failed to close {}".format(self._dev_uri_py_str))
        else:
            self.data = NULL

    def sync(self):
      if self.data == NULL:
        return # can't sync a closed FS
      print("FlexAlloc.sync()")
      if fla_sync(self.data):
        raise RuntimeError("failed to sync {}".format(self.dev_uri.py_str))

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            self.close()

cdef class ObjectHandle:
    def __init__(self):
        cdef fla_object *ptr = <fla_object *>malloc(sizeof(fla_object))
        if ptr == NULL:
            raise MemoryError("failed to allocate handle")
        self.owner = True
        self.data = ptr

    def __repr__(self):
        if self.data:
            return "ObjectHandle<slab_id: {}, entry_ndx: {}>".format(self.data.slab_id, self.data.entry_ndx)
        else:
            return "ObjectHandle<~null>"

    def __str__(self):
        return self.__repr__()

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            print("ObjectHandle.__dealloc__")
            free(self.data)
            self.data = NULL

    @property
    def slab_id(self):
        if self.data == NULL:
            raise RuntimeError("no reference")
        return self.data.slab_id

    @slab_id.setter
    def slab_id(self, value: int):
        if not isinstance(value, int) or value < 0:
            raise ValueError("invalid slab_id")
        elif self.data == NULL:
            raise RuntimeError("no data pointer associated")
        self.data.slab_id = value

    @property
    def entry_ndx(self):
        if self.data == NULL:
            raise RuntimeError("no reference")
        return self.data.entry_ndx

    @entry_ndx.setter
    def entry_ndx(self, value: int):
        if not isinstance(value, int) or value < 0:
            raise ValueError("invalid entry_ndx")
        elif self.data == NULL:
            raise RuntimeError("no data pointer associated")
        self.data.entry_ndx = value


cdef class PoolHandle:
    def __init__(self):
        cdef fla_pool *ptr = <fla_pool *>malloc(sizeof(fla_pool))
        if ptr == NULL:
            raise MemoryError("failed to allocate handle")
        self.owner = True
        self.data = ptr

    @staticmethod
    cdef public PoolHandle from_ptr(fla_pool *ptr):
        cdef PoolHandle inst = PoolHandle.__new__(PoolHandle)
        inst.data = ptr
        inst.owner = False
        return inst

    def __repr__(self):
        if self.data:
            return "PoolHandle<h2: {}, ndx: {}>".format(self.data.h2, self.data.ndx)
        else:
            return "PoolHandle<~null>"

    def __str__(self):
        return self.__repr__()

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            free(self.data)
            self.data = NULL

    @property
    def h2(self):
        if self.data == NULL:
            raise RuntimeError("no reference")
        return self.data.h2

    @h2.setter
    def h2(self, value: int):
        if not isinstance(value, int) or value < 0:
            raise ValueError("invalid h2")
        elif self.data == NULL:
            raise RuntimeError("no data pointer associated")
        self.data.h2 = value

    @property
    def ndx(self):
        if self.data == NULL:
            raise RuntimeError("no reference")
        return self.data.ndx

    @ndx.setter
    def ndx(self, value):
        if not isinstance(value, int) or value < 0:
            raise ValueError("invalid ndx")
        elif self.data == NULL:
            raise RuntimeError("no data pointer associated")
        self.data.ndx = value
