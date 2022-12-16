#!/bin/bash
set -e
set -x

# prepare
export LC_ALL=C.UTF-8
export PYTHONPATH="/usr/share/glib-2.0"

rm -rf /build/*

# info
ls -la /build
whoami

# actual building
meson -Dbuildtype=debug -Db_coverage=true -Dtests-speed=slow . /build
ninja -C /build
meson test -C /build --verbose

if [[ -x "$(command -v lcov)" ]]; then
    ninja -C /build coverage
fi

if [[ -x "$(command -v scan-build)" ]]; then
    ninja -C /build scan-build

    if [[ -n "$(ls -A /build/meson-logs/scanbuild/)" ]]; then
        echo "Scan build log found, assuming defects exist"
        exit 1
    fi
fi

if [[ -x "$(command -v lcov)" ]]; then
    scripts/uncrustify.sh --check
fi

if [[ -x $(command -v pylint) ]]; then
    pylint tests/test-integration
fi

if [[ -x $(command -v codespell) ]]; then
    codespell -S .git -S build
fi
