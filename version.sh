#! /bin/sh

# Copyright (c) 2015 Timothe Litt litt at acm ddot org
# All rights reserved
#
# This software is provided under GPL V2, including its disclaimer of
# warranty.  Licensing under other terms may be available from the author.

if [ -d .git ]; then
    NV=$((`git log | grep -P '^commit' | wc -l` + 1))
    sed -e"s/\\(#define VERSION_EDIT\\) .*\$/\\1 $NV/g" version.h >version.h.tmp
    if ! diff -q version.h.tmp version.h >/dev/null 2>&1 ; then
        cp version.h.tmp version.h
    fi
    rm version.h.tmp
fi
