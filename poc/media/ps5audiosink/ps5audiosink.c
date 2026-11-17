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

// ps5audiosink: GStreamer's sound out of the PS5, through sceAudioOut
// (docs/03-video-e-gpu.md, V3). A GstAudioSink: GStreamer's ring buffer
// thread hands write() the samples, and sceAudioOutOutput() blocks until
// the port takes the next block, so the port is the clock.
//
// Only 48 kHz stereo S16, in blocks of 256 frames (5.3 ms); the
// audioconvert and audioresample in front of every sink make anything else
// into that. Registered with a primary rank (browserps5_audio_sink_register()), so
// autoaudiosink, and with it WebKit's audio sink, picks it.
//
// BROWSERPS5_NO_SOUND set (e.g. in the WebProcess's env.txt) registers it
// with no rank: autoaudiosink then takes a fakesink, and nothing opens a
// port. For log-only tests of pages that may stop the WebProcess: one
// killed with a port open left the console's sound and app services stuck
// until a restart (V2 12:31, V3 13:27, 2026-11-04).
//
// With the browser's title running (the GPU browser), the sound goes to it
// instead, through a ring in shared memory (../../common/audio_ring.h), and
// the title plays it: while an app is in front the system silences other
// processes' sound, and the WebProcess is a payload (heard on 2026-11-04).
// The ring's free space is then the clock: write() waits while it is full.
// Without the title (no ring, gst_probe, the CPU browser) the sink opens a
// port of its own, as before.

#include "ps5audiosink.h"
#include "audio_ring.h"

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread_np.h>
#include <sched.h>
#include <gst/audio/gstaudiosink.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// sceAudioOut as the open PS4/PS5 homebrew SDKs declare it.
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index,
                        uint32_t frames, uint32_t rate, uint32_t format);
int32_t sceAudioOutOutput(int32_t handle, const void* samples);
int32_t sceAudioOutClose(int32_t handle);

#define PORT_TYPE_MAIN    0
#define FORMAT_S16_STEREO 1
#define USER_SYSTEM       0xff
#define BLOCK_FRAMES      256
#define BLOCK_BYTES       (BLOCK_FRAMES * 4)

GST_DEBUG_CATEGORY_STATIC(ps5_audio_sink_debug);


// The ports open now (handle + 1; 0 is a free slot). Built into libWebKit
// (build-webkit.sh defines BROWSERPS5_CRASH_HOOK), a WebProcess that
// crashes closes them before it exits (poc/webkit/crash-report): on the TV
// test of 2026-11-04 the console went down when one crashed with a port
// open, after the browser's title was closed from the system.
#define MAX_PORTS 8

static int32_t open_ports[MAX_PORTS];
// The title's rings this process writes, freed the same way.
static audio_slot_t* own_slots[AUDIO_RING_SLOTS];

#ifdef BROWSERPS5_CRASH_HOOK
void browserps5_crash_hook(void (*hook)(void));

// In a signal handler: no locks, no allocation.
static void
close_open_ports(void) {
  for(int i = 0; i < MAX_PORTS; i++) {
    int32_t port = __atomic_exchange_n(&open_ports[i], 0, __ATOMIC_ACQ_REL);
    if(port) {
      sceAudioOutClose(port - 1);
    }
  }
  for(int i = 0; i < AUDIO_RING_SLOTS; i++) {
    audio_slot_t* slot = __atomic_exchange_n(&own_slots[i], NULL,
                                             __ATOMIC_ACQ_REL);
    if(slot) {
      __atomic_store_n(&slot->state, AUDIO_SLOT_FREE, __ATOMIC_RELEASE);
    }
  }
}
#endif


static void
port_opened(int32_t handle) {
#ifdef BROWSERPS5_CRASH_HOOK
  browserps5_crash_hook(close_open_ports);
#endif
  for(int i = 0; i < MAX_PORTS; i++) {
    int32_t none = 0;
    if(__atomic_compare_exchange_n(&open_ports[i], &none, handle + 1, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      return;
    }
  }
}


static void
port_closing(int32_t handle) {
  for(int i = 0; i < MAX_PORTS; i++) {
    int32_t mine = handle + 1;
    if(__atomic_compare_exchange_n(&open_ports[i], &mine, 0, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      return;
    }
  }
}


#define GST_CAT_DEFAULT ps5_audio_sink_debug

// How fast the port takes the sound, in the log every this many frames (5 s):
// with sound, YouTube played at 0.4x (V3, 2026-11-04), and the port is the
// clock.
#define REPORT_FRAMES (48000 * 5)
// How long write() sleeps while the title's ring is full.
#define RING_WAIT_US  2000

typedef struct {
  GstAudioSink parent;
  int32_t handle;       // a port of our own, or -1
  audio_ring_t* ring;   // the title's rings, or NULL
  audio_slot_t* slot;   // ours among them
  gint resetting;       // reset() asks a waiting write() to return
  gboolean dropping;    // the title went away: the sound is dropped
  guint filled;
  guint8 block[BLOCK_BYTES];
  guint64 frames;       // since the last report
  gint64 report_start;  // g_get_monotonic_time() then, 0 before the first block
  gint64 in_port;       // of that, microseconds inside sceAudioOutOutput()
                        // or waiting for room in the ring
  gint64 longest;       // the longest of those calls, in microseconds
} Ps5AudioSink;

typedef struct {
  GstAudioSinkClass parent_class;
} Ps5AudioSinkClass;

GType ps5_audio_sink_get_type(void);
G_DEFINE_TYPE(Ps5AudioSink, ps5_audio_sink, GST_TYPE_AUDIO_SINK)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format = (string) S16LE, "
                    "layout = (string) interleaved, rate = (int) 48000, "
                    "channels = (int) 2"));


// sceAudioOutInit() once per process, and only before a port of our own:
// the title's rings need none. When the first call failed (0x80260002, on
// 2026-11-04 at 13:25 and 16:46) the next one never returned, and it came
// from autoaudiosink trying the sink on the WebProcess's main thread: the
// page stopped answering for good (in the runs where the first call gave 0,
// the second said 0x8026000e, done already, 0.2 ms later).
#define AUDIO_INIT_DONE_ALREADY ((int32_t)0x8026000e)

static pthread_once_t audio_init_once = PTHREAD_ONCE_INIT;
static int32_t audio_init_result;

static void
audio_init(void) {
  audio_init_result = sceAudioOutInit();
}


static gboolean
ps5_audio_sink_open(GstAudioSink* sink) {
  (void)sink;
  return TRUE;
}


// A ring of the title's, if the title is there and plays: TRUE if self has
// one now.
static gboolean
ring_take(Ps5AudioSink* self) {
  int fd = shm_open(AUDIO_RING_SHM, O_RDWR, 0);
  if(fd < 0) {
    GST_INFO_OBJECT(self, "no %s (no title): a port of our own",
                    AUDIO_RING_SHM);
    return FALSE;
  }
  void* map = mmap(NULL, sizeof(audio_ring_t), PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
  close(fd);
  if(map == MAP_FAILED) {
    GST_WARNING_OBJECT(self, "mmap %s: %s", AUDIO_RING_SHM, strerror(errno));
    return FALSE;
  }
  audio_ring_t* ring = map;
  if(!audio_ring_ready(ring) || !audio_ring_alive(ring)) {
    GST_WARNING_OBJECT(self, "%s is there but the title does not play it",
                       AUDIO_RING_SHM);
    munmap(map, sizeof(audio_ring_t));
    return FALSE;
  }
  for(int i = 0; i < AUDIO_RING_SLOTS; i++) {
    audio_slot_t* slot = &ring->slots[i];
    uint32_t free_state = AUDIO_SLOT_FREE;
    if(!__atomic_compare_exchange_n(&slot->state, &free_state,
                                    AUDIO_SLOT_TAKING, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      continue;
    }
    // The reader skips a slot being taken, so all of it is ours for now.
    slot->pid = getpid();
    slot->read = 0;
    slot->flush_to = 0;
    __atomic_store_n(&slot->written, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&slot->state, AUDIO_SLOT_PLAYING, __ATOMIC_RELEASE);
    for(int j = 0; j < AUDIO_RING_SLOTS; j++) {
      audio_slot_t* none = NULL;
      if(__atomic_compare_exchange_n(&own_slots[j], &none, slot, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        break;
      }
    }
#ifdef BROWSERPS5_CRASH_HOOK
    browserps5_crash_hook(close_open_ports);
#endif
    self->ring = ring;
    self->slot = slot;
    GST_INFO_OBJECT(self, "the title plays our sound: ring %d of %s", i,
                    AUDIO_RING_SHM);
    return TRUE;
  }
  GST_WARNING_OBJECT(self, "all %d of the title's rings are taken",
                     AUDIO_RING_SLOTS);
  munmap(map, sizeof(audio_ring_t));
  return FALSE;
}


static void
ring_give_back(Ps5AudioSink* self) {
  if(!self->slot) {
    return;
  }
  for(int j = 0; j < AUDIO_RING_SLOTS; j++) {
    audio_slot_t* mine = self->slot;
    if(__atomic_compare_exchange_n(&own_slots[j], &mine, NULL, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      break;
    }
  }
  __atomic_store_n(&self->slot->state, AUDIO_SLOT_FREE, __ATOMIC_RELEASE);
  munmap(self->ring, sizeof(audio_ring_t));
  self->slot = NULL;
  self->ring = NULL;
}


static gboolean
ps5_audio_sink_prepare(GstAudioSink* sink, GstAudioRingBufferSpec* spec) {
  Ps5AudioSink* self = (Ps5AudioSink*)sink;

  self->handle = -1;
  self->dropping = FALSE;
  g_atomic_int_set(&self->resetting, 0);
  if(!ring_take(self)) {
    pthread_once(&audio_init_once, audio_init);
    GST_INFO_OBJECT(sink, "sceAudioOutInit: 0x%08x",
                    (unsigned)audio_init_result);
    if(audio_init_result < 0 && audio_init_result != AUDIO_INIT_DONE_ALREADY) {
      GST_ELEMENT_ERROR(sink, RESOURCE, OPEN_WRITE, (NULL),
                        ("sceAudioOutInit: 0x%08x",
                         (unsigned)audio_init_result));
      return FALSE;
    }
    self->handle = sceAudioOutOpen(USER_SYSTEM, PORT_TYPE_MAIN, 0, BLOCK_FRAMES,
                                   48000, FORMAT_S16_STEREO);
    if(self->handle < 0) {
      GST_ELEMENT_ERROR(sink, RESOURCE, OPEN_WRITE, (NULL),
                        ("sceAudioOutOpen: 0x%08x", (unsigned)self->handle));
      return FALSE;
    }
    port_opened(self->handle);
  }
  self->filled = 0;
  self->frames = 0;
  self->report_start = 0;
  self->in_port = 0;
  self->longest = 0;

  // Eight blocks in the ring buffer: 43 ms ahead of the port.
  spec->segsize = BLOCK_BYTES;
  spec->segtotal = 8;
  GST_INFO_OBJECT(sink, "port %d, %d frames a block", self->handle,
                  BLOCK_FRAMES);
  return TRUE;
}


static gboolean
ps5_audio_sink_unprepare(GstAudioSink* sink) {
  Ps5AudioSink* self = (Ps5AudioSink*)sink;

  ring_give_back(self);
  if(self->handle >= 0) {
    port_closing(self->handle);
    int32_t ret = sceAudioOutClose(self->handle);
    GST_INFO_OBJECT(sink, "closed port %d: 0x%08x", self->handle,
                    (unsigned)ret);
    self->handle = -1;
  }
  return TRUE;
}


static gboolean
ps5_audio_sink_close(GstAudioSink* sink) {
  (void)sink;
  return TRUE;
}


// The writing thread's priority. A payload's threads are SCHED_FIFO 700
// (256 is the highest, 767 the lowest), and a FIFO thread that wakes up
// waits for any busy thread of its priority to let go of the core: with
// YouTube, sceAudioOutOutput() calls took up to 280 ms for a 5.3 ms block,
// and the sound, the pipeline's clock, ran at 0.4x (V3, 2026-11-04). This
// thread waits on the port nearly all the time and copies 1 KiB a block,
// so above the others it cannot keep a core from anyone.
// BROWSERPS5_AUDIO_PRIORITY in env.txt overrides it.
#define WRITER_PRIORITY 600


// The writing thread (GStreamer's ring buffer thread), once per port: its
// priority goes to WRITER_PRIORITY, and its scheduling and the cores it may
// use go to the log.
static void
log_thread(Ps5AudioSink* self) {
  cpuset_t cpus;
  int policy = -1;
  struct sched_param param;
  const char* env = g_getenv("BROWSERPS5_AUDIO_PRIORITY");
  int priority = env ? atoi(env) : WRITER_PRIORITY;

  CPU_ZERO(&cpus);
  memset(&param, 0, sizeof(param));
  int affinity = pthread_getaffinity_np(pthread_self(), sizeof(cpus), &cpus);
  pthread_getschedparam(pthread_self(), &policy, &param);
  int was = param.sched_priority;
  int err = -1;
  if(priority >= sched_get_priority_min(policy) &&
     priority <= sched_get_priority_max(policy)) {
    param.sched_priority = priority;
    err = pthread_setschedparam(pthread_self(), policy, &param);
  }
  GST_INFO_OBJECT(self, "writing thread: policy %d priority %d -> %d (%s), "
                  "%d core(s) (mask 0x%lx)", policy, was, priority,
                  err < 0 ? "out of range" : err ? strerror(err) : "done",
                  affinity ? -1 : CPU_COUNT(&cpus), (unsigned long)cpus.__bits[0]);
}


// frames went out between before and after (microseconds): every
// REPORT_FRAMES, how long that took, to the log.
static void
account(Ps5AudioSink* self, guint frames, gint64 before, gint64 after) {
  if(!self->report_start) {
    self->report_start = before;
    log_thread(self);
  }
  self->in_port += after - before;
  self->longest = MAX(self->longest, after - before);
  self->frames += frames;
  if(self->frames >= REPORT_FRAMES) {
    gint64 took = after - self->report_start;
    if(self->slot) {
      GST_INFO_OBJECT(self, "title's ring %d: %.1f s of sound took %.2f s, "
                      "%.2f s of it waiting for room, the longest write %.1f ms",
                      (int)(self->slot - self->ring->slots),
                      self->frames / 48000.0, took / 1e6, self->in_port / 1e6,
                      self->longest / 1e3);
    } else {
      GST_INFO_OBJECT(self, "port %d: %.1f s of sound took %.2f s, %.2f s of it "
                      "in sceAudioOutOutput, the longest call %.1f ms "
                      "(a block is %.1f ms)", self->handle,
                      self->frames / 48000.0, took / 1e6, self->in_port / 1e6,
                      self->longest / 1e3, BLOCK_FRAMES * 1000.0 / 48000);
    }
    self->frames = 0;
    self->in_port = 0;
    self->longest = 0;
    self->report_start = after;
  }
}


// Into our ring of the title's as room comes: the title takes 256 frames
// every 5.3 ms, so this waits about that long when the ring is full. If the
// title stops playing (closed from the system), the sound is dropped at the
// pace of real time, so the pipeline goes on and can close. reset() makes
// it return early.
static void
ring_write(Ps5AudioSink* self, const guint8* data, guint frames) {
  audio_slot_t* slot = self->slot;

  while(frames && !g_atomic_int_get(&self->resetting)) {
    uint64_t written = slot->written;  // only this thread moves it
    uint64_t read = __atomic_load_n(&slot->read, __ATOMIC_ACQUIRE);
    uint64_t queued = written - read;
    guint room = queued < AUDIO_RING_FRAMES ? AUDIO_RING_FRAMES - (guint)queued
                                            : 0;
    if(!room) {
      if(!audio_ring_alive(self->ring)) {
        if(!self->dropping) {
          GST_WARNING_OBJECT(self, "the title stopped playing: the sound is "
                             "dropped from now on");
          self->dropping = TRUE;
        }
        g_usleep((gulong)frames * G_USEC_PER_SEC / AUDIO_RING_RATE);
        return;
      }
      g_usleep(RING_WAIT_US);
      continue;
    }
    guint at = (guint)(written % AUDIO_RING_FRAMES);
    guint n = MIN(frames, MIN(room, AUDIO_RING_FRAMES - at));
    memcpy(&slot->samples[at * 2], data, (gsize)n * 4);
    __atomic_store_n(&slot->written, written + n, __ATOMIC_RELEASE);
    data += (gsize)n * 4;
    frames -= n;
  }
}


static gint
ps5_audio_sink_write(GstAudioSink* sink, gpointer data, guint length) {
  Ps5AudioSink* self = (Ps5AudioSink*)sink;
  const guint8* samples = data;
  guint left = length;

  if(self->slot) {
    g_atomic_int_set(&self->resetting, 0);
    gint64 before = g_get_monotonic_time();
    ring_write(self, samples, length / 4);
    account(self, length / 4, before, g_get_monotonic_time());
    return length;
  }

  while(left) {
    guint n = MIN(left, BLOCK_BYTES - self->filled);
    memcpy(self->block + self->filled, samples, n);
    self->filled += n;
    samples += n;
    left -= n;
    if(self->filled == BLOCK_BYTES) {
      gint64 before = g_get_monotonic_time();
      int32_t ret = sceAudioOutOutput(self->handle, self->block);
      account(self, BLOCK_FRAMES, before, g_get_monotonic_time());
      self->filled = 0;
      if(ret < 0) {
        GST_ELEMENT_ERROR(sink, RESOURCE, WRITE, (NULL),
                          ("sceAudioOutOutput: 0x%08x", (unsigned)ret));
        return -1;
      }
    }
  }
  return length;
}


static guint
ps5_audio_sink_delay(GstAudioSink* sink) {
  Ps5AudioSink* self = (Ps5AudioSink*)sink;

  if(self->slot) {
    // What waits in the ring, and about one block in the title's port.
    uint64_t written = __atomic_load_n(&self->slot->written, __ATOMIC_ACQUIRE);
    uint64_t read = __atomic_load_n(&self->slot->read, __ATOMIC_ACQUIRE);
    uint64_t queued = written >= read ? written - read : 0;
    return (guint)MIN(queued, AUDIO_RING_FRAMES) + AUDIO_RING_BLOCK;
  }
  // What waits here, and about one block in the port.
  return self->filled / 4 + BLOCK_FRAMES;
}


static void
ps5_audio_sink_reset(GstAudioSink* sink) {
  Ps5AudioSink* self = (Ps5AudioSink*)sink;

  self->filled = 0;
  if(self->slot) {
    // The title skips what is in the ring (a seek, a pause), and a write()
    // that waits for room returns.
    g_atomic_int_set(&self->resetting, 1);
    __atomic_store_n(&self->slot->flush_to,
                     __atomic_load_n(&self->slot->written, __ATOMIC_ACQUIRE),
                     __ATOMIC_RELEASE);
  }
}


static void
ps5_audio_sink_class_init(Ps5AudioSinkClass* klass) {
  GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
  GstAudioSinkClass* sink_class = GST_AUDIO_SINK_CLASS(klass);

  gst_element_class_set_static_metadata(element_class, "PS5 audio sink",
      "Sink/Audio", "Plays audio through sceAudioOut", "BrowserPS5");
  gst_element_class_add_static_pad_template(element_class, &sink_template);

  sink_class->open = ps5_audio_sink_open;
  sink_class->prepare = ps5_audio_sink_prepare;
  sink_class->unprepare = ps5_audio_sink_unprepare;
  sink_class->close = ps5_audio_sink_close;
  sink_class->write = ps5_audio_sink_write;
  sink_class->delay = ps5_audio_sink_delay;
  sink_class->reset = ps5_audio_sink_reset;
}


static void
ps5_audio_sink_init(Ps5AudioSink* self) {
  self->handle = -1;
  self->ring = NULL;
  self->slot = NULL;
}


gboolean
browserps5_audio_sink_register(void) {
  GST_DEBUG_CATEGORY_INIT(ps5_audio_sink_debug, "ps5audiosink", 0,
                          "PS5 audio sink");
  guint rank = g_getenv("BROWSERPS5_NO_SOUND") ? GST_RANK_NONE : GST_RANK_PRIMARY;
  if(rank == GST_RANK_NONE) {
    GST_WARNING("BROWSERPS5_NO_SOUND: not picked by autoaudiosink, no sound");
  }
  // And the video's sink for the title (ps5videosink.c), which WebKit
  // makes only with its Westeros quirks on.
  browserps5_video_sink_register();
  return gst_element_register(NULL, "ps5audiosink", rank,
                              ps5_audio_sink_get_type());
}
