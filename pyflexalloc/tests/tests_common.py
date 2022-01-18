"""
Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

NOTE: many test fixtures are split into a _<foo> and a <foo> function.
      This split is done to satisfy the type-checker while providing a way
      to delay operations until the test itself starts.

      In other words, each <foo> function returns a thunk which, when invoked,
      returns a context manager object which is designed to manage the lifetime
      of the object in question, cleaning up after itself as the code exits its
      scope.
"""
import os
import signal
from contextlib import contextmanager, suppress as ctx_suppress
from dataclasses import dataclass
from tempfile import NamedTemporaryFile
from pathlib import Path
from subprocess import run, PIPE, STDOUT, Popen
from typing import Optional, Union, Callable
from flexalloc import xnvme_env, FlexAlloc, mm, libflexalloc
from flexalloc.pyutils import loop
from typing import Generator


def subprocess_tail_lines(p: Popen) -> Generator[str, None, None]:
    """Create generator emitting each new line printed by process `p`."""
    buf = []
    while True:
        out = p.stdout.read(1)
        if out != "" and out != "\n":
            buf.append(out)
        elif out == "" and p.poll() is not None:
            if buf:
                yield ''.join(buf)
            break
        elif out == "\n":
            yield ''.join(buf)
            buf = []


def new_process(*args, **kwargs) -> Popen:
    """Construct process with reasonable defaults.

    Constructs a process object whose defaults make it amenable to process
    supervision/tailing using `subprocess_tail_lines`.

    The default options will:
    * interpret process output as UTF-8 formatted textual output (rather than bytes)
    * on encoding errors, replace the faulty character
    * redirect STDERR to STDOUT to capture all output in one unified stream
    * capture STDOUT output (STDOUT=PIPE)
    """
    return Popen(
        *args,
        **{**{
            "stdout": PIPE,
            "stderr": STDOUT,
            "shell": False,
            "encoding": "utf-8",
            "errors": "replace"
        },
           **kwargs}
    )


@dataclass
class Device:
    lb_nbytes: int
    nblocks: int
    uri: str

    def __repr__(self):
        return f"{type(self).__name__}<uri: {self.uri}, lb_nbytes: {self.lb_nbytes}, nblocks: {self.nblocks}>"

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


def temp_file_path() -> Path:
    tmp = NamedTemporaryFile(delete=True)
    tmp.close()
    return Path(tmp.name)


DeviceContext = Generator[Device, None, None]
DeviceContextThunk = Callable[[], Generator[Device, None, None]]


@contextmanager
def _dev_loop(size_mb: int, block_size_bytes: int) -> DeviceContext:
    with temp_backing_file(block_size=f"{size_mb}M", count=1) as file:
        loop_path: Path = loop.setup_loop(file, block_size=block_size_bytes)
        try:
            dev = xnvme_env.XnvmeDev(str(loop_path))
            lb_nbytes = xnvme_env.xne_dev_lba_nbytes(dev)
            nblocks = int(xnvme_env.xne_dev_tbytes(dev) / lb_nbytes)
            dev.close()
            yield Device(
                lb_nbytes=lb_nbytes,
                nblocks=nblocks,
                uri=str(loop_path),
            )
        finally:
            loop.remove_loop(loop_path)


def dev_loop(size_mb: int, block_size_bytes: int) -> DeviceContextThunk:
    """Setup loop device for the duration of the context manager.

    Sets up a loop device of capacity `size_mb` MiB and whose logical blocks are
    of size `block_size_bytes` bytes.
    The loop device and underlying backing file are automatically released upon
    exiting the context manager's scope.

    Args:
        size_mb:
        block_size_bytes:

    Returns:
        thunk function - when invoked, the device is created.
    """
    return lambda: _dev_loop(size_mb=size_mb, block_size_bytes=block_size_bytes)


@contextmanager
def _dev_hw(min_size_mb: int,
           device: Optional[Union[str, Path]] = None,
           device_envvar: Optional[str] = None) -> DeviceContext:
    # need access to the original, provided value to determine whether device
    # value was explicitly specified or resolved by examining an environment variable
    device_arg = device
    device_envvar = device_envvar or "FLEXALLOC_TEST_DEVICE"

    if not device:
        device = os.environ.get(device_envvar)
        if not device:
            raise ValueError(
                f"no device to use - no device specified and {device_envvar} unset"
            )
    if not isinstance(device, Path):
        device = Path(device)

    if not device.exists():
        if device_arg:
            raise RuntimeError(f"specified device '{device_arg}' does not exist!")
        raise RuntimeError(f"device '{device}' resolved from ENV var '{device_envvar}' does not exist!")

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
    yield Device(
        lb_nbytes=lb_nbytes, nblocks=nblocks, uri=str(device.absolute())
    )


def dev_hw(min_size_mb: int,
           device: Optional[Union[str, Path]] = None,
           device_envvar: Optional[str] = None) -> DeviceContextThunk:
    """Get `Device` reference to hardware device for duration of context.

    Provide `Device` reference for use during the context manager's scope.
    The physical device may either be specified or resolved from an environment variable
    which can be overridden by setting `device_envvar`.

    Args:
        min_size_mb:
        device:
        device_envvar:

    Returns:
        a thunk function which starts the work and returns a context manager
        when invoked.
    """
    return lambda: _dev_hw(min_size_mb=min_size_mb,
                           device=device,
                           device_envvar=device_envvar)


DiskFormatFn = Callable[[Device,], None]


def format_fla_mkfs(npools: int, slab_nlb: int) -> DiskFormatFn:
    def do_mkfs(device: Device) -> None:
        mm.mkfs(device.uri, npools, slab_nlb)

    return do_mkfs


@contextmanager
def _fla_open_direct(device: DeviceContextThunk,
                     formatter: Optional[DiskFormatFn] = None,
                     md_device: Optional[DeviceContextThunk] = None,
                     md_formatter: Optional[DiskFormatFn] = None) -> Generator[FlexAlloc, None, None]:
    """Open FlexAlloc system directly.

    NOTE: `device` and `md_device` may point to loop devices which are released upon exiting the scope.
          therefore, do not attempt to re-open an already initialized system!

    Args:
        device: device containing the FlexAlloc system (or the data, if a metadata device is provided)
        formatter: (optional) routine to use for formatting the `device`
        md_device: (optional) if provided, treat this as the device containing the FlexAlloc system's metadata.
        md_formatter: (optional) routine to use for formatting the `md_device`, if provided.

    Returns:
        None, however, context manager *yields* a `FlexAlloc` system instance.
    """

    # if no md device is provided, use a NOOP context manager
    md_device = md_device if md_device is not None else ctx_suppress
    with md_device() as md_device:
        if md_formatter:
            if md_device is None:
                raise RuntimeError("provided a function to format a metadata device, but `md_device` gave None, not a device")
            md_formatter(md_device)

        with device() as device:
            if formatter:
                formatter(device)

            fs = None
            try:
                if md_device:
                    fs = libflexalloc.md_open(device.uri, md_device.uri)
                else:
                    fs = libflexalloc.open(device.uri)
                yield fs
            finally:
                if fs:
                    libflexalloc.close(fs)


def fla_open_direct(device: DeviceContextThunk,
                    formatter: Optional[DiskFormatFn] = None,
                    md_device: Optional[DeviceContextThunk] = None,
                    md_formatter: Optional[DiskFormatFn] = None) -> Callable[[], Generator[FlexAlloc, None, None]]:
    return lambda: _fla_open_direct(
        device=device,
        formatter=formatter,
        md_device=md_device,
        md_formatter=md_formatter
    )


# TODO: extend support to also take a metadata device and formatter
#       A prerequisite of this change is to extend the daemon program to also
#       support opening a device and separate metadata device.
@contextmanager
def _fla_daemon(device: DeviceContextThunk,
               formatter: Optional[DiskFormatFn] = None) -> Generator[Path, None, None]:
    """Start FlexAlloc daemon instance in the background and yield path to its UNIX socket.

    Takes the provided device and formatter, opening the FlexAlloc system in daemon-mode
    and yielding a `Path` object pointing to the UNIX socket which the daemon is listening on.
    Upon exiting the context-provider scope, the daemon is automatically shut down and the
    underlying device is released.

    Args:
        device: context-provider providing a `Device` instance for the daemon to operate on.
        formatter: (optional) function specifying how to format `device`.

    Returns:
        None, the context manager *yields* a Path object pointing to the UNIX socket
        the daemon is listening on.
    """
    with device() as device:
        if formatter:
            formatter(device)

        daemon = None

        meson_build_root = os.environ.get("MESON_BUILD_ROOT")
        if not meson_build_root:
            raise RuntimeError("required environment variable MESON_BUILD_ROOT is not defined!")

        daemon_program_path = Path(meson_build_root) / "flexalloc_daemon"
        if not daemon_program_path.exists():
            raise RuntimeError(f"could not find daemon program in '{daemon_program_path}'")

        try:
            socket_path: Path = temp_file_path()
            daemon = new_process([
                str(daemon_program_path),
                "-d", device.uri,
                "-s", str(socket_path)
            ])

            daemon_ready = False
            for line in subprocess_tail_lines(daemon):
                # TODO: can get stuck here indefinitely - should have a timeout check
                if line.startswith("daemon ready for connections"):
                    daemon_ready = True
                    break
            if not daemon_ready:
                raise RuntimeError("failed to start daemon")
            yield socket_path
        finally:
            if not daemon:
                return
            daemon.send_signal(signal.SIGINT)
            daemon.wait(timeout=10)


def fla_daemon(device: DeviceContextThunk,
               formatter: Optional[DiskFormatFn] = None) -> Callable[[], Generator[Path, None, None]]:
    return lambda: _fla_daemon(device=device, formatter=formatter)


@contextmanager
def _fla_daemon_client(socket: Path) -> Generator[libflexalloc.FlexAllocDaemonClient, None, None]:
    client = None
    try:
        client = libflexalloc.daemon_open(socket)
        yield client
    finally:
        libflexalloc.close(client.fs)


def fla_daemon_client(socket: Path) -> Callable[[], Generator[libflexalloc.FlexAllocDaemonClient, None, None]]:
    return lambda: _fla_daemon_client(socket=socket)


@contextmanager
def _fla_open_daemon(device: DeviceContextThunk,
                    formatter: Optional[DiskFormatFn] = None) -> Generator[FlexAlloc, None, None]:
    with fla_daemon(device=device, formatter=formatter)() as daemon_socket_path:
        with fla_daemon_client(daemon_socket_path)() as client:
            yield client.fs


def fla_open_daemon(device: DeviceContextThunk,
                    formatter: Optional[DiskFormatFn] = None) -> Callable[[], Generator[FlexAlloc, None, None]]:
    return lambda: _fla_open_daemon(device=device, formatter=formatter)
