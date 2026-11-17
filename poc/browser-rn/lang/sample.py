"""A sample of a language's texts beside the Portuguese and the English, to see that they are the same texts.
    python sample.py es [every]"""
import json, os, sys, io, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
spec = importlib.util.spec_from_file_location('fl', os.path.join(HERE, 'from-list.py'))
src = open(os.path.join(HERE, 'from-list.py'), encoding='utf-8').read().replace('\nmain()\n', '\n')
ns = {'__file__': os.path.join(HERE, 'from-list.py')}
exec(compile(src, 'from-list.py', 'exec'), ns)
pairs, pats = ns['texts']()
code = sys.argv[1]
every = int(sys.argv[2]) if len(sys.argv) > 2 else 37
table = json.load(open(os.path.join(HERE, code + '.json'), encoding='utf-8'))
for i in range(0, len(pairs), every):
    pt, en = pairs[i]
    print('%3d  %s | %s | %s' % (i, pt[:44], en[:44], table['texts'].get(pt, '—')[:48]))
for i in range(0, len(pats), 9):
    pt, en = pats[i]
    print('P%-2d  %s | %s | %s' % (i, pt[:40], en[:40], dict(map(tuple, table['patterns'])).get(pt, '—')[:44]))
