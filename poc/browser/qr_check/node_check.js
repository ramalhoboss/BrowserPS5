// Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
// Runs qr.js on the cases (qr_check.sh) and writes the matrices as JSON.
const fs = require('fs');
const code = fs.readFileSync(process.argv[2], 'utf8');
const QR = new Function(code + '\nreturn QR;')();
const cases = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const extra = [];
for (const n of [30, 45, 60, 80, 100, 120, 150, 180, 200, 210, 230, 270])
  for (const l of ['L', 'M', 'Q', 'H']) extra.push(['x'.repeat(n).replace(/x/g, (c, i) => String.fromCharCode(97 + i * 7 % 26)), l, -1]);
for (let k = 0; k < 8; k++) extra.push(['http://192.168.1.51:8090/', 'M', k], ['Mask test 0123456789 abcdefghij klmnopqrstuvwxyz', 'Q', k]);
const out = [];
for (const [t, l, k] of cases.concat(extra)) {
  let m;
  try { m = QR.matrix(t, l, k).map(r => r.map(c => c ? '1' : '0').join('')); } catch (e) { m = ['error ' + e.message]; }
  out.push({t, l, k, m});
}
fs.writeFileSync(process.argv[4], JSON.stringify(out));
console.log(out.length + ' cases');
