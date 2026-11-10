#!/bin/sh
# packtest.sh - pack98 tests (run by `make test`, any POSIX shell): pack-level errors (tests/packerr/*.ini,
# "; expect:" lines), the self-contained packages (the generated sample, the demo, hello) packed, tested (-t),
# extracted (-x) and compared byte for byte with their sources, packing from another working folder, the stub
# refusing a second payload, damaged and truncated setups refused.
# The real-world examples (retrostrike.ini, pica.ini) need their release folders next to this repository
# (../sandline, ../editor); they are packed and compared too when those folders exist, and skipped otherwise.
# Usage: tests/packtest.sh <pack98> <stub.exe>     (the sample must exist: obj/mac/sample, from tests/gensample)
P="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; STUB="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
T=$(cd "$(dirname "$0")" && pwd); K=$(cd "$T/.." && pwd); O="$K/out/test"; fail=0; n=0; skipped=""
rm -rf "$O"; mkdir -p "$O"
[ -f "$K/obj/mac/sample/GAME.EXE" ] || { echo "packtest: obj/mac/sample is missing (make test generates it)"; exit 1; }
for f in "$T"/packerr/*.ini; do
  n=$((n+1)); exp=$(sed -n '1s/^; expect: //p' "$f" | tr -d '\r')
  out=$("$P" "$f" -stub "$STUB" -o "$O/pk.exe" -q 2>&1); rc=$?
  if [ "$exp" = ok ]; then [ $rc -eq 0 ] || { echo "FAIL $f: $out"; fail=$((fail+1)); }
  else [ $rc -ne 0 ] && echo "$out" | grep -qF "$exp" || { echo "FAIL $f: expected '$exp', got rc $rc: $out"; fail=$((fail+1)); }; fi
done
# cross-folder manifest packed from an unrelated working folder: paths are relative to the manifest
n=$((n+1)); (cd / && "$P" "$T/packerr/ok_crossdir.ini" -stub "$STUB" -o "$O/cross.exe" -q) && "$P" -x "$O/cross.exe" "$O/cross" -q \
  && cmp -s "$O/cross/docs/READ.TXT" "$T/data/pk/README.TXT" && cmp -s "$O/cross/sub/a.dat" "$T/data/pk/sub/a.dat" \
  || { echo "FAIL cross-folder pack"; fail=$((fail+1)); }

# roundtrip <name> <manifest> <source folder> <exact>: pack, test, extract; every extracted file must equal its
# source; with exact=1 the extracted tree must equal the whole source folder (diff -r)
roundtrip() {
  n=$((n+1)); r_ok=1
  "$P" "$2" -stub "$STUB" -o "$O/$1.exe" -q && "$P" -t "$O/$1.exe" -q && "$P" -x "$O/$1.exe" "$O/$1" -q \
    && rm "$O/$1/manifest.ini" || r_ok=0
  if [ $r_ok = 1 ] && [ "$4" = 1 ]; then diff -r "$O/$1" "$3" >/dev/null || r_ok=0; fi
  if [ $r_ok = 1 ]; then
    cnt=0; (cd "$O/$1" && find . -type f) > "$O/$1.files"
    while IFS= read -r g; do
      cnt=$((cnt+1)); cmp -s "$O/$1/$g" "$3/$g" || { echo "  $1: $g differs"; r_ok=0; }
    done < "$O/$1.files"
    [ $cnt -gt 0 ] || r_ok=0
  fi
  [ $r_ok = 1 ] || { echo "FAIL $1 round trip"; fail=$((fail+1)); }
}
# the sample ships everything but UserData; game.ini is a Keep file and is shipped too
roundtrip sample "$T/sample.ini" "$K/obj/mac/sample" 1
roundtrip demo "$K/examples/demo.ini" "$K/examples/demo" 0
roundtrip hello "$K/examples/hello.ini" "$K/examples/demo" 0
n=$((n+1)); [ -f "$O/demo/s98demo.ini" ] && [ -f "$O/demo/data/logo.bmp" ] && [ ! -d "$O/demo/src" ] \
  && [ "$(cd "$O/hello" && find . -type f)" = "./s98demo.exe" ] || { echo "FAIL demo/hello file sets"; fail=$((fail+1)); }
# the sample's file list as the setup records it: kinds and the Keep flag
n=$((n+1)); "$P" -l "$O/sample.exe" > "$O/sample.lst" 2>&1 && grep -q "game.ini" "$O/sample.lst" \
  && grep -q "data.empty.dat" "$O/sample.lst" && grep -q "A Long File Name.txt" "$O/sample.lst" \
  || { echo "FAIL sample listing"; cat "$O/sample.lst"; fail=$((fail+1)); }
# the canonical manifest inside the demo setup (-m) carries the [Types] and UserData lines
n=$((n+1)); "$P" -m "$O/demo.exe" | grep -q "s98demo.sav" || { echo "FAIL demo -m"; fail=$((fail+1)); }

# real-world examples (optional)
for ex in retrostrike:../sandline/release/RetroStrike pica:../editor/release/Pica-1.0; do
  name=${ex%%:*}; src="$K/${ex#*:}"
  if [ -d "$src" ]; then roundtrip "$name" "$K/examples/$name.ini" "$src" 1
  else skipped="$skipped $name"; fi
done

n=$((n+1)); "$P" "$K/examples/demo.ini" -stub "$O/sample.exe" -o "$O/x.exe" -q 2>/dev/null && { echo "FAIL: stub with payload accepted"; fail=$((fail+1)); }
# a damaged setup is refused (-t), truncated too
size=$(wc -c < "$O/sample.exe" | tr -d ' ')
n=$((n+1)); cp "$O/sample.exe" "$O/bad.exe"; printf 'X' | dd of="$O/bad.exe" bs=1 seek=$((size - 50000)) conv=notrunc 2>/dev/null
"$P" -t "$O/bad.exe" -q 2>/dev/null && { echo "FAIL: damaged setup accepted"; fail=$((fail+1)); }
n=$((n+1)); head -c $((size - 30000)) "$O/sample.exe" > "$O/trunc.exe"; "$P" -t "$O/trunc.exe" -q 2>/dev/null && { echo "FAIL: truncated setup accepted"; fail=$((fail+1)); }
n=$((n+1)); head -c $((size - 7)) "$O/sample.exe" > "$O/trunc2.exe"; "$P" -t "$O/trunc2.exe" -q 2>/dev/null && { echo "FAIL: setup without trailer accepted"; fail=$((fail+1)); }
[ -n "$skipped" ] && echo "packtest: skipped real-world examples (release folders not present):$skipped"
echo "packtest: $n checks, $fail failed"
[ $fail -eq 0 ]
