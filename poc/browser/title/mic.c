/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

/*
 * The controller's microphone, for BrowserPS5 RN's voice search (the user, 2026-11-14/15: "podemos também
 * ativar a pesquisa por voz com o microfone do DualSense"; the PS5's keyboard has no dictation on his
 * console). The engine has no speech API for the pages, so the title listens itself and the words are made
 * on the console (whisper.cpp, later).
 *
 * This file is the FIRST STEP, a proof: does the title get sound from the microphone at all? Built only with
 * TITLE_MIC=1, and run only when the file /app0/mic-probe is in the title's folder (removed as it is seen, so
 * once). It opens the PS5's audio input for the user, reads MIC_SECONDS of it, says in the title's log what
 * it got (how loud, how long it took) and writes it to /app0/mic-probe.wav (16 kHz, one channel, 16 bits),
 * for it to be listened to on the PC and given to the recogniser there. Nothing else reads the microphone.
 *
 * libSceAudioIn's three functions are imported by name at link time (audioin_link_stub.c), as the video
 * decoder's are (videodec2_link_stub.c): sceKernelDlsym gives nothing by name in a title. If the system has
 * not loaded the library with the title, it is loaded here by its path, and the imports looked at again.
 */
#include "mic.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "poc_log.h"

int sceSysmoduleLoadModule(uint32_t id);
const char *sceKernelGetFsSandboxRandomWord(void);
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);

int32_t sceAudioInOpen(int32_t user, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioInInput(int32_t handle, void *dest);
int32_t sceAudioInClose(int32_t handle);

/* An import's address as the loader left it in the GOT: 0 while unresolved (video_hw.h, HW_GOT). */
#define MIC_GOT(fn)                                                        \
    __extension__({                                                        \
        void *p_;                                                          \
        __asm__ volatile("movq " #fn "@GOTPCREL(%%rip), %0" : "=r"(p_));   \
        p_;                                                                \
    })

#define MIC_RATE    16000
#define MIC_BLOCK   256 /* samples a read gives (16 ms) */
#define MIC_SECONDS 4
#define MIC_BLOCKS  (MIC_SECONDS * MIC_RATE / MIC_BLOCK)
#define MIC_WAV     "/app0/mic-probe.wav"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int wav_write(const char *path, const int16_t *pcm, size_t samples)
{
    unsigned char head[44];
    const uint32_t bytes = (uint32_t)(samples * 2);
    memcpy(head, "RIFF", 4);
    put32(head + 4, 36 + bytes);
    memcpy(head + 8, "WAVEfmt ", 8);
    put32(head + 16, 16);
    head[20] = 1, head[21] = 0; /* PCM */
    head[22] = 1, head[23] = 0; /* one channel */
    put32(head + 24, MIC_RATE);
    put32(head + 28, MIC_RATE * 2);
    head[32] = 2, head[33] = 0;
    head[34] = 16, head[35] = 0;
    memcpy(head + 36, "data", 4);
    put32(head + 40, bytes);
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    const int ok = write(fd, head, sizeof head) == (ssize_t)sizeof head && write(fd, pcm, bytes) == (ssize_t)bytes;
    close(fd);
    return ok ? 0 : -1;
}

void mic_probe(int user)
{
    void *f[3] = {MIC_GOT(sceAudioInOpen), MIC_GOT(sceAudioInInput), MIC_GOT(sceAudioInClose)};
    POC_LOG("mic: imports at the start: open %p, input %p, close %p", f[0], f[1], f[2]);
    if (!f[0] || !f[1] || !f[2]) {
        const char *word = sceKernelGetFsSandboxRandomWord();
        char paths[2][128];
        snprintf(paths[0], sizeof paths[0], "/%s/common/lib/libSceAudioIn.sprx", word ? word : "system");
        snprintf(paths[1], sizeof paths[1], "/system/common/lib/libSceAudioIn.sprx");
        int h = -1;
        for (int i = 0; i < 2 && h < 0; i++) {
            int res = 0;
            h = sceKernelLoadStartModule(paths[i], 0, NULL, 0, NULL, &res);
            POC_LOG("mic: load %s -> 0x%x", paths[i], (unsigned)h);
        }
        f[0] = MIC_GOT(sceAudioInOpen), f[1] = MIC_GOT(sceAudioInInput), f[2] = MIC_GOT(sceAudioInClose);
        POC_LOG("mic: imports after the load: open %p, input %p, close %p", f[0], f[1], f[2]);
    }
    if (!f[0] || !f[1] || !f[2]) {
        POC_LOG("mic: FAIL the audio input's functions are not there: no microphone for the title this way");
        return;
    }
    /* The kinds of input, as the public homebrew headers name them: 1 general, 0 voice chat, 5 voice
     * recognition. The first that opens is used. One channel of 16 bits: param 0. */
    static const int kinds[] = {1, 0, 5};
    int handle = -1, kind = -1;
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0] && handle < 0; i++) {
        handle = sceAudioInOpen(user, kinds[i], 0, MIC_BLOCK, MIC_RATE, 0);
        POC_LOG("mic: sceAudioInOpen(user 0x%x, kind %d, %d samples, %d Hz): 0x%x", (unsigned)user, kinds[i], MIC_BLOCK,
                MIC_RATE, (unsigned)handle);
        kind = kinds[i];
    }
    if (handle < 0) {
        POC_LOG("mic: FAIL no kind of input opened");
        return;
    }
    int16_t *pcm = (int16_t *)calloc((size_t)MIC_BLOCKS * MIC_BLOCK, sizeof(int16_t));
    if (!pcm) {
        sceAudioInClose(handle);
        return;
    }
    const double start = now_ms();
    int blocks = 0, errors = 0, last_error = 0, peak = 0;
    long long sum = 0;
    for (; blocks < MIC_BLOCKS && errors < 20 && now_ms() - start < (MIC_SECONDS + 3) * 1000.0;) {
        const int got = sceAudioInInput(handle, pcm + (size_t)blocks * MIC_BLOCK); /* waits for the block */
        if (got < 0) {
            errors++;
            last_error = got;
            usleep(16000);
            continue;
        }
        blocks++;
    }
    const double took = now_ms() - start;
    const size_t samples = (size_t)blocks * MIC_BLOCK;
    for (size_t i = 0; i < samples; i++) {
        const int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        peak = a > peak ? a : peak;
        sum += a;
    }
    POC_LOG("mic: kind %d: %d block(s) of %d in %.0f ms (%d error(s), the last 0x%x); loudest sample %d of 32767, mean %lld",
            kind, blocks, MIC_BLOCKS, took, errors, (unsigned)last_error, peak, samples ? sum / (long long)samples : 0LL);
    POC_LOG("mic: %s", !blocks ? "FAIL nothing was read" : peak < 8 ? "only silence came (the microphone muted on the controller?)"
                                                                     : "PASS sound came from the microphone");
    if (blocks)
        POC_LOG("mic: %s: %s", MIC_WAV, wav_write(MIC_WAV, pcm, samples) ? "could NOT be written" : "written");
    free(pcm);
    POC_LOG("mic: sceAudioInClose: 0x%x", (unsigned)sceAudioInClose(handle));
}
