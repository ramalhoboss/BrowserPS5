/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * capture.c on the PC (docs/09-video.md):
 *   gcc -O3 -mavx2 -DCAPTURE_HOST_TEST -DCAPTURE_RAM_MIB=256 -DCAPTURE_DIR='"DIR"' \
 *       capture_test.c capture.c -lpthread -lm
 * Two pieces of a moving test picture at ~60 flips a second with hiccups,
 * a still stretch and a 440 Hz tone in 256-frame blocks; a second piece
 * must be refused while the first is still being written; then the files
 * are checked (index, places, sizes, colours of a known patch). SLOW_DISK=1
 * makes the disk slow (a 25 MB/s pause in every write), as /data was. */
#include "capture.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void say(const char *line) { fputs(line, stdout); }
static volatile int quit;

static void *sound(void *arg)
{
    (void)arg;
    int16_t block[512];
    unsigned long n = 0;
    while (!quit) {
        for (int i = 0; i < 256; i++, n++) {
            const int16_t s = (int16_t)(8000 * sin(2 * M_PI * 440 * n / 48000.0));
            block[2 * i] = block[2 * i + 1] = s;
        }
        capture_sound(block, 256);
        usleep(5333);
    }
    return NULL;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int piece(const char *name, int seconds)
{
    static uint32_t px[1920 * 1080];
    if (capture_begin(name, seconds))
        return 1;
    const double start = now_s();
    for (int f = 0; capture_running(); f++) {
        /* Still from 1 s to 2 s: the same frame, which the index fills. */
        const double t = now_s() - start;
        const int moving = !(t > 1 && t < 2);
        if (moving || f == 0)
            for (int y = 0; y < 1080; y++)
                for (int x = 0; x < 1920; x++)
                    px[y * 1920 + x] = 0xff000000u | ((uint32_t)((x + f * 8) & 0xff) << 16) |
                                       ((uint32_t)(y & 0xff) << 8) | 0x40;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                px[y * 1920 + x] = 0xffff0000u;
                px[(y + 64) * 1920 + x] = 0xffffffffu;
            }
        if (moving)
            capture_frame(px, 1920, 1080, 1920);
        usleep(f % 50 == 49 ? 120000 : 16666);  /* a hiccup now and then */
    }
    return 0;
}

static int check(const char *name)
{
    char path[300];
    struct stat st;
    const long frame = 1920 * 1080 * 3 / 2;
    snprintf(path, sizeof path, "%s/%s/video.yuv", CAPTURE_DIR, name);
    if (stat(path, &st))
        return printf("%s: no video\n", name), 1;
    const long frames = st.st_size / frame;
    snprintf(path, sizeof path, "%s/%s/index.txt", CAPTURE_DIR, name);
    FILE *index = fopen(path, "r");
    long lines = 0, last = -1, ordered = 1;
    unsigned long long place;
    while (index && fscanf(index, "%llu", &place) == 1) {
        if ((long)place <= last)
            ordered = 0;
        last = (long)place;
        lines++;
    }
    if (index)
        fclose(index);
    snprintf(path, sizeof path, "%s/%s/info.txt", CAPTURE_DIR, name);
    FILE *info = fopen(path, "r");
    char text[1000] = "";
    if (info) {
        text[fread(text, 1, sizeof text - 1, info)] = 0;
        fclose(info);
    }
    long places = -1;
    const char *p = strstr(text, "places ");
    if (p)
        places = atol(p + 7);
    snprintf(path, sizeof path, "%s/%s/sound.s16", CAPTURE_DIR, name);
    stat(path, &st);
    const double sound_s = st.st_size / 4 / 48000.0;
    printf("%s: %ld frames (%.2f exact), %ld index lines (%s), last place %ld of %ld places, sound %.3f s\n",
           name, frames, frames ? (double)frames : 0.0, lines, ordered ? "in order" : "OUT OF ORDER", last, places,
           sound_s);
    snprintf(path, sizeof path, "%s/%s/video.yuv", CAPTURE_DIR, name);
    FILE *v = fopen(path, "rb");
    static uint8_t buf[1920 * 1080 * 3 / 2];
    if (!v || fread(buf, 1, sizeof buf, v) != sizeof buf)
        return 1;
    fclose(v);
    const uint8_t *U = buf + 1920 * 1080, *V = U + 960 * 540;
    printf("  red: Y %d U %d V %d (expect 63 102 240); white: Y %d U %d V %d (expect 235 128 128)\n",
           buf[10 * 1920 + 10], U[5 * 960 + 5], V[5 * 960 + 5], buf[100 * 1920 + 10], U[50 * 960 + 5],
           V[50 * 960 + 5]);
    const int ok = frames == lines && ordered && last < places && places > 0 &&
                   fabs(sound_s - places / 30.0) < 0.2 && buf[10 * 1920 + 10] == 63 && U[5 * 960 + 5] == 102 &&
                   V[5 * 960 + 5] == 240;
    printf("  %s\n", ok ? "PASS" : "FAIL");
    return !ok;
}

int main(void)
{
    capture_set_report(say);
    pthread_t t;
    pthread_create(&t, NULL, sound, NULL);
    int failed = piece("test-1", 4);
    capture_end();
    /* Still writing: a new piece must wait. */
    /* (With a fast disk the piece may be written already.) */
    const int slow = getenv("SLOW_DISK") != NULL, busy = capture_busy();
    const int refused = capture_begin("test-2", 2) != 0;
    printf("second piece while %s: %s\n", busy ? "writing" : "idle", refused ? "refused" : "started");
    if (refused != busy || (slow && !refused))
        failed = 1;
    if (!refused)
        capture_end();
    const double wait = now_s();
    while (capture_busy() && now_s() - wait < 120)
        usleep(10000);
    printf("written %.2f s after the end; done %u\n", now_s() - wait, capture_done());
    failed |= piece("test-2", 3);
    while (capture_busy())
        usleep(10000);
    quit = 1;
    pthread_join(t, NULL);
    failed |= check("test-1");
    failed |= check("test-2");
    const unsigned want = refused ? 2 : 3;
    printf("done %u of %u: %s\n", capture_done(), want, failed || capture_done() != want ? "FAIL" : "PASS");
    return failed;
}
