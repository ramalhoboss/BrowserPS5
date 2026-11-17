# build-rn.sh: puts RN's style and script at the end of the start page's own.
#   python3 start-rn.py <start.html> <start-rn.css> <start-rn.js> [a script to go before it ...]
# (2026-11-15: patches-index.js, made by patches/make-index.js, goes before start-rn.js, which reads it.)
import sys

page, css, js = (open(p, encoding="utf-8").read() for p in sys.argv[1:4])
a = "</style></head><body>"
b = "</script></body></html>"
assert page.count(a) == 1 and page.count(b) == 1, "start.html changed: start-rn.py's marks"
before = "".join(open(p, encoding="utf-8").read() for p in sys.argv[4:])
page = page.replace(a, css + a).replace(b, before + js + b)
open(sys.argv[1], "w", encoding="utf-8", newline="\n").write(page)
