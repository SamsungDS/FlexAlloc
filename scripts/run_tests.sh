#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -x
set -e

export MESON_SOURCE_ROOT="${SCRIPT_DIR}/../"
source "${SCRIPT_DIR}/_set_env.sh"

cd "${PY_SOURCE_ROOT}"

# modify PYTHONPATH to use build directory copy of the module
# Run all tests in:
#   MESON_BUILD_ROOT: contains the wrapped C tests
#   (pyflexalloc/)tests: contains the python bindings tests
PYTHONPATH="${PY_BUILD_DIR}" ${VENV_BIN}/pytest "${MESON_BUILD_ROOT}" tests
