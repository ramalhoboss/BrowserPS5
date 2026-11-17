# The screens a test wrote into browser-run.txt (SHOT: lines, base64 PNG) as
# shot<n>.png next to it.   python shots.py <results folder>
import base64
import sys

d = sys.argv[1]
shots, cur = [], []
for line in open(d + '/browser-run.txt', encoding='utf-8', errors='replace').read().split('\n'):
    if line.startswith('SHOT:'):
        cur.append(line[5:].strip())
    elif cur:
        shots.append(''.join(cur))
        cur = []
if cur:
    shots.append(''.join(cur))
for i, s in enumerate(shots):
    open('%s/shot%d.png' % (d, i), 'wb').write(base64.b64decode(s + '=' * (-len(s) % 4)))
print(len(shots), 'screens')
