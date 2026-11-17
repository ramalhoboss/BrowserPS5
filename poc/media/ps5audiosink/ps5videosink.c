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

// ps5videosink.c: WebKit's video, still compressed, to the browser's title,
// which decodes it with many cores and puts it under the page
// (docs/08-4k.md, D2; the title's side is browser/title/video.c, the ring
// ../../common/video_ring.h).
//
// Registered as "westerossink", with no rank (nothing picks it by itself):
// WebKit's Westeros quirks make it, when the WebProcess's environment has
// WEBKIT_GST_QUIRKS=westeros and WEBKIT_GST_HOLE_PUNCH_QUIRK=westeros
// (crash-report.c sets them when the title's ring is there). Then:
//   - playbin3's uridecodebin3 stops at the caps of the template below, so
//     no avdec_* decodes in the WebProcess;
//   - WebKit's compositor leaves the video's place transparent and sets
//     "rectangle" to it, in the page's pixels;
//   - this sink is a GstBaseSink that syncs on the pipeline's clock (the
//     sound's) LEAD_NS early (ts-offset, kept under the one playbin3's
//     playsink sets when there is sound too), and hands each buffer over
//     with its running time; the title shows it when that time comes;
//   - it never drops a frame for being late: each is a reference for the
//     next ones until a key frame, and the title skips late pictures
//     itself. WebKit sets "qos" and "max-lateness" (100 ms) on every video
//     sink; here those two are taken and ignored (on the console, 98 frames
//     the title could not decode after a busy start, 2026-11-05 11:49).
// Flushes (seeks) bump the slot's generation; pause and play move the clock
// fields. After a seek into the middle of a group of pictures, the frames
// before the seek's place go to the title at once, around GstBaseSink, with
// negative running times (chain()): the title decodes them, never shows them;
// the frame at the place waits until it has (wait_decoded()), so the sound
// starts again with the picture. A sink waits for room in its slot 2 ms at a time (no spinning);
// with the title gone it drops the frames.

#include "ps5audiosink.h"
#include "video_ring.h"

#include <gst/base/gstbasesink.h>
#include <gst/gst.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(ps5_video_sink_debug);
#define GST_CAT_DEFAULT ps5_video_sink_debug

// How early the frames go to the title: room for its decoder's threads
// (8 frames at 60/s are 133 ms) and a margin.
#define LEAD_NS     400000000LL
#define ROOM_WAIT_US 2000
#define REPORT_US   (5 * G_USEC_PER_SEC)
// After a seek into a group of pictures (wait_decoded()): how often to look,
// how long without a new picture means the title is done, and the most to
// wait (a group of 5 s in 4K, decoded at ~100 frames/s).
#define DECODE_POLL_US  5000
#define DECODE_IDLE_US  50000
#define DECODE_WAIT_US  (5 * G_USEC_PER_SEC)

typedef struct {
  GstBaseSink parent;

  video_ring_t* ring;     // the title's, or NULL
  video_slot_t* slot;     // ours, or NULL
  int index;

  gint unlocked;          // unlock(): a waiting render returns
  int32_t rect[4];        // the last rectangle (under the object lock)
  gfloat zorder;
  GstBuffer* prerolled;   // the buffer preroll() handed over, if render() gets it next
  GstPadChainFunction base_chain;  // GstBaseSink's, which chain() calls
  guint before;           // frames before the segment chain() handed over since the last flush
  GstClockTime last_pts;
  // For the logs: frames and bytes handed over, waits for room, drops
  guint64 frames, bytes, waits, drops;
  gint64 report_start;
  // For "stats" (under the object lock): the slot's counts when we took
  // it, and the frames the title never got (no slot, no room).
  guint64 base_shown, base_late, base_behind;
  guint64 lost;
} Ps5VideoSink;

typedef struct {
  GstBaseSinkClass parent_class;
} Ps5VideoSinkClass;

enum { PROP_0, PROP_RECTANGLE, PROP_ZORDER, PROP_STATS, PROP_QOS, PROP_MAX_LATENESS,
       PROP_TS_OFFSET };

GType ps5_video_sink_get_type(void);
G_DEFINE_TYPE(Ps5VideoSink, ps5_video_sink, GST_TYPE_BASE_SINK)

// What the title decodes (browser/title/video.c), as the parsers give it.
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-vp9; "
                    "video/x-vp8; "
                    "video/x-h264, stream-format = (string) { avc, avc3, byte-stream }, "
                    "alignment = (string) au; "
                    "video/x-h265, stream-format = (string) { hvc1, hev1, byte-stream }, "
                    "alignment = (string) au"));


static void
put_rect(Ps5VideoSink* self) {
  if(self->slot) {
    video_ring_put4(&self->slot->rect_seq, self->slot->rect, self->rect);
  }
}


// The clock stands still: where it is (paused), or at 0 (a flush that
// resets the running time). Under the object lock, like publish_clock().
static void
freeze_clock(Ps5VideoSink* self, gboolean at_zero) {
  GST_OBJECT_LOCK(self);
  if(self->slot) {
    int playing;
    const uint64_t now = video_ring_now_ns();
    const int64_t running = at_zero ? 0 : video_slot_running(self->slot, now, &playing);
    video_slot_set_clock(self->slot, running, now, 0);
  }
  GST_OBJECT_UNLOCK(self);
}


// The clock runs: the running time now is the pipeline's clock less the
// base time, valid while the element goes to or stays in PLAYING (render(),
// and PAUSED_TO_PLAYING, before which the bin gives it the new base time).
// Under the object lock, where gst_element_set_state() sets the target: a
// render() that races a pause either comes before the pause's
// freeze_clock() or sees the target PAUSED and leaves the clock alone.
static void
publish_clock(Ps5VideoSink* self) {
  GstElement* element = GST_ELEMENT_CAST(self);

  GST_OBJECT_LOCK(self);
  GstClock* clock = GST_ELEMENT_CLOCK(element);
  if(self->slot && clock && GST_STATE_TARGET(element) == GST_STATE_PLAYING) {
    GstClockTime base = element->base_time;
    GstClockTime now = gst_clock_get_time(clock);
    if(GST_CLOCK_TIME_IS_VALID(now) && now >= base) {
      video_slot_set_clock(self->slot, (int64_t)(now - base), video_ring_now_ns(), 1);
    }
  }
  GST_OBJECT_UNLOCK(self);
}


// A slot of the title's ring: a free one, or one whose writer died.
static gboolean
take_slot(Ps5VideoSink* self) {
  int fd = shm_open(VIDEO_RING_SHM, O_RDWR, 0);
  if(fd < 0) {
    GST_WARNING_OBJECT(self, "no %s (no title): no video", VIDEO_RING_SHM);
    return FALSE;
  }
  void* map = mmap(NULL, sizeof(video_ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(map == MAP_FAILED) {
    GST_WARNING_OBJECT(self, "mmap %s: %s", VIDEO_RING_SHM, strerror(errno));
    return FALSE;
  }
  video_ring_t* ring = map;
  if(!video_ring_ready(ring)) {
    munmap(map, sizeof(video_ring_t));
    return FALSE;
  }
  for(int pass = 0; pass < 2; pass++) {
    for(int i = 0; i < VIDEO_RING_SLOTS; i++) {
      video_slot_t* slot = &ring->slots[i];
      uint32_t state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
      if(pass == 0 && state != VIDEO_SLOT_FREE) {
        continue;
      }
      if(pass == 1) {  // a dead WebProcess's
        int32_t pid = slot->pid;
        if(state == VIDEO_SLOT_FREE || pid == getpid() || kill(pid, 0) == 0 || errno != ESRCH) {
          continue;
        }
      }
      if(!__atomic_compare_exchange_n(&slot->state, &state, VIDEO_SLOT_TAKING, FALSE,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        continue;
      }
      slot->pid = getpid();
      __atomic_fetch_add(&slot->generation, 1, __ATOMIC_ACQ_REL);  // what was left goes
      __atomic_fetch_add(&slot->serial, 1, __ATOMIC_ACQ_REL);
      video_slot_set_clock(slot, 0, video_ring_now_ns(), 0);
      GST_OBJECT_LOCK(self);  // get_property() reads the slot from another thread
      self->ring = ring;
      self->slot = slot;
      self->index = i;
      self->base_shown = __atomic_load_n(&slot->shown, __ATOMIC_RELAXED);
      self->base_late = __atomic_load_n(&slot->late, __ATOMIC_RELAXED);
      self->base_behind = __atomic_load_n(&slot->behind, __ATOMIC_RELAXED);
      __atomic_store_n(&self->lost, 0, __ATOMIC_RELAXED);  // "stats" start again (WebKit sees it)
      put_rect(self);
      GST_OBJECT_UNLOCK(self);
      __atomic_store_n(&slot->state, VIDEO_SLOT_ACTIVE, __ATOMIC_RELEASE);
      fprintf(stderr, "[ps5videosink] slot %d of %s%s\n", i, VIDEO_RING_SHM,
              pass ? " (from a WebProcess that ended)" : "");
      return TRUE;
    }
  }
  fprintf(stderr, "[ps5videosink] all %d slots taken: no video\n", VIDEO_RING_SLOTS);
  munmap(map, sizeof(video_ring_t));
  return FALSE;
}


static void
release_slot(Ps5VideoSink* self) {
  GST_OBJECT_LOCK(self);  // get_property() reads the slot from another thread
  video_ring_t* ring = self->ring;
  video_slot_t* slot = self->slot;
  self->ring = NULL;
  self->slot = NULL;
  GST_OBJECT_UNLOCK(self);
  if(slot) {
    const int32_t none[4] = {0, 0, 0, 0};
    video_ring_put4(&slot->rect_seq, slot->rect, none);
    fprintf(stderr, "[ps5videosink] slot %d done: %llu frames, %llu decoded, %llu shown, %llu late, "
            "%llu behind\n", self->index, (unsigned long long)self->frames,
            (unsigned long long)slot->decoded, (unsigned long long)slot->shown,
            (unsigned long long)slot->late, (unsigned long long)slot->behind);
    __atomic_store_n(&slot->state, VIDEO_SLOT_FREE, __ATOMIC_RELEASE);
  }
  if(ring) {
    munmap(ring, sizeof(video_ring_t));
  }
}


// Room for size bytes in the slot: waits 2 ms at a time. FALSE if the sink
// is unlocked (a flush, a state change) or the title stopped looking.
static gboolean
wait_room(Ps5VideoSink* self, uint32_t size, uint64_t* offset) {
  gboolean waited = FALSE;
  for(;;) {
    if(video_slot_room(self->slot, size, offset)) {
      self->waits += waited;
      return TRUE;
    }
    if(g_atomic_int_get(&self->unlocked) || !video_ring_alive(self->ring)) {
      return FALSE;
    }
    waited = TRUE;
    g_usleep(ROOM_WAIT_US);
  }
}


static gboolean
ps5_video_sink_start(GstBaseSink* sink) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  self->frames = self->bytes = self->waits = self->drops = 0;
  self->before = 0;
  self->report_start = 0;
  self->last_pts = GST_CLOCK_TIME_NONE;
  take_slot(self);  // without one the frames are dropped; the page still plays
  return TRUE;
}


static gboolean
ps5_video_sink_stop(GstBaseSink* sink) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  gst_clear_buffer(&self->prerolled);
  release_slot(self);
  return TRUE;
}


static gboolean
ps5_video_sink_set_caps(GstBaseSink* sink, GstCaps* caps) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  const GstStructure* s = gst_caps_get_structure(caps, 0);
  const char* name = gst_structure_get_name(s);
  video_format_t format;
  GstMapInfo config = GST_MAP_INFO_INIT;
  GstBuffer* config_buffer = NULL;

  memset(&format, 0, sizeof(format));
  if(g_str_equal(name, "video/x-vp9")) {
    format.codec = VIDEO_CODEC_VP9;
  } else if(g_str_equal(name, "video/x-vp8")) {
    format.codec = VIDEO_CODEC_VP8;
  } else if(g_str_equal(name, "video/x-h264")) {
    format.codec = VIDEO_CODEC_H264;
  } else if(g_str_equal(name, "video/x-h265")) {
    format.codec = VIDEO_CODEC_HEVC;
  } else {
    GST_WARNING_OBJECT(self, "caps %" GST_PTR_FORMAT " not taken", caps);
    return FALSE;
  }
  int width = 0, height = 0;
  gst_structure_get_int(s, "width", &width);
  gst_structure_get_int(s, "height", &height);
  format.width = (uint32_t)MAX(width, 0);
  format.height = (uint32_t)MAX(height, 0);
  const GValue* value = gst_structure_get_value(s, "codec_data");
  if(value && G_VALUE_HOLDS(value, GST_TYPE_BUFFER)) {
    config_buffer = gst_value_get_buffer(value);
    if(config_buffer && gst_buffer_map(config_buffer, &config, GST_MAP_READ)) {
      format.config_size = (uint32_t)MIN(config.size, (gsize)VIDEO_RING_MAX_CONFIG);
    } else {
      config_buffer = NULL;
    }
  }
  fprintf(stderr, "[ps5videosink] %s %dx%d, %u bytes of codec data\n", name, width, height,
          format.config_size);
  if(self->slot) {
    uint64_t at;
    uint32_t size = (uint32_t)sizeof(format) + format.config_size;
    if(wait_room(self, size, &at)) {
      uint8_t* to = self->slot->data + at % VIDEO_RING_DATA;
      memcpy(to, &format, sizeof(format));
      if(format.config_size) {
        memcpy(to + sizeof(format), config.data, format.config_size);
      }
      video_slot_push(self->slot, at, size, VIDEO_FRAME_FORMAT, 0);
    }
  }
  if(config_buffer) {
    gst_buffer_unmap(config_buffer, &config);
  }
  return TRUE;
}


// One buffer to the title.
static GstFlowReturn
hand_over(Ps5VideoSink* self, GstBuffer* buffer) {
  GstBaseSink* sink = GST_BASE_SINK_CAST(self);
  GstMapInfo map;

  if(!self->slot) {
    self->drops++;
    __atomic_fetch_add(&self->lost, 1, __ATOMIC_RELAXED);  // the title never gets it
    return GST_FLOW_OK;
  }
  GstClockTime pts = GST_BUFFER_PTS(buffer);
  if(!GST_CLOCK_TIME_IS_VALID(pts)) {
    pts = GST_BUFFER_DTS(buffer);
  }
  if(!GST_CLOCK_TIME_IS_VALID(pts)) {
    pts = GST_CLOCK_TIME_IS_VALID(self->last_pts) ? self->last_pts + GST_SECOND / 60 : 0;
  }
  self->last_pts = pts;
  // The running time: negative for a frame that ends before the segment
  // starts (the title decodes it and never shows it, see chain()); one that
  // straddles the start shows from it, as GstBaseSink clips it.
  GstSegment segment;
  GST_OBJECT_LOCK(self);
  segment = sink->segment;
  GST_OBJECT_UNLOCK(self);
  GstClockTime end = GST_BUFFER_DURATION_IS_VALID(buffer) ? pts + GST_BUFFER_DURATION(buffer) : pts;
  if(segment.format == GST_FORMAT_TIME && pts < segment.start && end > segment.start) {
    pts = segment.start;
  }
  guint64 magnitude = 0;
  const int sign = gst_segment_to_running_time_full(&segment, GST_FORMAT_TIME, pts, &magnitude);
  if(!sign) {  // not in TIME, or past a reverse segment's end
    self->drops++;
    return GST_FLOW_OK;
  }
  const int64_t running = sign > 0 ? (int64_t)magnitude : -(int64_t)magnitude;
  if(!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    self->drops++;
    __atomic_fetch_add(&self->lost, 1, __ATOMIC_RELAXED);  // the title never gets it
    return GST_FLOW_OK;
  }
  uint64_t at;
  if(map.size > VIDEO_RING_MAX_FRAME || !wait_room(self, (uint32_t)map.size, &at)) {
    gst_buffer_unmap(buffer, &map);
    self->drops++;
    __atomic_fetch_add(&self->lost, 1, __ATOMIC_RELAXED);  // the title never gets it
    return g_atomic_int_get(&self->unlocked) ? GST_FLOW_FLUSHING : GST_FLOW_OK;
  }
  memcpy(self->slot->data + at % VIDEO_RING_DATA, map.data, map.size);
  uint32_t flags = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT) ? 0 : VIDEO_FRAME_KEY;
  video_slot_push(self->slot, at, (uint32_t)map.size, flags, running);
  self->frames++;
  self->bytes += map.size;
  gst_buffer_unmap(buffer, &map);

  gint64 now = g_get_monotonic_time();
  if(!self->report_start) {
    self->report_start = now;
  } else if(now - self->report_start >= REPORT_US) {
    fprintf(stderr, "[ps5videosink] slot %d: %llu frames (%.1f Mb/s), %llu waits for room, "
            "%llu dropped; the title decoded %llu, showed %llu, %llu late, %llu behind\n",
            self->index, (unsigned long long)self->frames,
            self->bytes * 8.0 / ((now - self->report_start) / 1e6) / 1e6,
            (unsigned long long)self->waits, (unsigned long long)self->drops,
            (unsigned long long)self->slot->decoded, (unsigned long long)self->slot->shown,
            (unsigned long long)self->slot->late, (unsigned long long)self->slot->behind);
    self->frames = self->bytes = self->waits = self->drops = 0;
    self->report_start = now;
  }
  return GST_FLOW_OK;
}


// The first buffer, while paused (a start, a seek): handed over now, so the
// title has it when the clock runs; if render() gets the same one when
// playing, it is not sent again. The clock stays as it is (stopped).
static GstFlowReturn
ps5_video_sink_preroll(GstBaseSink* sink, GstBuffer* buffer) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  GstFlowReturn ret = hand_over(self, buffer);
  gst_buffer_replace(&self->prerolled, buffer);
  return ret;
}


static GstFlowReturn
ps5_video_sink_render(GstBaseSink* sink, GstBuffer* buffer) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  publish_clock(self);
  if(self->prerolled == buffer) {
    gst_clear_buffer(&self->prerolled);
    return GST_FLOW_OK;
  }
  gst_clear_buffer(&self->prerolled);
  return hand_over(self, buffer);
}


// A frame that ends before the segment starts?
static gboolean
before_segment(Ps5VideoSink* self, GstBuffer* buffer) {
  GstBaseSink* sink = GST_BASE_SINK_CAST(self);
  const GstClockTime pts = GST_BUFFER_PTS(buffer);
  if(!GST_CLOCK_TIME_IS_VALID(pts)) {
    return FALSE;
  }
  GST_OBJECT_LOCK(self);
  const gboolean in_time = sink->segment.format == GST_FORMAT_TIME && sink->segment.rate > 0;
  const guint64 start = sink->segment.start;
  GST_OBJECT_UNLOCK(self);
  if(!in_time || pts >= start) {
    return FALSE;
  }
  return !GST_BUFFER_DURATION_IS_VALID(buffer) || pts + GST_BUFFER_DURATION(buffer) <= start;
}


// The sink pad's chain function, in front of GstBaseSink's. A seek into
// the middle of a group of pictures sends the frames again from the key
// frame before, with the segment starting at the seek's place (WebKit's
// MSE, the demuxers' accurate seeks); a decoder decodes the ones before the
// place without showing them. GstBaseSink drops them (out of the segment),
// and the title, with no key frame, then decoded nothing until the next
// one, seconds later (sink_test, 2026-11-05: 150 decoder errors after a
// seek to 1.5 s). So they go to the title here, at once, without
// GstBaseSink's preroll or clock, with a negative running time: the title
// decodes them and never shows them. The rest goes through GstBaseSink.
// After the frames before a seek's place, before the first frame at the
// place goes on to GstBaseSink: wait (at most DECODE_WAIT_US, 5 ms at a
// time) until the title has read them all and has made no new picture for
// DECODE_IDLE_US, i.e. has decoded them. GstBaseSink's preroll, and with it
// the pipeline's (the sound's) start, then waits for the title, as it
// would for a decoder in the pipeline; without this the sound went on at
// once and the picture came ~0.4 s later (sink_test, a seek to 1.5 s in
// 1080p; seconds in 4K). Here and not in preroll(): GstBaseSink holds its
// preroll lock there, which a state change from WebKit's main thread takes.
static void
wait_decoded(Ps5VideoSink* self, GstPad* pad) {
  video_slot_t* slot = self->slot;
  if(!slot) {
    return;
  }
  const gint64 start = g_get_monotonic_time();
  gint64 changed = start;
  uint64_t decoded = __atomic_load_n(&slot->decoded, __ATOMIC_RELAXED);
  const char* how = "decoded them";
  for(;;) {
    if(g_atomic_int_get(&self->unlocked) || GST_PAD_IS_FLUSHING(pad)) {
      how = "stopped (a flush)";
      break;
    }
    if(!video_ring_alive(self->ring)) {
      how = "stopped looking";
      break;
    }
    const gint64 now = g_get_monotonic_time();
    const uint64_t d = __atomic_load_n(&slot->decoded, __ATOMIC_RELAXED);
    if(d != decoded) {
      decoded = d;
      changed = now;
    }
    const gboolean all_read = __atomic_load_n(&slot->frames_read, __ATOMIC_ACQUIRE)
                              == __atomic_load_n(&slot->frames_written, __ATOMIC_RELAXED);
    if(all_read && now - changed >= DECODE_IDLE_US) {
      break;
    }
    if(now - start >= DECODE_WAIT_US) {
      how = "was not done: going on";
      break;
    }
    g_usleep(DECODE_POLL_US);
  }
  fprintf(stderr, "[ps5videosink] slot %d: a seek into a group of pictures: the title %s, "
          "%u frames before the place, in %lld ms\n", self->index, how, self->before,
          (long long)((g_get_monotonic_time() - start) / 1000));
}


static GstFlowReturn
ps5_video_sink_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer) {
  Ps5VideoSink* self = (Ps5VideoSink*)parent;
  if(!before_segment(self, buffer)) {
    if(self->before) {  // the first frame at the seek's place
      wait_decoded(self, pad);
      self->before = 0;
    }
    return self->base_chain(pad, parent, buffer);
  }
  GstFlowReturn ret = g_atomic_int_get(&self->unlocked) ? GST_FLOW_FLUSHING : hand_over(self, buffer);
  gst_buffer_unref(buffer);
  if(ret == GST_FLOW_OK) {
    self->before++;
  }
  return ret;
}


static gboolean
ps5_video_sink_event(GstBaseSink* sink, GstEvent* event) {
  Ps5VideoSink* self = (Ps5VideoSink*)sink;
  if(GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP) {
    self->before = 0;
  }
  if(GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP && self->slot) {
    // A seek: the title drops what it holds, and its clock stops (at 0 if
    // the running time starts again) until the pipeline plays again.
    gboolean reset_time = FALSE;
    gst_event_parse_flush_stop(event, &reset_time);
    __atomic_fetch_add(&self->slot->generation, 1, __ATOMIC_ACQ_REL);
    freeze_clock(self, reset_time);
    gst_clear_buffer(&self->prerolled);
    self->last_pts = GST_CLOCK_TIME_NONE;
  } else if(GST_EVENT_TYPE(event) == GST_EVENT_EOS && self->slot) {
    // The end: the title takes the last pictures out of its decoder (with
    // its threads it holds several).
    uint64_t at;
    if(wait_room(self, 0, &at)) {
      video_slot_push(self->slot, at, 0, VIDEO_FRAME_DRAIN, 0);
    }
  }
  return GST_BASE_SINK_CLASS(ps5_video_sink_parent_class)->event(sink, event);
}


static gboolean
ps5_video_sink_unlock(GstBaseSink* sink) {
  g_atomic_int_set(&((Ps5VideoSink*)sink)->unlocked, 1);
  return TRUE;
}


static gboolean
ps5_video_sink_unlock_stop(GstBaseSink* sink) {
  g_atomic_int_set(&((Ps5VideoSink*)sink)->unlocked, 0);
  return TRUE;
}


static GstStateChangeReturn
ps5_video_sink_change_state(GstElement* element, GstStateChange transition) {
  Ps5VideoSink* self = (Ps5VideoSink*)element;
  GstStateChangeReturn ret =
    GST_ELEMENT_CLASS(ps5_video_sink_parent_class)->change_state(element, transition);

  // To PLAYING the bin gave the element its new base time before; to
  // PAUSED the target is PAUSED already, so no render() runs the clock again.
  if(transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
    publish_clock(self);
  } else if(transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    freeze_clock(self, FALSE);
  }
  return ret;
}


static void
ps5_video_sink_set_property(GObject* object, guint id, const GValue* value, GParamSpec* pspec) {
  Ps5VideoSink* self = (Ps5VideoSink*)object;
  switch(id) {
  case PROP_RECTANGLE: {
    const char* text = g_value_get_string(value);
    int x, y, w, h;
    if(text && sscanf(text, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
      GST_OBJECT_LOCK(self);
      if(x != self->rect[0] || y != self->rect[1] || w != self->rect[2] || h != self->rect[3]) {
        self->rect[0] = x;
        self->rect[1] = y;
        self->rect[2] = w;
        self->rect[3] = h;
        put_rect(self);
      }
      GST_OBJECT_UNLOCK(self);
    }
    break;
  }
  case PROP_ZORDER:
    self->zorder = g_value_get_float(value);
    break;
  case PROP_QOS:
  case PROP_MAX_LATENESS:
    // Taken and ignored: GstBaseSink would drop late frames, and the title
    // could not decode the ones after them (see the top of this file).
    GST_INFO_OBJECT(self, "\"%s\" ignored: no frame is dropped here", pspec->name);
    break;
  case PROP_TS_OFFSET:
    // playbin3's playsink sets it (0 for "av-offset" 0) on the video sink
    // whenever the stream has sound too: taken, with our lead on top of it.
    // Until 2026-11-05 16:40 GstBaseSink's own took the lead away, and
    // every picture of a page with sound (YouTube's all) came to the title
    // at its time, ~130 ms before its decoder gave it back: the picture
    // behind the sound, and the page told that all of them dropped.
    gst_base_sink_set_ts_offset(GST_BASE_SINK_CAST(self), g_value_get_int64(value) - LEAD_NS);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
  }
}


// "stats", in place of GstBaseSink's: WebKit reads its "rendered" and
// "dropped" for the page's getVideoPlaybackQuality(), and sites (YouTube)
// choose a lower quality when many frames drop. GstBaseSink's would count
// every frame handed over as rendered, the title's late ones included.
// Here: rendered, the pictures the title showed on time; dropped, the ones
// it skipped or showed behind their time (VIDEO_BEHIND_NS), and the frames
// it never got. "average-rate" stays GstBaseSink's.
static GstStructure*
get_stats(Ps5VideoSink* self) {
  GstStructure* stats = NULL;
  GParamSpec* base = g_object_class_find_property(G_OBJECT_CLASS(ps5_video_sink_parent_class), "stats");
  if(base && G_IS_PARAM_SPEC_BOXED(base) && base->value_type == GST_TYPE_STRUCTURE) {
    GValue value = G_VALUE_INIT;
    g_value_init(&value, GST_TYPE_STRUCTURE);
    G_OBJECT_CLASS(ps5_video_sink_parent_class)->get_property(G_OBJECT(self), base->param_id, &value, base);
    stats = g_value_dup_boxed(&value);
    g_value_unset(&value);
  }
  if(!stats) {
    stats = gst_structure_new("application/x-gst-base-sink-stats", "average-rate", G_TYPE_DOUBLE, 0.0,
                              NULL);
  }
  guint64 shown = 0, late = 0, behind = 0;
  GST_OBJECT_LOCK(self);
  if(self->slot) {
    shown = __atomic_load_n(&self->slot->shown, __ATOMIC_RELAXED) - self->base_shown;
    late = __atomic_load_n(&self->slot->late, __ATOMIC_RELAXED) - self->base_late;
    behind = __atomic_load_n(&self->slot->behind, __ATOMIC_RELAXED) - self->base_behind;
  }
  GST_OBJECT_UNLOCK(self);
  if(behind > shown) {  // read a little apart
    behind = shown;
  }
  const guint64 lost = __atomic_load_n(&self->lost, __ATOMIC_RELAXED);
  gst_structure_set(stats, "rendered", G_TYPE_UINT64, shown - behind, "dropped", G_TYPE_UINT64,
                    late + behind + lost, NULL);
  // Lifetime counts (GStreamer 1.28's) would be taken over this one's
  // "rendered" and "dropped": they are this sink's, not the title's.
  gst_structure_remove_fields(stats, "total-rendered", "total-dropped", NULL);
  return stats;
}


static void
ps5_video_sink_get_property(GObject* object, guint id, GValue* value, GParamSpec* pspec) {
  Ps5VideoSink* self = (Ps5VideoSink*)object;
  switch(id) {
  case PROP_RECTANGLE: {
    char text[64];
    GST_OBJECT_LOCK(self);
    g_snprintf(text, sizeof(text), "%d,%d,%d,%d", self->rect[0], self->rect[1], self->rect[2],
               self->rect[3]);
    GST_OBJECT_UNLOCK(self);
    g_value_set_string(value, text);
    break;
  }
  case PROP_ZORDER:
    g_value_set_float(value, self->zorder);
    break;
  case PROP_STATS:
    g_value_take_boxed(value, get_stats(self));
    break;
  case PROP_QOS:
    g_value_set_boolean(value, gst_base_sink_is_qos_enabled(GST_BASE_SINK_CAST(self)));
    break;
  case PROP_MAX_LATENESS:
    g_value_set_int64(value, gst_base_sink_get_max_lateness(GST_BASE_SINK_CAST(self)));
    break;
  case PROP_TS_OFFSET:  // what was set, without our lead
    g_value_set_int64(value, gst_base_sink_get_ts_offset(GST_BASE_SINK_CAST(self)) + LEAD_NS);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
  }
}


static void
ps5_video_sink_finalize(GObject* object) {
  Ps5VideoSink* self = (Ps5VideoSink*)object;
  gst_clear_buffer(&self->prerolled);
  release_slot(self);
  G_OBJECT_CLASS(ps5_video_sink_parent_class)->finalize(object);
}


static void
ps5_video_sink_class_init(Ps5VideoSinkClass* klass) {
  GObjectClass* object_class = G_OBJECT_CLASS(klass);
  GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
  GstBaseSinkClass* sink_class = GST_BASE_SINK_CLASS(klass);

  object_class->set_property = ps5_video_sink_set_property;
  object_class->get_property = ps5_video_sink_get_property;
  object_class->finalize = ps5_video_sink_finalize;
  g_object_class_install_property(object_class, PROP_RECTANGLE,
      g_param_spec_string("rectangle", "Rectangle", "The video's place: x,y,width,height",
                          "0,0,0,0", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property(object_class, PROP_ZORDER,
      g_param_spec_float("zorder", "Z order", "Taken and ignored (WebKit sets it)", 0, 1, 0,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property(object_class, PROP_STATS, "stats");  // get_stats()
  g_object_class_override_property(object_class, PROP_QOS, "qos");  // set: ignored
  g_object_class_override_property(object_class, PROP_MAX_LATENESS, "max-lateness");  // set: ignored
  g_object_class_override_property(object_class, PROP_TS_OFFSET, "ts-offset");  // set: + our lead

  gst_element_class_set_static_metadata(element_class, "PS5 video sink",
      "Sink/Video", "Hands compressed video to the BrowserPS5 title", "BrowserPS5");
  gst_element_class_add_static_pad_template(element_class, &sink_template);
  element_class->change_state = ps5_video_sink_change_state;

  sink_class->start = ps5_video_sink_start;
  sink_class->stop = ps5_video_sink_stop;
  sink_class->set_caps = ps5_video_sink_set_caps;
  sink_class->preroll = ps5_video_sink_preroll;
  sink_class->render = ps5_video_sink_render;
  sink_class->event = ps5_video_sink_event;
  sink_class->unlock = ps5_video_sink_unlock;
  sink_class->unlock_stop = ps5_video_sink_unlock_stop;
}


static void
ps5_video_sink_init(Ps5VideoSink* self) {
  GstBaseSink* sink = GST_BASE_SINK_CAST(self);
  gst_base_sink_set_sync(sink, TRUE);
  gst_base_sink_set_ts_offset(sink, -LEAD_NS);
  gst_base_sink_set_max_lateness(sink, -1);  // the title drops what is late (and the
  gst_base_sink_set_qos_enabled(sink, FALSE);  // properties ignore WebKit's values)
  self->last_pts = GST_CLOCK_TIME_NONE;
  // chain() in front of GstBaseSink's; buffer lists then come as buffers
  GstPad* pad = GST_BASE_SINK_PAD(sink);
  self->base_chain = GST_PAD_CHAINFUNC(pad);
  gst_pad_set_chain_function(pad, ps5_video_sink_chain);
  gst_pad_set_chain_list_function(pad, NULL);
}


gboolean
browserps5_video_sink_register(void) {
  GST_DEBUG_CATEGORY_INIT(ps5_video_sink_debug, "ps5videosink", 0, "PS5 video sink");
  return gst_element_register(NULL, "westerossink", GST_RANK_NONE, ps5_video_sink_get_type());
}
