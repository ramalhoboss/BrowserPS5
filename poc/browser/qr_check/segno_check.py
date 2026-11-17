# Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
# Compares qr.js matrices (ours.json from node_check.js) with segno (qr_check.sh).
import json, sys, segno
import segno.encoder as _e, segno.consts as _c
def _pad(buff, version, length):
    # segno adds 8 zero bits when the stream is already whole bytes (valid, but
    # not the shortest): the standard pads only up to the boundary.
    if version not in (_c.VERSION_M1, _c.VERSION_M3):
        buff.extend([0] * ((8 - length % 8) % 8))
_e.write_padding_bits = _pad
cases = json.load(open(sys.argv[1], encoding='utf-8'))
same = differ = mask_differ = 0
for c in cases:
    t, l, k, ours = c['t'], c['l'], c['k'], c['m']
    if ours and ours[0].startswith('error'):
        print('OURS FAILED', l, k, len(t), ours[0]); differ += 1; continue
    q = segno.make(t, error=l.lower(), mask=None if k < 0 else k, boost_error=False, micro=False, mode='byte', encoding='utf-8')
    theirs = [''.join('1' if v else '0' for v in row) for row in q.matrix]
    if theirs == ours:
        same += 1
        continue
    if k < 0:  # maybe only the mask was chosen otherwise: same mask then?
        # find our mask by trying each with segno
        found = None
        for mk in range(8):
            qq = segno.make(t, error=l.lower(), mask=mk, boost_error=False, micro=False, mode='byte', encoding='utf-8')
            if [''.join('1' if v else '0' for v in row) for row in qq.matrix] == ours:
                found = mk
        if found is not None:
            mask_differ += 1
            print('mask choice differs: ours %d, segno %d (%s, %d bytes, v%s)' % (found, q.mask, l, len(t.encode()), q.version))
            continue
    differ += 1
    print('DIFFER', l, k, len(t.encode()), 'segno v%s mask %s size %d vs ours %d' % (q.version, q.mask, len(theirs), len(ours)))
print('same %d, mask choice only %d, different %d' % (same, mask_differ, differ))
