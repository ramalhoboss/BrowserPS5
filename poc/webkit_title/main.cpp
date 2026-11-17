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

// webkit_title (PPSA97022; docs/05-webprocess-no-titulo.md): the WebKit
// that the browser's payloads load as libWebKit.so, linked whole into a
// title with PS5_Vulkan's title recipe, before the WebProcess moves into
// the browser's title. It answers, without the WebProcess's IPC yet:
//
//   1. Does a title of this size (all of WebKit, static) start, and do
//      WebKit's static constructors run in it? ("main reached")
//   2. Does JavaScriptCore run in a title: its allocator, its threads and
//      its JIT? A script runs for a few seconds through the C API, and the
//      time it took goes to the report, with the direct memory in use
//      (title_memory.c gives WebKit's anonymous mappings direct memory).
//   3. How fast is it next to the browser's WebProcess, a payload? The
//      tests of poc/browser/jsbench.html run five times (best of each).
//
// Every line goes to /app0/webkit-title.txt (read_file reads it as
// /data/homebrew/PPSA97022/webkit-title.txt) and to klog. The title then
// ends with _exit(): WebKit's threads are still running, and its static
// destructors must not run under them (docs/02, M1 notes).

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "title_memory.h"

#define RESULTS_PATH "/app0/webkit-title.txt"

extern "C" {
int sceKernelDebugOutText(int channel, const char* text);
int sceKernelAvailableFlexibleMemorySize(size_t* size);

// JavaScriptCore's C API (JavaScriptCore/API/JSBase.h and friends), the few
// calls used here, with its opaque types.
typedef struct OpaqueJSContext* JSGlobalContextRef;
typedef const struct OpaqueJSContext* JSContextRef;
typedef struct OpaqueJSString* JSStringRef;
typedef struct OpaqueJSClass* JSClassRef;
typedef const struct OpaqueJSValue* JSValueRef;
typedef struct OpaqueJSValue* JSObjectRef;
JSGlobalContextRef JSGlobalContextCreate(JSClassRef globalObjectClass);
void JSGlobalContextRelease(JSGlobalContextRef context);
JSStringRef JSStringCreateWithUTF8CString(const char* string);
void JSStringRelease(JSStringRef string);
size_t JSStringGetUTF8CString(JSStringRef string, char* buffer, size_t bufferSize);
JSValueRef JSEvaluateScript(JSContextRef context, JSStringRef script, JSObjectRef thisObject,
                            JSStringRef sourceURL, int startingLineNumber, JSValueRef* exception);
JSStringRef JSValueToStringCopy(JSContextRef context, JSValueRef value, JSValueRef* exception);
}

namespace WebKit {
int WebProcessMain(int argc, char** argv);
}

static FILE* results;


static void
report(const char* fmt, ...) {
  char line[512];
  char klog_line[600];
  va_list args;

  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  snprintf(klog_line, sizeof(klog_line), "[webkit_title] %s\n", line);
  sceKernelDebugOutText(0, klog_line);
  if(results) {
    fprintf(results, "%s\n", line);
    fflush(results);
  }
}


static double
now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


static size_t
flexible_mib(void) {
  size_t size = 0;
  sceKernelAvailableFlexibleMemorySize(&size);
  return size >> 20;
}


// Runs a script and reports what it gave (a string) and how long it took.
static void
run(JSGlobalContextRef context, const char* what, const char* source) {
  JSStringRef script = JSStringCreateWithUTF8CString(source);
  JSValueRef exception = 0;
  double t0 = now_ms();
  JSValueRef value = JSEvaluateScript(context, script, 0, 0, 1, &exception);
  double took = now_ms() - t0;
  JSStringRelease(script);

  char text[256] = "(nothing)";
  JSValueRef shown = value ? value : exception;
  if(shown) {
    JSStringRef string = JSValueToStringCopy(context, shown, 0);
    if(string) {
      JSStringGetUTF8CString(string, text, sizeof(text));
      JSStringRelease(string);
    }
  }
  title_memory_stats_t memory;
  title_memory_stats(&memory);
  report("%s %s: %s in %.0f ms (direct memory %zu MiB in %zu ranges, peak %zu MiB, %zu left to mmap; "
         "flexible free %zu MiB)",
         value ? "PASS" : "FAIL", what, text, took, memory.direct_bytes >> 20, memory.ranges,
         memory.peak_bytes >> 20, memory.fallbacks, flexible_mib());
}


// The tests of poc/browser/jsbench.html (and poc/jsc_shell/bench.js),
// without its DOM test: the same numbers as the browser's WebProcess, a
// payload (docs/04-jit.md: best of each, in ms, with the JIT: loop 12,
// objects 18, strings 31, regexp 40, json 237, sort 68, closures 30).
static const char* const bench_names[] = {
  "loop", "objects", "strings", "regexp", "json", "sort", "closures",
};
static const char* const bench_sources[] = {
  "(() => { let s = 0; for (let i = 0; i < 1e7; i++) s += i; return s; })()",
  "(() => { class P { constructor(x, y) { this.x = x; this.y = y; } add(o) { return new P(this.x + o.x, "
  "this.y + o.y); } } let p = new P(0, 0); const d = new P(1, 2); for (let i = 0; i < 2e6; i++) "
  "p = p.add(d); return p.x + p.y; })()",
  "(() => { const a = []; for (let i = 0; i < 2e5; i++) a.push(\"item-\" + i.toString(16)); "
  "return a.join(\",\").split(\",\").filter(x => x.indexOf(\"f\") >= 0).length; })()",
  "(() => { const re = /^([a-z]+)-(\\d+)@([a-z]+)\\.(com|pt)$/; let n = 0; for (let i = 0; i < 2e5; i++) "
  "if (re.test(\"user-\" + i + \"@mail.\" + (i & 1 ? \"com\" : \"pt\"))) n++; return n; })()",
  "(() => { const obj = {list: []}; for (let i = 0; i < 200; i++) obj.list.push({id: i, name: \"n\" + i, "
  "tags: [\"a\", \"b\", \"c\"], ok: i % 2 === 0}); let n = 0; for (let j = 0; j < 2000; j++) "
  "n += JSON.parse(JSON.stringify(obj)).list.length; return n; })()",
  "(() => { const a = []; let x = 12345; for (let i = 0; i < 2e5; i++) { x = (x * 1103515245 + 12345) "
  "& 0x7fffffff; a.push(x); } a.sort((p, q) => p - q); return a[0] <= a[1] ? \"sorted\" : \"NOT sorted\"; })()",
  "(() => { const a = []; for (let i = 0; i < 1e6; i++) a.push(i); return a.map(v => v * 2)"
  ".filter(v => v % 3 === 0).reduce((s, v) => s + v, 0); })()",
};
#define BENCH_TESTS (sizeof(bench_names) / sizeof(bench_names[0]))
#define BENCH_ROUNDS 5


// One test, its time in ms (negative when it threw).
static double
bench_one(JSGlobalContextRef context, const char* source) {
  JSStringRef script = JSStringCreateWithUTF8CString(source);
  JSValueRef exception = 0;
  double t0 = now_ms();
  JSValueRef value = JSEvaluateScript(context, script, 0, 0, 1, &exception);
  double took = now_ms() - t0;
  JSStringRelease(script);
  return value ? took : -1;
}


// BENCH_ROUNDS rounds of the tests, a line per round and the best of each.
static void
bench(JSGlobalContextRef context) {
  double best[BENCH_TESTS];
  char line[400];

  for(size_t t = 0; t < BENCH_TESTS; t++) {
    best[t] = 1e12;
  }
  for(int round = 1; round <= BENCH_ROUNDS; round++) {
    double total = 0;
    int n = snprintf(line, sizeof(line), "bench round %d:", round);
    for(size_t t = 0; t < BENCH_TESTS; t++) {
      double ms = bench_one(context, bench_sources[t]);
      if(ms < 0) {
        report("FAIL bench %s threw", bench_names[t]);
        return;
      }
      total += ms;
      if(ms < best[t]) {
        best[t] = ms;
      }
      n += snprintf(line + n, sizeof(line) - n, " %s %.0f", bench_names[t], ms);
    }
    title_memory_stats_t memory;
    title_memory_stats(&memory);
    report("%s, total %.0f ms (direct memory %zu MiB, peak %zu MiB)", line, total,
           memory.direct_bytes >> 20, memory.peak_bytes >> 20);
  }
  double total = 0;
  int n = snprintf(line, sizeof(line), "PASS bench best of %d (ms):", BENCH_ROUNDS);
  for(size_t t = 0; t < BENCH_TESTS; t++) {
    total += best[t];
    n += snprintf(line + n, sizeof(line) - n, " %s %.0f", bench_names[t], best[t]);
  }
  report("%s, sum %.0f", line, total);
}


// Kept so the linker takes all of WebKit in; never called in this test.
int (*volatile webProcessMain)(int, char**) = WebKit::WebProcessMain;


int
main(void) {
  results = fopen(RESULTS_PATH, "w");
  report("main reached: WebKit's static constructors ran (pid %d, flexible memory free: %zu MiB)",
         (int)getpid(), flexible_mib());
  // Read, so the linker keeps it (and with it the WebProcess).
  report("WebProcessMain at %p", (void*)webProcessMain);

  double t0 = now_ms();
  JSGlobalContextRef context = JSGlobalContextCreate(0);
  report("%s JSGlobalContextCreate in %.0f ms", context ? "PASS" : "FAIL", now_ms() - t0);
  if(context) {
    run(context, "a sum", "1 + 2 + 39");
    // A hot loop, long enough for the JIT's tiers (Baseline, DFG) to take
    // it over; payload numbers are in docs/04-jit.md.
    run(context, "a hot loop",
        "(function(){ function f(n){ let s = 0; for (let i = 0; i < n; i++) s = (s + i * i) % 1000003;"
        " return s; } let r = 0; for (let k = 0; k < 40; k++) r += f(1000000); return r; })()");
    // Many small objects: the allocator and the garbage collector.
    run(context, "objects",
        "(function(){ let a = []; for (let i = 0; i < 2000000; i++) { a.push({i: i, s: 'x' + (i % 100)});"
        " if (a.length > 100000) a = []; } return a.length; })()");
    // jsbench's tests, to set beside the payload's (docs/05).
    bench(context);
    JSGlobalContextRelease(context);
  }
  report("done");
  if(results) {
    fclose(results);
  }
  _exit(0);
}
