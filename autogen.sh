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

enable_gtk_doc=--enable-gtk-doc
gtkdocize || {
    echo "No gtk-doc support found. You can't build the docs."
    # rm because gtk-doc.make might be a link to a protected file
    rm -f gtk-doc.make
    # Those need to be defined because the upstream Makefile boilerplate
    # (doc/reference/Makefile.am) relies on them.
    cat > gtk-doc.make <<EOF
EXTRA_DIST =
CLEANFILES =
EOF
    enable_gtk_doc=
}

autoreconf --verbose --force --install || exit 1

cd "$olddir"

if [ "$NOCONFIGURE" = "" ]; then
        $srcdir/configure $enable_gtk_doc "$@" || exit 1

        if [ "$1" = "--help" ]; then
                exit 0
        else
                echo "Now type 'make' to compile $PKG_NAME" || exit 1
        fi
else
        echo "Skipping configure process."
fi
