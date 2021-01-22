#!/usr/bin/env bash
# Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
set -euo pipefail

root_dir=$(dirname $(realpath $0))
pushd ${root_dir} > /dev/null

hash astyle 2>/dev/null || { echo >&2 "Please install astyle."; exit 1; }

FILES=$(find ../{src,tests} -type f -name *.h -o -name *.c )

astyle --options="../.astylerc" ${FILES} | grep "^Formatted"

popd > /dev/null
exit 0
