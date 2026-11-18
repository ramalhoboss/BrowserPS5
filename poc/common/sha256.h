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

// SHA-256 (FIPS 180-4), a block at a time, in one header: for the browser's own updates (browser/update.h), where
// the file fetched and the file handed to the ELF loader are each checked against the release's digest, by the
// browser and by the small payload that hands it over (update_after), which links no library for it.
//
//   sha256_t s; sha256_begin(&s); sha256_add(&s, data, n); ...; sha256_end(&s, out32);
//
// Tested on the PC against the standard's own examples (browser/update_test.c).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct sha256 {
  uint32_t h[8];
  uint64_t bytes;
  unsigned char block[64];
  size_t held;  // bytes of block waiting for the rest of it
} sha256_t;

static inline uint32_t
sha256_rotr(uint32_t x, int n) {
  return x >> n | x << (32 - n);
}

static inline void
sha256_block(sha256_t* s, const unsigned char* p) {
  static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64], a[8];

  for(int i = 0; i < 16; i++) {
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  }
  for(int i = 16; i < 64; i++) {
    const uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  memcpy(a, s->h, sizeof(a));
  for(int i = 0; i < 64; i++) {
    const uint32_t s1 = sha256_rotr(a[4], 6) ^ sha256_rotr(a[4], 11) ^ sha256_rotr(a[4], 25);
    const uint32_t ch = (a[4] & a[5]) ^ (~a[4] & a[6]);
    const uint32_t t1 = a[7] + s1 + ch + k[i] + w[i];
    const uint32_t s0 = sha256_rotr(a[0], 2) ^ sha256_rotr(a[0], 13) ^ sha256_rotr(a[0], 22);
    const uint32_t maj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
    const uint32_t t2 = s0 + maj;
    a[7] = a[6];
    a[6] = a[5];
    a[5] = a[4];
    a[4] = a[3] + t1;
    a[3] = a[2];
    a[2] = a[1];
    a[1] = a[0];
    a[0] = t1 + t2;
  }
  for(int i = 0; i < 8; i++) {
    s->h[i] += a[i];
  }
}

static inline void
sha256_begin(sha256_t* s) {
  static const uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s->h, h, sizeof(h));
  s->bytes = 0;
  s->held = 0;
}

static inline void
sha256_add(sha256_t* s, const void* data, size_t n) {
  const unsigned char* p = (const unsigned char*)data;

  s->bytes += n;
  if(s->held) {
    const size_t take = n < 64 - s->held ? n : 64 - s->held;
    memcpy(s->block + s->held, p, take);
    s->held += take;
    p += take;
    n -= take;
    if(s->held < 64) {
      return;
    }
    sha256_block(s, s->block);
    s->held = 0;
  }
  for(; n >= 64; p += 64, n -= 64) {
    sha256_block(s, p);
  }
  memcpy(s->block, p, n);
  s->held = n;
}

static inline void
sha256_end(sha256_t* s, unsigned char out[32]) {
  const uint64_t bits = s->bytes * 8;
  unsigned char tail[72];
  size_t n = 0;

  tail[n++] = 0x80;
  while((s->held + n) % 64 != 56) {
    tail[n++] = 0;
  }
  for(int i = 7; i >= 0; i--) {
    tail[n++] = (unsigned char)(bits >> (8 * i));
  }
  sha256_add(s, tail, n);
  for(int i = 0; i < 8; i++) {
    out[4 * i] = (unsigned char)(s->h[i] >> 24);
    out[4 * i + 1] = (unsigned char)(s->h[i] >> 16);
    out[4 * i + 2] = (unsigned char)(s->h[i] >> 8);
    out[4 * i + 3] = (unsigned char)s->h[i];
  }
}
