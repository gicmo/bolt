#!/bin/bash
set -e
set -x

export PYTHONPATH="/usr/share/glib-2.0"

rm -rf /build
mkdir /build
meson . /build
ninja -C /build
meson test -C /build --verbose
