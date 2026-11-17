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

// Where WebKit's CRASH()/RELEASE_ASSERT go on BrowserPS5.
//
// Upstream they execute an int3 (WTF_FATAL_CRASH_INST in
// Source/WTF/wtf/Assertions.h). Inside an app process the system's
// debugger catches the int3 first and freezes the whole process: no signal
// handler runs and the app stays open. build-webkit.sh turns the int3 into
// "call browserps5_wtf_crash". WTFCrashWithInfo() has already put the line,
// file and function in rdi, rsi and rdx (CRASH_ARG_GPR0..2), the argument
// registers, and the extra values in r11 and r10, so this prints
//
//   [WebKit] CRASH Source/WTF/wtf/Foo.cpp:123 in void WTF::foo()
//
// on stderr (the payload's connection) and exits. A plain CRASH() leaves
// junk in those registers, so the strings are only printed when they lie
// close to this code (the same library's read-only data); the call site is
// always printed, as an offset to look up with nm.
//
// Then the callers: the words on the stack that point into this library's
// code, as offsets into it (llvm-symbolizer --obj=libWebKit.so 0x...).
// Some are stale values, not frames, but the real callers are among them;
// a stack scan needs neither frame pointers nor the unwinder, which would
// have to find .eh_frame through the SDK's loader.
//
// browserps5_crash_signals() does the same for faults (SIGSEGV, SIGBUS,
// SIGILL, SIGFPE, SIGABRT, SIGTRAP), from the stack of the faulting code:
//
//   [WebKit] CRASH signal 11 code 1 addr 0x0 (pid 178)
//   [WebKit]   rip 0x..., library offset 0x...   (or: not in this library)
//
// WebProcessMain() and NetworkProcessMain() in libWebKit install it
// (build-webkit.sh): a fault in those processes left no trace before, the
// UIProcess only saw the process go (G2, 2026-11-04). A rip outside this
// library is most likely Mesa's: the compositor logs where libEGL's
// glGetString is ("composited by ..."), and the words near rip are printed
// too.
//
// The scans stop at the top of the stack where it is known: for WTF's
// CRASH(), the calling thread's (pthread_attr_get_np); for a fault, the main
// thread's, taken when browserps5_crash_signals() ran (nothing that may
// allocate inside a signal handler). Past the top the scan faulted, and the
// UIProcess's own handler printed a second crash (V3, 2026-11-04).

//
// browserps5_debug_env() is for trying options without a new build: each
// NAME=value line of DEBUG_ENV_FILE goes into the environment of the process
// that calls it, e.g. Mesa's ZINK_DEBUG or GALLIUM_THREAD. Without the file
// only the defaults are set (WEBKIT_GST_VIDEO_DECODING_LIMIT, for the
// largest video). browserps5_crash_signals() calls it first, so the
// WebProcess and the NetworkProcess read it before WebKit starts.
//
// browserps5_crash_hook() takes a function to run before a crashing process
// exits (ps5audiosink: close the sound ports).
//
// BROWSERPS5_STACK_DUMPS=15,30 in that file prints the stack of the
// process's main thread (the one that called browserps5_crash_signals()) at
// those seconds after it starts, the same way as for a crash, and goes on:
// for a WebProcess that stops answering, where its main thread waits (V3,
// YouTube, 2026-11-04). SIGUSR2 interrupts it (WTF suspends threads with
// SIGUSR1).
//
// BROWSERPS5_LOAD_PROBE=1 in that file: once a second, how much CPU the
// process used (getrusage, as cores busy on average) and how late a thread
// that sleeps 5 ms at a time woke up (the worst and the mean), e.g.
//
//   [WebKit] load pid 110 at 42 s: 1.93 cores, woke up late by 312 ms max, 41.2 ms mean
//
// Every payload thread is SCHED_FIFO 700 (256 the highest, 767 the lowest),
// and FIFO threads of one priority do not take turns: a busy one keeps its
// core until it blocks. The YouTube page froze for 10-20 s at a time, the
// TV getting 2-6 of its frames a second and the video ~1 (2026-11-04
// 17:05): this says whether the process's threads were waiting for a core.
// BROWSERPS5_LOAD_PROBE_PRIORITY=n gives the probe thread priority n, to see
// whether a thread above the others would have woken in time. The thread
// sleeps nearly all the time.
//
// The spin guard (on unless BROWSERPS5_SPIN_GUARD=0 in that file; =log
// only writes): on 2026-11-05 at 13:15, opening YouTube from the start page
// left the WebProcess with 2.2 cores busy for good, a ThreadedCompositor
// thread (priority 690) at 0.73 cores and ~1.5 cores in threads WebKit did
// not make (Mesa's), and its main thread at 0. The UIProcess, the SMP and
// the other payloads (priority 700) got no CPU, and the console looked dead
// until it was restarted; the freeze of 12:30 looked the same
// (docs/08-4k.md). A thread at priority 400, above all of ours, looks once a
// second for that: (A) 1.5 cores or more with a compositor thread at 0.4 or
// more, 5 s in a row, or (B) 1.8 cores or more with the threads WebKit made
// at 0.3 or less together, 8 s in a row; the main thread at 0.05 or less in
// both. Then it prints the stacks of the busiest thread and of the main
// thread, runs the crash hooks (the sound ports) and ends the process: the
// UIProcess reloads its panes, and a page that goes again within a minute
// gets its "the page stopped" page instead.

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <pthread_np.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "../libc-fixes/known_threads.h"
#include "video_ring.h"  // ../../common

#define DEBUG_ENV_FILE "/user/homebrew/browserps5/data/env.txt"
// 2026-11-11: BrowserPS5 RN's own, read after it (a test of RN's then leaves
// the first one, in the other browser's folder, as it is).
#define DEBUG_ENV_FILE_RN "/user/homebrew/browserps5-rn/data/env.txt"
// And the settings an engine of RN's own comes with (browser-rn/pack-lib-rn.sh
// puts the file in the engine's tar): BROWSERPS5_SQLITE_BATCH=1. Read before
// the one above, which is for tests.
#define ENGINE_ENV_FILE_RN "/user/homebrew/browserps5-rn/engine-env.txt"

// libkernel_web has this one (not pthread_get_name_np, which pthread_np.h
// declares); a 32-byte buffer is the longest name.
int pthread_getname_np(pthread_t thread, char* name, size_t size);

void browserps5_wtf_crash_report(long line, const char* file,
                                 const char* function, uintptr_t site,
                                 uintptr_t reason, uintptr_t misc1)
  __attribute__((noreturn, used));
void browserps5_crash_signals(void);
void browserps5_debug_probes(void);
void browserps5_debug_env(void);
void browserps5_crash_hook(void (*hook)(void));
void browserps5_report_fd(int fd);
void browserps5_dump_thread(pthread_t thread);
void browserps5_crash_end(void (*end)(int code));
void browserps5_stack_scan(const void* sp, uintptr_t top, uintptr_t pc);
void browserps5_code_end(uintptr_t end);
void browserps5_frame_walk(const void* fp, const void* sp, uintptr_t top);

// Where "our code" ends for the stack scans: this file's last function in
// libWebKit.so, or what the browser title sets. The title's eboot has Mesa
// and RADV after WebKit, and their frames were left out (2026-11-05 22:55:
// a crash inside RADV's vkEnumeratePhysicalDevices showed no RADV caller).
static uintptr_t code_end;

void
browserps5_code_end(uintptr_t end) {
  code_end = end;
}

static uintptr_t
our_code_end(void) {
  return code_end ? code_end : (uintptr_t)&browserps5_wtf_crash_report;
}

// Where the reports go: descriptor 2, or the browser title's own file (a
// title keeps no descriptor 2 and cannot dup2 one, 2026-11-05 19:47).
static int report_fd = STDERR_FILENO;

void
browserps5_report_fd(int fd) {
  report_fd = fd;
}

// What ends a crashing process: _exit() with its code, or the function
// given here, which must not return. The browser title, where the
// WebProcess is a thread, parks the thread and closes the title its own way
// (web_host.cpp): a title that dies with the GPU and its clients open takes
// the console down (2026-11-05 21:01).
static void (*crash_end)(int code);

void
browserps5_crash_end(void (*end)(int code)) {
  crash_end = end;
}

static void __attribute__((noreturn))
end_crash(int code) {
  if(crash_end) {
    crash_end(code);
  }
  _exit(code);
}

// The start of this library (lld defines it). Its code ends a little after
// this file's: the archive is linked last (not etext, which lld left
// undefined, 2026-11-03).
extern const char __ehdr_start[] __attribute__((visibility("hidden")));

#define SCAN_BYTES  (48 * 1024)  // of stack, above the crash
#define MAX_CALLERS 40
// BROWSERPS5_STACK_CALLERS=<n> (2026-11-11): more of them in the stack dumps
// (a thread deep inside SQLite showed nothing of what had called it).
static int max_callers = MAX_CALLERS;
#define NEAR_RIP    (32u << 20)  // bytes either side, for a rip elsewhere
// Where the payload loader put the libraries so far (libWebKit, libEGL and
// libvulkan at 0x20xxxxxxx on 2026-11-04).
#define LIBRARIES_LO 0x200000000ul
#define LIBRARIES_HI 0x220000000ul
#define STACK_DUMP_SIGNAL SIGUSR2
#define MAX_STACK_DUMPS   40

// browserps5_crash_hook(): what a crashing process must let go of before it
// exits. ps5audiosink gives the function that closes its sound ports: a
// WebProcess that went with a port open left the console's sound and app
// services stuck (V2, V3), and on the TV test of 2026-11-04 the console
// went down when the WebProcess crashed with one open (the title had been
// closed from the system). Each hook runs once, before the report (which
// can fault in turn).
#define MAX_CRASH_HOOKS 4

static void (*crash_hooks[MAX_CRASH_HOOKS])(void);

void
browserps5_crash_hook(void (*hook)(void)) {
  for(int i = 0; i < MAX_CRASH_HOOKS; i++) {
    void (*none)(void) = 0;
    if(__atomic_compare_exchange_n(&crash_hooks[i], &none, hook, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
       || none == hook) {
      return;
    }
  }
}


static void
run_crash_hooks(void) {
  for(int i = 0; i < MAX_CRASH_HOOKS; i++) {
    void (*hook)(void) = __atomic_exchange_n(&crash_hooks[i], 0,
                                             __ATOMIC_ACQ_REL);
    if(hook) {
      hook();
    }
  }
}


// Called from inline asm with any stack alignment: realign it, pass the
// return address (the crash site) and r11/r10, and call the C part.
__asm__(".text\n"
        ".globl browserps5_wtf_crash\n"
        ".type browserps5_wtf_crash, @function\n"
        "browserps5_wtf_crash:\n"
        "  mov (%rsp), %rcx\n"
        "  mov %r11, %r8\n"
        "  mov %r10, %r9\n"
        "  and $-16, %rsp\n"
        "  call browserps5_wtf_crash_report\n"
        "  ud2\n"
        ".size browserps5_wtf_crash, .-browserps5_wtf_crash\n");


// The words of the stack from sp up (bytes of it) that point into this
// library's code, as offsets into it. Printed as found: if the scan runs
// off the top of the stack, what came before is out.
static void
print_callers(const uintptr_t* sp, size_t bytes, uintptr_t site) {
  uintptr_t base = (uintptr_t)__ehdr_start;
  uintptr_t end = our_code_end();
  char msg[128];
  int callers = 0;

  for(size_t i = 0; i < bytes / sizeof(*sp) && callers < max_callers; i++) {
    uintptr_t word = sp[i];
    if(word > base + 0x1000 && word < end && word != site) {
      int n = snprintf(msg, sizeof(msg), "[WebKit]   caller? 0x%lx (stack +%zu)\n",
                       (unsigned long)(word - base), i * sizeof(*sp));
      if(n > 0) {
        write(report_fd, msg, (size_t)n);
      }
      callers++;
    }
  }
}


// The words of the stack from sp up (bytes of it) that point into the other
// libraries.
static void
print_other_libraries(const uintptr_t* sp, size_t bytes) {
  uintptr_t base = (uintptr_t)__ehdr_start;
  uintptr_t end = our_code_end();
  char msg[128];
  int shown = 0;

  for(size_t i = 0; i < bytes / sizeof(*sp) && shown < MAX_CALLERS; i++) {
    uintptr_t word = sp[i];
    if(word >= LIBRARIES_LO && word < LIBRARIES_HI && !(word > base && word < end)) {
      int n = snprintf(msg, sizeof(msg), "[WebKit]   other library? 0x%lx (stack +%zu)\n",
                       (unsigned long)word, i * sizeof(*sp));
      if(n > 0) {
        write(report_fd, msg, (size_t)n);
      }
      shown++;
    }
  }
}


// The main thread (the one that called browserps5_crash_signals()) and the
// top of its stack; 0 when unknown.
static pthread_t main_thread;
static uintptr_t main_stack_top;


// The top of the calling thread's stack, or 0. Not from a signal handler:
// pthread_attr_get_np() may allocate.
static uintptr_t
this_stack_top(void) {
  pthread_attr_t attr;
  void* stack = 0;
  size_t size = 0;

  if(pthread_attr_init(&attr)) {
    return 0;
  }
  if(!pthread_attr_get_np(pthread_self(), &attr)) {
    pthread_attr_getstack(&attr, &stack, &size);
  }
  pthread_attr_destroy(&attr);
  return stack && size ? (uintptr_t)stack + size : 0;
}


// How much of the stack to scan from sp up: SCAN_BYTES, or up to top when
// that comes first.
static size_t
scan_bytes(const void* sp, uintptr_t top) {
  uintptr_t at = (uintptr_t)sp;

  if(top > at && top - at < SCAN_BYTES) {
    return top - at;
  }
  return SCAN_BYTES;
}


// Strings of the crashing library are within its image, near this code.
static int
near_here(const void* p) {
  uintptr_t self = (uintptr_t)&browserps5_wtf_crash_report;
  uintptr_t addr = (uintptr_t)p;

  return addr && (addr > self ? addr - self : self - addr) < 0x10000000;
}


void
browserps5_wtf_crash_report(long line, const char* file, const char* function,
                            uintptr_t site, uintptr_t reason, uintptr_t misc1) {
  uintptr_t self = (uintptr_t)&browserps5_wtf_crash_report;
  char msg[1024];
  int n;

  run_crash_hooks();
  if(near_here(file) && near_here(function) && line > 0 && line < 1000000) {
    n = snprintf(msg, sizeof(msg),
                 "[WebKit] CRASH %s:%ld in %s (pid %d, reason 0x%lx 0x%lx)\n",
                 file, line, function, (int)getpid(), (unsigned long)reason,
                 (unsigned long)misc1);
  } else {
    n = snprintf(msg, sizeof(msg), "[WebKit] CRASH (no file/line, pid %d)\n",
                 (int)getpid());
  }
  if(n > 0) {
    write(report_fd, msg, (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1);
  }

  uintptr_t base = (uintptr_t)__ehdr_start;
  n = snprintf(msg, sizeof(msg),
               "[WebKit]   at 0x%lx, %+ld from browserps5_wtf_crash_report, "
               "library offset 0x%lx\n", (unsigned long)site,
               (long)(site - self), (unsigned long)(site - base));
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }

  const uintptr_t* sp = (const uintptr_t*)__builtin_frame_address(0);
  print_callers(sp, scan_bytes(sp, this_stack_top()), site);
  end_crash(134);
}


static void
crash_signal(int sig, siginfo_t* info, void* context) {
  static volatile sig_atomic_t busy;
  const ucontext_t* uc = (const ucontext_t*)context;
  uintptr_t base = (uintptr_t)__ehdr_start;
  uintptr_t end = our_code_end();
  uintptr_t rip = (uintptr_t)uc->uc_mcontext.mc_rip;
  const uintptr_t* sp = (const uintptr_t*)uc->uc_mcontext.mc_rsp;
  char msg[256];
  int n;

  // A fault while printing lands here again: just leave (hooks that have
  // not run yet first).
  if(busy) {
    run_crash_hooks();
    end_crash(128 + sig);
  }
  busy = 1;
  run_crash_hooks();
  size_t bytes = scan_bytes(sp, pthread_equal(pthread_self(), main_thread)
                                ? main_stack_top : 0);

  n = snprintf(msg, sizeof(msg),
               "[WebKit] CRASH signal %d code %d addr %p (pid %d)\n", sig,
               info->si_code, info->si_addr, (int)getpid());
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  if(rip > base && rip < end) {
    n = snprintf(msg, sizeof(msg), "[WebKit]   rip 0x%lx, library offset 0x%lx\n",
                 (unsigned long)rip, (unsigned long)(rip - base));
  } else {
    n = snprintf(msg, sizeof(msg), "[WebKit]   rip 0x%lx, not in this library\n",
                 (unsigned long)rip);
  }
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  // mc_err for a page fault (trap 12): 1 the page was there, 2 a write,
  // 0x10 an instruction fetch.
  char name[32] = "";
  if(pthread_getname_np(pthread_self(), name, sizeof(name))) {
    name[0] = 0;
  }
  name[sizeof(name) - 1] = 0;
  n = snprintf(msg, sizeof(msg),
               "[WebKit]   trap %u err 0x%lx rsp 0x%lx rbp 0x%lx base 0x%lx thread \"%s\"\n",
               (unsigned)uc->uc_mcontext.mc_trapno,
               (unsigned long)uc->uc_mcontext.mc_err, (unsigned long)sp,
               (unsigned long)uc->uc_mcontext.mc_rbp, (unsigned long)base, name);
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  // The 128 bytes below rsp (the red zone, which the kernel leaves alone
  // when it puts the signal frame on this stack): what last ran here.
  for(size_t i = 16; i > 0; i--) {
    n = snprintf(msg, sizeof(msg), "[WebKit]   rsp-%zu 0x%lx\n", i * sizeof(*sp),
                 (unsigned long)sp[-(ptrdiff_t)i]);
    if(n > 0) {
      write(report_fd, msg, (size_t)n);
    }
  }
  // The top of the stack as it is (a call to a bad pointer left its return
  // address at rsp), and code-looking words of any library above it.
  for(size_t i = 0; i < 4; i++) {
    n = snprintf(msg, sizeof(msg), "[WebKit]   rsp+%zu 0x%lx\n", i * sizeof(*sp),
                 (unsigned long)sp[i]);
    if(n > 0) {
      write(report_fd, msg, (size_t)n);
    }
  }
  print_other_libraries(sp, bytes);

  // Outside this library: the words near rip, likely that library's callers.
  if(!(rip > base && rip < end)) {
    int shown = 0;
    for(size_t i = 0; i < bytes / sizeof(*sp) && shown < MAX_CALLERS / 2;
        i++) {
      uintptr_t word = sp[i];
      if(word != rip && word + NEAR_RIP > rip && word < rip + NEAR_RIP) {
        n = snprintf(msg, sizeof(msg), "[WebKit]   near rip? 0x%lx (stack +%zu)\n",
                     (unsigned long)word, i * sizeof(*sp));
        if(n > 0) {
          write(report_fd, msg, (size_t)n);
        }
        shown++;
      }
    }
  }

  print_callers(sp, bytes, rip);
  end_crash(128 + sig);
}


static unsigned stack_dump_at[MAX_STACK_DUMPS];
static int stack_dumps;

// For a thread other than the main one (the spin guard's dumps): the top
// of its stack, set before the signal; and +1 when the handler is done.
static uintptr_t dump_top;
static unsigned dump_done;


// A thread's stack, while it goes on: the main thread's for
// BROWSERPS5_STACK_DUMPS, any for the spin guard. The context the signal
// brings had rip 0x1 and no rsp (2026-11-04), so the scan starts here: the
// handler runs on the interrupted thread's stack, just below its frames. It
// stops at the top of the stack (reading past it faulted, in the
// NetworkProcess); with the top unknown, one page only.
static void
stack_dump_signal(int sig, siginfo_t* info, void* context) {
  const uintptr_t* sp = (const uintptr_t*)__builtin_frame_address(0);
  int is_main = pthread_equal(pthread_self(), main_thread);
  uintptr_t top = is_main ? main_stack_top : __atomic_load_n(&dump_top, __ATOMIC_ACQUIRE);
  size_t bytes = top > (uintptr_t)sp ? top - (uintptr_t)sp : 4096;
  char name[32] = "";
  char msg[128];
  int n;

  (void)sig;
  (void)info;
  (void)context;
  if(bytes > SCAN_BYTES) {
    bytes = SCAN_BYTES;
  }
  if(!is_main && pthread_getname_np(pthread_self(), name, sizeof(name))) {
    name[0] = 0;
  }
  name[sizeof(name) - 1] = 0;
  n = snprintf(msg, sizeof(msg), "[WebKit] STACK of %s%s%s (pid %d)\n",
               is_main ? "the main thread" : "thread \"", name, is_main ? "" : "\"",
               (int)getpid());
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  print_other_libraries(sp, bytes);
  print_callers(sp, bytes, 0);
  write(report_fd, "[WebKit] STACK end\n", 19);
  __atomic_add_fetch(&dump_done, 1, __ATOMIC_ACQ_REL);
}


static void
stack_dump_handler(void) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_sigaction = stack_dump_signal;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  sigaction(STACK_DUMP_SIGNAL, &action, 0);
}


// BROWSERPS5_STACK_DUMP_THREAD (below, after guard_dump()).
static void dump_named_threads(unsigned at);
// The process's last sync messages (WebKit's Connection.cpp, patched: weak,
// a build without that patch has none).
void browserps5_ipc_notes(void) __attribute__((weak));

static void*
stack_dump_thread(void* arg) {
  unsigned slept = 0;

  (void)arg;
  for(int i = 0; i < stack_dumps; i++) {
    char msg[96];
    if(stack_dump_at[i] > slept) {
      sleep(stack_dump_at[i] - slept);
      slept = stack_dump_at[i];
    }
    // Not stdio: a thread stuck while holding stderr's lock would stop this
    // one too.
    int n = snprintf(msg, sizeof(msg), "[WebKit] stack of the main thread at %u s (pid %d):\n",
                     slept, (int)getpid());
    if(n > 0) {
      write(report_fd, msg, (size_t)n);
    }
    pthread_kill(main_thread, STACK_DUMP_SIGNAL);
    dump_named_threads(slept);
    if(browserps5_ipc_notes) {
      usleep(300000);  // after the stacks
      browserps5_ipc_notes();
    }
  }
  return 0;
}


static double
monotonic_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


// The CPU time of the whole process so far, -1 if it cannot be had.
static double
process_cpu_ms(void) {
  struct rusage usage;
  if(getrusage(RUSAGE_SELF, &usage)) {
    return -1;
  }
  return usage.ru_utime.tv_sec * 1e3 + usage.ru_utime.tv_usec / 1e3
         + usage.ru_stime.tv_sec * 1e3 + usage.ru_stime.tv_usec / 1e3;
}


#define LOAD_PROBE_SLEEP_US 5000
#define TOP_THREADS         4

// The threads libc-fixes.c lists (weak: without it, the main thread only).
const browserps5_known_thread_t* browserps5_known_threads(unsigned* count)
    __attribute__((weak));
int browserps5_thread_clock(clockid_t* clock) __attribute__((weak));

// The main thread's CPU clock, taken on it (start_load_probe()).
static clockid_t main_clock;
static int main_clock_known;

// Each listed thread's CPU time at the last line, the main thread's last;
// and the slot's generation then (a new thread in the slot starts from 0).
static double thread_last_ms[BROWSERPS5_MAX_KNOWN_THREADS + 1];
static unsigned thread_last_gen[BROWSERPS5_MAX_KNOWN_THREADS];

typedef struct {
  double ms;
  int slot;  // -1: the main thread
} busy_thread_t;


static void
consider(busy_thread_t* top, double ms, int slot) {
  for(int i = 0; i < TOP_THREADS; i++) {
    if(ms > top[i].ms) {
      memmove(&top[i + 1], &top[i], (TOP_THREADS - 1 - i) * sizeof(*top));
      top[i].ms = ms;
      top[i].slot = slot;
      return;
    }
  }
}


static double
clock_ms(clockid_t clock) {
  struct timespec t;
  if(clock_gettime(clock, &t)) {
    return -1;
  }
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


// " top: name 0.95, name 0.40, ..." for the threads that used the most CPU
// since the last call, over span_ms.
static int
top_threads(char* out, size_t size, double span_ms) {
  busy_thread_t top[TOP_THREADS];
  const browserps5_known_thread_t* list = 0;
  unsigned count = 0;

  memset(top, 0, sizeof(top));
  if(main_clock_known) {
    double ms = clock_ms(main_clock);
    if(ms >= 0) {
      consider(top, ms - thread_last_ms[BROWSERPS5_MAX_KNOWN_THREADS], -1);
      thread_last_ms[BROWSERPS5_MAX_KNOWN_THREADS] = ms;
    }
  }
  if(browserps5_known_threads) {
    list = browserps5_known_threads(&count);
  }
  for(unsigned i = 0; i < count; i++) {
    if(!__atomic_load_n(&list[i].alive, __ATOMIC_ACQUIRE)) {
      continue;
    }
    unsigned gen = __atomic_load_n(&list[i].generation, __ATOMIC_ACQUIRE);
    double ms = clock_ms(list[i].clock);
    if(ms < 0) {
      continue;
    }
    if(gen != thread_last_gen[i]) {
      thread_last_gen[i] = gen;
      thread_last_ms[i] = 0;
    }
    consider(top, ms - thread_last_ms[i], (int)i);
    thread_last_ms[i] = ms;
  }

  int n = snprintf(out, size, " top:");
  for(int i = 0; i < TOP_THREADS && top[i].ms > 0 && n > 0 && (size_t)n < size; i++) {
    char name[32] = "";
    if(top[i].slot < 0) {
      strcpy(name, "main");
    } else if(__atomic_load_n(&list[top[i].slot].alive, __ATOMIC_ACQUIRE)) {
      pthread_getname_np(list[top[i].slot].thread, name, sizeof(name));
    }
    if(!name[0]) {
      snprintf(name, sizeof(name), "#%d", top[i].slot);
    }
    n += snprintf(out + n, size - n, "%s %s %.2f", i ? "," : "", name,
                  top[i].ms / span_ms);
  }
  return n;
}


// BROWSERPS5_LOAD_PROBE: see the comment at the top.
static void*
load_probe_thread(void* arg) {
  int priority = (int)(intptr_t)arg;
  int policy = -1;
  struct sched_param param;

  memset(&param, 0, sizeof(param));
  pthread_getschedparam(pthread_self(), &policy, &param);
  if(priority > 0) {
    param.sched_priority = priority;
    pthread_setschedparam(pthread_self(), policy, &param);
    pthread_getschedparam(pthread_self(), &policy, &param);
  }
  char msg[400];
  char top[240];
  int n = snprintf(msg, sizeof(msg), "[WebKit] load probe in pid %d: policy %d "
                   "priority %d, a line a second\n", (int)getpid(), policy,
                   param.sched_priority);
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }

  double start = monotonic_ms();
  double second = start;
  double cpu = process_cpu_ms();
  double late_max = 0;
  double late_sum = 0;
  unsigned wakes = 0;
  for(;;) {
    double before = monotonic_ms();
    usleep(LOAD_PROBE_SLEEP_US);
    double now = monotonic_ms();
    double late = now - before - LOAD_PROBE_SLEEP_US / 1e3;
    if(late < 0) {
      late = 0;
    }
    if(late > late_max) {
      late_max = late;
    }
    late_sum += late;
    wakes++;
    if(now - second >= 1000) {
      double cpu_now = process_cpu_ms();
      double cores = cpu < 0 || cpu_now < 0 ? -1 : (cpu_now - cpu) / (now - second);
      if(top_threads(top, sizeof(top), now - second) < 0) {
        top[0] = 0;
      }
      // Not stdio, as for the stack dumps.
      n = snprintf(msg, sizeof(msg), "[WebKit] load pid %d at %.0f s: %.2f cores, "
                   "woke up late by %.0f ms max, %.1f ms mean;%s\n", (int)getpid(),
                   (now - start) / 1e3, cores, late_max, late_sum / wakes, top);
      if(n > 0) {
        write(report_fd, msg, (size_t)n);
      }
      second = now;
      cpu = cpu_now;
      late_max = late_sum = 0;
      wakes = 0;
    }
  }
  return 0;
}


static void
start_load_probe(void) {
  const char* on = getenv("BROWSERPS5_LOAD_PROBE");
  const char* priority = getenv("BROWSERPS5_LOAD_PROBE_PRIORITY");
  pthread_t thread;

  if(!on || !atoi(on)) {
    return;
  }
  intptr_t p = priority ? atoi(priority) : 0;
  if(!pthread_create(&thread, 0, load_probe_thread, (void*)p)) {
    pthread_detach(thread);
  }
}


// The spin guard (see the comment at the top). What it looks at, a second
// at a time:
#define SPIN_GUARD_PRIORITY      400  // above every thread of ours (690-767)
#define SPIN_GUARD_CORES_A       1.5  // A: the process, with
#define SPIN_GUARD_COMPOSITOR_A  0.4  //    a compositor thread this busy,
#define SPIN_GUARD_SECONDS_A     5    //    so many seconds in a row
#define SPIN_GUARD_CORES_B       1.8  // B: the process, with
#define SPIN_GUARD_KNOWN_B       0.3  //    the known threads together at most this,
#define SPIN_GUARD_SECONDS_B     8    //    so many seconds in a row
#define SPIN_GUARD_MAIN          0.05 // A and B: the main thread at most this
#define SPIN_GUARD_EXIT          75

// Each known thread's CPU time and slot generation at the last look.
static double guard_last_ms[BROWSERPS5_MAX_KNOWN_THREADS];
static unsigned guard_last_gen[BROWSERPS5_MAX_KNOWN_THREADS];
static double guard_main_last_ms;


static void
guard_say(const char* msg, int n) {
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
}


// thread's stack in the log (stack_dump_signal() on it), waiting up to
// half a second for it.
static void
guard_dump(pthread_t thread) {
  uintptr_t top = 0;
  pthread_attr_t attr;

  if(!pthread_equal(thread, main_thread) && !pthread_attr_init(&attr)) {
    void* stack = 0;
    size_t size = 0;
    if(!pthread_attr_get_np(thread, &attr) && !pthread_attr_getstack(&attr, &stack, &size)
       && stack && size) {
      top = (uintptr_t)stack + size;
    }
    pthread_attr_destroy(&attr);
  }
  unsigned before = __atomic_load_n(&dump_done, __ATOMIC_ACQUIRE);
  __atomic_store_n(&dump_top, top, __ATOMIC_RELEASE);
  if(pthread_kill(thread, STACK_DUMP_SIGNAL)) {
    return;
  }
  for(int i = 0; i < 50 && __atomic_load_n(&dump_done, __ATOMIC_ACQUIRE) == before; i++) {
    usleep(10000);
  }
}


// BROWSERPS5_STACK_DUMP_THREAD=<part of a name> (2026-11-11): at each of
// BROWSERPS5_STACK_DUMPS's seconds, after the main thread's, the stack of
// every listed thread whose name has that text. For the NetworkProcess's
// storage queue ("Storage"): a page's thread waited up to minutes for its
// answer (sessionStorage's first use, a sync message) with WhatsApp syncing.
static void
dump_named_threads(unsigned at) {
  const char* part = getenv("BROWSERPS5_STACK_DUMP_THREAD");
  const browserps5_known_thread_t* list = 0;
  unsigned count = 0;

  if(!part || !part[0] || !browserps5_known_threads) {
    return;
  }
  usleep(300000);  // the main thread's dump first, whole
  list = browserps5_known_threads(&count);
  for(unsigned i = 0; i < count; i++) {
    char name[32] = "";
    char msg[128];
    if(!__atomic_load_n(&list[i].alive, __ATOMIC_ACQUIRE)) {
      continue;
    }
    if(pthread_getname_np(list[i].thread, name, sizeof(name))) {
      continue;
    }
    name[sizeof(name) - 1] = 0;
    if(!strstr(name, part)) {
      continue;
    }
    int n = snprintf(msg, sizeof(msg), "[WebKit] stack of thread \"%s\" at %u s (pid %d):\n",
                     name, at, (int)getpid());
    if(n > 0) {
      write(report_fd, msg, (size_t)n);
    }
    guard_dump(list[i].thread);
  }
}


// One thread's stack in the report, now (the browser title's diagnostics:
// web_host.cpp dumps WebKit's named threads).
void
browserps5_dump_thread(pthread_t thread) {
  stack_dump_handler();
  guard_dump(thread);
}


// A crashed thread's stack in the report, from sp (the fault's) up to top
// (0: one page), and the place pc: the browser title's crash hook, in a
// signal handler, with the top the thread noted when it started (nothing
// here allocates).
void
browserps5_stack_scan(const void* sp, uintptr_t top, uintptr_t pc) {
  const uintptr_t* words = (const uintptr_t*)sp;
  size_t bytes = top > (uintptr_t)sp ? top - (uintptr_t)sp : 4096;
  char msg[160];
  int n;

  if(bytes > SCAN_BYTES) {
    bytes = SCAN_BYTES;
  }
  n = snprintf(msg, sizeof(msg),
               "[WebKit] STACK of the crashed thread (pid %d), pc offset 0x%lx, %zu bytes\n",
               (int)getpid(), (unsigned long)(pc - (uintptr_t)__ehdr_start), bytes);
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  print_other_libraries(words, bytes);
  print_callers(words, bytes, pc);
  write(report_fd, "[WebKit] STACK end\n", 19);
}


// The chain of frame pointers from fp (the crashed thread's rbp) while it
// stays on the stack [sp, top) and goes up: each frame's return address,
// as an offset when it is our code. Mesa, RADV and WebKit keep rbp as the
// frame pointer, so this is the exact way back where the scan only guesses.
// At a "ret" to a bad address, rbp is already the caller's: the chain then
// starts one frame up.
void
browserps5_frame_walk(const void* fp, const void* sp, uintptr_t top) {
  const uintptr_t base = (uintptr_t)__ehdr_start;
  const uintptr_t end = our_code_end();
  uintptr_t at = (uintptr_t)fp;
  char msg[160];
  int n = snprintf(msg, sizeof(msg), "[WebKit] FRAMES from rbp 0x%lx (stack +%ld)\n",
                   (unsigned long)at, (long)(at - (uintptr_t)sp));
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  for(int i = 0; i < 64; i++) {
    if(at < (uintptr_t)sp || at + 2 * sizeof(uintptr_t) > top || (at & 7)) {
      break;
    }
    const uintptr_t* frame = (const uintptr_t*)at;
    const uintptr_t ret = frame[1];
    if(ret > base && ret < end) {
      n = snprintf(msg, sizeof(msg), "[WebKit]   frame #%d: caller? 0x%lx (stack +%ld)\n", i,
                   (unsigned long)(ret - base), (long)(at - (uintptr_t)sp));
    } else {
      n = snprintf(msg, sizeof(msg), "[WebKit]   frame #%d: return 0x%lx, not our code (stack +%ld)\n",
                   i, (unsigned long)ret, (long)(at - (uintptr_t)sp));
    }
    if(n > 0) {
      write(report_fd, msg, (size_t)n);
    }
    if(frame[0] <= at) {
      break;
    }
    at = frame[0];
  }
  write(report_fd, "[WebKit] FRAMES end\n", 20);
}


static void*
spin_guard_thread(void* arg) {
  const int log_only = (int)(intptr_t)arg;
  int policy = -1;
  struct sched_param param;
  char msg[400];
  int n;

  memset(&param, 0, sizeof(param));
  pthread_getschedparam(pthread_self(), &policy, &param);
  param.sched_priority = SPIN_GUARD_PRIORITY;
  pthread_setschedparam(pthread_self(), policy, &param);
  pthread_getschedparam(pthread_self(), &policy, &param);
  n = snprintf(msg, sizeof(msg), "[WebKit] spin guard in pid %d: priority %d%s\n",
               (int)getpid(), param.sched_priority, log_only ? ", log only" : "");
  guard_say(msg, n);

  double then = monotonic_ms();
  double cpu_then = process_cpu_ms();
  int seconds_a = 0;
  int seconds_b = 0;
  for(;;) {
    sleep(1);
    double now = monotonic_ms();
    double cpu = process_cpu_ms();
    double span = now - then;
    if(span < 500 || cpu < 0 || cpu_then < 0) {
      then = now;
      cpu_then = cpu;
      continue;
    }
    double cores = (cpu - cpu_then) / span;
    then = now;
    cpu_then = cpu;

    // The main thread, if its clock is known (else taken as idle: then only
    // a compositor that spins, or threads WebKit did not make, trip it).
    double main_cores = 0;
    if(main_clock_known) {
      double ms = clock_ms(main_clock);
      if(ms >= 0) {
        main_cores = (ms - guard_main_last_ms) / span;
        guard_main_last_ms = ms;
      }
    }
    // The known threads: together, and the busiest.
    const browserps5_known_thread_t* list = 0;
    unsigned count = 0;
    double known_cores = 0;
    double busiest = 0;
    int busiest_slot = -1;
    if(browserps5_known_threads) {
      list = browserps5_known_threads(&count);
    }
    for(unsigned i = 0; i < count; i++) {
      if(!__atomic_load_n(&list[i].alive, __ATOMIC_ACQUIRE)) {
        continue;
      }
      unsigned gen = __atomic_load_n(&list[i].generation, __ATOMIC_ACQUIRE);
      double ms = clock_ms(list[i].clock);
      if(ms < 0) {
        continue;
      }
      if(gen != guard_last_gen[i]) {
        guard_last_gen[i] = gen;
        guard_last_ms[i] = 0;
      }
      double c = (ms - guard_last_ms[i]) / span;
      guard_last_ms[i] = ms;
      if(c > 0) {
        known_cores += c;
        if(c > busiest) {
          busiest = c;
          busiest_slot = (int)i;
        }
      }
    }
    char name[32] = "";
    if(busiest_slot >= 0 && __atomic_load_n(&list[busiest_slot].alive, __ATOMIC_ACQUIRE)) {
      pthread_getname_np(list[busiest_slot].thread, name, sizeof(name));
      name[sizeof(name) - 1] = 0;
    }
    int stuck_a = cores >= SPIN_GUARD_CORES_A && main_cores <= SPIN_GUARD_MAIN
                  && busiest >= SPIN_GUARD_COMPOSITOR_A && strstr(name, "ThreadedComposi");
    int stuck_b = cores >= SPIN_GUARD_CORES_B && main_cores <= SPIN_GUARD_MAIN
                  && known_cores <= SPIN_GUARD_KNOWN_B;
    seconds_a = stuck_a ? seconds_a + 1 : 0;
    seconds_b = stuck_b ? seconds_b + 1 : 0;
    if(seconds_a < SPIN_GUARD_SECONDS_A && seconds_b < SPIN_GUARD_SECONDS_B) {
      continue;
    }

    n = snprintf(msg, sizeof(msg), "[WebKit] spin guard: pid %d stuck (%c) for %d s: %.2f cores, "
                 "the main thread %.2f, the known threads %.2f, the busiest \"%s\" %.2f\n",
                 (int)getpid(), seconds_a >= SPIN_GUARD_SECONDS_A ? 'A' : 'B',
                 seconds_a >= SPIN_GUARD_SECONDS_A ? seconds_a : seconds_b, cores, main_cores,
                 known_cores, name, busiest);
    guard_say(msg, n);
    if(busiest_slot >= 0 && __atomic_load_n(&list[busiest_slot].alive, __ATOMIC_ACQUIRE)) {
      guard_dump(list[busiest_slot].thread);
    }
    if(main_stack_top) {
      guard_dump(main_thread);
    }
    seconds_a = seconds_b = 0;
    if(log_only) {
      continue;
    }
    run_crash_hooks();
    n = snprintf(msg, sizeof(msg), "[WebKit] spin guard: the WebProcess (pid %d) ends here, "
                 "exit %d, so that the console keeps its CPU\n", (int)getpid(), SPIN_GUARD_EXIT);
    guard_say(msg, n);
    end_crash(SPIN_GUARD_EXIT);
  }
  return 0;
}


// BROWSERPS5_SPIN_GUARD: on unless "0"; "log" only writes what it sees.
static void
start_spin_guard(void) {
  const char* mode = getenv("BROWSERPS5_SPIN_GUARD");
  pthread_t thread;

  if(mode && *mode == '0') {
    return;
  }
  stack_dump_handler();
  intptr_t log_only = mode && !strcmp(mode, "log");
  if(!pthread_create(&thread, 0, spin_guard_thread, (void*)log_only)) {
    pthread_detach(thread);
  }
}


// BROWSERPS5_STACK_DUMPS: seconds, in increasing order, separated by commas.
static void
start_stack_dumps(void) {
  const char* list = getenv("BROWSERPS5_STACK_DUMPS");
  const char* callers = getenv("BROWSERPS5_STACK_CALLERS");
  pthread_t thread;

  if(!list) {
    return;
  }
  if(callers && atoi(callers) > 0) {
    max_callers = atoi(callers) > 400 ? 400 : atoi(callers);
  }
  while(*list && stack_dumps < MAX_STACK_DUMPS) {
    char* next;
    unsigned long at = strtoul(list, &next, 10);
    if(next == list) {
      break;
    }
    stack_dump_at[stack_dumps++] = (unsigned)at;
    list = *next == ',' ? next + 1 : next;
  }
  if(!stack_dumps) {
    return;
  }

  // Where the main thread's stack ends: without it, no dumps.
  if(!main_stack_top) {
    return;
  }

  stack_dump_handler();
  if(!pthread_create(&thread, 0, stack_dump_thread, 0)) {
    pthread_detach(thread);
  }
}


// The browser's title decodes the video (docs/08-4k.md, D2/D3) when its
// ring is there and it looks at it: WebKit's Westeros quirks then hand the
// compressed video to ps5videosink ("westerossink") and leave a hole in
// the page where the title puts it. The largest video is then the title's
// (its video.txt, max=). BROWSERPS5_VIDEO=cpu in the file keeps the video
// in the WebProcess, decoded on its CPU and drawn in the page, as before.
static void
title_video_env(void) {
  const char* mode = getenv("BROWSERPS5_VIDEO");
  if(mode && !strcmp(mode, "cpu")) {
    return;
  }
  // Read and write, as the sink and the sound's ring open theirs: read only,
  // the console's first try of the title's video kept the video in the
  // WebProcess with no word why (2026-11-05 11:25). Each "no" is said now.
  int fd = shm_open(VIDEO_RING_SHM, O_RDWR, 0);
  if(fd < 0) {
    fprintf(stderr, "[WebKit] no title video: shm_open %s: %s (pid %d)\n", VIDEO_RING_SHM,
            strerror(errno), (int)getpid());
    return;
  }
  // The header only: the slots are the sink's business.
  const size_t header = offsetof(video_ring_t, slots);
  const video_ring_t* ring = mmap(NULL, header, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(ring == MAP_FAILED) {
    fprintf(stderr, "[WebKit] no title video: mmap %s: %s (pid %d)\n", VIDEO_RING_SHM,
            strerror(errno), (int)getpid());
    return;
  }
  if(!video_ring_ready(ring) || !video_ring_alive(ring)) {
    const uint64_t then = __atomic_load_n(&ring->consumer_ns, __ATOMIC_ACQUIRE);
    const uint64_t now = video_ring_now_ns();
    fprintf(stderr, "[WebKit] no title video: magic 0x%x, version %u, the title looked %lld ms "
            "ago (pid %d)\n", __atomic_load_n(&ring->magic, __ATOMIC_ACQUIRE), ring->version,
            then ? (long long)((int64_t)(now - then) / 1000000) : -1LL, (int)getpid());
  } else {
    char limit[48];
    snprintf(limit, sizeof(limit), "%ux%u@%u", ring->max_width, ring->max_height,
             ring->max_fps);
    setenv("WEBKIT_GST_QUIRKS", "westeros", 0);
    setenv("WEBKIT_GST_HOLE_PUNCH_QUIRK", "westeros", 0);
    setenv("WEBKIT_GST_VIDEO_DECODING_LIMIT", limit, 0);
    fprintf(stderr, "[WebKit] the title decodes the video (up to %s, pid %d)\n", limit,
            (int)getpid());
  }
  munmap((void*)ring, header);
}


static void
debug_env_file(const char* path) {
  FILE* file = fopen(path, "r");
  char line[512];

  while(file && fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = 0;
    char* equals = strchr(line, '=');
    if(line[0] == '#' || !equals || equals == line) {
      continue;
    }
    *equals = 0;
    setenv(line, equals + 1, 1);
    fprintf(stderr, "[WebKit] %s: %s=%s (pid %d)\n", path, line,
            equals + 1, (int)getpid());
  }
  if(file) {
    fclose(file);
  }
}


void
browserps5_debug_env(void) {
  debug_env_file(DEBUG_ENV_FILE);
  debug_env_file(ENGINE_ENV_FILE_RN);
  debug_env_file(DEBUG_ENV_FILE_RN);
  // Defaults, which the file can change. The title's video, if it is there.
  title_video_env();
  // Else WebKit's GStreamer says a video larger than the screen (1080p)
  // cannot be played, in MediaSource.isTypeSupported() and
  // MediaCapabilities, so YouTube does not pick 1440p or 4K: the CPU decodes
  // those too slowly (the TV test of 2026-11-04 went to 4K at 2 frames/s,
  // then 1440p with frames dropped).
  setenv("WEBKIT_GST_VIDEO_DECODING_LIMIT", "1920x1080@60", 0);
}


// The calling thread is WebKit's main thread from here on: its cores and
// scheduling in the log, then the probes the environment asks for (stack
// dumps, the load probe, the spin guard). browserps5_crash_signals() calls
// it; the browser title (2c-2, poc/browser/title/web_host.cpp) calls it
// alone, with the title's own crash handlers left in place.
void
browserps5_debug_probes(void) {
  main_thread = pthread_self();
  main_stack_top = this_stack_top();

  // The cores this process may use and its main thread's scheduling, in the
  // log: with YouTube the sound fell behind under load, and a payload's
  // threads are SCHED_FIFO 700 (V3, 2026-11-04).
  cpuset_t cpus;
  int policy = -1;
  struct sched_param param;
  char msg[160];
  CPU_ZERO(&cpus);
  memset(&param, 0, sizeof(param));
  int affinity = pthread_getaffinity_np(pthread_self(), sizeof(cpus), &cpus);
  pthread_getschedparam(pthread_self(), &policy, &param);
  int n = snprintf(msg, sizeof(msg),
                   "[WebKit] pid %d: %d core(s) (mask 0x%lx%s), main thread "
                   "policy %d priority %d\n", (int)getpid(),
                   affinity ? -1 : CPU_COUNT(&cpus), (unsigned long)cpus.__bits[0],
                   affinity ? ", no affinity" : "", policy, param.sched_priority);
  if(n > 0) {
    write(report_fd, msg, (size_t)n);
  }
  // The main thread's CPU clock, for the load probe and the spin guard.
  main_clock_known = browserps5_thread_clock && !browserps5_thread_clock(&main_clock);
  start_stack_dumps();
  start_load_probe();
  start_spin_guard();
}


void
browserps5_crash_signals(void) {
  // SIGTRAP: an int3, such as GLib's G_BREAKPOINT() after a fatal g_log().
  static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
  struct sigaction action;

  browserps5_debug_env();
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = crash_signal;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  for(size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
    sigaction(signals[i], &action, 0);
  }
  browserps5_debug_probes();
}
