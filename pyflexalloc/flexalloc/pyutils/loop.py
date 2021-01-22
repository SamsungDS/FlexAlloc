from subprocess import run, PIPE
from pathlib import Path


def setup_loop(file: Path, block_size=512) -> Path:
    """Return Path pointing to created loop device on success, raise error otherwise

    Raises:
        CalledProcessError: if command fails, consult the returncode, stderr and stdout
        attributes to determine the cause.
    """
    ret = run(
        f"losetup -fP -b {block_size} --show {file}",
        shell=True,
        check=True,
        stdout=PIPE,
        stderr=PIPE,
    )
    return Path(ret.stdout.decode("ascii").strip())


def remove_loop(loop: Path) -> None:
    """Remove loop device (if it exists)

    Raises:
        CalledProcessError: if command fails, consult the returncode, stderr and stdout
        attributes to determine the cause.
    """
    if not loop.exists():
        return
    run(f"losetup -d {loop}", shell=True, check=True, stdout=PIPE, stderr=PIPE)
