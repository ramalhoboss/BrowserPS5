// BrowserPS5's own updates (2026-11-17; browser/update.h): the files the browser and update_after.elf (main.c)
// share, all in one folder beside the browser's data. The browser fetches a release's installer there and checks
// it; the next time it is opened it leaves a request, sends update_after.elf to the console's payload loader and
// leaves as "Sair" does; update_after hands the installer over once the browser is gone.
#pragma once

#define UPDATE_DIR     "/user/homebrew/browserps5-rn/update"
#define UPDATE_ELF     UPDATE_DIR "/BrowserPS5.elf"    // a release's installer, whole
#define UPDATE_READY   UPDATE_DIR "/ready.txt"         // "<version> <bytes> <sha256, 64 hex>\n": what UPDATE_ELF is, checked
#define UPDATE_ASK     UPDATE_DIR "/install-next.txt"  // the same line and " <pid of the app> <pid of its launcher>\n"
#define UPDATE_FAILED  UPDATE_DIR "/failed.txt"        // "<version>\n": one that is not fetched or installed by itself again
#define UPDATE_TRIES   UPDATE_DIR "/tries.txt"         // "<version> <count>\n": the times the browser left for its installer
#define UPDATE_LOG     UPDATE_DIR "/update-log.txt"
#define UPDATE_STAY    UPDATE_DIR "/no-reopen"         // (a test's) the browser is not opened again after it
#define UPDATE_RELEASE "/user/homebrew/browserps5-rn/release.txt"  // the installed release (the installer writes it)
#define UPDATE_TITLE   "PPSA97024"
