// Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
//
// QR codes (ISO/IEC 18004) for the phone's address on the TV (settings.html,
// "Telemóvel"): byte mode (UTF-8), versions 1 to 10, the smallest that fits.
// The steps are those of Project Nayuki's QR Code generator, written again
// here, small. Checked module by module against segno on the PC
// (qr_check.sh, docs/07 "Telemóvel").
//
//   QR.matrix(text, level = 'M', mask = -1)  rows of booleans, true = dark
//   QR.svg(text, level = 'M')                an SVG with the quiet zone round it
'use strict';
const QR = (function () {
  const ECC_PER_BLOCK = {  // [level][version]
    L: [-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18],
    M: [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26],
    Q: [-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24],
    H: [-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28],
  };
  const BLOCKS = {
    L: [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4],
    M: [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5],
    Q: [-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8],
    H: [-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8],
  };
  const FORMAT = {L: 1, M: 0, Q: 3, H: 2};

  function rawModules(v) {
    let r = (16 * v + 128) * v + 64;
    if (v >= 2) {
      const n = Math.floor(v / 7) + 2;
      r -= (25 * n - 10) * n - 55;
      if (v >= 7) r -= 36;
    }
    return r;
  }
  function dataCodewords(v, level) {
    return Math.floor(rawModules(v) / 8) - ECC_PER_BLOCK[level][v] * BLOCKS[level][v];
  }

  // Reed-Solomon over GF(2^8) with the polynomial 0x11d.
  function mul(x, y) {
    let z = 0;
    for (let i = 7; i >= 0; i--) {
      z = (z << 1) ^ ((z >>> 7) * 0x11d);
      z ^= ((y >>> i) & 1) * x;
    }
    return z;
  }
  function divisor(degree) {
    const r = new Array(degree).fill(0);
    r[degree - 1] = 1;
    let root = 1;
    for (let i = 0; i < degree; i++) {
      for (let j = 0; j < degree; j++) {
        r[j] = mul(r[j], root);
        if (j + 1 < degree) r[j] ^= r[j + 1];
      }
      root = mul(root, 2);
    }
    return r;
  }
  function remainder(data, div) {
    const r = div.map(() => 0);
    for (const b of data) {
      const factor = b ^ r.shift();
      r.push(0);
      div.forEach((c, i) => { r[i] ^= mul(c, factor); });
    }
    return r;
  }

  // The codewords: mode, length, the bytes, the end, the padding, and the
  // error correction of each block, interleaved.
  function codewords(bytes, v, level) {
    const capacity = dataCodewords(v, level) * 8;
    const bits = [];
    const put = (value, n) => { for (let i = n - 1; i >= 0; i--) bits.push((value >>> i) & 1); };
    put(4, 4);
    put(bytes.length, v < 10 ? 8 : 16);
    for (const b of bytes) put(b, 8);
    put(0, Math.min(4, capacity - bits.length));
    put(0, (8 - bits.length % 8) % 8);
    for (let pad = 0xec; bits.length < capacity; pad ^= 0xec ^ 0x11) put(pad, 8);
    const data = [];
    for (let i = 0; i < bits.length; i += 8) {
      let b = 0;
      for (let j = 0; j < 8; j++) b = b << 1 | bits[i + j];
      data.push(b);
    }
    const count = BLOCKS[level][v], eccLength = ECC_PER_BLOCK[level][v];
    const raw = Math.floor(rawModules(v) / 8);
    const short = count - raw % count, shortLength = Math.floor(raw / count);
    const div = divisor(eccLength);
    const blocks = [];
    for (let i = 0, k = 0; i < count; i++) {
      const block = data.slice(k, k + shortLength - eccLength + (i < short ? 0 : 1));
      k += block.length;
      const ecc = remainder(block, div);
      if (i < short) block.push(0);
      blocks.push(block.concat(ecc));
    }
    const out = [];
    for (let i = 0; i < blocks[0].length; i++) {
      blocks.forEach((b, j) => {
        if (i != shortLength - eccLength || j >= short) out.push(b[i]);
      });
    }
    return out;
  }

  function penalty(m, size) {
    let score = 0;
    const add = (run, h) => { if (h[0] == 0) run += size; h.pop(); h.unshift(run); };
    const finders = h => {
      const n = h[1];
      const core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
      return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) +
             (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
    };
    for (let pass = 0; pass < 2; pass++) {
      for (let a = 0; a < size; a++) {
        let color = false, run = 0;
        const h = [0, 0, 0, 0, 0, 0, 0];
        for (let b = 0; b < size; b++) {
          const c = pass == 0 ? m[a][b] : m[b][a];
          if (c == color) {
            run++;
            if (run == 5) score += 3;
            else if (run > 5) score++;
          } else {
            add(run, h);
            if (!color) score += finders(h) * 40;
            color = c;
            run = 1;
          }
        }
        if (color) { add(run, h); run = 0; }
        add(run + size, h);
        score += finders(h) * 40;
      }
    }
    for (let y = 0; y < size - 1; y++) {
      for (let x = 0; x < size - 1; x++) {
        const c = m[y][x];
        if (c == m[y][x + 1] && c == m[y + 1][x] && c == m[y + 1][x + 1]) score += 3;
      }
    }
    let dark = 0;
    for (const row of m) for (const c of row) if (c) dark++;
    const total = size * size;
    return score + (Math.ceil(Math.abs(dark * 20 - total * 10) / total) - 1) * 10;
  }

  function matrix(text, level, mask) {
    level = level || 'M';
    if (mask === undefined) mask = -1;
    const bytes = Array.from(new TextEncoder().encode(text));
    let v = 1;
    while (v <= 10 && 4 + (v < 10 ? 8 : 16) + 8 * bytes.length > dataCodewords(v, level) * 8) v++;
    if (v > 10) throw new Error('QR: too long');
    const words = codewords(bytes, v, level);

    const size = v * 4 + 17;
    const m = [], fixed = [];
    for (let i = 0; i < size; i++) {
      m.push(new Array(size).fill(false));
      fixed.push(new Array(size).fill(false));
    }
    const set = (x, y, dark) => { m[y][x] = dark; fixed[y][x] = true; };

    // Timing, finders, alignment.
    for (let i = 0; i < size; i++) {
      set(6, i, i % 2 == 0);
      set(i, 6, i % 2 == 0);
    }
    for (const [fx, fy] of [[3, 3], [size - 4, 3], [3, size - 4]]) {
      for (let dy = -4; dy <= 4; dy++) {
        for (let dx = -4; dx <= 4; dx++) {
          const d = Math.max(Math.abs(dx), Math.abs(dy)), x = fx + dx, y = fy + dy;
          if (x >= 0 && x < size && y >= 0 && y < size) set(x, y, d != 2 && d != 4);
        }
      }
    }
    const align = [];
    if (v > 1) {
      const n = Math.floor(v / 7) + 2;
      const step = Math.ceil((v * 4 + 4) / (n * 2 - 2)) * 2;
      align.push(6);
      for (let p = size - 7; align.length < n; p -= step) align.splice(1, 0, p);
    }
    const last = align.length - 1;
    align.forEach((ay, i) => align.forEach((ax, j) => {
      if ((i == 0 && j == 0) || (i == 0 && j == last) || (i == last && j == 0)) return;
      for (let dy = -2; dy <= 2; dy++) {
        for (let dx = -2; dx <= 2; dx++) set(ax + dx, ay + dy, Math.max(Math.abs(dx), Math.abs(dy)) != 1);
      }
    }));

    // Format (level and mask) and, from version 7, the version.
    const format = k => {
      const d = FORMAT[level] << 3 | k;
      let r = d;
      for (let i = 0; i < 10; i++) r = (r << 1) ^ ((r >>> 9) * 0x537);
      const bits = (d << 10 | r) ^ 0x5412;
      const bit = i => ((bits >>> i) & 1) != 0;
      for (let i = 0; i <= 5; i++) set(8, i, bit(i));
      set(8, 7, bit(6));
      set(8, 8, bit(7));
      set(7, 8, bit(8));
      for (let i = 9; i < 15; i++) set(14 - i, 8, bit(i));
      for (let i = 0; i < 8; i++) set(size - 1 - i, 8, bit(i));
      for (let i = 8; i < 15; i++) set(8, size - 15 + i, bit(i));
      set(8, size - 8, true);
    };
    format(0);
    if (v >= 7) {
      let r = v;
      for (let i = 0; i < 12; i++) r = (r << 1) ^ ((r >>> 11) * 0x1f25);
      const bits = v << 12 | r;
      for (let i = 0; i < 18; i++) {
        const dark = ((bits >>> i) & 1) != 0, a = size - 11 + i % 3, b = Math.floor(i / 3);
        set(a, b, dark);
        set(b, a, dark);
      }
    }

    // The data, two columns at a time, up and down, from the right.
    let i = 0;
    for (let right = size - 1; right >= 1; right -= 2) {
      if (right == 6) right = 5;
      for (let vert = 0; vert < size; vert++) {
        for (let j = 0; j < 2; j++) {
          const x = right - j, up = ((right + 1) & 2) == 0, y = up ? size - 1 - vert : vert;
          if (!fixed[y][x] && i < words.length * 8) {
            m[y][x] = ((words[i >>> 3] >>> (7 - (i & 7))) & 1) != 0;
            i++;
          }
        }
      }
    }

    // The mask: the one asked for, or the one with the lowest penalty.
    const flip = k => {
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          let invert;
          switch (k) {
            case 0: invert = (x + y) % 2 == 0; break;
            case 1: invert = y % 2 == 0; break;
            case 2: invert = x % 3 == 0; break;
            case 3: invert = (x + y) % 3 == 0; break;
            case 4: invert = (Math.floor(x / 3) + Math.floor(y / 2)) % 2 == 0; break;
            case 5: invert = x * y % 2 + x * y % 3 == 0; break;
            case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
            default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
          }
          if (!fixed[y][x] && invert) m[y][x] = !m[y][x];
        }
      }
    };
    if (mask < 0) {
      let best = Infinity;
      for (let k = 0; k < 8; k++) {
        flip(k);
        format(k);
        const p = penalty(m, size);
        if (p < best) {
          best = p;
          mask = k;
        }
        flip(k);
      }
    }
    flip(mask);
    format(mask);
    return m;
  }

  // Black on white whatever the theme (most phones read only that), with
  // the quiet zone of 4 modules the standard asks for.
  function svg(text, level) {
    const m = matrix(text, level), size = m.length, all = size + 8;
    let path = '';
    m.forEach((row, y) => row.forEach((dark, x) => {
      if (dark) path += 'M' + (x + 4) + ' ' + (y + 4) + 'h1v1h-1z';
    }));
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + all + ' ' + all +
           '" shape-rendering="crispEdges"><rect width="' + all + '" height="' + all +
           '" fill="#fff"/><path d="' + path + '" fill="#000"/></svg>';
  }

  return {matrix, svg};
})();
