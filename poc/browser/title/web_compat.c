/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

/*
 * libc functions the title has no import for, needed once WebKit and Mesa
 * are linked in (WEBKIT=1 build.sh; ../../webkit_title/compat.c has the
 * others). Each is a path these never take in the browser: RADV's shader
 * disassembly to a temporary file (mkstemp), Mesa's memory streams
 * (tmpfile, only where open_memstream fails) and its X11 software
 * displays (System V shared memory).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int mkostemp(char *template_, int flags);

int mkstemp(char *template_)
{
    return mkostemp(template_, 0);
}

FILE *tmpfile(void)
{
    errno = ENOSYS;
    return NULL;
}

int shmget(long key, size_t size, int flags)
{
    (void)key;
    (void)size;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

void *shmat(int id, const void *address, int flags)
{
    (void)id;
    (void)address;
    (void)flags;
    errno = ENOSYS;
    return (void *)-1;
}

int shmctl(int id, int command, void *buffer)
{
    (void)id;
    (void)command;
    (void)buffer;
    errno = ENOSYS;
    return -1;
}

int shmdt(const void *address)
{
    (void)address;
    errno = ENOSYS;
    return -1;
}
