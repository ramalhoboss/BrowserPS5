"""The downloads' engine of the browser (main.cpp multi_*: several parts, a failing line, pause, another session),
run on the PC against a pretend network and a pretend WebKit, with a clock of its own (2026-11-14).

It cuts the code out of ../../browser/main.cpp AS IT IS (nothing is written again here) and puts it between
stand-ins: a server that gives ranges or refuses them, connections that are cut, go quiet, are lost; the engine's
side of a download (blocks of 1 MiB written at their place, cancels answered late or never); the user pausing;
the browser closing (cleanly, or as a fall) and opening again. Thousands of seeded scenarios, each checked:

  - a download that ends "done" has EVERY byte of its file written (no hole, whatever happened on the way);
  - one that can go on (the server gives ranges) does end, once the line is calm again: no stop for ever;
  - after a pause or another session, every byte the file has not is in the ranges asked for again;
  - nothing reads or writes outside its memory (AddressSanitizer, UndefinedBehaviorSanitizer).

    python3 make-multi-sim.py <out.cpp>          then build-and-run.sh compiles and runs it
"""
import sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', '..', 'browser', 'main.cpp'), encoding='utf-8').read()


def cut(start, end, keep_end=True):
    a = src.index(start)
    b = src.index(end, a)
    return src[a:b + (len(end) if keep_end else 0)]


def function(head):
    a = src.index(head)
    return src[a:src.index('\n}\n', a) + 3]


tick = src.index('\nmulti_tick(void) {')
cuts = {
    'DOWNLOAD_T': cut('typedef struct download {', '} download_t;\n'),
    'ENGINE': src[src.index('#define MULTI_WORKERS 8'):src.index('\n#endif\n', tick) + 1],
    'PROGRESS': function('static void\ndownload_progress('),
    'FINISHED': function('static void\ndownload_finished('),
    'FAILED': function('static void\ndownload_failed('),
    'KEPT': cut("// A map read: the set's facts and missing ranges into m", '#endif\n\n// Asks for a download that failed or was cancelled once more', keep_end=False),
    'URL_IS_WEB': function('static bool\nurl_is_web('),
    'CANCELLED': function('static void\ndownload_cancelled('),
    'CANCEL': function('static void\ndownload_cancel('),
    'NAME_ENDS': function('static bool\nname_ends_with('),
}
template = open(os.path.join(HERE, 'multi-sim.in.cpp'), encoding='utf-8').read()
for name, text in cuts.items():
    assert template.count('@' + name + '@') == 1, name
    template = template.replace('@' + name + '@', text)
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(template)
print('multi sim written: %s (%d bytes of the browser\'s own code)' % (sys.argv[1], sum(len(t) for t in cuts.values())))
