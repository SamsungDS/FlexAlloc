"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
from pathlib import Path
from typing import Union


def open(dev_uri: str) -> FlexAlloc:
    py_str = dev_uri.encode("ascii")
    cdef char *c_str = py_str
    cdef flexalloc *data
    if fla_open(c_str, &data):
        raise MemoryError("failed to open FlexAlloc system")

    cdef FlexAlloc fs = FlexAlloc.from_ptr(data)
    return fs


def md_open(dev_uri: str, md_dev_uri: str) -> FlexAlloc:
    dev_pystr = dev_uri.encode("ascii")
    cdef char *dev_cstr = dev_pystr
    md_dev_pystr = md_dev_uri.encode("ascii")
    cdef char *md_dev_cstr = md_dev_pystr

    cdef flexalloc *data
    if fla_md_open(dev_cstr, md_dev_cstr, &data):
        raise MemoryError("failed to open FlexAlloc system")

    cdef FlexAlloc fs = FlexAlloc.from_ptr(data)
    return fs


def daemon_open(socket: Union[str, Path]) -> FlexAllocDaemonClient:
    return FlexAllocDaemonClient.open(socket)


def close(fs: FlexAlloc) -> None:
    # delegate to actual implementation on type itself
    fs.close()


def sync(fs: FlexAlloc) -> int:
    if fs.data == NULL:
        return 1  # cannot sync already closed FS
    if fla_sync(fs.data):
        raise RuntimeError("failed to sync system")
    return 0


def pool_create(fs: FlexAlloc, name: str, obj_nlb: int) -> PoolHandle:
    py_str = name.encode("ascii")
    cdef char *c_str = py_str
    if obj_nlb <= 0:
        raise ValueError("must be a positive integer")
    cdef fla_pool *hptr
    if fla_pool_create(fs.data, c_str, len(name), obj_nlb, &hptr):
        raise RuntimeError("failed to create pool")
    return PoolHandle.from_ptr(hptr)


def pool_destroy(fs: FlexAlloc, handle: PoolHandle):
    if fla_pool_destroy(fs.data, handle.data):
        # TODO: delete pool obj, somehow?
        raise RuntimeError("failed to destroy pool")


def pool_open(fs: FlexAlloc, name: str) -> PoolHandle:
    py_str = name.encode("ascii")
    cdef char *c_str = py_str
    cdef fla_pool *hptr
    if fla_pool_open(fs.data, c_str, &hptr):
        raise RuntimeError("failed to open pool")
    return PoolHandle.from_ptr(hptr)

def pool_close(fs: FlexAlloc, handle: PoolHandle) -> None:
    pool_close(fs, handle)

def pool_set_root(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle, fla_root_object_set_action act) -> None:
    if fla_pool_set_root_object(fs.data, pool.data, obj.data, act):
        raise RuntimeError("Failed to set root object for pool")

def pool_get_root(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle) -> None:
    if fla_pool_get_root_object(fs.data, pool.data, obj.data):
        raise RuntimeError("Failed to get root object for pool")

def object_create(fs: FlexAlloc, pool: PoolHandle) -> ObjectHandle:
  oh = ObjectHandle()
  if fla_object_create(fs.data, pool.data, oh.data):
    raise RuntimeError("failed to allocate object")
  return oh

def object_open(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle) -> None:
    if fla_object_open(fs.data, pool.data, obj.data):
        raise RuntimeError("failed to open object")


def object_destroy(fs: FlexAlloc, pool: PoolHandle, object: ObjectHandle):
    if fla_object_destroy(fs.data, pool.data, object.data):
        raise RuntimeError("failed to free object")
    return


cdef class IOBuffer:
    def __init__(self, fs: FlexAlloc, nbytes: int):
        if nbytes <= 0:
            raise ValueError("nbytes must be a positive integer")
        cdef void *ptr = fla_buf_alloc(fs.data, nbytes)
        if ptr == NULL:
            raise MemoryError("failed to allocate handle")
        self.fs = fs
        self.nbytes = nbytes
        self.owner = True
        self.data = ptr
        self._view = <uint8_t[:nbytes]>ptr

    def __repr__(self):
        if self.data:
            return "IOBuffer<nbytes: {}>".format(self.nbytes)
        else:
            return "IOBuffer<~null~>"

    def __dealloc__(self):
        self.close()

    def close(self):
        if self.owner and self.data != NULL:
            fla_buf_free(self.fs.data, self.data)
            self.data = NULL

    @property
    def view(self) -> memoryview:
        if self.data == NULL:
            raise RuntimeError("no underlying buffer associated")
        return self._view


def object_read(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle,
                buf: IOBuffer, offset: int, length: int) -> None:
    if offset < 0:
        raise ValueError("offset must be 0 or greater")
    elif length <= 0:
        raise ValueError("length must be 1 or greater")
    if fla_object_read(fs.data, pool.data, obj.data, buf.data, offset, length):
        raise RuntimeError("failed to read from object")
    return


def object_write(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle,
                 buf: IOBuffer, offset: int, length: int) -> None:
    if offset < 0:
        raise ValueError("offset must be 0 or greater")
    elif length <= 0:
        raise ValueError("length must be 1 or greater")
    if fla_object_write(fs.data, pool.data, obj.data, buf.data, offset, length):
        raise RuntimeError("failed to write to object")
    return


def object_unaligned_write(fs: FlexAlloc, pool: PoolHandle, obj: ObjectHandle,
                           buf: IOBuffer, offset: int, length: int) -> None:
    if offset < 0:
        raise ValueError("offset must be 0 or greater")
    elif length <= 0:
        raise ValueError("length must be 1 or greater")
    if fla_object_unaligned_write(
        fs.data, pool.data, obj.data, buf.data, offset, length):
        raise RuntimeError("failed to write to object")
    return


def fs_lb_nbytes(fs: FlexAlloc) -> int:
    return fla_fs_lb_nbytes(fs.data)


cdef class FlexAllocDaemonClient:
    def __init__(self):
        raise RuntimeError("do not instantiate directly, use `daemon_open` method")

    @staticmethod
    def open(socket: Union[str, Path]) -> FlexAllocDaemonClient:
        if not isinstance(socket, Path):
            socket = Path(socket)

        if not Path(socket).exists():
            raise RuntimeError(f"specified socket '{socket}' does not exist!")

        py_str = str(socket).encode("ascii")
        cdef char *c_str = py_str

        cdef FlexAllocDaemonClient inst = FlexAllocDaemonClient.__new__(FlexAllocDaemonClient)
        if fla_daemon_open(c_str, &inst.client):
            raise RuntimeError("failed to connect to daemon")

        inst._fs = FlexAlloc.from_ptr(inst.client.flexalloc)
        return inst

    @property
    def fs(self) -> FlexAlloc:
        return self._fs