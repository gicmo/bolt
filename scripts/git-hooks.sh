#!/bin/bash
#
# git hooks management

# global setup
set -u
SRCDIR=${MESON_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. >/dev/null 2>&1 && pwd)}
HOOKDIR="$SRCDIR/scripts/git-hooks/"
DESTDIR="$SRCDIR/.git/hooks"

cd "$SRCDIR"
declare -a HOOKS=()
readarray -t HOOKS < <(find "$HOOKDIR" -maxdepth 1 -type f -exec basename {} \;)

# helpers
ensure_destdir () {
    if [ ! -d "$DESTDIR" ]; then
        echo "$DESTDIR does not exist"
        exit 2
    fi
}

# individual commands
check () {
    ensure_destdir

    for hook in ${HOOKS[@]}; do
        echo -n "$hook "
        DEST="$DESTDIR/$hook"
        if [ -x "$DEST" ]; then
            echo "installed"
        else
            echo "missing"
        fi
    done
}

install () {
    ensure_destdir

    RESULT=0
    for hook in ${HOOKS[@]}; do
        DEST="$DESTDIR/$hook"
        if [ -x "$DEST" ]; then
            continue
        fi

        cp -p "$HOOKDIR/$hook" "$DEST"
        chmod a+x "$DEST"
        ((RESULT -= 1))
        echo "Installed '$hook' hook"
    done
    exit $RESULT
}

uninstall () {
    ensure_destdir

    for hook in ${HOOKS[@]}; do
        DEST="$DESTDIR/$hook"
        if [ ! -x "$DEST" ]; then
            continue
        fi

        rm "$DEST"
        echo "Uninstalled '$hook' hook"
    done
}

# main

usage_error () {
    echo "usage: $0 <check|install|uninstall>"
    exit 1
}

if [ "$#" -ne 1 ]; then
    usage_error
fi

case "$1" in
    check)
        check
        ;;
    install)
        install
        ;;
    uninstall)
        uninstall
        ;;
    *)
        usage_error
        ;;
esac
