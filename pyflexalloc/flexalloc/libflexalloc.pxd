"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
from libc.stdint cimport int32_t, uint8_t, uint16_t
from flexalloc.flexalloc cimport *


cdef extern from "flexalloc_daemon_base.h" nogil:

    cdef struct fla_msg_header:
        uint32_t len
        uint16_t cmd
        uint16_t tag


    cdef struct fla_msg:
        fla_msg_header *hdr
        char *data


    cdef struct fla_sys_identity:
        uint32_t type
        uint32_t version


    cdef struct fla_daemon_client:
        flexalloc *flexalloc
        pass

    int fla_daemon_open(char *socket_path, fla_daemon_client *client)


cdef extern from "libflexalloc.h" nogil:
    int FA_ERR_ERROR
    ctypedef enum fla_root_object_set_action:
      ROOT_OBJ_SET_DEF   = 0,
      ROOT_OBJ_SET_FORCE = 1 << 0,
      ROOT_OBJ_SET_CLEAR = 1 << 1,

    int fla_open(char *dev_uri, flexalloc ** fs)
    int fla_md_open(char *dev_uri, char *md_dev_uri, flexalloc ** fs)
    # wrapper for this call is implemented directly on the FlexAlloc type
    # int fla_close(flexalloc *fs)
    int fla_sync(flexalloc *fs)

    int fla_pool_create(flexalloc *fs, const char *name, int name_len,
                          uint32_t obj_nlb, fla_pool **handle)
    int fla_pool_destroy(flexalloc *fs, fla_pool *handle)
    int fla_pool_open(flexalloc *fs, const char *name, fla_pool **handle)
    int fla_pool_set_root_object(const flexalloc *fs, const fla_pool *pool_handle,
                                   const fla_object *obj, fla_root_object_set_action act)
    int fla_pool_get_root_object(const flexalloc *fs, const fla_pool *pool_handle,
                                   fla_object *obj)
    void fla_pool_close(flexalloc *fs, fla_pool *handle)

    int fla_object_create(flexalloc *fs, fla_pool *pool_handle,
                           fla_object *obj)
    int fla_object_open(flexalloc *fs, fla_pool *pool_handle,
                          fla_object *obj)
    int fla_object_destroy(flexalloc *fs, fla_pool *pool_handle,
                          fla_object *obj)

    void *fla_buf_alloc(const flexalloc *fs, size_t nbytes)
    void fla_buf_free(const flexalloc *fs, void *buf)

    int fla_object_read(
            const flexalloc *fs, const fla_pool *pool_handle,
            const fla_object *obj, void *buf, size_t offset, size_t len)
    int fla_object_write(
            const flexalloc *fs, const fla_pool *pool_handle,
            const fla_object *obj, void *buf, size_t offset, size_t len)
    int fla_object_unaligned_write(
            const flexalloc *fs, const fla_pool *pool_handle,
            const fla_object *obj, const void *buf, size_t offset,
            size_t len)

    int32_t fla_fs_lb_nbytes(const flexalloc *fs)


cdef class IOBuffer:
    cdef FlexAlloc fs
    cdef void *data
    cdef uint8_t[:] _view
    cdef bint owner
    cdef int nbytes


cdef class FlexAllocDaemonClient:
    cdef fla_daemon_client client
    # (Python) property `fs` provides access but prevents re-assignment
    cdef FlexAlloc _fs

    # open() method not typed as static cpdef methods are not supported.