if [ -z "${MESON_SOURCE_ROOT}" ] || [ -z "${MESON_BUILD_ROOT}" ]; then
    echo "variables MESON_BUILD_ROOT and MESON_SOURCE_ROOT must be set before running this script"
    exit 1
fi

# resolv MESON_{SOURCE,BUILD}_ROOT to absolute paths
# this in turn renders all paths derived from these* absolute.
# This, in turn, avoids errors where we change the CWD and execute a command, only to have it
# fail because the (relative) paths to the various commands are then invalid.
# (derived path vars: PY_{SOURCE,BUILD}_ROOT, VIRTUAL_ENV, VENV_{BIN,PY,PIP})
export MESON_SOURCE_ROOT="`readlink -e $MESON_SOURCE_ROOT`"
export MESON_BUILD_ROOT="`readlink -e $MESON_BUILD_ROOT`"

export PY_SOURCE_ROOT="${MESON_SOURCE_ROOT}/pyflexalloc"
export PY_BUILD_ROOT="${MESON_BUILD_ROOT}/pyflexalloc"
export VIRTUAL_ENV="${PY_SOURCE_ROOT}/.venv"

PYTHON_BIN="${PYTHON_BIN:-python3}"

export VENV_BIN="${VIRTUAL_ENV}/bin"
export VENV_PY="${VENV_BIN}/python3"
export VENV_PIP="${VENV_BIN}/pip"

create_env() {
    ${PYTHON_BIN} -m venv "${VIRTUAL_ENV}"
    # TODO: move out, install dependencies iff requirements.txt changed
    # (possibly nuke and re-create, TBH)
    $VENV_PIP install -r "${PY_SOURCE_ROOT}/requirements.txt"
    cp "${PY_SOURCE_ROOT}/requirements.txt" "${VIRTUAL_ENV}/requirements.txt"
    $VENV_PIP install -r "${PY_SOURCE_ROOT}/requirements.dev.txt"
    cp "${PY_SOURCE_ROOT}/requirements.dev.txt" "${VIRTUAL_ENV}/requirements.dev.txt"
}

# Create virtual environment if:
# * does not exist
# * requirements.txt or requirements.dev.txt have changed since venv was created.
if [ ! -d "${VIRTUAL_ENV}" ]; then
    create_env
elif ! diff "${PY_SOURCE_ROOT}/requirements.txt" "${VIRTUAL_ENV}/requirements.txt" 2>&1 >/dev/null; then
    rm -rf "${VIRTUAL_ENV}"
    create_env
elif ! diff "${PY_SOURCE_ROOT}/requirements.dev.txt" "${VIRTUAL_ENV}/requirements.dev.txt"; then
    rm -rf "${VIRTUAL_ENV}"
    create_env
fi

export LD_LIBRARY_PATH="${MESON_BUILD_ROOT}:$LD_LIBRARY_PATH"
export LIBRARY_PATH="${MESON_BUILD_ROOT}:$LIBRARY_PATH"
export PY_BUILD_DIR="${PY_BUILD_ROOT}/build"
export PY_DIST_DIR="${PY_BUILD_ROOT}/dist"
