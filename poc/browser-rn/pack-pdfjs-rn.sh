# 2026-11-11: BrowserPS5 RN's PDF reader as a tar for install_tar (out/pdfjs-rn.tar):
# PDF.js, Mozilla's (Apache 2.0), the copy WebKit itself carries
# (Source/ThirdParty/pdfjs), into /user/homebrew/browserps5-rn/pdfjs
# (main.cpp PDF_DIR). Without the debugger, the scripting sandbox and the
# languages the browser does not speak. Installed once; the app works without.
#   wsl -e bash pack-pdfjs-rn.sh
set -e
src=$HOME/browserps5-build/WebKit/Source/ThirdParty/pdfjs
out=/mnt/c/Users/<you>/Desktop/BrowserPS5/BrowserPS5-WebKit/poc/browser-rn/out/pdfjs-rn.tar
T=$(mktemp -d); D=$T/user/homebrew/browserps5-rn/pdfjs
mkdir -p $D/build $D/web/locale
cp $src/LICENSE $D/
# The port's file types know ".js" only (MIMETypeRegistryPlayStation.cpp): a ".mjs" from a file is not
# JavaScript to it, and a module that is not JavaScript is refused (2026-11-11 20:33: "load failed: SCRIPT
# .../pdf.mjs"). The same files as ".js", and their names where they are asked for.
cp $src/build/pdf.mjs $D/build/pdf.js
cp $src/build/pdf.worker.mjs $D/build/pdf.worker.js
cp $src/web/viewer.html $src/web/viewer.css $D/web/
cp $src/web/viewer.mjs $D/web/viewer.js
sed -i 's/pdf\.worker\.mjs/pdf.worker.js/g; s/pdf\.sandbox\.mjs/pdf.sandbox.js/g; s/pdf\.mjs/pdf.js/g; s/viewer\.mjs/viewer.js/g'     $D/web/viewer.html $D/web/viewer.js $D/build/pdf.js $D/build/pdf.worker.js
# 2026-11-11 21:55: the reader on a TV (pages/pdf-rn.css: its bar shown larger, the tools this browser has no use
# for left out), and its "automatic" size up to 1.75 instead of 1.25 (a page of text is then read from the sofa).
cp "$(dirname "$0")/pages/pdf-rn.css" $D/web/browserps5.css
grep -q '</head>' $D/web/viewer.html || { echo "viewer.html: no </head>"; exit 1; }
sed -i 's|</head>|<link rel="stylesheet" href="browserps5.css"></head>|' $D/web/viewer.html
grep -q 'inlined export .MAX_AUTO_SCALE \*/1.25)' $D/web/viewer.js || { echo "viewer.js: MAX_AUTO_SCALE is not where it was"; exit 1; }
sed -i 's|inlined export .MAX_AUTO_SCALE \*/1.25)|inlined export .MAX_AUTO_SCALE */1.75)|; s|const MAX_AUTO_SCALE = 1.25;|const MAX_AUTO_SCALE = 1.75;|' $D/web/viewer.js
grep -l '\.mjs' $D/web/viewer.html $D/web/viewer.js $D/build/pdf.js $D/build/pdf.worker.js | sed 's/^/still names a .mjs: /' || true
for d in images cmaps standard_fonts wasm iccs; do [ -d $src/web/$d ] && cp -r $src/web/$d $D/web/; done
cp $src/web/locale/locale.json $D/web/locale/
for l in en-US pt-PT pt-BR; do [ -d $src/web/locale/$l ] && cp -r $src/web/locale/$l $D/web/locale/; done
(cd $T && tar --format=ustar --owner=0 --group=0 -cf $out user/homebrew/browserps5-rn/pdfjs)
echo "pdfjs-rn.tar: $(tar -tf $out | grep -vc '/$') files, $(du -m $out | cut -f1) MiB, PDF.js $(grep -m1 -o 'pdfjsVersion = "[0-9.]*"' $src/build/pdf.mjs)"
rm -rf $T
