/*
 * 2026-11-15: host-link stub for the PS5's audio input library (libSceAudioIn), as videodec2_link_stub.c is
 * for the video decoder: it only names the imports of mic.c (BrowserPS5 RN's voice search). It is never
 * packaged or executed: the console's own module answers them.
 */

#include <stdint.h>

int32_t sceAudioInOpen(int32_t user, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param) {
  (void)user;
  (void)type;
  (void)index;
  (void)len;
  (void)freq;
  (void)param;
  return -1;
}

int32_t sceAudioInInput(int32_t handle, void* dest) {
  (void)handle;
  (void)dest;
  return -1;
}

int32_t sceAudioInClose(int32_t handle) {
  (void)handle;
  return -1;
}
