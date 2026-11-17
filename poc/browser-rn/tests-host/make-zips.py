"""The .zip files inputs-fuzz.c starts from (2026-11-14): a plain one, one whose names try to leave the folder they
are extracted into, and one far bigger inside than outside.

    python3 make-zips.py <folder>
"""
import os, stat, sys, zipfile

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def entry(z, name, data, method=zipfile.ZIP_DEFLATED, mode=None):
    info = zipfile.ZipInfo(name)
    info.compress_type = method
    if mode is not None:
        info.external_attr = mode << 16
    z.writestr(info, data)


with zipfile.ZipFile(os.path.join(out, 'plain.zip'), 'w') as z:
    entry(z, 'read me.txt', b'hello\n' * 40)
    entry(z, 'stored.bin', bytes(range(256)) * 8, zipfile.ZIP_STORED)
    entry(z, 'folder/', b'')
    entry(z, 'folder/inner/deep.txt', b'deep ' * 3000)
    entry(z, 'folder/caf\u00e9 \u4e2d.txt', 'texto \u00e9'.encode('utf-8') * 50)
    entry(z, 'big.dat', (b'0123456789abcdef' * 4096) * 5)  # 320 KiB, compresses well

with zipfile.ZipFile(os.path.join(out, 'evil.zip'), 'w') as z:
    entry(z, 'ok.txt', b'fine')
    entry(z, '../up1.txt', b'ESCAPED')
    entry(z, '../../up2.txt', b'ESCAPED')
    entry(z, 'a/../../up3.txt', b'ESCAPED')
    entry(z, '/tmp/bps5-zip-escape-test/abs.txt', b'ESCAPED')
    entry(z, 'C:\\win.txt', b'ESCAPED')
    entry(z, 'dir\\..\\..\\up4.txt', b'ESCAPED')
    entry(z, '..\\up5.txt', b'ESCAPED')
    entry(z, './ok/./file.txt', b'fine too')
    entry(z, 'link-out', b'/tmp/bps5-zip-escape-test', zipfile.ZIP_STORED, stat.S_IFLNK | 0o777)
    entry(z, 'link-out/through.txt', b'ESCAPED')
    entry(z, 'x', b'a file')
    entry(z, 'x/y.txt', b'under a file')
    entry(z, 'dup.txt', b'first')
    entry(z, 'dup.txt', b'second')
    entry(z, 'n' * 300 + '.txt', b'long name')
    entry(z, 'ctl\x01\x1f\x7f name.txt', b'control characters')
    entry(z, 'que?stion*star|pipe<lt>gt:colon".txt', b'characters drives refuse')
    entry(z, '.hidden', b'dot first')
    entry(z, 'trail /', b'')
    entry(z, 'deep/' * 60 + 'bottom.txt', b'very deep')

with zipfile.ZipFile(os.path.join(out, 'bomb.zip'), 'w') as z:
    entry(z, 'zeros.bin', b'\0' * (24 << 20))
    entry(z, 'after.txt', b'after the big one')
print('zips written in', out)
