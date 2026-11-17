/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * 2026-11-09: the pages' video on the PS5's own video decoder
 * (libSceVideodec2, a system library), included by video.c (VIDEO_HWDEC).
 * The user, after 4K on the CPU stopped YouTube's buffer at ~17 s (8 FFmpeg
 * threads at 4K60 left the page and its downloads little): "descodifica o
 * video pela gpu".
 *
 * The same calls as the NK player that ran on this console up to 4K
 * (Desktop\BrowserPS5\poc\neko\shell\player.c, PL_HWDEC, 2026-11-08/09):
 *   - the library by its system module number (207) at the title's start,
 *     its functions imported at link time (build.sh links a stub), two
 *     compute queues made then (H.264, VP9: a queue made later failed);
 *   - a decoder on the compute queue, its memory as the library asks,
 *     direct memory of type 12 (CPU and GPU read/write), a pool of picture
 *     buffers, the access units in direct memory too;
 *   - VP9 superframes (YouTube's: a hidden reference frame and the shown one
 *     in one block) go in frame by frame, each tagged (its attached data:
 *     a count << 1 | hidden), and each picture out says its tag through
 *     GetPictureInfo: hidden ones are left out, the others get their time.
 * It puts out NV12, 8-bit: VP9 profile 0 and H.264 up to High; anything else
 * (10-bit VP9, HEVC, VP8, bigger than 4K, 3 errors in a row) goes to FFmpeg
 * as before. One video at a time has it (the first big one): YouTube's small
 * previews stay on FFmpeg.
 *
 * Each picture out is an AVFrame (NV12) whose buffer is a view of a picture
 * buffer in the pool: freeing the frame gives the buffer back. The pool
 * itself lives until the decoder and the last of those frames are gone (the
 * picture on the screen outlives a decoder made anew for a bigger video).
 */
#include <setjmp.h>

int sceSysmoduleLoadModule(uint16_t id);
int sceKernelMapFlexibleMemory(void **vaddr, size_t len, int prot, int flags);

struct vd2_config {
    uint64_t this_size;
    uint32_t resource_type, codec_type, profile, max_level;
    int32_t max_w, max_h, max_dpb;
    uint32_t depth;
    void *queue;
    uint64_t affinity;
    int32_t prio;
    uint8_t progressive, check_memory, r0, r1;
    void *extra;
};
_Static_assert(sizeof(struct vd2_config) == 0x48, "vd2_config");
struct vd2_memory {
    uint64_t this_size, cpu_size;
    void *cpu;
    uint64_t gpu_size;
    void *gpu;
    uint64_t cpu_gpu_size;
    void *cpu_gpu;
    uint64_t max_frame;
    uint32_t align, r0;
};
_Static_assert(sizeof(struct vd2_memory) == 0x48, "vd2_memory");
struct vd2_input {
    uint64_t this_size;
    const void *au;
    uint64_t au_size, pts, dts, attached;
};
_Static_assert(sizeof(struct vd2_input) == 0x30, "vd2_input");
struct vd2_frame {
    uint64_t this_size;
    void *buf;
    uint64_t size;
    uint8_t accepted, pad[7];
};
_Static_assert(sizeof(struct vd2_frame) == 0x20, "vd2_frame");
struct vd2_output {
    uint64_t this_size;
    uint8_t valid, error, pictures, pad0;
    uint32_t codec, w, pitch, h, pad1;
    uint8_t *buf;
    uint64_t size;
    uint32_t format, pitch_bytes;
};
_Static_assert(sizeof(struct vd2_output) == 0x38, "vd2_output");
struct vd2_avc_picture {
    uint64_t this_size;
    uint8_t valid;
    uint64_t pts, dts, attached;
    uint8_t idr, profile_idc, level_idc;
    uint32_t width_mbs_minus1, height_map_units_minus1;
    uint8_t frame_mbs_only, cropping;
    uint32_t crop_left, crop_right, crop_top, crop_bottom;
    uint8_t aspect_present, aspect_idc;
    uint16_t sar_w, sar_h;
    uint8_t signal_present, video_format, full_range, colour_present, primaries, transfer, matrix, timing_present;
    uint32_t units_in_tick, time_scale;
    uint8_t flags[21];
};
_Static_assert(sizeof(struct vd2_avc_picture) == 0x78, "vd2_avc_picture");
struct vd2_cmem {
    uint64_t this_size, size;
    void *mem;
};
_Static_assert(sizeof(struct vd2_cmem) == 0x18, "vd2_cmem");
struct vd2_ccfg {
    uint64_t this_size;
    uint16_t pipe, queue;
    uint8_t check_memory, r0;
    uint16_t r1;
};
_Static_assert(sizeof(struct vd2_ccfg) == 0x10, "vd2_ccfg");

// Imported at link time (videodec2_link_stub.c names them for the link);
// called through the addresses the loader put in the GOT (HW_GOT).
int sceVideodec2QueryComputeMemoryInfo(struct vd2_cmem *memory);
int sceVideodec2AllocateComputeQueue(const struct vd2_ccfg *config, const struct vd2_cmem *memory, void **queue);
int sceVideodec2QueryDecoderMemoryInfo(const struct vd2_config *config, struct vd2_memory *memory);
int sceVideodec2CreateDecoder(const struct vd2_config *config, const struct vd2_memory *memory, void **decoder);
int sceVideodec2Decode(void *decoder, const struct vd2_input *input, struct vd2_frame *frame,
                       struct vd2_output *output);
int sceVideodec2Reset(void *decoder);
int sceVideodec2DeleteDecoder(void *decoder);
int sceVideodec2GetPictureInfo(const struct vd2_output *output, void *first, void *second);
int sceVideodec2ReleaseComputeQueue(void *queue);  // (vdec_probe 2026-11-08: in the module)

// An import's address as the loader left it in the GOT: 0 while unresolved.
// Read in asm: the compiler takes a declared function's address as never 0.
#define HW_GOT(fn)                                                         \
    __extension__({                                                        \
        void *p_;                                                          \
        __asm__ volatile("movq " #fn "@GOTPCREL(%%rip), %0" : "=r"(p_));   \
        p_;                                                                \
    })

#define HW_PAGE ((size_t)0x4000)
#define HW_DIRECT 12      // direct memory the decoder works with (CPU and GPU)
#define HW_AVC 1          // the library's codec numbers
#define HW_VP9 2382845
#define HW_AUS 8          // access units: the chip reads them after Decode returns
#define HW_FRAMES 16      // picture buffers at most
#define HW_SMALL (1280 * 720)  // smaller videos stay on FFmpeg (previews)

struct hw_mem {
    uint8_t *addr;
    size_t len;
    off_t start;  // direct memory; -1: flexible
};

// The picture buffers: the decoder's, then a picture's (in the queue or on
// the screen), then free again.
typedef struct hw_pool {
    struct hw_mem mem;
    size_t frame_size;
    int count;
    uint8_t dec[HW_FRAMES];  // the decoder has it (the decoder thread's)
    uint8_t out[HW_FRAMES];  // a picture has it (atomic)
    int refs;                // the decoder's 1, and one a picture (atomic)
} hw_pool_t;

typedef struct hw_ref {
    hw_pool_t *pool;
    int index;
} hw_ref_t;

static struct {
    int enabled;  // /app0/video.txt hw=0 turns it off
    int loaded;   // 1 yes, -1 no
    int (*query)(const struct vd2_config *, struct vd2_memory *);
    int (*create)(const struct vd2_config *, const struct vd2_memory *, void **);
    int (*decode)(void *, const struct vd2_input *, struct vd2_frame *, struct vd2_output *);
    int (*reset)(void *);
    int (*picinfo)(const struct vd2_output *, void *, void *);
    int (*del)(void *);
    // 2026-11-09 18:5x: the console went down twice right after the title went
    // to the background (the PS button) with the compute queues made at the
    // start still there: closing the title from the system with its GPU work
    // still held is what took it down before (title.cpp, "The pause"). So the
    // queues come with the first video, and go when the title is away.
    int (*cmem)(struct vd2_cmem *);
    int (*qalloc)(const struct vd2_ccfg *, const struct vd2_cmem *, void **);
    int (*qrelease)(void *);  // sceVideodec2ReleaseComputeQueue
    int away;                 // __atomic: the system is over the title: no hardware
    void *queue, *queue9;
    struct hw_mem qmem, qmem9, cpu;

    decoder_t *owner;  // the slot that has it (one video at a time)
    pthread_mutex_t owner_lock;

    // The owner's decoder (its thread's).
    void *dec;
    uint32_t codec;
    struct vd2_config c;
    struct vd2_memory m;
    struct hw_mem gpu, cg, au[HW_AUS];
    hw_pool_t *pool;
    size_t au_size;
    int next_au;
    int vw, vh;        // VP9: the size its last key frame said
    uint64_t tag;      // VP9 frames fed (each one's attached data: tag << 1 | hidden)
    int pi_size;       // the VP9 picture info's size: 0 not tried, -1 none works
    int64_t q_pts[64]; // VP9: one a frame given, in that order
    uint8_t q_hidden[64];
    int q_n;
    uint8_t head[1024];  // H.264: the SPS and PPS, Annex B, before each key frame
    size_t head_n;
    int nal_len;         // its NAL length size (avcC); 0: Annex B already
    int wait_key, resyncs, fails, errors, said;
    unsigned pictures, fallbacks;  // (status)
    // 17:1x, 4K60 had ~13% of its pictures late (1080p none): per status line,
    // the time in Decode (all, the most), the waits for a free buffer, and how
    // far ahead of the clock the pictures came out (mean, least)
    double busy_ms, busy_max, lead_sum, lead_min;
    unsigned calls, waits, leads;
} hw = {.enabled = 1, .owner_lock = PTHREAD_MUTEX_INITIALIZER};

static int hw_direct(struct hw_mem *m, size_t len, size_t align)
{
    m->len = (len + HW_PAGE - 1) & ~(HW_PAGE - 1);
    m->addr = NULL;
    m->start = -1;
    if (align < HW_PAGE)
        align = HW_PAGE;
    void *va = NULL;
    off_t start = -1;
    int r = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(), m->len, align, HW_DIRECT, &start);
    if (!r) {
        r = sceKernelMapDirectMemory(&va, m->len, 0x33, 0, start, align);  // CPU and GPU read/write
        if (r)
            sceKernelReleaseDirectMemory(start, m->len);
    }
    if (r) {
        VLOG("hardware: %zu KiB of direct memory: 0x%08x", m->len >> 10, (unsigned)r);
        return 0;
    }
    m->addr = va;
    m->start = start;
    return 1;
}

static int hw_flexible(struct hw_mem *m, size_t len)
{
    m->len = (len + HW_PAGE - 1) & ~(HW_PAGE - 1);
    m->start = -1;
    void *va = NULL;
    const int r = sceKernelMapFlexibleMemory(&va, m->len, 0x03, 0);  // CPU read/write
    if (r) {
        VLOG("hardware: %zu KiB of flexible memory: 0x%08x", m->len >> 10, (unsigned)r);
        m->addr = NULL;
        return 0;
    }
    m->addr = va;
    return 1;
}

static void hw_free(struct hw_mem *m)
{
    if (!m->addr)
        return;
    munmap(m->addr, m->len);
    if (m->start >= 0)
        sceKernelReleaseDirectMemory(m->start, m->len);
    m->addr = NULL;
    m->start = -1;
}

static void hw_pool_unref(hw_pool_t *p)
{
    if (__atomic_sub_fetch(&p->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        hw_free(&p->mem);
        free(p);
    }
}

// A picture's frame freed (the display took a newer one, a seek, the end).
static void hw_picture_free(void *opaque, uint8_t *data)
{
    hw_ref_t *r = opaque;
    (void)data;
    __atomic_store_n(&r->pool->out[r->index], 0, __ATOMIC_RELEASE);
    hw_pool_unref(r->pool);
    free(r);
}

// The query with faults caught (in NK the first query ended the title while
// an import was unresolved): its result, or 0x7fff0000 | the signal.
static sigjmp_buf hw_jmp;
static volatile sig_atomic_t hw_sig;

static void hw_on_fault(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;
    hw_sig = sig;
    siglongjmp(hw_jmp, 1);
}

static int hw_guard(const struct vd2_config *c, struct vd2_memory *m)
{
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};
    struct sigaction sa, old[5];
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = hw_on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < 5; i++)
        sigaction(sigs[i], &sa, &old[i]);
    volatile int r = 0x7fff0000;
    hw_sig = 0;
    if (!sigsetjmp(hw_jmp, 1))
        r = hw.query(c, m);
    else
        r = 0x7fff0000 | (int)hw_sig;
    for (int i = 0; i < 5; i++)
        sigaction(sigs[i], &old[i], NULL);
    return r;
}

static void *hw_queue(int (*cmem)(struct vd2_cmem *),
                      int (*alloc)(const struct vd2_ccfg *, const struct vd2_cmem *, void **), struct hw_mem *mem,
                      const char *what)
{
    struct vd2_cmem cm = {sizeof(cm), 0, NULL};
    int r = cmem(&cm);
    void *q = NULL;
    if (!r && cm.size && hw_direct(mem, cm.size, HW_PAGE)) {
        cm.mem = mem->addr;
        cm.size = mem->len;
        struct vd2_ccfg cc;
        memset(&cc, 0, sizeof(cc));
        cc.this_size = sizeof(cc);
        r = alloc(&cc, &cm, &q);
        if (r || !q) {
            q = NULL;
            hw_free(mem);
        }
    }
    VLOG("hardware: %s compute queue 0x%08x (%p, %llu KiB)", what, (unsigned)r, q,
         (unsigned long long)(cm.size >> 10));
    return q;
}

// The compute queues, for the first video (under owner_lock): 1 if there.
static int hw_queues_up(void)
{
    if (hw.queue)
        return 1;
    hw.queue = hw_queue(hw.cmem, hw.qalloc, &hw.qmem, "H.264");
    if (!hw.queue)
        return 0;
    hw.queue9 = hw_queue(hw.cmem, hw.qalloc, &hw.qmem9, "VP9");  // none: VP9 shares the other
    return 1;
}

// The queues away (under owner_lock, no decoder on them).
static void hw_queues_down(void)
{
    if (!hw.queue && !hw.queue9)
        return;
    if (!hw.qrelease) {
        VLOG("hardware: the compute queues stay (no ReleaseComputeQueue)");
        return;
    }
    int r9 = 0, r = 0;
    if (hw.queue9) {
        r9 = hw.qrelease(hw.queue9);
        hw.queue9 = NULL;
        hw_free(&hw.qmem9);
    }
    if (hw.queue) {
        r = hw.qrelease(hw.queue);
        hw.queue = NULL;
        hw_free(&hw.qmem);
    }
    VLOG("hardware: compute queues released (0x%08x, 0x%08x)", (unsigned)r, (unsigned)r9);
}

// The library, once, at the title's start (its queues come with a video).
static void hw_load(void)
{
    if (hw.loaded || !hw.enabled) {
        if (!hw.enabled)
            VLOG("hardware: off (video.txt hw=0): FFmpeg only");
        return;
    }
    hw.loaded = -1;
    const int r = sceSysmoduleLoadModule(207);
    void *const f[8] = {HW_GOT(sceVideodec2QueryComputeMemoryInfo), HW_GOT(sceVideodec2AllocateComputeQueue),
                        HW_GOT(sceVideodec2QueryDecoderMemoryInfo),  HW_GOT(sceVideodec2CreateDecoder),
                        HW_GOT(sceVideodec2Decode),                  HW_GOT(sceVideodec2Reset),
                        HW_GOT(sceVideodec2GetPictureInfo),          HW_GOT(sceVideodec2DeleteDecoder)};
    VLOG("hardware: system module 207 (libSceVideodec2): 0x%08x; imports %p %p %p %p %p %p %p %p", (unsigned)r,
         f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
    for (int i = 0; i < 8; i++) {
        if (!f[i]) {
            VLOG("hardware: imports not resolved: FFmpeg only");
            return;
        }
    }
    hw.query = (int (*)(const struct vd2_config *, struct vd2_memory *))f[2];
    hw.create = (int (*)(const struct vd2_config *, const struct vd2_memory *, void **))f[3];
    hw.decode = (int (*)(void *, const struct vd2_input *, struct vd2_frame *, struct vd2_output *))f[4];
    hw.reset = (int (*)(void *))f[5];
    hw.picinfo = (int (*)(const struct vd2_output *, void *, void *))f[6];
    hw.del = (int (*)(void *))f[7];
    hw.cmem = (int (*)(struct vd2_cmem *))f[0];
    hw.qalloc = (int (*)(const struct vd2_ccfg *, const struct vd2_cmem *, void **))f[1];
    hw.qrelease = (int (*)(void *))HW_GOT(sceVideodec2ReleaseComputeQueue);
    VLOG("hardware: ReleaseComputeQueue %p; the compute queues come with the first video", (void *)hw.qrelease);
    hw.loaded = 1;
}

// From the title (away_send): the system is over it (away), or no more. Away:
// a video on the hardware lets go of it from its own thread (decoder_thread),
// and with no video on it the queues go now.
void video_away(int away)
{
    if (hw.loaded <= 0)
        return;
    pthread_mutex_lock(&hw.owner_lock);
    __atomic_store_n(&hw.away, away ? 1 : 0, __ATOMIC_RELEASE);
    if (away && !hw.owner)
        hw_queues_down();
    pthread_mutex_unlock(&hw.owner_lock);
#ifdef TITLE_RN
    // 2026-11-14: the video that has the hardware lets go of it on its own thread, a moment later; the
    // title went on at once, and what it said to the browser, and the browser to the system, could come
    // before that. Away now waits for it (the thread looks every 2 ms; 500 ms at most) and says how long
    // it took and what is still held: when the system closes the title, nothing of the decoder's is on
    // the GPU.
    if (away) {
        const uint64_t t0 = video_ring_now_ns();
        int held = 1;
        while (held && video_ring_now_ns() - t0 < 500000000ull) {
            pthread_mutex_lock(&hw.owner_lock);
            held = hw.owner != NULL || hw.dec != NULL || hw.queue != NULL || hw.queue9 != NULL;
            pthread_mutex_unlock(&hw.owner_lock);
            if (held)
                usleep(2000);
        }
        VLOG("hardware: away: %s after %.0f ms; no hardware until back", held ? "STILL HELD" : "nothing of it is held",
             (double)(video_ring_now_ns() - t0) / 1e6);
        return;
    }
#endif
    VLOG("hardware: %s", away ? "away: no hardware until back" : "back");
}

static void hw_config(struct vd2_config *c, uint32_t codec, int w, int h, uint32_t depth, uint32_t level)
{
    memset(c, 0, sizeof(*c));
    c->this_size = sizeof(*c);
    c->resource_type = 1;  // decoding on the compute queue
    c->codec_type = codec;
    c->profile = codec == HW_AVC ? 100 : 0;  // H.264 up to High; VP9 profile 0
    c->max_level = level;
    c->max_w = w;
    c->max_h = h;
    c->max_dpb = -1;
    c->depth = depth;
    c->queue = codec == HW_VP9 && hw.queue9 ? hw.queue9 : hw.queue;
    c->affinity = 0x3f;
    c->prio = 700;  // as NK and VLC; they wait on the chip, never spin
    c->progressive = 1;
}

// The decoders' CPU memory: one block, kept for the next one (VLC: flexible
// memory given back did not come back).
static int hw_cpu(size_t len)
{
    if (hw.cpu.addr && hw.cpu.len >= len)
        return 1;
    hw_free(&hw.cpu);
    return hw_flexible(&hw.cpu, len);
}

static void hw_free_decoder_memory(void)
{
    hw_free(&hw.gpu);
    hw_free(&hw.cg);
    for (int i = 0; i < HW_AUS; i++)
        hw_free(&hw.au[i]);
    if (hw.pool) {
        hw_pool_unref(hw.pool);  // its memory goes with the last picture out
        hw.pool = NULL;
    }
}

// The memory for this config and the decoder on it: 0, or the error.
static int hw_bring_up(uint32_t codec, int w, int h, uint32_t depth, uint32_t level)
{
    hw_config(&hw.c, codec, w, h, depth, level);
    memset(&hw.m, 0, sizeof(hw.m));
    hw.m.this_size = sizeof(hw.m);
    int r = hw_guard(&hw.c, &hw.m);
    VLOG("hardware: %s %dx%d level %u depth %u: memory 0x%08x: cpu %llu KiB, gpu %llu KiB, cpu+gpu %llu KiB, "
         "picture %llu KiB (align 0x%x)",
         codec == HW_AVC ? "H.264" : "VP9", w, h, level, depth, (unsigned)r, (unsigned long long)(hw.m.cpu_size >> 10),
         (unsigned long long)(hw.m.gpu_size >> 10), (unsigned long long)(hw.m.cpu_gpu_size >> 10),
         (unsigned long long)(hw.m.max_frame >> 10), hw.m.align);
    if (r)
        return r;
    const size_t align = hw.m.align > HW_PAGE ? hw.m.align : HW_PAGE;
    const size_t frame_size = ((size_t)hw.m.max_frame + align - 1) & ~(align - 1);
    if (!frame_size || frame_size > ((size_t)64 << 20))
        return -1;
    hw.au_size = w * h <= 1920 * 1088 ? (size_t)2 << 20 : (size_t)4 << 20;
    hw_pool_t *pool = calloc(1, sizeof(*pool));
    if (!pool)
        return -1;
    pool->refs = 1;
    pool->frame_size = frame_size;
    pool->mem.start = -1;
    hw.pool = pool;
    int ok = (!hw.m.cpu_size || hw_cpu(hw.m.cpu_size)) &&
             (!hw.m.gpu_size || hw_direct(&hw.gpu, hw.m.gpu_size, HW_PAGE)) &&
             (!hw.m.cpu_gpu_size || hw_direct(&hw.cg, hw.m.cpu_gpu_size, HW_PAGE));
    for (int n = HW_FRAMES; ok && n >= 8 && !pool->count; n -= 4) {
        if (hw_direct(&pool->mem, frame_size * (size_t)n, align))
            pool->count = n;
    }
    ok = ok && pool->count;
    for (int i = 0; ok && i < HW_AUS; i++)
        ok = hw_direct(&hw.au[i], hw.au_size, HW_PAGE);
    r = -1;
    if (ok) {
        // as new: direct memory comes back as it was left; pictures mid-grey
        if (hw.m.cpu_size)
            memset(hw.cpu.addr, 0, (size_t)hw.m.cpu_size);
        if (hw.gpu.addr)
            memset(hw.gpu.addr, 0, hw.gpu.len);
        if (hw.cg.addr)
            memset(hw.cg.addr, 0, hw.cg.len);
        memset(pool->mem.addr, 0x80, pool->mem.len);
        hw.m.cpu = hw.m.cpu_size ? hw.cpu.addr : NULL;
        hw.m.gpu = hw.gpu.addr;
        hw.m.cpu_gpu = hw.cg.addr;
        void *dec = NULL;
        r = hw.create(&hw.c, &hw.m, &dec);
        VLOG("hardware: decoder 0x%08x (%p): %d picture buffers of %zu KiB", (unsigned)r, dec, pool->count,
             frame_size >> 10);
        if (!r && dec) {
            hw.dec = dec;
            return 0;
        }
        if (!r)
            r = -1;
    }
    hw_free_decoder_memory();
    return r;
}

static void hw_close(void)
{
    if (!hw.dec)
        return;
    const int r = hw.del(hw.dec);
    VLOG("hardware: decoder deleted 0x%08x (%u pictures)", (unsigned)r, hw.pictures);
    hw.dec = NULL;
    hw_free_decoder_memory();
    hw.q_n = 0;
}

// A decoder for this codec up to w x h: pipeline depth 4, else 1; VP9 also
// at lower levels (VLC's list). 0x811D01xx is the chip itself: not retried.
static int hw_open(uint32_t codec, int w, int h)
{
    const uint32_t level = codec == HW_AVC ? 52 : 62;
    int r = hw_bring_up(codec, w, h, 4, level);
    if (r && ((unsigned)r & 0xFFFFFF00u) != 0x811D0100u)
        r = hw_bring_up(codec, w, h, 1, level);
    static const uint32_t vp9_levels[] = {61, 52, 51};
    for (int i = 0; r && codec == HW_VP9 && i < 3 && ((unsigned)r & 0xFFFFFF00u) != 0x811D0100u; i++)
        r = hw_bring_up(codec, w, h, 1, vp9_levels[i]);
    if (r)
        return 0;
    hw.codec = codec;
    hw.q_n = 0;
    hw.next_au = 0;
    hw.said = 0;
    hw.wait_key = codec == HW_VP9;
    return 1;
}

// Everything in the decoder away (a seek, the end): its buffers free again
// (the pictures out keep theirs until they go).
static void hw_reset(void)
{
    if (!hw.dec)
        return;
    hw.reset(hw.dec);
    if (hw.pool)
        memset(hw.pool->dec, 0, sizeof(hw.pool->dec));
    hw.q_n = 0;
    hw.wait_key = hw.codec == HW_VP9;  // VP9 again from a key frame (the chip refuses a start elsewhere)
}

// A track's avcC: its NAL length size and its SPS and PPS, as Annex B.
static void hw_avcc(const uint8_t *x, size_t n)
{
    hw.head_n = 0;
    hw.nal_len = 0;
    if (!x || n < 7 || x[0] != 1)
        return;  // not avcC: the samples are taken as Annex B
    hw.nal_len = (x[4] & 3) + 1;
    size_t at = 5;
    for (int list = 0; list < 2; list++) {  // the SPSs, then the PPSs
        if (at >= n)
            return;
        int count = list ? x[at] : x[at] & 31;
        at++;
        while (count-- > 0 && at + 2 <= n) {
            const size_t len = (size_t)x[at] << 8 | x[at + 1];
            at += 2;
            if (len > n - at || hw.head_n + 4 + len > sizeof(hw.head))
                return;
            memcpy(hw.head + hw.head_n, "\0\0\0\1", 4);
            memcpy(hw.head + hw.head_n + 4, x + at, len);
            hw.head_n += 4 + len;
            at += len;
        }
    }
}

static unsigned hw_bits(const uint8_t *p, size_t n, size_t *bit, int k)
{
    unsigned v = 0;
    while (k-- > 0) {
        const size_t i = *bit >> 3;
        v = v << 1 | (i < n ? (unsigned)(p[i] >> (7 - (*bit & 7))) & 1u : 0u);
        ++*bit;
    }
    return v;
}

// VP9 (the format's spec): one frame's first header fields: shown or not,
// and on a key frame its profile and size. 0: not a VP9 frame.
static int hw_vp9_head(const uint8_t *p, size_t n, int *shows, int *key, int *profile, int *w, int *h)
{
    size_t b = 0;
    if (!n || hw_bits(p, n, &b, 2) != 2)
        return 0;
    const int lo = (int)hw_bits(p, n, &b, 1);
    const int prof = (int)hw_bits(p, n, &b, 1) << 1 | lo;
    if (prof == 3)
        hw_bits(p, n, &b, 1);
    *key = 0;
    if (hw_bits(p, n, &b, 1)) {  // show_existing_frame
        *shows = 1;
        return 1;
    }
    const int type = (int)hw_bits(p, n, &b, 1);
    *shows = (int)hw_bits(p, n, &b, 1);
    hw_bits(p, n, &b, 1);  // error_resilient_mode
    if (type == 0) {       // a key frame: sync code, colour config, size
        if (hw_bits(p, n, &b, 24) != 0x498342)
            return 0;
        if (prof >= 2)
            hw_bits(p, n, &b, 1);
        if (hw_bits(p, n, &b, 3) != 7) {  // not RGB
            hw_bits(p, n, &b, 1);
            if (prof == 1 || prof == 3)
                hw_bits(p, n, &b, 3);
        } else if (prof == 1 || prof == 3) {
            hw_bits(p, n, &b, 1);
        }
        *key = 1;
        *profile = prof;
        *w = (int)hw_bits(p, n, &b, 16) + 1;
        *h = (int)hw_bits(p, n, &b, 16) + 1;
    }
    return 1;
}

// A VP9 block: one frame, or a superframe's (an index at its end). Its
// frames: offsets, sizes, shown or not; shown (any), key (any) with profile
// and size. 0: not understood.
static int hw_vp9_block(const uint8_t *p, size_t n, int *shows, int *key, int *profile, int *w, int *h,
                        size_t *off, size_t *len, int *fshow)
{
    int frames = 1;
    off[0] = 0;
    len[0] = n;
    const uint8_t m = n ? p[n - 1] : 0;
    if ((m & 0xe0) == 0xc0) {
        const int count = (m & 7) + 1, bytes = ((m >> 3) & 3) + 1;
        const size_t index = 2 + (size_t)bytes * (size_t)count;
        if (n >= index && p[n - index] == m) {
            size_t at = 0;
            for (int i = 0; i < count; i++) {
                size_t s = 0;
                for (int k = 0; k < bytes; k++)
                    s |= (size_t)p[n - index + 1 + (size_t)(i * bytes + k)] << (8 * k);
                if (!s || at + s > n - index)
                    return 0;
                off[i] = at;
                len[i] = s;
                at += s;
            }
            frames = count;
        }
    }
    *shows = *key = 0;
    for (int i = 0; i < frames; i++) {
        int s = 0, k = 0;
        if (!hw_vp9_head(p + off[i], len[i], &s, &k, profile, w, h))
            return 0;
        fshow[i] = s;
        *shows |= s;
        *key |= k;
    }
    return frames;
}

static int hw_free_buffers(void)
{
    int n = 0;
    for (int i = 0; hw.pool && i < hw.pool->count; i++)
        n += !hw.pool->dec[i] && !__atomic_load_n(&hw.pool->out[i], __ATOMIC_ACQUIRE);
    return n;
}

static int hw_slot(const uint8_t *buf)
{
    hw_pool_t *p = hw.pool;
    if (!buf || !p || !p->mem.addr || buf < p->mem.addr)
        return -1;
    const size_t i = (size_t)(buf - p->mem.addr) / p->frame_size;
    return i < (size_t)p->count ? (int)i : -1;
}

// A picture out of the decoder into d's queue, as an NV12 frame on its buffer.
static void hw_hand_on(decoder_t *d, int os, const uint8_t *buf, int w, int h, int cl, int ct, size_t pitch,
                       uint32_t oh, int64_t when, int key)
{
    hw_pool_t *p = hw.pool;
    AVFrame *fr = av_frame_alloc();
    hw_ref_t *ref = malloc(sizeof(*ref));
    if (fr && ref) {
        ref->pool = p;
        ref->index = os;
        __atomic_store_n(&p->out[os], 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&p->refs, 1, __ATOMIC_ACQ_REL);
        p->dec[os] = 0;
        fr->buf[0] = av_buffer_create((uint8_t *)buf, p->frame_size, hw_picture_free, ref, 0);
        if (!fr->buf[0]) {
            __atomic_store_n(&p->out[os], 0, __ATOMIC_RELEASE);
            __atomic_sub_fetch(&p->refs, 1, __ATOMIC_ACQ_REL);
            free(ref);
            av_frame_free(&fr);
            return;
        }
        fr->format = AV_PIX_FMT_NV12;
        fr->width = w;
        fr->height = h;
        fr->data[0] = (uint8_t *)buf + (size_t)ct * pitch + (size_t)cl;
        fr->data[1] = (uint8_t *)buf + pitch * oh + (size_t)(ct / 2) * pitch + (size_t)cl;
        fr->linesize[0] = fr->linesize[1] = (int)pitch;
        fr->extended_data = fr->data;
        fr->pts = when;
        fr->best_effort_timestamp = when;
        fr->flags = key ? AV_FRAME_FLAG_KEY : 0;
        hw.pictures++;
        {
            int playing = 0;
            const int64_t running = video_slot_running(d->slot, video_ring_now_ns(), &playing);
            if (playing) {
                const double lead = (double)(when - running) / 1e6;
                hw.lead_sum += lead;
                if (!hw.leads || lead < hw.lead_min)
                    hw.lead_min = lead;
                hw.leads++;
            }
        }
        queue_picture(d, fr);  // (its reference moves into the queue, or goes)
        av_frame_free(&fr);
        return;
    }
    free(ref);
    av_frame_free(&fr);
    p->dec[os] = 0;
}

// One access unit (VP9: one frame) into the decoder; hidden: its picture is
// left out. 0, or -1: an error.
static int hw_one(decoder_t *d, const uint8_t *p, size_t n, int key, int64_t pts, int hidden)
{
    hw_pool_t *pool = hw.pool;
    int slot = -1;
    for (int i = 0; i < pool->count && slot < 0; i++) {
        if (!pool->dec[i] && !__atomic_load_n(&pool->out[i], __ATOMIC_ACQUIRE))
            slot = i;
    }
    if (slot < 0)
        return 0;  // (hw_decode looked first: never)
    uint8_t *const au = hw.au[hw.next_au].addr;
    hw.next_au = (hw.next_au + 1) % HW_AUS;
    size_t k = 0;
    if (key && hw.head_n) {
        memcpy(au, hw.head, hw.head_n);
        k = hw.head_n;
    }
    if (hw.nal_len) {
        size_t at = 0;
        while (at + (size_t)hw.nal_len <= n) {
            size_t len = 0;
            for (int i = 0; i < hw.nal_len; i++)
                len = len << 8 | p[at + (size_t)i];
            at += (size_t)hw.nal_len;
            if (len > n - at || k + 4 + len > hw.au_size)
                return -1;
            memcpy(au + k, "\0\0\0\1", 4);
            memcpy(au + k + 4, p + at, len);
            k += 4 + len;
            at += len;
        }
    } else {
        if (k + n > hw.au_size)
            return -1;
        memcpy(au + k, p, n);
        k += n;
    }
    const uint64_t tag = hw.codec == HW_VP9 ? (++hw.tag << 1 | (uint64_t)!!hidden) : (uint64_t)key;
    const struct vd2_input in = {sizeof(in), au, k, (uint64_t)pts, UINT64_MAX, tag};
    struct vd2_frame fb = {sizeof(fb), pool->mem.addr + (size_t)slot * pool->frame_size, pool->frame_size, 0, {0}};
    struct vd2_output o;
    memset(&o, 0, sizeof(o));
    o.this_size = sizeof(o);
    const int r = hw.decode(hw.dec, &in, &fb, &o);
    if (r) {
        if (hw.errors++ < 6)
            VLOG("hardware: decode 0x%08x (%zu bytes, key %d)", (unsigned)r, k, key);
        // A stream error leaves the decoder refusing what follows: reset, and
        // again from the next key frame, 5 times a track at most.
        if ((unsigned)r >= 0x811D0300u && (unsigned)r <= 0x811D0304u && hw.resyncs < 5) {
            hw.resyncs++;
            hw_reset();
            hw.wait_key = 1;
            VLOG("hardware: stream error 0x%08x: reset, from the next key frame (%d of 5)", (unsigned)r, hw.resyncs);
            return 0;
        }
        return -1;
    }
    if (fb.accepted)
        pool->dec[slot] = 1;  // the decoder's until its picture is out
    if (hw.codec == HW_VP9 && hw.q_n < 64) {
        hw.q_pts[hw.q_n] = pts;
        hw.q_hidden[hw.q_n] = (uint8_t)hidden;
        hw.q_n++;
    }
    if (!o.valid || !o.buf)
        return 0;
    int64_t when = pts;
    hidden = 0;
    int told = 0;  // the chip said which frame this is
    if (hw.codec == HW_VP9 && hw.pi_size >= 0) {
        uint64_t pv[64];
        int ri = -1;
        for (int sz = hw.pi_size ? hw.pi_size : 0x28; sz <= (hw.pi_size ? hw.pi_size : 0x200); sz += 8) {
            memset(pv, 0, sizeof(pv));
            pv[0] = (uint64_t)sz;
            ri = hw.picinfo(&o, pv, NULL);
            if (!ri && (pv[1] & 0xff)) {
                if (!hw.pi_size) {
                    hw.pi_size = sz;
                    VLOG("hardware: VP9 picture info: size 0x%x", sz);
                }
                break;
            }
            ri = -1;
        }
        const uint64_t a = pv[4], t = a >> 1;
        if (!ri && t && t <= hw.tag && hw.tag - t < 64) {
            told = 1;
            when = (int64_t)pv[2];
            hidden = (int)(a & 1);
            for (int i = 0; i < hw.q_n; i++) {
                if (hw.q_pts[i] == when && hw.q_hidden[i] == (uint8_t)hidden) {
                    hw.q_n--;
                    memmove(hw.q_pts + i, hw.q_pts + i + 1, (size_t)(hw.q_n - i) * sizeof(hw.q_pts[0]));
                    memmove(hw.q_hidden + i, hw.q_hidden + i + 1, (size_t)(hw.q_n - i));
                    break;
                }
            }
        } else if (!hw.pi_size) {
            hw.pi_size = -1;
            VLOG("hardware: no VP9 picture info (0x%08x): the order is guessed", (unsigned)ri);
        }
    }
    if (hw.codec == HW_VP9 && hw.q_n && !told) {
        // NK, 2026-11-09: the chip gives a hidden frame one place early,
        // before the shown frame that went in ahead of it
        const int at = hw.q_n >= 2 && !hw.q_hidden[0] && hw.q_hidden[1];
        when = hw.q_pts[at];
        hidden = hw.q_hidden[at];
        hw.q_n--;
        memmove(hw.q_pts + at, hw.q_pts + at + 1, (size_t)(hw.q_n - at) * sizeof(hw.q_pts[0]));
        memmove(hw.q_hidden + at, hw.q_hidden + at + 1, (size_t)(hw.q_n - at));
    }
    const int os = hw_slot(o.buf);
    if (os >= 0 && (hidden || o.error)) {
        pool->dec[os] = 0;
        return 0;
    }
    if (os < 0)
        return 0;
    struct vd2_avc_picture pi;
    memset(&pi, 0, sizeof(pi));
    pi.this_size = sizeof(pi);
    int w = (int)o.w, h = (int)o.h, cl = 0, ct = 0, idr = key;
    const int ri = hw.codec == HW_AVC ? hw.picinfo(&o, &pi, NULL) : -1;
    if (hw.codec == HW_VP9 && hw.vw > 1 && hw.vw <= w && hw.vh > 1 && hw.vh <= h) {
        w = hw.vw;  // the key frame's size (the decoder's rows may be padded)
        h = hw.vh;
    }
    if (!ri && pi.valid) {
        const int fields = pi.frame_mbs_only ? 1 : 2;
#ifdef TITLE_RN
        // 2026-11-14: every 1080p H.264 picture came out as "shown 1920x1072" (13 of 13 in the logs,
        // YouTube's too): the chip gives the crop in pixels (8 for 1088 to 1080), not in the stream's
        // units of 2 that the lines below take it for, so 8 rows were cut and the picture stretched.
        // Both readings are worked out; the one that gives the size the stream's own description says
        // wins, the pixels one if neither does.
        const int cw = (int)(pi.width_mbs_minus1 + 1) * 16, ch = (int)(pi.height_map_units_minus1 + 1) * 16 * fields;
        const int l1 = pi.cropping ? (int)pi.crop_left : 0, r1 = pi.cropping ? (int)pi.crop_right : 0;
        const int t1 = pi.cropping ? (int)pi.crop_top : 0, b1 = pi.cropping ? (int)pi.crop_bottom : 0;
        const int units = (int)d->format.width == cw - 2 * (l1 + r1) && (int)d->format.height == ch - 2 * fields * (t1 + b1)
                          && !((int)d->format.width == cw - (l1 + r1) && (int)d->format.height == ch - (t1 + b1));
        const int mx = units ? 2 : 1, my = units ? 2 * fields : 1;
        const int x0 = mx * l1, y0 = my * t1;
        const int pw = cw - mx * (l1 + r1);
        const int ph = ch - my * (t1 + b1);
#else
        const int x0 = pi.cropping ? 2 * (int)pi.crop_left : 0, y0 = pi.cropping ? 2 * fields * (int)pi.crop_top : 0;
        const int pw = (int)(pi.width_mbs_minus1 + 1) * 16 - (pi.cropping ? 2 * (int)(pi.crop_left + pi.crop_right) : 0);
        const int ph = (int)(pi.height_map_units_minus1 + 1) * 16 * fields -
                       (pi.cropping ? 2 * fields * (int)(pi.crop_top + pi.crop_bottom) : 0);
#endif
        if (pw > 1 && x0 + pw <= w && ph > 1 && y0 + ph <= h) {
            w = pw;
            h = ph;
            cl = x0 & ~1;
            ct = y0 & ~1;
        }
        when = (int64_t)pi.pts;
        idr = pi.idr;
    }
    const size_t pitch = o.pitch_bytes ? o.pitch_bytes : o.pitch;
    if (!hw.said) {
        hw.said = 1;
        VLOG("hardware: first picture %ux%u, pitch %u (%u bytes), format %u: shown %dx%d", o.w, o.h, o.pitch,
             o.pitch_bytes, o.format, w, h);
    }
    if (w < 2 || h < 2 || !pitch) {
        pool->dec[os] = 0;
        return 0;
    }
    hw_hand_on(d, os, o.buf, w, h, cl, ct, pitch, o.h, when, idr);
    return 0;
}

// The ring's format for d's new video: 1 if the hardware takes it.
static void hw_drop(decoder_t *d);
static int hw_take_video(decoder_t *d, const video_format_t *format, const uint8_t *config)
{
    const uint32_t codec = format->codec == VIDEO_CODEC_H264 ? HW_AVC : format->codec == VIDEO_CODEC_VP9 ? HW_VP9 : 0;
    const int w = (int)format->width, h = (int)format->height;
    // room for 1920x1088 at least (quality changes up to 1080p need no new
    // decoder), the video's own size above that
    int dw = (w + 15) & ~15, dh = (h + 15) & ~15;
    dw = dw > 1920 ? dw : 1920;
    dh = dh > 1088 ? dh : 1088;
    if (hw.loaded <= 0 || !codec || (w > 0 && h > 0 && w * h < HW_SMALL)  // a preview: FFmpeg has it
        || dw > 3840 || dh > 3840 || dw * dh > 3840 * 2176) {             // past 4K: FFmpeg
        hw_drop(d);
        return 0;
    }
    pthread_mutex_lock(&hw.owner_lock);
    int mine = !__atomic_load_n(&hw.away, __ATOMIC_ACQUIRE) && (!hw.owner || hw.owner == d);
    if (mine && !hw_queues_up())
        mine = 0;
    if (mine)
        hw.owner = d;
    pthread_mutex_unlock(&hw.owner_lock);
    if (!mine)
        return 0;  // another video has it (or away, or no queue)
    // 2026-11-09 18:4x: a decoder for each size, as NK had them (1080p in a
    // 1920x1088 one, 4K in a 4K one). A smaller video in a bigger decoder
    // (YouTube down from 2160p when the player shrank) was never tried, and
    // the video was seen bent after a few resizes: the chip may lay the
    // picture out for the decoder's size, not the video's (the colours read
    // at pitch x height would then be wrong). So a new one, smaller too.
    if (hw.dec && (hw.codec != codec || dw != (int)hw.c.max_w || dh != (int)hw.c.max_h))
        hw_close();
    if (hw.dec) {
        hw_reset();
    } else if (!hw_open(codec, dw, dh)) {
        VLOG("slot %d: no hardware decoder for %s %dx%d: FFmpeg", d->index, codec_name(format->codec), w, h);
        hw_drop(d);
        return 0;
    }
    hw.vw = hw.vh = 0;
    hw.tag = 0;
    hw.resyncs = hw.fails = hw.errors = 0;
    if (codec == HW_AVC) {
        hw_avcc(format->config_size ? config : NULL, format->config_size);
    } else {
        hw.head_n = 0;
        hw.nal_len = 0;
    }
    hw.wait_key = codec == HW_VP9;
    VLOG("slot %d: %s %ux%u on the hardware decoder (up to %dx%d, %d picture buffers)", d->index,
         codec_name(format->codec), format->width, format->height, hw.c.max_w, hw.c.max_h, hw.pool->count);
    return 1;
}

// d lets go of the hardware (its video ended, or FFmpeg takes it over).
static void hw_drop(decoder_t *d)
{
    pthread_mutex_lock(&hw.owner_lock);
    if (hw.owner == d) {
        hw.owner = NULL;
        hw_close();  // (another video may want another size: made anew)
        if (__atomic_load_n(&hw.away, __ATOMIC_ACQUIRE))
            hw_queues_down();  // (under the lock: video_away sees no owner and no queue)
    }
    pthread_mutex_unlock(&hw.owner_lock);
}

enum { HW_FED = 0, HW_WAIT = 1, HW_FAIL = -1, HW_REFUSED = -2 };

// One packet of d's video. HW_WAIT: no picture buffer free for it yet (the
// display has them): the packet stays in the ring.
static int hw_decode(decoder_t *d, const uint8_t *p, size_t n, int key, int64_t pts)
{
    size_t off[8] = {0}, len[8] = {n};
    int fshow[8] = {1}, frames = 1;
    if (hw.codec == HW_VP9) {
        int shows = 1, vk = 0, prof = 0, vw = 0, vh = 0;
        frames = hw_vp9_block(p, n, &shows, &vk, &prof, &vw, &vh, off, len, fshow);
        if (!frames) {  // not understood: whole, as it is
            frames = 1;
            off[0] = 0;
            len[0] = n;
            fshow[0] = 1;
        }
        if (vk && prof != 0) {
            VLOG("slot %d: VP9 profile %d (%s), %dx%d", d->index, prof, prof == 2 ? "10-bit" : "4:4:4", vw, vh);
            return HW_REFUSED;  // 10-bit (profile 2) or 4:4:4: FFmpeg
        }
        if (vk && vw > 0 && vh > 0) {
            int dw = (vw + 15) & ~15, dh = (vh + 15) & ~15;
            dw = dw > 1920 ? dw : 1920;
            dh = dh > 1088 ? dh : 1088;
            if (dw != (int)hw.c.max_w || dh != (int)hw.c.max_h) {  // another size in the same stream: its own decoder (18:4x)
                if (dw > 3840 || dh > 3840 || dw * dh > 3840 * 2176)
                    return HW_REFUSED;
                VLOG("slot %d: VP9 %dx%d in the stream: a new hardware decoder", d->index, vw, vh);
                hw_close();
                if (!hw_open(HW_VP9, dw, dh))
                    return HW_REFUSED;
            }
            else if (hw.vw && (vw != hw.vw || vh != hw.vh))  // 19:4x: each change of size in the log
                VLOG("slot %d: VP9 %dx%d in the stream (was %dx%d), the same decoder (up to %ux%u)", d->index, vw, vh,
                     hw.vw, hw.vh, hw.c.max_w, hw.c.max_h);
            hw.vw = vw;
            hw.vh = vh;
        }
        key = vk;
    }
    if (hw.wait_key && !key)
        return HW_FED;  // after a stream error, a new decoder or a seek (VP9): from the next key frame
    if (hw_free_buffers() < frames) {
        hw.waits++;
        return HW_WAIT;
    }
    hw.wait_key = 0;
    for (int i = 0; i < frames; i++) {
        const uint64_t t0 = video_ring_now_ns();
        const int r = hw_one(d, p + off[i], len[i], key && !i, pts, !fshow[i]);
        const double ms = (double)(video_ring_now_ns() - t0) / 1e6;
        hw.busy_ms += ms;
        hw.busy_max = ms > hw.busy_max ? ms : hw.busy_max;
        hw.calls++;
        if (r < 0)
            return HW_FAIL;
        if (hw.wait_key)  // a stream error: the rest of the block is left
            break;
    }
    return HW_FED;
}
