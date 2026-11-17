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
 * The WebProcess inside the browser title (2c-2,
 * docs/05-webprocess-no-titulo.md). The title has 13 CPUs where a payload
 * gets about 2, and its GPU is its own (Mesa's zink over the title's RADV,
 * title_vulkan.c), with no broker in between.
 *
 * The UIProcess's process launcher (poc/webkit/process-launcher) asks here
 * first, on 127.0.0.1:WEB_HOST_PORT (web_host_proto.h): the first request
 * gets the WebProcess in this process, as a thread with WebKit's
 * WebProcessMain; any other gets "NO", and the launcher payload starts that
 * one as before. Its IPC end comes as a line (ipc_transport.c in title
 * mode: a title has no dup()), already met when WebKit gets it.
 *
 * The title cannot see /user: HOME and fontconfig's file are under /app0.
 * WebKit's own crash hook (browserps5_crash_signals) is not installed here:
 * the title's (title.cpp, crash_hooks) stays, which writes the crash into
 * the title's report. Only its setup runs (browserps5_debug_env: the
 * title's video).
 *
 * When WebKit closes the connection (the UIProcess quits or ends it), the
 * connection's work queue calls _exit(0) and WebProcessMain returns: both
 * threads park (__wrap__exit, web_process()), and the title ends as
 * before, when the UIProcess quits (2026-11-06 13:01, below).
 */
#include "web_host.h"

#include "../../webkit/ipc-transport/ipc_transport.h"
#include "../../webkit/process-launcher/web_host_proto.h"
#include "../../common/title_link.h"  // a title that may not listen calls the menu instead

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <ps5platform/klog.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/thr.h>
#include <sys/types.h>
#include <sys/user.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" int klog_printf(const char *format, ...);  // title.cpp: the report
extern "C" void browserps5_debug_env(void);           // poc/webkit/crash-report
extern "C" void browserps5_debug_probes(void);        // poc/webkit/crash-report
extern "C" void browserps5_report_fd(int fd);         // poc/webkit/crash-report
extern "C" void browserps5_dump_thread(pthread_t thread);  // poc/webkit/crash-report
extern "C" void browserps5_crash_end(void (*end)(int code));  // poc/webkit/crash-report
extern "C" void browserps5_stack_scan(const void *sp, uintptr_t top, uintptr_t pc);
extern "C" void browserps5_code_end(uintptr_t end);  // poc/webkit/crash-report
extern "C" void browserps5_frame_walk(const void *fp, const void *sp, uintptr_t top);

namespace WebKit {
int WebProcessMain(int argc, char **argv);
}
namespace PlayStation {
void setConnectionIdentifier(int fd);
}

#define HOME_DIR "/app0/web"
#define FONTS_CONF "/app0/fonts/fonts.conf"
#define STDERR_PATH "/app0/web-stderr.txt"
#define WEB_STACK_BYTES (16u << 20)

static char identifier[32];
static char connection[16];
static int started;
static pthread_t main_thread;
static int main_known;

/* WebKit ends a WebProcess with exit() once its connection closes. In the
 * title that would run the libraries' destructors under the screen, the
 * broker and the sound (title.cpp, "The end"). A thread other than main()'s
 * that calls exit() stays parked instead; the title ends its own way when
 * the UIProcess quits (LINK_QUIT, or the link closing). build.sh: --wrap. */
extern "C" void __real_exit(int code) __attribute__((noreturn));
static void park(void) __attribute__((noreturn));
extern "C" void __wrap_exit(int code)
{
    if (!main_known || pthread_equal(pthread_self(), main_thread))
        __real_exit(code);
    klog_printf("[web_host] exit(%d) from a WebKit thread: the WebProcess has ended; "
                "the title goes on until the UIProcess quits\n", code);
    /* An error: as a crash (the title closes its own way). */
    if (code)
        title_web_failed("the WebProcess ended with an error");
    park();
}

/* A thread of the WebProcess that crashed, or is stopped for the title's
 * close: it waits here with every signal blocked, asleep (no spinning). */
static void park(void)
{
    sigset_t all;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, nullptr);
    for (;;)
        sleep(3600);
}

/* Which threads are the WebProcess's: its own (web_process()) and every
 * one a thread of it makes (Mesa's included), with the top of each one's
 * stack, noted as it starts (a crash handler may not ask). */
static thread_local bool web_thread;
static thread_local uintptr_t web_stack_top;
static thread_local struct made_thread *my_made;
/* web_thread, or a thread whose thread-locals are no longer there (one that
 * is ending runs its destructors after them: 2026-11-06 13:01, the
 * WebProcessMain thread's end crashed and was not counted as the
 * WebProcess's) known by its tid: web_process()'s own, or one in made[]. */
static long web_main_tid;
static bool is_web_thread(void);

/* And _exit(): when its connection closes, the PlayStation port's WebProcess
 * ends at once from the connection's work queue (WebProcess.cpp callExit(),
 * WTF::terminateProcess(); GTK and WPE wait for their main thread). A title
 * may not make that system call: SIGSYS (2026-11-06 13:01, the 2c2n run,
 * as the UIProcess quit at the end of the GO test; title.cpp's handler
 * parked it). A thread of the WebProcess that calls _exit() parks here
 * first; every other thread (main()'s, the title's own) goes on to _exit as
 * before. build.sh: --wrap=_exit. */
extern "C" void __real__exit(int code) __attribute__((noreturn));
extern "C" void __wrap__exit(int code)
{
    if (!is_web_thread())
        __real__exit(code);
    klog_printf("[web_host] _exit(%d) from a thread of the WebProcess: the WebProcess has "
                "ended; the title goes on until the UIProcess quits\n", code);
    if (code)
        title_web_failed("the WebProcess ended with an error");
    park();
}

static uintptr_t stack_top_now(void)
{
    pthread_attr_t attr;
    void *stack = nullptr;
    size_t size = 0;
    if (pthread_attr_init(&attr))
        return 0;
    if (!pthread_attr_get_np(pthread_self(), &attr))
        pthread_attr_getstack(&attr, &stack, &size);
    pthread_attr_destroy(&attr);
    return stack && size ? (uintptr_t)stack + size : 0;
}

/* WebKit's WebProcessMain() calls browserps5_crash_signals() first; in the
 * title it comes here instead (build.sh: --wrap): the title's crash
 * handlers stay, and the probes run (serve() sets them: stack dumps of
 * this thread, the load probe, the spin guard only writing). */
extern "C" void __wrap_browserps5_crash_signals(void)
{
    browserps5_debug_env();
    browserps5_debug_probes();
    klog_printf("[web_host] WebKit's crash signals are the title's; probes on\n");
}

static int conn_fd = -1;  // the WebProcess's end towards the UIProcess
static int web_log = -1;  // web-stderr.txt's descriptor
static int web_log_read = -1;  // and one to read it back (log_forward())

/* TITLE_QUIET_LOG (2026-11-11 16:05, build.sh; BrowserPS5 RN's titles): the
 * lines wait in memory and a thread of their own writes them twice a second.
 * A write() to a file is 0.75 ms on the console when the disk is idle and
 * 100 to 250 ms when it is busy (the network cache and the sites' databases
 * being written): every thread of the WebProcess wrote three or four lines
 * here as it was made and before it ran its work, WebKit makes a thread for
 * each work queue (an image decoder's) and its maker waits for it, and so
 * the page's main thread stood still for 20 to 60 s with two threads made a
 * second (the stacks asked for at 15:54: even the handler's own lines came
 * out one every 100 ms). */
#ifdef TITLE_QUIET_LOG
static pthread_mutex_t say_lock = PTHREAD_MUTEX_INITIALIZER;
static char say_pending[1 << 17];
static size_t say_length;

/* What waits goes to the file: the lines' own thread, and before a crash's. */
static void web_say_flush(void)
{
    static char out[sizeof(say_pending)];
    static int busy;
    int idle = 0;
    if (web_log < 0 || !__atomic_compare_exchange_n(&busy, &idle, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;
    size_t n = 0;
    if (!pthread_mutex_trylock(&say_lock)) {
        n = say_length;
        memcpy(out, say_pending, n);
        say_length = 0;
        pthread_mutex_unlock(&say_lock);
    }
    if (n)
        (void)!write(web_log, out, n);
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
}
#else
static void web_say_flush(void)
{
}
#endif

static void web_say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void web_say(const char *format, ...)
{
    if (web_log < 0)
        return;
    char line[256];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = sizeof(line) - 1;
#ifdef TITLE_QUIET_LOG
    pthread_mutex_lock(&say_lock);
    const bool kept = say_length + (size_t)n <= sizeof(say_pending);
    if (kept) {
        memcpy(say_pending + say_length, line, (size_t)n);
        say_length += (size_t)n;
    }
    pthread_mutex_unlock(&say_lock);
    if (kept)
        return;  // else: no room (128 KiB in half a second): as before
#endif
    (void)!write(web_log, line, (size_t)n);
}

/* WebKit's Connection::open() makes its end non-blocking first
 * (setNonBlock(): fcntl F_GETFL, then F_SETFL with O_NONBLOCK) and stops
 * there, quietly, when that fails; then its "SocketMonitor" thread, the one
 * that reads the end, is never made (2026-11-05 20:47: no thread made after
 * the "IPC.ReceiveQueue"). A title may not use a socket's ioctl (FIONREAD:
 * EACCES), which F_SETFL goes through. For the transport's ends, the mode
 * the system refuses is kept by the transport (ipc_transport_keeps()), and
 * F_GETFL answers with it. build.sh: --wrap=fcntl. */
extern "C" int __real_fcntl(int fd, int cmd, ...);
extern "C" int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    const long arg = va_arg(ap, long);
    va_end(ap);
    if ((cmd != F_GETFL && cmd != F_SETFL) || !ipc_transport_keeps(fd))
        return __real_fcntl(fd, cmd, arg);
    static unsigned said;
    const bool say = __atomic_add_fetch(&said, 1, __ATOMIC_RELAXED) <= 12;
    if (cmd == F_GETFL) {
        int flags = __real_fcntl(fd, F_GETFL);
        if (flags == -1) {
            if (say)
                web_say("[web_host] fcntl F_GETFL on descriptor %d: %s (read-write given)\n", fd,
                        strerror(errno));
            flags = O_RDWR;
        }
        return ipc_transport_kept_nonblock(fd) ? flags | O_NONBLOCK : flags;
    }
    if (!__real_fcntl(fd, F_SETFL, arg)) {
        ipc_transport_keep_nonblock(fd, 0);  // the system has it
        if (say)
            web_say("[web_host] fcntl F_SETFL 0x%lx on descriptor %d: ok\n", arg, fd);
        return 0;
    }
    const int saved = errno;
    if (arg & ~(long)(O_NONBLOCK | O_ACCMODE)) {
        if (say)
            web_say("[web_host] fcntl F_SETFL 0x%lx on descriptor %d: %s\n", arg, fd,
                    strerror(saved));
        errno = saved;
        return -1;
    }
    ipc_transport_keep_nonblock(fd, (arg & O_NONBLOCK) != 0);
    if (say)
        web_say("[web_host] fcntl F_SETFL 0x%lx on descriptor %d: %s; the transport keeps "
                "O_NONBLOCK %s\n", arg, fd, strerror(saved), (arg & O_NONBLOCK) ? "on" : "off");
    return 0;
}

/* The transport's counters in the title's report, every 5 s for the first
 * minute and then every 30 s: whether the WebProcess's frames come and go
 * (2026-11-05 19:30: none came back from it), with the bytes waiting on its
 * end and what the transport holds for it (20:00: its main thread waited in
 * the run loop with nothing received). */
static void dump_named(void);
static void dump_made(void);
static void list_threads(const char *when);
static void test_thread(void);

/* The title's "flexible" memory left, MiB (-1: no answer): POSIX shared
 * memory takes it, and the 2c2l run (2026-11-06 11:47) had "ftruncate
 * /bps5layer-11: Cannot allocate memory" for the second page's layer
 * (24.9 MiB each, AcceleratedSurfacePlayStation.cpp.patch). */
extern "C" int sceKernelAvailableFlexibleMemorySize(size_t *size);
static long flexible_mib(void)
{
    size_t available = 0;
    return sceKernelAvailableFlexibleMemorySize(&available) ? -1 : (long)(available >> 20);
}

/* 2026-11-11 15:40: how long a thread of the WebProcess takes to be made and
 * to start running. Its main thread stood still for 20 to 60 s inside
 * WorkQueue's constructor (the generic WorkQueue makes a thread per queue
 * and waits for it to run; an image decoder's queue each time): the
 * milliseconds of each step, every one over 20 ms in the log, and the sums
 * with the title's counters (watch()). */
static double thread_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static unsigned long long thread_made_n, thread_create_us, thread_start_us, thread_create_max_us, thread_start_max_us;
static void thread_time(unsigned long long *sum, unsigned long long *max, double ms)
{
    const unsigned long long us = (unsigned long long)(ms * 1000);
    __atomic_add_fetch(sum, us, __ATOMIC_RELAXED);
    unsigned long long was = __atomic_load_n(max, __ATOMIC_RELAXED);
    while (us > was && !__atomic_compare_exchange_n(max, &was, us, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void *watch(void *)
{
    for (unsigned s = 0;; s += s < 60 ? 5 : 30) {
        sleep(s < 60 ? 5 : 30);
        ipc_transport_stats_t t;
        ipc_transport_stats(&t);
        int waiting = -1;
        if (ioctl(conn_fd, FIONREAD, &waiting))
            waiting = -errno;
        char held[160];
        ipc_transport_describe(conn_fd, held, sizeof(held));
        klog_printf("[web_host] at %u s: flexible memory %ld MiB left; met %llu, sent %llu, "
                    "received %llu, attachments %llu "
                    "(%llu lost, %llu refused), named %llu; %d bytes waiting; %s\n",
                    s + (s < 60 ? 5 : 30), flexible_mib(), (unsigned long long)t.met,
                    (unsigned long long)t.frames_sent, (unsigned long long)t.frames_received,
                    (unsigned long long)t.attachments_sent, (unsigned long long)t.attachments_lost,
                    (unsigned long long)t.refused, (unsigned long long)t.named, waiting, held);
        {
            const unsigned long long n = __atomic_load_n(&thread_made_n, __ATOMIC_RELAXED);
            klog_printf("[web_host] threads: %llu made; pthread_create %.1f ms each (%.0f ms the longest, "
                        "%.1f s in all); running %.1f ms after being asked for (%.0f ms the longest, "
                        "%.1f s in all)\n", n,
                        n ? __atomic_load_n(&thread_create_us, __ATOMIC_RELAXED) / 1e3 / n : 0,
                        __atomic_load_n(&thread_create_max_us, __ATOMIC_RELAXED) / 1e3,
                        __atomic_load_n(&thread_create_us, __ATOMIC_RELAXED) / 1e6,
                        n ? __atomic_load_n(&thread_start_us, __ATOMIC_RELAXED) / 1e3 / n : 0,
                        __atomic_load_n(&thread_start_max_us, __ATOMIC_RELAXED) / 1e3,
                        __atomic_load_n(&thread_start_us, __ATOMIC_RELAXED) / 1e6);
        }
        if (s + 5 == 10) {
            list_threads("10 s");
            dump_made();
            dump_named();
        }
        if (s + 5 == 15)
            test_thread();
        if (s + 5 == 30)
            list_threads("30 s");
    }
    return nullptr;
}

/* Every thread made through the chain (build.sh: libc-fixes' wrapper calls
 * web_host_pthread_create(), and it calls the platform's), in the
 * WebProcess's log as it is made and as it first runs: 20:21, WebKit's
 * "SocketMonitor" thread never reached libc-fixes' known_start(). */
extern "C" int ps5platform_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                          void *(*start)(void *), void *arg);
extern "C" const char __ehdr_start[] __attribute__((visibility("hidden")));
// The end of the title's code: .eh_frame_hdr comes right after .text in the
// title's link (PS5_Vulkan's ps5-pie-unwind.ld provides it; no etext there).
// WebKit, Mesa and RADV are all below it, and the stack scans count all of
// them as ours.
extern "C" const char __eh_frame_hdr_start[] __attribute__((visibility("hidden")));

#define MAX_MADE 512
static struct made_thread {
    pthread_t thread;
    void *(*start)(void *);
    void *arg;
    long creator;
    long tid;
    double asked_ms;
    int made;
    int ran;
    int web;    // the WebProcess's (made by one of its threads, or its own)
    int ended;  // its start routine returned
} made[MAX_MADE];
static unsigned made_count;

static bool is_web_thread(void)
{
    if (web_thread)
        return true;
    long tid = 0;
    thr_self(&tid);
    if (!tid)
        return false;
    if (tid == __atomic_load_n(&web_main_tid, __ATOMIC_ACQUIRE))
        return true;
    const unsigned count = __atomic_load_n(&made_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count && i < MAX_MADE; i++) {
        if (__atomic_load_n(&made[i].ran, __ATOMIC_ACQUIRE) && __atomic_load_n(&made[i].web, __ATOMIC_ACQUIRE) &&
            __atomic_load_n(&made[i].tid, __ATOMIC_RELAXED) == tid)
            return true;
    }
    return false;
}

static unsigned long image_offset(const void *p)
{
    return (unsigned long)((uintptr_t)p - (uintptr_t)__ehdr_start);
}

static void *made_entry(void *p)
{
    made_thread *m = static_cast<made_thread *>(p);
    long tid = 0;
    thr_self(&tid);
    m->tid = tid;
    my_made = m;
    web_thread = __atomic_load_n(&m->web, __ATOMIC_ACQUIRE);
    if (web_thread)
        web_stack_top = stack_top_now();
    __atomic_store_n(&m->ran, 1, __ATOMIC_RELEASE);
    const double started_after = thread_ms() - m->asked_ms;
    thread_time(&thread_start_us, &thread_start_max_us, started_after);
    int policy = -1;
    struct sched_param sp = {};
    pthread_getschedparam(pthread_self(), &policy, &sp);
    web_say("[web_host] made #%u runs: tid %ld, policy %d priority %d%s\n",
            (unsigned)(m - made), tid, policy, sp.sched_priority, web_thread ? ", WebProcess" : "");
    if (started_after >= 20)
        web_say("[web_host] SLOW thread #%u: it runs %.0f ms after it was asked for\n",
                (unsigned)(m - made), started_after);
    void *const result = m->start(m->arg);
    __atomic_store_n(&m->ended, 1, __ATOMIC_RELEASE);
    return result;
}

extern "C" int web_host_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                       void *(*start)(void *), void *arg)
{
    const unsigned n = __atomic_fetch_add(&made_count, 1, __ATOMIC_ACQ_REL);
    if (n >= MAX_MADE)
        return ps5platform_pthread_create(thread, attr, start, arg);
    made_thread *m = &made[n];
    size_t size = 0;
    int inherit = -1, policy = -1, detach = -1;
    struct sched_param sp = {};
    if (attr) {
        pthread_attr_getstacksize(attr, &size);
        pthread_attr_getinheritsched(attr, &inherit);
        pthread_attr_getschedpolicy(attr, &policy);
        pthread_attr_getschedparam(attr, &sp);
        pthread_attr_getdetachstate(attr, &detach);
    }
    thr_self(&m->creator);
    m->start = start;
    m->arg = arg;
    __atomic_store_n(&m->web, web_thread ? 1 : 0, __ATOMIC_RELEASE);
    web_say("[web_host] made #%u by tid %ld: start 0x%lx, stack %zu, inherit %d, policy %d "
            "priority %d, detached %d\n",
            n, m->creator, image_offset((const void *)start), size, inherit, policy,
            sp.sched_priority, detach);
    m->asked_ms = thread_ms();
    const int err = ps5platform_pthread_create(thread, attr, made_entry, m);
    const double create_ms = thread_ms() - m->asked_ms;
    __atomic_add_fetch(&thread_made_n, 1, __ATOMIC_RELAXED);
    thread_time(&thread_create_us, &thread_create_max_us, create_ms);
    if (!err) {
        m->thread = *thread;
        __atomic_store_n(&m->made, 1, __ATOMIC_RELEASE);
    }
    web_say("[web_host] made #%u: %s\n", n, err ? strerror(err) : "ok");
    if (create_ms >= 20)
        web_say("[web_host] SLOW thread #%u: pthread_create took %.0f ms\n", n, create_ms);
    return err;
}

/* The threads made that never ran, and a try at their stacks (a thread
 * that waits takes the signal; one that never runs does not). */
static void dump_made(void)
{
    const unsigned count = __atomic_load_n(&made_count, __ATOMIC_ACQUIRE);
    unsigned never = 0;
    for (unsigned i = 0; i < count && i < MAX_MADE; i++) {
        if (!__atomic_load_n(&made[i].made, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&made[i].ran, __ATOMIC_ACQUIRE))
            continue;
        never++;
        web_say("[web_host] made #%u (by tid %ld) never ran; its stack:\n", i, made[i].creator);
        browserps5_dump_thread(made[i].thread);
    }
    web_say("[web_host] %u thread(s) made, %u never ran\n", count, never);
}

/* All of the title's threads as the system has them (kern.proc with
 * threads): each one's state (2 runs or can run, 3 sleeps, 7 waits for a
 * lock), what it waits on, its CPU and priority. Only read, and only when
 * the system's entry is at least ours. */
static void list_threads(const char *when)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, (int)getpid()};
    size_t bytes = 0;
    if (sysctl(mib, 4, nullptr, &bytes, nullptr, 0)) {
        web_say("[web_host] threads at %s: %s\n", when, strerror(errno));
        return;
    }
    bytes += 32 * sizeof(struct kinfo_proc);
    char *buffer = static_cast<char *>(malloc(bytes));
    if (!buffer)
        return;
    if (sysctl(mib, 4, buffer, &bytes, nullptr, 0)) {
        web_say("[web_host] threads at %s: %s\n", when, strerror(errno));
        free(buffer);
        return;
    }
    const struct kinfo_proc *first = reinterpret_cast<const struct kinfo_proc *>(buffer);
    const size_t entry = bytes >= sizeof(int) && first->ki_structsize > 0
                             ? (size_t)first->ki_structsize : sizeof(struct kinfo_proc);
    web_say("[web_host] threads at %s: %zu bytes, entries of %zu (ours %zu)\n", when, bytes,
            entry, sizeof(struct kinfo_proc));
    if (entry >= sizeof(struct kinfo_proc)) {
        for (size_t at = 0; at + entry <= bytes; at += entry) {
            const struct kinfo_proc *k = reinterpret_cast<const struct kinfo_proc *>(buffer + at);
            web_say("[web_host]   tid %d \"%.16s\": state %d, waits \"%.8s\", cpu %d (last %d), "
                    "priority %u/%u/%u class %u\n",
                    (int)k->ki_tid, k->ki_tdname, (int)k->ki_stat, k->ki_wmesg, k->ki_oncpu,
                    k->ki_lastcpu, (unsigned)k->ki_pri.pri_level, (unsigned)k->ki_pri.pri_user,
                    (unsigned)k->ki_pri.pri_native, (unsigned)k->ki_pri.pri_class);
        }
    }
    free(buffer);
}

/* A thread made the way WebKit makes its own (the whole chain, the
 * creator does not wait), from this thread: whether a new thread still
 * runs once the WebProcess waits. */
static void *test_body(void *)
{
    web_say("[web_host] the test thread runs\n");
    return nullptr;
}

static void test_thread(void)
{
    pthread_t thread;
    const int err = pthread_create(&thread, nullptr, test_body, nullptr);
    web_say("[web_host] test thread: %s\n", err ? strerror(err) : "made");
    if (!err)
        pthread_detach(thread);
}

/* WebKit's threads by name (WTF names each one as it starts; build.sh:
 * --wrap=pthread_setname_np, one argument here), for their stacks at 10 s:
 * 20:11, the connection's "SocketMonitor" never called select(). */
#define MAX_NAMED 64
static struct {
    pthread_t thread;
    char name[32];
} named[MAX_NAMED];
static unsigned named_count;

extern "C" int __real_pthread_setname_np(const char *name);
extern "C" int __wrap_pthread_setname_np(const char *name)
{
    const unsigned i = __atomic_fetch_add(&named_count, 1, __ATOMIC_ACQ_REL);
    if (i < MAX_NAMED) {
        named[i].thread = pthread_self();
        snprintf(named[i].name, sizeof(named[i].name), "%s", name ? name : "");
    }
    web_say("[web_host] thread %u named \"%s\"\n", i, name ? name : "");
    return __real_pthread_setname_np(name);
}

static void dump_named(void)
{
    const unsigned count = __atomic_load_n(&named_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count && i < MAX_NAMED; i++) {
        web_say("[web_host] stack of thread %u \"%s\":\n", i, named[i].name);
        browserps5_dump_thread(named[i].thread);
    }
}

/* WebKit's PlayStation connection waits for its socket in select() (its
 * "SocketMonitor" thread, ConnectionUnix.cpp): the first calls in the
 * WebProcess's log, before and after (build.sh: --wrap=select). */
extern "C" int __real_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t);
extern "C" int __wrap_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
    static unsigned calls;
    const unsigned call = __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    const bool traced = call <= 30;
    if (traced)
        web_say("[web_host] select #%u: %d descriptors, %s%s%s, %s\n", call, nfds,
                r ? "read" : "", w ? " write" : "", e ? " error" : "", t ? "a timeout" : "no timeout");
    const int ret = __real_select(nfds, r, w, e, t);
    const int saved = errno;
    if (traced)
        web_say("[web_host] select #%u: %d (%s)\n", call, ret, ret < 0 ? strerror(saved) : "ok");
    /* The "SocketMonitor" loop calls select() again at once after an error:
     * a refused select() would make it spin at the title's priority (no
     * spinning threads: memory, 2026-11-04 14:02). A pause after each. */
    if (ret < 0 && saved != EINTR)
        usleep(10000);
    errno = saved;
    return ret;
}

/* WebKit's and the transport's lines go to stderr and to descriptor 2
 * (write(2, ...)), and a title keeps neither (no dup2 here; closing 2 and
 * opening again did not give 2 back, 2026-11-05 19:47): all of them go to
 * a file of their own. */
static void log_to_file(void)
{
    const int fd = open(STDERR_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        klog_printf("[web_host] %s: %s\n", STDERR_PATH, strerror(errno));
        return;
    }
    FILE *log = fdopen(fd, "w");
    if (!log) {
        close(fd);
        return;
    }
#ifdef TITLE_QUIET_LOG
    setvbuf(log, nullptr, _IOFBF, 1 << 18);  // written out twice a second (dump_watch())
    setenv("BROWSERPS5_THREAD_LOG", "0", 0);  // libc-fixes.c: no line for each thread
#else
    setvbuf(log, nullptr, _IOLBF, 0);
#endif
    stderr = log;
    /* The lines written to the descriptor itself (no stdio): the
     * transport's and crash-report.c's (stacks, the load probe). */
    ipc_transport_log_fd(fd);
    browserps5_report_fd(fd);
    web_log = fd;
    web_log_read = open(STDERR_PATH, O_RDONLY);
    klog_printf("[web_host] the WebProcess's stderr goes to %s (descriptor %d)\n", STDERR_PATH, fd);
}

/* The WebProcess's log as it grows, in the system's log too: a console that
 * goes down keeps nothing that was only in the file (2026-11-05 21:06: both
 * reports came back as zeros), and run-2c2.sh saves the system's log on the
 * PC as the test runs (poc/klog.ps1). Whole lines, a few times a second. */
static off_t log_sent;
static int log_busy;

static void log_forward(void)
{
    int idle = 0;
    if (web_log_read < 0 ||
        !__atomic_compare_exchange_n(&log_busy, &idle, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;
    static char chunk[4096];
    for (;;) {
        const ssize_t n = pread(web_log_read, chunk, sizeof(chunk) - 1, log_sent);
        if (n <= 0)
            break;
        ssize_t end = n;
        const bool full = n == (ssize_t)sizeof(chunk) - 1;
        if (!full) {
            while (end > 0 && chunk[end - 1] != '\n')
                --end;
            if (!end)
                break;  // half a line: the rest comes
        }
        for (ssize_t at = 0; at < end;) {
            ssize_t stop = at;
            while (stop < end && chunk[stop] != '\n' && stop - at < 500)
                ++stop;
            const ssize_t next = stop < end && chunk[stop] == '\n' ? stop + 1 : stop;
            char line[520];
            const size_t bytes = (size_t)(stop - at);
            memcpy(line, chunk + at, bytes);
            line[bytes] = '\n';
            line[bytes + 1] = 0;
            sceKernelDebugOutText(0, line);
            at = next;
        }
        log_sent += end;
        if (!full)
            break;
    }
    __atomic_store_n(&log_busy, 0, __ATOMIC_RELEASE);
}

static void *log_thread(void *)
{
    for (;;) {
        usleep(200000);
        log_forward();
    }
    return nullptr;
}

void web_host_log_flush(void)
{
    for (int i = 0; i < 50 && __atomic_load_n(&log_busy, __ATOMIC_ACQUIRE); i++)
        usleep(10000);
    log_forward();
}

/* A crash on a thread of the WebProcess (title.cpp's handler): its
 * registers, the frame-pointer chain and the stack from the fault up, then
 * the title closes its own way and the thread waits here. */
int web_host_crashed(int number, const void *context)
{
    if (!is_web_thread())
        return 0;
    const mcontext_t *mc = &((const ucontext_t *)context)->uc_mcontext;
    const void *sp = (const void *)mc->mc_rsp;
    const uintptr_t pc = (uintptr_t)mc->mc_rip;
    long tid = 0;
    thr_self(&tid);
    char line[400];
    int n = snprintf(line, sizeof(line),
                     "[web_host] a thread of the WebProcess (tid %ld) crashed with signal %d at "
                     "offset 0x%lx: it stays parked, the title closes its own way\n",
                     tid, number, image_offset((const void *)pc));
    if (n > 0 && web_log >= 0)
        (void)!write(web_log, line, (size_t)n);
    // The registers, and the two words under the stack pointer: after a
    // "ret" to a bad address, the one just under it is where it came from
    // (22:55: pc 0 with no return address at the stack pointer).
    const uintptr_t *words = (const uintptr_t *)sp;
    const bool readable = (uintptr_t)sp >= 16 && (uintptr_t)sp < web_stack_top;
    n = snprintf(line, sizeof(line),
                 "[web_host] registers: rip %#lx rsp %#lx rbp %#lx rax %#lx rbx %#lx rcx %#lx rdx %#lx "
                 "rsi %#lx rdi %#lx r8 %#lx r9 %#lx r12 %#lx r13 %#lx r14 %#lx r15 %#lx; "
                 "[rsp-16] %#lx [rsp-8] %#lx [rsp] %#lx; code ends at offset %#lx\n",
                 (unsigned long)mc->mc_rip, (unsigned long)mc->mc_rsp, (unsigned long)mc->mc_rbp,
                 (unsigned long)mc->mc_rax, (unsigned long)mc->mc_rbx, (unsigned long)mc->mc_rcx,
                 (unsigned long)mc->mc_rdx, (unsigned long)mc->mc_rsi, (unsigned long)mc->mc_rdi,
                 (unsigned long)mc->mc_r8, (unsigned long)mc->mc_r9, (unsigned long)mc->mc_r12,
                 (unsigned long)mc->mc_r13, (unsigned long)mc->mc_r14, (unsigned long)mc->mc_r15,
                 readable ? (unsigned long)words[-2] : 0UL, readable ? (unsigned long)words[-1] : 0UL,
                 readable ? (unsigned long)words[0] : 0UL, image_offset(__eh_frame_hdr_start));
    if (n > 0 && web_log >= 0)
        (void)!write(web_log, line, (size_t)n);
    // Only with the stack's top known: without it the scan reads 4 KiB up
    // from the stack pointer, which may be past the top (a fault in here).
    if (web_stack_top) {
        browserps5_frame_walk((const void *)mc->mc_rbp, sp, web_stack_top);
        browserps5_stack_scan(sp, web_stack_top, pc);
    }
    web_say_flush();
    log_forward();
    title_web_failed("a thread of the WebProcess crashed");
    park();
}

/* WebKit's own crash (crash-report.c, after its file, line and callers):
 * the same, on a thread of the WebProcess; elsewhere _exit as before. */
static void web_crash_end(int code)
{
    if (!is_web_thread())
        _exit(code);
    log_forward();
    title_web_failed("WebKit's own crash on a thread of the WebProcess");
    park();
}

/* The title's close: the WebProcess's other threads stop where they are
 * (no more GPU work, no more writing to the rings) before its GPU client,
 * video, sound and screen go. */
#define FREEZE_SIGNAL SIGXCPU

static void frozen(int)
{
    park();
}

void web_host_freeze(void)
{
    struct sigaction action = {};
    action.sa_handler = frozen;
    sigemptyset(&action.sa_mask);
    sigaction(FREEZE_SIGNAL, &action, nullptr);
    const pthread_t self = pthread_self();
    const unsigned count = __atomic_load_n(&made_count, __ATOMIC_ACQUIRE);
    unsigned stopped = 0;
    for (unsigned i = 0; i < count && i < MAX_MADE; i++) {
        made_thread *m = &made[i];
        if (!__atomic_load_n(&m->made, __ATOMIC_ACQUIRE) || !__atomic_load_n(&m->ran, __ATOMIC_ACQUIRE) ||
            !__atomic_load_n(&m->web, __ATOMIC_ACQUIRE) || __atomic_load_n(&m->ended, __ATOMIC_ACQUIRE) ||
            pthread_equal(m->thread, self))
            continue;
        if (!pthread_kill(m->thread, FREEZE_SIGNAL))
            ++stopped;
    }
    if (stopped)
        usleep(100000);
    klog_printf("[web_host] %u thread(s) of the WebProcess stopped for the title's close\n", stopped);
}

/* 2026-11-11 15:00: the UIProcess's page watch (main.cpp) asks for the
 * WebProcess's stacks when the page has not answered for 6 s, with the file
 * DUMP_REQUEST in the title's folder: its main thread's. A timed dump
 * (BROWSERPS5_STACK_DUMPS) interrupts the thread every time, stall or not,
 * and the runs with those had no long stall: this one only looks once the
 * page stands still. 15:03: the first run with it took the CONSOLE DOWN: it
 * then also signalled every named thread (some eighty: workers, the
 * compositors, the collector's) of the stalled process, four times. The main
 * thread only, and at most three times a run. */
#define DUMP_REQUEST "/app0/dump-request"
static pthread_t web_main_pthread;
static int web_main_pthread_known;

static void *dump_watch(void *)
{
    unlink(DUMP_REQUEST);  // one left by a run that ended
    for (;;) {
        usleep(500000);
#ifdef TITLE_QUIET_LOG
        web_say_flush();  // the lines kept in memory, and stdio's
        fflush(stderr);
#endif
        if (access(DUMP_REQUEST, F_OK))
            continue;
        unlink(DUMP_REQUEST);
        static unsigned asked;
        if (++asked > 3)
            continue;
        web_say("[web_host] stack asked for (the page does not answer): the WebProcess's main thread\n");
        if (__atomic_load_n(&web_main_pthread_known, __ATOMIC_ACQUIRE))
            browserps5_dump_thread(web_main_pthread);
        /* 15:50: the main thread waited for a thread it had just made (a
         * work queue's): the two newest threads of the WebProcess that run,
         * and no more (15:03). */
        {
            const unsigned count = __atomic_load_n(&made_count, __ATOMIC_ACQUIRE);
            unsigned shown = 0;
            for (unsigned i = count < MAX_MADE ? count : MAX_MADE; i-- > 0 && shown < 2;) {
                made_thread *m = &made[i];
                if (!__atomic_load_n(&m->made, __ATOMIC_ACQUIRE) || !__atomic_load_n(&m->ran, __ATOMIC_ACQUIRE) ||
                    !__atomic_load_n(&m->web, __ATOMIC_ACQUIRE) || __atomic_load_n(&m->ended, __ATOMIC_ACQUIRE))
                    continue;
                web_say("[web_host] stack asked for: thread #%u (tid %ld), one of the two newest\n", i, m->tid);
                browserps5_dump_thread(m->thread);
                ++shown;
            }
        }
        web_say("[web_host] stack asked for: done\n");
    }
    return nullptr;
}

/* 2026-11-11: the right time for the pages. A console that never asks the
 * network for the time keeps the clock set by hand (on the test console: 23 minutes
 * ahead), and a page that signs something with the time (YouTube's check of
 * the browser) is then refused. One SNTP question (RFC 4330: 48 bytes each
 * way) to Google's public time servers, by their addresses (no name to look
 * up), 700 ms at most for each of two; the difference goes to JavaScript's
 * Date and to nothing else (webkit/runtime/CurrentTime.cpp.patch and
 * DateMath.h.patch; the first version moved the engine's whole wall clock,
 * and every timed wait ended late). Less than 2 s off: the
 * console's clock as it is. BROWSERPS5_CLOCK_OFFSET_MS in /app0/web-env.txt
 * says it instead (0: never asked). */
extern "C" void browserps5_clock_offset_ms(double);

static void clock_from_network(void)
{
    if (const char *said = getenv("BROWSERPS5_CLOCK_OFFSET_MS")) {
        browserps5_clock_offset_ms(atof(said));
        klog_printf("[web_host] clock: %s ms, as web-env.txt says\n", said);
        return;
    }
    static const char *const servers[] = {"216.239.35.0", "216.239.35.4"};  // time1 and time2.google.com
    for (const char *server : servers) {
        const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0)
            break;
        struct timeval wait = {0, 700000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
        struct sockaddr_in to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(123);
        inet_pton(AF_INET, server, &to.sin_addr);
        unsigned char packet[48] = {0x23};  // no leap warning, version 4, a client
        struct timespec asked = {}, answered = {};
        clock_gettime(CLOCK_REALTIME, &asked);
        ssize_t n = sendto(s, packet, sizeof(packet), 0, (struct sockaddr *)&to, sizeof(to));
        if (n == (ssize_t)sizeof(packet))
            n = recv(s, packet, sizeof(packet), 0);
        clock_gettime(CLOCK_REALTIME, &answered);
        close(s);
        if (n < 48 || (packet[0] & 7) != 4 || !packet[1]) {  // not a server's answer, or its "go away"
            klog_printf("[web_host] clock: no time from %s (%zd bytes, errno %d)\n", server, n, errno);
            continue;
        }
        const uint32_t seconds = (uint32_t)packet[40] << 24 | (uint32_t)packet[41] << 16 | (uint32_t)packet[42] << 8 | packet[43];
        const uint32_t fraction = (uint32_t)packet[44] << 24 | (uint32_t)packet[45] << 16 | (uint32_t)packet[46] << 8 | packet[47];
        // Seconds since 1900; from 2036 on they start again at 0.
        const double there = (double)seconds + (seconds < 0x80000000u ? 4294967296.0 : 0.0) - 2208988800.0 + fraction / 4294967296.0;
        const double sent = (double)asked.tv_sec + asked.tv_nsec / 1e9, got = (double)answered.tv_sec + answered.tv_nsec / 1e9;
        const double off = there - (sent + got) / 2;  // the answer left the server about half way through
        if (there < 1.7e9 || there > 4.1e9) {  // 2023 to 2100: anything else is no time
            klog_printf("[web_host] clock: %s said %.0f: not taken\n", server, there);
            continue;
        }
        if (off > -2 && off < 2) {
            browserps5_clock_offset_ms(0);
            klog_printf("[web_host] clock: the console's is right (%.2f s off, %s in %.0f ms)\n", off, server, (got - sent) * 1e3);
        } else {
            browserps5_clock_offset_ms(off * 1e3);
            klog_printf("[web_host] clock: the console's is %.0f s %s: the pages get the network's time (%s in %.0f ms)\n",
                        off < 0 ? -off : off, off < 0 ? "ahead" : "behind", server, (got - sent) * 1e3);
        }
        return;
    }
    klog_printf("[web_host] clock: no time from the network: the console's clock as it is\n");
}

/* 2026-11-11: the pages' time zone. The WebProcess had none (TZ unset): a
 * page's hours (a chat's, a mail's, a calendar's) were Greenwich's, right in
 * Portugal in winter only. The console's own setting (its Date and Time): the
 * minutes from Greenwich and whether summer time is on, as a zone the
 * engine's ICU knows: Etc/GMT-1 is one hour east (the sign is the other way
 * round there), and the places whose hour is not a whole one by name. The
 * summer hour is read when the app starts, not when it changes. TZ in
 * /app0/web-env.txt says it instead. */
extern "C" int sceSystemServiceParamGetInt(int id, int *value);

static void zone_from_console(void)
{
    if (const char *said = getenv("TZ")) {
        klog_printf("[web_host] zone: %s, as web-env.txt says\n", said);
        return;
    }
    int minutes = 0, summer = 0;
    const int err = sceSystemServiceParamGetInt(4, &minutes);  // SCE_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE
    const int err2 = sceSystemServiceParamGetInt(5, &summer);  // ..._SUMMERTIME
    if (err) {
        klog_printf("[web_host] zone: the console did not say (0x%08x): UTC\n", (unsigned)err);
        return;
    }
    const int total = minutes + (!err2 && summer ? 60 : 0);
    static const struct {
        int minutes;
        const char *name;
    } odd[] = {{330, "Asia/Kolkata"}, {345, "Asia/Kathmandu"}, {210, "Asia/Tehran"}, {270, "Asia/Kabul"},
               {390, "Asia/Yangon"}, {570, "Australia/Darwin"}, {630, "Australia/Adelaide"}, {-210, "America/St_Johns"},
               {-150, "America/St_Johns"}};
    char zone[40] = "";
    if (total % 60 == 0 && total >= -12 * 60 && total <= 14 * 60) {
        if (total)
            snprintf(zone, sizeof(zone), "Etc/GMT%+d", -total / 60);
        else
            snprintf(zone, sizeof(zone), "Etc/GMT");
    } else {
        for (const auto &o : odd)
            if (o.minutes == total)
                snprintf(zone, sizeof(zone), "%s", o.name);
    }
    if (!zone[0]) {
        klog_printf("[web_host] zone: %d minutes from Greenwich has no name here: UTC\n", total);
        return;
    }
    setenv("TZ", zone, 1);
    klog_printf("[web_host] zone: %s (the console: %d minutes from Greenwich, summer time %s)\n", zone, minutes,
                err2 ? "not said" : summer ? "on" : "off");
}

static void *web_process(void *)
{
    web_thread = true;
    web_main_pthread = pthread_self();
    __atomic_store_n(&web_main_pthread_known, 1, __ATOMIC_RELEASE);
    web_stack_top = stack_top_now();
    if (my_made)
        __atomic_store_n(&my_made->web, 1, __ATOMIC_RELEASE);
    long tid = 0;
    thr_self(&tid);
    __atomic_store_n(&web_main_tid, tid, __ATOMIC_RELEASE);
    char program[] = "WebProcess";
    char *argv[] = {program, identifier, connection, nullptr};
    klog_printf("[web_host] WebProcess %s starts in the title (connection %s); flexible memory "
                "%ld MiB left\n", identifier, connection, flexible_mib());
    clock_from_network();
    zone_from_console();
    const int code = WebKit::WebProcessMain(3, argv);
    /* Its thread never ends: ending it ran WebKit's main-thread teardown
     * (the thread's destructors) with the WebProcess's other threads still
     * there, a RELEASE_ASSERT in MainThreadSharedTimer::setFiredFunction
     * followed, its exit(134) was a SIGSYS that nothing parked (the thread
     * no longer counted as the WebProcess's), the system killed the title
     * with its GPU client, video and sound open, and the console went down
     * (2026-11-06 13:01, the 2c2n run). It waits, as a parked crash does;
     * the title ends its own way when the UIProcess quits. */
    klog_printf("[web_host] WebProcessMain returned %d: its thread stays parked; the title "
                "goes on until the UIProcess quits\n", code);
    if (code)
        title_web_failed("WebProcessMain returned an error");
    park();
}

static void answer(int s, const char *line)
{
    (void)!write(s, line, strlen(line));
}

/* One request: "BPS5WEB1 <identifier> BPS5RV1 <port> <secret>\n". */
static void serve(int s)
{
    char line[WEB_HOST_LINE_MAX];
    size_t got = 0;
    while (got + 1 < sizeof(line)) {
        struct pollfd p = {s, POLLIN, 0};
        if (poll(&p, 1, 5000) != 1 || read(s, line + got, 1) != 1)
            break;
        if (line[got++] == '\n')
            break;
    }
    line[got] = 0;
    const size_t magic = strlen(WEB_HOST_MAGIC);
    char id[32];
    int offset = 0;
    if (got < magic || strncmp(line, WEB_HOST_MAGIC, magic)
        || sscanf(line + magic, "%31s %n", id, &offset) != 1 || !offset) {
        klog_printf("[web_host] a request that is not ours: closed\n");
        answer(s, "NO not a request\n");
        return;
    }
    if (started) {
        klog_printf("[web_host] WebProcess %s: one runs here already, the launcher starts it\n", id);
        answer(s, "NO one runs in the title already\n");
        return;
    }
    ipc_transport_title_mode();
    const int fd = ipc_transport_endpoint_from_line(line + magic + offset);
    if (fd < 0) {
        char no[96];
        snprintf(no, sizeof(no), "NO meeting the UIProcess: %s\n", strerror(errno));
        klog_printf("[web_host] WebProcess %s: %s", id, no + 3);
        answer(s, no);
        return;
    }
    snprintf(identifier, sizeof(identifier), "%s", id);
    snprintf(connection, sizeof(connection), "%d", fd);
    conn_fd = fd;
    PlayStation::setConnectionIdentifier(fd);
    mkdir(HOME_DIR, 0700);
    log_to_file();
    pthread_t forwarder;
    if (!pthread_create(&forwarder, nullptr, log_thread, nullptr))
        pthread_detach(forwarder);
    browserps5_crash_end(web_crash_end);
    browserps5_code_end((uintptr_t)__eh_frame_hdr_start);
    setenv("HOME", HOME_DIR, 1);
    setenv("FONTCONFIG_FILE", FONTS_CONF, 1);
    /* 2026-11-09: no GStreamer log by default. With "2", the media players'
     * errors and warnings (YouTube, publico.pt) were written from the
     * WebProcess thread, which then stopped answering for tens of seconds:
     * the browser stopped navigating (BrowserPS5-WebKit/README.md).
     * GST_DEBUG=2 in /app0/web-env.txt turns it back on for a test. */
    setenv("GST_DEBUG", "0", 0);
    /* crash-report.c's probes (the payloads take them from env.txt, which a
     * title cannot read): the WebProcess thread's stack at these seconds,
     * the busiest threads every second, and the spin guard only writing (its
     * exit would park the thread, not end anything). */
    /* 2026-11-09: /app0/web-env.txt (KEY=VALUE lines), when a test puts it
     * there, goes before these defaults: e.g. BROWSERPS5_STACK_DUMPS=30,60,90
     * for a WebProcess that stops answering after 45 s (v1's stall after
     * pages with video, BrowserPS5-WebKit/README.md). */
    if (FILE* env = fopen("/app0/web-env.txt", "r")) {
        char line[512];
        while (fgets(line, sizeof(line), env)) {
            line[strcspn(line, "\r\n")] = 0;
            char* eq = strchr(line, '=');
            if (eq && eq != line && line[0] != '#') {
                *eq = 0;
                setenv(line, eq + 1, 1);
                klog_printf("[web_host] web-env.txt: %s=%s\n", line, eq + 1);
            }
        }
        fclose(env);
    }
    setenv("BROWSERPS5_STACK_DUMPS", "8,20,45", 0);
    setenv("BROWSERPS5_STACK_CALLERS", "70", 0);
    setenv("BROWSERPS5_LOAD_PROBE", "1", 0);
    setenv("BROWSERPS5_LOAD_PROBE_PRIORITY", "400", 0);
    setenv("BROWSERPS5_SPIN_GUARD", "log", 1);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WEB_STACK_BYTES);
    pthread_t thread;
    const int err = pthread_create(&thread, &attr, web_process, nullptr);
    pthread_attr_destroy(&attr);
    if (err) {
        klog_printf("[web_host] WebProcess %s: no thread: %s\n", id, strerror(err));
        close(fd);
        answer(s, "NO no thread for it\n");
        return;
    }
    pthread_detach(thread);
    started = 1;
    pthread_t watcher;
    if (!pthread_create(&watcher, nullptr, watch, nullptr))
        pthread_detach(watcher);
    pthread_t dumper;
    if (!pthread_create(&dumper, nullptr, dump_watch, nullptr))
        pthread_detach(dumper);
    char ok[32];
    snprintf(ok, sizeof(ok), "OK %d\n", (int)getpid());
    answer(s, ok);
}

static void *host(void *arg)
{
    const int listener = (int)(intptr_t)arg;
    for (;;) {
        /* (listener < 0: a title that may not listen calls the menu instead: web_host_start()) */
        const int s = listener < 0 ? title_link_dial(WEB_HOST_PORT) : accept(listener, nullptr, nullptr);
        if (s < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            klog_printf("[web_host] accept: %s; no more WebProcesses here\n", strerror(errno));
            break;
        }
        serve(s);
        close(s);
    }
    close(listener);
    return nullptr;
}

int web_host_start(void)
{
    main_thread = pthread_self();  // main() calls this
    main_known = 1;
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        klog_printf("[web_host] socket: %s\n", strerror(errno));
        return -1;
    }
    const int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(WEB_HOST_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* 2026-11-12: a title of the media category may not listen (EACCES):
     * it calls the browser's menu instead (../../common/title_link.h). */
    int served = listener;
    if (bind(listener, (struct sockaddr *)&a, sizeof(a)) || listen(listener, 4)) {
        klog_printf("[web_host] no listening on 127.0.0.1:%d (%s): the title calls the browser's menu on %d instead\n",
                    WEB_HOST_PORT, strerror(errno), WEB_HOST_PORT + TITLE_LINK_OFFSET);
        close(listener);
        served = -1;
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, host, (void *)(intptr_t)served)) {
        if (served >= 0)
            close(served);
        return -1;
    }
    pthread_detach(thread);
    if (served >= 0)
        klog_printf("[web_host] the WebProcess may run here: listening on 127.0.0.1:%d\n",
                    WEB_HOST_PORT);
    return 0;
}
