from setuptools import Extension
from typing import List


def _flexalloc_module(sources: List[str]) -> Extension:
    return Extension("", sources, include_dirs=["{FLEXALLOC_SRC}/src"], libraries=["flexalloc"])


# Python dicts retain insertion order.
# Thus modules will be compiled in the order they are defined in this dict.
CYTHON_MODULES = {
    "flexalloc.xnvme_env": _flexalloc_module(["flexalloc/xnvme_env.pyx"]),
    "flexalloc.flexalloc": _flexalloc_module(["flexalloc/flexalloc.pyx"]),
    "flexalloc.hash": _flexalloc_module(["flexalloc/hash.pyx"]),
    "flexalloc.libflexalloc": _flexalloc_module(["flexalloc/libflexalloc.pyx"]),
    "flexalloc.mm": _flexalloc_module(["flexalloc/mm.pyx"]),
}
