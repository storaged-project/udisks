#!/bin/sh
# Run this to generate all the initial makefiles, etc.
test -n "$srcdir" || srcdir=$(dirname "$0")
test -n "$srcdir" || srcdir=.

olddir=$(pwd)

cd "$srcdir"

(test -f configure.ac) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level project directory"
    exit 1
}

PKG_NAME=$(autoconf --trace 'AC_INIT:$1' configure.ac)

gtkdocize --copy || exit 1
autoreconf --verbose --force --install || exit 1

cd "$olddir"

if [ "$NOCONFIGURE" = "" ]; then
        $srcdir/configure --enable-gtk-doc "$@" || exit 1

        if [ "$1" = "--help" ]; then
                exit 0
        else
                echo "Now type 'make' to compile $PKG_NAME" || exit 1
        fi
else
        echo "Skipping configure process."
fi
