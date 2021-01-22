"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
PYX
"""
from libc.stdlib cimport malloc, free

cdef class MkfsParams:
    def __init__(self):
        cdef fla_mkfs_p *ptr = <fla_mkfs_p *>malloc(sizeof(fla_mkfs_p))
        if ptr == NULL:
            raise MemoryError("failed to allocate struct")
        self.dev_uri = "".encode("ascii")
        self.owner = True
        self.data = ptr

    @staticmethod
    cdef public MkfsParams from_ptr(fla_mkfs_p *ptr):
        cdef MkfsParams inst = MkfsParams.__new__(MkfsParams)
        # TODO memset to zero
        inst.data = ptr
        inst.owner = False
        return inst

    def __repr__(self):
        if self.data:
            return "MkfsParams<dev_uri: {}, slab_nlb: {}, npools: {}, verbose: {}>".format(
                self.dev_uri,
                self.data.slab_nlb,
                self.data.npools,
                self.data.verbose
            )
        else:
            return "MkfsParams<~null>"

    def __dealloc__(self):
        if self.owner and self.data != NULL:
            free(self.data)
            self.data = NULL

    @property
    def dev_uri(self):
        if not self.data:
            raise RuntimeError("no data reference")
        return self.dev_uri

    @dev_uri.setter
    def dev_uri(self, value: str):
        if not isinstance(value, str):
            raise ValueError("value must be a string")
        if not self.data:
            raise RuntimeError("no data reference")
        self.dev_uri = value.encode("ascii")
        cdef char *c_str = self.dev_uri
        self.data.dev_uri = c_str

    @property
    def slab_nlb(self) -> int:
        if not self.data:
            raise RuntimeError("no data reference")
        return self.data.slab_nlb

    @slab_nlb.setter
    def slab_nlb(self, value: int) -> None:
        if not isinstance(value, int) or value < 1:
            raise ValueError("slab_nlb must be a positive integer")
        if not self.data:
            raise RuntimeError("no data reference")
        self.data.slab_nlb = value

    @property
    def npools(self) -> int:
        if not self.data:
            raise RuntimeError("no data reference")
        return self.data.npools

    @npools.setter
    def npools(self, value: int) -> None:
        if not isinstance(value, int) or value < 1:
            raise ValueError("npools must be a positive integer")
        if not self.data:
            raise RuntimeError("no data reference")
        self.data.npools = value

    @property
    def verbose(self) -> bool:
        if not self.data:
            raise RuntimeError("no data reference")
        return self.data.verbose

    @verbose.setter
    def verbose(self, value: bool) -> None:
        if not isinstance(value, bool):
            raise ValueError("verbose must be a boolean")
        if not self.data:
            raise RuntimeError("no data reference")
        self.data.verbose = value


def mkfs(dev_uri: str, npools: int, slab_nlb: int, verbose=False) -> None:
    cdef MkfsParams params = MkfsParams()

    # doing the same work as the property setter does
    params.dev_uri = dev_uri.encode("ascii")
    cdef char *c_str = params.dev_uri
    params.data.dev_uri = c_str

    params.npools = npools
    params.slab_nlb = slab_nlb
    if verbose:
        params.verbose = True
    if fla_mkfs(params.data):
        raise RuntimeError("failed to create file system")
    return
