/*   Copyright (C) 2026 BrowserPS5 contributors
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * "Extrair" for a downloaded .zip (Definições, Transferências;
 * docs/16-abrir-e-extrair.md): its files into a folder of their own, with
 * zlib. Stored and deflated entries, ZIP64 (over 4 GiB). Nothing is ever
 * written outside the folder: absolute names, "..", drive letters and
 * symbolic links are skipped, and an entry may not be bigger than it says.
 * Encrypted entries and other methods (bzip2, LZMA, zstd) are skipped and
 * counted. No RAR (its licence).
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  UNZIP_OK = 0,
  UNZIP_ERR_OPEN = -1,      // the .zip could not be read
  UNZIP_ERR_FORMAT = -2,    // not a ZIP, or one in parts, or broken
  UNZIP_ERR_SPACE = -3,     // its files do not fit in the free space
  UNZIP_ERR_WRITE = -4,     // a file or folder could not be written
  UNZIP_ERR_DATA = -5,      // an entry's data is broken (CRC, size)
  UNZIP_ERR_CANCELLED = -6,
};

/* The most entries a ZIP may have here. */
#define UNZIP_ENTRIES_MAX 200000
/* The biggest central directory read (all the names). */
#define UNZIP_DIRECTORY_MAX (64 << 20)
/* Left free on the drive after the files. */
#define UNZIP_SPACE_MARGIN (64LL << 20)

typedef struct unzip_result {
  int files;            // written
  int dirs;             // made
  int skipped;          // encrypted, other methods, links, unsafe names
  long long bytes;      // written
  long long total;      // to write (the entries taken)
  char entry[256];      // the entry an error is about, or ""
} unzip_result_t;

/* Called about every MiB written (and at the end): done of total bytes.
 * Returns non-zero to stop (UNZIP_ERR_CANCELLED). May be 0. */
typedef int (*unzip_progress_fn)(void* ctx, long long done, long long total);

/* An entry's name as a path inside the folder: '\\' as '/', "." and
 * empty parts dropped, characters the drives refuse as '_', bytes that are
 * not UTF-8 as '_', each part at most 255 bytes. 0 and the path in out, or
 * -1 for a name that would leave the folder (absolute, "..", "C:") or is
 * empty. */
int unzip_safe_name(const char* in, size_t length, char* out, size_t size);

/* The folder a .zip goes into: its name without ".zip" (and the like),
 * cleaned as above, "zip" if nothing is left. */
void unzip_folder_name(const char* zip_name, char* out, size_t size);

/* Extracts zip_path into dest_dir (made already, empty). free_bytes is the
 * room on its drive (-1: not known, not checked). UNZIP_OK or an error;
 * result says what was done (an error leaves what was written before it,
 * but not the entry it happened in). */
int unzip_extract(const char* zip_path, const char* dest_dir, long long free_bytes,
                  unzip_progress_fn progress, void* ctx, unzip_result_t* result);

#ifdef __cplusplus
}
#endif
