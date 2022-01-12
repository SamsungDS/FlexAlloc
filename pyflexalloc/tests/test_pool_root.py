from flexalloc import libflexalloc
from flexalloc import mm
from flexalloc.flexalloc import FlexAlloc
from flexalloc.flexalloc import ObjectHandle
import flexalloc
from tests_common import loop_device
import pytest
from functools import partial


@pytest.mark.parametrize("dev", [partial(loop_device, 100, 512)])
def test_pool_root(dev):
    with dev() as tdev:
        mm.mkfs(tdev.dev_uri, 10, 1000, True)
        print("Getting device")
        fs = libflexalloc.open(tdev.dev_uri)
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
            print ("Root obj sid:", ro.slab_id, " entry_ndx:", ro.entry_ndx)
            print ("Pool obj sid:", p1o1.slab_id, " entry_ndx:", p1o1.entry_ndx)
            raise RuntimeError("Root object != Pool obj, bailing!")
        libflexalloc.close(fs)
        print(tdev)
