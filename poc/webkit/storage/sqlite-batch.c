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

// sqlite-batch.c (2026-11-11): SQLite's write-ahead log, written in blocks.
//
// On the PS5 a write() to a file costs about 0.75 ms whatever its size (24
// bytes or 64 KiB; a megabyte 5 ms; a read 0.04 ms: poc/io_probe). SQLite
// writes each changed page to the log as two writes (a 24-byte frame header,
// then the page), so a site that stores much (WhatsApp Web syncing a night's
// messages) kept the NetworkProcess's storage queue busy for 20 to 75 s at
// 0.17 cores, and every page waited for it: the first sessionStorage of a
// new tab is a sync message to that queue, and the whole WebProcess stood
// still (docs/28, "O bloqueio do 4.o separador").
//
// This is a VFS over the usual one that keeps the log's writes in memory
// while they follow one another, and writes them out as one block:
//   - when SQLite syncs the log (each commit, with synchronous=FULL);
//   - when SQLite publishes the commit to other connections: it calls
//     xShmBarrier between the two copies of the log's index header
//     (walIndexWriteHdr), and a reader only trusts the new header once both
//     copies match, so the frames are in the file before any other
//     connection or process can look for them;
//   - before a read that needs part of them (a read inside them is served
//     from memory), a truncate, a size query, the close;
//   - when the block is full (256 KiB, up to 4 MiB for a large transaction)
//     or a write is past its end.
// So when a commit returns, its frames are in the file as before (and
// synced as before): only the number of write() calls changes. A crash in
// the middle of a transaction leaves a shorter log, which SQLite's recovery
// reads as it reads any log cut short. The database file itself is written
// as always (checkpoints): nothing of it is kept back.
//
// browserps5_sqlite_batch() makes it the default VFS when
// BROWSERPS5_SQLITE_BATCH=1 (WebCore's SQLiteDatabase.cpp calls it right
// after sqlite3_initialize(); build-webkit.sh).

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#define BATCH_BYTES (256 << 10)  // a block at first
#define BATCH_MOST  (4 << 20)    // and at most: it doubles when a transaction fills it
// The usual VFS's write takes 0x1ffff bytes at most (seekAndWriteFd masks the
// length: a megabyte became 0 bytes and "database or disk is full", the
// first run of batch-test): a block goes out in pieces of 64 KiB.
#define PIECE_BYTES (64 << 10)

typedef struct batch_file batch_file;
struct batch_file {
  sqlite3_file base;     // pMethods: batch_io
  batch_file* next;      // the open logs (list_lock)
  const char* name;      // SQLite keeps it until xClose (0: a temporary file)
  int is_wal;
  pthread_mutex_t lock;  // the block: its connection's thread, and another's barrier
  char* block;
  int room;              // the block's size now
  sqlite3_int64 start;   // where the block goes in the file
  int length;
  // the usual VFS's file follows
};

#define REAL(p) ((sqlite3_file*)((batch_file*)(p) + 1))

static sqlite3_vfs* real_vfs;
static sqlite3_vfs batch_vfs;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static batch_file* logs;

// For the tests and the log: writes SQLite asked for, and writes made.
static unsigned long long asked_writes;
static unsigned long long made_writes;

// Why a block went out (the tests print them).
enum { WHY_CLOSE, WHY_READ, WHY_WRITE, WHY_TRUNCATE, WHY_SYNC, WHY_SIZE, WHY_BARRIER, WHY_COUNT };
static unsigned long long blocks_out[WHY_COUNT];

void
browserps5_sqlite_batch_counts(unsigned long long* asked, unsigned long long* made) {
  *asked = __atomic_load_n(&asked_writes, __ATOMIC_RELAXED);
  *made = __atomic_load_n(&made_writes, __ATOMIC_RELAXED);
}

// close, read, write, truncate, sync, size, barrier: 7 counts.
void
browserps5_sqlite_batch_blocks(unsigned long long* out) {
  for(int i = 0; i < WHY_COUNT; i++) {
    out[i] = __atomic_load_n(&blocks_out[i], __ATOMIC_RELAXED);
  }
}


// With p->lock held.
static int
flush_locked(batch_file* p, int why) {
  int rc = SQLITE_OK;

  if(p->length > 0) {
    __atomic_add_fetch(&blocks_out[why], 1, __ATOMIC_RELAXED);
  }
  for(int at = 0; at < p->length && rc == SQLITE_OK; at += PIECE_BYTES) {
    const int piece = p->length - at < PIECE_BYTES ? p->length - at : PIECE_BYTES;
    rc = REAL(p)->pMethods->xWrite(REAL(p), p->block + at, piece, p->start + at);
    __atomic_add_fetch(&made_writes, 1, __ATOMIC_RELAXED);
  }
  p->length = 0;
  return rc;
}


static int
flush(batch_file* p, int why) {
  int rc = SQLITE_OK;

  if(p->is_wal) {
    pthread_mutex_lock(&p->lock);
    rc = flush_locked(p, why);
    pthread_mutex_unlock(&p->lock);
  }
  return rc;
}


static int
batch_close(sqlite3_file* file) {
  batch_file* p = (batch_file*)file;
  int rc = SQLITE_OK;

  if(p->is_wal) {
    pthread_mutex_lock(&list_lock);
    for(batch_file** at = &logs; *at; at = &(*at)->next) {
      if(*at == p) {
        *at = p->next;
        break;
      }
    }
    pthread_mutex_unlock(&list_lock);
    rc = flush(p, WHY_CLOSE);
    free(p->block);
    p->block = 0;
    pthread_mutex_destroy(&p->lock);
    p->is_wal = 0;
  }
  int closed = REAL(p)->pMethods->xClose(REAL(p));
  return rc != SQLITE_OK ? rc : closed;
}


static int
batch_read(sqlite3_file* file, void* out, int amount, sqlite3_int64 offset) {
  batch_file* p = (batch_file*)file;

  if(p->is_wal) {
    pthread_mutex_lock(&p->lock);
    if(p->length > 0 && offset < p->start + p->length && offset + amount > p->start) {
      if(offset >= p->start && offset + amount <= p->start + p->length) {
        memcpy(out, p->block + (offset - p->start), (size_t)amount);  // all of it is in the block
        pthread_mutex_unlock(&p->lock);
        return SQLITE_OK;
      }
      int rc = flush_locked(p, WHY_READ);
      if(rc != SQLITE_OK) {
        pthread_mutex_unlock(&p->lock);
        return rc;
      }
    }
    pthread_mutex_unlock(&p->lock);
  }
  return REAL(p)->pMethods->xRead(REAL(p), out, amount, offset);
}


static int
batch_write(sqlite3_file* file, const void* data, int amount, sqlite3_int64 offset) {
  batch_file* p = (batch_file*)file;
  int rc = SQLITE_OK;
  char* grown = 0;

  if(!p->is_wal) {
    return REAL(p)->pMethods->xWrite(REAL(p), data, amount, offset);
  }
  __atomic_add_fetch(&asked_writes, 1, __ATOMIC_RELAXED);
  pthread_mutex_lock(&p->lock);
  if(p->length > 0 && offset >= p->start && offset + amount <= p->start + p->length) {
    // Inside the block: SQLite writes a page of the transaction under way
    // over its earlier frame, and the checksums after it again at the commit.
    memcpy(p->block + (offset - p->start), data, (size_t)amount);
  } else if(p->length > 0 && offset == p->start + p->length && p->length + amount <= p->room) {
    memcpy(p->block + p->length, data, (size_t)amount);  // it follows the last one
    p->length += amount;
  } else if(p->length > 0 && offset == p->start + p->length && p->room < BATCH_MOST
            && (grown = (char*)realloc(p->block, (size_t)p->room * 2))) {
    // A transaction larger than the block: a larger block, so that what
    // SQLite writes again of it (above) is still in memory.
    p->block = grown;
    p->room *= 2;
    memcpy(p->block + p->length, data, (size_t)amount);
    p->length += amount;
  } else if(p->length > 0 && offset + amount <= p->start) {
    // Before the block (the same, for a frame that is in the file already):
    // straight to the file, and the block stays as it is.
    rc = REAL(p)->pMethods->xWrite(REAL(p), data, amount, offset);
    __atomic_add_fetch(&made_writes, 1, __ATOMIC_RELAXED);
  } else {
    rc = flush_locked(p, WHY_WRITE);
    if(rc == SQLITE_OK && !p->block && amount <= PIECE_BYTES) {
      p->block = (char*)malloc(BATCH_BYTES);
      p->room = p->block ? BATCH_BYTES : 0;
    }
    if(rc == SQLITE_OK) {
      if(amount > PIECE_BYTES || !p->block) {  // larger than SQLite writes, or no memory: as usual
        rc = REAL(p)->pMethods->xWrite(REAL(p), data, amount, offset);
        __atomic_add_fetch(&made_writes, 1, __ATOMIC_RELAXED);
      } else {
        memcpy(p->block, data, (size_t)amount);
        p->start = offset;
        p->length = amount;
      }
    }
  }
  pthread_mutex_unlock(&p->lock);
  return rc;
}


static int
batch_truncate(sqlite3_file* file, sqlite3_int64 size) {
  batch_file* p = (batch_file*)file;
  int rc = flush(p, WHY_TRUNCATE);
  return rc != SQLITE_OK ? rc : REAL(p)->pMethods->xTruncate(REAL(p), size);
}


static int
batch_sync(sqlite3_file* file, int flags) {
  batch_file* p = (batch_file*)file;
  int rc = flush(p, WHY_SYNC);
  return rc != SQLITE_OK ? rc : REAL(p)->pMethods->xSync(REAL(p), flags);
}


static int
batch_file_size(sqlite3_file* file, sqlite3_int64* size) {
  batch_file* p = (batch_file*)file;
  int rc = flush(p, WHY_SIZE);
  return rc != SQLITE_OK ? rc : REAL(p)->pMethods->xFileSize(REAL(p), size);
}


static int
batch_lock(sqlite3_file* file, int level) {
  return REAL(file)->pMethods->xLock(REAL(file), level);
}


static int
batch_unlock(sqlite3_file* file, int level) {
  return REAL(file)->pMethods->xUnlock(REAL(file), level);
}


static int
batch_check_reserved_lock(sqlite3_file* file, int* out) {
  return REAL(file)->pMethods->xCheckReservedLock(REAL(file), out);
}


static int
batch_file_control(sqlite3_file* file, int op, void* arg) {
  return REAL(file)->pMethods->xFileControl(REAL(file), op, arg);
}


static int
batch_sector_size(sqlite3_file* file) {
  return REAL(file)->pMethods->xSectorSize(REAL(file));
}


static int
batch_device_characteristics(sqlite3_file* file) {
  return REAL(file)->pMethods->xDeviceCharacteristics(REAL(file));
}


static int
batch_shm_map(sqlite3_file* file, int page, int page_size, int extend, void volatile** out) {
  return REAL(file)->pMethods->xShmMap(REAL(file), page, page_size, extend, out);
}


static int
batch_shm_lock(sqlite3_file* file, int offset, int n, int flags) {
  return REAL(file)->pMethods->xShmLock(REAL(file), offset, n, flags);
}


// SQLite is about to show a commit to the other connections (see the top):
// the frames of this database's log go to the file first. A database opened
// twice in this process has two logs open by the same name: both go out
// (the other one is empty unless a transaction of its own was undone).
static void
batch_shm_barrier(sqlite3_file* file) {
  batch_file* p = (batch_file*)file;

  if(p->name) {
    const size_t n = strlen(p->name);
    pthread_mutex_lock(&list_lock);
    for(batch_file* log = logs; log; log = log->next) {
      if(log->name && !strncmp(log->name, p->name, n) && !strcmp(log->name + n, "-wal")) {
        pthread_mutex_lock(&log->lock);
        flush_locked(log, WHY_BARRIER);  // an error here comes back at the log's next write or sync
        pthread_mutex_unlock(&log->lock);
      }
    }
    pthread_mutex_unlock(&list_lock);
  }
  REAL(p)->pMethods->xShmBarrier(REAL(p));
}


static int
batch_shm_unmap(sqlite3_file* file, int remove) {
  return REAL(file)->pMethods->xShmUnmap(REAL(file), remove);
}


static int
batch_fetch(sqlite3_file* file, sqlite3_int64 offset, int amount, void** out) {
  return REAL(file)->pMethods->xFetch(REAL(file), offset, amount, out);
}


static int
batch_unfetch(sqlite3_file* file, sqlite3_int64 offset, void* page) {
  return REAL(file)->pMethods->xUnfetch(REAL(file), offset, page);
}


static const sqlite3_io_methods batch_io = {
  3,
  batch_close,
  batch_read,
  batch_write,
  batch_truncate,
  batch_sync,
  batch_file_size,
  batch_lock,
  batch_unlock,
  batch_check_reserved_lock,
  batch_file_control,
  batch_sector_size,
  batch_device_characteristics,
  batch_shm_map,
  batch_shm_lock,
  batch_shm_barrier,
  batch_shm_unmap,
  batch_fetch,
  batch_unfetch,
};


static int
batch_open(sqlite3_vfs* vfs, sqlite3_filename name, sqlite3_file* file, int flags, int* out_flags) {
  batch_file* p = (batch_file*)file;

  (void)vfs;
  memset(p, 0, sizeof(*p));
  int rc = real_vfs->xOpen(real_vfs, name, REAL(p), flags, out_flags);
  if(!REAL(p)->pMethods) {
    return rc;  // nothing open: SQLite does not close it
  }
  // The usual VFS's files have all the methods (shared memory, fetch); one
  // that had fewer would be used as it is, with no block.
  if(REAL(p)->pMethods->iVersion < 3) {
    static int said;
    if(!said) {
      said = 1;
      fprintf(stderr, "[WebKit] SQLite: the VFS under the batch one has version %d methods\n",
              REAL(p)->pMethods->iVersion);
    }
  }
  p->base.pMethods = &batch_io;
  p->name = name;
  if(rc == SQLITE_OK && (flags & SQLITE_OPEN_WAL) && name && REAL(p)->pMethods->iVersion >= 3) {
    pthread_mutex_init(&p->lock, 0);
    p->is_wal = 1;
    pthread_mutex_lock(&list_lock);
    p->next = logs;
    logs = p;
    pthread_mutex_unlock(&list_lock);
  }
  return rc;
}


// The VFS, by the name "browserps5-batch"; the default one if asked. 0 if
// it is registered.
int
browserps5_sqlite_batch_register(int as_default) {
  if(real_vfs) {
    return 0;
  }
  sqlite3_vfs* usual = sqlite3_vfs_find(0);
  if(!usual || usual->iVersion < 1) {
    return -1;
  }
  batch_vfs = *usual;  // its other methods take no notice of the VFS they are given
  batch_vfs.pNext = 0;
  batch_vfs.zName = "browserps5-batch";
  batch_vfs.szOsFile = (int)sizeof(batch_file) + usual->szOsFile;
  batch_vfs.xOpen = batch_open;
  real_vfs = usual;
  if(sqlite3_vfs_register(&batch_vfs, as_default) != SQLITE_OK) {
    real_vfs = 0;
    return -1;
  }
  return 0;
}


// WebCore's SQLiteDatabase.cpp calls this once, right after
// sqlite3_initialize() (build-webkit.sh).
void
browserps5_sqlite_batch(void) {
  const char* on = getenv("BROWSERPS5_SQLITE_BATCH");

  if(!on || on[0] != '1') {
    return;
  }
  const int err = browserps5_sqlite_batch_register(1);
  fprintf(stderr, "[WebKit] SQLite: the log's writes %s (BROWSERPS5_SQLITE_BATCH=1, pid %d)\n",
          err ? "stay as they are: the batch VFS could not be registered" : "go out in blocks",
          (int)getpid());
}
