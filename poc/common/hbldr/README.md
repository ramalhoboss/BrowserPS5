# hbldr helpers

`elfldr.c`, `elfldr.h`, `pt.c` and `pt.h` are copied unchanged from
[ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv)
(`src/ps5/`, commit 7748df9d2a43), © 2024 John Törnblom, GPLv3 (see LICENSE).

They are websrv's variant of elfldr: besides loading an ELF into another
process, it can set that process's stdio, cwd, environment and heap size,
which [../../bigapp_gl/main.c](../../bigapp_gl/main.c) needs to run a
payload inside an app process the way websrv's `hbldr.c` does.

They are kept apart from [../elfldr/](../elfldr/) (the modified upstream
elfldr used by spawn_ipc) because both define the same function names.
