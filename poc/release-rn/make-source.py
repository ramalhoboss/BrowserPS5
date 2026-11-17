#!/usr/bin/env python3
"""make-source.py [version]        (2026-11-16)

The source that goes with a release (GPL-3.0-or-later, and the LGPL parts of the engine): a copy of the folders the
release is built from, made only of source files (no builds, tars, ELFs, logs or test results), into
browser-rn/out/release/BrowserPS5-<version>-source/. Nothing is published by this script.

Then every text file of the copy is read for what must not leave this PC: the build machine's account names (as
common/scrub-paths.py finds them), addresses of a home network (192.168.x.x, 10.x.x.x), e-mail addresses, Discord
webhooks and tokens. Paths of the build machine in scripts are written with a placeholder in the copy; anything else
found is listed (file, line, kind; never the value itself) and the script ends with an error.

Run on Windows with:  python -I release-rn/make-source.py
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
POC = os.path.dirname(HERE)
VERSION = sys.argv[1] if len(sys.argv) > 1 else open(os.path.join(HERE, 'VERSION')).read().strip()
DEST = os.path.join(POC, 'browser-rn', 'out', 'release', 'BrowserPS5-%s-source' % VERSION)

# What the release is built from (build-release-rn.sh, browser-rn/*.sh, the Makefiles they call).
FOLDERS = ['browser', 'browser-rn', 'release-rn', 'common', 'webkit', 'webkit_title', 'jsc_shell', 'gl_zink',
           'gpu_broker', 'media', 'hello_page', 'bigapp_gl', 'launcher', 'play_after', 'install_tar', 'remove_files']
# Folders the build reads that live in the project's first folder (Desktop/BrowserPS5/poc), copied to poc/<name>.
OTHER_FOLDERS = {'gl_soft': os.path.join(os.path.dirname(os.path.dirname(POC)), 'BrowserPS5', 'poc', 'gl_soft')}
FILES = ['send.ps1']
# This console's and this PC's addresses in the scripts and tests: the same example address in every file.
ADDRESSES = {'192.168.1.50': '192.168.1.50', '192.168.1.51': '192.168.1.51'}
# Never copied: builds, test results, the console's tests and logs.
SKIP_DIRS = {'out', 'build', 'results', 'preview', '__pycache__', 'node_modules', '.git', '.vs', 'console',
             'tests-host-out', 'video-test', 'old'}
SOURCE_EXT = {'.c', '.cc', '.cpp', '.h', '.hpp', '.hh', '.inc', '.S', '.s', '.ld', '.map', '.ver', '.def',
              '.js', '.mjs', '.css', '.html', '.json', '.py', '.sh', '.ps1', '.md', '.txt', '.cmake', '.in',
              '.patch', '.diff', '.conf', '.xml', '.svg', '.png', '.jpg', '.jpeg', '.webp', '.ico', '.ttf', '.otf',
              '.woff', '.woff2', '.ini', '.yml', '.yaml', '.toml', '.mk', '.frag', '.vert', '.glsl', '.pc', '.awk',
              '.dds', '.raw'}
SOURCE_NAMES = {'Makefile', 'CMakeLists.txt', 'LICENSE', 'COPYING', 'README', 'NOTICE', 'VERSION'}
TEXT_EXT = {'.c', '.cc', '.cpp', '.h', '.hpp', '.hh', '.inc', '.S', '.s', '.ld', '.map', '.ver', '.def', '.js',
            '.mjs', '.css', '.html', '.json', '.py', '.sh', '.ps1', '.md', '.txt', '.cmake', '.in', '.patch',
            '.diff', '.conf', '.xml', '.svg', '.ini', '.yml', '.yaml', '.toml', '.mk', '.frag', '.vert', '.glsl',
            '.pc', '.awk', ''}
MAX_FILE = 24 * 1024 * 1024
# Files of the console's tests and of this PC only (logs, a test's own settings, scratch files).
# (and the pictures of unknown origin that the art was once made from: no script uses them)
SKIP_FILE = re.compile(r'(\.log$|^browser-run|^browser-title|^web-stderr|^web-env|^klog|^ps5-ip|^webhook|-results?\.|'
                       r'-src\.jpg$)', re.I)


def account_names():
    names = []
    for n in (os.environ.get('SCRUB_NAMES', '').split(',') + [os.environ.get('USERNAME', ''),
                                                              os.environ.get('USER', '')]):
        if n and n.lower() not in ('root', 'build') and n.lower() not in [x.lower() for x in names]:
            names.append(n)
    m = re.search(r'[\\/]Users[\\/]([^\\/]+)[\\/]', os.path.realpath(__file__))
    if m and m.group(1).lower() not in [x.lower() for x in names]:
        names.append(m.group(1))
    return names


def copy():
    if os.path.exists(DEST):
        shutil.rmtree(DEST)
    count, size, left = 0, 0, {}
    tops = [(os.path.join(POC, f), os.path.join(DEST, 'poc', f)) for f in FOLDERS]
    tops += [(src, os.path.join(DEST, 'poc', f)) for f, src in OTHER_FOLDERS.items()]
    for top, into in tops:
        if not os.path.isdir(top):
            sys.exit('error: %s is missing' % top)
        for root, dirs, files in os.walk(top):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith('build')]
            for name in files:
                src = os.path.join(root, name)
                ext = os.path.splitext(name)[1].lower() if os.path.splitext(name)[1].lower() != '.s' \
                    else os.path.splitext(name)[1]
                ok = (ext in SOURCE_EXT or name in SOURCE_NAMES) and not SKIP_FILE.search(name)
                if ok and os.path.getsize(src) > MAX_FILE:
                    ok = False
                if not ok:
                    left[ext or name] = left.get(ext or name, 0) + 1
                    continue
                dst = os.path.join(into, os.path.relpath(src, top))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                count += 1
                size += os.path.getsize(src)
    for name in FILES:
        shutil.copy2(os.path.join(POC, name), os.path.join(DEST, 'poc', name))
        count += 1
    # The copy's own README (how it is laid out and built) and the licence, at its top.
    shutil.copy2(os.path.join(HERE, 'SOURCE-README.md'), os.path.join(DEST, 'README.md'))
    shutil.copy2(os.path.join(HERE, 'LICENSE'), os.path.join(DEST, 'LICENSE'))
    count += 2
    print('copied %d files, %.1f MiB, into %s' % (count, size / 1048576, DEST))
    print('left out: ' + ', '.join('%s x%d' % (k, v) for k, v in sorted(left.items(), key=lambda kv: -kv[1])[:25]))


def scan():
    names = account_names()
    # The build machine's paths in scripts: a placeholder in the copy.
    path_res = [re.compile(r'([A-Za-z]:\\\\?Users\\\\?|/mnt/[a-z]/Users/|/[a-z]/Users/|/home/|/Users/)' + re.escape(n)
                           + r'(?=[\\/])', re.I) for n in names]
    finds = [('account name', re.compile('|'.join(re.escape(n) for n in names), re.I)),
             ('home network address', re.compile(r'\b(192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3})\b')),
             ('e-mail address', re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+\.[A-Za-z0-9.-]*[A-Za-z]{2,}')),
             ('Discord webhook', re.compile(r'discord(app)?\.com/api/webhooks/\d+/[\w-]{20,}', re.I)),
             ('token', re.compile(r'\b(ghp_[A-Za-z0-9]{20,}|github_pat_\w{20,}|sk-[A-Za-z0-9]{20,}|xox[bp]-[\w-]{10,})'))]
    # Addresses and mails that are fine in a public source (examples, upstream authors' public addresses).
    fine = re.compile(r'(192\.168\.[01]\.(0|1|50|51)$|10\.0\.0\.[01]$|\.example\b|@example\.|noreply|no-reply|'
                      r'@[a-z]\.pt$|@bank\.pt$|@m\.youtube\.com$|@sony\.com$|webhooks/123456|@users\.noreply|'
                      r'@webkit\.org|@apple\.com|@gnu\.org|@fsf\.org|@freedesktop\.org|@mesa3d|@igalia\.com|'
                      r'@gnome\.org|@mozilla\.|@google\.com|@chromium\.org|@[\w.-]*\.(png|jpg|svg|webp)\b)', re.I)
    # 2026-11-17: quoted requests in comments (who asked, a colon, the words), also when a comment's line break
    # splits them.
    quote = re.compile(r'\b(the|o|do)\b[\s*/#!-]*\b(user|utilizador)\b[\s*/#]*:', re.I)
    changed, hits = 0, []
    for root, _, files in os.walk(DEST):
        for name in files:
            p = os.path.join(root, name)
            if os.path.splitext(name)[1] not in TEXT_EXT and name not in SOURCE_NAMES:
                continue
            try:
                text = open(p, encoding='utf-8', errors='surrogateescape').read()
            except OSError:
                continue
            new = text
            for r in path_res:
                new = r.sub(lambda m: m.group(1) + '<you>', new)
            for old, example in ADDRESSES.items():
                new = re.sub(re.escape(old) + r'(?!\d)', example, new)
            if new != text:
                open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(new)
                changed += 1
            for i, line in enumerate(new.splitlines(), 1):
                for kind, r in finds:
                    # (an upstream project's own authors, in its own licence and headers: public on purpose)
                    if kind == 'e-mail address' and ('plutovg' in p or name.upper().startswith(('LICENSE', 'FTL'))):
                        continue
                    for m in r.finditer(line):
                        if not fine.search(m.group(0)):
                            hits.append((os.path.relpath(p, DEST), i, kind))
            for m in quote.finditer(new):
                hits.append((os.path.relpath(p, DEST), new.count('\n', 0, m.start()) + 1, 'quoted request'))
    print('paths of the build machine replaced in %d file(s)' % changed)
    if hits:
        by = {}
        for f, i, kind in hits:
            by.setdefault((f, kind), []).append(i)
        for (f, kind), lines in sorted(by.items()):
            print('  %s: %s, line(s) %s' % (f, kind, ' '.join(map(str, lines[:8])) + (' ...' if len(lines) > 8 else '')))
        print('NOT CLEAN: %d finding(s) in %d file(s)' % (len(hits), len({f for f, _, _ in hits})))
        return 1
    print('clean: no account name, home address, e-mail, webhook, token or quoted request')
    return 0


if __name__ == '__main__':
    copy()
    sys.exit(scan())
