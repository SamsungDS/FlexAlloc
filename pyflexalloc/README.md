# FlexAlloc Python Wrapper

This project uses Python's `pip` and `venv` modules to manage third-party dependencies, and `setuptools` to create the Python binary wheel and source packages.

The bindings themselves use the [Cython](https://cython.org/) compiler.


## Getting started
The easiest way to build the project is as part of building the C library. From the parent directory, issue

```
# create a build directory
$ meson setup my-build-dir

# compile C library
# compile Python bindings
# create Python packages
$ meson compile -C my-build-dir
```

If the process succeeds, then:
* `my-build-dir/python/dist` contains the binary (whl) and source packages
* `.venv` contains a initialized virtual environment which you may use during development

## Running tests
Tests use [pytest](https://pytest.org), but must be able to locate the C library and must run within the Python virtual environment to not depend on system packages.

To run all tests, issue the following command:
```
meson compile -C my-build-dir run-tests
```

This (re)compiles the C library and Python bindings, and ensures both the C and Python-based tests are run.

## Trouble-shooting
### Python programs crash when importing the binding package
The bindings themselves rely on the C library being available. On Linux systems, this can be achieved either by installing it system-wide using `meson install -C my-build-dir` or instruct the linker to look in the build directory by prefixing commands with `LD_LIBRARY_PATH=/path/to/my-build-dir`.
