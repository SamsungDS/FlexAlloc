from flexalloc import libflexalloc
from flexalloc import mm
from flexalloc.flexalloc import FlexAlloc
import flexalloc
from tests_common import loop_device
import pytest
from functools import partial


@pytest.mark.parametrize("dev", [partial(loop_device, 100, 512)])
def test_sync(dev):
    with dev() as tdev:
        mm.mkfs(tdev.dev_uri, 10, 1000, True)
        print("Getting device")
        fs = FlexAlloc(tdev.dev_uri)
        print("creating pool")
        p1 = libflexalloc.pool_create(fs, "lol", 10)
        print("object_alloc")
        p1o1 = libflexalloc.object_alloc(fs, p1)
        print(p1o1)
        print("sync fs")
        fs.sync()
        p1o2 = libflexalloc.object_alloc(fs, p1)
        print(p1o2)
        fs.close()
        print(tdev)
