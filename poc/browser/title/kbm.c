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
// libSceMouse and libSceKeyboard are not among the libraries the system loads for every title, and an import of
// a library it did not load stays NULL (sysime.c: a call through one ended the title, and with the GPU open the
// console). So both are loaded here with sceKernelLoadStartModule and bound by name with sceKernelDlsym; a
// function that is not bound is never called, and the log says which.
//
// The mouse's data is read as the PS4's libSceMouse gives it (40 bytes: the time, connected, the buttons, x and y
// moved, the wheel, the tilt), the layout the public PS4 emulators use. That it is the same here is checked by
// the log: the first readings are written there as bytes. The keyboard's layout is not public: its readings go
// to the log only, for now.

#define _DEFAULT_SOURCE
#include "kbm.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "poc_log.h"

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
int sceKernelDlsym(int handle, const char *name, void **addr);
const char *sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceGetInitialUser(int *user_id);

#define POLL_US        8000
#define MOUSE_ENTRIES  8
#define READ_BYTES     4096      // far more than MOUSE_ENTRIES of any layout: a bigger one cannot write past it
#define RAW_LOGGED     24        // the first readings written to the log as bytes, for each device
#define MOUSE_MAX_STEP 400       // counts in one reading; more is a misread layout, not a hand
#define WHEEL_STEP     120.0f    // as the D-pad's (input.c, DPAD_STEP)

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

typedef struct {
    int (*init)(void);
    int (*open)(int user, int type, int index, const void *param);
    int (*read)(int handle, void *data, int num);
} device_t;

static struct {
    input_handler_t handler;
    device_t mouse, keyboard;
    int mouse_handle, keyboard_handle;
    int mouse_connected;
    uint32_t mouse_buttons;
    unsigned mouse_logged, keyboard_logged;
    unsigned char keyboard_last[128];
    int mouse_error, keyboard_error;
} kbm = {.mouse_handle = -1, .keyboard_handle = -1, .mouse_connected = -1};

static _Alignas(16) unsigned char read_buffer[READ_BYTES];


static int load(const char *name)
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
    return h;
}


/* The three functions by name; 0 when all three are bound. */
static int bind(const char *library, const char *prefix, device_t *d)
{
    const int h = load(library);
    if (h < 0)
        return -1;
    const char *what[3] = {"Init", "Open", "Read"};
    void **slots[3] = {(void **)&d->init, (void **)&d->open, (void **)&d->read};
    int bound = 0;
    for (int i = 0; i < 3; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s%s", prefix, what[i]);
        *slots[i] = NULL;
        const int rc = sceKernelDlsym(h, name, slots[i]);
        if (rc == 0 && *slots[i])
            bound++;
        else
            *slots[i] = NULL;
        POC_LOG("kbm: %s: %s -> 0x%x, %p", library, name, (unsigned)rc, *slots[i]);
    }
    return bound == 3 ? 0 : -1;
}


static void raw_log(const char *what, int n, const unsigned char *b, size_t bytes)
{
    char hex[3 * 64 + 1];
    size_t at = 0;
    for (size_t i = 0; i < bytes && i < 64; i++)
        at += (size_t)snprintf(hex + at, sizeof hex - at, "%02x%s", b[i], i % 4 == 3 ? " " : "");
    POC_LOG("kbm: %s read %d: %s", what, n, hex);
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
    if (now != was)
        POC_LOG("kbm: mouse buttons 0x%x", now);
    kbm.mouse_buttons = now;
    if (m->wheel || m->tilt)
        emit(INPUT_SCROLL, (float)m->tilt * WHEEL_STEP, -(float)m->wheel * WHEEL_STEP, 1);
}


static void mouse_poll(void)
{
    memset(read_buffer, 0, sizeof read_buffer);
    const int n = kbm.mouse.read(kbm.mouse_handle, read_buffer, MOUSE_ENTRIES);
    if (n < 0) {
        if (!kbm.mouse_error++)
            POC_LOG("kbm: sceMouseRead: 0x%x (written once)", (unsigned)n);
        return;
    }
    for (int i = 0; i < n && i < MOUSE_ENTRIES; i++) {
        const mouse_data_t *m = (const mouse_data_t *)(read_buffer + i * sizeof(mouse_data_t));
        if (kbm.mouse_logged < RAW_LOGGED && (m->buttons || m->x || m->y || m->wheel || m->tilt)) {
            kbm.mouse_logged++;
            raw_log("mouse", n, (const unsigned char *)m, sizeof(*m));
        }
        mouse_entry(m);
    }
}


static void keyboard_poll(void)
{
    memset(read_buffer, 0, sizeof read_buffer);
    const int n = kbm.keyboard.read(kbm.keyboard_handle, read_buffer, 1);
    if (n < 0) {
        if (!kbm.keyboard_error++)
            POC_LOG("kbm: sceKeyboardRead: 0x%x (written once)", (unsigned)n);
        return;
    }
    if (n > 0 && memcmp(read_buffer + 8, kbm.keyboard_last + 8, sizeof kbm.keyboard_last - 8)) {
        memcpy(kbm.keyboard_last, read_buffer, sizeof kbm.keyboard_last);
        if (kbm.keyboard_logged < RAW_LOGGED * 2) {
            kbm.keyboard_logged++;
            raw_log("keyboard", n, read_buffer, 64);
        }
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
    if (bind("libSceMouse", "sceMouse", &kbm.mouse) == 0) {
        const int init = kbm.mouse.init();
        mouse_open_param_t param = {1, {0}};
        kbm.mouse_handle = kbm.mouse.open(user, 0, 0, &param);
        POC_LOG("kbm: sceMouseInit 0x%x, sceMouseOpen(user 0x%x) -> 0x%x", (unsigned)init, (unsigned)user,
                (unsigned)kbm.mouse_handle);
    }
    if (bind("libSceKeyboard", "sceKeyboard", &kbm.keyboard) == 0) {
        const int init = kbm.keyboard.init();
        kbm.keyboard_handle = kbm.keyboard.open(user, 0, 0, NULL);
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
