# Building
**Dependencies**
* Flexalloc depends on the following projects
  - [xnvme](https://github.com/OpenMPDK/xNVMe) - a library for flexible access to storage device
  - [meson](https://mesonbuild.com) - build system leveraged by flexalloc. [How to install](#installmesonninja)
  - [ninja](https://pypi.org/project/ninja/) - small build system leveraged by meson. [How to install](#installmesonninja)


**Obtain Source**
```shell
git clone https://github.com/OpenMPDK/FlexAlloc
cd flexalloc
```

**Build**
```shell
meson build
meson compile -C build
```

**Install**
```shell
meson install -C build
```

**Running tests**
```shell
meson test -C build
```

# Conventions:
**Code Formating**
* Execute the astyle script to format your code properly.

**Return Value**
* Always return int as error: In general methods should return an int where
  zero means that no errors occured and any other value means an error.
  - If errno is detected or set within the function it should be returned as -errno
  - If an error different from what errno defines is detected or set it should be
    returned as a positive number.

* Return void when there are no errors to handle: Use void when there are no
  errors to handle within the method. This is quite rare but happens when you for
  example wrap a simple free.

**Naming**
* Prefix "fla_" for functions and variables : In general, functions that might
  involve a name clash must start with "fla_". This includes API functions.

* When creating a regression tests you should include "_rt_" in the name, in the
  same way include "_ut_" when creating unit tests.

**Error handling**
* For every error encountered the `FLA_ERR_*` methods should be used. These will
  print an error message to stderr and set the appropriate return value.
  Using these macros allows us to control the message format and how these get
  output.
* Regular execution of libflexalloc should NOT output anything to
  std{out,err,in}.  Only when there is an error should the stderr be used. This
  is true only when FLA_VERBOSITY is set to 0.
* Where desired, use the `FLA_DBG_\*` macros. The output will then be shown when
  FlexAlloc is compiled with debugging enabled, such as the unit- and regression tests.

**Feature Test Macros**
* No thank you

**Documentation**
* We use doxygen for all comments in source. It is prefered for the documentation
to include all subsections : general comments, arguments, variables, return
values etc. Documentation can be forgone or reduced when what is being
documented has high probability of changing (avoid documenting in vain). Reduced
documentation means that only a small doxygen formated comment can be included.

**Versioning**
* We follow semantic versioning. https://semver.org/.

**Compile Options**
* To set the options you can either do it at setup or configure time. In both cases you need
  to add the -Doption=value to the command line in order to set the value.
* FLA_VERBOSITY is an int options that can either be 0 or 1. 0 will show no extra output.
  1 means that messages called with FLA_VBS_PRINTF will be shown. It is set at 0 by default.

# Troubleshooting

**<a name="installmesonninja"></a>Cannot install messon or ninja**
* You will need the [meson](https://mesonbuild.com) build system. The easiest way to
get the latest meson package is through [pipx](https://pipxproject.github.io/pipx/) like so:
```shell
# (replace 'python3' with 'python' if this is your Python 3 binary)
python3 -m pip install --user pipx
python3 -m pipx ensurepath

# Afterward, install `meson` and `ninja` into their own virtual environment:
pipx install meson
pipx runpip meson install ninja
```

* If you see errors indicating that you miss `venv` or `virtualenv`. In some Distribution,
this Python module is shipped separately. For Ubuntu/Debian you will need to install the
`python3-venv` package.

**Cannot open loopback-control for writing**
* Most tests create a loopback device in lieu of using a real hardware device.
  To do this, your user must be able to create loopback devices. Try the following and if
  you don't see `permission denied`, you are ready to run the tests.
```shell
# Create an image file to test with:
dd if=/dev/zero of=/tmp/loop1.img bs=1M count=10

# Mount the image as a loopback device:
losetup -f /tmp/loop1.img
```

* If you see `permission denied`, check the permissions of the `/dev/loop-control` file.
```shell
$ ls -la /dev/loop-control 
crw-rw---- 1 root disk 10, 237 Mar 17 13:06 /dev/loop-control
```
  - In this case, the `disk` group also has read-write access. In this case, run the
    following command to add your user to the `disk` group:
```sell
usermod -aG disk `whoami`
```
  - Log out and then back into your desktop environment or run `newgrp disk` in the
    relevant terminal window for the permissions change to take effect.


