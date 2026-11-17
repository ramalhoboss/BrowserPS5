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

/* The screen and the sound to the disk (capture.h; docs/09-video.md).
 *
 * Three steps, each its own thread, none waiting for the next:
 *   the screen's thread  copies a shown frame into a free stage (about 1 ms);
 *   the converter        makes it YUV 4:2:0 (1.6 ms on the console) into the
 *                        RAM ring, and moves the sound into the RAM sound
 *                        buffer;
 *   the writer           writes the ring to the disk, in order, as fast as
 *                        the disk goes, and then the sound and info.txt.
 * The first capture on the console (2026-11-06 00:00) wrote straight to
 * /data: after ~1.3 GB the disk took only ~25 MB/s of the 93 MB/s a second
 * of raw video is, so most frames were lost, and capture_end() waited for
 * the writer on the link's thread, which then missed the browser's quit.
 * Now a piece is recorded into RAM (direct memory, CAPTURE_RAM_MIB) and
 * written out while it is recorded and after; a frame the screen did not
 * change is not stored again (index.txt says which places each frame
 * fills); and nothing here ever waits for the disk on a caller's thread. */

#include "capture.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FPS 30
#define STAGES 6                     /* shown frames waiting for the converter */
#define MAX_SECONDS 120              /* a piece */
#define SOUND_RATE 48000
#define SOUND_RING (SOUND_RATE * 4)  /* frames: 4 s between the sound's thread and the converter */
#define SOUND_BYTES ((size_t)SOUND_RATE * 4 * (MAX_SECONDS + 2))
#define MAX_W 1920
#define MAX_H 1080
#define STAGE_BYTES ((size_t)MAX_W * MAX_H * 4)
#define FRAME_BYTES ((size_t)MAX_W * MAX_H * 3 / 2)
#define PAGE ((size_t)16384)
#define CHUNK_BYTES ((size_t)64 << 20)
#define FRAMES_PER_CHUNK (CHUNK_BYTES / FRAME_BYTES) /* 21 */
#define MAX_CHUNKS 128                               /* 8 GiB at most */
#define WRITE_FRAMES 8                               /* frames in one write() */
#ifndef CAPTURE_RAM_MIB
#define CAPTURE_RAM_MIB 2048
#endif
/* What a second may take on the disk (every frame new, and the sound), and
 * what is left free besides. */
#define BYTES_PER_SECOND (FRAME_BYTES * FPS + SOUND_RATE * 4ull)
#define DISK_SPARE (2ull << 30)

/* IDLE: nothing. RUNNING: frames are taken. CLOSING: the converter takes
 * what is staged and the last sound. WRITING: only the writer is left. */
enum { IDLE, RUNNING, CLOSING, WRITING };

struct stage {
    uint32_t *pixels;
    uint64_t place;  /* 1/FPS s steps from the start */
    int full;
};

static void (*say_line)(const char *line);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t convert_wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t write_wake = PTHREAD_COND_INITIALIZER;
static int state;          /* the enum above; written under lock, read with __atomic */
static int threads_up;     /* the two threads are made once, then sleep */
static unsigned pieces_done;
static struct stage stages[STAGES];
/* The RAM ring: frames [ring_out, ring_in) are converted and not written;
 * frame n is chunk (n % ring_frames) / FRAMES_PER_CHUNK. All under lock. */
static uint8_t *chunks[MAX_CHUNKS];
static unsigned chunk_count;
static uint64_t ring_frames, ring_in, ring_out, ring_peak;
static uint64_t ring_place[MAX_CHUNKS * FRAMES_PER_CHUNK];
/* The piece: set by capture_begin() while IDLE, then read by all. */
static char folder[160];
static int width, height;  /* of the piece's frames (the first one says) */
static double start_ms, end_ms;
static uint64_t next_place;                  /* the screen's thread */
static uint64_t dropped_stage, dropped_ring; /* lock */
/* The sound: the sound's thread and the converter (SPSC, __atomic); then
 * the converter's buffer for the piece, which the writer writes at the end. */
static int16_t *sound_ring;
static uint64_t sound_in, sound_out, sound_lost;
static int16_t *sound_buffer;
static size_t sound_frames;

void capture_set_report(void (*say)(const char *line))
{
    say_line = say;
}

static void report(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *format, ...)
{
    char line[400];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof(line) - 1, format, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n] = '\n';
    line[n + 1] = 0;
    if (say_line)
        say_line(line);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void set_state(int value)
{
    __atomic_store_n(&state, value, __ATOMIC_RELEASE);
}

int capture_running(void)
{
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == RUNNING;
}

int capture_busy(void)
{
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) != IDLE;
}

unsigned capture_done(void)
{
    return __atomic_load_n(&pieces_done, __ATOMIC_ACQUIRE);
}

/* Memory that is not malloc's: in a title malloc() has 448 MiB in all, and
 * the decoders and the GPU need it (video.c); direct memory has 12 GiB. */
#ifdef CAPTURE_HOST_TEST
static void *big_alloc(size_t bytes)
{
    bytes = (bytes + PAGE - 1) & ~(PAGE - 1);
    void *at = NULL;
    return posix_memalign(&at, PAGE, bytes) ? NULL : at;
}
#else
int sceKernelAllocateDirectMemory(off_t start, off_t end, size_t len, size_t alignment, int type, off_t *phys);
int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, off_t phys, size_t alignment);
int sceKernelReleaseDirectMemory(off_t phys, size_t len);
size_t sceKernelGetDirectMemorySize(void);

static void *big_alloc(size_t bytes)
{
    bytes = (bytes + PAGE - 1) & ~(PAGE - 1);
    static size_t total;
    if (!total)
        total = sceKernelGetDirectMemorySize();
    off_t phys;
    void *at = NULL;
    if (sceKernelAllocateDirectMemory(0, (off_t)total, bytes, PAGE, 0, &phys))
        return NULL;
    if (sceKernelMapDirectMemory(&at, bytes, PROT_READ | PROT_WRITE, 0, phys, PAGE)) {
        sceKernelReleaseDirectMemory(phys, bytes);
        return NULL;
    }
    return at;
}
#endif

/* BGRA to I420, BT.709 limited range (the 1024ths: 0.1826, 0.6142,
 * 0.0620 for luma; the chroma of each 2x2 block from its mean). */
static void luma_row(const uint32_t *restrict in, int w, uint8_t *restrict out)
{
    for (int x = 0; x < w; x++) {
        const uint32_t p = in[x];
        const int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
        out[x] = (uint8_t)(16 + ((187 * r + 629 * g + 63 * b + 512) >> 10));
    }
}

/* Simple loops, so the compiler makes them AVX2 (capture.c is built with
 * -O3 -mavx2, as video.c). */
static void to_i420(const uint32_t *in, int w, int h, uint8_t *out)
{
    uint8_t *yp = out, *up = out + (size_t)w * h, *vp = up + (size_t)(w / 2) * (h / 2);
    for (int y = 0; y < h; y += 2) {
        const uint32_t *restrict a = in + (size_t)y * w, *restrict b = a + w;
        luma_row(a, w, yp + (size_t)y * w);
        luma_row(b, w, yp + (size_t)(y + 1) * w);
        uint8_t *restrict u = up + (size_t)(y / 2) * (w / 2), *restrict v = vp + (size_t)(y / 2) * (w / 2);
        for (int x = 0; x < w / 2; x++) {
            const uint32_t p0 = a[2 * x], p1 = a[2 * x + 1], p2 = b[2 * x], p3 = b[2 * x + 1];
            const int rs = (int)((p0 >> 16) & 0xff) + (int)((p1 >> 16) & 0xff) + (int)((p2 >> 16) & 0xff) +
                           (int)((p3 >> 16) & 0xff);
            const int gs = (int)((p0 >> 8) & 0xff) + (int)((p1 >> 8) & 0xff) + (int)((p2 >> 8) & 0xff) +
                           (int)((p3 >> 8) & 0xff);
            const int bs = (int)(p0 & 0xff) + (int)(p1 & 0xff) + (int)(p2 & 0xff) + (int)(p3 & 0xff);
            /* Sums of 4: the 1024ths over 4096, kept positive; always
             * within 16..240. */
            u[x] = (uint8_t)((-103 * rs - 347 * gs + 450 * bs + 128 * 4096 + 2048) >> 12);
            v[x] = (uint8_t)((450 * rs - 409 * gs - 41 * bs + 128 * 4096 + 2048) >> 12);
        }
    }
}

static int write_all(int fd, const void *data, size_t size)
{
#ifdef CAPTURE_HOST_TEST
    /* SLOW_DISK=1 (capture_test.c): a disk of 25 MB/s, as /data was. */
    static int slow = -1;
    if (slow < 0)
        slow = getenv("SLOW_DISK") != NULL;
    if (slow)
        usleep((useconds_t)(size / 25));
#endif
    const uint8_t *p = (const uint8_t *)data;
    while (size) {
        const ssize_t n = write(fd, p, size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static uint8_t *ring_frame(uint64_t n)
{
    const uint64_t at = n % ring_frames;
    return chunks[at / FRAMES_PER_CHUNK] + (size_t)(at % FRAMES_PER_CHUNK) * FRAME_BYTES;
}

/* The sound that came in since the last time, into the piece's buffer. */
static void drain_sound(void)
{
    const uint64_t in = __atomic_load_n(&sound_in, __ATOMIC_ACQUIRE);
    uint64_t out = __atomic_load_n(&sound_out, __ATOMIC_RELAXED);
    const size_t room = SOUND_BYTES / 4;
    for (; out < in; out++) {
        const uint64_t at = out % SOUND_RING;
        if (sound_frames < room) {
            sound_buffer[sound_frames * 2] = sound_ring[at * 2];
            sound_buffer[sound_frames * 2 + 1] = sound_ring[at * 2 + 1];
            sound_frames++;
        } else {
            __atomic_add_fetch(&sound_lost, 1, __ATOMIC_RELAXED);
        }
    }
    __atomic_store_n(&sound_out, out, __ATOMIC_RELEASE);
}

static double convert_ms;
static uint64_t converted;

/* Staged frames, in order, into the ring; the sound into its buffer. Wakes
 * at least every half second for the sound, so a screen that stops does not
 * let the 4 s sound ring run over. */
static void *convert_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    for (;;) {
        struct stage *next = NULL;
        for (int i = 0; i < STAGES; i++)
            if (stages[i].full && (!next || stages[i].place < next->place))
                next = &stages[i];
        const int now = state;
        if (!next && now == CLOSING) {
            pthread_mutex_unlock(&lock);
            drain_sound();
            pthread_mutex_lock(&lock);
            set_state(WRITING);
            pthread_cond_signal(&write_wake);
            continue;
        }
        if (!next) {
            if (now == RUNNING) {
                pthread_mutex_unlock(&lock);
                drain_sound();
                pthread_mutex_lock(&lock);
            }
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += 500 * 1000000L;
            if (until.tv_nsec >= 1000000000L) {
                until.tv_sec++;
                until.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&convert_wake, &lock, &until);
            continue;
        }
        const int room = ring_in - ring_out < ring_frames;
        uint8_t *into = room ? ring_frame(ring_in) : NULL;
        if (!room)
            dropped_ring++;  /* the disk is behind and the RAM full: this place repeats the frame before */
        pthread_mutex_unlock(&lock);
        if (into) {
            const double t = now_ms();
            to_i420(next->pixels, width, height, into);
            convert_ms += now_ms() - t;
            converted++;
        }
        drain_sound();
        pthread_mutex_lock(&lock);
        if (into) {
            ring_place[ring_in % ring_frames] = next->place;
            ring_in++;
            if (ring_in - ring_out > ring_peak)
                ring_peak = ring_in - ring_out;
            pthread_cond_signal(&write_wake);
        }
        next->full = 0;
    }
    return NULL;
}

/* The piece's files: frames as they come, then the sound and info.txt. */
static void *write_thread(void *arg)
{
    (void)arg;
    char path[200];
    for (;;) {
        pthread_mutex_lock(&lock);
        while (__atomic_load_n(&state, __ATOMIC_RELAXED) == IDLE)
            pthread_cond_wait(&write_wake, &lock);
        pthread_mutex_unlock(&lock);

        snprintf(path, sizeof(path), "%s/video.yuv", folder);
        const int video = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        snprintf(path, sizeof(path), "%s/index.txt", folder);
        FILE *index = fopen(path, "w");
        int failed = video < 0 || !index;
        if (failed)
            report("[capture] cannot write in %s: %s", folder, strerror(errno));
        uint64_t frames = 0, bytes = 0;
        double write_ms = 0, slowest_mb_s = 0;
        int have_rate = 0;

        pthread_mutex_lock(&lock);
        for (;;) {
            while (ring_out == ring_in && state != WRITING)
                pthread_cond_wait(&write_wake, &lock);
            if (ring_out == ring_in)
                break;  /* the converter is done and so are we */
            /* Up to WRITE_FRAMES frames that follow each other in one chunk. */
            const uint64_t first = ring_out;
            uint64_t n = 1;
            const size_t frame = (size_t)width * height * 3 / 2;
            while (frame == FRAME_BYTES && n < WRITE_FRAMES && first + n < ring_in &&
                   (first + n) % ring_frames % FRAMES_PER_CHUNK != 0)
                n++;
            uint64_t places[WRITE_FRAMES];
            for (uint64_t i = 0; i < n; i++)
                places[i] = ring_place[(first + i) % ring_frames];
            uint8_t *from = ring_frame(first);
            pthread_mutex_unlock(&lock);

            if (!failed) {
                const double t = now_ms();
                if (write_all(video, from, (size_t)n * frame)) {
                    failed = 1;
                    report("[capture] %s: write: %s", folder, strerror(errno));
                }
                const double took = now_ms() - t;
                write_ms += took;
                bytes += (uint64_t)n * frame;
                /* The slowest of the long writes, in MB/s. */
                if (n == WRITE_FRAMES && took > 0) {
                    const double rate = n * frame / 1e3 / took;
                    if (!have_rate || rate < slowest_mb_s)
                        slowest_mb_s = rate;
                    have_rate = 1;
                }
                for (uint64_t i = 0; i < n; i++)
                    fprintf(index, "%llu\n", (unsigned long long)places[i]);
            }
            frames += n;
            pthread_mutex_lock(&lock);
            ring_out += n;
        }
        const uint64_t lost_stage = dropped_stage, lost_ring = dropped_ring, peak = ring_peak;
        pthread_mutex_unlock(&lock);

        if (video >= 0)
            close(video);
        if (index)
            fclose(index);
        snprintf(path, sizeof(path), "%s/sound.s16", folder);
        const int sound = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        const double t = now_ms();
        int sound_failed = sound < 0 || write_all(sound, sound_buffer, sound_frames * 4);
        write_ms += now_ms() - t;
        bytes += sound_frames * 4;
        if (sound >= 0)
            close(sound);
        const double seconds = (end_ms - start_ms) / 1e3;
        const uint64_t places_total = (uint64_t)(seconds * FPS);
        snprintf(path, sizeof(path), "%s/info.txt", folder);
        FILE *info = fopen(path, "w");
        if (info) {
            fprintf(info,
                    "video video.yuv %dx%d yuv420p bt709 tv %d fps\n"
                    "frames %llu places %llu index index.txt\n"
                    "sound sound.s16 s16le 2 channels %d Hz %llu frames\n"
                    "seconds %.3f dropped_stage %llu dropped_ram %llu sound_lost %llu convert_ms %.2f "
                    "ram_frames %llu ram_peak %llu write_mb_s %.1f slowest_mb_s %.1f failed %d\n",
                    width, height, FPS, (unsigned long long)frames, (unsigned long long)places_total, SOUND_RATE,
                    (unsigned long long)sound_frames, seconds, (unsigned long long)lost_stage,
                    (unsigned long long)lost_ring, (unsigned long long)__atomic_load_n(&sound_lost, __ATOMIC_RELAXED),
                    converted ? convert_ms / (double)converted : 0.0, (unsigned long long)ring_frames,
                    (unsigned long long)peak, write_ms > 0 ? bytes / 1e3 / write_ms : 0.0, slowest_mb_s,
                    failed || sound_failed);
            fclose(info);
        }
        report("[capture] %s: %llu frames for %llu places (dropped %llu staged, %llu RAM full), "
               "%.1f s of sound, %.2f ms a conversion, RAM peak %llu of %llu frames, disk %.1f MB/s "
               "(slowest %.1f)%s",
               folder, (unsigned long long)frames, (unsigned long long)places_total, (unsigned long long)lost_stage,
               (unsigned long long)lost_ring, sound_frames / (double)SOUND_RATE,
               converted ? convert_ms / (double)converted : 0.0, (unsigned long long)peak,
               (unsigned long long)ring_frames, write_ms > 0 ? bytes / 1e3 / write_ms : 0.0, slowest_mb_s,
               failed || sound_failed ? "; WRITING FAILED" : "");

        pthread_mutex_lock(&lock);
        set_state(IDLE);
        __atomic_add_fetch(&pieces_done, 1, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static int name_ok(const char *name)
{
    if (!name || !*name || strlen(name) > 40)
        return 0;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_'))
            return 0;
    return 1;
}

/* The memory and the threads, the first time; kept for the next pieces. */
static int set_up(void)
{
    if (threads_up)
        return 0;
    for (int i = 0; i < STAGES; i++)
        if (!stages[i].pixels && !(stages[i].pixels = big_alloc(STAGE_BYTES)))
            return report("[capture] no memory for the frames"), -1;
    if (!sound_ring && !(sound_ring = big_alloc(SOUND_RING * 4)))
        return report("[capture] no memory for the sound"), -1;
    if (!sound_buffer && !(sound_buffer = big_alloc(SOUND_BYTES)))
        return report("[capture] no memory for the sound"), -1;
    unsigned want = CAPTURE_RAM_MIB / 64;
    if (want > MAX_CHUNKS)
        want = MAX_CHUNKS;
    while (chunk_count < want && (chunks[chunk_count] = big_alloc(CHUNK_BYTES)))
        chunk_count++;
    if (chunk_count < 2)
        return report("[capture] no memory for the RAM ring (%u of %u MiB)", chunk_count * 64, want * 64), -1;
    ring_frames = (uint64_t)chunk_count * FRAMES_PER_CHUNK;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_t thread;
    int err = pthread_create(&thread, &attr, convert_thread, NULL);
    if (!err) {
        pthread_detach(thread);
        err = pthread_create(&thread, &attr, write_thread, NULL);
        /* Without the writer the converter only sleeps; no piece starts. */
        if (!err)
            pthread_detach(thread);
    }
    pthread_attr_destroy(&attr);
    if (err)
        return report("[capture] pthread_create: %s", strerror(err)), -1;
    threads_up = 1;
    report("[capture] RAM ring: %u MiB, %llu frames (%.1f s of new frames)", chunk_count * 64,
           (unsigned long long)ring_frames, ring_frames / (double)FPS);
    return 0;
}

int capture_begin(const char *name, unsigned seconds)
{
    if (!name_ok(name)) {
        report("[capture] not a capture's name");
        return -1;
    }
    if (!seconds || seconds > MAX_SECONDS)
        seconds = MAX_SECONDS;
    if (capture_busy()) {
        report("[capture] %s: the piece before is still being %s", name, capture_running() ? "recorded" : "written");
        return -1;
    }
    if (set_up())
        return -1;
    mkdir(CAPTURE_DIR, 0777);
    char where[sizeof(folder)];
    snprintf(where, sizeof(where), "%s/%s", CAPTURE_DIR, name);
    if (mkdir(where, 0777) && errno != EEXIST) {
        report("[capture] mkdir %s: %s", where, strerror(errno));
        return -1;
    }
    struct statvfs fs;
    const unsigned long long need = BYTES_PER_SECOND * seconds + DISK_SPARE;
    if (!statvfs(where, &fs) && (unsigned long long)fs.f_bavail * fs.f_frsize < need) {
        report("[capture] %s: %llu MiB free, %llu needed", name,
               (unsigned long long)fs.f_bavail * fs.f_frsize >> 20, need >> 20);
        return -1;
    }

    pthread_mutex_lock(&lock);
    /* IDLE: both threads are asleep and touch none of this. */
    memcpy(folder, where, sizeof(folder));
    for (int i = 0; i < STAGES; i++)
        stages[i].full = 0;
    width = 0;  /* the first frame says */
    dropped_stage = dropped_ring = 0;
    ring_in = ring_out = ring_peak = 0;
    convert_ms = 0;
    converted = 0;
    sound_frames = 0;
    __atomic_store_n(&sound_lost, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sound_out, __atomic_load_n(&sound_in, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    next_place = 0;
    start_ms = now_ms();
    end_ms = start_ms + seconds * 1e3;
    set_state(RUNNING);
    pthread_cond_signal(&write_wake);
    pthread_cond_signal(&convert_wake);
    pthread_mutex_unlock(&lock);
    report("[capture] %s: up to %u s", folder, seconds);
    return 0;
}

void capture_end(void)
{
    pthread_mutex_lock(&lock);
    if (state == RUNNING) {
        end_ms = now_ms();
        set_state(CLOSING);
        pthread_cond_signal(&convert_wake);
    }
    pthread_mutex_unlock(&lock);
}

void capture_frame(const uint32_t *pixels, int w, int h, int stride)
{
    if (!capture_running() || w > MAX_W || h > MAX_H || w < 2 || h < 2)
        return;
    const double now = now_ms();
    if (now >= end_ms) {
        capture_end();
        return;
    }
    if (!width) {
        height = h & ~1;
        __atomic_store_n(&width, w & ~1, __ATOMIC_RELEASE);
    }
    const uint64_t place = (uint64_t)((now - start_ms) * FPS / 1e3);
    if (place < next_place)
        return;  /* not yet the next place */
    next_place = place + 1;
    pthread_mutex_lock(&lock);
    struct stage *free_stage = NULL;
    for (int i = 0; i < STAGES && !free_stage; i++)
        if (!stages[i].full)
            free_stage = &stages[i];
    if (!free_stage)
        dropped_stage++;
    pthread_mutex_unlock(&lock);
    if (!free_stage)
        return;  /* the converter is behind: this place repeats the frame before */
    for (int y = 0; y < height; y++)
        memcpy(free_stage->pixels + (size_t)y * width, pixels + (size_t)y * stride, (size_t)width * 4);
    pthread_mutex_lock(&lock);
    /* Only while still running: a frame staged after the converter closed
     * the piece would wait for the next one. */
    if (state == RUNNING) {
        free_stage->place = place;
        free_stage->full = 1;
        pthread_cond_signal(&convert_wake);
    }
    pthread_mutex_unlock(&lock);
}

void capture_sound(const int16_t *samples, unsigned frames)
{
    if (!capture_running() || !sound_ring)
        return;
    const uint64_t in = __atomic_load_n(&sound_in, __ATOMIC_RELAXED);
    const uint64_t out = __atomic_load_n(&sound_out, __ATOMIC_ACQUIRE);
    if (in + frames - out > SOUND_RING) {
        __atomic_add_fetch(&sound_lost, frames, __ATOMIC_RELAXED);
        return;
    }
    for (unsigned i = 0; i < frames; i++) {
        const uint64_t at = (in + i) % SOUND_RING;
        sound_ring[at * 2] = samples[i * 2];
        sound_ring[at * 2 + 1] = samples[i * 2 + 1];
    }
    __atomic_store_n(&sound_in, in + frames, __ATOMIC_RELEASE);
}
