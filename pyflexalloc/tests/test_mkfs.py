import pytest
from tests_common import dev_loop
import flexalloc


@pytest.mark.parametrize("loop_dev", [dev_loop(size_mb=10, block_size_bytes=512)])
@pytest.mark.parametrize(
    "npools, slab_nlb, success",
    (
        # fail -- too many pools
        [20, 2000, False],
        [20, 1000, True],
    ),
)
def test_mkfs(loop_dev, npools, slab_nlb, success):
    with loop_dev() as tdev:
        if success:
            flexalloc.mkfs(tdev.uri, npools, slab_nlb)
        else:
            with pytest.raises(RuntimeError):
                flexalloc.mkfs(tdev.uri, npools, slab_nlb)
