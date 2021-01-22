"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
"""
import os
from contextlib import contextmanager
from dataclasses import dataclass
from tempfile import NamedTemporaryFile
from pathlib import Path
from subprocess import run, PIPE
from typing import Optional, Union
from flexalloc import xnvme_env
from flexalloc.pyutils import loop


@dataclass
class TestDeviceParams:
    nblocks: int
    slab_nlb: int
    npools: int


@dataclass
class TestDevice:
    lb_nbytes: int
    nblocks: int

    dev_uri: str
    loop: Optional[Path] = None

    @property
    def is_loop(self) -> bool:
        return self.loop is not None

    def __repr__(self):
        return f"{type(self).__name__}<dev_uri: {self.dev_uri}, is_loop: {self.is_loop}, lb_nbytes: {self.lb_nbytes}, nblocks: {self.nblocks}>"

    def __str__(self):
        return self.__repr__()


def dd_create_file(dst: Path, block_size: str, count: int) -> None:
    """Create temporary file of specified size

    Raises:
        CalledProcessError: if operation failed, inspect error for details"""
    if count <= 0:
        raise ValueError("block count must be 1 or greater")
    run(
        f"""dd if=/dev/zero of="{str(dst.absolute())}" bs={block_size} count={count}""",
        shell=True,
        check=True,
        stdout=PIPE,
        stderr=PIPE,
    )


@contextmanager
def temp_backing_file(block_size: str, count: int) -> Path:
    tmp = NamedTemporaryFile(delete=False)
    try:
        tmp.close()
        dd_create_file(Path(tmp.name), block_size, count)
        yield Path(tmp.name)
    finally:
        Path(tmp.name).unlink(missing_ok=True)


@contextmanager
def loop_device(size_mb: int, block_size_bytes: int):
    print("LOOP DEVICE")
    with temp_backing_file(block_size=f"{size_mb}M", count=1) as file:
        loop_path: Path = loop.setup_loop(file, block_size=block_size_bytes)
        try:
            dev = xnvme_env.XnvmeDev(str(loop_path))
            lb_nbytes = xnvme_env.xne_dev_lba_nbytes(dev)
            nblocks = int(xnvme_env.xne_dev_tbytes(dev) / lb_nbytes)
            dev.close()
            yield TestDevice(
                lb_nbytes=lb_nbytes,
                nblocks=nblocks,
                dev_uri=str(loop_path),
                loop=loop_path,
            )
        finally:
            loop.remove_loop(loop_path)


@contextmanager
def hw_device(min_size_mb: int, device: Optional[Union[str, Path]] = None):
    if not device:
        device = os.environ.get("FLEXALLOC_TEST_DEVICE")
        if not device:
            raise ValueError(
                f"no device to use - no device specified and FLEXALLOC_TEST_DEVICE unset"
            )
    if not isinstance(device, Path):
        device = Path(device)

    dev = xnvme_env.XnvmeDev(str(device))
    try:
        lb_nbytes = xnvme_env.xne_dev_lba_nbytes(dev)
        tbytes = xnvme_env.xne_dev_tbytes(dev)
        nblocks = int(tbytes / lb_nbytes)
    finally:
        dev.close()
    if tbytes <= (min_size_mb * 1024 ** 2):
        raise RuntimeError(
            f"hardware device too small, test desires {min_size_mb}MB, device has {tbytes / 1024**2}MB"
        )
    yield TestDevice(
        lb_nbytes=lb_nbytes, nblocks=nblocks, dev_uri=str(device.absolute()), loop=None
    )


# def get_device(params: TestDeviceParams) -> TestDevice:
#     if "FLEXALLOC_TEST_DEV" not in environ:
#         # loop device setup
#         # TODO: can create loop devices with different base block sizes.
#         # TODO: allocate backing file of sufficient size.
#         loop_path: Path = loop.setup_loop(FILE, 512)
#         dev = xnvme_env.XnvmeDev(str(loop_path))
#         lb_nbytes = xnvme_env.xne_dev_lba_nbytes(dev)
#         nblocks = int(xnvme_env.xne_dev_tbytes(dev) / lb_nbytes)
#         dev.close()
#         return TestDevice(
#             lb_nbytes=lb_nbytes,
#             nblocks=nblocks,
#             dev_uri=str(loop_path),
#             loop=loop_path
#         )
#     else:
#         # HW device setup
#         dev_uri: Path = Path(environ.get("FLEXALLOC_TEST_DEV"))
#         if not dev_uri.exists():
#             raise RuntimeError(f"specified device '{dev_uri}' does not exist")
#
#
#     if "FLEXALLOC_TEST_DEV" in environ:
#         dev = xnvme_env.XnvmeDev(loop.setup_loop())
#         lb_nbytes = xnvme_env.xne_dev_lba_nbytes()
#         return
