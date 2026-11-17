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

// hello_page's display, input and ime modules for a UIProcess that runs in
// a payload (docs/03-video-e-gpu.md, G1): the same functions, done by the
// browser's title over link.h. display_open() starts the title and waits
// for it. remote.h adds the pause: the title says when the system is over
// the app, and hears which WebProcess ended.

#ifndef POC_NAME
#define POC_NAME "browser/remote"
#endif

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "display.h"
#include "ime.h"
#include "input.h"
#include "link.h"
#include "poc_log.h"
#include "remote.h"

#ifndef TITLE_ID
#define TITLE_ID "PPSA97020"
#endif
#define CONNECT_SECONDS 60
#define VSYNC_WAIT_MS   100

_Static_assert(DISPLAY_WIDTH == LINK_WIDTH && DISPLAY_HEIGHT == LINK_HEIGHT,
               "the link carries the display's size");

typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;

int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceSystemServiceLaunchApp(const char* title_id, char** argv,
                              app_launch_ctx_t* ctx);
int sceLncUtilSuspendApp(int app_id, int cause);

static struct {
  int sock;
  pthread_mutex_t send_lock;

  link_view_t* view;
  unsigned back;  // the buffer this side writes

  pthread_mutex_t lock;
  pthread_cond_t vsync;
  uint64_t vsyncs;
  int closed;

  int user_id;
  input_handler_t handler;
  float x, y;

  int ime_open;
  ime_done_t ime_done;
  void* ime_ctx;

  remote_away_t away;
  remote_closed_t on_closed;
} rm = {
  .sock = -1,
  .send_lock = PTHREAD_MUTEX_INITIALIZER,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .vsync = PTHREAD_COND_INITIALIZER,
  .user_id = -1,
  .x = DISPLAY_WIDTH / 2.0f,
  .y = DISPLAY_HEIGHT / 2.0f,
};


static double
now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}


static int
send_all(const void* data, size_t size) {
  const char* p = data;

  while(size) {
    ssize_t n = send(rm.sock, p, size, MSG_NOSIGNAL);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return -1;
    }
    p += n;
    size -= n;
  }
  return 0;
}


static int
recv_all(void* data, size_t size) {
  char* p = data;

  while(size) {
    ssize_t n = recv(rm.sock, p, size, 0);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return -1;
    }
    p += n;
    size -= n;
  }
  return 0;
}


static int
send_msg(const link_msg_t* msg, const char* text) {
  int err;

  pthread_mutex_lock(&rm.send_lock);
  err = send_all(msg, sizeof(*msg));
  if(!err && msg->text_bytes) {
    err = send_all(text, msg->text_bytes);
  }
  pthread_mutex_unlock(&rm.send_lock);
  return err;
}


static void*
reader_thread(void* arg) {
  static char text[LINK_TEXT_MAX + 1];
  link_msg_t msg;
  (void)arg;

  while(!recv_all(&msg, sizeof(msg))) {
    if(msg.text_bytes > LINK_TEXT_MAX
       || (msg.text_bytes && recv_all(text, msg.text_bytes))) {
      break;
    }
    text[msg.text_bytes] = 0;

    if(msg.type == LINK_HELLO) {
      pthread_mutex_lock(&rm.lock);
      rm.user_id = msg.u.hello.user_id;
      pthread_mutex_unlock(&rm.lock);
      POC_LOG("title says hello, user 0x%x", msg.u.hello.user_id);

    } else if(msg.type == LINK_VSYNC) {
      pthread_mutex_lock(&rm.lock);
      rm.vsyncs++;
      pthread_cond_broadcast(&rm.vsync);
      pthread_mutex_unlock(&rm.lock);

    } else if(msg.type == LINK_INPUT) {
      input_event_t e = {
        (input_kind_t)msg.u.input.kind, msg.u.input.x, msg.u.input.y,
        msg.u.input.dx, msg.u.input.dy, msg.u.input.pressed,
      };
      input_handler_t handler;

      pthread_mutex_lock(&rm.lock);
      if(e.kind != INPUT_PAD) {  // its x, y are a stick, not the cursor
        rm.x = e.x;
        rm.y = e.y;
      }
      handler = rm.handler;
      pthread_mutex_unlock(&rm.lock);
      if(handler) {
        handler(&e);
      }

    } else if(msg.type == LINK_IME_DONE) {
      ime_done_t done;
      void* ctx;

      pthread_mutex_lock(&rm.lock);
      done = rm.ime_done;
      ctx = rm.ime_ctx;
      rm.ime_open = 0;
      pthread_mutex_unlock(&rm.lock);
      if(done) {
        done(msg.u.ime_done.ok ? text : 0, ctx);
      }

    } else if(msg.type == LINK_AWAY) {
      remote_away_t away;

      pthread_mutex_lock(&rm.lock);
      away = rm.away;
      pthread_mutex_unlock(&rm.lock);
      POC_LOG("the title says %s%s", msg.u.away.away ? "away" : "back",
              msg.u.away.test ? " (test)" : "");
      if(away) {  // (2026-11-14: "leave" rides in the second number's bit 1)
        away(msg.u.away.away, (msg.u.away.test ? 1 : 0) | (msg.u.away.leave == 1 ? 2 : 0));
      }
    }
  }

#ifndef APP_RN  // (RN, 2026-11-14: said after the handler, which first sends this process's output to a file)
  POC_LOG("the title closed the link");
#endif
  remote_closed_t on_closed;
  pthread_mutex_lock(&rm.lock);
  rm.closed = 1;
  on_closed = rm.on_closed;
  pthread_cond_broadcast(&rm.vsync);
  pthread_mutex_unlock(&rm.lock);
  if(on_closed) {
    on_closed();
  }
#ifdef APP_RN
  POC_LOG("the title closed the link");
#endif
  return 0;
}


/**
 * The title's state (link.h, LINK_TITLE_SHM): LINK_TITLE_WAITING or
 * LINK_TITLE_CONNECTED while it runs, 0 when it does not.
 **/
static uint32_t
title_state(void) {
  uint32_t state = 0;
  int fd;

  if((fd=shm_open(LINK_TITLE_SHM, O_RDONLY, 0)) < 0) {
    return 0;
  }
  void* memory = mmap(0, LINK_TITLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return 0;
  }
  const link_title_t* title = (const link_title_t*)memory;
  double age = now_ms() - (double)__atomic_load_n(&title->heartbeat_ms, __ATOMIC_ACQUIRE);
  if(__atomic_load_n(&title->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC) {
    if(age < LINK_TITLE_FRESH_MS) {
      state = __atomic_load_n(&title->state, __ATOMIC_ACQUIRE);
    } else {
      POC_LOG("the title's state is %.0f ms old: it is gone", age);
    }
  }
  munmap(memory, LINK_TITLE_BYTES);
  return state;
}


/**
 * Open the browser's title, as the home screen would. An app in front (a
 * game) is suspended, not closed: the system keeps it, and it comes back
 * where it was when it is picked again on the home screen (the user's
 * TwitterPS5, src/open.c; docs/10-achados-do-desktop.md, 2). It used to be
 * closed, a game we did not open; the suspend is not tried on the console
 * yet (the tests run with nothing open). Not when the title is open
 * already: opened from the home screen, it waits for this UIProcess (it
 * sent the browser itself), and showing another browser it must not be
 * closed (link.h).
 **/
static int
launch_title(void) {
  app_launch_ctx_t ctx = {0};
  int app_id;
  int err;

  const uint32_t state = title_state();
  switch(state) {
  case 0: {
    // (2026-11-14) The title that sent this browser left before it came (the PS5's home screen came over
    // it at once: title.cpp, leave_startup): it is not opened again, and this browser quits.
    static const char left[] = "/data/homebrew/" TITLE_ID "/left-at-start";
    struct stat st;
    if(!stat(left, &st)) {
      const double age = difftime(time(0), st.st_mtime);
      unlink(left);
      if(age >= 0 && age < 60) {
        POC_LOG("the title left at its start %.0f s ago (the home screen came over it): not opening it again", age);
        return -2;
      }
    }
    break;
  }
  case LINK_TITLE_WAITING:
    POC_LOG("the title is open and waiting for a browser: not opening it again");
    return 0;
  case LINK_TITLE_CONNECTED:
    POC_LOG("FAIL the browser is open on the TV already: close it there first "
            "(closing the title while its pages draw took the console down)");
    return -1;
  default:  // another browser's quit (LINK_TITLE_READY), or 16:45's state 3
    POC_LOG("FAIL the title is closing (state %u): not touching it "
            "(closing a closing title took the console down)", state);
    return -1;
  }

  sceUserServiceInitialize(0);
  if((err=sceUserServiceGetForegroundUser(&ctx.user_id))) {
    POC_LOG("sceUserServiceGetForegroundUser = 0x%x", err);
    return err;
  }
  if((app_id=sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    err = sceLncUtilSuspendApp(app_id, 0);
    POC_LOG("suspended the app in front 0x%x: 0x%x", app_id, err);
    if(err < 0) {
      POC_LOG("FAIL the app in front was not suspended: the browser does not open over it");
      return err;
    }
    usleep(500 * 1000);
  }
  ctx.structsize = sizeof(ctx);
  err = sceSystemServiceLaunchApp(TITLE_ID, 0, &ctx);
  POC_LOG("sceSystemServiceLaunchApp(%s) = 0x%x", TITLE_ID, err);
  return err > 0 ? 0 : err;
}


static int
open_view(void) {
  int fd;

  shm_unlink(LINK_VIEW_SHM);  // a view left by a browser that crashed
  if((fd=shm_open(LINK_VIEW_SHM, O_RDWR | O_CREAT | O_EXCL, 0666)) < 0) {
    POC_PERROR("shm_open(" LINK_VIEW_SHM ")");
    return -1;
  }
  if(ftruncate(fd, LINK_VIEW_BYTES)) {
    POC_PERROR("ftruncate");
    close(fd);
    return -1;
  }
  rm.view = mmap(0, LINK_VIEW_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if(rm.view == MAP_FAILED) {
    rm.view = 0;
    POC_PERROR("mmap(view)");
    return -1;
  }

  // This side writes into 0, the title reads 2, 1 is in the middle.
  memset(rm.view, 0, LINK_HEADER_BYTES);
  link_view_init(rm.view);
  rm.back = 0;
  return 0;
}


int
display_open(void) {
  struct sockaddr_in address = {0};
  struct timeval timeout = {CONNECT_SECONDS, 0};
  pthread_t thread;
  int one = 1;
  int listener;
  int err;

  if(open_view()) {
    return -1;
  }

  listener = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  address.sin_family = AF_INET;
  address.sin_port = htons(LINK_PORT);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(listener < 0 || bind(listener, (struct sockaddr*)&address, sizeof(address))
     || listen(listener, 1)) {
    POC_PERROR("listen on 127.0.0.1:9050");
    if(listener >= 0) {
      close(listener);
    }
    return -1;
  }

  if((err=launch_title())) {
    close(listener);
    return err;
  }

  POC_LOG("waiting up to %d s for the title on 127.0.0.1:%d", CONNECT_SECONDS,
          LINK_PORT);
  rm.sock = accept(listener, 0, 0);
  close(listener);
  if(rm.sock < 0) {
    POC_PERROR("accept (the title did not connect)");
    return -1;
  }
  setsockopt(rm.sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  POC_LOG("the title is connected");

  if((err=pthread_create(&thread, 0, reader_thread, 0))) {
    return err;
  }
  pthread_detach(thread);
  return 0;
}


int
display_show(const uint32_t* frame, double* tile_ms, double* wait_ms) {
  double start = now_ms();
  struct timespec deadline;
  uint64_t seen;
  int closed;

  memcpy(link_buffer(rm.view, rm.back), frame, LINK_FRAME_BYTES);
  rm.back = link_publish(rm.view, rm.back);
  double copied = now_ms();
  *tile_ms += copied - start;

  // Wait for the title's next flip, so this side runs at the screen's pace.
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_nsec += VSYNC_WAIT_MS * 1000000L;
  deadline.tv_sec += deadline.tv_nsec / 1000000000L;
  deadline.tv_nsec %= 1000000000L;
  pthread_mutex_lock(&rm.lock);
  seen = rm.vsyncs;
  while(rm.vsyncs == seen && !rm.closed
        && pthread_cond_timedwait(&rm.vsync, &rm.lock, &deadline) != ETIMEDOUT) {
  }
  closed = rm.closed;
  pthread_mutex_unlock(&rm.lock);
  *wait_ms += now_ms() - copied;
  return closed ? -EPIPE : 0;
}


/**
 * The quit's end, before LINK_QUIT: have the system close the title, as the
 * user's close from the system does, so that it ends without the system's
 * error dialog (a title ending its own process gets SIGSYS, "Erro no
 * jogo", and the system service gives a title no app id; 2026-11-05 16:39).
 * Only when main.cpp's quit saw no WebProcess left (remote_web_gone()) and
 * the title says LINK_TITLE_READY (link.h): it let go of the GPU client and
 * still shows the screen, as in the pause before the user's safe close
 * (2026-11-04 16:22). Never after LINK_QUIT: closing the title once it had
 * closed its GPU, video and sound took the console down twice (2026-11-05
 * 16:51 and 17:14), and so did closing it with a client drawing (2026-11-04
 * 15:53). Off unless built with CLOSE_TITLE_BY_SYSTEM (a test the user
 * agrees to). Returns 1 when the system closed the title.
 **/
static int web_gone;  // remote_web_gone(), main thread as display_close()


void
remote_web_gone(int gone) {
  web_gone = gone;
}


int
remote_title_running(void) {
  return title_state() != 0;
}


static int
close_title(void) {
#ifndef CLOSE_TITLE_BY_SYSTEM
  POC_LOG("the title ends by itself (the system's close is off: CLOSE_TITLE_BY_SYSTEM)");
  return 0;
#else
  const double start = now_ms();
  uint32_t state;
  int closed = 0;

  if(!web_gone) {
    POC_LOG("a WebProcess may still be there: the title ends by itself");
    return 0;
  }
  while((state=title_state()) == LINK_TITLE_CONNECTED && now_ms() - start < 3000) {
    usleep(50 * 1000);
  }
  if(state != LINK_TITLE_READY) {
    POC_LOG("the title did not say it let go of the GPU (state %u after %.0f ms): it ends by itself",
            state, now_ms() - start);
    return 0;
  }
  const int app_id = sceSystemServiceGetAppIdOfRunningBigApp();
  if(app_id <= 0) {
    POC_LOG("no app id for the title: 0x%x; it ends by itself", (unsigned)app_id);
    return 0;
  }
  // Said before the call: if the console goes down, the log shows where.
  POC_LOG("the title let go of the GPU and shows the screen (%.0f ms); the system closes it (app 0x%x)",
          now_ms() - start, (unsigned)app_id);
  const double asked = now_ms();
  const int err = sceSystemServiceKillApp(app_id, -1, 0, 0);
  POC_LOG("sceSystemServiceKillApp: 0x%x in %.0f ms", (unsigned)err, now_ms() - asked);
  if(err) {
    return 0;
  }
  while(!closed && now_ms() - asked < 5000) {
    pthread_mutex_lock(&rm.lock);
    closed = rm.closed;
    pthread_mutex_unlock(&rm.lock);
    if(!closed) {
      usleep(50 * 1000);
    }
  }
  POC_LOG("the title %s %.0f ms after the system's close", closed ? "was gone" : "was still linked",
          now_ms() - asked);
  return 1;
#endif
}


void
display_close(void) {
  link_msg_t msg = {LINK_QUIT, 0};
  int linked;
  int killed = 0;

  pthread_mutex_lock(&rm.lock);
  linked = rm.sock >= 0 && !rm.closed;  // not when the title went first (the system's close)
  pthread_mutex_unlock(&rm.lock);
  if(linked) {
    killed = close_title();
  }
  if(rm.sock >= 0) {
    if(!killed) {
      send_msg(&msg, 0);
    }
    shutdown(rm.sock, SHUT_RDWR);
  }
  if(rm.view) {
    munmap(rm.view, LINK_VIEW_BYTES);
    rm.view = 0;
  }
  shm_unlink(LINK_VIEW_SHM);
}


int
input_start(input_handler_t handler, int width, int height) {
  (void)width;
  (void)height;
  pthread_mutex_lock(&rm.lock);
  rm.handler = handler;
  pthread_mutex_unlock(&rm.lock);
  return 0;
}


void
input_cursor(float* x, float* y) {
  pthread_mutex_lock(&rm.lock);
  *x = rm.x;
  *y = rm.y;
  pthread_mutex_unlock(&rm.lock);
}


// The title has the controller, so the cursor is its: it is told, and this
// side moves at once (the next event from the title agrees).
void
input_set_cursor(float x, float y) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_CURSOR;
  msg.u.cursor.x = x < 0 ? 0 : x > LINK_WIDTH - 1 ? LINK_WIDTH - 1 : x;
  msg.u.cursor.y = y < 0 ? 0 : y > LINK_HEIGHT - 1 ? LINK_HEIGHT - 1 : y;
  pthread_mutex_lock(&rm.lock);
  rm.x = msg.u.cursor.x;
  rm.y = msg.u.cursor.y;
  pthread_mutex_unlock(&rm.lock);
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
input_set_speed(int cursor_percent, int scroll_percent) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_CONFIG;
  msg.u.config.cursor_percent = cursor_percent;
  msg.u.config.scroll_percent = scroll_percent;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
remote_sound(int which, int percent) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_SOUND;
  msg.u.sound.which = which;
  msg.u.sound.percent = percent;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
remote_capture(int on, const char* name, int seconds) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_CAPTURE;
  msg.u.capture.on = on;
  msg.u.capture.seconds = seconds;
  msg.text_bytes = name ? (uint32_t)strnlen(name, 64) : 0;
  if(rm.sock >= 0) {
    send_msg(&msg, name);
  }
}


int
remote_capture_status(unsigned* done) {
  int busy = -1;
  int fd;

  *done = 0;
  if((fd=shm_open(LINK_TITLE_SHM, O_RDONLY, 0)) < 0) {
    return -1;
  }
  void* memory = mmap(0, LINK_TITLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return -1;
  }
  const link_title_t* title = (const link_title_t*)memory;
  double age = now_ms() - (double)__atomic_load_n(&title->heartbeat_ms, __ATOMIC_ACQUIRE);
  if(__atomic_load_n(&title->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC
     && age < LINK_TITLE_FRESH_MS) {
    *done = __atomic_load_n(&title->capture_done, __ATOMIC_ACQUIRE);
    busy = __atomic_load_n(&title->capture_busy, __ATOMIC_ACQUIRE) ? 1 : 0;
  }
  munmap(memory, LINK_TITLE_BYTES);
  return busy;
}


int
remote_title_ime_ready(void) {
  int ready = -1;
  int fd;

  if((fd=shm_open(LINK_TITLE_SHM, O_RDONLY, 0)) < 0) {
    return -1;
  }
  void* memory = mmap(0, LINK_TITLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return -1;
  }
  const link_title_t* title = (const link_title_t*)memory;
  double age = now_ms() - (double)__atomic_load_n(&title->heartbeat_ms, __ATOMIC_ACQUIRE);
  if(__atomic_load_n(&title->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC
     && age < LINK_TITLE_FRESH_MS) {
    ready = __atomic_load_n(&title->ime_ready, __ATOMIC_ACQUIRE) == 1;
  }
  munmap(memory, LINK_TITLE_BYTES);
  return ready;
}


int
remote_title_direct_free(void) {
  int mib = -1;
  int fd;

  if((fd=shm_open(LINK_TITLE_SHM, O_RDONLY, 0)) < 0) {
    return -1;
  }
  void* memory = mmap(0, LINK_TITLE_BYTES, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if(memory == MAP_FAILED) {
    return -1;
  }
  const link_title_t* title = (const link_title_t*)memory;
  double age = now_ms() - (double)__atomic_load_n(&title->heartbeat_ms, __ATOMIC_ACQUIRE);
  if(__atomic_load_n(&title->magic, __ATOMIC_ACQUIRE) == LINK_MAGIC
     && age < LINK_TITLE_FRESH_MS) {
    const uint32_t said = __atomic_load_n(&title->direct_free_mib, __ATOMIC_ACQUIRE);
    mib = said ? (int)said : -1;
  }
  munmap(memory, LINK_TITLE_BYTES);
  return mib;
}


void
input_set_raw(int on) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_GAMEPAD;
  msg.u.gamepad.on = on;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
remote_feel(int large, int small, int ms) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_FEEL;
  msg.u.feel.large = large;
  msg.u.feel.small = small;
  msg.u.feel.ms = ms;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
remote_triggers(int mode, int strength) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_TRIGGERS;
  msg.u.triggers.mode = mode;
  msg.u.triggers.strength = strength;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


void
remote_light(int on, int r, int g, int b) {
  link_msg_t msg;

  memset(&msg, 0, sizeof(msg));
  msg.type = LINK_LIGHT;
  msg.u.light.on = on;
  msg.u.light.r = r;
  msg.u.light.g = g;
  msg.u.light.b = b;
  if(rm.sock >= 0) {
    send_msg(&msg, 0);
  }
}


int
input_user(void) {
  int user_id;

  pthread_mutex_lock(&rm.lock);
  user_id = rm.user_id;
  pthread_mutex_unlock(&rm.lock);
  return user_id;
}


int
ime_open(int user_id, ime_kind_t kind, ime_enter_t enter, const char* text,
         ime_done_t done, void* ctx) {
  link_msg_t msg = {LINK_IME_OPEN, 0};
  size_t bytes = text ? strlen(text) : 0;
  (void)user_id;  // the title opens it for the user in front

  if(bytes > LINK_TEXT_MAX) {
    bytes = LINK_TEXT_MAX;
  }
  pthread_mutex_lock(&rm.lock);
  if(rm.ime_open || rm.sock < 0) {
    pthread_mutex_unlock(&rm.lock);
    return -EBUSY;
  }
  rm.ime_open = 1;
  rm.ime_done = done;
  rm.ime_ctx = ctx;
  pthread_mutex_unlock(&rm.lock);

  msg.text_bytes = (uint32_t)bytes;
  msg.u.ime_open.kind = kind;
  msg.u.ime_open.enter = enter;
  if(send_msg(&msg, text)) {
    pthread_mutex_lock(&rm.lock);
    rm.ime_open = 0;
    pthread_mutex_unlock(&rm.lock);
    return -EPIPE;
  }
  return 0;
}


void
remote_on_away(remote_away_t handler) {
  pthread_mutex_lock(&rm.lock);
  rm.away = handler;
  pthread_mutex_unlock(&rm.lock);
}


void
remote_on_closed(remote_closed_t handler) {
  int closed;

  pthread_mutex_lock(&rm.lock);
  rm.on_closed = handler;
  closed = rm.closed;
  pthread_mutex_unlock(&rm.lock);
  if(closed && handler) {  // the title went before this was set
    handler();
  }
}


int
remote_ended(int32_t pid, int for_away) {
  link_msg_t msg = {LINK_ENDED, 0};

  if(rm.sock < 0) {
    return -EPIPE;
  }
  msg.u.ended.pid = pid;
  msg.u.ended.for_away = for_away;
  return send_msg(&msg, 0) ? -EPIPE : 0;
}


int
remote_ended_for_quit(int32_t pid) {
  return remote_ended(pid, LINK_ENDED_QUIT);
}


int
ime_is_open(void) {
  int open;

  pthread_mutex_lock(&rm.lock);
  open = rm.ime_open;
  pthread_mutex_unlock(&rm.lock);
  return open;
}
