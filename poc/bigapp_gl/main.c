/* Copyright (C) 2026 BrowserPS5 contributors
   Launch sequence adapted from websrv's src/ps5/hbldr.c,
   Copyright (C) 2024 John Törnblom.

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

// Runs payload.elf (gl_triangle by default, see build-windows.ps1) inside an
// app process ("BigApp") instead of a plain payload process. A payload
// process cannot register with the system compositor (sceCompositorInitWithProcessOrder fails with 0x80d40009), so
// GLSlim has no video memory there. Homebrew that draws on screen gets an
// app process the way websrv's hbldr does: launch the fake title FAKE00000
// (whose eboot.bin is a copy of the PS Now app), stop it right before its
// main() and load our ELF in its place.
//
// Unlike hbldr, this refuses to run when a game is already open instead of
// closing it.
//
// Built with -DBIGAPP_CTL_FD=<fd>, it also hands that descriptor to the app
// at the same number: poc/launcher uses it to give WebKit's UIProcess the
// control socket of the launcher payload (docs/02-desenho-sem-gpu.md).

#define POC_NAME "bigapp_gl"

#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/event.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <ps5/kernel.h>

#include "poc_log.h"
#include "elfldr/asset.h"
#include "hbldr/elfldr.h"
#include "hbldr/pt.h"


#define PSNOW_EBOOT "/system_ex/app/NPXS40106/eboot.bin"
#define FAKE_PATH   "/system_ex/app/FAKE00000"

#ifndef GL_TRIANGLE_PIGLET
#define GL_TRIANGLE_PIGLET "webprocess"
#endif

INCASSET(payload_elf, "payload.elf");


// Same layout websrv creates, so its "Homebrew Launcher" package
// (IV9999-FAKE00000_00-HOMEBREWLOADER01) and this PoC share one fake app.
static const char param_json[] = "{\n"
  "  \"applicationCategoryType\": 0,\n"
  "  \"attribute\": 1,\n"
  "  \"attribute2\": 0,\n"
  "  \"attribute3\": 4,\n"
  "  \"titleId\": \"FAKE00000\",\n"
  "  \"contentId\": \"IV9999-FAKE00000_00-HOMEBREWLOADER00\",\n"
  "  \"localizedParameters\": {\n"
  "    \"defaultLanguage\": \"en-US\",\n"
  "    \"en-US\": {\n"
  "      \"titleName\": \"Homebrew Loader\"\n"
  "    }\n"
  "  }\n"
  "}\n";


typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;


int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t *user_id);

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char* title_id, char** argv,
			      app_launch_ctx_t* ctx);


static int
write_file(const char* path, const void* data, size_t size, mode_t mode) {
  int fd;

  if((fd=open(path, O_CREAT | O_WRONLY | O_TRUNC, mode)) < 0) {
    POC_PERROR(path);
    return -1;
  }
  if(write(fd, data, size) != (ssize_t)size) {
    POC_PERROR(path);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}


static int
copy_file(const char* from, const char* to, mode_t mode) {
  struct stat info;
  uint8_t* buf;
  int fd;
  int ret;

  if((fd=open(from, O_RDONLY)) < 0 || fstat(fd, &info)) {
    POC_PERROR(from);
    if(fd >= 0) {
      close(fd);
    }
    return -1;
  }
  if(!(buf=malloc(info.st_size))) {
    POC_PERROR("malloc");
    close(fd);
    return -1;
  }
  if(read(fd, buf, info.st_size) != info.st_size) {
    POC_PERROR(from);
    free(buf);
    close(fd);
    return -1;
  }
  close(fd);

  ret = write_file(to, buf, info.st_size, mode);
  free(buf);
  return ret;
}


static int
fakeapp_create_if_missing(void) {
  struct stat info;

  if(!stat(FAKE_PATH "/eboot.bin", &info) &&
     !stat(FAKE_PATH "/sce_sys/param.json", &info)) {
    POC_LOG(FAKE_PATH " already exists");
    return 0;
  }

  POC_LOG("creating " FAKE_PATH " (eboot.bin copied from " PSNOW_EBOOT ")");
  if((mkdir(FAKE_PATH, 0755) && stat(FAKE_PATH, &info)) ||
     (mkdir(FAKE_PATH "/sce_sys", 0755) && stat(FAKE_PATH "/sce_sys", &info))) {
    POC_PERROR("mkdir");
    return -1;
  }
  if(write_file(FAKE_PATH "/sce_sys/param.json", param_json,
		sizeof(param_json)-1, 0644)) {
    return -1;
  }
  return copy_file(PSNOW_EBOOT, FAKE_PATH "/eboot.bin", 0755);
}


static pid_t
find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    POC_PERROR("sysctl");
    return -1;
  }
  if(!(buf=malloc(buf_size))) {
    POC_PERROR("malloc");
    return -1;
  }
  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    POC_PERROR("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && mypid != ki_pid) {
      pid = ki_pid;
    }
  }

  free(buf);
  return pid;
}


static void*
bigapp_launch_thread(void* arg) {
  app_launch_ctx_t ctx = {0};
  int ret;

  if(sceUserServiceGetForegroundUser(&ctx.user_id)) {
    POC_PERROR("sceUserServiceGetForegroundUser");
    return 0;
  }

  ret = sceSystemServiceLaunchApp("FAKE00000", (char**)arg, &ctx);
  POC_LOG("sceSystemServiceLaunchApp(FAKE00000) = 0x%x", ret);
  return 0;
}


/**
 * Launch FAKE00000 and catch its process as SceSysCore forks it, stopped
 * right after it execs the eboot.
 **/
static pid_t
bigapp_launch(char** argv) {
  struct kevent evt = {0};
  struct timespec timeout = {30, 0};
  pid_t parent;
  pid_t child;
  pthread_t trd;
  int kq;

  if((parent=find_pid("SceSysCore.elf")) < 0) {
    POC_LOG("SceSysCore.elf not found");
    return -1;
  }
  if((kq=kqueue()) < 0) {
    POC_PERROR("kqueue");
    return -1;
  }

  EV_SET(&evt, parent, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR,
	 NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, 0);
  if(kevent(kq, &evt, 1, 0, 0, 0) < 0) {
    POC_PERROR("kevent");
    close(kq);
    return -1;
  }

  pthread_create(&trd, 0, &bigapp_launch_thread, (void*)argv);
  pthread_detach(trd);

  if(kevent(kq, 0, 0, &evt, 1, &timeout) <= 0) {
    POC_LOG("SceSysCore did not fork an app process within 30 s");
    close(kq);
    return -1;
  }
  close(kq);

  if(!(evt.fflags & NOTE_CHILD)) {
    POC_LOG("unexpected kevent fflags 0x%x", evt.fflags);
    return -1;
  }
  child = evt.ident;

  if(pt_attach(child)) {
    POC_PERROR("pt_attach");
    return -1;
  }
  if(pt_follow_exec(child) < 0) {
    POC_PERROR("pt_follow_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }
  if(pt_continue(child, SIGCONT) < 0) {
    POC_PERROR("pt_continue");
    pt_detach(child, SIGKILL);
    return -1;
  }
  if(pt_await_exec(child)) {
    POC_PERROR("pt_await_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }

  return child;
}


static int
bigapp_set_argv0(pid_t pid, const char* argv0) {
  intptr_t pos = pt_getargv(pid);
  intptr_t buf;

  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_WRITE | PROT_READ,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)) == -1) {
    pt_perror(pid, "pt_mmap");
    return -1;
  }
  if(pt_copyin(pid, argv0, buf, strlen(argv0)+1) || pt_setlong(pid, pos, buf)) {
    POC_PERROR("pt_copyin");
    pt_munmap(pid, buf, PAGE_SIZE);
    return -1;
  }
  return 0;
}


#ifdef BIGAPP_CTL_FD
/**
 * Duplicate our descriptor fd into the app process, at the same number.
 **/
static int
bigapp_pass_fd(pid_t pid, int fd) {
  int remote;

  if((remote=pt_rdup(pid, getpid(), fd)) < 0) {
    pt_perror(pid, "pt_rdup");
    return -1;
  }
  if(remote != fd && (pt_dup2(pid, remote, fd) < 0 || pt_close(pid, remote) < 0)) {
    pt_perror(pid, "pt_dup2");
    return -1;
  }
  return 0;
}
#endif


/**
 * Let the app run up to the call of its main(), then load our ELF instead.
 **/
static int
bigapp_replace(pid_t pid, uint8_t* elf, const char* progname, int stdio,
	       char** envp) {
  uint8_t int3instr = 0xcc;
  intptr_t brkpoint;
  uint8_t orginstr;

  // Let the kernel assign process parameters accessed via sceKernelGetProcParam()
  if(pt_syscall(pid, 599)) {
    POC_LOG("sys_dynlib_process_needed_and_relocate failed");
  }

  // Allow libc to allocate arbitrary amount of memory.
  elfldr_set_heap_size(pid, -1);

  if(!(brkpoint=kernel_dynlib_entry_addr(pid, 0))) {
    POC_LOG("kernel_dynlib_entry_addr failed");
    return -1;
  }
  brkpoint += 58; // offset to invocation of main()

  if(kernel_mprotect(pid, brkpoint, PAGE_SIZE,
		     PROT_READ | PROT_WRITE | PROT_EXEC)) {
    POC_LOG("kernel_mprotect failed");
    return -1;
  }
  if(pt_copyout(pid, brkpoint, &orginstr, sizeof(orginstr)) ||
     pt_copyin(pid, &int3instr, brkpoint, sizeof(int3instr))) {
    POC_PERROR("pt_copy");
    return -1;
  }

  // Continue execution until we hit the breakpoint, then remove it.
  if(pt_continue(pid, SIGCONT)) {
    POC_PERROR("pt_continue");
    return -1;
  }
  if(waitpid(pid, 0, 0) == -1) {
    POC_PERROR("waitpid");
    return -1;
  }
  if(pt_copyin(pid, &orginstr, brkpoint, sizeof(orginstr))) {
    POC_PERROR("pt_copyin");
    return -1;
  }

  bigapp_set_argv0(pid, progname);
  elfldr_raise_privileges(pid);
  elfldr_set_procname(pid, basename((char*)progname));
  elfldr_set_environ(pid, envp);
  elfldr_set_cwd(pid, "/");
  elfldr_set_stdio(pid, stdio);
#ifdef BIGAPP_CTL_FD
  if(bigapp_pass_fd(pid, BIGAPP_CTL_FD)) {
    return -1;
  }
#endif

  // sys_budget_set(0), as hbldr does on firmwares above 10.00.
  if(kernel_get_fw_version() > 0x10000000) {
    pt_syscall(pid, 0x23b, 0);
  }

  POC_LOG("app process %d stopped before main(), loading payload.elf", pid);
  return elfldr_exec(pid, elf);
}


/**
 * Keep this payload's connection open until the app process exits, so the
 * output of both ends up in the same log.
 **/
static void
await_exit(pid_t pid, int seconds) {
  struct kevent evt = {0};
  struct timespec timeout = {seconds, 0};
  int kq;

  if((kq=kqueue()) < 0) {
    POC_PERROR("kqueue");
    return;
  }
  EV_SET(&evt, pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0, 0);
  if(kevent(kq, &evt, 1, &evt, 1, &timeout) > 0) {
    POC_NOTIFY("app process %d exited, status 0x%lx", pid, (long)evt.data);
  } else {
    POC_LOG("app process %d still running after %d s", pid, seconds);
  }
  close(kq);
}


#ifdef BIGAPP_TRACE_CRASH
#define TRACE_STACK_WORDS 1024
#define TRACE_MAX_THREADS 64

// Where libWebKit's code is: hello_page logs its load address (0x200040000
// on 2026-11-03), and its text is about 70 MiB. Stack words in this range
// are return addresses to look up with nm.
#define TRACE_LIB_LO 0x200000000UL
#define TRACE_LIB_HI 0x208000000UL


/**
 * Print where one thread is: its registers and the stack words that point
 * into libWebKit. Code built without frame pointers has no frame chain to
 * follow, so this is the backtrace.
 **/
static void
trace_dump(pid_t id, const char* what) {
  static uint64_t stack[TRACE_STACK_WORDS];
  struct reg r;
  int words = 0;
  int shown = 0;

  if(pt_getregs(id, &r)) {
    POC_LOG("%s: pt_getregs failed", what);
    return;
  }
  POC_LOG("%s: rip 0x%lx rsp 0x%lx rbp 0x%lx rax 0x%lx rdi 0x%lx rsi 0x%lx",
	  what, (long)r.r_rip, (long)r.r_rsp, (long)r.r_rbp, (long)r.r_rax,
	  (long)r.r_rdi, (long)r.r_rsi);

  // In chunks: the stack may end before TRACE_STACK_WORDS.
  while(words < TRACE_STACK_WORDS &&
	!pt_copyout(id, r.r_rsp + words * 8, stack + words, 64)) {
    words += 8;
  }
  for(int i=0; i<words && shown<24; i++) {
    if(stack[i] >= TRACE_LIB_LO && stack[i] < TRACE_LIB_HI) {
      POC_LOG("  stack+0x%04x 0x%lx", i * 8, (long)stack[i]);
      shown++;
    }
  }
  POC_LOG("  (%d stack words read)", words);
}


/**
 * Print every thread of the stopped app.
 **/
static void
trace_dump_threads(pid_t pid) {
  lwpid_t lwps[TRACE_MAX_THREADS];
  char what[32];
  int n = pt_getlwplist(pid, lwps, TRACE_MAX_THREADS);

  POC_LOG("%d thread(s)", n);
  for(int i=0; i<n; i++) {
    snprintf(what, sizeof(what), "thread %d", (int)lwps[i]);
    trace_dump(lwps[i], what);
  }
}


/**
 * Instead of await_exit(): attach to the running app and wait for it to
 * exit or stop on a fatal signal. WebKit's CRASH() is an int3; untraced,
 * the system's debugger catches it and leaves the app suspended and open.
 * Traced, we get the stop first, print where it happened and kill the app.
 * Other signals are passed on.
 **/
static void
trace_crash(pid_t pid, int seconds) {
  time_t end = time(0) + seconds;
  int traps = 0;
  int status;

  if(pt_attach(pid) || pt_continue(pid, 0)) {
    POC_PERROR("pt_attach");
    await_exit(pid, seconds);
    return;
  }

  while(time(0) < end) {
    pid_t ret = waitpid(pid, &status, WNOHANG);
    int sig;

    if(ret == 0) {
      usleep(100 * 1000);
      continue;
    }
    if(ret < 0) {
      POC_PERROR("waitpid");
      return;
    }
    if(!WIFSTOPPED(status)) {
      POC_NOTIFY("app process %d exited, status 0x%x", pid, status);
      return;
    }

    sig = WSTOPSIG(status);

    // A traced process also stops with SIGTRAP when it creates a thread:
    // the new thread at its start in libkernel (rbp 0), whose code can not
    // be read. Only a readable int3 just before rip, WebKit's CRASH(), is a
    // crash; let the rest go on without the signal.
    if(sig == SIGTRAP) {
      struct reg r;
      uint8_t prev = 0;

      if(!pt_getregs(pid, &r) &&
	 (pt_copyout(pid, r.r_rip - 1, &prev, 1) || prev != 0xcc)) {
	if(traps++ < 4) {
	  POC_LOG("SIGTRAP at 0x%lx is not an int3 (rax %ld), continuing",
		  (long)r.r_rip, (long)r.r_rax);
	}
	pt_continue(pid, 0);
	continue;
      }
    }

    if(sig == SIGTRAP || sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
       sig == SIGFPE || sig == SIGABRT || sig == SIGSYS) {
      POC_LOG("app stopped on signal %d", sig);
      trace_dump(pid, "faulting thread");
      trace_dump_threads(pid);
      kill(pid, SIGKILL);
      pt_detach(pid, 0);
      POC_NOTIFY("app process %d stopped on signal %d and was killed", pid,
		 sig);
      return;
    }
    pt_continue(pid, sig);
  }

  // Still running: stop it and print where every thread is waiting.
  // The stop may never come (seen 2026-11-03), so wait for it 3 s at most.
  POC_LOG("app process %d still running after %d s", pid, seconds);
  if(!kill(pid, SIGSTOP)) {
    for(int i=0; i<30; i++) {
      if(waitpid(pid, &status, WNOHANG) == pid && WIFSTOPPED(status)) {
	trace_dump_threads(pid);
	break;
      }
      usleep(100 * 1000);
    }
  }
  kill(pid, SIGKILL);
  pt_detach(pid, 0);
  POC_NOTIFY("app process %d killed after %d s", pid, seconds);
}
#endif


int
main(void) {
  char* argv[] = {0};
  char* envp[] = {"GL_TRIANGLE_PIGLET=" GL_TRIANGLE_PIGLET, 0};
  uint8_t* elf;
  int app_id;
  pid_t pid;

  // Keep going if whoever sent the payload has closed the connection.
  signal(SIGPIPE, SIG_IGN);

  if(sceUserServiceInitialize(0)) {
    POC_PERROR("sceUserServiceInitialize");
  }

  if((app_id=sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    POC_NOTIFY("FAIL an app is already open (id 0x%x); close it and retry",
	       app_id);
    return 1;
  }

  if(fakeapp_create_if_missing()) {
    POC_NOTIFY("FAIL could not create " FAKE_PATH);
    return 1;
  }

  // elfldr_exec patches the image it is given, so hand it a copy.
  if(!(elf=malloc(payload_elf_size))) {
    POC_PERROR("malloc");
    return 1;
  }
  memcpy(elf, payload_elf, payload_elf_size);

  if((pid=bigapp_launch(argv)) < 0) {
    POC_NOTIFY("FAIL could not catch the FAKE00000 app process");
    return 1;
  }
  POC_LOG("caught app process %d (authid 0x%016lx)", pid,
	  kernel_get_ucred_authid(pid));

  if(bigapp_replace(pid, elf, "payload.elf", STDOUT_FILENO, envp)) {
    pt_detach(pid, SIGKILL);
    POC_NOTIFY("FAIL could not load payload.elf into the app process");
    return 1;
  }
  free(elf);
#ifdef BIGAPP_CTL_FD
  // Only the app keeps the control socket now, so the launcher sees it
  // close when the app exits.
  close(BIGAPP_CTL_FD);
#endif

  POC_NOTIFY("PASS payload.elf running as an app, pid %d "
	     "(GL_TRIANGLE_PIGLET=" GL_TRIANGLE_PIGLET ")", pid);
#ifdef BIGAPP_TRACE_CRASH
  trace_crash(pid, 45);
#else
  await_exit(pid, 90);
#endif
  return 0;
}
