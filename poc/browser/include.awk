# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# awk -v dir=DIR -f include.awk page.html > page.built.html
# Puts the files named by "<!--#include NAME-->" lines (theme.css,
# icons.svg) in place of those lines, from DIR (Makefile, the browser's
# own pages; docs/07-navegacao-e-definicoes.md). One level: an included
# file's own include lines are kept as they are.
/^[ \t]*<!--#include [^ ]+-->[ \t]*$/ {
    name = $0
    sub(/^[ \t]*<!--#include /, "", name)
    sub(/-->[ \t]*$/, "", name)
    path = (dir == "" ? "." : dir) "/" name
    found = 0
    while ((getline line < path) > 0) {
        print line
        found = 1
    }
    close(path)
    if (!found) {
        print "include.awk: " path ": missing or empty" > "/dev/stderr"
        exit 1
    }
    next
}
{ print }
