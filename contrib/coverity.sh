#!/bin/bash
set -e
set -x

GIT_DESC=$(git describe)

env

# prepare
export LC_ALL=C.UTF-8
export PYTHONPATH="/usr/share/glib-2.0"

rm -rf /build/*

# info
ls -la /build
whoami

# actual building
export CC=clang CXX=clang
meson -Dcoverity=true /build
cov-build --dir /build/cov-int ninja -C /build
pushd /build
tar -caf coverity.xz cov-int
popd

if [[ -v COVERITY_TOKEN && -v COVERITY_EMAIL ]]; then

    curl --form "token=${COVERITY_TOKEN}" \
	 --form "email=${COVERITY_EMAIL}" \
	 --form "file=@/build/coverity.xz" \
	 --form "version=main" \
	 --form "description=${GIT_DESC}" \
	 https://scan.coverity.com/builds?project=gicmo%2Fbolt
fi
