#!/bin/bash
# Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
# qr.js against segno (a QR library for Python), module by module: 70 texts,
# the four levels, versions 1 to 10 and the eight masks. Run from Git Bash on
# the PC: node for Windows, segno in WSL's tools venv
#   (~/browserps5-build/ext/tools-venv/bin/python -m pip install segno==1.6.6).
# Expected (2026-11-05): "same 43, mask choice only 13, different 14", the 14
# being only texts too long for version 10 (qr.js stops there on purpose).
# Another mask choice is fine: any mask makes a valid code.
set -e
here=$(cd "$(dirname "$0")" && pwd)   # /c/...
wsl_here="/mnt/c${here#/c}"
"/c/Program Files/nodejs/node" "$(cygpath -w "$here/node_check.js")" \
  "$(cygpath -w "$here/../qr.js")" "$(cygpath -w "$here/cases.json")" \
  "$(cygpath -w "$here/ours.json")"
wsl -d Ubuntu-24.04 -e bash -lc \
  "~/browserps5-build/ext/tools-venv/bin/python '$wsl_here/segno_check.py' '$wsl_here/ours.json'"
rm -f "$here/ours.json"
