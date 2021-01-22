import sys
import os
from pathlib import Path
from typing import Dict, Optional, Union, List
from setuptools.extension import Extension
from scriptutils.cli import Log
from scriptutils import coerce, colors
from scriptutils.flexalloc_modules_conf import CYTHON_MODULES


# import Cython or provide an informative error message and exit
try:
    from Cython.Build import cythonize
except ImportError:
    print(f"{colors.YELLOW}{colors.B_CLR}\n\nFailed to import Cython!{colors.CLR}")
    print(
        f"{colors.YELLOW}Be sure to operate from within a virtual environment and to have installed"
    )
    print(f"all dependencies of this package.{colors.CLR}")
    sys.exit(1)


# ensure necessary env var is set for out-of-source-tree build.
if not "PY_BUILD_ROOT" in os.environ:
    raise RuntimeError("This setup.py is written to support out-of-tree builds, you MUST provide a BUILD_ROOT variable pointing to where you'd want to build to be placed")

PY_BUILD_ROOT = Path(os.environ.get("PY_BUILD_ROOT"))
print(f"{colors.B_MAGENTA}PY_BUILD_ROOT: {PY_BUILD_ROOT}{colors.CLR}")


# Put options affecting the compilation here and add where appropriate.
# E.g. CYTHON_ANNOTATE corresponds to the Cython.Compiler.Options.annotate option,
# given to cythonize as a keyword argument.
# For more cython compiler options, refer to:
#    https://cython.readthedocs.io/en/latest/src/userguide/source_files_and_compilation.html#compiler-options
ENV_DEFAULTS = {
    "CFLAGS": "-O3",
    # if true an annotated HTML file is built for each pyx file, showing the degree
    # to which the code depends on the Python interpreter.
    "CYTHON_ANNOTATE": "TRUE",
}


def build_extensions_list(
        modules: Dict[str, Union[List[str], Extension]], ctx: Optional[Dict[str, str]]
) -> List[Extension]:
    def fmt_list_args(e: Extension, attr: str):
        if not hasattr(e, attr):
            raise ValueError(f"attr '{attr}' does not exist in Extension")
        setattr(e, attr, [e.format(**ctx) for e in getattr(e, attr)])

    def to_extension(module_name: str, entry: Union[List[str], Extension]) -> Extension:
        if isinstance(entry, Extension):
            # set extension name
            entry.name = module_name

            # expand variables in the following attributes
            # can, for example, refer to files relative to the FLEXALLOC_SRC
            # directory in this way.
            for attr in [
                "sources",
                "include_dirs",
                "library_dirs",
                "runtime_library_dirs",
                "extra_objects",
            ]:
                try:
                    fmt_list_args(entry, attr)
                except KeyError as e:
                    raise RuntimeError(
                        f"""value in '{attr}' of extension for module {module_name} refers to undefined variable {e.args[0]}"""
                    ) from e
            return entry
        elif isinstance(entry, list):
            # create an extension
            return Extension(module_name, [e.format(**ctx) for e in entry])
        else:
            raise ValueError(
                f"unexpected entry type ({type(entry).__name__}) - {repr(entry)}"
            )

    return [to_extension(mod_name, files) for mod_name, files in modules.items()]


def build(setup_kwargs):
    print(
        f"{colors.B_CLR}> {colors.GREEN}Build flags, override using ENV variables{colors.CLR}:"
    )
    env = {k: os.environ.get(k, v) for k, v in ENV_DEFAULTS.items()}
    for env_key, env_val in env.items():
        print(
            f"  {colors.B_WHITE}* {colors.MAGENTA}{env_key}{colors.WHITE}: {colors.BLUE}{env_val}{colors.CLR}"
        )
    print("")

    # validate FLEXALLOC_SRC environment variable
    flexalloc_src_dir = Path(__file__).parent.parent
    print(f"{colors.B_CLR}> {colors.MAGENTA}FLEXALLOC_SRC{colors.B_CLR}:{colors.BLUE}{flexalloc_src_dir}{colors.CLR}")
    flexalloc_invalid_prefix = f"{colors.MAGENTA}FLEXALLOC_SRC{colors.CLR} invalid, {colors.BLUE}{flexalloc_src_dir}{colors.CLR}"
    if not flexalloc_src_dir.exists():
        Log.err(f"{flexalloc_invalid_prefix} does not exist")
        sys.exit(3)
    elif not flexalloc_src_dir.is_dir():
        Log.err(f"{flexalloc_invalid_prefix} is not a directory!")
        sys.exit(3)

    extensions = build_extensions_list(
        CYTHON_MODULES, {"FLEXALLOC_SRC": str(flexalloc_src_dir)}
    )
    for ext in extensions:
        print(f"{ext.name}:")
        for file in ext.sources:
            print(f"  {file}")

    setup_kwargs.update(
        {
            "ext_modules": cythonize(
                extensions,
                # Cython compiler options
                # See note above 'ENV_DEFAULTS' for more information
                language_level=3,
                annotate=coerce.to_bool(env["CYTHON_ANNOTATE"]),
                build_dir=str(PY_BUILD_ROOT / "tmp" / "cython"),
            ),
            "options": {
                "build": {
                    "build_lib": str(PY_BUILD_ROOT / "tmp" / "lib"),
                    "build_temp": str(PY_BUILD_ROOT / "tmp" / "temp"),
                }},
        }
    )
