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

// Log where a PoC crashes, then exit, so an app process does not stay open
// after a crash. The klog only says that a signal was sent; this prints the
// signal, the faulting address, the registers and the frame-pointer chain.
// Subtract a library's load address (log one of its functions at start) to
// look the addresses up with nm/addr2line on the PC.
//
//   crash_log_install();

#pragma once

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "poc_log.h"

#define CRASH_LOG_FRAMES 16

// crash_log_watchdog(): stack words of the main thread in [lo, hi) are
// printed as return addresses (e.g. libWebKit's code).
#define CRASH_LOG_SCAN_WORDS 4096

static volatile sig_atomic_t crash_log_busy = 0;
static pthread_t crash_log_main;
static uintptr_t crash_log_stack_top;
static uintptr_t crash_log_code_lo;
static uintptr_t crash_log_code_hi;
static unsigned  crash_log_watch_seconds;


static void
crash_log_handler(int sig, siginfo_t* info, void* context) {
  ucontext_t* uc = (ucontext_t*)context;
  uintptr_t rbp = (uintptr_t)uc->uc_mcontext.mc_rbp;

  // A fault while walking the frames lands here again: just leave.
  if(crash_log_busy) {
    _exit(128 + sig);
  }
  crash_log_busy = 1;

  POC_LOG("CRASH signal %d code %d addr %p", sig, info->si_code,
          info->si_addr);
  POC_LOG("  rip 0x%lx rsp 0x%lx rbp 0x%lx",
          (unsigned long)uc->uc_mcontext.mc_rip,
          (unsigned long)uc->uc_mcontext.mc_rsp, (unsigned long)rbp);

  // Frames as [rbp] = caller's rbp, [rbp+8] = return address. Code built
  // without frame pointers ends the chain early or gives junk.
  for(int i=0; i<CRASH_LOG_FRAMES && rbp && !(rbp & 7); i++) {
    uintptr_t* frame = (uintptr_t*)rbp;
    POC_LOG("  frame %d ret 0x%lx", i, (unsigned long)frame[1]);
    if(frame[0] <= rbp) {
      break;
    }
    rbp = frame[0];
  }

  _exit(128 + sig);
}


/**
 * SIGUSR2 handler, run on the main thread: where it is now.
 **/
static void
crash_log_where(int sig, siginfo_t* info, void* context) {
  ucontext_t* uc = (ucontext_t*)context;
  uintptr_t rsp = (uintptr_t)uc->uc_mcontext.mc_rsp;
  int shown = 0;

  (void)sig;
  (void)info;
  POC_LOG("WATCHDOG main thread still busy after %u s: rip 0x%lx rsp 0x%lx",
          crash_log_watch_seconds, (unsigned long)uc->uc_mcontext.mc_rip,
          (unsigned long)rsp);

  // From rsp up to the top of main()'s frame, which is our own memory.
  for(uintptr_t* p=(uintptr_t*)rsp; (uintptr_t)p < crash_log_stack_top &&
        p < (uintptr_t*)rsp + CRASH_LOG_SCAN_WORDS && shown < 40; p++) {
    if(*p >= crash_log_code_lo && *p < crash_log_code_hi) {
      POC_LOG("  stack+0x%04lx 0x%lx", (unsigned long)((uintptr_t)p - rsp),
              (unsigned long)*p);
      shown++;
    }
  }
}


static void*
crash_log_watch(void* arg) {
  (void)arg;
  sleep(crash_log_watch_seconds);
  pthread_kill(crash_log_main, SIGUSR2);
  return 0;
}


/**
 * If the main thread is still running after `seconds` (e.g. stuck in a
 * library), log where it is: rip and the stack words in [code_lo, code_hi).
 * Call from main(); stack_top is the address of one of main()'s locals.
 **/
static inline void
crash_log_watchdog(unsigned seconds, void* stack_top, uintptr_t code_lo,
                   uintptr_t code_hi) {
  struct sigaction sa;
  pthread_t thread;

  crash_log_main = pthread_self();
  crash_log_stack_top = (uintptr_t)stack_top;
  crash_log_code_lo = code_lo;
  crash_log_code_hi = code_hi;
  crash_log_watch_seconds = seconds;

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_log_where;
  // SA_RESTART: if main is fine, just slow, its system calls go on.
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, 0);

  if(!pthread_create(&thread, 0, crash_log_watch, 0)) {
    pthread_detach(thread);
  }
}


static void
crash_log_install(void) {
  static const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP,
                             SIGABRT, SIGSYS};
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_log_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  for(size_t i=0; i<sizeof(sigs)/sizeof(sigs[0]); i++) {
    sigaction(sigs[i], &sa, 0);
  }
}
