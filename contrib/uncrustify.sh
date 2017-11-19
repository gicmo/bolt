#!/bin/sh
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/contrib/uncrustify.cfg"
echo "srcroot: $SRCROOT"
pushd "$SRCROOT"
uncrustify -c "$CFG" --no-backup `git ls-tree --name-only -r HEAD | grep \\\.[ch]$ | grep -v gvdb | grep -v build/`
popd
