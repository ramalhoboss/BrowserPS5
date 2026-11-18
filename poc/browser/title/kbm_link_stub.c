/*
 * 2026-11-18: host-link stubs for the PS5's mouse and keyboard libraries (libSceMouse, libSceKeyboard), as
 * audioin_link_stub.c and videodec2_link_stub.c are for theirs: they only name the imports of kbm.c. Built twice,
 * once a library: -DKBM_STUB_MOUSE (soname libSceMouse.prx) and -DKBM_STUB_KEYBOARD (libSceKeyboard.prx). They are
 * never packaged or executed: the console's own modules answer them.
 */

#include <stdint.h>

#ifdef KBM_STUB_MOUSE
int32_t sceMouseInit(void) { return -1; }

int32_t sceMouseOpen(int32_t user, int32_t type, int32_t index, const void *param) {
  (void)user;
  (void)type;
  (void)index;
  (void)param;
  return -1;
}

int32_t sceMouseRead(int32_t handle, void *data, int32_t count) {
  (void)handle;
  (void)data;
  (void)count;
  return -1;
}
#endif

#ifdef KBM_STUB_KEYBOARD
int32_t sceKeyboardInit(void) { return -1; }

int32_t sceKeyboardOpen(int32_t user, int32_t type, int32_t index, const void *param) {
  (void)user;
  (void)type;
  (void)index;
  (void)param;
  return -1;
}

int32_t sceKeyboardReadState(int32_t handle, void *state) {
  (void)handle;
  (void)state;
  return -1;
}
#endif
