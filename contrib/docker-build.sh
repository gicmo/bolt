#!/bin/bash
set -e
set -x

export LC_ALL=C.UTF-8
export PYTHONPATH="/usr/share/glib-2.0"

rm -rf /build
mkdir /build
meson . /build
ninja -C /build
meson test -C /build --verbose
