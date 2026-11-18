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

// kbm.c (2026-11-18): a mouse and a keyboard plugged into the console (kbm.h).
//
// libSceMouse and libSceKeyboard are not among the libraries the system loads for every title. On 13.60 both
// load (by path, and by the system's ids), but sceKernelDlsym finds no name in them (0x80020003), as in
// libSceImeDialog (sysime.c). So their functions are imported by name at link time (kbm_link_stub.c), as the
// video decoder's and the microphone's are, and the system is asked to load each library by its id: the loader
// then fills the imports, which are read from the GOT (0 while unresolved) and called only when filled. A
// function that is not there is never called, and the log says which.
//
// The data, as the public PS4/PS5 reimplementations agree on it (shadPS4, GPCS4, fpPS4, OpenOrbis): a mouse
// reading is 40 bytes (the time, connected, the buttons, x and y moved, the wheel, the tilt); the keyboard's
// state is 96 bytes (the time, 8 bytes, connected, how many keys are down, the locks, the modifiers as the USB
// boot report's bits, up to 16 USB HID usages). The first readings go to the log as bytes, so a layout this
// firmware does not share shows at once. The keys go to the browser as their USB usages (INPUT_KEY), which makes
// the characters with the layout of its language (main.cpp, hw_key_input()).

#define _DEFAULT_SOURCE
#include "kbm.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "poc_log.h"

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
const char *sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceGetInitialUser(int *user_id);
int sceSysmoduleLoadModule(uint32_t id);

int32_t sceMouseInit(void);
int32_t sceMouseOpen(int32_t user, int32_t type, int32_t index, const void *param);
int32_t sceMouseRead(int32_t handle, void *data, int32_t count);
int32_t sceKeyboardInit(void);
int32_t sceKeyboardOpen(int32_t user, int32_t type, int32_t index, const void *param);
int32_t sceKeyboardReadState(int32_t handle, void *state);

/* An import's address as the loader left it in the GOT: 0 while unresolved (video_hw.h, HW_GOT). */
#define KBM_GOT(fn)                                                        \
    __extension__({                                                        \
        void *p_;                                                          \
        __asm__ volatile("movq " #fn "@GOTPCREL(%%rip), %0" : "=r"(p_));   \
        p_;                                                                \
    })

/* The system's ids for the two libraries (the PS4's, which the PS5 keeps: OpenOrbis's sysmodule.h). */
#define SYSMODULE_MOUSE    0x00a9
#define SYSMODULE_KEYBOARD 0x0106

#define POLL_US        8000
#define MOUSE_ENTRIES  8
#define READ_BYTES     4096      // far more than MOUSE_ENTRIES of any layout: a bigger one cannot write past it
#define BUTTONS_LOGGED 8         // the mouse's first button changes written to the log (which bits its buttons are)
#define MOUSE_MAX_STEP 400       // counts in one reading; more is a misread layout, not a hand
#define WHEEL_STEP     120.0f    // as the D-pad's (input.c, DPAD_STEP)
#define KEYBOARD_STATE 96

#define MOUSE_PRIMARY   0x01
#define MOUSE_SECONDARY 0x02
#define MOUSE_MIDDLE    0x04
#define MOUSE_BACK      0x08     // (the side buttons: which bits they are is in the log's first readings)
#define MOUSE_FORWARD   0x10

typedef struct {
    uint8_t behavior;            // 1: every mouse as one
    uint8_t reserved[7];
} mouse_open_param_t;

typedef struct {
    uint64_t timestamp;
    uint8_t connected;
    uint8_t pad[3];
    uint32_t buttons;
    int32_t x, y, wheel, tilt;
    uint8_t reserved[8];
} mouse_data_t;
_Static_assert(sizeof(mouse_data_t) == 40, "libSceMouse's data is 40 bytes");

typedef int32_t (*init_t)(void);
typedef int32_t (*open_t)(int32_t user, int32_t type, int32_t index, const void *param);
typedef int32_t (*mouse_read_t)(int32_t handle, void *data, int32_t count);
typedef int32_t (*keyboard_state_t)(int32_t handle, void *state);

static struct {
    input_handler_t handler;
    mouse_read_t mouse_read;
    keyboard_state_t keyboard_state;
    int mouse_handle, keyboard_handle;
    int mouse_connected;
    uint32_t mouse_buttons;
    unsigned buttons_logged;
    int mouse_error, keyboard_error;
} kbm = {.mouse_handle = -1, .keyboard_handle = -1, .mouse_connected = -1};

static _Alignas(16) unsigned char read_buffer[READ_BYTES];


/* The library loaded: by the system, by its id, and if its imports are still empty, by its path too. */
static void load(const char *name, uint32_t id)
{
    POC_LOG("kbm: sceSysmoduleLoadModule(0x%x) %s -> 0x%x", (unsigned)id, name, (unsigned)sceSysmoduleLoadModule(id));
}

static void load_by_path(const char *name)
{
    const char *word = sceKernelGetFsSandboxRandomWord();
    char paths[2][128];
    snprintf(paths[0], sizeof paths[0], "/%s/common/lib/%s.sprx", word ? word : "system", name);
    snprintf(paths[1], sizeof paths[1], "/system/common/lib/%s.sprx", name);
    int h = -1;
    for (int i = 0; i < 2 && h < 0; i++) {
        int res = 0;
        h = sceKernelLoadStartModule(paths[i], 0, NULL, 0, NULL, &res);
        POC_LOG("kbm: load %s -> 0x%x (%d)", paths[i], (unsigned)h, res);
    }
}


static void emit(input_kind_t kind, float dx, float dy, int pressed)
{
    input_event_t e = {kind, 0, 0, dx, dy, pressed};
    input_cursor(&e.x, &e.y);
    kbm.handler(&e);
}


static void mouse_entry(const mouse_data_t *m)
{
    if ((int)m->connected != kbm.mouse_connected) {
        kbm.mouse_connected = m->connected;
        POC_LOG("kbm: mouse %s", m->connected ? "connected" : "not connected");
    }
    if (!m->connected)
        return;
    const int dx = m->x, dy = m->y;
    if (dx < -MOUSE_MAX_STEP || dx > MOUSE_MAX_STEP || dy < -MOUSE_MAX_STEP || dy > MOUSE_MAX_STEP)
        return;
    const uint32_t was = kbm.mouse_buttons, now = m->buttons;
    if (dx || dy) {
        float x, y;
        input_cursor(&x, &y);
        input_set_cursor(x + (float)dx, y + (float)dy);
        emit(INPUT_MOVE, 0, 0, (now & MOUSE_PRIMARY) != 0);
    }
    if ((now ^ was) & MOUSE_PRIMARY)
        emit((now & MOUSE_PRIMARY) ? INPUT_BUTTON_DOWN : INPUT_BUTTON_UP, 0, 0, 0);
    if ((now & ~was) & MOUSE_BACK)
        emit(INPUT_BACK, 0, 0, 0);
    if ((now & ~was) & MOUSE_FORWARD)
        emit(INPUT_FORWARD, 0, 0, 0);
    if (now != was && kbm.buttons_logged < BUTTONS_LOGGED) {
        kbm.buttons_logged++;
        POC_LOG("kbm: mouse buttons 0x%x", now);
    }
    kbm.mouse_buttons = now;
    if (m->wheel || m->tilt)
        emit(INPUT_SCROLL, (float)m->tilt * WHEEL_STEP, -(float)m->wheel * WHEEL_STEP, 1);
}


static void mouse_poll(void)
{
    memset(read_buffer, 0, sizeof read_buffer);
    const int n = kbm.mouse_read(kbm.mouse_handle, read_buffer, MOUSE_ENTRIES);
    if (n < 0) {
        if (!kbm.mouse_error++)
            POC_LOG("kbm: sceMouseRead: 0x%x (written once)", (unsigned)n);
        return;
    }
    for (int i = 0; i < n && i < MOUSE_ENTRIES; i++) {
        mouse_entry((const mouse_data_t *)(read_buffer + i * sizeof(mouse_data_t)));
    }
}


/* The keys as the page gets them (INPUT_KEY, input.h): what went down, what is held (repeated after
 * REPEAT_DELAY_MS, every REPEAT_EVERY_MS: the library gives states, not a typist's repeat), what came up. */
#define KEYS_MAX        16
#define REPEAT_DELAY_MS 500
#define REPEAT_EVERY_MS 33

static struct {
    int connected;                  // -1 before the first reading
    uint32_t modifiers;
    uint16_t down[KEYS_MAX];
    int count;
    uint16_t repeat;                // the last key down, while held
    uint64_t repeat_at;
} keys = {.connected = -1};

static uint64_t kbm_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static void key_event(unsigned usage, uint32_t modifiers, int pressed)
{
    input_event_t e = {INPUT_KEY, 0, 0, (float)usage, (float)modifiers, pressed};
    input_cursor(&e.x, &e.y);
    kbm.handler(&e);
}

static int key_in(const uint16_t *set, int count, uint16_t usage)
{
    for (int i = 0; i < count; i++)
        if (set[i] == usage)
            return 1;
    return 0;
}

/* Everything up (the keyboard went). */
static void keys_release(void)
{
    for (int i = 0; i < keys.count; i++)
        key_event(keys.down[i], 0, 0);
    for (unsigned bit = 0; bit < 8; bit++)
        if (keys.modifiers >> bit & 1)
            key_event(0xe0 + bit, 0, 0);
    keys.count = 0, keys.modifiers = 0, keys.repeat = 0;
}

static void keyboard_poll(void)
{
    memset(read_buffer, 0, sizeof read_buffer);
    const int rc = kbm.keyboard_state(kbm.keyboard_handle, read_buffer);
    if (rc < 0) {
        if (!kbm.keyboard_error++)
            POC_LOG("kbm: sceKeyboardReadState: 0x%x (written once)", (unsigned)rc);
        return;
    }
    int32_t count;
    uint32_t modifiers;
    memcpy(&count, read_buffer + 20, 4);
    memcpy(&modifiers, read_buffer + 28, 4);
    count = count < 0 ? 0 : count > KEYS_MAX ? KEYS_MAX : count;
    modifiers &= 0xff;
    /* The log has whether it is there, never the keys (they may be a password). */
    const int connected = read_buffer[16] != 0;
    if (connected != keys.connected) {
        POC_LOG("kbm: keyboard %s", connected ? "connected" : "not connected");
        if (keys.connected == 1)
            keys_release();
        keys.connected = connected;
        key_event(0, 0, connected);  // the browser types with it, and shows no keyboard for fields
    }
    if (!connected)
        return;
    uint16_t now[KEYS_MAX];
    int n = 0;
    for (int i = 0; i < count; i++) {
        uint16_t usage;
        memcpy(&usage, read_buffer + 32 + 2 * i, 2);
        if (usage == 0x01)  // too many keys at once (ErrorRollOver): the last state stays
            return;
        if (usage >= 0x04 && usage < 0xe0 && !key_in(now, n, usage))
            now[n++] = usage;
    }
    for (unsigned bit = 0; bit < 8; bit++)
        if ((modifiers ^ keys.modifiers) >> bit & 1)
            key_event(0xe0 + bit, modifiers, (int)(modifiers >> bit & 1));
    keys.modifiers = modifiers;
    for (int i = 0; i < keys.count; i++)
        if (!key_in(now, n, keys.down[i])) {
            key_event(keys.down[i], modifiers, 0);
            if (keys.down[i] == keys.repeat)
                keys.repeat = 0;
        }
    const uint64_t t = kbm_now_ms();
    for (int i = 0; i < n; i++)
        if (!key_in(keys.down, keys.count, now[i])) {
            key_event(now[i], modifiers, 1);
            keys.repeat = now[i];
            keys.repeat_at = t + REPEAT_DELAY_MS;
        }
    memcpy(keys.down, now, sizeof now);
    keys.count = n;
    if (keys.repeat && t >= keys.repeat_at) {
        key_event(keys.repeat, modifiers, 2);
        keys.repeat_at = t + REPEAT_EVERY_MS;
    }
}


static void *kbm_thread(void *unused)
{
    (void)unused;
    for (;;) {
        usleep(POLL_US);
        if (kbm.mouse_handle >= 0 && kbm.mouse_error < 1000)
            mouse_poll();
        if (kbm.keyboard_handle >= 0 && kbm.keyboard_error < 1000)
            keyboard_poll();
        if ((kbm.mouse_handle < 0 || kbm.mouse_error >= 1000) && (kbm.keyboard_handle < 0 || kbm.keyboard_error >= 1000)) {
            POC_LOG("kbm: nothing left to read: the thread ends");
            return NULL;
        }
    }
}


/* The three imports of a library, filled by the loader (load() first if not yet); 0 when all three are. */
#define KBM_BIND(library, id, f0, f1, f2, out)                                                                  \
    __extension__({                                                                                            \
        void *f_[3] = {KBM_GOT(f0), KBM_GOT(f1), KBM_GOT(f2)};                                                 \
        POC_LOG("kbm: %s imports at the start: %p %p %p", library, f_[0], f_[1], f_[2]);                      \
        if (!f_[0] || !f_[1] || !f_[2]) {                                                                      \
            load(library, id);                                                                                 \
            f_[0] = KBM_GOT(f0), f_[1] = KBM_GOT(f1), f_[2] = KBM_GOT(f2);                                     \
            POC_LOG("kbm: %s imports after the system loaded it: %p %p %p", library, f_[0], f_[1], f_[2]);    \
        }                                                                                                      \
        if (!f_[0] || !f_[1] || !f_[2]) {                                                                      \
            load_by_path(library);                                                                             \
            f_[0] = KBM_GOT(f0), f_[1] = KBM_GOT(f1), f_[2] = KBM_GOT(f2);                                     \
            POC_LOG("kbm: %s imports after the load by path: %p %p %p", library, f_[0], f_[1], f_[2]);        \
        }                                                                                                      \
        const int ok_ = f_[0] && f_[1] && f_[2];                                                               \
        for (int i_ = 0; i_ < 3; i_++)                                                                         \
            (out)[i_] = ok_ ? f_[i_] : NULL;                                                                   \
        ok_ ? 0 : -1;                                                                                          \
    })


int kbm_start(input_handler_t handler, int width, int height)
{
    (void)width;
    (void)height;
    kbm.handler = handler;
    if (access("/app0/no-kbm", F_OK) == 0) {
        POC_LOG("kbm: off (/app0/no-kbm)");
        return -1;
    }
    int user = -1;
    sceUserServiceGetInitialUser(&user);
    void *f[3];
    if (KBM_BIND("libSceMouse", SYSMODULE_MOUSE, sceMouseInit, sceMouseOpen, sceMouseRead, f) == 0) {
        const int32_t init = ((init_t)f[0])();
        mouse_open_param_t param = {1, {0}};
        kbm.mouse_handle = ((open_t)f[1])(user, 0, 0, &param);
        if (kbm.mouse_handle < 0) {  // one handle a mouse, where the system refuses them merged
            param.behavior = 0;
            kbm.mouse_handle = ((open_t)f[1])(user, 0, 0, &param);
        }
        kbm.mouse_read = (mouse_read_t)f[2];
        POC_LOG("kbm: sceMouseInit 0x%x, sceMouseOpen(user 0x%x, merged %u) -> 0x%x", (unsigned)init, (unsigned)user,
                (unsigned)param.behavior, (unsigned)kbm.mouse_handle);
    }
    if (KBM_BIND("libSceKeyboard", SYSMODULE_KEYBOARD, sceKeyboardInit, sceKeyboardOpen, sceKeyboardReadState, f) == 0) {
        const int32_t init = ((init_t)f[0])();
        kbm.keyboard_handle = ((open_t)f[1])(user, 0, 0, NULL);
        kbm.keyboard_state = (keyboard_state_t)f[2];
        POC_LOG("kbm: sceKeyboardInit 0x%x, sceKeyboardOpen(user 0x%x) -> 0x%x", (unsigned)init, (unsigned)user,
                (unsigned)kbm.keyboard_handle);
    }
    if (kbm.mouse_handle < 0 && kbm.keyboard_handle < 0) {
        POC_LOG("kbm: neither the mouse nor the keyboard could be opened");
        return -1;
    }
    pthread_t thread;
    if (pthread_create(&thread, NULL, kbm_thread, NULL)) {
        POC_LOG("kbm: no thread");
        return -1;
    }
    pthread_detach(thread);
    return 0;
}
