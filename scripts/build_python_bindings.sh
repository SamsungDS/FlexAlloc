#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -x
set -e
source "${SCRIPT_DIR}/_set_env.sh"

cd "${PY_SOURCE_ROOT}"

# dist dir won't automatically be cleaned.
rm -rf "${PY_DIST_DIR}"

# only clean build dir now before rebuild
# (using it otherwise for running tests etc.)
rm -rf "${PY_BUILD_DIR}"

# build source package
${VENV_PY} setup.py sdist -d "${PY_DIST_DIR}"

# build binary (wheel) package
${VENV_PY} setup.py bdist_wheel -d "${PY_DIST_DIR}" -b "${PY_BUILD_DIR}" -k
