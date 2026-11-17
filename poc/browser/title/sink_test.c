/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * ps5videosink.c and video.c together on a PC (Linux, a host GStreamer and
 * FFmpeg; video_test.sh): the WebProcess's sink in a real pipeline, the
 * title's display reading what it sends. A thread pushes an IVF clip's
 * frames into the sink, which syncs them on the pipeline's clock 400 ms
 * early, as in WebKit; the main loop is the title, once every 16.7 ms.
 * On the way: a pause of half a second, then a seek to 1.5 s, between two
 * key frames (a flush, as playbin's seeks; the frames come again from the
 * key frame before, with the segment starting at 1.5 s, as WebKit's MSE
 * and the demuxers do). It measures how far the picture shown is from the
 * pipeline's clock, that no picture from before the seek's place shows and
 * that the decoder has no errors, and writes four screens as PPM. At the
 * end, the sink's "stats" (what WebKit gives the page as
 * getVideoPlaybackQuality()): few frames dropped. The sink gets WebKit's
 * "qos" and "max-lateness" (100 ms), as in createVideoSink().
 *
 *   sink_test CLIP.ivf OUT_PREFIX [slow|stall|again]
 *
 * "slow" is for a title that cannot keep up (a 4K clip with VIDEO_CONFIG's
 * threads=1): then only the stats are checked, and most frames must count
 * as dropped, so a site chooses a lower quality.
 * "stall": the frames stop for 0.7 s after the 60th (a busy WebProcess, as
 * at the console's start on 2026-11-05 11:49). None may be dropped on the
 * way: the title must decode them all, with no errors.
 * "again": the seek is a new start instead, as WebKit's after the end: the
 * pipeline to NULL (GstBaseSink stops: the sink lets its slot go) and back to PLAYING (it
 * takes one again), the caps, then a flush before the first frame. The
 * title must still open its decoder (on the console it lost the caps to
 * that flush, and showed nothing for that run of the clip).
 */
#include "ps5audiosink.h"
#include "video.h"
#include "video_ring.h"

#include <gst/base/gstbasesink.h>
#include <gst/gst.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define W 1920
#define H 1080
#define FLIPS 480
#define PAUSE_AT 150
#define PLAY_AT 180
#define PAUSE_AHEAD_NS 8000000  // video.c's AHEAD_NS
#define SEEK_AT 240
#define END_FRAMES 180   // after the seek: 3 s of the clip from its place, then the end
#define SEEK_FRAME 90    // the seek's place: 1.5 s, between the key frames at 0 and 4 s
#define STALL_AFTER 60   // "stall": the frames stop after this one...
#define STALL_US 700000  // ...for this long (more than the 400 ms lead and WebKit's 100 ms)

// What a feeder sends.
enum { FEED_SEEK, FEED_FIRST, FEED_AGAIN };

static GstPad *src;
static FILE *clip;
static unsigned clip_w, clip_h;
static uint32_t rate, scale;
static unsigned pushed;
static int stall;

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// A VP9 frame's first byte: a key frame (frame_marker 2, profile 0 to 2,
// not show_existing_frame, frame_type 0)?
static int vp9_key(const unsigned char *data, size_t size)
{
    return size && (data[0] & 0xc0) == 0x80 && !(data[0] & 0x08) && !(data[0] & 0x04);
}

// The clip from its start into the sink, as a demuxer would; the first run
// sends the stream's start and its caps. After the seek, the segment starts
// at SEEK_FRAME, and the frames before it come too (from the key frame).
// FEED_AGAIN: a new start (stream start, caps), then a flush before the
// segment, from 0, and the frames.
static void *feeder(void *arg)
{
    const int feed = (int)(intptr_t)arg;
    if (feed != FEED_SEEK) {
        gst_pad_push_event(src, gst_event_new_stream_start("sink-test"));
        GstCaps *caps = gst_caps_new_simple("video/x-vp9", "width", G_TYPE_INT, (int)clip_w,
                                            "height", G_TYPE_INT, (int)clip_h, NULL);
        gst_pad_push_event(src, gst_event_new_caps(caps));
        gst_caps_unref(caps);
    }
    if (feed == FEED_AGAIN) {
        gst_pad_push_event(src, gst_event_new_flush_start());
        gst_pad_push_event(src, gst_event_new_flush_stop(TRUE));
    }
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    if (feed == FEED_SEEK)
        segment.start = segment.time = (guint64)SEEK_FRAME * GST_SECOND * scale / rate;
    gst_pad_push_event(src, gst_event_new_segment(&segment));
    fseek(clip, 32, SEEK_SET);
    unsigned char fh[12];
    // After the seek: 3 s of the clip, then its end (the title must show
    // the last picture, which its decoder's threads held). After a new
    // start: as many frames from 0, so the last has the same running time.
    const unsigned frames = feed == FEED_FIRST ? ~0u : feed == FEED_SEEK ? SEEK_FRAME + END_FRAMES : END_FRAMES;
    for (unsigned i = 0; i < frames && fread(fh, 1, 12, clip) == 12; i++) {
        if (stall && feed == FEED_FIRST && i == STALL_AFTER) {
            printf("stall: no frames for %d ms after frame %d\n", STALL_US / 1000, STALL_AFTER);
            usleep(STALL_US);
        }
        const uint32_t size = rd32(fh);
        const uint64_t pts = rd32(fh + 4) | (uint64_t)rd32(fh + 8) << 32;
        GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_WRITE);
        const size_t got = fread(map.data, 1, size, clip);
        const int key = vp9_key(map.data, got);
        gst_buffer_unmap(buffer, &map);
        if (got != size) {
            gst_buffer_unref(buffer);
            break;
        }
        GST_BUFFER_PTS(buffer) = pts * GST_SECOND * scale / rate;
        GST_BUFFER_DURATION(buffer) = GST_SECOND * scale / rate;
        if (!key)
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        const GstFlowReturn ret = gst_pad_push(src, buffer);
        if (ret != GST_FLOW_OK) {
            printf("feeder: %s at frame %u\n", gst_flow_get_name(ret), i);
            return NULL;
        }
        __atomic_fetch_add(&pushed, 1, __ATOMIC_RELAXED);
    }
    gst_pad_push_event(src, gst_event_new_eos());
    printf("feeder: the end\n");
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

// The pipeline's running time now, if it plays.
static int pipeline_running(GstElement *pipeline, int64_t *running)
{
    GstState state, pending;
    if (gst_element_get_state(pipeline, &state, &pending, 0) != GST_STATE_CHANGE_SUCCESS
        || state != GST_STATE_PLAYING)
        return 0;
    GstClock *clock = gst_pipeline_get_clock(GST_PIPELINE(pipeline));
    if (!clock)
        return 0;
    const GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);
    *running = (int64_t)(now - gst_element_get_base_time(pipeline));
    return 1;
}

// The sink's "stats", as WebKit reads them for getVideoPlaybackQuality().
static void sink_stats(GstElement *sink, guint64 *rendered, guint64 *dropped)
{
    GstStructure *stats = NULL;
    *rendered = *dropped = 0;
    g_object_get(sink, "stats", &stats, NULL);
    if (stats) {
        gst_structure_get_uint64(stats, "rendered", rendered);
        gst_structure_get_uint64(stats, "dropped", dropped);
        gst_structure_free(stats);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: sink_test CLIP.ivf OUT_PREFIX [slow|stall|again]\n");
        return 2;
    }
    const int slow = argc > 3 && !strcmp(argv[3], "slow");
    const int again = argc > 3 && !strcmp(argv[3], "again");
    stall = argc > 3 && !strcmp(argv[3], "stall");
    setvbuf(stdout, NULL, _IOLBF, 0);  // in order with the sink's and the title's lines (stderr)
    gst_init(&argc, &argv);
    if (video_start(getenv("VIDEO_CONFIG")))
        return 1;
    const int fd = shm_open(VIDEO_RING_SHM, O_RDWR, 0);
    video_ring_t *ring = mmap(NULL, sizeof(video_ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED || !video_ring_ready(ring)) {
        fprintf(stderr, "no ring\n");
        return 1;
    }
    unsigned char head[32];
    clip = fopen(argv[1], "rb");
    if (!clip || fread(head, 1, 32, clip) != 32 || memcmp(head, "DKIF", 4)) {
        fprintf(stderr, "not an IVF: %s\n", argv[1]);
        return 2;
    }
    clip_w = head[12] | head[13] << 8;
    clip_h = head[14] | head[15] << 8;
    rate = rd32(head + 16);
    scale = rd32(head + 20);

    // The UIProcess's part: the page below a 152-row bar.
    const int page[4] = {0, 152, W, H - 152};
    const int rect[4] = {120, 60, 1280, 720};
    video_ring_put4(&ring->page_seq, ring->page, page);

    // WebKit's part: the sink, its rectangle, a pipeline.
    browserps5_video_sink_register();
    GstElement *pipeline = gst_pipeline_new("pipeline");
    GstElement *sink = gst_element_factory_make("westerossink", "sink");
    if (!sink) {
        fprintf(stderr, "no westerossink\n");
        return 1;
    }
    g_object_set(sink, "rectangle", "120,60,1280,720", "zorder", 0.0f, NULL);
    // As WebKit's createVideoSink() does to every video sink.
    g_object_set(sink, "qos", TRUE, "max-lateness", (gint64)(100 * GST_MSECOND), NULL);
    gboolean qos = TRUE;
    gint64 max_lateness = 0;
    g_object_get(sink, "qos", &qos, "max-lateness", &max_lateness, NULL);
    printf("after WebKit's settings: qos %s, max-lateness %lld\n", qos ? "on" : "off",
           (long long)max_lateness);
    // As playbin3's playsink does when the stream has sound too
    // (update_av_offset(), "av-offset" 0): "ts-offset" 0 on the video sink.
    // Before 2026-11-05 16:40 that took away the sink's 400 ms lead, and
    // every picture reached the title at its time, ~130 ms before its
    // decoder's threads gave it back: all of them "behind" (the console's
    // sound + video MSE test at 16:22: the page saw 100% dropped).
    g_object_set(sink, "ts-offset", (gint64)0, NULL);
    gint64 ts_offset = -1;
    g_object_get(sink, "ts-offset", &ts_offset, NULL);
    printf("after playsink's \"ts-offset\" 0: it reads %lld, the sink syncs %lld ms early\n",
           (long long)ts_offset, (long long)(-gst_base_sink_get_ts_offset(GST_BASE_SINK(sink)) / GST_MSECOND));
    gst_bin_add(GST_BIN(pipeline), sink);
    src = gst_pad_new("src", GST_PAD_SRC);
    gst_pad_set_active(src, TRUE);
    GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
    if (gst_pad_link(src, sink_pad) != GST_PAD_LINK_OK) {
        fprintf(stderr, "link failed\n");
        return 1;
    }
    gst_object_unref(sink_pad);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    pthread_t feed;
    pthread_create(&feed, NULL, feeder, (void *)(intptr_t)FEED_FIRST);

    // The page as video_test makes it: grey, the hole, the controls' bar,
    // an opaque box.
    uint32_t *screen = malloc((size_t)W * H * 4);
    for (int i = 0; i < W * H; i++)
        screen[i] = 0xff606060u;
    const int hx = page[0] + rect[0], hy = page[1] + rect[1];
    for (int y = hy; y < hy + rect[3]; y++)
        for (int x = hx; x < hx + rect[2]; x++)
            screen[y * W + x] = y >= hy + rect[3] - 80 ? 0x80000000u
                                : y < hy + 60 && x < hx + 300 ? 0xffffffffu : 0;

    const video_slot_t *slot = &ring->slots[0];
    double worst = 0, sum = 0;
    double sync_min = 1e9, sync_max = -1e9, sync_sum = 0;
    unsigned sync_count = 0, settle = 0, generation = 0;
    int seek_flip = -1, seek_shown = -1, caught_up = -1, flash = 0;
    double seek_lag = 1e9;  // the clock ahead of the first picture after the seek, ms
    uint64_t shown_at_pause = 0;
    int64_t picture_at_pause = -1, paused_clock = -1, picture;
    int pause_ok = -1, paused_playing = 1;
    int again_serial = 0;  // "again": the sink took a slot anew
    guint64 steady_dropped[2] = {0, 0};  // the sink's "dropped" at flips 60 and 120
    char path[512], line[512];
    for (int i = 0; i < FLIPS; i++) {
        const uint64_t t0 = video_ring_now_ns();
        const uint32_t *out = video_compose(screen, W, H, W);
        const double ms = (double)(video_ring_now_ns() - t0) / 1e6;
        sum += ms;
        worst = ms > worst ? ms : worst;

        // How far behind the pipeline's clock the picture is, once settled
        // (half a second after a start or a seek).
        int64_t running;
        if (slot->generation != generation) {  // a seek: settle again
            generation = slot->generation;
            settle = 0;
        }
        // After the seek: the pictures from before its place are decoded
        // but never shown (a "flash" if one is); the first from its place,
        // then how long until the pictures are on time again (the title
        // decodes the 90 frames before the place first).
        if (seek_flip >= 0 && seek_shown < 0 && video_picture_ns(0, &picture)) {
            if (picture < 0) {
                if (!flash++)
                    printf("seek: a picture from before the seek's place shows (%.3f s)\n", picture / 1e9);
            } else if (picture < GST_SECOND) {
                seek_shown = i - seek_flip;
                int64_t clock_now = 0;
                if (pipeline_running(pipeline, &clock_now))
                    seek_lag = (double)(clock_now - picture) / 1e6;
                printf("seek: the first picture from its place (%.3f s after it) after %d flips (%.0f ms); "
                       "the clock (the sound) %.1f ms ahead of it\n",
                       picture / 1e9, seek_shown, seek_shown * 16.667, seek_lag);
            }
        }
        if (seek_shown >= 0 && caught_up < 0 && pipeline_running(pipeline, &running)
            && video_picture_ns(0, &picture) && running - picture < 20000000) {
            caught_up = i - seek_flip;
            printf("seek: on time again after %d flips (%.0f ms)\n", caught_up, caught_up * 16.667);
        }
        if (seek_flip >= 0 && caught_up < 0)
            settle = 0;
        if (pipeline_running(pipeline, &running) && video_picture_ns(0, &picture)
            && !(seek_flip >= 0 && running >= (int64_t)END_FRAMES * GST_SECOND / 60)) {
            if (++settle > 30) {
                const double diff = (double)(running - picture) / 1e6;
                sync_min = diff < sync_min ? diff : sync_min;
                sync_max = diff > sync_max ? diff : sync_max;
                sync_sum += diff;
                sync_count++;
            }
        } else {
            settle = 0;
        }

        // The pause: the clock stops where the sound stopped. The picture
        // due by then (less than PAUSE_AHEAD_NS ahead, as while playing)
        // may still come on the next flip; after that it holds.
        if (i == PAUSE_AT) {
            gst_element_set_state(pipeline, GST_STATE_PAUSED);
            shown_at_pause = slot->shown;
            paused_clock = video_slot_running(slot, video_ring_now_ns(), &paused_playing);
        } else if (i == PAUSE_AT + 2) {
            if (!video_picture_ns(0, &picture_at_pause))
                picture_at_pause = -1;
        } else if (i == PLAY_AT - 1) {
            int64_t now_picture = -2;
            video_picture_ns(0, &now_picture);
            pause_ok = !paused_playing && slot->shown - shown_at_pause <= 1
                       && now_picture == picture_at_pause && now_picture <= paused_clock + PAUSE_AHEAD_NS;
            printf("paused 0.5 s at %.3f s: %llu pictures taken, picture at %.3f s, then %.3f s: %s\n",
                   paused_clock / 1e9, (unsigned long long)(slot->shown - shown_at_pause),
                   picture_at_pause / 1e9, now_picture / 1e9, pause_ok ? "held" : "MOVED");
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
        } else if (i == SEEK_AT && again) {
            // WebKit's start again after the end: NULL (the slot goes; READY keeps it),
            // PLAYING (a slot again), then caps and a flush (FEED_AGAIN).
            gst_pad_push_event(src, gst_event_new_flush_start());
            pthread_join(feed, NULL);
            gst_pad_push_event(src, gst_event_new_flush_stop(TRUE));
            const uint32_t serial = slot->serial;
            const GstStateChangeReturn to_null = gst_element_set_state(pipeline, GST_STATE_NULL);
            const GstStateChangeReturn to_playing = gst_element_set_state(pipeline, GST_STATE_PLAYING);
            pthread_create(&feed, NULL, feeder, (void *)(intptr_t)FEED_AGAIN);
            printf("again from 0 at flip %d (to NULL %s, to PLAYING %s; slot %s, serial %u then %u)\n", i,
                   gst_element_state_change_return_get_name(to_null),
                   gst_element_state_change_return_get_name(to_playing),
                   __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == VIDEO_SLOT_ACTIVE ? "taken" : "NOT TAKEN",
                   serial, slot->serial);
            again_serial = slot->serial != serial;
            seek_flip = i;
        } else if (i == SEEK_AT) {
            // playbin's flushing seek, to SEEK_FRAME.
            gst_pad_push_event(src, gst_event_new_flush_start());
            pthread_join(feed, NULL);
            gst_pad_push_event(src, gst_event_new_flush_stop(TRUE));
            pthread_create(&feed, NULL, feeder, (void *)(intptr_t)FEED_SEEK);
            printf("seek to %.3f s at flip %d (generation %u)\n", SEEK_FRAME / 60.0, i, slot->generation);
            seek_flip = i;
        }
        if (i == 60 || i == 120) {
            guint64 r;
            sink_stats(sink, &r, &steady_dropped[i == 120]);
        }
        if (i == 120 || i == 170 || i == 330 || i == FLIPS - 1) {
            snprintf(path, sizeof(path), "%s-%d.ppm", argv[2], i);
            write_ppm(path, out);
        }
        if (i % 60 == 59) {
            int64_t p = -1;
            video_picture_ns(0, &p);
            if (video_status(line, sizeof(line)))
                printf("flip %d (picture %.3f s): %s\n", i + 1, p / 1e9, line);
        }
        const uint64_t next = t0 + 16666667ull;
        const uint64_t now = video_ring_now_ns();
        if (next > now)
            usleep((useconds_t)((next - now) / 1000));
    }
    int64_t last = -1;
    video_picture_ns(0, &last);
    printf("pushed %u, written %llu, decoded %llu, shown %llu, late %llu, picture %ux%u, last at %.3f s\n",
           pushed, (unsigned long long)slot->frames_written, (unsigned long long)slot->decoded,
           (unsigned long long)slot->shown, (unsigned long long)slot->late, slot->width, slot->height,
           last / 1e9);
    printf("compose %.2f ms average, %.2f worst\n", sum / FLIPS, worst);
    const unsigned errors = video_errors(0);
    printf("decoder errors: %u\n", errors);
    if (sync_count)
        printf("clock - picture: %.1f ms min, %.1f average, %.1f max (%u flips)\n", sync_min,
               sync_sum / sync_count, sync_max, sync_count);

    // What WebKit reads for getVideoPlaybackQuality(): while playing on its
    // own (flips 60 to 120) at most one drops; over the whole test, with
    // its start, pause and seek, a few. Slow: most. At most one, not none:
    // this loop sleeps (usleep), it has no vsync, and one oversleep makes two
    // pictures due at the same flip, the older counted late. At 10:35 the
    // window had none; at 12:01, after WSL had restarted, the same code had
    // one in every run, the picture at flip 120 one frame further on.
    guint64 rendered, dropped;
    sink_stats(sink, &rendered, &dropped);
    const double dropped_pc = rendered + dropped ? 100.0 * (double)dropped / (double)(rendered + dropped) : 0;
    printf("sink stats (the page's getVideoPlaybackQuality): %llu rendered, %llu dropped (%.1f%%), "
           "%llu of them from flip 60 to 120; the title: %llu late, %llu behind\n",
           (unsigned long long)rendered, (unsigned long long)dropped, dropped_pc,
           (unsigned long long)(steady_dropped[1] - steady_dropped[0]), (unsigned long long)slot->late,
           (unsigned long long)slot->behind);
    const int stats_ok = slow ? dropped_pc >= 30
                         : rendered > 0 && dropped_pc <= 5 && steady_dropped[1] - steady_dropped[0] <= 1;

    gst_pad_push_event(src, gst_event_new_flush_start());
    pthread_join(feed, NULL);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    printf("slot after the pipeline's end: %s\n",
           __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == VIDEO_SLOT_FREE ? "free" : "STILL TAKEN");
    gst_object_unref(pipeline);
    gst_object_unref(src);
    usleep(50000);
    video_stop();
    // "stall": the picture stops while the frames do, so the timing checks
    // do not hold; what counts is that the title got them all.
    const int ok = !qos && max_lateness == -1 && (slow ? stats_ok && !errors
                   : stall ? !errors && seek_shown >= 0 && caught_up >= 0
                             && last == (int64_t)(END_FRAMES - 1) * GST_SECOND / 60
                   : pause_ok == 1 && sync_count && sync_min > -12 && sync_max < 40
                   && seek_shown >= 0 && seek_shown < 60 && caught_up >= 0 && caught_up < 90
                   && seek_lag < 40 && stats_ok && (!again || again_serial)
                   && !flash && !errors && last == (int64_t)(END_FRAMES - 1) * GST_SECOND / 60);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
