#!/usr/bin/env python3
"""scrub-paths.py [--check] <file>...        (2026-11-14)

The build machine's user names out of the binaries of a release. A compiler leaves the paths of its sources in
what it makes (the messages of asserts, the names of lambdas): /home/<user>/browserps5-build/WebKit/Source/...,
/mnt/c/Users/<USER>/Desktop/... Thousands of them were in the engine's library and in the title, and with them the
name of the account they were built on.

Each "/home/<user>/" and "/Users/<USER>/" becomes the same text with "build" in the name's place, cut or filled
to the SAME LENGTH: nothing moves in the file, no size or offset changes; only those letters do. Then the file is
read again: the names must be nowhere in it, in any case, or this ends with an error.

    --check    change nothing: count, and end with an error if a name is there

Which names: SCRUB_NAMES (comma separated) if set; else this machine's $USER and the <USER> of /mnt/c/Users/<USER>/
in this script's own path.
"""
import os
import re
import sys


def names():
    said = os.environ.get('SCRUB_NAMES', '')
    if said:
        return [n for n in said.split(',') if n]
    found = []
    user = os.environ.get('USER') or os.environ.get('LOGNAME') or ''
    if user and user != 'root':
        found.append(user)
    here = os.path.realpath(__file__).replace('\\', '/')
    m = re.search(r'/Users/([^/]+)/', here)
    if m and m.group(1) not in found:
        found.append(m.group(1))
    return found


def other(name):
    """A word of the same length to put in a name's place."""
    return ('build' + 'x' * len(name))[:len(name)]


def main(argv):
    check = '--check' in argv
    files = [a for a in argv if not a.startswith('--')]
    who = names()
    if not who or not files:
        print('scrub-paths: no names or no files (names: %s)' % ', '.join(who), file=sys.stderr)
        return 2
    pairs = []
    for name in who:
        for before in ('/home/', '/Users/'):
            pairs.append(((before + name + '/').encode(), (before + other(name) + '/').encode()))
    anywhere = re.compile(b'|'.join(re.escape(n.encode()) for n in who), re.IGNORECASE)
    bad = 0
    total = 0
    for path in files:
        with open(path, 'rb') as f:
            data = f.read()
        count = sum(data.count(old) for old, new in pairs)
        if check:
            left = len(anywhere.findall(data))
            if left:
                bad += 1
                print('scrub-paths: %s: the name is there %d time(s)' % (path, left))
            continue
        if count:
            size = len(data)
            for old, new in pairs:
                assert len(old) == len(new)
                data = data.replace(old, new)
            assert len(data) == size
            mode = os.stat(path).st_mode
            with open(path, 'r+b') as f:  # in place: the same file, the same length
                f.write(data)
            os.chmod(path, mode)
        left = anywhere.findall(data)
        total += count
        if left:
            bad += 1
            m = anywhere.search(data)
            around = data[max(0, m.start() - 40):m.end() + 20]
            print('scrub-paths: %s: %d changed, but the name is STILL there %d time(s), as in %r' % (path, count, len(left), around))
        elif count:
            print('scrub-paths: %s: %d path(s) changed, the name is nowhere in it now' % (os.path.basename(path), count))
    if check and not bad:
        print('scrub-paths: checked %d file(s): the names are in none' % len(files))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
