#!/bin/bash
# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Makes icons.svg, the icons of the browser's own pages, from Microsoft's
# Fluent UI System Icons (MIT license, the icons of Windows 11), fetched
# into ~/browserps5-build/ext/fluent-icons (docs/07-navegacao-e-
# definicoes.md). One <symbol> an icon, id "i-<name>" (regular) and
# "i-<name>-f" (filled), drawn with the text colour. Run it again after
# fetching more icons; icons.svg is kept in the project.
set -euo pipefail
src=${FLUENT_ICONS:-$HOME/browserps5-build/ext/fluent-icons}
out=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/icons.svg
{
    echo '<!-- Fluent UI System Icons, Copyright (c) 2020 Microsoft Corporation, MIT'
    echo '     License (see LICENSE-fluent-icons.txt); made by mkicons.sh. -->'
    echo '<svg xmlns="http://www.w3.org/2000/svg" style="display:none">'
    for f in "$src"/ic_fluent_*_24_*.svg; do
        name=$(basename "$f" .svg)
        name=${name#ic_fluent_}
        style=${name##*_}
        name=${name%_24_*}
        id="i-${name//_/-}"
        [[ $style == filled ]] && id="$id-f"
        d=$(sed -n 's/.*<path d="\([^"]*\)".*/\1/p' "$f" | head -1)
        [[ -n $d ]] || { echo "no path in $f" >&2; exit 1; }
        echo "<symbol id=\"$id\" viewBox=\"0 0 24 24\"><path d=\"$d\"/></symbol>"
    done
    echo '</svg>'
} > "$out"
cp "$src/LICENSE" "$(dirname "$out")/LICENSE-fluent-icons.txt"
echo "$(grep -c '<symbol' "$out") icons in $out"
