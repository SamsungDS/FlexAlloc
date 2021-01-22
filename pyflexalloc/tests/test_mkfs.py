import pytest
from functools import partial
from tests_common import loop_device
import flexalloc


@pytest.mark.parametrize("mkdev", [partial(loop_device, 10, 512)])
@pytest.mark.parametrize(
    "npools, slab_nlb, success",
    (
        # fail -- too many pools
        [20, 2000, False],
        [20, 1000, True],
    ),
)
def test_mkfs(mkdev, npools, slab_nlb, success):
    with mkdev() as tdev:
        if success:
            flexalloc.mkfs(tdev.dev_uri, npools, slab_nlb)
        else:
            with pytest.raises(RuntimeError):
                flexalloc.mkfs(tdev.dev_uri, npools, slab_nlb)
