from contextlib import contextmanager
from flexalloc.flexalloc import FlexAlloc
from flexalloc.libflexalloc import IOBuffer


@contextmanager
def io_buffer(fs: FlexAlloc, nbytes: int):
    """Convenience wrapper to ensure IO buffers are freed.

    IO buffers *must* be closed/freed *before* the FlexAlloc system itself
    is closed. This context manager only helps insofar as it ensures that .close()
    is called when the IO buffer goes out of scope."""
    buf = IOBuffer(fs, nbytes)
    try:
        yield buf
    finally:
        buf.close()
