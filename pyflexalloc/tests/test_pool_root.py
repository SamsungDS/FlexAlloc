from flexalloc import libflexalloc
from flexalloc.flexalloc import ObjectHandle
from tests_common import dev_loop, fla_open_direct, fla_open_daemon, format_fla_mkfs
import pytest


@pytest.mark.parametrize("fs", [
    fla_open_direct(
        device=dev_loop(size_mb=100, block_size_bytes=512),
        formatter=format_fla_mkfs(npools=10, slab_nlb=1000)
    ),
    fla_open_daemon(
        device=dev_loop(size_mb=100, block_size_bytes=512),
        formatter=format_fla_mkfs(npools=10, slab_nlb=1000)
    )
])
def test_pool_root(fs):
    with fs() as fs:
        print("creating pool")
        p1 = libflexalloc.pool_create(fs, "lol", 10)
        print("object_alloc")
        p1o1 = libflexalloc.object_alloc(fs, p1)
        try:
            ro1 = libflexalloc.pool_get_root(fs, p1)
        except:
            print("Root object not set as expected")
        else:
            raise RuntimeError("Root object set by default, bailing!")
        print("Setting root to allocated object")
        libflexalloc.pool_set_root(fs, p1, p1o1, 0)
        ro = ObjectHandle()
        print("Pool get root")
        libflexalloc.pool_get_root(fs, p1, ro)
        if p1o1.slab_id == ro.slab_id and p1o1.entry_ndx == ro.entry_ndx:
            print("Root object set and get successful")
        else:
            print("Root obj sid:", ro.slab_id, " entry_ndx:", ro.entry_ndx)
            print("Pool obj sid:", p1o1.slab_id, " entry_ndx:", p1o1.entry_ndx)
            raise RuntimeError("Root object != Pool obj, bailing!")
