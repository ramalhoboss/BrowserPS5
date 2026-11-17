/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The title's video (docs/08-4k.md, D3). ps5videosink in the WebProcess
 * writes each <video>'s frames, still compressed, into a slot of
 * VIDEO_RING_SHM (../../common/video_ring.h). Here:
 *   - a decoder thread for each slot decodes them with the browser's FFmpeg
 *     (the same libavcodec.a as WebKit's GStreamer), with many threads: a
 *     title has 13 cores, the payloads ~2 between them (docs/05);
 *   - once a flip, video_compose() takes for each video the newest picture
 *     whose time has come on the sink's clock (the pipeline's: the sound's),
 *     turns it into BGRA at the size of its place (bilinear, BT.709 or
 *     BT.601), and puts it under the screen: where the page is transparent
 *     (WebKit's hole) the video shows, where it is half transparent
 *     (YouTube's controls over the video) the two mix, premultiplied as
 *     Cairo's pixels are.
 * The conversion and the mixing run on WORKERS threads plus the caller, a
 * band of rows each; they wait on a condition variable between flips.
 * Nothing here spins: the decoders sleep 2 ms when there is nothing to do.
 */
#include "video.h"
#include "video_ring.h"

#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef VIDEO_HOST_TEST
#define VLOG(fmt, ...) fprintf(stderr, "[video] " fmt "\n", ##__VA_ARGS__)
#else
#include "poc_log.h"
#define VLOG(fmt, ...) POC_LOG("video: " fmt, ##__VA_ARGS__)

// FFmpeg's log asks whether stderr is a terminal (for its colours). A title
// has isatty only from libScePosixForWebKit, which does not load in a title
// (poc/webkit_title/posix.c), so build.sh leaves that module out and this
// answers.
int isatty(int fd)
{
    (void)fd;
    errno = ENOTTY;
    return 0;
}
#endif

#define QUEUE    6   // decoded pictures held for each video
#define WORKERS  4   // threads that convert and mix, besides the caller
#define AHEAD_NS 8000000  // a picture is shown when its time is less than this away
#define CLOCK_SNAP_NS 40000000  // the clock moved more than this: a jump, taken at once
#define CLOCK_SMOOTH  16        // else 1/16 of the gap each flip (~0.27 s)
#define IDLE_US  2000

typedef struct picture {
    AVFrame *frame;
    int64_t running_ns;
    uint32_t generation;
} picture_t;

typedef struct decoder {
    int index;
    video_slot_t *slot;
    pthread_t thread;
    int started;

    // The decoder thread's.
    AVCodecContext *ctx;
    uint32_t codec;
    uint32_t serial;      // the writer it decodes for
    uint32_t generation;  // the flushes it has seen
    unsigned odd_format;  // pictures in a format it cannot show (logged once)
    unsigned errors;      // packets or pictures the decoder refused (atomic)
    int hw;               // its video is on the PS5's decoder (video_hw.h), not FFmpeg
    int hw_refused;       // the hardware gave this video up: FFmpeg until the next one
    int hw_resume;        // 18:5x: let go while the title was away: the hardware again when back
    video_format_t format;  // the video's format and codec data (for FFmpeg if the hardware gives up)
    uint8_t *config;

    // Its pictures' memory (get_buffer(), from its threads, under pool_lock).
    pthread_mutex_t pool_lock;
    int pool_format, pool_width, pool_height;
    int pool_linesize[4];
    AVBufferPool *pools[4];
    uint64_t pid_check_ns;  // when it last looked whether the writer lives

    // Shared with the display thread, under lock.
    pthread_mutex_t lock;
    picture_t queue[QUEUE + 4];  // oldest first; a packet can give two
    int count;
    uint32_t queue_serial;       // whose pictures they are

    // The display thread's.
    AVFrame *current;      // the picture shown
    uint32_t current_serial;
    uint32_t current_generation;
    uint64_t picture_ns;   // when current last changed (the mixing order)
    int converted;         // current is in pixels at the place below
    int place[4];          // the picture's place on the screen: x, y, w, h
    int clip[4];           // the part of it on the screen
    uint32_t *pixels;      // clip[2] x clip[3], BGRA
    size_t pixels_size;
    // 2026-11-09: the pipeline's clock (the sound's) comes in steps with each
    // frame; read straight, its running time shook by a few ms from flip to
    // flip, and a 60 fps video on 60 Hz near the edge got 0 and 2 pictures
    // in turn (13% late, a stutter). Its offset from the monotonic clock,
    // smoothed; a jump (a seek, a pause) takes it at once.
    int64_t clock_off;     // smoothed running - now
    int clock_ok;
    double jitter_max;     // the largest gap raw-smoothed, ms, since the last status
} decoder_t;

static struct {
    video_ring_t *ring;
    decoder_t decoders[VIDEO_RING_SLOTS];
    volatile int quit;
    int threads;           // libavcodec's
    int thread_type;
    uint32_t *out;         // the screen with the videos
    size_t out_size;
    uint64_t composed;     // flips with a video
    uint64_t status_shown[VIDEO_RING_SLOTS];
    uint32_t status_moves[VIDEO_RING_SLOTS];  // (TITLE_RN) the place's sequence count at the last status
    double convert_ms, mix_ms;
    unsigned converts, mixes;
    // The pictures' memory: in use now, the most, and blocks that came
    // from malloc because direct memory failed (atomics).
    uint64_t frame_bytes, frame_peak, frame_fallbacks;
} video;


// ---------------------------------------------------------------------------
// Bands of rows on several threads.

typedef void (*band_fn)(void *ctx, int y0, int y1);

static struct {
    pthread_t threads[WORKERS];
    int started;
    pthread_mutex_t lock;
    pthread_cond_t go, done;
    unsigned job;
    int pending;
    band_fn fn;
    void *ctx;
    int rows;
    int quit;
} pool = {.lock = PTHREAD_MUTEX_INITIALIZER, .go = PTHREAD_COND_INITIALIZER,
          .done = PTHREAD_COND_INITIALIZER};

static void *pool_thread(void *arg)
{
    const int band = (int)(intptr_t)arg + 1;  // the caller does band 0
    unsigned seen = 0;

    pthread_mutex_lock(&pool.lock);
    for (;;) {
        while (pool.job == seen && !pool.quit)
            pthread_cond_wait(&pool.go, &pool.lock);
        if (pool.quit)
            break;
        seen = pool.job;
        band_fn fn = pool.fn;
        void *ctx = pool.ctx;
        const int rows = pool.rows;
        pthread_mutex_unlock(&pool.lock);
        const int y0 = rows * band / (WORKERS + 1), y1 = rows * (band + 1) / (WORKERS + 1);
        if (y1 > y0)
            fn(ctx, y0, y1);
        pthread_mutex_lock(&pool.lock);
        if (--pool.pending == 0)
            pthread_cond_signal(&pool.done);
    }
    pthread_mutex_unlock(&pool.lock);
    return NULL;
}

static void pool_run(band_fn fn, void *ctx, int rows)
{
    if (!pool.started || rows < 64) {
        fn(ctx, 0, rows);
        return;
    }
    pthread_mutex_lock(&pool.lock);
    pool.fn = fn;
    pool.ctx = ctx;
    pool.rows = rows;
    pool.pending = WORKERS;
    pool.job++;
    pthread_cond_broadcast(&pool.go);
    pthread_mutex_unlock(&pool.lock);
    const int y1 = rows / (WORKERS + 1);
    if (y1 > 0)
        fn(ctx, 0, y1);
    pthread_mutex_lock(&pool.lock);
    while (pool.pending)
        pthread_cond_wait(&pool.done, &pool.lock);
    pthread_mutex_unlock(&pool.lock);
}

static void pool_start(void)
{
    for (int i = 0; i < WORKERS; i++) {
        if (pthread_create(&pool.threads[i], NULL, pool_thread, (void *)(intptr_t)i)) {
            VLOG("no worker thread %d: %s", i, strerror(errno));
            pthread_mutex_lock(&pool.lock);
            pool.quit = 1;
            pthread_cond_broadcast(&pool.go);
            pthread_mutex_unlock(&pool.lock);
            for (int k = 0; k < i; k++)
                pthread_join(pool.threads[k], NULL);
            pool.quit = 0;
            return;
        }
    }
    pool.started = 1;
}

static void pool_stop(void)
{
    if (!pool.started)
        return;
    pthread_mutex_lock(&pool.lock);
    pool.quit = 1;
    pthread_cond_broadcast(&pool.go);
    pthread_mutex_unlock(&pool.lock);
    for (int i = 0; i < WORKERS; i++)
        pthread_join(pool.threads[i], NULL);
    pool.started = 0;
}


// ---------------------------------------------------------------------------
// YUV 4:2:0 to BGRA, scaled into the picture's place.

typedef struct convert {
    const AVFrame *frame;
    int bits;                // 8 or 10
    int fw, fh, cw, ch;      // the frame's size and its chroma's
    int place[4];            // where the whole picture goes (screen)
    int clip[4];             // the part to make (screen)
    uint32_t *out;           // clip[2] x clip[3]
    // per column of the clip: the luma's left sample and weight (0..256),
    // the chroma's the same
    int *lx, *lw, *cx, *cwt;
    // the source columns those use: luma lx[0]..ylast, chroma cx[0]..clast
    // (the right samples included)
    int ylast, clast;
    // the matrix, 16.16 fixed point
    int yoff, ymul, rv, gu, gv, bu;
    int nv12;  // the chroma in one plane, U and V interleaved (the hardware decoder's)
} convert_t;

// Where output pixel i (0..out-1) samples a line of n source samples, the
// picture being out wide over those n: the left sample and the weight of the
// right one (bilinear, centers aligned).
static void sample_at(int i, int out, int n, int *left, int *weight)
{
    const double s = ((double)i + 0.5) * n / out - 0.5;
    int l = (int)(s < 0 ? s - 1 : s);  // floor
    int w = (int)((s - l) * 256 + 0.5);
    if (w >= 256) {
        l++;
        w = 0;
    }
    if (l < 0) {
        l = 0;
        w = 0;
    }
    if (l >= n - 1) {
        l = n - 1;
        w = 0;
    }
    *left = l;
    *weight = w;
}

// The conversion goes row by row, in passes over plain arrays that the
// compiler makes into vector code (on the PC, a 1080p picture in full
// screen went from 3.7 to 1.4 ms, docs/08-4k.md). The vertical pass first, at
// the source's resolution, then the horizontal one through the column
// tables, then the colors. The sums are the same integers as a bilinear
// sample taken at once, so the pixels are too (exact integers, one
// rounding).

// dst[k] = a[k] * (256 - w) + b[k] * w for the source columns lo..hi
// (hi < n), and dst[hi + 1] = 0 when that is past the line: the right
// sample of the last column, read only with a weight of 0.
#define VERTICAL(TYPE, SHIFT, dst, a, b, w, lo, hi, n)                                \
    do {                                                                               \
        const TYPE *a_ = (const TYPE *)(a), *b_ = (const TYPE *)(b);                   \
        const int w_ = (w), hi_ = (hi) < (n) ? (hi) : (n) - 1;                         \
        for (int k = (lo); k <= hi_; k++)                                              \
            (dst)[k] = (a_[k] >> SHIFT) * (256 - w_) + (b_[k] >> SHIFT) * w_;          \
        if ((hi) >= (n))                                                               \
            (dst)[n] = 0;                                                              \
    } while (0)

// The same for NV12's chroma (8-bit, U and V interleaved): both rows at once.
#define VERTICAL_UV(du, dv, a, b, w, lo, hi, n)                                        \
    do {                                                                               \
        const uint8_t *a_ = (const uint8_t *)(a), *b_ = (const uint8_t *)(b);          \
        const int w_ = (w), hi_ = (hi) < (n) ? (hi) : (n) - 1;                         \
        for (int k = (lo); k <= hi_; k++) {                                            \
            (du)[k] = a_[2 * k] * (256 - w_) + b_[2 * k] * w_;                         \
            (dv)[k] = a_[2 * k + 1] * (256 - w_) + b_[2 * k + 1] * w_;                 \
        }                                                                              \
        if ((hi) >= (n))                                                               \
            (du)[n] = (dv)[n] = 0;                                                     \
    } while (0)

// out[x] = the bilinear sample of the vertical pass's row at column x.
static void horizontal(int32_t *restrict out, const int32_t *restrict row, const int *restrict left,
                       const int *restrict weight, int count, int bias)
{
    for (int x = 0; x < count; x++) {
        const int l = left[x], w = weight[x];
        out[x] = ((row[l] * (256 - w) + row[l + 1] * w + 32768) >> 16) - bias;
    }
}

static void colors(uint32_t *restrict o, const int32_t *restrict ys, const int32_t *restrict us,
                   const int32_t *restrict vs, int count, const convert_t *c)
{
    const int yoff = c->yoff, ymul = c->ymul, rv = c->rv, gu = c->gu, gv = c->gv, bu = c->bu;
    for (int x = 0; x < count; x++) {
        const int l = (ys[x] - yoff) * ymul, uu = us[x], vv = vs[x];
        int r = (l + rv * vv + 32768) >> 16, g = (l - gu * uu - gv * vv + 32768) >> 16,
            b = (l + bu * uu + 32768) >> 16;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        o[x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
}

// The picture half the frame's size each way (4K in a 1080p place): the
// luma is the mean of 2x2 samples (the two rows added, then the pairs of
// columns), the chroma one sample.
#define CONVERT_HALF(TYPE, SHIFT)                                                      \
    for (int y = y0; y < y1; y++) {                                                    \
        const int py = c->clip[1] + y - c->place[1];                                    \
        const int n = c->clip[2], px0 = c->clip[0] - c->place[0];                       \
        const uint8_t *const *d = (const uint8_t *const *)f->data;                      \
        const TYPE *y_a = (const TYPE *)(d[0] + (size_t)(2 * py) * f->linesize[0]) + 2 * px0; \
        const TYPE *y_b = (const TYPE *)(d[0] + (size_t)(2 * py + 1) * f->linesize[0]) + 2 * px0; \
        const TYPE *u = (const TYPE *)(d[1] + (size_t)py * f->linesize[1]) + px0;      \
        const TYPE *v = (const TYPE *)(d[2] + (size_t)py * f->linesize[2]) + px0;      \
        for (int k = 0; k < 2 * n; k++)                                                 \
            yrow[k] = (y_a[k] >> SHIFT) + (y_b[k] >> SHIFT);                            \
        for (int x = 0; x < n; x++)                                                     \
            ys[x] = (yrow[2 * x] + yrow[2 * x + 1] + 2) >> 2;                           \
        if (c->nv12) {                                                                  \
            const uint8_t *uv = d[1] + (size_t)py * f->linesize[1] + 2 * (size_t)px0;   \
            for (int x = 0; x < n; x++) {                                               \
                us[x] = uv[2 * x] - 128;                                                \
                vs[x] = uv[2 * x + 1] - 128;                                            \
            }                                                                           \
        } else {                                                                        \
            for (int x = 0; x < n; x++) {                                               \
                us[x] = (u[x] >> SHIFT) - 128;                                          \
                vs[x] = (v[x] >> SHIFT) - 128;                                          \
            }                                                                           \
        }                                                                               \
        colors(c->out + (size_t)y * n, ys, us, vs, n, c);                               \
    }

// One band of rows, the general place (any scale) or the exact one (the
// picture as large as the frame: the luma as it is). rows: the scratch,
// (fw + 1) + 2 * (cw + 1) + 3 * clip[2] ints.
#define CONVERT_PASSES(TYPE, SHIFT)                                                    \
    for (int y = y0; y < y1; y++) {                                                    \
        const int py = c->clip[1] + y - c->place[1];                                    \
        const int n = c->clip[2];                                                       \
        int cy, cwy;                                                                    \
        sample_at(py, c->place[3], c->ch, &cy, &cwy);                                   \
        const int cy2 = cy + (cwy ? 1 : 0);                                             \
        const uint8_t *const *d = (const uint8_t *const *)f->data;                      \
        if (exact) {                                                                    \
            const TYPE *yr = (const TYPE *)(d[0] + (size_t)py * f->linesize[0]) + c->clip[0] - c->place[0]; \
            for (int x = 0; x < n; x++)                                                 \
                ys[x] = yr[x] >> SHIFT;                                                 \
        } else {                                                                        \
            int ly, lwy;                                                                \
            sample_at(py, c->place[3], c->fh, &ly, &lwy);                               \
            const int ly2 = ly + (lwy ? 1 : 0);                                         \
            VERTICAL(TYPE, SHIFT, yrow, d[0] + (size_t)ly * f->linesize[0],             \
                     d[0] + (size_t)ly2 * f->linesize[0], lwy, c->lx[0], c->ylast, c->fw); \
            horizontal(ys, yrow, c->lx, c->lw, n, 0);                                   \
        }                                                                               \
        if (c->nv12) {                                                                  \
            VERTICAL_UV(urow, vrow, d[1] + (size_t)cy * f->linesize[1],                 \
                        d[1] + (size_t)cy2 * f->linesize[1], cwy, c->cx[0], c->clast, c->cw); \
        } else {                                                                        \
            VERTICAL(TYPE, SHIFT, urow, d[1] + (size_t)cy * f->linesize[1],             \
                     d[1] + (size_t)cy2 * f->linesize[1], cwy, c->cx[0], c->clast, c->cw); \
            VERTICAL(TYPE, SHIFT, vrow, d[2] + (size_t)cy * f->linesize[2],             \
                     d[2] + (size_t)cy2 * f->linesize[2], cwy, c->cx[0], c->clast, c->cw); \
        }                                                                               \
        horizontal(us, urow, c->cx, c->cwt, n, 128);                                    \
        horizontal(vs, vrow, c->cx, c->cwt, n, 128);                                    \
        colors(c->out + (size_t)y * n, ys, us, vs, n, c);                               \
    }

static void convert_band(void *arg, int y0, int y1)
{
    const convert_t *c = arg;
    const AVFrame *f = c->frame;
    const int exact = c->place[2] == c->fw && c->place[3] == c->fh;
    const int half = 2 * c->place[2] == c->fw && 2 * c->place[3] == c->fh;

    // (the half place's luma rows, 2 * clip[2], fit in fw + 1)
    int32_t *scratch = malloc(sizeof(int32_t) * ((size_t)c->fw + 1 + 2 * ((size_t)c->cw + 1) + 3 * (size_t)c->clip[2]));
    if (!scratch)
        return;  // this band stays as it was
    int32_t *yrow = scratch, *urow = yrow + c->fw + 1, *vrow = urow + c->cw + 1;
    int32_t *ys = vrow + c->cw + 1, *us = ys + c->clip[2], *vs = us + c->clip[2];
    if (half) {
        if (c->bits == 8) {
            CONVERT_HALF(uint8_t, 0)
        } else {
            CONVERT_HALF(uint16_t, 2)
        }
    } else if (c->bits == 8) {
        CONVERT_PASSES(uint8_t, 0)
    } else {
        CONVERT_PASSES(uint16_t, 2)
    }
    free(scratch);
}

// The matrix for the frame's colors: BT.709 unless it says BT.601 (or is
// small and says nothing), limited range unless it says full.
static void convert_matrix(convert_t *c, const AVFrame *f)
{
    int bt601 = f->colorspace == AVCOL_SPC_BT470BG || f->colorspace == AVCOL_SPC_SMPTE170M
        || f->colorspace == AVCOL_SPC_FCC
        || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height < 720);
    int full = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
    double kr = bt601 ? 0.299 : 0.2126, kb = bt601 ? 0.114 : 0.0722;
    double kg = 1 - kr - kb;
    double ys = full ? 1.0 : 255.0 / 219.0, cs = full ? 1.0 : 255.0 / 224.0;

    c->yoff = full ? 0 : 16;
    c->ymul = (int)(ys * 65536 + 0.5);
    c->rv = (int)(2 * (1 - kr) * cs * 65536 + 0.5);
    c->bu = (int)(2 * (1 - kb) * cs * 65536 + 0.5);
    c->gu = (int)(2 * (1 - kb) * kb / kg * cs * 65536 + 0.5);
    c->gv = (int)(2 * (1 - kr) * kr / kg * cs * 65536 + 0.5);
}

static int frame_bits(const AVFrame *f)
{
    switch (f->format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_NV12:  // the hardware decoder's
        return 8;
    case AV_PIX_FMT_YUV420P10LE:
        return 10;
    default:
        return 0;
    }
}

// d->current into d->pixels at d->place, the d->clip part of it.
static int convert_current(decoder_t *d)
{
    const AVFrame *f = d->current;
    convert_t c;
    const size_t need = (size_t)d->clip[2] * d->clip[3] * 4;

    memset(&c, 0, sizeof(c));
    c.bits = frame_bits(f);
    if (!c.bits || d->clip[2] <= 0 || d->clip[3] <= 0)
        return 0;
    if (d->pixels_size < need) {
        free(d->pixels);
        d->pixels = malloc(need);
        d->pixels_size = d->pixels ? need : 0;
        if (!d->pixels)
            return 0;
    }
    int *columns = malloc(sizeof(int) * 4 * (size_t)d->clip[2]);
    if (!columns)
        return 0;
    c.frame = f;
    c.nv12 = f->format == AV_PIX_FMT_NV12;
    c.fw = f->width;
    c.fh = f->height;
    c.cw = (f->width + 1) / 2;
    c.ch = (f->height + 1) / 2;
    memcpy(c.place, d->place, sizeof(c.place));
    memcpy(c.clip, d->clip, sizeof(c.clip));
    c.out = d->pixels;
    c.lx = columns;
    c.lw = columns + d->clip[2];
    c.cx = columns + 2 * d->clip[2];
    c.cwt = columns + 3 * d->clip[2];
    for (int x = 0; x < d->clip[2]; x++) {
        const int px = d->clip[0] + x - d->place[0];
        sample_at(px, d->place[2], c.fw, &c.lx[x], &c.lw[x]);
        sample_at(px, d->place[2], c.cw, &c.cx[x], &c.cwt[x]);
    }
    c.ylast = c.lx[d->clip[2] - 1] + 1;  // the tables only go right
    c.clast = c.cx[d->clip[2] - 1] + 1;
    convert_matrix(&c, f);
    pool_run(convert_band, &c, d->clip[3]);
    free(columns);
    return 1;
}


// ---------------------------------------------------------------------------
// The mix: the screen over the video, where the screen is not opaque.

typedef struct mix {
    uint32_t *out;          // the screen, copied: mixed in place
    int stride;
    int hole[4];            // the video's whole place on the screen (clipped)
    const decoder_t *d;     // its picture, or 0: black
} mix_t;

// x / 255, rounded, for x up to 255 * 255.
static inline uint32_t div255(uint32_t x)
{
    x += 128;
    return (x + (x >> 8)) >> 8;
}

// The screen over the picture, premultiplied: out = s + v * (255 - a) / 255
// per color, opaque. No branches, so that it becomes vector code: an
// opaque screen pixel (a = 255) gives the video no share and stays as it
// is; a transparent one (s = 0) gives div255(v * 255) == v, the picture.
static void mix_span(uint32_t *restrict row, const uint32_t *restrict pic, int count)
{
    for (int x = 0; x < count; x++) {
        const uint32_t s = row[x], v = pic[x], k = 255 - (s >> 24);
        uint32_t r = ((s >> 16) & 255) + div255(((v >> 16) & 255) * k);
        uint32_t g = ((s >> 8) & 255) + div255(((v >> 8) & 255) * k);
        uint32_t b = (s & 255) + div255((v & 255) * k);
        r = r > 255 ? 255 : r;
        g = g > 255 ? 255 : g;
        b = b > 255 ? 255 : b;
        row[x] = 0xff000000u | r << 16 | g << 8 | b;
    }
}

// Over black (no picture there): the screen's pixel, made opaque.
static void mix_black(uint32_t *row, int count)
{
    for (int x = 0; x < count; x++)
        row[x] |= 0xff000000u;
}

static void mix_row(const mix_t *m, int sy)
{
    const decoder_t *d = m->d;
    uint32_t *row = m->out + (size_t)sy * m->stride;
    const int x0 = m->hole[0], x1 = m->hole[0] + m->hole[2];
    if (!(d && d->converted && sy >= d->clip[1] && sy < d->clip[1] + d->clip[3])) {
        mix_black(row + x0, x1 - x0);
        return;
    }
    const uint32_t *pic = d->pixels + (size_t)(sy - d->clip[1]) * d->clip[2];
    // the picture's columns inside the hole; black around them
    const int p0 = d->clip[0] > x0 ? d->clip[0] : x0;
    const int p1 = d->clip[0] + d->clip[2] < x1 ? d->clip[0] + d->clip[2] : x1;
    if (p1 <= p0) {
        mix_black(row + x0, x1 - x0);
        return;
    }
    mix_black(row + x0, p0 - x0);
    mix_span(row + p0, pic + (p0 - d->clip[0]), p1 - p0);
    mix_black(row + p1, x1 - p1);
}

typedef struct compose {
    uint32_t *out;
    const uint32_t *in;
    int width, stride;
    mix_t mixes[VIDEO_RING_SLOTS];
    int count;
} compose_t;

static void compose_band(void *arg, int y0, int y1)
{
    const compose_t *c = arg;
    for (int y = y0; y < y1; y++) {
        memcpy(c->out + (size_t)y * c->stride, c->in + (size_t)y * c->stride, (size_t)c->width * 4);
        for (int i = 0; i < c->count; i++) {
            const mix_t *m = &c->mixes[i];
            if (y >= m->hole[1] && y < m->hole[1] + m->hole[3])
                mix_row(m, y);
        }
    }
}

// ---------------------------------------------------------------------------
// The decoders' pictures, in direct memory.
//
// In a title, malloc() and anonymous mmap() take "flexible" memory, 448 MiB
// in all (poc/webkit_title/title_memory.c), and a 4K picture is ~12 MB: with
// the VP9 references, the frame threads' pictures and the queue, 4K would
// need ~30 of them. Direct memory has 12 GiB. So the decoders' pictures,
// nearly all of what they use, come from pools of direct memory blocks
// (get_buffer2, laid out as FFmpeg's own avcodec_default_get_buffer2()
// does); the rest stays with malloc(). On the PC (VIDEO_HOST_TEST) the
// blocks come from posix_memalign(), to test the rest.

#define BLOCK_PAGE ((size_t)16384)

typedef struct block {
    size_t length;
    off_t phys;   // direct memory; -1 from malloc
} block_t;

#ifndef VIDEO_HOST_TEST
int sceKernelAllocateDirectMemory(off_t start, off_t end, size_t len, size_t alignment, int type, off_t *phys);
int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, off_t phys, size_t alignment);
int sceKernelReleaseDirectMemory(off_t phys, size_t len);
size_t sceKernelGetDirectMemorySize(void);
#endif

static void block_free(void *opaque, uint8_t *data)
{
    block_t *b = opaque;

#ifndef VIDEO_HOST_TEST
    if (b->phys >= 0) {
        munmap(data, b->length);
        sceKernelReleaseDirectMemory(b->phys, b->length);
    } else
#endif
        free(data);
    __atomic_fetch_sub(&video.frame_bytes, b->length, __ATOMIC_RELAXED);
    free(b);
}

static void frame_bytes_add(size_t length)
{
    const uint64_t now = __atomic_add_fetch(&video.frame_bytes, length, __ATOMIC_RELAXED);
    uint64_t peak = __atomic_load_n(&video.frame_peak, __ATOMIC_RELAXED);
    while (now > peak
           && !__atomic_compare_exchange_n(&video.frame_peak, &peak, now, 1, __ATOMIC_RELAXED,
                                           __ATOMIC_RELAXED))
        ;
}

// A block of size bytes for a pool: direct memory, else malloc.
static AVBufferRef *block_alloc(void *opaque, size_t size)
{
    (void)opaque;
    block_t *b = malloc(sizeof(*b));
    if (!b)
        return NULL;
    b->length = (size + BLOCK_PAGE - 1) & ~(BLOCK_PAGE - 1);
    b->phys = -1;
    void *at = NULL;
#ifndef VIDEO_HOST_TEST
    static size_t total;
    static int warned;
    if (!total)
        total = sceKernelGetDirectMemorySize();
    off_t phys;
    int err = sceKernelAllocateDirectMemory(0, (off_t)total, b->length, BLOCK_PAGE, 0, &phys);
    if (!err) {
        err = sceKernelMapDirectMemory(&at, b->length, PROT_READ | PROT_WRITE, 0, phys, BLOCK_PAGE);
        if (err) {
            sceKernelReleaseDirectMemory(phys, b->length);
            at = NULL;
        } else {
            b->phys = phys;
        }
    }
    if (!at && !__atomic_fetch_add(&warned, 1, __ATOMIC_RELAXED))
        VLOG("pictures: %zu KiB of direct memory: 0x%x; malloc instead", b->length >> 10, (unsigned)err);
#endif
    if (!at) {
        if (posix_memalign(&at, BLOCK_PAGE, b->length)) {
            free(b);
            return NULL;
        }
#ifndef VIDEO_HOST_TEST
        __atomic_fetch_add(&video.frame_fallbacks, 1, __ATOMIC_RELAXED);  // on the PC, malloc is the plan
#endif
    }
    AVBufferRef *ref = av_buffer_create(at, size, block_free, b, 0);
    if (!ref) {
#ifndef VIDEO_HOST_TEST
        if (b->phys >= 0) {
            munmap(at, b->length);
            sceKernelReleaseDirectMemory(b->phys, b->length);
        } else
#endif
            free(at);
        free(b);
        return NULL;
    }
    frame_bytes_add(b->length);
    return ref;
}

static void pools_free(decoder_t *d)
{
    for (int i = 0; i < 4; i++)
        av_buffer_pool_uninit(&d->pools[i]);  // the blocks go when their pictures do
    d->pool_format = -1;
}

// The pools for pictures of this format and size, as FFmpeg's default
// lays them out (libavcodec/get_buffer.c): the codec's alignment, lines
// whose sizes it accepts, and room past each plane for reads ahead.
static int pools_make(decoder_t *d, AVCodecContext *ctx, const AVFrame *frame)
{
    int w = frame->width, h = frame->height;
    int align[AV_NUM_DATA_POINTERS];
    int linesize[4];

    pools_free(d);
    avcodec_align_dimensions2(ctx, &w, &h, align);
    for (int unaligned = 1; unaligned;) {
        if (av_image_fill_linesizes(linesize, frame->format, w) < 0)
            return 0;
        w += w & ~(w - 1);
        unaligned = 0;
        for (int i = 0; i < 4; i++)
            unaligned |= linesize[i] % align[i];
    }
    ptrdiff_t lines[4];
    size_t sizes[4];
    for (int i = 0; i < 4; i++)
        lines[i] = linesize[i];
    if (av_image_fill_plane_sizes(sizes, frame->format, h, lines) < 0)
        return 0;
    for (int i = 0; i < 4; i++) {
        d->pool_linesize[i] = linesize[i];
        if (sizes[i] && !(d->pools[i] = av_buffer_pool_init2(sizes[i] + 16 + 64 - 1, d, block_alloc, NULL))) {
            pools_free(d);
            return 0;
        }
    }
    d->pool_format = frame->format;
    d->pool_width = frame->width;
    d->pool_height = frame->height;
    return 1;
}

static int get_buffer(AVCodecContext *ctx, AVFrame *frame, int flags)
{
    decoder_t *d = ctx->opaque;

    if (!frame_bits(frame) || frame->width <= 0 || frame->height <= 0)
        return avcodec_default_get_buffer2(ctx, frame, flags);
    pthread_mutex_lock(&d->pool_lock);
    if ((frame->format != d->pool_format || frame->width != d->pool_width
         || frame->height != d->pool_height)
        && !pools_make(d, ctx, frame)) {
        pthread_mutex_unlock(&d->pool_lock);
        return avcodec_default_get_buffer2(ctx, frame, flags);
    }
    for (int i = 0; i < 4 && d->pools[i]; i++) {
        if (!(frame->buf[i] = av_buffer_pool_get(d->pools[i]))) {
            pthread_mutex_unlock(&d->pool_lock);
            for (int j = 0; j < i; j++)
                av_buffer_unref(&frame->buf[j]);
            memset(frame->data, 0, sizeof(frame->data));
            return AVERROR(ENOMEM);
        }
        frame->data[i] = frame->buf[i]->data;
        frame->linesize[i] = d->pool_linesize[i];
    }
    pthread_mutex_unlock(&d->pool_lock);
    frame->extended_data = frame->data;
    return 0;
}


// ---------------------------------------------------------------------------
// The decoders.

static double now_ms(void)
{
    return (double)video_ring_now_ns() / 1e6;
}

static void queue_clear(decoder_t *d)
{
    pthread_mutex_lock(&d->lock);
    for (int i = 0; i < d->count; i++)
        av_frame_free(&d->queue[i].frame);
    d->count = 0;
    pthread_mutex_unlock(&d->lock);
}

static void decoder_close(decoder_t *d)
{
    if (d->ctx)
        avcodec_free_context(&d->ctx);
    pthread_mutex_lock(&d->pool_lock);
    pools_free(d);
    pthread_mutex_unlock(&d->pool_lock);
    d->codec = VIDEO_CODEC_NONE;
}

static const char *codec_name(uint32_t codec)
{
    switch (codec) {
    case VIDEO_CODEC_VP9: return "VP9";
    case VIDEO_CODEC_H264: return "H.264";
    case VIDEO_CODEC_HEVC: return "HEVC";
    case VIDEO_CODEC_VP8: return "VP8";
    default: return "?";
    }
}

static int decoder_open(decoder_t *d, const video_format_t *format, const uint8_t *config)
{
    enum AVCodecID id;

    decoder_close(d);
    switch (format->codec) {
    case VIDEO_CODEC_VP9: id = AV_CODEC_ID_VP9; break;
    case VIDEO_CODEC_H264: id = AV_CODEC_ID_H264; break;
    case VIDEO_CODEC_HEVC: id = AV_CODEC_ID_HEVC; break;
    case VIDEO_CODEC_VP8: id = AV_CODEC_ID_VP8; break;
    default:
        VLOG("slot %d: codec %u unknown", d->index, format->codec);
        return 0;
    }
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec || !(d->ctx = avcodec_alloc_context3(codec))) {
        VLOG("slot %d: no %s decoder", d->index, codec_name(format->codec));
        return 0;
    }
    d->ctx->thread_count = video.threads;
    d->ctx->thread_type = video.thread_type;
    d->ctx->opaque = d;
    d->ctx->get_buffer2 = get_buffer;  // pictures in direct memory
    d->ctx->pkt_timebase = (AVRational){1, 1000000000};
    d->ctx->width = (int)format->width;
    d->ctx->height = (int)format->height;
    if (format->config_size) {
        d->ctx->extradata = av_mallocz(format->config_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (d->ctx->extradata) {
            memcpy(d->ctx->extradata, config, format->config_size);
            d->ctx->extradata_size = (int)format->config_size;
        }
    }
    int err = avcodec_open2(d->ctx, codec, NULL);
    if (err < 0) {
        VLOG("slot %d: avcodec_open2 %s: %d", d->index, codec_name(format->codec), err);
        avcodec_free_context(&d->ctx);
        return 0;
    }
    d->codec = format->codec;
    VLOG("slot %d: %s %ux%u, %d threads (%s), %u bytes of codec data", d->index,
         codec_name(format->codec), format->width, format->height, video.threads,
         video.thread_type == FF_THREAD_SLICE ? "slices" : "frames", format->config_size);
    return 1;
}

// A decoded picture into the queue.
static void queue_picture(decoder_t *d, AVFrame *frame)
{
    {
        __atomic_fetch_add(&d->slot->decoded, 1, __ATOMIC_RELAXED);
        d->slot->width = (uint32_t)frame->width;
        d->slot->height = (uint32_t)frame->height;
        if (!frame_bits(frame)) {
            if (!d->odd_format++)
                VLOG("slot %d: pictures in format %d, not shown", d->index, frame->format);
            av_frame_unref(frame);
            return;
        }
        // Before a seek's place (a negative running time, ps5videosink's
        // chain()): decoded for the ones after it, never shown.
        const int64_t when = frame->pts == AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
        if (when < 0 && when != AV_NOPTS_VALUE) {
            av_frame_unref(frame);
            return;
        }
        AVFrame *keep = av_frame_alloc();
        if (!keep) {
            av_frame_unref(frame);
            return;
        }
        av_frame_move_ref(keep, frame);
        pthread_mutex_lock(&d->lock);
        if (d->count == (int)(sizeof(d->queue) / sizeof(d->queue[0]))) {  // the oldest goes
            av_frame_free(&d->queue[0].frame);
            memmove(&d->queue[0], &d->queue[1], sizeof(d->queue[0]) * (size_t)(d->count - 1));
            d->count--;
            __atomic_fetch_add(&d->slot->late, 1, __ATOMIC_RELAXED);
        }
        d->queue[d->count].frame = keep;
        d->queue[d->count].running_ns = keep->pts == AV_NOPTS_VALUE ? keep->best_effort_timestamp : keep->pts;
        d->queue[d->count].generation = d->generation;
        d->queue_serial = d->serial;
        d->count++;
        pthread_mutex_unlock(&d->lock);
    }
}

// The decoder's pictures into the queue (some may wait for the next packet).
static void decoder_receive(decoder_t *d, AVFrame *frame)
{
    int err;
    while ((err = avcodec_receive_frame(d->ctx, frame)) == 0)
        queue_picture(d, frame);
    if (err != AVERROR(EAGAIN) && err != AVERROR_EOF)
        __atomic_fetch_add(&d->errors, 1, __ATOMIC_RELAXED);
}

#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
#include "video_hw.h"

// d's video from the hardware to FFmpeg (from here; the next key frame
// decodes it right).
static void hw_to_ffmpeg(decoder_t *d, const char *why)
{
    hw_drop(d);
    d->hw = 0;
    hw.fallbacks++;
    d->hw_refused = 1;
    VLOG("slot %d: %s: FFmpeg takes the video", d->index, why);
    decoder_open(d, &d->format, d->config);
}
#else
void video_away(int away)  // (no hardware here: nothing to let go of)
{
    (void)away;
}
#endif

// d's video ended (or another came): the hardware back for the next one.
static void hw_leave(decoder_t *d)
{
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
    hw_drop(d);
#endif
    d->hw = 0;
}

static int queue_full(decoder_t *d)
{
    pthread_mutex_lock(&d->lock);
    const int full = d->count >= QUEUE;
    pthread_mutex_unlock(&d->lock);
    return full;
}

// The end of the stream: the pictures the decoder's threads still hold, as
// the queue has room for them (a seek or the video's end stops it); then
// the decoder starts afresh (the next packet is a key frame).
static void decoder_drain(decoder_t *d, AVFrame *frame)
{
    video_slot_t *slot = d->slot;
    int err = avcodec_send_packet(d->ctx, NULL);

    while (!err && !video.quit) {
        __atomic_store_n(&video.ring->consumer_ns, video_ring_now_ns(), __ATOMIC_RELEASE);
        if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != VIDEO_SLOT_ACTIVE
            || __atomic_load_n(&slot->serial, __ATOMIC_ACQUIRE) != d->serial
            || __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE) != d->generation)
            break;
        if (queue_full(d)) {
            usleep(IDLE_US);
            continue;
        }
        err = avcodec_receive_frame(d->ctx, frame);
        if (!err)
            queue_picture(d, frame);
    }
    avcodec_flush_buffers(d->ctx);
}

static void *decoder_thread(void *arg)
{
    decoder_t *d = arg;
    video_slot_t *slot = d->slot;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    if (!packet || !frame) {
        VLOG("slot %d: no memory for the decoder", d->index);
        av_packet_free(&packet);
        av_frame_free(&frame);
        return NULL;
    }
    while (!video.quit) {
        __atomic_store_n(&video.ring->consumer_ns, video_ring_now_ns(), __ATOMIC_RELEASE);
        if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != VIDEO_SLOT_ACTIVE) {
            if (d->ctx || d->hw) {
                VLOG("slot %d: done (%llu decoded, %llu shown, %llu late, %llu behind)%s", d->index,
                     (unsigned long long)slot->decoded, (unsigned long long)slot->shown,
                     (unsigned long long)slot->late, (unsigned long long)slot->behind,
                     d->hw ? " on the hardware decoder" : "");
                decoder_close(d);
                queue_clear(d);
                hw_leave(d);
            }
            usleep(10000);
            continue;
        }
        // A WebProcess that ended (a crash) with its video open: the slot
        // goes, and with it the picture (twice a second; only "no such
        // process" counts, not a refusal to look).
        const uint64_t now_ns = video_ring_now_ns();
        if (now_ns - d->pid_check_ns >= 500000000ull) {
            d->pid_check_ns = now_ns;
            const int32_t pid = slot->pid;
            uint32_t active = VIDEO_SLOT_ACTIVE;
            if (pid > 0 && kill(pid, 0) == -1 && errno == ESRCH
                && __atomic_compare_exchange_n(&slot->state, &active, VIDEO_SLOT_FREE, 0,
                                               __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                const int32_t none[4] = {0, 0, 0, 0};
                video_ring_put4(&slot->rect_seq, slot->rect, none);
                VLOG("slot %d: its writer, pid %d, has ended", d->index, pid);
                continue;
            }
        }
        const uint32_t serial = __atomic_load_n(&slot->serial, __ATOMIC_ACQUIRE);
        if (serial != d->serial) {  // another video
            decoder_close(d);
            queue_clear(d);
            hw_leave(d);
            d->hw_refused = 0;
            d->hw_resume = 0;
            d->serial = serial;
            d->generation = __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE);
            d->odd_format = 0;
            VLOG("slot %d: a video from pid %d", d->index, slot->pid);
        }
        const uint32_t generation = __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE);
        if (generation != d->generation) {  // a seek: what is held goes
            if (d->ctx)
                avcodec_flush_buffers(d->ctx);
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
            if (d->hw)
                hw_reset();
#endif
            queue_clear(d);
            d->generation = generation;
        }
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
        // 18:5x: the title is away (video_away): the hardware and its queues
        // go before the system may close the title. Not to FFmpeg (a 4K
        // video stayed on it at 25 a second after the pause): while away
        // its packets go undecoded (no decoder: dropped below), and back,
        // the hardware takes it again from its next key frame.
        if (d->hw && __atomic_load_n(&hw.away, __ATOMIC_ACQUIRE)) {
            hw_leave(d);
            d->hw_resume = 1;
            VLOG("slot %d: the title is away: the hardware let go, the video waits", d->index);
        } else if (d->hw_resume && !__atomic_load_n(&hw.away, __ATOMIC_ACQUIRE)) {
            d->hw_resume = 0;
            if (hw_take_video(d, &d->format, d->config)) {
                d->hw = 1;
            } else {
                decoder_open(d, &d->format, d->config);
            }
        }
#endif
        video_frame_t f;
        if (queue_full(d) || !video_slot_peek(slot, &f)) {
            usleep(IDLE_US);
            continue;
        }
#ifdef TITLE_RN
        // 2026-11-14: a frame the ring cannot hold (its writer broke the ring: two writers on a slot, one
        // that died half way). Its size went unchecked into the decoders' copies; a wrong one read past
        // the ring, on a thread whose crash takes the title, and the console, down. All that is in the
        // ring goes, and the video goes on from its next format or key frame.
        if (f.size > VIDEO_RING_MAX_FRAME || f.offset % VIDEO_RING_DATA + f.size > VIDEO_RING_DATA) {
            VLOG("slot %d: a frame of %u bytes at %llu does not fit the ring: what the ring holds is dropped",
                 d->index, f.size, (unsigned long long)(f.offset % VIDEO_RING_DATA));
            __atomic_fetch_add(&d->errors, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&slot->data_read, __atomic_load_n(&slot->data_written, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
            __atomic_store_n(&slot->frames_read, __atomic_load_n(&slot->frames_written, __ATOMIC_ACQUIRE),
                             __ATOMIC_RELEASE);
            usleep(IDLE_US);
            continue;
        }
#endif
        const uint8_t *bytes = video_slot_bytes(slot, &f);
        // The format whatever its generation: it says what the stream is, not
        // a picture, and a flush does not send it again. WebKit's start
        // after the end (the sink takes its slot anew) sends the caps, then
        // flushes before the first frame: the format came with the
        // generation before, and was dropped here, and with no decoder the
        // title showed nothing for that run of the clip (console, 2026-11-05
        // 11:49; sink_test "again").
        if (f.generation != d->generation && !(f.flags & VIDEO_FRAME_FORMAT)) {
            video_slot_release(slot, &f);
            continue;
        }
        if (f.flags & VIDEO_FRAME_FORMAT) {
            video_format_t format;
            if (f.size >= sizeof(format)) {
                memcpy(&format, bytes, sizeof(format));
#ifdef TITLE_RN
                // (2026-11-14: and a format no stream has is not taken)
                if (format.config_size > VIDEO_RING_MAX_CONFIG || format.width > 16384 || format.height > 16384)
                    format.config_size = UINT32_MAX;
#endif
                if (format.config_size <= f.size && sizeof(format) + format.config_size <= f.size) {
                    // kept: FFmpeg opens it if the hardware gives up
                    free(d->config);
                    d->config = format.config_size ? malloc(format.config_size) : NULL;
                    if (d->config)
                        memcpy(d->config, bytes + sizeof(format), format.config_size);
                    d->format = format;
                    if (format.config_size && !d->config)
                        d->format.config_size = 0;
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
                    if (!d->hw_refused && hw_take_video(d, &d->format, d->config)) {
                        decoder_close(d);  // (FFmpeg's, if it had one)
                        d->codec = format.codec;
                        d->hw = 1;
                    } else
#endif
                    {
                        d->hw = 0;
                        decoder_open(d, &d->format, d->config);
                    }
                }
            }
            video_slot_release(slot, &f);
            continue;
        }
        if (!d->ctx && !d->hw) {
            video_slot_release(slot, &f);
            continue;
        }
        if (f.flags & VIDEO_FRAME_DRAIN) {
            video_slot_release(slot, &f);
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
            if (d->hw) {  // the end: what is still in its pipeline is left there
                hw_reset();
                continue;
            }
#endif
            decoder_drain(d, frame);
            continue;
        }
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
        if (d->hw) {
            const int r = hw_decode(d, bytes, f.size, !!(f.flags & VIDEO_FRAME_KEY), f.running_ns);
            if (r == HW_WAIT) {  // every picture buffer taken: the display gives them back
                usleep(IDLE_US);
                continue;
            }
            if (r == HW_REFUSED) {  // this packet to FFmpeg too (still in the ring)
                hw_to_ffmpeg(d, "the hardware does not take this stream (10-bit, or past 4K)");
                continue;
            }
            video_slot_release(slot, &f);
            if (r == HW_FAIL) {
                __atomic_fetch_add(&d->errors, 1, __ATOMIC_RELAXED);
                if (++hw.fails >= 3)
                    hw_to_ffmpeg(d, "3 hardware errors in a row");
            } else {
                hw.fails = 0;
            }
            continue;
        }
#endif
        av_packet_unref(packet);
        packet->data = (uint8_t *)bytes;  // copied by avcodec_send_packet (no buf)
        packet->size = (int)f.size;
        packet->pts = f.running_ns;
        packet->dts = AV_NOPTS_VALUE;
        packet->flags = f.flags & VIDEO_FRAME_KEY ? AV_PKT_FLAG_KEY : 0;
        int err = avcodec_send_packet(d->ctx, packet);
        if (err == AVERROR(EAGAIN)) {
            decoder_receive(d, frame);
            err = avcodec_send_packet(d->ctx, packet);
        }
        packet->data = NULL;
        packet->size = 0;
        video_slot_release(slot, &f);
        if (err < 0 && err != AVERROR(EAGAIN))
            __atomic_fetch_add(&d->errors, 1, __ATOMIC_RELAXED);
        if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_INVALIDDATA)
            VLOG("slot %d: avcodec_send_packet: %d", d->index, err);
        decoder_receive(d, frame);
    }
    decoder_close(d);
    queue_clear(d);
    hw_leave(d);
    free(d->config);
    d->config = NULL;
    av_packet_free(&packet);
    av_frame_free(&frame);
    return NULL;
}


// ---------------------------------------------------------------------------
// Each flip.

// The picture to show now, if a newer one came: the queue's newest whose
// time has come (the older ones were late). Nothing shown yet for this
// video, or paused after a seek: the first that came.
static void take_picture(decoder_t *d, uint64_t now_ns)
{
    int playing;
    int64_t running = video_slot_running(d->slot, now_ns, &playing);
    if (playing && running != AV_NOPTS_VALUE) {
        const int64_t raw = running - (int64_t)now_ns;
        const int64_t gap = raw - d->clock_off;
        if (!d->clock_ok || gap > CLOCK_SNAP_NS || gap < -CLOCK_SNAP_NS) {
            d->clock_off = raw;
            d->clock_ok = 1;
        } else {
            const double g = (double)(gap < 0 ? -gap : gap) / 1e6;
            if (g > d->jitter_max)
                d->jitter_max = g;
            d->clock_off += gap / CLOCK_SMOOTH;
        }
        running = (int64_t)now_ns + d->clock_off;
    } else {
        d->clock_ok = 0;
    }

    pthread_mutex_lock(&d->lock);
    if (d->queue_serial != d->current_serial && d->current) {
        av_frame_free(&d->current);
        d->converted = 0;
    }
    int pick = -1;
    for (int i = 0; i < d->count; i++) {
        if (d->queue[i].running_ns <= running + AHEAD_NS)
            pick = i;
    }
    if (pick < 0 && d->count
        && (!d->current || (!playing && d->queue[0].generation != d->current_generation)))
        pick = 0;
    if (pick >= 0) {
        if (pick > 0)
            __atomic_fetch_add(&d->slot->late, (uint64_t)pick, __ATOMIC_RELAXED);
        // Shown, but behind its time: the decoder does not keep up.
        if (playing && d->queue[pick].running_ns != AV_NOPTS_VALUE
            && running - d->queue[pick].running_ns > VIDEO_BEHIND_NS)
            __atomic_fetch_add(&d->slot->behind, 1, __ATOMIC_RELAXED);
        for (int i = 0; i < pick; i++)
            av_frame_free(&d->queue[i].frame);
        av_frame_free(&d->current);
        d->current = d->queue[pick].frame;
        d->current_serial = d->queue_serial;
        d->current_generation = d->queue[pick].generation;
        d->picture_ns = now_ns;
        d->converted = 0;
        memmove(&d->queue[0], &d->queue[pick + 1], sizeof(d->queue[0]) * (size_t)(d->count - pick - 1));
        d->count -= pick + 1;
        __atomic_fetch_add(&d->slot->shown, 1, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&d->lock);
}

unsigned video_errors(int slot)
{
    if (slot < 0 || slot >= VIDEO_RING_SLOTS)
        return 0;
    return __atomic_load_n(&video.decoders[slot].errors, __ATOMIC_RELAXED);
}

int video_picture_ns(int slot, int64_t *running_ns)
{
    if (slot < 0 || slot >= VIDEO_RING_SLOTS || !video.decoders[slot].current)
        return 0;
    const AVFrame *f = video.decoders[slot].current;
    *running_ns = f->pts == AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    return 1;
}

static int intersect(int *r, const int *with)
{
    const int x0 = r[0] > with[0] ? r[0] : with[0];
    const int y0 = r[1] > with[1] ? r[1] : with[1];
    const int x1 = r[0] + r[2] < with[0] + with[2] ? r[0] + r[2] : with[0] + with[2];
    const int y1 = r[1] + r[3] < with[1] + with[3] ? r[1] + r[3] : with[1] + with[3];
    r[0] = x0;
    r[1] = y0;
    r[2] = x1 > x0 ? x1 - x0 : 0;
    r[3] = y1 > y0 ? y1 - y0 : 0;
    return r[2] > 0 && r[3] > 0;
}

const uint32_t *video_compose(const uint32_t *screen, int width, int height, int stride)
{
    if (!video.ring)
        return screen;
    const uint64_t now = video_ring_now_ns();
    int page[4];
    video_ring_get4(&video.ring->page_seq, video.ring->page, page);
    if (page[2] <= 0 || page[3] <= 0)
        return screen;
    const int screen_rect[4] = {0, 0, width, height};
    int view[4] = {page[0], page[1], page[2], page[3]};
    if (!intersect(view, screen_rect))
        return screen;

    int any = 0;
    int holes[VIDEO_RING_SLOTS][4];
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        decoder_t *d = &video.decoders[i];
        holes[i][2] = 0;
        if (__atomic_load_n(&d->slot->state, __ATOMIC_ACQUIRE) != VIDEO_SLOT_ACTIVE) {
            if (d->current) {  // its last picture: 12 MB at 4K
                av_frame_free(&d->current);
                d->converted = 0;
            }
            continue;
        }
        int rect[4];
        video_ring_get4(&d->slot->rect_seq, d->slot->rect, rect);
        if (rect[2] <= 0 || rect[3] <= 0)
            continue;
        int hole[4] = {page[0] + rect[0], page[1] + rect[1], rect[2], rect[3]};
        const int whole[4] = {hole[0], hole[1], hole[2], hole[3]};
        if (!intersect(hole, view))
            continue;
        memcpy(holes[i], hole, sizeof(hole));
        any = 1;
        take_picture(d, now);
        if (!d->current)
            continue;
        // Its place: as large as fits in the hole, with its shape.
        const AVFrame *f = d->current;
        double sar = f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0
            ? (double)f->sample_aspect_ratio.num / f->sample_aspect_ratio.den : 1.0;
        const double shape = (double)f->width * sar / (f->height > 0 ? f->height : 1);
        int place[4];
        if ((double)whole[2] / whole[3] > shape) {
            place[3] = whole[3];
            place[2] = (int)(whole[3] * shape + 0.5);
        } else {
            place[2] = whole[2];
            place[3] = (int)(whole[2] / shape + 0.5);
        }
        place[0] = whole[0] + (whole[2] - place[2]) / 2;
        place[1] = whole[1] + (whole[3] - place[3]) / 2;
        int clip[4] = {place[0], place[1], place[2], place[3]};
        if (!intersect(clip, hole)) {
            d->converted = 0;
            continue;
        }
        if (!d->converted || memcmp(place, d->place, sizeof(place)) || memcmp(clip, d->clip, sizeof(clip))) {
            memcpy(d->place, place, sizeof(place));
            memcpy(d->clip, clip, sizeof(clip));
            const double t0 = now_ms();
            d->converted = convert_current(d);
            video.convert_ms += now_ms() - t0;
            video.converts++;
        }
    }
    if (!any)
        return screen;

    const size_t need = (size_t)stride * height * 4;
    if (video.out_size < need) {
        free(video.out);
        video.out = malloc(need);
        video.out_size = video.out ? need : 0;
        if (!video.out)
            return screen;
    }
    const double t0 = now_ms();
    compose_t compose = {video.out, screen, width, stride, {{0}}, 0};
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        if (holes[i][2] <= 0)
            continue;
        const decoder_t *d = &video.decoders[i];
        mix_t m = {video.out, stride, {holes[i][0], holes[i][1], holes[i][2], holes[i][3]},
                   d->current ? d : NULL};
        // 2026-11-09 (the user's recording: a black box in the player's top
        // left corner): where holes overlap, the first mix makes the screen
        // opaque and wins. The page's holes say nothing of which video is on
        // top, and the sink keeps the last place of a video that WebKit no
        // longer draws (a YouTube ad, 640x360, paused after 2 pictures, in
        // slot 0 over the film in slot 1). So the video with the newest
        // picture goes first, and a video with no picture last.
        int at = compose.count++;
        while (at > 0) {
            const decoder_t *o = compose.mixes[at - 1].d;
            const uint64_t mine = m.d ? m.d->picture_ns + 1 : 0;
            const uint64_t theirs = o ? o->picture_ns + 1 : 0;
            if (theirs >= mine)
                break;
            compose.mixes[at] = compose.mixes[at - 1];
            at--;
        }
        compose.mixes[at] = m;
    }
    pool_run(compose_band, &compose, height);
    video.mix_ms += now_ms() - t0;
    video.mixes++;
    video.composed++;
    return video.out;
}


// ---------------------------------------------------------------------------

static void read_config(const char *path)
{
    FILE *file = path ? fopen(path, "r") : NULL;
    char line[256];

    if (!file)
        return;
    while (fgets(line, sizeof(line), file)) {
        unsigned w, h, fps;
        int n;
        if (sscanf(line, "threads=%d", &n) == 1 && n >= 1 && n <= 16) {
            video.threads = n;
        } else if (sscanf(line, "slice=%d", &n) == 1) {
            video.thread_type = n ? FF_THREAD_SLICE : FF_THREAD_FRAME | FF_THREAD_SLICE;
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
        } else if (sscanf(line, "hw=%d", &n) == 1) {
            hw.enabled = n != 0;
#endif
        } else if (sscanf(line, "max=%ux%u@%u", &w, &h, &fps) == 3 && w && h && fps) {
            video.ring->max_width = w;
            video.ring->max_height = h;
            video.ring->max_fps = fps;
        }
    }
    fclose(file);
}

int video_start(const char *config)
{
    shm_unlink(VIDEO_RING_SHM);  // a ring left by a title that was closed
    const int fd = shm_open(VIDEO_RING_SHM, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        VLOG("shm_open %s: %s", VIDEO_RING_SHM, strerror(errno));
        return -1;
    }
    void *map = MAP_FAILED;
    if (!ftruncate(fd, sizeof(video_ring_t)))
        map = mmap(NULL, sizeof(video_ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        VLOG("%s of %zu bytes: %s", VIDEO_RING_SHM, sizeof(video_ring_t), strerror(errno));
        shm_unlink(VIDEO_RING_SHM);
        return -1;
    }
    video.ring = map;
    memset(video.ring, 0, offsetof(video_ring_t, slots));
    for (int i = 0; i < VIDEO_RING_SLOTS; i++)
        memset(&video.ring->slots[i], 0, offsetof(video_slot_t, frames));
    video.ring->version = VIDEO_RING_VERSION;
    // 2026-11-09 16:3x: no limits on YouTube's video. This is
    // what WebKit is told the title decodes (WEBKIT_GST_VIDEO_DECODING_LIMIT),
    // so it is set past anything a site offers (8K, 120 fps); it was 1080p60.
    // /app0/video.txt's max= still sets it lower for a test.
    video.ring->max_width = 7680;
    video.ring->max_height = 4320;
    video.ring->max_fps = 120;
    video.threads = 8;
    video.thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    read_config(config);
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
    hw_load();  // the PS5's decoder and its compute queues, at the start (VLC, NK: later failed)
#endif

    pool_start();
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        decoder_t *d = &video.decoders[i];
        d->index = i;
        d->slot = &video.ring->slots[i];
        pthread_mutex_init(&d->lock, NULL);
        pthread_mutex_init(&d->pool_lock, NULL);
        d->pool_format = -1;
        // FFmpeg's threads are its own; this one needs little stack.
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        d->started = !pthread_create(&d->thread, &attr, decoder_thread, d);
        pthread_attr_destroy(&attr);
        if (!d->started)
            VLOG("no decoder thread %d: %s", i, strerror(errno));
    }
    __atomic_store_n(&video.ring->consumer_ns, video_ring_now_ns(), __ATOMIC_RELEASE);
    __atomic_store_n(&video.ring->magic, VIDEO_RING_MAGIC, __ATOMIC_RELEASE);
    VLOG("%s ready: %d slots of %u MiB, up to %ux%u@%u, %d threads (%s), %d mixing threads",
         VIDEO_RING_SHM, VIDEO_RING_SLOTS, VIDEO_RING_DATA >> 20, video.ring->max_width,
         video.ring->max_height, video.ring->max_fps, video.threads,
         video.thread_type == FF_THREAD_SLICE ? "slices" : "frames", pool.started ? WORKERS : 0);
    return 0;
}

// 2026-11-09 18:5x: a WebProcess ended (title.cpp, LINK_ENDED): its slots go now.
// The one in the title has the title's own pid, so kill(pid, 0) in the
// decoder thread never sees it gone: its 4K video kept the hardware decoder
// and the restarted page's video had to use FFmpeg.
void video_writer_ended(int32_t pid)
{
    if (!video.ring || pid <= 0)
        return;
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        video_slot_t *slot = &video.ring->slots[i];
        uint32_t active = VIDEO_SLOT_ACTIVE;
        if (__atomic_load_n(&slot->pid, __ATOMIC_ACQUIRE) != pid
            || !__atomic_compare_exchange_n(&slot->state, &active, VIDEO_SLOT_FREE, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        const int32_t none[4] = {0, 0, 0, 0};
        video_ring_put4(&slot->rect_seq, slot->rect, none);
        VLOG("slot %d: its writer, pid %d, has ended (told)", i, pid);
    }
}

int video_status(char *line, int size)
{
    if (!video.ring)
        return 0;
    int n = 0;
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        video_slot_t *s = &video.ring->slots[i];
        if (__atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != VIDEO_SLOT_ACTIVE)
            continue;
        const uint64_t shown = __atomic_load_n(&s->shown, __ATOMIC_RELAXED);
        n += snprintf(line + n, size > n ? (size_t)(size - n) : 0,
                      "%svideo %d: %ux%u, %llu decoded, %llu shown (+%llu), %llu late, %llu behind",
                      n ? "; " : "", i, s->width, s->height, (unsigned long long)s->decoded,
                      (unsigned long long)shown, (unsigned long long)(shown - video.status_shown[i]),
                      (unsigned long long)s->late,
                      (unsigned long long)__atomic_load_n(&s->behind, __ATOMIC_RELAXED));
        const unsigned errors = video_errors(i);
        if (errors)
            n += snprintf(line + n, size > n ? (size_t)(size - n) : 0, ", %u decoder errors", errors);
        if (video.decoders[i].hw)
            n += snprintf(line + n, size > n ? (size_t)(size - n) : 0, ", hardware");
        n += snprintf(line + n, size > n ? (size_t)(size - n) : 0, ", clock jitter %.1f ms",
                      video.decoders[i].jitter_max);
        video.decoders[i].jitter_max = 0;
#ifdef TITLE_RN
        {
            // 2026-11-14: where WebKit says the video is, and how many times it said another place
            // since the last line. An element with a backdrop-filter over the video (a player's
            // subtitles, its seek bar's label) made the place jump twice a frame, to one moved by the
            // element's own position: the video then showed in the wrong place, black where it should be.
            int rect[4];
            video_ring_get4(&s->rect_seq, s->rect, rect);
            const uint32_t seq = __atomic_load_n(&s->rect_seq, __ATOMIC_ACQUIRE) / 2;
            n += snprintf(line + n, size > n ? (size_t)(size - n) : 0, ", at %d,%d %dx%d (moved %u times)", rect[0],
                          rect[1], rect[2], rect[3], seq - video.status_moves[i]);
            video.status_moves[i] = seq;
        }
#endif
        video.status_shown[i] = shown;
    }
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST)
    if (n && n < size && (hw.pictures || hw.fallbacks))
    {
        n += snprintf(line + n, (size_t)(size - n),
                      "; hardware %u pictures, %u to FFmpeg; decode %.1f ms a frame (most %.1f, %u frames), %u waits, "
                      "lead %.0f ms (least %.0f)",
                      hw.pictures, hw.fallbacks, hw.calls ? hw.busy_ms / hw.calls : 0.0, hw.busy_max, hw.calls,
                      hw.waits, hw.leads ? hw.lead_sum / hw.leads : 0.0, hw.leads ? hw.lead_min : 0.0);
        hw.busy_ms = hw.busy_max = hw.lead_sum = hw.lead_min = 0;
        hw.calls = hw.waits = hw.leads = 0;
    }
#endif
    if (n && n < size) {
        n += snprintf(line + n, (size_t)(size - n), "; pictures %llu MiB (most %llu)%s",
                      (unsigned long long)(__atomic_load_n(&video.frame_bytes, __ATOMIC_RELAXED) >> 20),
                      (unsigned long long)(__atomic_load_n(&video.frame_peak, __ATOMIC_RELAXED) >> 20),
                      __atomic_load_n(&video.frame_fallbacks, __ATOMIC_RELAXED) ? ", some from malloc" : "");
    }
    if (n && video.mixes && n < size) {
        n += snprintf(line + n, (size_t)(size - n), "; convert %.2f ms (%u), mix %.2f ms per flip",
                      video.converts ? video.convert_ms / video.converts : 0.0, video.converts,
                      video.mix_ms / video.mixes);
        video.convert_ms = video.mix_ms = 0;
        video.converts = video.mixes = 0;
    }
    return n;
}

void video_stop(void)
{
    if (!video.ring)
        return;
    video.quit = 1;
    for (int i = 0; i < VIDEO_RING_SLOTS; i++) {
        decoder_t *d = &video.decoders[i];
        if (d->started)
            pthread_join(d->thread, NULL);
        av_frame_free(&d->current);
        free(d->pixels);
        d->pixels = NULL;
        d->pixels_size = 0;
    }
#if defined(VIDEO_HWDEC) && !defined(VIDEO_HOST_TEST) && defined(TITLE_RN)
    // 2026-11-14: the compute queues too. They only went when the title was away, so a title that ended
    // after a video (its own exit, or the close after a WebProcess's crash) ended with them still held.
    pthread_mutex_lock(&hw.owner_lock);
    if (!hw.owner && !hw.dec)
        hw_queues_down();
    pthread_mutex_unlock(&hw.owner_lock);
#endif
    pool_stop();
    __atomic_store_n(&video.ring->magic, 0, __ATOMIC_RELEASE);
    munmap(video.ring, sizeof(video_ring_t));
    video.ring = NULL;
    shm_unlink(VIDEO_RING_SHM);
    free(video.out);
    video.out = NULL;
    video.out_size = 0;
}
