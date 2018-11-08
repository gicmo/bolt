#!/bin/bash
set -e
set -x

export LC_ALL=C.UTF-8
export PYTHONPATH="/usr/share/glib-2.0"

rm -rf /build
mkdir /build
meson -Db_coverage=true . /build
ninja -C /build
meson test -C /build --verbose

if [[ -x "$(command -v lcov)" ]]; then
    ninja -C /build coverage
fi
