from flexalloc import libflexalloc
from tests_common import dev_loop, fla_open_direct, fla_open_daemon, format_fla_mkfs
import pytest
import faulthandler


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
def test_sync(fs):
    faulthandler.enable()
    with fs() as fs:
        p1 = libflexalloc.pool_create(fs, "lol", 10)
        print("object_alloc #1")
        p1o1 = libflexalloc.object_alloc(fs, p1)
        print(p1o1)
        print("sync fs")
        libflexalloc.sync(fs)
        print("object_alloc #2")
        p1o2 = libflexalloc.object_alloc(fs, p1)
        print(p1o2)
