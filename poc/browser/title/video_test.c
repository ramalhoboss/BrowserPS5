/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * video.c on a PC (Linux, a host FFmpeg; video_test.sh): this program is
 * the sink and the display. A thread writes an IVF clip into slot 0 of
 * the ring as ps5videosink would (the format, then the frames up to 400 ms
 * ahead of the clock); the main loop is the title's display: once every
 * 16.7 ms it makes a screen with a hole where the video goes, a half
 * transparent bar over the hole's bottom (YouTube's controls) and an
 * opaque box over its corner, and calls video_compose(). It writes two
 * screens as PPM and checks that the pictures came on time.
 *
 *   video_test CLIP.ivf OUT_PREFIX [MODE]
 *
 * MODE has any of these words, joined by commas: full: the page is the
 * whole screen and the video too (a video in full screen); paused: the
 * clock stands still at 0 (one picture, any speed); bench: once the
 * picture is there, the page moves by 2 rows on every flip, so that every
 * flip converts the picture again, and the conversion's time per flip is
 * measured (with paused: the same picture, 600 flips, no PPMs); cut: the
 * video starts 200 px left of the page, which cuts it.
 */
#include "video.h"
#include "video_ring.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define W 1920
#define H 1080
#define LEAD_NS 400000000LL

static video_ring_t *ring;
static uint64_t start_ns;
static int paused;
static volatile int writer_done;
static unsigned frames_pushed;

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void *writer(void *arg)
{
    const char *path = arg;
    FILE *f = fopen(path, "rb");
    unsigned char head[32];
    video_slot_t *slot = &ring->slots[0];

    if (!f || fread(head, 1, 32, f) != 32 || memcmp(head, "DKIF", 4)) {
        fprintf(stderr, "not an IVF: %s\n", path);
        exit(2);
    }
    const unsigned width = head[12] | head[13] << 8, height = head[14] | head[15] << 8;
    const uint32_t rate = rd32(head + 16), scale = rd32(head + 20);

    // Take the slot, as the sink does.
    __atomic_store_n(&slot->state, VIDEO_SLOT_TAKING, __ATOMIC_RELEASE);
    slot->pid = getpid();
    __atomic_fetch_add(&slot->serial, 1, __ATOMIC_ACQ_REL);
    video_slot_set_clock(slot, 0, start_ns, !paused);
    video_format_t format = {VIDEO_CODEC_VP9, width, height, 0};
    uint64_t at;
    if (!video_slot_room(slot, sizeof(format), &at)) {
        fprintf(stderr, "no room for the format\n");
        exit(2);
    }
    memcpy(slot->data + at % VIDEO_RING_DATA, &format, sizeof(format));
    video_slot_push(slot, at, sizeof(format), VIDEO_FRAME_FORMAT, 0);
    __atomic_store_n(&slot->state, VIDEO_SLOT_ACTIVE, __ATOMIC_RELEASE);

    unsigned char fh[12];
    static unsigned char buf[VIDEO_RING_MAX_FRAME];
    for (unsigned i = 0; fread(fh, 1, 12, f) == 12; i++) {
        const uint32_t size = rd32(fh);
        const uint64_t pts = rd32(fh + 4) | (uint64_t)rd32(fh + 8) << 32;
        if (size > sizeof(buf) || fread(buf, 1, size, f) != size)
            break;
        const int64_t running = (int64_t)(pts * 1000000000ull * scale / rate);
        for (;;) {  // no further than LEAD_NS ahead, as the sink's ts-offset
            const int64_t now = video_slot_running(slot, video_ring_now_ns(), NULL);
            if (running - now < LEAD_NS || paused)
                if (video_slot_room(slot, size, &at))
                    break;
            usleep(2000);
        }
        memcpy(slot->data + at % VIDEO_RING_DATA, buf, size);
        video_slot_push(slot, at, size, i == 0 ? VIDEO_FRAME_KEY : 0, running);
        frames_pushed++;
        if (paused && i == 24)  // past the frame threads' latency
            break;
    }
    fclose(f);
    writer_done = 1;
    return NULL;
}

static void write_ppm(const char *path, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: video_test CLIP.ivf OUT_PREFIX [full,paused,bench]\n");
        return 2;
    }
    const char *mode = argc > 3 ? argv[3] : "";
    const int full = strstr(mode, "full") != NULL;
    const int cut = strstr(mode, "cut") != NULL;  // the video starts 200 px left of the page
    const int bench = strstr(mode, "bench") != NULL;
    paused = strstr(mode, "paused") != NULL;
    if (video_start(getenv("VIDEO_CONFIG")))
        return 1;
    const int fd = shm_open(VIDEO_RING_SHM, O_RDWR, 0);
    ring = mmap(NULL, sizeof(video_ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED || !video_ring_ready(ring)) {
        fprintf(stderr, "no ring\n");
        return 1;
    }

    // The page below a 152-row bar, and the video's hole in it (page
    // pixels); in full screen both are the screen.
    const int page[4] = {0, full ? 0 : 152, W, full ? H : H - 152};
    const int rect[4] = {cut ? -200 : full ? 0 : 120, full ? 0 : 60, full ? W : 1280, full ? H : 720};
    video_ring_put4(&ring->page_seq, ring->page, page);
    video_ring_put4(&ring->slots[0].rect_seq, ring->slots[0].rect, rect);

    // The screen: grey, the hole transparent, a half transparent black bar
    // over the hole's bottom 80 rows, an opaque white box at its top left.
    uint32_t *screen = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            screen[y * W + x] = 0xff606060u;
    const int hx = page[0] + rect[0], hy = page[1] + rect[1];
    for (int y = hy < page[1] ? page[1] : hy; y < hy + rect[3] && y < H; y++)
        for (int x = hx < page[0] ? page[0] : hx; x < hx + rect[2] && x < W; x++) {
            uint32_t p = 0;
            if (y >= hy + rect[3] - 80)
                p = 0x80000000u;               // black at 50%, premultiplied
            if (y < hy + 60 && x < hx + 300)
                p = 0xffffffffu;               // opaque white
            screen[y * W + x] = p;
        }

    start_ns = video_ring_now_ns() + 300000000ull;  // the clock starts in 300 ms
    pthread_t t;
    pthread_create(&t, NULL, writer, argv[1]);

    const int flips = bench && paused ? 600 : paused ? 60 : 300;
    double worst = 0, sum = 0, bench_sum = 0, bench_worst = 0;
    unsigned bench_count = 0;
    char path[512], line[512];
    for (int i = 0; i < flips; i++) {
        // bench: from flip 60 (the picture is there), the page 2 rows lower
        // on odd flips: the picture's place moves, so it is converted again.
        const int moved = bench && i >= 60 && (i & 1);
        if (bench && i >= 60) {
            const int p[4] = {page[0], page[1] + (moved ? 2 : 0), page[2], page[3] - (moved ? 2 : 0)};
            video_ring_put4(&ring->page_seq, ring->page, p);
        }
        const uint64_t t0 = video_ring_now_ns();
        const uint32_t *out = video_compose(screen, W, H, W);
        const double ms = (double)(video_ring_now_ns() - t0) / 1e6;
        sum += ms;
        worst = ms > worst ? ms : worst;
        if (bench && i >= 60) {
            bench_sum += ms;
            bench_worst = ms > bench_worst ? ms : bench_worst;
            bench_count++;
        }
        if (!(bench && paused) && (i == flips / 3 || i == flips - 1)) {
            snprintf(path, sizeof(path), "%s-%d.ppm", argv[2], i);
            write_ppm(path, out);
        }
        if (i % 60 == 59 && video_status(line, sizeof(line)))
            printf("flip %d: %s\n", i + 1, line);
        const uint64_t next = t0 + 16666667ull;
        const uint64_t now = video_ring_now_ns();
        if (next > now)
            usleep((useconds_t)((next - now) / 1000));
    }
    const video_slot_t *slot = &ring->slots[0];
    printf("pushed %u, decoded %llu, shown %llu, late %llu, picture %ux%u; compose %.2f ms average, %.2f worst\n",
           frames_pushed, (unsigned long long)slot->decoded, (unsigned long long)slot->shown,
           (unsigned long long)slot->late, slot->width, slot->height, sum / flips, worst);
    if (bench_count)
        printf("bench: %ux%u into %dx%d, a conversion on every flip: compose %.2f ms average, %.2f worst (%u flips)\n",
               slot->width, slot->height, rect[2], rect[3], bench_sum / bench_count, bench_worst, bench_count);
    __atomic_store_n(&ring->slots[0].state, VIDEO_SLOT_FREE, __ATOMIC_RELEASE);
    usleep(50000);
    video_stop();
    return 0;
}
