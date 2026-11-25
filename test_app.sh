#!/usr/bin/env bash
# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required to run Jobman tests." >&2
    exit 1
fi
exec python3 "$script_dir/CTest/run_all.py" "$@"
