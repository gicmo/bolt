#!/bin/sh

if [ -z $MESON_INSTALL_PREFIX ]; then
    echo 'This is meant to be run by meson'
    exit 1
fi

BOLT_DBDIR=$1

echo "Creating database dir: ${BOLT_DBDIR}"
mkdir -p "${DESTDIR}/${BOLT_DBDIR}"
