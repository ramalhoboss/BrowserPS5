/*
 * 2026-11-08: host-link stub for the PS5's system video decoder library
 * (libSceVideodec2). It only names the imports for player.c's PL_HWDEC path;
 * it is never packaged or executed: the console's own module answers them.
 * (sceKernelDlsym on the module the title loads by path gives 0x80020003 for
 * every name, so the names are imported at link time instead.)
 */

#include <stdint.h>

int32_t sceVideodec2QueryDecoderMemoryInfo(const void* config, void* memory) {
  (void)config;
  (void)memory;
  return -1;
}

int32_t sceVideodec2CreateDecoder(const void* config, const void* memory, void** decoder) {
  (void)config;
  (void)memory;
  (void)decoder;
  return -1;
}

int32_t sceVideodec2Decode(void* decoder, const void* input, void* frame, void* output) {
  (void)decoder;
  (void)input;
  (void)frame;
  (void)output;
  return -1;
}

int32_t sceVideodec2Reset(void* decoder) {
  (void)decoder;
  return -1;
}

int32_t sceVideodec2GetPictureInfo(const void* output, void* first, void* second) {
  (void)output;
  (void)first;
  (void)second;
  return -1;
}

int32_t sceVideodec2MapDirectMemory(void* decoder, const void* memory) {
  (void)decoder;
  (void)memory;
  return -1;
}

/* 22:3x: the compute queue the decoder runs on (resource type 1). */
int32_t sceVideodec2QueryComputeMemoryInfo(void* memory) {
  (void)memory;
  return -1;
}

int32_t sceVideodec2AllocateComputeQueue(const void* config, const void* memory, void** queue) {
  (void)config;
  (void)memory;
  (void)queue;
  return -1;
}

/* 23:xx: a decoder away (another codec, a bigger video). */
int32_t sceVideodec2DeleteDecoder(void* decoder) {
  (void)decoder;
  return -1;
}

/* 2026-11-09 18:5x: the compute queues go when the title is away (video_hw.h). */
int32_t sceVideodec2ReleaseComputeQueue(void* queue) {
  (void)queue;
  return -1;
}
