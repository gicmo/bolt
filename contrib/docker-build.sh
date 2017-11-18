#!/bin/bash
set -e
set -x

rm -rf /build
mkdir /build
meson . /build \
    -Denable-werror=true \
    -Denable-doc=true \
    -Denable-man=true \
    -Denable-tests=true \
    -Denable-dummy=true \
    -Denable-thunderbolt=true \
    -Denable-uefi=true \
    -Denable-dell=true \
    -Denable-synaptics=true \
    -Denable-colorhug=true
ninja-build -C /build test
