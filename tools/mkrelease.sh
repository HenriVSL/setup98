#!/bin/sh
# mkrelease.sh - build the binary kit out/setup98-<version>.zip (version from src/version.h):
#   setup98-<version>/bin/setup98.exe, bin/pack98.exe, bin/s98test.exe   (the committed VC6 builds)
#   setup98-<version>/README.TXT (docs/README.TXT, for Win98 users), README.md, LICENSE, CHANGES
#   setup98-<version>/examples/...   demo (program, data, source), hello, and the real-world manifests
# Deterministic: files in sorted order, every entry dated from the newest CHANGES heading, fixed permissions,
# text files with CRLF line ends (Win98 Notepad needs them). Only files git tracks (or would track) go in
# (outside a git checkout: every file in examples/ except build output).
# Usage: tools/mkrelease.sh [-notest]      (runs `make test` first unless -notest; needs python3, make)
set -e
K=$(cd "$(dirname "$0")/.." && pwd)
cd "$K"
VER=$(sed -n 's/^#define S98_VERSION_STR "\(.*\)"/\1/p' src/version.h)
[ -n "$VER" ] || { echo "mkrelease: no version in src/version.h"; exit 1; }
DATE=$(sed -n 's/^\([0-9][0-9.]*\) (\([0-9-]*\))$/\2/p' CHANGES | head -1)
TOP=$(sed -n 's/^\([0-9][0-9.]*\) (\([0-9-]*\))$/\1/p' CHANGES | head -1)
[ "$TOP" = "$VER" ] || { echo "mkrelease: CHANGES starts with $TOP, src/version.h says $VER"; exit 1; }
for f in bin/setup98.exe bin/pack98.exe bin/s98test.exe examples/demo/s98demo.exe; do
  [ -f "$f" ] || { echo "mkrelease: $f is missing (build it with VC6: nmake)"; exit 1; }
done
[ "$1" = "-notest" ] || make -s test
OUT="out/setup98-$VER.zip"
mkdir -p out
{ echo bin/setup98.exe; echo bin/pack98.exe; echo bin/s98test.exe; echo README.md; echo LICENSE; echo CHANGES
  git ls-files -co --exclude-standard examples 2>/dev/null \
    || find examples -type f ! -name '*.obj' ! -name '*.res' ! -name '.*'; } > out/.mkrelease.lst
python3 - "$OUT" "setup98-$VER" "$VER" "$DATE" out/.mkrelease.lst <<'EOF'
import sys, zipfile
out, top, ver, date, lst = sys.argv[1:6]
y, m, d = (int(x) for x in date.split("-"))
stamp = (y, m, d, 12, 0, 0)
TEXT_EXT = (".md", ".txt", ".ini", ".rc", ".cpp", ".h", ".py", ".greet")
TEXT_NAMES = ("LICENSE", "CHANGES", "Makefile")
def crlf(b):
    return b.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
names = sorted(set(l.strip() for l in open(lst) if l.strip()))
entries = []
for n in names:
    data = open(n, "rb").read()
    base = n.rsplit("/", 1)[-1]
    if base.lower().endswith(TEXT_EXT) or base in TEXT_NAMES:
        data = crlf(data)
    entries.append((top + "/" + n, data))
# the Win98 readme, with the version filled in and its title underline fitted
txt = open("docs/README.TXT", "rb").read().decode("ascii").replace("\r\n", "\n").replace("@VERSION@", ver)
lines = txt.split("\n")
lines[1] = "=" * len(lines[0])
entries.append((top + "/README.TXT", crlf("\n".join(lines).encode("ascii"))))
entries.sort()
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name, data in entries:
        zi = zipfile.ZipInfo(name, stamp)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.create_system = 0                 # MS-DOS: no Unix permission bits
        zi.external_attr = 0x20              # archive bit
        z.writestr(zi, data, compresslevel=9)
print("%s: %d files" % (out, len(entries)))
EOF
rm -f out/.mkrelease.lst
ls -l "$OUT" | awk '{print $5" bytes"}'
