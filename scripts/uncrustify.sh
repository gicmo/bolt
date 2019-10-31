#!/bin/sh
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/scripts/uncrustify.cfg"
echo "srcroot: $SRCROOT"

case "$1" in
    -c|--check)
	OPTS="--check"
        ;;

    *)
	OPTS="--replace --no-backup"
        ;;
esac

pushd "$SRCROOT"
uncrustify -c "$CFG" $OPTS `git ls-tree --name-only -r HEAD | grep \\\.[ch]$ | grep -v gvdb | grep -v build/`
RES=$?
popd
exit $RES
