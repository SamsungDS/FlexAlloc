from flexalloc import libflexalloc
from flexalloc import mm
from flexalloc.flexalloc import FlexAlloc
import flexalloc
from tests_common import loop_device
import pytest
from functools import partial


@pytest.mark.parametrize("mkdev", [partial(loop_device, 10, 512)])
def test_open_close(mkdev):
    with mkdev() as tdev:
        mm.mkfs(tdev.dev_uri, 10, 10)
        fs = libflexalloc.open(tdev.dev_uri)
        libflexalloc.close(fs)


@pytest.mark.parametrize("dev", [partial(loop_device, 100, 512)])
@pytest.mark.parametrize("num", [1])
def test_loop_write_read(dev, num):
    with dev() as tdev:
        mm.mkfs(tdev.dev_uri, 10, 1000, True)
        print("Getting device")
        fs = libflexalloc.open(tdev.dev_uri)
        print("creating pool")
        p1 = libflexalloc.pool_create(fs, "lol", 10)
        print("object_alloc")
        p1o1 = libflexalloc.object_alloc(fs, p1)
        print(p1o1)
        print("IOBuffer (#1)")

        with flexalloc.io_buffer(fs, 512 * 8) as buf:
            print("write to memoryview")
            buf.view[0:5] = "hello".encode("ascii")
            print("object_write")
            libflexalloc.object_write(fs, p1, p1o1, buf, 0, 512 * 8)

        with flexalloc.io_buffer(fs, 512 * 12) as buf2:
            print("object_read")
            libflexalloc.object_read(fs, p1, p1o1, buf2, 0, 512 * 8)
            print("try to decode buffer contents")
            print(bytes(buf2.view[0:5]).decode("ascii"))
            print("free buf2")

        print("close fs")
        libflexalloc.close(fs)
        print(tdev)
