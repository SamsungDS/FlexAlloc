from flexalloc import libflexalloc
from flexalloc import mm
import flexalloc
from tests_common import dev_loop, fla_open_direct, fla_open_daemon, format_fla_mkfs
import pytest


@pytest.mark.parametrize("loop_dev", [dev_loop(size_mb=10, block_size_bytes=512)])
def test_open_close(loop_dev):
    with loop_dev() as tdev:
        mm.mkfs(tdev.uri, 10, 10)
        fs = libflexalloc.open(tdev.uri)
        libflexalloc.close(fs)


@pytest.mark.xyz
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
def test_loop_write_read(fs):
    with fs() as fs:
        print("creating pool")
        p1 = libflexalloc.pool_create(fs, "lol", 10)
        print("object_alloc")
        p1o1 = libflexalloc.object_alloc(fs, p1)
        print(p1o1)
        print("IOBuffer (#1)")

        msg = "hello"

        with flexalloc.io_buffer(fs, 512 * 8) as buf:
            print("write to memoryview")
            buf.view[0:5] = msg.encode("ascii")
            print("object_write")
            libflexalloc.object_write(fs, p1, p1o1, buf, 0, 512 * 8)

        with flexalloc.io_buffer(fs, 512 * 12) as buf2:
            print("object_read")
            libflexalloc.object_read(fs, p1, p1o1, buf2, 0, 512 * 8)
            print("try to decode buffer contents")
            buf2_contents = bytes(buf2.view[0:5]).decode("ascii")
            print(buf2_contents)
            assert buf2_contents == msg
            print("free buf2")

        print("close fs")
