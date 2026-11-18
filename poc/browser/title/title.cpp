/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The browser's title (PPSA97020; docs/03-video-e-gpu.md, G1). Only a title
 * gets the GPU, and opening a title closes the app in front, so the
 * browser's screen lives here and WebKit's UIProcess runs in a payload
 * (remote.c). This title:
 *   - owns the GPU for payloads: PS5_Vulkan's broker on 127.0.0.1:9040, as
 *     ../../gpu_broker/owner.cpp;
 *   - shows the frames the UIProcess puts in LINK_VIEW_SHM (link.h);
 *   - reads the controller and opens the system keyboard (hello_page's
 *     input.c and ime.c) and sends what they give to the UIProcess;
 *   - plays the sound of WebKit's media, which ps5audiosink in the
 *     WebProcess writes into the rings of AUDIO_RING_SHM
 *     (../../common/audio_ring.h): while an app is in front, the system
 *     silences the sound of other processes (2026-11-04);
 *   - pauses the pages while the system is over it (see "The pause"), so a
 *     close from the system finds no GPU client.
 * It connects to the UIProcess on 127.0.0.1:LINK_PORT, and closes when
 * the UIProcess closes that link. Opened from the home screen, with no
 * UIProcess, it sends /app0/browser.elf to elfldr first (start_browser()).
 * Its report: /app0/browser-title.txt and klog; the browser's own output,
 * when the title started it: /app0/browser-run.txt.
 */
#include "server.h"
#include "packet_io_tcp.h"

#include "display.h"
#include "ime.h"
#ifdef TITLE_MIC
#include "mic.h"
#endif
#include "input.h"
#ifdef TITLE_KBM
#include "kbm.h"
#endif
#include "feel.h"
#include "link.h"
#include "tone.h"
#include "audio_ring.h"
#include "video.h"
#include "splash.h"
#include "capture.h"
#ifdef WEBKIT_IN_TITLE
#include "web_host.h"
#endif

#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" int sceKernelUsleep(unsigned microseconds);

#include "../../common/title_link.h"  // a title that may not listen calls the menu instead

#define OWNER_PORT 9040
#define REPORT_PATH "/app0/browser-title.txt"
#define CONNECT_SECONDS 60

static FILE *report_file;
static pthread_mutex_t report_lock = PTHREAD_MUTEX_INITIALIZER;

static void report_line(const char *line)
{
    pthread_mutex_lock(&report_lock);
    if (report_file)
        fputs(line, report_file);
    sceKernelDebugOutText(0, line);
    pthread_mutex_unlock(&report_lock);
}

static void report(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int n = snprintf(line, sizeof(line), "[browser_title] ");
    vsnprintf(line + n, sizeof(line) - n - 1, format, arguments);
    va_end(arguments);
    strcat(line, "\n");
    report_line(line);
}

/* The title's "flexible" memory at each step of its start (2026-11-06, 2c2n:
 * of its 448 MiB, only 219 were left when the WebProcess started, and the
 * pages took it down to 40 MiB in 50 s; docs/05). The system's own modules
 * (malloc, their anonymous mappings) take it; what the title links in maps
 * direct memory (poc/webkit_title/title_memory.c). In KiB: it goes in 16 KiB
 * pages. */
extern "C" int sceKernelAvailableFlexibleMemorySize(size_t *size);
extern "C" int sceKernelConfiguredFlexibleMemorySize(size_t *size);
static long flexible_last_kib = -1;

/* 2026-11-10: the direct memory WebKit's heaps take in the title
 * (poc/webkit_title/title_memory.c), in the screen's line every 5 s. Weak:
 * only the WEBKIT=1 titles link it in. */
extern "C" {
typedef struct title_memory_stats {
    size_t direct_bytes, ranges, peak_bytes, fallbacks;
    size_t alloc_failures, map_failures, table_full, joined;
} title_memory_stats_t;
void title_memory_stats(title_memory_stats_t *stats) __attribute__((weak));
/* 2026-11-14: the title's malloc on libpas (poc/webkit/malloc/ps5malloc.c, TITLE_MALLOC=1), in a line of
 * its own every 5 s. Under -DTITLE_MALLOC (a weak reference that nothing defines is an import to the
 * title's converter, which refuses it). */
#ifdef TITLE_MALLOC
typedef struct ps5malloc_stats {
    int pas;
    uint64_t foreign_frees, foreign_reallocs, fallbacks, bitmap_failures;
} ps5malloc_stats_t;
void ps5malloc_stats(ps5malloc_stats_t *stats);
#endif
/* 2026-11-14: the title's own malloc is the SDK's heap in direct memory (ps5platform/heap.h, linked by
 * every title: dlmalloc arenas, libc's small private heap when direct memory refuses). On the user's
 * console g_malloc() got NULL for 23 KiB twice, on the second and the fifth film of a session, with
 * 455 and 471 MiB of flexible memory left: it is the direct memory that ran out, and since the title
 * is a media application (2026-11-13) it has far less of it (title_memory's own requests were refused at
 * 2041 MiB on 11-14; on 11-11, as a game, it held 2548 MiB with none refused). So the report says what
 * the title may take when it starts, and every 5 s what the heap holds and the largest free piece. */
struct ps5_heap_stats {
    uintptr_t range_base;
    size_t range_bytes, mapped_bytes, peak_bytes;
    unsigned segments;
    unsigned long long libc_fallbacks;
    unsigned arenas;
};
void ps5_heap_stats(struct ps5_heap_stats *stats);
/* (sceKernelAllocateDirectMemory and its family: ps5platform/kernel.h) */
}

/* How much direct memory the title may take now: 64 MiB pieces allocated (never mapped) until one is
 * refused, then all given back. Only where no other thread allocates: the first lines of main(). */
static void direct_budget_note(const char *step)
{
    static int64_t taken[256];
    const size_t piece = (size_t)64 << 20;
    const int64_t pool = sceKernelGetDirectMemorySize();
    int n = 0, err = 0;
    while (n < 256) {
        int64_t phys = 0;
        if ((err = sceKernelAllocateDirectMemory(0, pool, piece, 0x4000, 0, &phys)))
            break;
        taken[n++] = phys;
    }
    for (int i = 0; i < n; i++)
        sceKernelReleaseDirectMemory(taken[i], piece);
    size_t flexible = 0, configured = 0;
    sceKernelAvailableFlexibleMemorySize(&flexible);
    sceKernelConfiguredFlexibleMemorySize(&configured);
    report("direct memory %s: %d MiB could be taken, 64 MiB at a time (then 0x%08x); the pool is %zu MiB; "
           "flexible %zu MiB left of %zu",
           step, n * 64, (unsigned)err, (size_t)pool >> 20, flexible >> 20, configured >> 20);
}

static void flexible_note(const char *step)
{
    size_t available = 0;
    if (sceKernelAvailableFlexibleMemorySize(&available)) {
        report("flexible memory after %s: no answer", step);
        return;
    }
    const long kib = (long)(available >> 10);
    if (flexible_last_kib < 0) {
        /* What param.json gave the title (build.sh FLEXIBLE_MIB; 448 MiB
         * without it). */
        size_t configured = 0;
        sceKernelConfiguredFlexibleMemorySize(&configured);
        report("flexible memory after %s: %.1f MiB left of %zu MiB", step, kib / 1024.0, configured >> 20);
    } else
        report("flexible memory after %s: %.1f MiB left (%+.1f MiB)", step, kib / 1024.0,
               (kib - flexible_last_kib) / 1024.0);
    flexible_last_kib = kib;
}

/* The end (2026-11-05, the TV: "Ocorreu um erro" after Options quit the
 * browser, the report ending at "sound: port closed"). main() used to
 * return: exit() then ran the libraries' destructors (RADV, the broker's
 * statics, libc++) while the broker's thread and the controller's still
 * ran, and a title that ends its own process gets a SIGSYS at the end,
 * which the system reports as the game's error (docs/02: ShadowMountPlus's
 * log, "Erro no jogo", after _exit too: both seen again on 2026-11-05 at
 * 16:39, SIGSYS at the same place). Asking the system from here does not
 * work either: sceSystemServiceGetAppIdOfRunningBigApp() gives a title
 * 0x8094000f, no app id. A payload (the UIProcess) asking the system to
 * close the title after all this was closed took the console down twice
 * (2026-11-05 16:51 and 17:14). So the quit now comes first as LINK_ENDED
 * with LINK_ENDED_QUIT: the title lets go of the GPU client and, still
 * showing the screen as in the pause (the state of the user's safe close
 * from the system, 2026-11-04 16:22), says LINK_TITLE_READY; the UIProcess
 * may then have the system close it (remote.c, close_title()). If LINK_QUIT
 * comes instead, all is closed as above and the title ends with _exit(0)
 * (the error dialog, but safe). With EXIT_NORMAL_MARKER it returns from main() as before, for a log-only
 * test that shows what that did. Either way a crash writes its signal and
 * place into the report (on_crash()), then the process dies of it. */
#define EXIT_NORMAL_MARKER "/app0/exit_normal"
/* The clean end to try (docs/10-achados-do-desktop.md, 3; 2026-11-06): with
 * this marker, once all is closed as for _exit(0), the title asks the
 * system to end it with sceSystemServiceLoadExec("exit", NULL), as the
 * user's Orbit title does (from RecompStore): on 13.60 its log showed "Kill
 * for LoadExec", an error 0x80aa001a, and the app left with the front free.
 * The system ends the process from outside, so the title waits; still
 * there after LOADEXEC_WAIT_MS, it ends with _exit(0) as before. The marker
 * stays (a file of its own tar): a log-only test first, without the video
 * (no /app0/video.txt), since every close from outside of a title with the
 * video took the console down (2026-11-05). */
#define EXIT_LOADEXEC_MARKER "/app0/exit_loadexec"
#define LOADEXEC_WAIT_MS 15000
extern "C" int sceSystemServiceLoadExec(const char *path, char *const argv[]);
static int report_fd = -1;           // the report's, for on_crash()
static volatile int exiting;          // 1 once main() has finished

static void crash_write(const char *line)
{
    if (report_fd >= 0)
        (void)!write(report_fd, line, strlen(line));
    sceKernelDebugOutText(0, line);
}

static void on_crash(int number, siginfo_t *info, void *context)
{
    char line[512];
    const ucontext_t *uc = (const ucontext_t *)context;
    const uintptr_t pc = (uintptr_t)uc->uc_mcontext.mc_rip;
    Dl_info where = {};
    int n = snprintf(line, sizeof(line), "[browser_title] CRASH %s: signal %d (code %d) at %p, pc %#lx",
                     exiting ? "while exiting" : "while running", number, info->si_code, info->si_addr,
                     (unsigned long)pc);
    if (dladdr((void *)pc, &where) && where.dli_fname)
        n += snprintf(line + n, sizeof(line) - n, " = %s+%#lx (%s+%#lx)", where.dli_fname,
                      (unsigned long)(pc - (uintptr_t)where.dli_fbase),
                      where.dli_sname ? where.dli_sname : "?",
                      (unsigned long)(pc - (uintptr_t)where.dli_saddr));
    snprintf(line + n, sizeof(line) - n, "\n");
    crash_write(line);
#ifdef WEBKIT_IN_TITLE
    /* A thread of the WebProcess (2c-2): its stack into the WebProcess's
     * log, then the title closes its own way (GPU client first) and the
     * thread stays parked: web_host_crashed() does not return. A title
     * that died of it took the console down (2026-11-05 21:01). SIGSYS
     * too: WebKit ending the WebProcess through a system call a title may
     * not make killed the title at 22:30. */
    if (!exiting)
        web_host_crashed(number, context);
    web_host_log_flush();
#endif
    // The default back: a fault comes again and kills; a SIGSYS (a system
    // call refused, _exit at the end) would only return.
    struct sigaction fallback = {};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(number, &fallback, nullptr);
    raise(number);
}

static void crash_hooks()
{
    struct sigaction action = {};
    action.sa_sigaction = on_crash;
    // Not SA_RESETHAND: a thread of the WebProcess that crashes is parked
    // and the hooks stay for the next; anything else dies of it as before.
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    static const int numbers[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    for (int number : numbers)
        sigaction(number, &action, nullptr);
    /* A write to a closed connection is an error, never a death: at
     * 2026-11-06 00:02 the title sent LINK_VSYNC to the browser that had
     * just quit, died of SIGPIPE with the video and the sound open, and
     * the system's kill of it took the console down. The link sends say
     * MSG_NOSIGNAL too; this covers every other socket (the GPU clients,
     * the WebProcess in the title). */
    struct sigaction ignore = {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPIPE, &ignore, nullptr);
}

/* With EXIT_NORMAL_MARKER: how far exit() gets. atexit() runs in the
 * opposite order of registration, so this one, made in main(), runs
 * before _fini (app_crt.cpp) and the destructors. */
static void exit_started()
{
    crash_write("[browser_title] exit: atexit, the libraries' destructors next\n");
}

__attribute__((destructor(101))) static void exit_destructors_done()
{
    if (exiting)
        crash_write("[browser_title] exit: the last destructor ran\n");
}

/* poc_log.h's second copy of each line (the shim's ps5/klog.h): the modules
 * of hello_page print their lines here. */
extern "C" int klog_printf(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    const int n = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    report_line(line);
    return n;
}

static double now_ms();

/* 2026-11-09: the starting screen (splash.h) while the browser comes: a
 * thread of its own until the UIProcess answers (each picture waits for its
 * flip, display_show: never a loop without a wait), then the main loop's,
 * until the UIProcess's first picture. */
static volatile bool splash_running;
static volatile double splash_target;  // where the line goes, 0..1
static double splash_line, splash_begun;
static pthread_t splash_thread;

static const uint32_t *splash_next()
{
    splash_line += (splash_target - splash_line) * 0.12;  // the line glides
    return splash_frame((now_ms() - splash_begun) / 1e3, splash_line);
}

static void *splash_run(void *)
{
    unsigned pictures = 0;
    while (splash_running) {
        const uint32_t *frame = splash_next();
        double tile = 0, wait = 0;
        if (!frame || display_show(frame, &tile, &wait))
            sceKernelUsleep(16000);
        pictures++;
    }
    report("starting screen: %u picture(s) in %.2f s", pictures, (now_ms() - splash_begun) / 1e3);
    return nullptr;
}

static bool splash_begin()
{
    if (splash_open(LINK_WIDTH, LINK_HEIGHT))
        return false;
    splash_begun = now_ms();
    splash_running = true;
    if (pthread_create(&splash_thread, nullptr, splash_run, nullptr)) {
        splash_running = false;
        splash_close();
        return false;
    }
    return true;
}

static void splash_pause()  // the main loop shows it from here on
{
    if (splash_running) {
        splash_running = false;
        pthread_join(splash_thread, nullptr);
    }
}

static double now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


/* The GPU broker, as owner.cpp. */

static bool absent(int) { return true; }

static int named_alloc(size_t bytes)
{
    static unsigned next;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    const unsigned number = next++;
    pthread_mutex_unlock(&lock);
    char name[32];
    snprintf(name, sizeof(name), "/bps5gpu-%u", number);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0 && errno == EEXIST) {
        shm_unlink(name);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    }
    if (fd < 0) {
        report("shm_open %s: %s", name, strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)bytes) || ps5_gpu_packet_name(fd, name)) {
        report("shared memory %s of %zu bytes: %s", name, bytes, strerror(errno));
        close(fd);
        shm_unlink(name);
        return -1;
    }
    return fd;
}

/* The client being served (one WebProcess at a time), and the one to let go
 * of once the UIProcess says its WebProcess ended for the pause (see "The
 * pause" below). Whoever closes a client takes its cookie out under the
 * lock first, so none is closed twice. */
static pthread_mutex_t gpu_lock = PTHREAD_MUTEX_INITIALIZER;
static int gpu_cookie;
static int gpu_away_cookie;

/* Waits for the client's thread, lets its last GPU work finish and frees
 * what it had (server.cpp). */
static void gpu_close(int cookie, const char *why)
{
    const double start = now_ms();
    ps5_gpu_broker_close(cookie, 0);
    report("GPU client %d closed (%s) in %.0f ms", cookie, why, now_ms() - start);
}

static void *broker_thread(void *)
{
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(OWNER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* 2026-11-12: a title of the media category (param.json
     * applicationCategoryType 65536: the PS5's media tab) may not listen:
     * bind() is EACCES. Then the title calls the browser's menu instead,
     * which listens for it (../../common/title_link.h). */
    bool calls = false;
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 4)) {
        report("broker: no listening on 127.0.0.1:%d (%s): the title calls the browser's menu on %d instead",
               OWNER_PORT, strerror(errno), OWNER_PORT + TITLE_LINK_OFFSET);
        if (listener >= 0)
            close(listener);
        calls = true;
    } else
        report("broker listening on 127.0.0.1:%d", OWNER_PORT);

    for (;;) {
        const int client = calls ? title_link_dial(OWNER_PORT) : accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno != EINTR)
                report("broker accept: %s", strerror(errno));
            sceKernelUsleep(100000);
            continue;
        }
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        /* A new WebProcess: the one before it is gone (it crashed, or was
         * ended and the pause did not close it). */
        pthread_mutex_lock(&gpu_lock);
        const int old = gpu_cookie;
        gpu_cookie = 0;
        pthread_mutex_unlock(&gpu_lock);
        if (old)
            gpu_close(old, "a new client came");
        ps5_gpu_packet_adopt(client);
        int cookie = 0;
        const int ours = ps5_gpu_broker_open(&cookie);
        if (ours < 0) {
            report("FAIL ps5_gpu_broker_open: %s", strerror(errno));
            close(client);
            continue;
        }
        close(ours);
        pthread_mutex_lock(&gpu_lock);
        gpu_cookie = cookie;
        pthread_mutex_unlock(&gpu_lock);
        report("GPU client %d connected", cookie);
    }
}


/* The sound test (docs/03-video-e-gpu.md, "Som"): when /app0/tone is there,
 * the title deletes it and, before it connects, plays ../../common/tone.h's
 * ways itself, as the app in front (the payload ../../audio_tone plays the
 * same ways, lower). */

#define TONE_MARKER "/app0/tone"

extern "C" int sceUserServiceInitialize(void *);
extern "C" int sceUserServiceGetForegroundUser(int *);
extern "C" int sceKernelSendNotificationRequest(int, void *, size_t, int);

static void tone_say(int notify, const char *text)
{
    report("%s", text);
    if (notify) {
        /* As poc_log.h's poc_notify(). */
        struct {
            char unused[45];
            char message[3075];
        } request;
        memset(&request, 0, sizeof(request));
        snprintf(request.message, sizeof(request.message), "[browser_title] %s", text);
        sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    }
}

static void tone_test()
{
    if (access(TONE_MARKER, F_OK))
        return;
    if (unlink(TONE_MARKER))
        report("unlink %s: %s", TONE_MARKER, strerror(errno));
    report("sceAudioOutInit: 0x%08x", (unsigned)sceAudioOutInit());
    int user = -1;
    sceUserServiceInitialize(nullptr);
    const int err = sceUserServiceGetForegroundUser(&user);
    report("sceUserServiceGetForegroundUser: 0x%08x, user 0x%x", (unsigned)err, (unsigned)user);
    if (err)
        user = -1;
    const tone_way_t ways[] = {
        {"title way 1 (system user, MAIN)", TONE_USER_SYSTEM, TONE_PORT_MAIN, 0},
        {"title way 2 (user in front, MAIN)", user, TONE_PORT_MAIN, 0},
        {"title way 3 (system user, BGM)", TONE_USER_SYSTEM, TONE_PORT_BGM, 0},
        {"title way 4 (user in front, BGM)", user, TONE_PORT_BGM, 0},
        {"title way 5 (system user, MAIN, 0 dB)", TONE_USER_SYSTEM, TONE_PORT_MAIN, 1},
    };
    const int n = (int)(sizeof(ways) / sizeof(ways[0]));
    for (int i = 0; i < n; ++i) {
        if (ways[i].user < 0) {
            report("%s: no user in front, skipped", ways[i].name);
            continue;
        }
        char line[96];
        snprintf(line, sizeof(line), "Som %d de %d (titulo): %d bipes", i + 1, n, i + 1);
        tone_say(1, line);
        sceKernelUsleep(2000000);
        double ms;
        tone_play(&ways[i], i + 1, 880, tone_say, now_ms, &ms);
        sceKernelUsleep(2000000);
    }
    report("tone test done");
}


/* The sound (audio_ring.h): one port, which plays the rings mixed. It plays
 * silence when no ring has sound, so the port is the clock throughout and
 * consumer_ns says the title is there. */

static audio_ring_t *sound_ring;
static volatile bool sound_quit;

/* The interface's sounds (LINK_SOUND, link.h; docs/07, "Navegação
 * magnética"): made here once, mono, quiet (a tenth of full scale at most),
 * and mixed into the port's blocks. One plays at a time: a new one starts
 * over the last. */
#define UI_SOUND_MAX (AUDIO_RING_RATE / 8) /* 125 ms */

static int16_t ui_sounds[LINK_SOUNDS][UI_SOUND_MAX];
static unsigned ui_sound_frames[LINK_SOUNDS];
static int ui_sound_request; /* 0, or which + 1 | percent << 8 */

static void ui_sounds_make()
{
    const double rate = AUDIO_RING_RATE, pi = 3.14159265358979;
    for (int which = 0; which < LINK_SOUNDS; ++which) {
        /* length (ms), start and end pitch (Hz), loudness, decay (ms) */
        double ms = 24, f0 = 2400, f1 = 2400, amp = 0.07, decay = 5;
        if (which == LINK_SOUND_OPEN) {
            ms = 90, f0 = 520, f1 = 1180, amp = 0.06, decay = 40;
        } else if (which == LINK_SOUND_CLOSE) {
            ms = 80, f0 = 1100, f1 = 480, amp = 0.05, decay = 35;
        } else if (which == LINK_SOUND_SELECT) {
            ms = 40, f0 = 1500, f1 = 1500, amp = 0.08, decay = 10;
        }
        const unsigned n = (unsigned)(rate * ms / 1000);
        double phase = 0;
        for (unsigned i = 0; i < n && i < UI_SOUND_MAX; ++i) {
            const double t = i / rate, k = (double)i / n;
            const double f = f0 + (f1 - f0) * k;
            phase += 2 * pi * f / rate;
            const double attack = t < 0.002 ? t / 0.002 : 1; /* no click at the start */
            const double env = attack * exp(-t * 1000 / decay) * (1 - k * k);
            /* The note and an octave up, softer: a glassy tick. */
            const double s = 0.75 * sin(phase) + 0.25 * sin(2 * phase);
            ui_sounds[which][i] = (int16_t)(32767 * amp * env * s);
        }
        ui_sound_frames[which] = n < UI_SOUND_MAX ? n : UI_SOUND_MAX;
    }
}

static void ui_sound_play(int which, int percent)
{
    if (which < 0 || which >= LINK_SOUNDS)
        return;
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    __atomic_store_n(&ui_sound_request, (which + 1) | percent << 8, __ATOMIC_RELEASE);
}

static audio_ring_t *sound_ring_make()
{
    shm_unlink(AUDIO_RING_SHM); /* a ring left by a title that was closed */
    const int fd = shm_open(AUDIO_RING_SHM, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        report("shm_open %s: %s", AUDIO_RING_SHM, strerror(errno));
        return nullptr;
    }
    void *map = MAP_FAILED;
    if (!ftruncate(fd, sizeof(audio_ring_t)))
        map = mmap(nullptr, sizeof(audio_ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        report("%s of %zu bytes: %s", AUDIO_RING_SHM, sizeof(audio_ring_t), strerror(errno));
        shm_unlink(AUDIO_RING_SHM);
        return nullptr;
    }
    auto *ring = (audio_ring_t *)map;
    memset(ring, 0, sizeof(*ring));
    ring->version = AUDIO_RING_VERSION;
    __atomic_store_n(&ring->magic, AUDIO_RING_MAGIC, __ATOMIC_RELEASE);
    return ring;
}

static void *sound_thread(void *)
{
    static int16_t block[AUDIO_RING_BLOCK * 2];
    static int32_t mix[AUDIO_RING_BLOCK * 2];
    const int32_t port = sceAudioOutOpen(TONE_USER_SYSTEM, TONE_PORT_MAIN, 0, AUDIO_RING_BLOCK, AUDIO_RING_RATE,
                                         TONE_S16_STEREO);
    if (port < 0) {
        report("FAIL sound: sceAudioOutOpen 0x%08x (no sound for the pages)", (unsigned)port);
        return nullptr;
    }
    report("sound: port %d, %u frames a block, rings in %s", port, AUDIO_RING_BLOCK, AUDIO_RING_SHM);
    uint64_t sounding = 0, last_report = 0;
    unsigned playing_was = 0;
    int effect = -1;
    unsigned effect_at = 0, effect_percent = 0;
    while (!sound_quit) {
        memset(mix, 0, sizeof(mix));
        const int request = __atomic_exchange_n(&ui_sound_request, 0, __ATOMIC_ACQ_REL);
        if (request) {
            effect = (request & 0xff) - 1;
            effect_percent = (unsigned)request >> 8;
            effect_at = 0;
        }
        if (effect >= 0) {
            for (unsigned i = 0; i < AUDIO_RING_BLOCK && effect_at < ui_sound_frames[effect]; ++i) {
                const int32_t s = ui_sounds[effect][effect_at++] * (int32_t)effect_percent / 100;
                mix[2 * i] += s;
                mix[2 * i + 1] += s;
            }
            if (effect_at >= ui_sound_frames[effect])
                effect = -1;
        }
        unsigned playing = 0;
        for (unsigned s = 0; s < AUDIO_RING_SLOTS; ++s) {
            audio_slot_t *slot = &sound_ring->slots[s];
            if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != AUDIO_SLOT_PLAYING)
                continue;
            ++playing;
            const uint64_t written = __atomic_load_n(&slot->written, __ATOMIC_ACQUIRE);
            const uint64_t flush_to = __atomic_load_n(&slot->flush_to, __ATOMIC_ACQUIRE);
            uint64_t read = slot->read; /* only this thread moves it */
            if (flush_to > read && flush_to <= written)
                read = flush_to;
            if (written < read || written - read > AUDIO_RING_FRAMES)
                read = written; /* a ring that was set up again */
            const unsigned n = (unsigned)(written - read < AUDIO_RING_BLOCK ? written - read : AUDIO_RING_BLOCK);
            for (unsigned i = 0; i < n; ++i) {
                const unsigned at = (unsigned)((read + i) % AUDIO_RING_FRAMES);
                mix[2 * i] += slot->samples[2 * at];
                mix[2 * i + 1] += slot->samples[2 * at + 1];
            }
            __atomic_store_n(&slot->read, read + n, __ATOMIC_RELEASE);
            sounding += n;
        }
        for (unsigned i = 0; i < AUDIO_RING_BLOCK * 2; ++i)
            block[i] = (int16_t)(mix[i] > 32767 ? 32767 : mix[i] < -32768 ? -32768 : mix[i]);
        capture_sound(block, AUDIO_RING_BLOCK); /* docs/09-video.md: what is played, mixed */
        const int32_t ret = sceAudioOutOutput(port, block); /* blocks 5.3 ms */
        __atomic_store_n(&sound_ring->consumer_ns, audio_ring_now_ns(), __ATOMIC_RELEASE);
        const uint64_t blocks = __atomic_add_fetch(&sound_ring->blocks, 1, __ATOMIC_RELAXED);
        if (ret < 0) {
            report("FAIL sound: sceAudioOutOutput 0x%08x", (unsigned)ret);
            break;
        }
        if (playing != playing_was) {
            report("sound: %u ring(s) playing", playing);
            playing_was = playing;
        }
        /* Every ~10 s of port time: how much of it had sound. */
        if (blocks - last_report >= 10 * AUDIO_RING_RATE / AUDIO_RING_BLOCK) {
            report("sound: %llu frames from the rings in the last %llu blocks",
                   (unsigned long long)sounding, (unsigned long long)(blocks - last_report));
            sounding = 0;
            last_report = blocks;
        }
    }
    __atomic_store_n(&sound_ring->consumer_ns, 0, __ATOMIC_RELEASE);
    sceAudioOutClose(port);
    report("sound: port closed");
    return nullptr;
}

static pthread_t sound;
static bool sound_started;

static void sound_start()
{
    report("sceAudioOutInit: 0x%08x", (unsigned)sceAudioOutInit()); /* done already is fine */
    ui_sounds_make();
    if (!(sound_ring = sound_ring_make()))
        return;
    sound_started = !pthread_create(&sound, nullptr, sound_thread, nullptr);
    if (!sound_started)
        report("FAIL sound: no thread");
}

/* A WebProcess that ended (LINK_ENDED) writes its rings no more: free them
 * for the next one. */
static void sound_forget(int32_t pid)
{
    if (!sound_ring || pid <= 0)
        return;
    for (unsigned s = 0; s < AUDIO_RING_SLOTS; ++s) {
        audio_slot_t *slot = &sound_ring->slots[s];
        if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != AUDIO_SLOT_FREE && slot->pid == pid) {
            __atomic_store_n(&slot->state, AUDIO_SLOT_FREE, __ATOMIC_RELEASE);
            report("sound: ring %u of WebProcess %d freed", s, pid);
        }
    }
}

static void sound_stop()
{
    if (sound_started) {
        sound_quit = true;
        pthread_join(sound, nullptr);
        sound_started = false;
    }
    if (sound_ring) {
        munmap(sound_ring, sizeof(audio_ring_t));
        sound_ring = nullptr;
        shm_unlink(AUDIO_RING_SHM);
    }
}


/* The system's state for this app, in the report whenever it changes, and
 * the system's events. The layout is the one the open PS4 SDKs give
 * SceSystemServiceStatus (events waiting, then flags: system UI over the
 * app, running in the background, ...); the raw bytes go to the report too.
 * With /app0/status there, the title waits STATUS_PROBE_SECONDS for the
 * UIProcess instead of CONNECT_SECONDS, for a person to try the PS button.
 *
 * The pause. Closing the title from the system while the WebProcess draws
 * through its GPU took the console down (2026-11-04, 15:16 and 15:53);
 * closing it with no GPU client did not (16:02). The close itself sends the
 * app nothing, but it always comes after the system's menu or home screen
 * is over the app, which these flags show within 0.1 s (the PS button at
 * 86.9 s: over 1, then in the background 1 at 87.1 s). So the title says
 * "away" (LINK_AWAY) as soon as they show it: the UIProcess ends the
 * WebProcess and says so (LINK_ENDED), and the title lets go of its GPU
 * client; "back" once they have been clear for BACK_STEADY_MS, and the
 * pages load again. Two exceptions, for the flags' "system UI over":
 *   - from the start until the flags are first clear: a title opened by a
 *     payload had it set for 78 s, until the person came in through its
 *     card (16:00);
 *   - while the system's keyboard is open, and KEYBOARD_GRACE_MS after: the
 *     keyboard is system UI too (the PS button then still sends the app to
 *     the background).
 * /app0/away_test there: the title also says "away" by itself, at 40 s and
 * 100 s after the UIProcess came, for 4 s and 15 s, for runs with no person. */

#define STATUS_MARKER "/app0/status"
#define STATUS_PROBE_SECONDS 180
#define AWAY_TEST_MARKER "/app0/away_test"
#define BACK_STEADY_MS 500
#define KEYBOARD_GRACE_MS 1500

extern "C" int sceSystemServiceGetStatus(void *status);
extern "C" int sceSystemServiceReceiveEvent(void *event);

static bool send_msg(const link_msg_t &msg, const char *text = nullptr);

static volatile bool status_quit;
static volatile bool link_closed;
/* 2026-11-14 17:3x (with the title
 * that leaves at the home screen): that leave needs a browser to tell, and for the first seconds there is
 * none yet. Set by the status thread when the home screen comes over a title still waiting for its
 * browser: the wait ends, the title closes what it has and leaves (main()), and the file LEFT_MARKER tells
 * the browser that is starting not to open the title again (remote.c, launch_title()). */
static volatile bool leave_startup;
/* The link ended from the browser's side (it said it is closing, or it is gone): nothing of it to wait for. */
static volatile bool link_by_browser;
/* When the browser was told to quit at the home screen (0: it was not): the exit says how long the leave took. */
static volatile double leave_told_ms;
#define LEFT_MARKER "/app0/left-at-start"

#ifdef WEBKIT_IN_TITLE
/* web_host.h: the WebProcess failed (a crash on one of its threads,
 * WebKit's own crash, an exit with an error); the title closes as when the
 * link closes, its GPU client first. Safe in a signal handler. */
static int web_failed;

extern "C" void title_web_failed(const char *why)
{
    if (__atomic_exchange_n(&web_failed, 1, __ATOMIC_ACQ_REL))
        return;
    char line[256];
    snprintf(line, sizeof(line), "[browser_title] %s: the title closes (its GPU client first)\n", why);
    crash_write(line);
    link_closed = true;
}
#endif
static bool link_up;            /* __atomic: hello went, the UIProcess listens */
static double link_up_ms;       /* written before link_up */
static bool away_test;          /* set before the status thread starts */
static int64_t keyboard_closed; /* __atomic: ms (now_ms) when the keyboard closed */

/* The keyboard's pause (BrowserPS5 RN, 2026-11-11). The system's keyboard opened while the
 * WebProcess drew through the GPU took the console down (2026-11-04 18:19:
 * sceAgcDriverSubmitDcb failed under it, less than 1 s after it opened). So
 * LINK_IME_OPEN only notes the request: the status thread says "away" as
 * for the PS button (the PS5's decoder and its queues go, the UIProcess
 * hides the views and pauses the media), opens the keyboard
 * KEYBOARD_AFTER_AWAY_MS later, and keeps "away" while it is open; "back"
 * comes BACK_STEADY_MS after it closes (the flags' "system UI over" is not
 * counted for KEYBOARD_GRACE_MS, as before). The UIProcess puts the text
 * into the field once the pages are back.
 * /app0/ime_test there: the keyboard closes by itself IME_TEST_OPEN_MS
 * after it opens (sceImeDialogAbort), for runs with no person. */
#define IME_TEST_MARKER "/app0/ime_test"
#define KEYBOARD_AFTER_AWAY_MS 400
#define IME_TEST_OPEN_MS 4000
extern "C" int sceImeDialogAbort(void);
#ifdef TITLE_SYSIME
/* 2026-11-14: through what sysime.c bound (hello_page/ime.c, ime_abort()): the import itself is NULL in a
 * title on 13.60, as the keyboard's other functions were. */
#define sceImeDialogAbort ime_abort
#endif
static int kb_pending;            /* __atomic: asked, opens once the pages paused */
static int32_t kb_kind, kb_enter; /* written before kb_pending */
static char kb_text[LINK_TEXT_MAX + 1];
static bool ime_test;             /* set before the status thread starts */
static void on_keyboard_closed(const char *text, void *);

static bool keyboard_over(double now)
{
    return ime_is_open() || now - (double)__atomic_load_n(&keyboard_closed, __ATOMIC_RELAXED) < KEYBOARD_GRACE_MS;
}

static bool away_test_now(double now)
{
    if (!away_test || !__atomic_load_n(&link_up, __ATOMIC_ACQUIRE))
        return false;
    const double s = (now - link_up_ms) / 1e3;
    // 2026-11-09: the first time 4 s only, under the 10 s after which the
    // hidden page's buffers go (AcceleratedSurface's release timer); coming
    // back before that crashed the WebProcess (a timer stopped off its thread).
    return (s >= 40 && s < 44) || (s >= 100 && s < 115);
}

static void away_send(bool away, bool test, double at_s, int over, int background)
{
    if (away) {
        pthread_mutex_lock(&gpu_lock);
        gpu_away_cookie = gpu_cookie;
        pthread_mutex_unlock(&gpu_lock);
    }
    video_away(away ? 1 : 0);  // 18:5x: the PS5's decoder and its GPU queues go first
    link_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = LINK_AWAY;
    msg.u.away.away = away;
    msg.u.away.test = test;
    feel_away(away);
    const bool sent = send_msg(msg);
    report("%s at %.1f s%s (system UI over the app %d, in the background %d)%s",
           away ? "away: the pages pause" : "back: the pages go on", at_s, test ? " (test)" : "", over, background,
           sent ? "" : "; FAIL the UIProcess could not be told");
    /* 2026-11-14 (a kernel panic on closing the browser from the PS5's menu, and no report of
     * it left: its last lines were still in memory): what the title was doing when the system came over
     * it is on the disk at once. Twice a pause, on this thread: never in the way of a picture. */
    if (report_fd >= 0)
        fsync(report_fd);
}

/* The title's state for a UIProcess about to open it (link.h,
 * LINK_TITLE_SHM): set before the status thread starts, which beats. */
static link_title_t *title_state;

static void title_state_beat()
{
    if (title_state) {
        __atomic_store_n(&title_state->capture_done, capture_done(), __ATOMIC_RELEASE);
        __atomic_store_n(&title_state->capture_busy, (uint32_t)capture_busy(), __ATOMIC_RELEASE);
        /* 2026-11-14: once a second, how much direct memory is left in one piece (link.h). */
        static double asked_ms;
        const double beat_ms = now_ms();
        if (beat_ms - asked_ms >= 1000) {
            asked_ms = beat_ms;
            int64_t at = 0;
            size_t largest = 0;
            if (sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 0x4000, &at, &largest))
                largest = 0;
            const uint32_t mib = (uint32_t)(largest >> 20);
            __atomic_store_n(&title_state->direct_free_mib, mib ? mib : 1, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&title_state->heartbeat_ms, (uint64_t)now_ms(), __ATOMIC_RELEASE);
    }
}

/* The reader thread sets LINK_TITLE_READY while main may be closing the
 * state (the UIProcess gone): both under this lock. */
static pthread_mutex_t title_state_lock = PTHREAD_MUTEX_INITIALIZER;

static void title_state_set(uint32_t state)
{
    pthread_mutex_lock(&title_state_lock);
    if (title_state) {
        __atomic_store_n(&title_state->state, state, __ATOMIC_RELEASE);
        title_state_beat();
    }
    pthread_mutex_unlock(&title_state_lock);
}

static void title_state_open()
{
    shm_unlink(LINK_TITLE_SHM); /* left by a title that was killed */
    const int fd = shm_open(LINK_TITLE_SHM, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        report("shm_open(%s): %s", LINK_TITLE_SHM, strerror(errno));
        return;
    }
    void *memory = MAP_FAILED;
    if (!ftruncate(fd, LINK_TITLE_BYTES))
        memory = mmap(nullptr, LINK_TITLE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (memory == MAP_FAILED) {
        report("%s: %s", LINK_TITLE_SHM, strerror(errno));
        shm_unlink(LINK_TITLE_SHM);
        return;
    }
    title_state = (link_title_t *)memory;
    title_state->pid = getpid();
    title_state_set(LINK_TITLE_WAITING);
    __atomic_store_n(&title_state->magic, LINK_MAGIC, __ATOMIC_RELEASE);
}

static void title_state_close()
{
    pthread_mutex_lock(&title_state_lock);
    if (title_state) {
        shm_unlink(LINK_TITLE_SHM);
        munmap(title_state, LINK_TITLE_BYTES);
        title_state = nullptr;
    }
    pthread_mutex_unlock(&title_state_lock);
}

static void *status_thread(void *)
{
    static unsigned char now[512], was[512];
    static unsigned char event[16384];
    bool first = true;
    int last_err = 0;
    bool seen_front = false; /* the flags were clear once */
    bool told_away = false;  /* what the UIProcess was told last */
    double away_told = 0;    /* when it was told "away" */
#ifdef TITLE_RN
    /* 2026-11-14: the home screen over the app for LEAVE_AFTER_MS: the browser is told to quit (link.h,
     * leave), once. Not with the file stay-in-background in the title's folder. */
    #define LEAVE_AFTER_MS 400   /* (1000 at first: a quick close from the tile could still come before) */
    #define LEAVE_GRACE_MS 2500  /* a browser told to quit that is still there after this: the title goes */
    double background_since = -1, leave_told_at = 0;
    bool leave_told = false;
    const bool stays = !access("/app0/stay-in-background", F_OK);
#endif
    double ime_abort_at = 0; /* the keyboard test: when it closes it */
    double clear_since = -1;
    const double start = now_ms();
    while (!status_quit) {
        title_state_beat();
        memset(now, 0, sizeof(now));
        const int err = sceSystemServiceGetStatus(now);
        if (err) {
            if (err != last_err)
                report("system status: sceSystemServiceGetStatus 0x%08x (no pause)", (unsigned)err);
            last_err = err;
            sceKernelUsleep(1000000);
            continue;
        }
        if (first || memcmp(now, was, 16)) {
            int32_t events;
            memcpy(&events, now, sizeof(events));
            report("system status at %.1f s: %d event(s), system UI over the app %d, in the background %d; "
                   "bytes %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x",
                   (now_ms() - start) / 1e3, events, now[4], now[5], now[0], now[1], now[2], now[3], now[4], now[5],
                   now[6], now[7], now[8], now[9], now[10], now[11], now[12], now[13], now[14], now[15]);
            memcpy(was, now, sizeof(was));
            first = false;
        }

        const double t = now_ms();
        const bool over = now[4] != 0, background = now[5] != 0;
        if (!over && !background)
            seen_front = true;
        const bool system_away = background || (over && seen_front && !keyboard_over(t));
        const bool test = away_test_now(t);
        /* The keyboard's pause: asked, or open. */
        const bool keyboard_hold = __atomic_load_n(&kb_pending, __ATOMIC_ACQUIRE) || ime_is_open();
        const bool away = system_away || test || keyboard_hold;
        if (away)
            clear_since = -1;
        else if (clear_since < 0)
            clear_since = t;
#ifdef TITLE_RN
        if (background && seen_front) {
            if (background_since < 0)
                background_since = t;
        } else {
            background_since = -1;
        }
        if (!leave_told && !stays && told_away && background_since >= 0 && t - background_since >= LEAVE_AFTER_MS
            && __atomic_load_n(&link_up, __ATOMIC_ACQUIRE) && !link_closed) {
            leave_told = true;
            leave_told_at = t;
            leave_told_ms = t;
            link_msg_t leave;
            memset(&leave, 0, sizeof(leave));
            leave.type = LINK_AWAY;
            leave.u.away.away = 1;
            leave.u.away.leave = 1;
            const bool sent = send_msg(leave);
            report("the home screen has been over the app for %.0f ms: the browser is told to quit%s", t - background_since,
                   sent ? "" : "; FAIL it could not be told");
            if (report_fd >= 0)
                fsync(report_fd);
        }
        if (leave_told && !link_closed && t - leave_told_at >= LEAVE_GRACE_MS) {
            report("the browser was told to quit %.0f ms ago and is still there: the title closes by itself", t - leave_told_at);
            if (report_fd >= 0)
                fsync(report_fd);
            link_closed = true;
        }
        /* No browser yet (the title is still starting it): the wait for it ends, main() leaves. */
        if (!leave_startup && !stays && background_since >= 0 && t - background_since >= LEAVE_AFTER_MS
            && !__atomic_load_n(&link_up, __ATOMIC_ACQUIRE) && !link_closed) {
            leave_startup = true;
            report("the home screen has been over the app for %.0f ms, with no browser yet: the title leaves",
                   t - background_since);
            if (report_fd >= 0)
                fsync(report_fd);
        }
#endif
        if (__atomic_load_n(&link_up, __ATOMIC_ACQUIRE) && !link_closed) {
            if (away && !told_away) {
                away_send(true, !system_away, (t - start) / 1e3, now[4], now[5]);
                told_away = true;
                away_told = t;
            } else if (!away && told_away && t - clear_since >= BACK_STEADY_MS) {
                away_send(false, false, (t - start) / 1e3, now[4], now[5]);
                told_away = false;
            }
        }
        if (__atomic_load_n(&kb_pending, __ATOMIC_ACQUIRE)) {
            if (system_away || link_closed || !__atomic_load_n(&link_up, __ATOMIC_ACQUIRE)) {
                __atomic_store_n(&kb_pending, 0, __ATOMIC_RELEASE);
                report("the system keyboard: not opened (the system is over the app, or no UIProcess)");
                on_keyboard_closed(nullptr, nullptr);
            } else if (told_away && t - away_told >= KEYBOARD_AFTER_AWAY_MS) {
                const int err = ime_open(input_user(), (ime_kind_t)kb_kind, (ime_enter_t)kb_enter, kb_text,
                                         on_keyboard_closed, nullptr);
                __atomic_store_n(&kb_pending, 0, __ATOMIC_RELEASE);
                report("the system keyboard opens %.0f ms after the pause: 0x%x%s", t - away_told, (unsigned)err,
                       ime_test && !err ? " (test: it closes by itself)" : "");
                if (err)
                    on_keyboard_closed(nullptr, nullptr);
                else if (ime_test)
                    ime_abort_at = now_ms() + IME_TEST_OPEN_MS;
            }
        }
        if (ime_abort_at > 0 && now_ms() >= ime_abort_at) {
            ime_abort_at = 0;
            if (ime_is_open())
                report("keyboard test: sceImeDialogAbort = 0x%x", (unsigned)sceImeDialogAbort());
        }
        int32_t events;
        memcpy(&events, now, sizeof(events));
        for (int i = 0; i < events && i < 8; ++i) {
            memset(event, 0, sizeof(event));
            const int got = sceSystemServiceReceiveEvent(event);
            int32_t type;
            memcpy(&type, event, sizeof(type));
            report("system event at %.1f s: type 0x%x (0x%08x), data %02x %02x %02x %02x %02x %02x %02x %02x",
                   (now_ms() - start) / 1e3, (unsigned)type, (unsigned)got, event[4], event[5], event[6], event[7],
                   event[8], event[9], event[10], event[11]);
            if (got)
                break;
        }
        sceKernelUsleep(100000);
    }
    return nullptr;
}


/* The link to the UIProcess. */

static int link_sock = -1;
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;

static bool send_all(const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size) {
        const ssize_t n = send(link_sock, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

static bool recv_all(void *data, size_t size)
{
    char *p = (char *)data;
    while (size) {
        const ssize_t n = recv(link_sock, p, size, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

static bool send_msg(const link_msg_t &msg, const char *text)
{
    pthread_mutex_lock(&send_lock);
    bool ok = send_all(&msg, sizeof(msg));
    if (ok && msg.text_bytes)
        ok = send_all(text, msg.text_bytes);
    pthread_mutex_unlock(&send_lock);
    return ok;
}

static bool connect_ui(int seconds)
{
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(LINK_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* At least one try (seconds 0: just that one). */
    const double give_up = now_ms() + seconds * 1e3;
    for (;;) {
        const int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0 && !connect(sock, (struct sockaddr *)&address, sizeof(address))) {
            const int yes = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
            link_sock = sock;
            return true;
        }
        if (sock >= 0)
            close(sock);
        if (now_ms() >= give_up || leave_startup)
            return false;
        sceKernelUsleep(leave_startup ? 0 : 50000);  /* (was 250 ms: the leave is seen sooner) */
    }
}

/* Opened from the home screen (2026-11-04): no UIProcess comes, so the
 * title sends the browser (the launcher payload, browser_gpu.elf built for
 * this, installed as /app0/browser.elf) to elfldr on 127.0.0.1, as the PC
 * does with send.ps1. elfldr starts it, so the title still starts no
 * process itself. The payload's output comes back on the same connection:
 * a thread writes it to BROWSER_LOG, and the connection stays open while
 * the title runs (the payload writes its log to it). */
#define BROWSER_ELF "/app0/browser.elf"
#define BROWSER_LOG "/app0/browser-run.txt"
#define ELFLDR_PORT 9021

#ifdef TITLE_RN
/* 2026-11-18: a report came with this title's log only, and what it needed was in the browser's own
 * (BROWSER_LOG): the interface had ended two seconds after it connected, and why it did was written there. So
 * when the browser's output ends early (under BROWSER_EARLY bytes: a session writes far more than that), its
 * last lines are written here too, each cut at the first web address it has. */
#define BROWSER_EARLY (64 * 1024)
#define BROWSER_TAIL 1800

static void browser_tail_report(char *tail, size_t len, bool whole)
{
    char *line = tail;
    char *const end = tail + len;
    if (!whole) {  /* the first line may be the end of one: left out */
        while (line < end && *line != '\n')
            line++;
        line += line < end;
    }
    while (line < end) {
        char *stop = line;
        while (stop < end && *stop != '\n')
            stop++;
        *stop = 0;
        char *address = strstr(line, "http://");
        char *secure = strstr(line, "https://");
        if (secure && (!address || secure < address))
            address = secure;
        if (address)
            *address = 0;
        if (*line)
            report("the browser's last lines: %.220s%s", line, address ? "<address>" : "");
        line = stop + 1;
    }
}
#endif

static void *browser_output_thread(void *arg)
{
    const int sock = (int)(intptr_t)arg;
    FILE *log = fopen(BROWSER_LOG, "w");
    static char buffer[16384];
    ssize_t n;
    size_t total = 0;
#ifdef TITLE_RN
    static char tail[BROWSER_TAIL + 1];
    size_t kept = 0;
#endif
    while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        total += (size_t)n;
        if (log) {
            fwrite(buffer, 1, (size_t)n, log);
            fflush(log);
        }
#ifdef TITLE_RN
        if (total <= BROWSER_EARLY) {  /* the end of what came so far */
            const size_t got = (size_t)n;
            if (got >= BROWSER_TAIL) {
                memcpy(tail, buffer + got - BROWSER_TAIL, BROWSER_TAIL);
                kept = BROWSER_TAIL;
            } else {
                if (kept + got > BROWSER_TAIL) {
                    const size_t drop = kept + got - BROWSER_TAIL;
                    memmove(tail, tail + drop, kept - drop);
                    kept -= drop;
                }
                memcpy(tail + kept, buffer, got);
                kept += got;
            }
        }
#endif
    }
    report("the browser's output ended after %zu bytes (%s)", total, n < 0 ? strerror(errno) : "closed");
#ifdef TITLE_RN
    if (total > 0 && total < BROWSER_EARLY)
        browser_tail_report(tail, kept, total <= BROWSER_TAIL);
#endif
    if (log)
        fclose(log);
    close(sock);
    return nullptr;
}

static bool start_browser()
{
    const int fd = open(BROWSER_ELF, O_RDONLY);
    if (fd < 0) {
        report("FAIL no %s (%s): send the browser from the PC", BROWSER_ELF, strerror(errno));
        return false;
    }
    struct stat st;
    char *elf = nullptr;
    size_t size = 0;
    if (!fstat(fd, &st) && st.st_size > 0 && (elf = (char *)malloc((size_t)st.st_size))) {
        while (size < (size_t)st.st_size) {
            const ssize_t n = read(fd, elf + size, (size_t)st.st_size - size);
            if (n <= 0)
                break;
            size += (size_t)n;
        }
    }
    close(fd);
    if (!elf || size != (size_t)st.st_size) {
        report("FAIL reading %s: %zu bytes", BROWSER_ELF, size);
        free(elf);
        return false;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(ELFLDR_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0 || connect(sock, (struct sockaddr *)&address, sizeof(address))) {
        report("FAIL elfldr does not answer on 127.0.0.1:%d (%s)", ELFLDR_PORT, strerror(errno));
        if (sock >= 0)
            close(sock);
        free(elf);
        return false;
    }
    const int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    size_t sent = 0;
    while (sent < size) {
        const ssize_t n = send(sock, elf + sent, size - sent, 0);
        if (n <= 0)
            break;
        sent += (size_t)n;
    }
    free(elf);
    if (sent != size) {
        report("FAIL sending %s to elfldr: %zu of %zu bytes (%s)", BROWSER_ELF, sent, size, strerror(errno));
        close(sock);
        return false;
    }
    report("sent %s (%zu bytes) to elfldr; its output goes to %s", BROWSER_ELF, size, BROWSER_LOG);
    pthread_t thread;
    if (!pthread_create(&thread, nullptr, browser_output_thread, (void *)(intptr_t)sock))
        pthread_detach(thread);
    else
        close(sock);
    return true;
}

static link_view_t *map_view()
{
    for (unsigned tries = 0; tries < 100; ++tries) {
        const int fd = shm_open(LINK_VIEW_SHM, O_RDWR, 0);
        if (fd >= 0) {
            void *view = mmap(nullptr, LINK_VIEW_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close(fd);
            if (view != MAP_FAILED) {
                auto *v = (link_view_t *)view;
                if (link_view_ready(v))
                    return v;
                munmap(view, LINK_VIEW_BYTES);
            }
        }
        sceKernelUsleep(100000);
    }
    return nullptr;
}

static void on_input(const input_event_t *event)
{
    link_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = LINK_INPUT;
    msg.u.input.kind = event->kind;
    msg.u.input.x = event->x;
    msg.u.input.y = event->y;
    msg.u.input.dx = event->dx;
    msg.u.input.dy = event->dy;
    msg.u.input.pressed = event->pressed;
    send_msg(msg);
}

static void on_keyboard_closed(const char *text, void *)
{
    __atomic_store_n(&keyboard_closed, (int64_t)now_ms(), __ATOMIC_RELAXED);
    link_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = LINK_IME_DONE;
    msg.u.ime_done.ok = text != nullptr;
    msg.text_bytes = text ? (uint32_t)strnlen(text, LINK_TEXT_MAX) : 0;
    send_msg(msg, text);
}

static void *link_thread(void *)
{
    static char text[LINK_TEXT_MAX + 1];
    link_msg_t msg;
    while (recv_all(&msg, sizeof(msg))) {
        if (msg.text_bytes > LINK_TEXT_MAX || (msg.text_bytes && !recv_all(text, msg.text_bytes)))
            break;
        text[msg.text_bytes] = 0;
        if (msg.type == LINK_IME_OPEN) {
            /* The keyboard's pause: the status thread opens it. */
            if (__atomic_load_n(&kb_pending, __ATOMIC_ACQUIRE) || ime_is_open()) {
                report("the system keyboard was asked again while asked or open: not taken");
            } else {
                kb_kind = msg.u.ime_open.kind;
                kb_enter = msg.u.ime_open.enter;
                memcpy(kb_text, text, (size_t)msg.text_bytes + 1);
                __atomic_store_n(&kb_pending, 1, __ATOMIC_RELEASE);
                report("the system keyboard was asked: the pages pause first");
            }
        } else if (msg.type == LINK_ENDED) {
            const bool for_quit = msg.u.ended.for_away == LINK_ENDED_QUIT;
            report("WebProcess %d ended%s", msg.u.ended.pid,
                   for_quit ? " for the quit" : msg.u.ended.for_away ? " for the pause" : "");
            sound_forget(msg.u.ended.pid);
            video_writer_ended(msg.u.ended.pid);
            if (for_quit) {
                /* The UIProcess saw the process gone (the launcher reaps it
                 * before it answers the kill): let go of its client whether
                 * or not the title was away, keep showing the screen, and
                 * say the system may close the title now (link.h,
                 * LINK_TITLE_READY). */
                pthread_mutex_lock(&gpu_lock);
                const int cookie = gpu_cookie;
                gpu_cookie = 0;
                gpu_away_cookie = 0;
                pthread_mutex_unlock(&gpu_lock);
                if (cookie) {
                    sceKernelUsleep(250000);
                    gpu_close(cookie, "its WebProcess ended for the quit");
                } else {
                    report("no GPU client to let go of");
                }
                title_state_set(LINK_TITLE_READY);
                report("quit: no GPU client, the screen stays; the UIProcess may have the system close the title");
                if (report_file)
                    fflush(report_file);
            } else if (msg.u.ended.for_away) {
                pthread_mutex_lock(&gpu_lock);
                int cookie = 0;
                if (gpu_away_cookie && gpu_cookie == gpu_away_cookie) {
                    cookie = gpu_cookie;
                    gpu_cookie = 0;
                }
                gpu_away_cookie = 0;
                pthread_mutex_unlock(&gpu_lock);
                if (cookie) {
                    /* The launcher's SIGKILL is on its way: give the process
                     * a moment to be gone before its memory is freed. */
                    sceKernelUsleep(250000);
                    gpu_close(cookie, "its WebProcess ended for the pause");
                } else {
                    report("no GPU client to let go of");
                }
            }
        } else if (msg.type == LINK_QUIT) {
            report("the browser is closing");
            break;
        } else if (msg.type == LINK_CURSOR) {
            input_set_cursor(msg.u.cursor.x, msg.u.cursor.y);
        } else if (msg.type == LINK_CONFIG) {
            report("stick speeds: cursor %d%%, scrolling %d%%", msg.u.config.cursor_percent,
                   msg.u.config.scroll_percent);
            input_set_speed(msg.u.config.cursor_percent, msg.u.config.scroll_percent);
        } else if (msg.type == LINK_SOUND) {
            ui_sound_play(msg.u.sound.which, msg.u.sound.percent);
        } else if (msg.type == LINK_FEEL) {
            feel_vibrate(msg.u.feel.large, msg.u.feel.small, msg.u.feel.ms);
        } else if (msg.type == LINK_TRIGGERS) {
            report("triggers: %s, strength %d", msg.u.triggers.mode == LINK_TRIGGERS_CLICK ? "click" : "off",
                   msg.u.triggers.strength);
            feel_triggers(msg.u.triggers.mode == LINK_TRIGGERS_CLICK ? FEEL_TRIGGERS_CLICK : FEEL_TRIGGERS_OFF,
                          msg.u.triggers.strength);
        } else if (msg.type == LINK_LIGHT) {
            feel_light(msg.u.light.on, msg.u.light.r, msg.u.light.g, msg.u.light.b);
        } else if (msg.type == LINK_GAMEPAD) {
            report("game mode %s", msg.u.gamepad.on ? "on: the controller goes to the page" : "off");
            input_set_raw(msg.u.gamepad.on);
        } else if (msg.type == LINK_CAPTURE) {
            /* The demo's pieces (docs/09-video.md). */
            if (msg.u.capture.on)
                capture_begin(text, (unsigned)msg.u.capture.seconds);
            else
                capture_end();
        }
    }
    link_by_browser = true;
    link_closed = true;
    return nullptr;
}


/* 2026-11-10: the user called v1 "too unstable", but the logs held only the
 * last session, a good one: each start overwrote the session that had gone
 * wrong. So each start first moves the three logs one place back:
 * browser-title.txt -> browser-title.1.txt -> .2 -> .3 (the oldest goes),
 * and the same for browser-run.txt and web-stderr.txt. */
#define KEEP_LOGS 3
static void keep_old_logs()
{
    static const char *const names[] = {"/app0/browser-title", "/app0/browser-run", "/app0/web-stderr"};
    char from[64], to[64];
    for (const char *base : names) {
        for (int i = KEEP_LOGS - 1; i >= 0; --i) {
            if (i == 0)
                snprintf(from, sizeof from, "%s.txt", base);
            else
                snprintf(from, sizeof from, "%s.%d.txt", base, i);
            snprintf(to, sizeof to, "%s.%d.txt", base, i + 1);
            rename(from, to);  // a missing file is fine
        }
    }
}


#ifdef TITLE_RN
/* 2026-11-17: the firmware in the log's first lines: a log sent from another console says which it is. */
struct sw_version {
    uint64_t size;
    char text[0x1c];
    uint32_t version;  // 0xMMmm....: "MM.mm" in hexadecimal digits
};
extern "C" int sceKernelGetProsperoSystemSwVersion(struct sw_version *version);

static const char *firmware_text()
{
    static char text[40];
    struct sw_version v;
    if (text[0])
        return text;
    memset(&v, 0, sizeof(v));
    v.size = sizeof(v);
    if (sceKernelGetProsperoSystemSwVersion(&v))
        snprintf(text, sizeof(text), "unknown");
    else if (v.version >> 24)
        snprintf(text, sizeof(text), "%x.%02x", v.version >> 24, (v.version >> 16) & 0xff);
    else {
        v.text[sizeof(v.text) - 1] = 0;
        const char *t = v.text;
        while (*t == ' ')
            t++;
        snprintf(text, sizeof(text), "%s", t[0] ? t : "unknown");
    }
    return text;
}

#endif

#ifdef TITLE_RN
/* A start that fails (no screen, no browser in time, no view; 2026-11-18) leaves as the normal end does,
 * with sceSystemServiceLoadExec("exit"). Returning from main() ran exit(), and on consoles that reported it
 * a destructor crashed there (mspace_free from pixman_destructor, 2026-11-17): the system showed its error
 * for the game, and the reason for the failed start stayed only in this report. The caller has closed
 * what it opened first, as the end of main() does. */
static void leave_after_failed_start(const char *why)
{
    exiting = 1;
    report("exit: %s: sceSystemServiceLoadExec(\"exit\") (not exit() and its destructors)", why);
    if (report_file)
        fflush(report_file);
    if (report_fd >= 0)
        fsync(report_fd);
    const double asked = now_ms();
    sceSystemServiceLoadExec("exit", nullptr);
    while (now_ms() - asked < LOADEXEC_WAIT_MS)
        sceKernelUsleep(100000);
    _exit(0);
}

/* Whether pipe() gives a pair of sockets here (2026-11-18, build.sh TITLE_PIPES: --wrap=pipe and
 * --wrap=pipe2 on libc-fixes' wrappers, for GLib's and GStreamer's pipes in the WebProcess). A pipe's
 * buffer comes from a pool all the console's processes share; when it is used up, a write to a pipe fails
 * and the thread waiting on the other end never wakes. */
static void pipes_note()
{
    int fds[2];
    if (pipe(fds)) {
        report("pipes: pipe() failed (%s)", strerror(errno));
        return;
    }
    struct stat st;
    const bool sockets = !fstat(fds[0], &st) && S_ISSOCK(st.st_mode);
    report("pipes: %s", sockets ? "pairs of sockets (not the system's pool of pipes)" : "the system's pipes");
    close(fds[0]);
    close(fds[1]);
}
#endif

int main()
{
    keep_old_logs();
    report_file = fopen(REPORT_PATH, "w");
    /* 2026-11-09 19:4x: each line on the disk at once: the console went down
     * mid-video and the file was empty (the lines were all in the buffer). */
    if (report_file)
        setvbuf(report_file, nullptr, _IOLBF, 0);
    if (report_file) {
        setvbuf(report_file, nullptr, _IOLBF, 0);
        report_fd = fileno(report_file);
    }
    direct_budget_note("at the start");
    crash_hooks();
    report("browser title pid %d", getpid());
#ifdef TITLE_RN
    report("firmware %s", firmware_text());
    pipes_note();
#endif
    flexible_note("the title's start");

    ps5_gpu_broker_set_shared_memory(named_alloc, nullptr);
    if (ps5_gpu_broker_start("/app0", false, true, absent)) {
        pthread_t broker;
        pthread_create(&broker, nullptr, broker_thread, nullptr);
        report("broker started");
    } else {
        /* The browser still works: WebKit draws on the CPU. */
        report("ps5_gpu_broker_start failed (its log: /app0/gpu-broker-results.txt); no GPU for payloads");
    }
    flexible_note("the GPU broker (RADV)");

    int err = display_open();
    if (err) {
        report("FAIL display_open: 0x%x", err);
#ifdef TITLE_RN
        leave_after_failed_start("no screen");
#endif
        return 1;
    }
    flexible_note("the display");
    const bool splash_on = splash_begin();
    report("starting screen: %s", splash_on ? "the neon comet" : "none (no memory or no font)");
    splash_target = 0.15;
    tone_test();
    sound_start();
    flexible_note("the sound");
    /* The pages' video, decoded here (docs/08-4k.md, D3): before the
     * browser starts, so its WebProcesses see the ring and hand the video
     * over (crash-report.c). /app0/video.txt can set threads=, slice=, max=. */
    const bool video_on = !video_start("/app0/video.txt");
    report(video_on ? "video: the title decodes the pages' video (ring %s)"
                    : "video: no ring (%s), the pages decode their video", "/bps5video");
    flexible_note("the video");
    splash_target = 0.3;
#ifdef WEBKIT_IN_TITLE
    /* 2c-2: the WebProcess runs here when the UIProcess asks (web_host.cpp),
     * after the video's ring is there (its setup looks for it). */
    web_host_start();
    flexible_note("the WebProcess's host");
#endif
    int wait_seconds = CONNECT_SECONDS;
    bool status_probe = false;
    if (!access(STATUS_MARKER, F_OK)) {
        unlink(STATUS_MARKER);
        wait_seconds = STATUS_PROBE_SECONDS;
        status_probe = true;
        report("status probe: waiting %d s for the UIProcess", wait_seconds);
    }
    if (!access(IME_TEST_MARKER, F_OK)) {
        unlink(IME_TEST_MARKER);
        ime_test = true;
        report("keyboard test: the system keyboard closes by itself %d ms after it opens", IME_TEST_OPEN_MS);
    }
    if (!access(AWAY_TEST_MARKER, F_OK)) {
        unlink(AWAY_TEST_MARKER);
        away_test = true;
        report("pause test: away at 40 s for 4 s and at 100 s for 15 s after the UIProcess comes");
    }
    title_state_open();
#ifdef TITLE_SYSIME
    /* 2026-11-14: the system keyboard's library (sysime.c). Its first runs on the console (16:42, 16:44,
     * 16:47, with a marker file then) loaded and bound it each time and opened the keyboard twice, so
     * every start loads it now; the file no-sysime in the title's folder leaves it out. */
    if (access("/app0/no-sysime", F_OK)) {
        const int none = ime_load();
        report("the system keyboard: %s", none ? "not there: the browser's own stays" : "ready");
        if (title_state)
            __atomic_store_n(&title_state->ime_ready, none ? 0u : 1u, __ATOMIC_RELEASE);
    } else {
        report("the system keyboard: its library is not loaded (the no-sysime marker)");
    }
#endif
#ifdef TITLE_MIC
    /* 2026-11-15: the microphone's proof for the voice search (mic.c): once, when the file mic-probe is in the
     * title's folder (it goes as it is seen). */
    if (!access("/app0/mic-probe", F_OK)) {
        int mic_user = -1;
        unlink("/app0/mic-probe");
        report("mic: the proof is asked for (sceUserServiceGetForegroundUser: 0x%08x)",
               (unsigned)sceUserServiceGetForegroundUser(&mic_user));
        mic_probe(mic_user);
    }
#endif
#ifdef TITLE_SYSIME
#endif
    pthread_t status;
    const bool status_started = !pthread_create(&status, nullptr, status_thread, nullptr);
    /* A UIProcess that opened the title listens before it opens it
     * (remote.c), so the first try connects; refused: opened from the home
     * screen, and the browser starts at once (the menu start took ~2 s
     * longer with 2 s of tries first, 2026-11-04 22:13). */
    bool connected = connect_ui(status_probe ? wait_seconds : 0);
    if (!connected && !status_probe) {
        report("no UIProcess listening: opened from the home screen, starting the browser");
        splash_target = 0.45;
        if (start_browser()) {
            splash_target = 0.6;
            connected = connect_ui(wait_seconds);
        }
    }
    splash_pause();
    if (!connected) {
        splash_close();
        if (leave_startup) {
            const int marker = open(LEFT_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (marker >= 0)
                close(marker);
            report("leaving before the browser came: %s %s for the browser that is starting", LEFT_MARKER,
                   marker >= 0 ? "is there" : "could NOT be made");
        } else {
            report("FAIL the UIProcess did not answer on 127.0.0.1:%d in %d s", LINK_PORT, wait_seconds);
        }
        status_quit = true;
        if (status_started)
            pthread_join(status, nullptr);
        title_state_close();
        if (video_on)
            video_stop();
        sound_stop();
        display_close();
        if (leave_startup) {  /* the clean way out, as at the end of main() */
            exiting = 1;
            report("exit: everything is closed: sceSystemServiceLoadExec(\"exit\") (left at the start)");
            if (report_file)
                fflush(report_file);
            if (report_fd >= 0)
                fsync(report_fd);
            const double asked = now_ms();
            sceSystemServiceLoadExec("exit", nullptr);
            while (now_ms() - asked < LOADEXEC_WAIT_MS)
                sceKernelUsleep(100000);
            _exit(0);
        }
#ifdef TITLE_RN
        leave_after_failed_start("the browser did not come");
#endif
        return 1;
    }
    title_state_set(LINK_TITLE_CONNECTED);
    report("connected to the UIProcess");
    splash_target = 0.85;
    flexible_note("the UIProcess's connection");
    link_view_t *view = map_view();
    if (!view) {
        report("FAIL no view in %s", LINK_VIEW_SHM);
        status_quit = true;
        if (status_started)
            pthread_join(status, nullptr);
        title_state_close();
        if (video_on)
            video_stop();
        sound_stop();
        display_close();
#ifdef TITLE_RN
        leave_after_failed_start("no view");
#endif
        return 1;
    }

    if ((err = input_start(on_input, LINK_WIDTH, LINK_HEIGHT)))
        report("input_start: 0x%x (no controller)", err);
    else
        feel_start(input_handle());
#ifdef TITLE_KBM
    /* 2026-11-18: a USB or Bluetooth mouse and keyboard (kbm.h), with the controller's cursor (input.c). */
    if (!err && kbm_start(on_input, LINK_WIDTH, LINK_HEIGHT))
        report("kbm: no mouse or keyboard library (the log's kbm lines say why)");
#endif
    flexible_note("the screen's view and the controller");
    link_msg_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.type = LINK_HELLO;
    hello.u.hello.user_id = input_user();
    send_msg(hello);
    link_up_ms = now_ms();
    __atomic_store_n(&link_up, true, __ATOMIC_RELEASE);

    capture_set_report(report_line);
    pthread_t reader;
    pthread_create(&reader, nullptr, link_thread, nullptr);

    /* The UIProcess writes 0 and 1 is in the middle (remote.c): this side
     * starts with 2. */
    unsigned front = 2;
    uint64_t shown = 0, taken = 0;
    bool splash_done = false;
    double tile_ms = 0, wait_ms = 0, last = now_ms();
    uint64_t last_taken = 0;
    /* The idle frames left out (below): the front buffer last flipped as it is (~0u: none, or one with
     * video or the starting screen in it). */
    uint64_t flipped = 0, skipped = 0, last_flipped = 0, last_skipped = 0;
    unsigned plain_front = ~0u;
    const bool always_flip = !access("/app0/always-flip", F_OK);
    report("screen: %s", always_flip ? "every frame flipped (/app0/always-flip)"
                                     : "a frame with nothing new is not flipped");
    while (!link_closed) {
        int fresh;
        front = link_take(view, front, &fresh);
        taken += fresh;
        /* The video under the page, where it is transparent. */
        const uint32_t *screen = link_buffer(view, front);
        if (splash_on && !splash_done) {
            /* The starting screen until the UIProcess's first picture (or
             * 20 s: a browser that draws nothing is seen as it is). */
            if (taken || now_ms() - splash_begun > 20000) {
                splash_done = true;
                splash_close();
                report("starting screen: off at %.2f s (%s)", (now_ms() - splash_begun) / 1e3,
                       taken ? "the browser's first picture" : "no picture from the browser in 20 s");
            } else if (const uint32_t *frame = splash_next()) {
                screen = frame;
            }
        }
        if (video_on && screen == link_buffer(view, front))  // (not under the starting screen)
            screen = video_compose(screen, LINK_WIDTH, LINK_HEIGHT, LINK_WIDTH);
        /* 2026-11-18 (the browser was reported as power-hungry; poc/power_probe: the GPU's rails took 4.4 W on
         * the home screen and 7.9 W with the browser on a page that stood still, while the title copied and
         * flipped the screen 60 times a second with no new frame): when nothing on the screen can have changed
         * (no new frame from the browser, no video under the page, the starting screen over, no capture), the
         * copy and the flip are left out and the loop sleeps about a frame. The browser still gets its VSYNC.
         * The file /app0/always-flip keeps the flip of every frame, as before. */
        const bool plain = screen == link_buffer(view, front);
        if (!fresh && plain && plain_front == front && !(splash_on && !splash_done) && !always_flip &&
            !capture_running()) {
            ++skipped;
            sceKernelUsleep(16000);
        } else {
            if ((err = display_show(screen, &tile_ms, &wait_ms))) {
                report("display_show: 0x%x", err);
                sceKernelUsleep(16000);
            }
            ++flipped;
            plain_front = plain && !err ? front : ~0u;
            /* The video of the browser (docs/09-video.md): what was just shown,
             * copied (~1 ms) while a capture runs. */
            capture_frame(screen, LINK_WIDTH, LINK_HEIGHT, LINK_WIDTH);
        }
        ++shown;
        link_msg_t vsync;
        memset(&vsync, 0, sizeof(vsync));
        vsync.type = LINK_VSYNC;
        vsync.u.vsync.frames = shown;
        if (!send_msg(vsync))
            break;

        const double now = now_ms();
        if (now - last >= 5000) {
            const uint64_t n = flipped - last_flipped;
            report("screen: %llu flips, %llu new frames in %.1f s; copy %.2f ms, wait %.2f ms per flip; "
                   "%llu frames with nothing new not flipped",
                   (unsigned long long)n, (unsigned long long)(taken - last_taken), (now - last) / 1e3,
                   n ? tile_ms / n : 0.0, n ? wait_ms / n : 0.0, (unsigned long long)(skipped - last_skipped));
            last_flipped = flipped;
            last_skipped = skipped;
            if (title_memory_stats) {
                title_memory_stats_t m;
                title_memory_stats(&m);
                size_t flexible = 0;
                sceKernelAvailableFlexibleMemorySize(&flexible);
                report("memory: %zu MiB direct (peak %zu) in %zu ranges, %zu joined; %zu left to the kernel, "
                       "refused: %zu allocate, %zu map, %zu table full; flexible %zu MiB left",
                       m.direct_bytes >> 20, m.peak_bytes >> 20, m.ranges, m.joined, m.fallbacks,
                       m.alloc_failures, m.map_failures, m.table_full, flexible >> 20);
            }
#ifdef TITLE_MALLOC
            {
                ps5malloc_stats_t a;
                ps5malloc_stats(&a);
                report("malloc: %s; of the console's own blocks %llu freed and %llu grown here; %llu taken from "
                       "the console's for lack of memory; %llu page notes failed",
                       a.pas ? "on libpas" : "the console's (no-pasmalloc)", (unsigned long long)a.foreign_frees,
                       (unsigned long long)a.foreign_reallocs, (unsigned long long)a.fallbacks,
                       (unsigned long long)a.bitmap_failures);
            }
#endif
            {
                struct ps5_heap_stats h;
                memset(&h, 0, sizeof(h));
                ps5_heap_stats(&h);
                int64_t at = 0;
                size_t largest = 0;
                const int err = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 0x4000, &at,
                                                                   &largest);
                report("heap: %zu MiB of direct memory (peak %zu) in %u segments, %u arenas; %llu allocations left to "
                       "libc; largest free piece of direct memory %zu MiB (0x%x)",
                       h.mapped_bytes >> 20, h.peak_bytes >> 20, h.segments, h.arenas, h.libc_fallbacks, largest >> 20,
                       (unsigned)err);
            }
            char video_line[400];
            if (video_on && video_status(video_line, sizeof(video_line)))
                report("%s", video_line);
            last = now;
            last_taken = taken;
            tile_ms = wait_ms = 0;
        }
    }

    report("link closed after %llu flips (%llu frames from the UIProcess); closing", (unsigned long long)shown,
           (unsigned long long)taken);
#ifdef TITLE_RN
    /* 2026-11-14: the browser's process maps memory of this title's (the video ring, the WebProcess's
     * frames) and lets go of it the moment the link closes (main.cpp, on_title_closed()): closed now,
     * with a moment for that, before anything of the title's goes. It left before the browser did once
     * (after its WebProcess crashed, 13:14), and the console went down. */
    if (!link_by_browser) {  /* (the title's own close; when the browser closed, it has let go already) */
        if (link_sock >= 0)
            shutdown(link_sock, SHUT_RDWR);
        sceKernelUsleep(250000);
        report("the link was closed from here first, with 250 ms for the browser to let go of the title's memory");
    }
#endif
#ifdef WEBKIT_IN_TITLE
    if (web_failed)
        report("the WebProcess failed in the title: closing as when the link closes");
    /* The WebProcess's threads stop where they are first: no more GPU work
     * or ring writes while the rest goes. */
    web_host_freeze();
#endif
    /* The broker's owner stops only after its clients close (its README):
     * the GPU work they left finishes before their memory goes. After
     * LINK_QUIT the pages are closed and the WebProcess draws nothing; if
     * the UIProcess died instead, it may still be drawing. */
    pthread_mutex_lock(&gpu_lock);
    const int last_cookie = gpu_cookie;
    gpu_cookie = 0;
    pthread_mutex_unlock(&gpu_lock);
    if (last_cookie)
        gpu_close(last_cookie, "the title is closing");
    capture_end(); // its files closed before the sound and the screen go
    status_quit = true;
    if (status_started)
        pthread_join(status, nullptr);
    // No one may have the system close the title from here on (link.h,
    // LINK_TITLE_READY): its state goes.
    title_state_close();
    feel_stop();
    if (video_on)
        video_stop();
    sound_stop();
    display_close();
    munmap(view, LINK_VIEW_BYTES);
    // The report stays open: on_crash() may still write into it.
    if (!access(EXIT_NORMAL_MARKER, F_OK)) {
        unlink(EXIT_NORMAL_MARKER);
        report("exit: returning from main (the exit_normal marker): exit() and the destructors");
        atexit(exit_started);
        exiting = 1;
        return 0;
    }
    exiting = 1;
#ifdef WEBKIT_IN_TITLE
    web_host_log_flush();
#endif
    if (!access(EXIT_LOADEXEC_MARKER, F_OK)) {
        // Said before the call: if the console goes down, the report shows
        // where.
#ifdef TITLE_RN
        if (leave_told_ms > 0)
            report("exit: %.0f ms after the browser was told to quit at the home screen", now_ms() - leave_told_ms);
#endif
        report("exit: everything is closed: sceSystemServiceLoadExec(\"exit\") (the exit_loadexec marker)");
        if (report_file)
            fflush(report_file);
        const double asked = now_ms();
        const int err = sceSystemServiceLoadExec("exit", nullptr);
        report("exit: sceSystemServiceLoadExec: 0x%08x in %.0f ms; waiting up to %d s for the system", (unsigned)err,
               now_ms() - asked, LOADEXEC_WAIT_MS / 1000);
        if (report_file)
            fflush(report_file);
        while (now_ms() - asked < LOADEXEC_WAIT_MS)
            sceKernelUsleep(100000);
        report("exit: still running %.0f ms after LoadExec: _exit(0) (the system's error dialog follows)",
               now_ms() - asked);
    } else {
        report("exit: everything is closed: _exit(0) (the system's error dialog follows)");
    }
    if (report_file)
        fflush(report_file);
    _exit(0);
}
