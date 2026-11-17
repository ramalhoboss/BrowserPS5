"""What the app itself says in the pages' other languages (2026-11-14):  python lang_c_test.py
Every LM("...") of ../../browser/main.cpp has its row in ../pages/lang_c.h; every row has five texts, none empty,
each with the same %s, %d and %u as the English, in the same order; and no row is left that main.cpp does not ask."""
import re, os, sys, io

HERE = os.path.dirname(os.path.abspath(__file__))
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
LIT = r'"((?:[^"\\\n]|\\.)*)"'


def c_text(parts):
    raw = ''.join(parts).encode('latin-1', 'backslashreplace').decode('latin-1')
    out = bytearray()
    i = 0
    data = ''.join(parts)
    while i < len(data):
        c = data[i]
        if c == '\\' and i + 1 < len(data):
            n = data[i + 1]
            if n == 'x':
                out.append(int(data[i + 2:i + 4], 16))
                i += 4
                continue
            out += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\', "'": b"'"}.get(n, n.encode('utf-8'))
            i += 2
            continue
        out += c.encode('utf-8')
        i += 1
    return out.decode('utf-8')


main = open(os.path.join(HERE, '..', '..', 'browser', 'main.cpp'), encoding='utf-8').read()
asked = []
for m in re.finditer(r'\bLM\(\s*(' + LIT + r'(?:\s*' + LIT + r')*)\s*\)', main):
    asked.append(c_text(re.findall(LIT, m.group(1))))
table = open(os.path.join(HERE, '..', 'pages', 'lang_c.h'), encoding='utf-8').read()
rows = {}
for m in re.finditer(r'\{\s*' + LIT + r'\s*,\s*\{((?:\s*' + LIT + r'\s*,?)+)\}\s*\}', table):
    rows[c_text([m.group(1)])] = [c_text([x]) for x in re.findall(LIT, m.group(2))]
fail = 0


def check(ok, what):
    global fail
    if not ok:
        fail += 1
        print('FAIL:', what)


marks = lambda t: re.findall(r'%[sdu]', t)
for en in sorted(set(asked)):
    check(en in rows, 'no row for: ' + en)
for en, to in rows.items():
    check(en in asked, 'a row main.cpp does not ask for: ' + en)
    check(len(to) == 5 and all(t.strip() == t and t for t in to), 'not five whole texts: ' + en)
    for t in to:
        check(marks(t) == marks(en), 'other marks than the English: %s -> %s' % (en, t))
        check('"' not in t and '\\' not in t, 'a quote or a backslash in: ' + t)
print('%s: %d texts asked by main.cpp (%d places), %d rows, %d failed' % ('FAIL' if fail else 'PASS', len(set(asked)), len(asked), len(rows), fail))
sys.exit(1 if fail else 0)
