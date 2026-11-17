// batch-test.c (2026-11-11): sqlite-batch.c on the PC, before it goes to the
// console. In WSL:
//   gcc -O1 -g -I/opt/ps5-payload-sdk/target/user/homebrew/include batch-test.c sqlite-batch.c \
//       -l:libsqlite3.so.0 -lpthread -o /tmp/batch-test
//   /tmp/batch-test stress /tmp/bt.db 20 full      (or: normal; plain = without the batch VFS)
//   /tmp/batch-test crash  /tmp/bt.db 50 full
//
// A writer moves amounts between 200 accounts (their sum never changes) and
// churns blobs, in transactions of every size, some undone, with a small
// page cache so that large ones spill into the log before they commit. While
// it runs: a reader on another connection of the same process (through the
// batch VFS) and another process (the usual VFS, opening and closing) check
// the sum. "crash" kills the writer at any moment, many times, and checks the
// database with the usual VFS each time (integrity_check and the sum).

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>

#include <sqlite3.h>

int browserps5_sqlite_batch_register(int as_default);
void browserps5_sqlite_batch_counts(unsigned long long* asked, unsigned long long* made);
void browserps5_sqlite_batch_blocks(unsigned long long* out);

#define ACCOUNTS 200
#define TOTAL    (ACCOUNTS * 1000)

static const char* db_path;
static const char* vfs_name;  // "browserps5-batch", or 0 for the usual one
static const char* sync_mode = "FULL";
static volatile int stop_now;
static volatile long reader_checks, reader_bad;


static double
now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}


static void
must(sqlite3* db, int rc, const char* what) {
  if(rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
    fprintf(stderr, "FAIL %s: %s (%d)\n", what, db ? sqlite3_errmsg(db) : "", rc);
    exit(2);
  }
}


static sqlite3*
open_db(const char* vfs) {
  sqlite3* db = 0;
  int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs);
  must(db, rc, "open");
  sqlite3_busy_timeout(db, 20000);
  return db;
}


// The sum of the accounts, or -1 if the query failed (busy and the like).
static long long
total_of(sqlite3* db, int* rows) {
  sqlite3_stmt* st = 0;
  long long sum = -1;

  if(sqlite3_prepare_v2(db, "SELECT sum(bal), count(*) FROM acct", -1, &st, 0) != SQLITE_OK) {
    return -1;
  }
  if(sqlite3_step(st) == SQLITE_ROW) {
    sum = sqlite3_column_int64(st, 0);
    *rows = sqlite3_column_int(st, 1);
  }
  sqlite3_finalize(st);
  return sum;
}


static int
integrity_ok(sqlite3* db) {
  sqlite3_stmt* st = 0;
  int ok = 0;

  if(sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, 0) != SQLITE_OK) {
    return 0;
  }
  if(sqlite3_step(st) == SQLITE_ROW) {
    const char* text = (const char*)sqlite3_column_text(st, 0);
    ok = text && !strcmp(text, "ok");
    if(!ok) {
      fprintf(stderr, "integrity_check: %s\n", text ? text : "(null)");
    }
  }
  sqlite3_finalize(st);
  return ok;
}


static void
setup(sqlite3* db) {
  char sql[128];
  must(db, sqlite3_exec(db, "PRAGMA journal_mode=WAL", 0, 0, 0), "wal");
  snprintf(sql, sizeof(sql), "PRAGMA synchronous=%s", sync_mode);
  must(db, sqlite3_exec(db, sql, 0, 0, 0), "synchronous");
  if(!getenv("BATCH_TEST_CACHE")) {  // BATCH_TEST_CACHE=1: SQLite's usual cache (no spilling)
    must(db, sqlite3_exec(db, "PRAGMA cache_size=40", 0, 0, 0), "cache");  // large transactions spill
  }
  must(db, sqlite3_exec(db,
    "CREATE TABLE IF NOT EXISTS acct(id INTEGER PRIMARY KEY, bal INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS blobs(k INTEGER PRIMARY KEY, v BLOB, tag INTEGER);"
    "CREATE INDEX IF NOT EXISTS blobs_tag ON blobs(tag);", 0, 0, 0), "schema");
  int rows = 0;
  if(total_of(db, &rows) < 0 || rows == 0) {
    must(db, sqlite3_exec(db, "BEGIN", 0, 0, 0), "begin");
    for(int i = 0; i < ACCOUNTS; i++) {
      snprintf(sql, sizeof(sql), "INSERT INTO acct VALUES(%d, 1000)", i);
      must(db, sqlite3_exec(db, sql, 0, 0, 0), "insert");
    }
    must(db, sqlite3_exec(db, "COMMIT", 0, 0, 0), "commit");
  }
}


static unsigned
rnd(unsigned* seed) {
  *seed = *seed * 1103515245u + 12345u;
  return *seed >> 8;
}


// Transactions until the time is over (or for ever: the crash test).
static long
writer(double seconds) {
  sqlite3* db = open_db(vfs_name);
  unsigned seed = (unsigned)getpid() * 7919u + (unsigned)time(0);
  const double end = now_s() + seconds;
  long done = 0;
  char sql[256];

  setup(db);
  while(seconds <= 0 || now_s() < end) {
    const int kind = (int)(rnd(&seed) % 100);
    const int transfers = 1 + (int)(rnd(&seed) % 20);
    int churn = 1 + (int)(rnd(&seed) % 30);
    if(kind < 4 && !getenv("BATCH_TEST_SMALL")) {  // BATCH_TEST_SMALL=1: none of these
      churn = 1500;  // a large one: it spills into the log before the commit
    }
    must(db, sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0), "begin immediate");
    for(int i = 0; i < transfers; i++) {
      const int a = (int)(rnd(&seed) % ACCOUNTS), b = (int)(rnd(&seed) % ACCOUNTS);
      const int amount = (int)(rnd(&seed) % 50);
      snprintf(sql, sizeof(sql), "UPDATE acct SET bal = bal - %d WHERE id = %d;"
               "UPDATE acct SET bal = bal + %d WHERE id = %d;", amount, a, amount, b);
      must(db, sqlite3_exec(db, sql, 0, 0, 0), "transfer");
    }
    for(int i = 0; i < churn; i++) {
      snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO blobs VALUES(%u, randomblob(%u), %u)",
               rnd(&seed) % 6000, 100 + rnd(&seed) % 3000, rnd(&seed) % 97);
      must(db, sqlite3_exec(db, sql, 0, 0, 0), "churn");
    }
    if(kind >= 90) {  // undone: an amount vanishes, and comes back with the rollback
      must(db, sqlite3_exec(db, "UPDATE acct SET bal = bal - 777 WHERE id = 3", 0, 0, 0), "break");
      must(db, sqlite3_exec(db, "ROLLBACK", 0, 0, 0), "rollback");
    } else {
      must(db, sqlite3_exec(db, "COMMIT", 0, 0, 0), "commit");
    }
    if(kind == 50) {
      must(db, sqlite3_exec(db, "DELETE FROM blobs WHERE tag < 20", 0, 0, 0), "delete");
    }
    done++;
  }
  sqlite3_close(db);
  return done;
}


static void*
reader(void* arg) {
  sqlite3* db = open_db(vfs_name);
  (void)arg;
  while(!stop_now) {
    int rows = 0;
    long long sum = total_of(db, &rows);
    if(sum >= 0) {
      reader_checks++;
      if(sum != TOTAL || rows != ACCOUNTS) {
        reader_bad++;
        fprintf(stderr, "FAIL the reader saw a sum of %lld in %d rows\n", sum, rows);
      }
    }
    if(reader_checks % 400 == 399 && !integrity_ok(db)) {
      reader_bad++;
    }
    // BATCH_TEST_READER_US: a reader that looks less often (each look of another
    // connection of this process sends the writer's block out: the barrier).
    usleep(getenv("BATCH_TEST_READER_US") ? (useconds_t)atoi(getenv("BATCH_TEST_READER_US")) : 500);
  }
  sqlite3_close(db);
  return 0;
}


// Another process, the usual VFS: opens, checks, closes (a close may checkpoint).
static void
other_process(void) {
  long checks = 0, bad = 0;
  while(!stop_now) {
    sqlite3* db = open_db(0);
    int rows = 0;
    long long sum = total_of(db, &rows);
    if(sum >= 0) {
      checks++;
      if(sum != TOTAL || rows != ACCOUNTS) {
        bad++;
        fprintf(stderr, "FAIL the other process saw a sum of %lld in %d rows\n", sum, rows);
      }
    }
    sqlite3_close(db);
    usleep(3000);
  }
  printf("the other process: %ld checks, %ld bad\n", checks, bad);
  exit(bad ? 3 : 0);
}


static void
on_term(int sig) {
  (void)sig;
  stop_now = 1;
}


int
main(int argc, char** argv) {
  if(argc < 5) {
    fprintf(stderr, "usage: batch-test stress|crash <db> <seconds|times> full|normal|plain\n");
    return 1;
  }
  db_path = argv[2];
  const int n = atoi(argv[3]);
  if(strcmp(argv[4], "plain")) {
    if(browserps5_sqlite_batch_register(0)) {
      fprintf(stderr, "FAIL the batch VFS did not register\n");
      return 2;
    }
    vfs_name = "browserps5-batch";
    sync_mode = !strcmp(argv[4], "normal") ? "NORMAL" : "FULL";
  }

  if(!strcmp(argv[1], "stress")) {
    sqlite3* first = open_db(vfs_name);
    setup(first);
    sqlite3_close(first);
    signal(SIGTERM, on_term);
    pid_t other = fork();
    if(!other) {
      other_process();
    }
    pthread_t thread;
    pthread_create(&thread, 0, reader, 0);
    const double t = now_s();
    const long done = writer(n);
    const double took = now_s() - t;
    stop_now = 1;
    pthread_join(thread, 0);
    kill(other, SIGTERM);
    int status = 0;
    waitpid(other, &status, 0);
    unsigned long long asked = 0, made = 0;
    browserps5_sqlite_batch_counts(&asked, &made);
    unsigned long long why[7];
    browserps5_sqlite_batch_blocks(why);
    printf("blocks out: close %llu, read %llu, write %llu, truncate %llu, sync %llu, size %llu, barrier %llu\n",
           why[0], why[1], why[2], why[3], why[4], why[5], why[6]);
    sqlite3* db = open_db(0);
    int rows = 0;
    const long long sum = total_of(db, &rows);
    const int ok = integrity_ok(db) && sum == TOTAL && rows == ACCOUNTS && !reader_bad
                   && WIFEXITED(status) && !WEXITSTATUS(status);
    sqlite3_close(db);
    printf("%s stress (%s, synchronous %s): %ld transactions in %.1f s (%.0f/s); the log's writes: "
           "%llu asked, %llu made; the reader: %ld checks, %ld bad; at the end: sum %lld in %d rows\n",
           ok ? "PASS" : "FAIL", vfs_name ? "batch" : "plain", sync_mode, done, took, done / took,
           asked, made, (long)reader_checks, (long)reader_bad, sum, rows);
    return ok ? 0 : 3;
  }

  if(!strcmp(argv[1], "crash")) {
    int bad = 0;
    unsigned seed = (unsigned)time(0);
    for(int i = 0; i < n; i++) {
      pid_t child = fork();
      if(!child) {
        pthread_t thread;
        pthread_create(&thread, 0, reader, 0);
        writer(0);  // for ever
        exit(0);
      }
      usleep(30000 + rnd(&seed) % 700000);
      kill(child, SIGKILL);
      waitpid(child, 0, 0);
      sqlite3* db = open_db(0);  // the usual VFS reads what the killed one left
      int rows = 0;
      const long long sum = total_of(db, &rows);
      const int ok = integrity_ok(db) && sum == TOTAL && rows == ACCOUNTS;
      sqlite3_close(db);
      if(!ok) {
        bad++;
        fprintf(stderr, "FAIL after kill %d: sum %lld in %d rows\n", i + 1, sum, rows);
      }
    }
    printf("%s crash (%s, synchronous %s): %d kills, %d bad\n", bad ? "FAIL" : "PASS",
           vfs_name ? "batch" : "plain", sync_mode, n, bad);
    return bad ? 3 : 0;
  }
  return 1;
}
