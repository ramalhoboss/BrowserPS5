// multi-sim.in.cpp: the stand-ins round the browser's own download code. make-multi-sim.py puts that code, cut out
// of browser/main.cpp as it is, at the @...@ marks; build-and-run.sh compiles and runs the result with the
// sanitizers. What it checks is said in make-multi-sim.py.
//   multi_sim <work folder> [scenarios] [first seed] [v]      (v: one scenario, with the browser's own log lines)
#define APP_RN 1
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

// ---- the clock, the log
static double sim_ms = 100000;
static bool sim_verbose;
static double poc_now_ms(void) { return sim_ms; }
#define POC_LOG(fmt, ...) do { if(sim_verbose) printf("%9.1f s  " fmt "\n", sim_ms / 1000, ##__VA_ARGS__); } while(0)
#define POC_RESULT(ok, fmt, ...) POC_LOG(fmt, ##__VA_ARGS__)
#define SIM_LOG(fmt, ...) do { if(sim_verbose) printf("%9.1f s  [sim] " fmt "\n", sim_ms / 1000, ##__VA_ARGS__); } while(0)

// ---- a pretend WebKit
struct sim_dl;
// A navigation asked of a page is "pending" until it begins (here: until its download is there). The engine keeps
// ONE of those a page: asked for another meanwhile, the first is dropped and nobody is told (2026-11-14).
struct sim_page { int n; unsigned long long pending; std::string pending_url; };
static unsigned long long nav_serial;
static int replaced_navs;  // navigations of parts dropped because another part's was asked of the same page
typedef sim_dl* WKDownloadRef;
typedef sim_page* WKPageRef;
typedef void* WKDataRef;
typedef char* WKStringRef;
typedef char* WKURLRef;
typedef int* WKURLResponseRef;
typedef sim_dl* WKURLRequestRef;
typedef const char* WKErrorRef;
typedef struct { int version; const void* clientInfo; } WKDownloadClientBase;
typedef struct {
  WKDownloadClientBase base;
  WKStringRef (*decideDestinationWithResponse)(WKDownloadRef, WKURLResponseRef, WKStringRef, const void*);
  void (*didWriteData)(WKDownloadRef, long long, long long, long long, const void*);
  void (*didFinish)(WKDownloadRef, const void*);
  void (*didFailWithError)(WKDownloadRef, WKErrorRef, WKDataRef, const void*);
} WKDownloadClientV0;

struct sim_dl {
  bool main = false;
  std::string url;
  long long from = 0, last = -1;      // the bytes asked of the server
  long long buffered = 0, written = 0;
  int state = 0;                      // 0 connecting, 1 its bytes coming, 2 ended
  double ready_ms = 0;
  bool cancelled = false;
  int fd = -1;
  WKDownloadClientV0 client = {};
  int owner = 0;                      // (a download's own connection) the download's id
  long long total = 0;                // (a download's own connection) its file's size
  std::string path;                   // (a download's own connection) its file, by the name the engine knows
  std::string file_key;               // whose file its bytes go into ("" with one download only)
};

static std::vector<sim_dl*> dls;
static std::multimap<double, std::function<void()>> events;
static sim_page pages[2];
static bool tab_closed;       // the tab the download came from is gone: the tab shown is pages[1]
static double tab_busy_until; // the tab is opening a page of the user's
static double nav_lost_until; // navigations asked for never arrive
static unsigned long long rng_state = 88172645463325252ull;
static unsigned rnd(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return (unsigned)(rng_state >> 11);
}
static double rnd_between(double a, double b) { return a + (b - a) * (rnd() % 100000) / 100000.0; }
static unsigned long long rng_more;  // (what was added later draws from here: the seeds before keep their scenarios)
static unsigned rnd_more(void) {
  rng_more ^= rng_more << 13; rng_more ^= rng_more >> 7; rng_more ^= rng_more << 17;
  return (unsigned)(rng_more >> 11);
}
static double rnd_more_between(double a, double b) { return a + (b - a) * (rnd_more() % 100000) / 100000.0; }
static void later(double ms, std::function<void()> fn) { events.emplace(sim_ms + ms, fn); }

static void WKRelease(sim_dl*) {}
static void WKRelease(char* s) { free(s); }
static sim_dl* WKRetain(sim_dl* d) { return d; }
static WKStringRef string(const char* s) { return strdup(s); }
static const char* take_string(WKStringRef s, char* buf, size_t size) {
  buf[0] = 0;
  if(s) { snprintf(buf, size, "%s", s); free(s); }
  return buf;
}
static const char* take_url(WKURLRef url, char* buf, size_t size) { return take_string(url, buf, size); }
static WKURLRef WKURLCreateWithUTF8CString(const char* s) { return strdup(s); }
static WKStringRef WKErrorCopyLocalizedDescription(WKErrorRef e) { return strdup(e ? e : ""); }
static int WKURLResponseHTTPStatusCode(WKURLResponseRef r) { return *r; }
static WKURLRequestRef WKDownloadCopyRequest(WKDownloadRef d) { return d; }
static WKURLRef WKURLRequestCopyURL(WKURLRequestRef r) { return strdup(r->url.c_str()); }
static WKPageRef WKDownloadGetOriginatingPage(WKDownloadRef) { return &pages[0]; }
static WKURLRef WKPageCopyProvisionalURL(WKPageRef) { return sim_ms < tab_busy_until ? strdup("https://the.user/goes/elsewhere") : 0; }
static WKURLRef WKPageCopyPendingAPIRequestURL(WKPageRef p) { return p->pending ? strdup(p->pending_url.c_str()) : 0; }
static void WKDownloadSetClient(WKDownloadRef d, const WKDownloadClientBase* base) { d->client = *(const WKDownloadClientV0*)base; }
static void WKRunLoopCallOnMainThread(void (*fn)(void*), void* ctx) { later(0, [fn, ctx]() { fn(ctx); }); }
static void WKDownloadCancel(WKDownloadRef d, const void* ctx, void (*cb)(WKDataRef, const void*));
static void WKPageLoadURL(WKPageRef p, WKURLRef address);
#define PAGE 0
static WKPageRef page(int) { return &pages[tab_closed ? 1 : 0]; }
static void bar_notify(const char*, const char*) {}
static bool lang_en(void) { return false; }
#define LM(en) (en)
#define TITLE_ID "SIM00000"
#define PARTIAL_DIR "partial"
#define PARTIAL_NEAR ".parcial"
static struct { int multipart; char downloads_dir[64], pkg_dir[64], media_dir[64]; } settings = {1, "out", "out", "out"};
static double last_gesture_ms;
static struct { double start_ms; } app;
static const char* download_dir(const char*) { return settings.downloads_dir; }
typedef struct { int unused; } dl_rate_t;
static void dl_rate_add(dl_rate_t*, long long, double) {}
static void dialog_hide(void) {}
static int dialog_download;
// (the map's thread: its work at once, on this one)
#define pthread_create(thread, attr, fn, arg) ((void)(thread), fn(arg), 0)
#define pthread_detach(thread) ((void)0)

static char*
read_whole(const char* path, size_t max, size_t* length) {
  FILE* f = fopen(path, "rb");
  if(!f) {
    return 0;
  }
  char* text = (char*)malloc(max + 1);
  const size_t n = fread(text, 1, max, f);
  fclose(f);
  text[n] = 0;
  *length = n;
  return text;
}

@DOWNLOAD_T@
enum { DL_RUNNING, DL_WAITING, DL_MOVING, DL_DONE, DL_FAILED, DL_CANCELLED };
static const char* const download_states[] = {"running", "waiting", "running", "done", "failed", "cancelled"};
#define DOWNLOADS_MAX 16
static download_t downloads[DOWNLOADS_MAX];
static int download_ids;
static bool downloads_changed;
static download_t*
download_by_id(int id) {
  for(download_t& d : downloads) {
    if(id && d.id == id) {
      return &d;
    }
  }
  return 0;
}

@URL_IS_WEB@
@NAME_ENDS@
// ======================================================== the browser's own code, from here ...
@ENGINE@
// ======================================================== ... to here
static bool
multi_page_fine(WKPageRef p) {
  return p == &pages[1] || (p == &pages[0] && !tab_closed);
}
static int all_in_calls;
static std::map<std::string, int> all_in_by;  // by the file's first name
static void
download_all_in(download_t* d) {
  d->written = true;
  d->state = DL_DONE;
  all_in_calls++;
  all_in_by[d->partial]++;
}
// ======================================================== the browser's own code again
@PROGRESS@
@FINISHED@
@FAILED@
@KEPT@
@CANCELLED@
@CANCEL@
// ========================================================

// ---- what the file has: the ranges written so far (they survive the browser closing)
static std::map<std::string, std::map<long long, long long>> covers;  // by the file's first name ("" with one download only)
static std::string cover_key;
#define cover covers[cover_key]  // start -> end (not included), never touching each other
static void
cover_add(long long a, long long b) {
  if(a >= b) {
    return;
  }
  auto it = cover.upper_bound(a);
  if(it != cover.begin()) {
    auto before = std::prev(it);
    if(before->second >= a) {
      a = before->first;
      b = b > before->second ? b : before->second;
      it = cover.erase(before);
    }
  }
  while(it != cover.end() && it->first <= b) {
    b = b > it->second ? b : it->second;
    it = cover.erase(it);
  }
  cover[a] = b;
}
static long long
cover_bytes(void) {
  long long n = 0;
  for(const auto& r : cover) {
    n += r.second - r.first;
  }
  return n;
}

// ---- the scenario
struct sim_event { double at_ms, len_ms; int kind; };
enum { EV_CUT = 1, EV_QUIET, EV_RESET_ONE, EV_TAB_BUSY, EV_TAB_CLOSED, EV_PAUSE, EV_QUIT, EV_FALL, EV_NAV_LOST, EV_CANCEL, EV_LIMIT };
static struct {
  long long total, rate;
  int limit;         // connections the server gives at once
  bool ranges;       // it gives ranges at all
  double latency_ms;
  bool lose_cancel;  // some cancels are never answered
  long long line;    // bytes a second the whole line carries, shared by its connections (0: no such limit)
  double busy_from, busy_until;  // the server answers 503 to new connections between these
  double expire_ms;  // from then on new connections get 403: the link is no longer good (0: never)
} sc;
static bool expired_hit;    // a connection was refused because the link was no longer good
static double net_cut_until, net_quiet_until;
static int main_id;
static char main_path[700];
static bool legit_loss;     // the browser closed or fell with nothing on the disk to go on with
static double fail_ok_until;// its own connection failed with no file kept yet: it may fail now (as it always did)
static bool own_hit;        // its own connection failed or was stopped (a server with no ranges cannot give the rest)
static std::map<std::string, bool> own_hit_by;  // the same, by the file's first name (two downloads at once)
static bool duo_mode;       // two downloads at once (run_duo())
static void duo_part_file(sim_dl* dl, const char* path);
static char why_fail[300];
static int failures;

static void
fail_at(const char* fmt, ...) {
  va_list args;
  if(!why_fail[0]) {
    va_start(args, fmt);
    vsnprintf(why_fail, sizeof(why_fail), fmt, args);
    va_end(args);
  }
}

static int
flowing(void) {
  int n = 0;
  for(const sim_dl* dl : dls) {
    n += dl->state == 1 && !dl->cancelled;
  }
  return n;
}

static void
dl_close(sim_dl* dl) {
  dl->state = 2;
  dl->buffered = 0;
  if(dl->fd >= 0) {
    close(dl->fd);
    dl->fd = -1;
  }
}

// The engine's side of a download that fails: what it had not written is lost; the download itself loses its
// file by the name the engine knows; then the browser is told.
static void
dl_fail(sim_dl* dl, const char* why) {
  if(dl->state == 2) {
    return;
  }
  SIM_LOG("%s connection %lld-%lld fails %lld bytes in: %s", dl->main ? "the download's own" : "a part's", dl->from, dl->last, dl->written, why);
  dl_close(dl);
  if(dl->main) {
    own_hit = true;
    own_hit_by[dl->path] = true;
    unlink(dl->path.c_str());
    download_failed(dl, why, 0, (const void*)(intptr_t)dl->owner);
  } else if(dl->client.didFailWithError) {
    dl->client.didFailWithError(dl, why, 0, dl->client.base.clientInfo);
  }
}

static void
WKDownloadCancel(WKDownloadRef dl, const void* ctx, void (*cb)(WKDataRef, const void*)) {
  const bool was_live = dl->state != 2;
  dl->cancelled = true;
  dl_close(dl);
  if(dl->main) {
    own_hit = true;
    own_hit_by[dl->path] = true;
  }
  if(dl->main && was_live) {
    unlink(dl->path.c_str());  // (the engine removes a cancelled download's file, by its name)
    if(rnd() % 2) {            // and may tell of it as a failure, before or after the cancel's answer
      later(rnd_between(1, 300), [dl]() { download_failed(dl, "cancelled", 0, (const void*)(intptr_t)dl->owner); });
    }
  }
  if(sc.lose_cancel && rnd() % 12 == 0) {
    SIM_LOG("a cancel's answer is lost");
    return;
  }
  later(rnd_between(5, 250), [cb, ctx]() { cb(0, ctx); });
}

static void
WKPageLoadURL(WKPageRef p, WKURLRef address) {
  std::string url = address;
  if(p->pending) {  // the one this page had not begun yet goes, and nobody is told
    multi_t* waits = 0;
    if(p->pending_url != url && multi_asked(p->pending_url.c_str(), &waits)) {  // (a piece is still waited for: it is lost)
      replaced_navs++;
      SIM_LOG("a part's navigation is REPLACED by another's, asked of the same tab before it began");
    } else {  // (its set ended or went on alone meanwhile: nothing is lost with it)
      SIM_LOG("a navigation nobody waits for any more gives its place to another's");
    }
  }
  const unsigned long long mine = ++nav_serial;
  p->pending = mine;
  p->pending_url = url;
  if(sim_ms < nav_lost_until) {
    SIM_LOG("a part's navigation is lost");
    p->pending = 0;
    return;
  }
  later(rnd_between(20, 300), [url, p, mine]() {
    if(p->pending != mine) {  // replaced meanwhile (by another part's, or by a page the user went to)
      return;
    }
    p->pending = 0;
    multi_t* owner = 0;
    if(!multi_asked(url.c_str(), &owner)) {  // (decidePageNavigation(): nobody waits for it: ignored)
      return;
    }
    sim_dl* dl = new sim_dl;
    long long from = 0, last = 0;
    sscanf(strstr(url.c_str(), "#bps5range=") + 11, "%lld-%lld", &from, &last);
    dl->url = url;
    dl->from = from;
    dl->last = last;
    dl->ready_ms = sim_ms + sc.latency_ms * rnd_between(0.5, 2);
    dls.push_back(dl);
    multi_claim(dl);
  });
}

// A block of a download reaches the file, and the browser is told.
static void
dl_write(sim_dl* dl, long long n) {
  const long long at = dl->from + dl->written;
  struct stat st;
  cover_key = dl->file_key;
  cover_add(at, at + n);
  if(dl->fd >= 0 && !fstat(dl->fd, &st) && st.st_size < at + n && ftruncate(dl->fd, at + n)) {
    fail_at("the file could not grow");
  }
  dl->written += n;
  if(dl->main) {
    download_progress(dl, n, dl->written, dl->total, (const void*)(intptr_t)dl->owner);
  } else if(dl->client.didWriteData) {
    dl->client.didWriteData(dl, n, dl->written, 0, dl->client.base.clientInfo);
  }
}

static void
engine_step(double dt_ms) {
  const bool cut = sim_ms < net_cut_until, quiet = sim_ms < net_quiet_until;
  const int alive = flowing();
  const long long each = sc.line && alive > 0 && sc.line / alive < sc.rate ? sc.line / alive : sc.rate;
  for(size_t i = 0; i < dls.size(); i++) {  // (the list may grow under it)
    sim_dl* dl = dls[i];
    if(dl->state == 0 && !dl->cancelled && sim_ms >= dl->ready_ms) {
      if(cut) {
        dl_fail(dl, "could not connect");
        continue;
      }
      if(quiet) {
        continue;
      }
      int status = !sc.ranges ? 200 : sc.expire_ms && sim_ms >= sc.expire_ms ? 403
                   : sim_ms >= sc.busy_from && sim_ms < sc.busy_until ? 503 : flowing() >= sc.limit ? 429 : 206;
      if(status == 403 && !expired_hit) {
        expired_hit = true;
        SIM_LOG("the link is no longer good: 403 to every new connection from now on");
      }
      char* path = dl->client.decideDestinationWithResponse ? dl->client.decideDestinationWithResponse(dl, &status, 0, dl->client.base.clientInfo) : strdup("");
      if(path[0] && status != 206) {
        fail_at("a part was given a file though the server answered %d", status);
      }
      if(path[0] && status == 206) {
        dl->fd = open(path, O_RDWR);
        if(dl->fd < 0) {
          fail_at("a part's file is not there: %s", path);
        }
        if(duo_mode) {
          duo_part_file(dl, path);
        }
        dl->state = 1;
      } else {
        dl_close(dl);
      }
      free(path);
      continue;
    }
    if(dl->state != 1 || dl->cancelled || cut || quiet) {
      continue;
    }
    const long long size = dl->last + 1 - dl->from;
    long long room = size - dl->written - dl->buffered;
    long long add = (long long)(each * dt_ms / 1000.0);
    dl->buffered += add < room ? add : room;
    while(dl->state == 1 && !dl->cancelled && (dl->buffered >= (1 << 20) || (dl->buffered > 0 && dl->written + dl->buffered == size))) {
      const long long n = dl->buffered < (1 << 20) ? dl->buffered : (1 << 20);
      dl->buffered -= n;
      dl_write(dl, n);
    }
    if(dl->state == 1 && !dl->cancelled && dl->written == size) {
      dl_close(dl);
      if(dl->main) {
        download_finished(dl, (const void*)(intptr_t)dl->owner);
      } else if(dl->client.didFinish) {
        dl->client.didFinish(dl, dl->client.base.clientInfo);
      }
    }
  }
}

// The browser's memory goes (it closed or fell): the engine's connections with it, and what was still to be called.
static void
browser_gone(void) {
  for(sim_dl* dl : dls) {
    dl_close(dl);
  }
  events.clear();
  for(multi_t& m : multis) {
    for(char*& note : m.maps) {
      free(note);
      note = 0;
    }
  }
  memset(multis, 0, sizeof(multis));
  memset(downloads, 0, sizeof(downloads));
  multi_map_busy = 0;
  tab_closed = false;
  tab_busy_until = 0;
  pages[0].pending = pages[1].pending = 0;
}

// Every byte the file has not must be in a range the set will ask for.
static void
check_missing_is_asked(const multi_t* m, const char* when) {
  long long at = 0;
  auto gap = [&](long long a, long long b) {
    for(long long p = a; p < b;) {  // [a, b) is not in the file: walk it through the set's ranges
      long long next = -1;
      for(int i = 0; i < m->segs; i++) {
        const multi_seg_t* s = &m->seg[i];
        if(s->state != MS_DONE && s->from + (s->got < 0 ? 0 : 0) <= p && p < s->to) {
          next = s->to;
        }
      }
      if(next < 0) {
        fail_at("%s: byte %lld is not in the file and no range asks for it", when, p);
        return;
      }
      p = next;
    }
  };
  for(const auto& r : cover) {
    gap(at, r.first);
    at = r.second;
  }
  gap(at, m->total);
}

static download_t*
the_download(void) {
  for(download_t& d : downloads) {
    if(d.id) {
      return &d;
    }
  }
  return 0;
}

static bool
map_on_disk(void) {
  bool there = false;
  if(DIR* dir = opendir(PARTIAL_DIR)) {
    for(struct dirent* e; (e = readdir(dir));) {
      there = there || name_ends_with(e->d_name, ".map");
    }
    closedir(dir);
  }
  return there;
}

static int
files_left(void) {
  int n = 0;
  if(DIR* dir = opendir(PARTIAL_DIR)) {
    for(struct dirent* e; (e = readdir(dir));) {
      n += e->d_name[0] != '.';
    }
    closedir(dir);
  }
  return n;
}

static bool
set_has_file(void) {
  for(const multi_t& m : multis) {
    if(m.id && m.file[0]) {
      return true;
    }
  }
  return false;
}

static void
remove_all_in(const char* dir_path) {
  if(DIR* dir = opendir(dir_path)) {
    for(struct dirent* e; (e = readdir(dir));) {
      if(e->d_name[0] != '.') {
        char path[900];
        snprintf(path, sizeof(path), "%s/%s", dir_path, e->d_name);
        unlink(path);
      }
    }
    closedir(dir);
  }
}

static const char*
run_scenario(unsigned long long seed, double* took_s) {
  static int partial_n;
  std::vector<sim_event> plan;
  double paused_until = -1, away_until = -1;
  bool was_fall = false;
  int restarts = 0, pauses = 0;

  rng_state = seed * 2654435761ull + 88172645463325252ull;
  rng_more = seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
  for(int i = 0; i < 8; i++) {
    rnd();
    rnd_more();
  }
  // -- clean ground
  for(sim_dl* dl : dls) {
    dl_close(dl);
    delete dl;
  }
  dls.clear();
  browser_gone();
  covers.clear();
  cover_key.clear();
  own_hit_by.clear();
  all_in_by.clear();
  duo_mode = false;
  replaced_navs = 0;
  remove_all_in(PARTIAL_DIR);
  download_ids = 0;
  why_fail[0] = 0;
  legit_loss = own_hit = expired_hit = false;
  fail_ok_until = 0;
  bool cancelled_by_user = false;
  net_cut_until = net_quiet_until = nav_lost_until = 0;
  all_in_calls = 0;
  sim_ms += 100000;
  // -- the server and the line
  static const long long sizes[] = {12, 40, 120, 260, 700, 1500, 2400, 9000};
  sc.total = sizes[rnd() % 8] * 1048576LL + rnd() % 1048576;
  sc.rate = (2 + rnd() % 38) * 1048576LL;
  sc.limit = rnd() % 4 == 0 ? 1 + rnd() % 4 : 99;
  sc.ranges = rnd() % 12 != 0;
  sc.latency_ms = 20 + rnd() % 600;
  sc.lose_cancel = rnd() % 5 == 0;
  settings.multipart = rnd() % 6 != 0;
  sc.line = rnd_more() % 4 == 0 ? (long long)(sc.rate * rnd_more_between(1.0, 5.0)) : 0;  // (more connections bring little or nothing)
  const double plain_s = (double)sc.total / sc.rate;  // by one connection, with nothing in its way
  const double span_ms = (plain_s < 20 ? 20 : plain_s) * 1000;
  const int troubles = rnd() % 5 == 0 ? 8 + rnd() % 10 : rnd() % 7;
  double calm_ms = sim_ms + 6000;
  sc.busy_from = sc.busy_until = sc.expire_ms = 0;
  if(rnd_more() % 6 == 0) {  // the server is busy for a while: 503 to whoever comes
    sc.busy_from = sim_ms + rnd_more_between(3000, span_ms * 1.2);
    sc.busy_until = sc.busy_from + rnd_more_between(5000, 120000);
    calm_ms = sc.busy_until;
  }
  if(rnd_more() % 12 == 0) {  // a link that is good for a while only
    sc.expire_ms = sim_ms + rnd_more_between(10000, span_ms * 1.5);
  }
  for(int i = 0; i < troubles; i++) {
    sim_event e;
    e.at_ms = sim_ms + rnd_between(5000, span_ms * 1.2);
    static const int kinds[] = {EV_CUT, EV_CUT, EV_QUIET, EV_QUIET, EV_RESET_ONE, EV_RESET_ONE, EV_TAB_BUSY, EV_TAB_CLOSED, EV_PAUSE,
                                EV_PAUSE, EV_QUIT, EV_QUIT, EV_FALL, EV_NAV_LOST, EV_CANCEL, EV_LIMIT};
    e.kind = kinds[rnd() % (sizeof(kinds) / sizeof(kinds[0]))];
    e.len_ms = e.kind == EV_CUT ? rnd_between(500, 70000) : e.kind == EV_QUIET ? rnd_between(3000, 100000)
               : e.kind == EV_TAB_BUSY ? rnd_between(300, 4000) : e.kind == EV_NAV_LOST ? rnd_between(1000, 20000)
               : e.kind == EV_PAUSE || e.kind == EV_QUIT || e.kind == EV_FALL ? rnd_between(1000, 30000) : 0;
    plan.push_back(e);
    if(e.at_ms + e.len_ms > calm_ms) {
      calm_ms = e.at_ms + e.len_ms;
    }
  }
  // -- the download
  download_t* d = &downloads[0];
  sim_dl* own = new sim_dl;
  memset(d, 0, sizeof(*d));
  d->id = ++download_ids;
  main_id = d->id;
  d->state = DL_RUNNING;
  d->status = 200;
  d->can_retry = d->chosen = true;
  d->total = sc.total;
  snprintf(d->url, sizeof(d->url), "https://sim.example/start?x=%u", rnd());
  snprintf(d->final_url, sizeof(d->final_url), "https://cdn.sim.example/a file.bin");
  snprintf(d->name, sizeof(d->name), "a file.bin");
  snprintf(d->kind, sizeof(d->kind), "file");
  snprintf(d->dir, sizeof(d->dir), "out");
  snprintf(d->host, sizeof(d->host), "cdn.sim.example");
  snprintf(d->partial, sizeof(d->partial), PARTIAL_DIR "/%d-%d.part", d->id, ++partial_n);
  snprintf(main_path, sizeof(main_path), "%s", d->partial);
  own->main = true;
  own->last = sc.total - 1;
  own->state = 1;
  own->fd = open(d->partial, O_RDWR | O_CREAT | O_TRUNC, 0644);
  own->owner = d->id;
  own->total = sc.total;
  own->path = d->partial;
  d->ref = own;
  dls.push_back(own);
  SIM_LOG("seed %llu: %lld bytes at %lld MiB/s a connection, the server gives %d at once, ranges %d, parts %d, %d trouble(s), latency %.0f ms",
          seed, sc.total, sc.rate >> 20, sc.limit, sc.ranges, settings.multipart, troubles, sc.latency_ms);
  if(sc.line || sc.busy_until || sc.expire_ms) {
    SIM_LOG("and: the line carries %.1f MiB/s in all (0: no limit), busy (503) from %.0f s for %.0f s, the link is good for %.0f s (0: always)",
            sc.line / 1048576.0, sc.busy_until ? (sc.busy_from - sim_ms) / 1000 : 0, (sc.busy_until - sc.busy_from) / 1000,
            sc.expire_ms ? (sc.expire_ms - sim_ms) / 1000 : 0);
  }
  const double t0 = sim_ms;
  const double deadline_ms = calm_ms + (plain_s * 1.5 + 900) * 1000;
  // -- the run
  for(; sim_ms < deadline_ms && !why_fail[0]; sim_ms += 100) {
    for(sim_event& e : plan) {
      if(e.kind && sim_ms >= e.at_ms) {
        const int kind = e.kind;
        e.kind = 0;
        d = the_download();
        const bool running = d && d->state == DL_RUNNING && paused_until < 0 && away_until < 0;
        if(kind == EV_CUT) {
          SIM_LOG("the line is CUT for %.1f s", e.len_ms / 1000);
          net_cut_until = sim_ms + e.len_ms;
          if(running && !set_has_file()) {
            fail_ok_until = sim_ms + 2000;  // its own connection fails with nothing kept yet
          }
          for(size_t i = 0; i < dls.size(); i++) {
            dl_fail(dls[i], "the connection was reset");
          }
        } else if(kind == EV_QUIET) {
          SIM_LOG("the line is QUIET for %.1f s", e.len_ms / 1000);
          net_quiet_until = sim_ms + e.len_ms;
        } else if(kind == EV_RESET_ONE) {
          std::vector<sim_dl*> live;
          for(sim_dl* dl : dls) {
            if(dl->state == 1 && !dl->cancelled) {
              live.push_back(dl);
            }
          }
          if(!live.empty()) {
            sim_dl* dl = live[rnd() % live.size()];
            if(dl->main && !set_has_file()) {
              fail_ok_until = sim_ms + 2000;
            }
            dl_fail(dl, "the connection was reset");
          }
        } else if(kind == EV_TAB_BUSY) {
          tab_busy_until = sim_ms + e.len_ms;
          pages[0].pending = pages[1].pending = 0;  // (the user's page takes the place of a part's navigation not begun yet)
        } else if(kind == EV_TAB_CLOSED) {
          SIM_LOG("its tab is closed");
          tab_closed = true;
        } else if(kind == EV_NAV_LOST) {
          nav_lost_until = sim_ms + e.len_ms;
        } else if(kind == EV_LIMIT) {  // the server changes how many connections it gives
          sc.limit = rnd() % 3 == 0 ? 99 : 1 + rnd() % 5;
          SIM_LOG("the server now gives %d connection(s) at once", sc.limit);
        } else if(kind == EV_CANCEL && d && (d->state == DL_RUNNING || (d->state == DL_FAILED && d->kept)) && away_until < 0 && rnd() % 3 == 0) {
          SIM_LOG("the user cancels it");
          cancelled_by_user = true;
          paused_until = -1;
          download_cancel(d);
        } else if(kind == EV_PAUSE && running) {
          const bool can = multi_of(d->id) && set_has_file();
          SIM_LOG("the user pauses (%s)", can ? "it can be" : "it cannot be yet");
          download_pause(d);
          if(can) {
            pauses++;
            if(d->state != DL_FAILED || !d->kept || multi_of(d->id)) {
              fail_at("paused, but its state is %s, kept %d", download_states[d->state], d->kept);
            }
            paused_until = sim_ms + e.len_ms;
          }
        } else if((kind == EV_QUIT || kind == EV_FALL) && running) {
          const bool had_file = set_has_file();
          SIM_LOG("the browser %s (%s)", kind == EV_QUIT ? "is closed" : "FALLS", had_file ? "its set has its file" : "nothing to keep yet");
          if(kind == EV_QUIT) {
            multi_keep_all();
          }
          was_fall = kind == EV_FALL;
          // Closed: its map is written now, if its set has its file. Fallen: what the disk has, if a map got there.
          legit_loss = was_fall ? !map_on_disk() : !had_file;
          own_hit = true;  // (its own connection goes with the browser)
          browser_gone();
          away_until = sim_ms + e.len_ms;
          restarts++;
        }
      }
    }
    if(away_until >= 0) {  // nobody runs
      if(sim_ms >= away_until) {
        away_until = -1;
        multi_kept_scan();
        d = the_download();
        SIM_LOG("the browser opens again: %s", d ? "a download was kept" : "nothing was kept");
        if(!d) {
          if(!legit_loss) {
            fail_at("closed with its file and its map, and the next session found nothing");
          }
          break;
        }
        main_id = d->id;
        if(!multi_resume(d) || (d->state != DL_DONE && (d->state != DL_RUNNING || !multi_of(d->id)))) {
          fail_at("the kept download did not go on (state %s)", download_states[d->state]);
          break;
        }
        if(d->state == DL_RUNNING) {  // (with nothing missing it is done at once)
          check_missing_is_asked(multi_of(d->id), "after another session");
        }
      }
      continue;
    }
    d = the_download();
    if(paused_until >= 0 && sim_ms >= paused_until && d) {
      paused_until = -1;
      SIM_LOG("the user goes on with it");
      if(!multi_resume(d) || (d->state != DL_DONE && (d->state != DL_RUNNING || !multi_of(d->id)))) {
        fail_at("the paused download did not go on (state %s)", download_states[d->state]);
        break;
      }
      if(d->state == DL_RUNNING) {
        check_missing_is_asked(multi_of(d->id), "after a pause");
      }
    }
    // what was to be called by now
    while(!events.empty() && events.begin()->first <= sim_ms) {
      std::function<void()> fn = events.begin()->second;
      events.erase(events.begin());
      fn();
    }
    engine_step(100);
    multi_tick();
    d = the_download();
    if(!d) {
      fail_at("the download's line is gone");
      break;
    }
    if(d->received > sc.total || d->received < 0) {
      fail_at("it counts %lld bytes of %lld", d->received, sc.total);
    }
    for(const multi_t& m : multis) {
      for(int i = 0; m.id && i < m.segs; i++) {
        if(m.seg[i].from < 0 || m.seg[i].from > m.seg[i].to || m.seg[i].to > m.total || m.seg[i].got < 0) {
          fail_at("a piece out of bounds: %lld-%lld, %lld in, of %lld", m.seg[i].from, m.seg[i].to, m.seg[i].got, m.total);
        }
      }
    }
    if(d->state != DL_RUNNING && paused_until < 0) {
      break;
    }
  }
  // -- how it ended
  *took_s = (sim_ms - t0) / 1000;
  d = the_download();
  struct stat st;
  if(replaced_navs) {
    fail_at("a part's navigation was replaced by another's in the same tab, %d time(s)", replaced_navs);
  }
  if(why_fail[0]) {
    return "FAIL";
  }
  if(!d) {
    return "lost (as it may be: closed or fallen with nothing on the disk yet)";
  }
  if(d->state == DL_DONE) {
    if(cover.size() != 1 || cover.begin()->first != 0 || cover.begin()->second != sc.total) {
      fail_at("DONE with holes: %zu range(s) written, %lld of %lld bytes", cover.size(), cover_bytes(), sc.total);
    } else if(stat(d->partial, &st) || st.st_size != sc.total) {
      fail_at("DONE, but its file is not there whole under its name");
    } else if(d->received != sc.total || multi_of(d->id) || all_in_calls != 1) {
      fail_at("DONE, but it counts %lld of %lld bytes, set %d, told %d time(s)", d->received, sc.total, multi_of(d->id) != 0, all_in_calls);
    }
    return why_fail[0] ? "FAIL" : "done";
  }
  if(cancelled_by_user) {  // nothing of it may stay: no set, no file, no map
    for(int i = 0; i < 20 && !events.empty(); i++, sim_ms += 100) {  // (its cancel's answer)
      while(!events.empty() && events.begin()->first <= sim_ms) {
        std::function<void()> fn = events.begin()->second;
        events.erase(events.begin());
        fn();
      }
    }
    if(d->state != DL_CANCELLED || multi_of(d->id) || files_left()) {
      fail_at("cancelled by the user, but: state %s, set %d, %d file(s) left", download_states[d->state], multi_of(d->id) != 0, files_left());
      return "FAIL";
    }
    return "cancelled by the user (nothing left)";
  }
  if(d->state == DL_RUNNING || paused_until >= 0) {
    fail_at("never ended: %lld of %lld bytes %.0f s after the line was calm again (%d connection(s) alive, set %d)", d->received, sc.total,
            (sim_ms - calm_ms) / 1000, flowing(), multi_of(d->id) != 0);
    return "FAIL";
  }
  // failed: with a reason?
  if(!sc.ranges && own_hit) {
    return "failed (as it may: its own connection ended and this server gives no ranges)";
  }
  if(sim_ms <= fail_ok_until) {
    return "failed (as it may: its own connection failed before anything was kept)";
  }
  if(expired_hit && own_hit) {
    return "failed (as it may: its own connection ended and the link was no longer good)";
  }
  fail_at("ended %s with no reason to (ranges %d, its own connection hit %d)", download_states[d->state], sc.ranges, own_hit);
  return "FAIL";
}

// ---- two downloads at once (2026-11-14): begun from the same tab, of the same server (and one time in five of the
// same address: the link pressed twice). What one set of parts does to the other: a part's navigation that takes
// the place of the other's, bytes of one address in the other's file, a cancel that takes both, two maps kept.
struct duo_file {
  std::string key, url;      // its file's first name (the same in every session); its address
  long long total = 0;
  int end = 0;               // 0 going on, 1 done, 2 cancelled by the user, 3 failed (as it may), 4 lost (as it may be)
  bool cancelled = false;    // the user cancelled it
  bool lost_ok = false;      // the browser was closed with nothing of it on the disk
  double fail_ok_until = 0;  // its own connection failed with no file kept yet
};
static std::vector<duo_file> duo_files;

static download_t*
download_of(const std::string& key) {
  for(download_t& d : downloads) {
    if(d.id && key == d.partial) {
      return &d;
    }
  }
  return 0;
}

// A part was given a file: whose is it, and is it its own address's?
static void
duo_part_file(sim_dl* dl, const char* path) {
  for(const duo_file& f : duo_files) {
    if(!f.key.empty() && (f.key == path || f.key + ".multi" == path)) {
      const std::string asked = dl->url.substr(0, dl->url.find('#'));
      dl->file_key = f.key;
      if(asked != f.url) {
        fail_at("bytes of %s were given the file of %s", asked.c_str(), f.url.c_str());
      }
      return;
    }
  }
  fail_at("a part was given a file that is no download's: %s", path);
}

static bool
duo_set_has_file(const download_t* d) {
  const multi_t* m = d ? multi_of(d->id) : 0;
  return m && m->file[0];
}

// What is left on the disk of a download: its file by either name, its map.
static int
duo_left(const duo_file& f) {
  const std::string in_parts = f.key + ".multi";
  char map[700];
  struct stat st;
  multi_map_path(in_parts.c_str(), map, sizeof(map));
  return !stat(f.key.c_str(), &st) + !stat(in_parts.c_str(), &st) + !stat(map, &st);
}

static void
duo_begin(duo_file& f, int k) {
  static int partial_n;
  download_t* d = 0;
  for(download_t& slot : downloads) {
    if(!slot.id) {
      d = &slot;
      break;
    }
  }
  sim_dl* own = new sim_dl;
  memset(d, 0, sizeof(*d));
  d->id = ++download_ids;
  d->state = DL_RUNNING;
  d->status = 200;
  d->can_retry = d->chosen = true;
  d->total = f.total;
  snprintf(d->url, sizeof(d->url), "https://sim.example/start?x=%u", rnd());
  snprintf(d->final_url, sizeof(d->final_url), "%s", f.url.c_str());
  snprintf(d->name, sizeof(d->name), "file %d.bin", k);
  snprintf(d->kind, sizeof(d->kind), "file");
  snprintf(d->dir, sizeof(d->dir), "out");
  snprintf(d->host, sizeof(d->host), "cdn.sim.example");
  snprintf(d->partial, sizeof(d->partial), PARTIAL_DIR "/%d-%d.part", d->id, ++partial_n);
  f.key = d->partial;
  own->main = true;
  own->url = f.url;
  own->last = f.total - 1;
  own->state = 1;
  own->fd = open(d->partial, O_RDWR | O_CREAT | O_TRUNC, 0644);
  own->owner = d->id;
  own->total = f.total;
  own->path = d->partial;
  own->file_key = f.key;
  d->ref = own;
  dls.push_back(own);
  SIM_LOG("download %d begins: %lld bytes of %s", d->id, f.total, f.url.c_str());
}

static const char*
run_duo(unsigned long long seed, double* took_s) {
  static const char* const ends[] = {"?", "done", "cancelled by the user (nothing left)", "failed (as it may)", "lost (as it may be)"};
  static char how[160];
  std::vector<sim_event> plan;
  double away_until = -1;

  rng_state = seed * 2654435761ull + 88172645463325252ull;
  rng_more = seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
  for(int i = 0; i < 8; i++) {
    rnd();
    rnd_more();
  }
  // -- clean ground
  for(sim_dl* dl : dls) {
    dl_close(dl);
    delete dl;
  }
  dls.clear();
  browser_gone();
  covers.clear();
  cover_key.clear();
  own_hit_by.clear();
  all_in_by.clear();
  duo_files.clear();
  duo_mode = true;
  replaced_navs = 0;
  remove_all_in(PARTIAL_DIR);
  download_ids = 0;
  why_fail[0] = 0;
  legit_loss = own_hit = expired_hit = false;
  fail_ok_until = 0;
  net_cut_until = net_quiet_until = nav_lost_until = 0;
  all_in_calls = 0;
  sim_ms += 100000;
  // -- the server and the line, for both
  static const long long sizes[] = {40, 120, 260, 260, 700, 1500, 2400};
  const bool same = rnd() % 5 == 0;  // the same address twice
  duo_files.resize(2);
  duo_files[0].total = sizes[rnd() % 7] * 1048576LL + rnd() % 1048576;
  duo_files[1].total = same ? duo_files[0].total : sizes[rnd() % 7] * 1048576LL + rnd() % 1048576;
  duo_files[0].url = "https://cdn.sim.example/file one.bin";
  duo_files[1].url = same ? duo_files[0].url : "https://cdn.sim.example/file two.bin";
  sc.total = duo_files[0].total > duo_files[1].total ? duo_files[0].total : duo_files[1].total;
  sc.rate = (2 + rnd() % 38) * 1048576LL;
  sc.limit = rnd() % 4 == 0 ? 1 + rnd() % 6 : 99;
  sc.ranges = rnd() % 12 != 0;
  sc.latency_ms = 20 + rnd() % 600;
  sc.lose_cancel = rnd() % 5 == 0;
  sc.line = rnd() % 4 == 0 ? (long long)(sc.rate * rnd_between(1.0, 6.0)) : 0;
  sc.busy_from = sc.busy_until = sc.expire_ms = 0;
  settings.multipart = rnd() % 8 != 0;
  const long long slowest = sc.line && sc.line < sc.rate ? sc.line : sc.rate;
  const double plain_s = (double)(duo_files[0].total + duo_files[1].total) / slowest;  // one after the other, by one connection
  const double span_ms = (plain_s < 20 ? 20 : plain_s) * 1000;
  const int troubles = rnd() % 5 == 0 ? 6 + rnd() % 8 : rnd() % 6;
  double calm_ms = sim_ms + 30000;
  for(int i = 0; i < troubles; i++) {
    sim_event e;
    static const int kinds[] = {EV_CUT, EV_CUT, EV_QUIET, EV_QUIET, EV_RESET_ONE, EV_RESET_ONE, EV_TAB_BUSY, EV_TAB_CLOSED, EV_NAV_LOST, EV_LIMIT,
                                EV_CANCEL, EV_QUIT};
    e.at_ms = sim_ms + rnd_between(3000, span_ms * 0.7);
    e.kind = kinds[rnd() % (sizeof(kinds) / sizeof(kinds[0]))];
    e.len_ms = e.kind == EV_CUT ? rnd_between(500, 70000) : e.kind == EV_QUIET ? rnd_between(3000, 100000)
               : e.kind == EV_TAB_BUSY ? rnd_between(300, 4000) : e.kind == EV_NAV_LOST ? rnd_between(1000, 20000)
               : e.kind == EV_QUIT ? rnd_between(1000, 30000) : 0;
    plan.push_back(e);
    if(e.at_ms + e.len_ms > calm_ms) {
      calm_ms = e.at_ms + e.len_ms;
    }
  }
  const double t0 = sim_ms;
  const double second_ms = sim_ms + (rnd() % 2 ? 0 : rnd_between(500, 25000));  // the second: at once, or a while after
  const double deadline_ms = calm_ms + (plain_s * 2 + 1200) * 1000;
  SIM_LOG("seed %llu, two at once: %lld and %lld bytes (%s), %lld MiB/s a connection, %.1f MiB/s the line (0: no limit), the server gives %d "
          "at once, ranges %d, parts %d, %d trouble(s), latency %.0f ms; the second begins %.1f s after the first",
          seed, duo_files[0].total, duo_files[1].total, same ? "the same address" : "two addresses", sc.rate >> 20, sc.line / 1048576.0, sc.limit,
          sc.ranges, settings.multipart, troubles, sc.latency_ms, (second_ms - sim_ms) / 1000);
  duo_begin(duo_files[0], 1);
  // How each of them is: its end is checked once, when it is there. True while one of them goes on.
  auto look = [&]() {
    bool going = duo_files[1].key.empty();
    for(duo_file& f : duo_files) {
      if(f.end || f.key.empty()) {
        continue;
      }
      download_t* d = download_of(f.key);
      struct stat st;
      if(!d) {
        fail_at("the line of %s is gone", f.key.c_str());
        break;
      }
      if(d->received > f.total || d->received < 0) {
        fail_at("%s counts %lld bytes of %lld", f.key.c_str(), d->received, f.total);
      }
      if(d->state == DL_DONE) {
        cover_key = f.key;
        if(f.cancelled) {
          fail_at("%s was cancelled by the user, and ended done", f.key.c_str());
        } else if(cover.size() != 1 || cover.begin()->first != 0 || cover.begin()->second != f.total) {
          fail_at("%s is DONE with holes: %zu range(s) written, %lld of %lld bytes", f.key.c_str(), cover.size(), cover_bytes(), f.total);
        } else if(stat(d->partial, &st) || st.st_size != f.total) {
          fail_at("%s is DONE, but its file is not there whole under its name", f.key.c_str());
        } else if(d->received != f.total || multi_of(d->id) || all_in_by[f.key] != 1) {
          fail_at("%s is DONE, but it counts %lld of %lld bytes, set %d, told %d time(s)", f.key.c_str(), d->received, f.total,
                  multi_of(d->id) != 0, all_in_by[f.key]);
        }
        f.end = 1;
      } else if(d->state == DL_CANCELLED) {
        if(!f.cancelled) {
          fail_at("%s is cancelled, and the user did not cancel it", f.key.c_str());
        } else if(multi_of(d->id) || duo_left(f)) {
          fail_at("%s was cancelled by the user, but: set %d, %d file(s) of it left", f.key.c_str(), multi_of(d->id) != 0, duo_left(f));
        }
        f.end = 2;
      } else if(d->state == DL_FAILED) {
        if((!sc.ranges && own_hit_by[f.key]) || sim_ms <= f.fail_ok_until) {
          f.end = 3;
        } else {
          fail_at("%s ended failed with no reason to (ranges %d, its own connection hit %d)", f.key.c_str(), sc.ranges, (int)own_hit_by[f.key]);
        }
      } else {
        going = true;
      }
    }
    return going;
  };
  // -- the run
  for(; sim_ms < deadline_ms && !why_fail[0]; sim_ms += 100) {
    if(duo_files[1].key.empty() && sim_ms >= second_ms && away_until < 0) {
      duo_begin(duo_files[1], 2);
    }
    for(sim_event& e : plan) {
      if(!e.kind || sim_ms < e.at_ms) {
        continue;
      }
      const int kind = e.kind;
      e.kind = 0;
      if(kind == EV_CUT) {
        SIM_LOG("the line is CUT for %.1f s", e.len_ms / 1000);
        net_cut_until = sim_ms + e.len_ms;
        for(duo_file& f : duo_files) {
          download_t* d = f.key.empty() ? 0 : download_of(f.key);
          if(d && d->state == DL_RUNNING && !duo_set_has_file(d)) {
            f.fail_ok_until = sim_ms + 2000;  // its own connection fails with nothing kept yet
          }
        }
        for(size_t i = 0; i < dls.size(); i++) {
          dl_fail(dls[i], "the connection was reset");
        }
      } else if(kind == EV_QUIET) {
        SIM_LOG("the line is QUIET for %.1f s", e.len_ms / 1000);
        net_quiet_until = sim_ms + e.len_ms;
      } else if(kind == EV_RESET_ONE) {
        std::vector<sim_dl*> live;
        for(sim_dl* dl : dls) {
          if(dl->state == 1 && !dl->cancelled) {
            live.push_back(dl);
          }
        }
        if(!live.empty()) {
          sim_dl* dl = live[rnd() % live.size()];
          for(duo_file& f : duo_files) {
            if(dl->main && f.key == dl->path && !duo_set_has_file(download_of(f.key))) {
              f.fail_ok_until = sim_ms + 2000;
            }
          }
          dl_fail(dl, "the connection was reset");
        }
      } else if(kind == EV_TAB_BUSY) {
        tab_busy_until = sim_ms + e.len_ms;
        pages[0].pending = pages[1].pending = 0;
      } else if(kind == EV_TAB_CLOSED) {
        SIM_LOG("their tab is closed");
        tab_closed = true;
      } else if(kind == EV_NAV_LOST) {
        nav_lost_until = sim_ms + e.len_ms;
      } else if(kind == EV_LIMIT) {
        sc.limit = rnd() % 3 == 0 ? 99 : 1 + rnd() % 6;
        SIM_LOG("the server now gives %d connection(s) at once", sc.limit);
      } else if(kind == EV_CANCEL && away_until < 0) {
        duo_file& f = duo_files[rnd() % 2];
        download_t* d = f.key.empty() ? 0 : download_of(f.key);
        if(d && d->state == DL_RUNNING && !f.cancelled && !f.end) {
          SIM_LOG("the user cancels download %d", d->id);
          f.cancelled = true;
          download_cancel(d);
        }
      } else if(kind == EV_QUIT && away_until < 0) {
        bool any = false;
        look();  // (one that ended in this very moment is seen now: its line goes with the browser)
        for(duo_file& f : duo_files) {
          download_t* d = f.key.empty() || f.end ? 0 : download_of(f.key);
          if(d && d->state == DL_RUNNING) {
            f.lost_ok = !duo_set_has_file(d);
            own_hit_by[f.key] = true;  // (its own connection goes with the browser)
            any = true;
          }
        }
        if(any) {
          SIM_LOG("the browser is closed (%s; %s)", duo_files[0].lost_ok ? "nothing to keep of the first" : "the first has its file",
                  duo_files[1].key.empty() ? "the second not begun" : duo_files[1].lost_ok ? "nothing to keep of the second" : "the second has its file");
          multi_keep_all();
          browser_gone();
          away_until = sim_ms + e.len_ms;
        }
      }
    }
    if(away_until >= 0) {  // nobody runs
      if(sim_ms >= away_until) {
        away_until = -1;
        multi_kept_scan();
        SIM_LOG("the browser opens again");
        for(duo_file& f : duo_files) {
          if(f.end || f.key.empty()) {
            continue;
          }
          download_t* d = download_of(f.key);
          if(f.cancelled) {  // (cancelled, and the browser closed before the engine said so)
            if(d || duo_left(f)) {
              fail_at("cancelled by the user, and in the next session: its line %d, %d file(s) of it", d != 0, duo_left(f));
            }
            f.end = 2;
          } else if(!d) {
            if(!f.lost_ok) {
              fail_at("closed with its file and its map, and the next session found nothing of %s", f.key.c_str());
            }
            f.end = 4;
          } else if(!multi_resume(d) || (d->state != DL_DONE && (d->state != DL_RUNNING || !multi_of(d->id)))) {
            fail_at("the kept download %s did not go on (state %s)", f.key.c_str(), download_states[d->state]);
          } else if(d->state == DL_RUNNING) {
            cover_key = f.key;
            check_missing_is_asked(multi_of(d->id), "after another session");
          }
        }
      }
      continue;
    }
    // what was to be called by now
    while(!events.empty() && events.begin()->first <= sim_ms) {
      std::function<void()> fn = events.begin()->second;
      events.erase(events.begin());
      fn();
    }
    engine_step(100);
    multi_tick();
    for(const multi_t& m : multis) {
      for(int i = 0; m.id && i < m.segs; i++) {
        if(m.seg[i].from < 0 || m.seg[i].from > m.seg[i].to || m.seg[i].to > m.total || m.seg[i].got < 0) {
          fail_at("a piece out of bounds: %lld-%lld, %lld in, of %lld", m.seg[i].from, m.seg[i].to, m.seg[i].got, m.total);
        }
      }
    }
    if(!look()) {
      break;
    }
  }
  // -- how it ended
  *took_s = (sim_ms - t0) / 1000;
  if(replaced_navs) {
    fail_at("a part's navigation was replaced by another's in the same tab, %d time(s): two were asked of one tab at once", replaced_navs);
  }
  for(const duo_file& f : duo_files) {
    if(!f.end && !why_fail[0]) {
      const download_t* d = f.key.empty() ? 0 : download_of(f.key);
      fail_at("never ended: %s: %lld of %lld bytes %.0f s after the line was calm again (state %s, set %d, %d connection(s) alive in all)",
              f.key.empty() ? "(the second, not begun)" : f.key.c_str(), d ? d->received : 0, f.total, (sim_ms - calm_ms) / 1000,
              d ? download_states[d->state] : "none", d && multi_of(d->id), flowing());
    }
  }
  if(why_fail[0]) {
    return "FAIL";
  }
  const int a = duo_files[0].end < duo_files[1].end ? duo_files[0].end : duo_files[1].end;
  const int b = duo_files[0].end < duo_files[1].end ? duo_files[1].end : duo_files[0].end;
  snprintf(how, sizeof(how), "%s + %s", ends[a], ends[b]);
  return how;
}

int
main(int argc, char** argv) {
  const int count = argc > 2 ? atoi(argv[2]) : 2000;
  const unsigned long long first = argc > 3 ? strtoull(argv[3], 0, 10) : 1;
  std::map<std::string, int> ends;
  double sim_total = 0;

  sim_verbose = argc > 4 && argv[4][0] == 'v';
  const bool duo = argc > 5 && !strcmp(argv[5], "duo");
  if(chdir(argv[1]) || (mkdir(PARTIAL_DIR, 0755) && errno != EEXIST) || (mkdir("out", 0755) && errno != EEXIST)) {
    perror(argv[1]);
    return 2;
  }
  for(int i = 0; i < count; i++) {
    double took = 0;
    const char* how = duo ? run_duo(first + i, &took) : run_scenario(first + i, &took);
    ends[how]++;
    sim_total += took;
    if(why_fail[0]) {
      failures++;
      if(failures <= 12) {
        printf("FAIL seed %llu: %s  [%lld bytes, %lld MiB/s, limit %d, ranges %d, parts %d, %.0f s]\n", first + i, why_fail, sc.total,
               sc.rate >> 20, sc.limit, sc.ranges, settings.multipart, took);
      }
    }
  }
  for(sim_dl* dl : dls) {
    dl_close(dl);
    delete dl;
  }
  dls.clear();
  browser_gone();
  remove_all_in(PARTIAL_DIR);
  for(const auto& e : ends) {
    printf("  %6d  %s\n", e.second, e.first.c_str());
  }
  printf("%s: %d scenarios%s (%.1f hours of downloads), %d failed\n", failures ? "FAIL" : "PASS", count, duo ? " of two downloads at once" : "",
         sim_total / 3600, failures);
  return failures ? 1 : 0;
}
