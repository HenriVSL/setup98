#!/bin/sh
# packtest.sh - Mac tests of pack98 (run by `make test`): pack-level errors (tests/packerr/*.ini, "; expect:" lines),
# the two examples packed, tested (-t), extracted (-x) and compared byte for byte with their sources, packing from
# another working folder, and the stub refusing a second payload.
# Usage: tests/packtest.sh <pack98> <stub.exe>
P="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; STUB="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; T=$(cd "$(dirname "$0")" && pwd); K="$T/.."; O="$K/out/test"; fail=0; n=0
rm -rf "$O"; mkdir -p "$O"
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
for ex in retrostrike:../sandline/release/RetroStrike pica:../editor/release/Pica-1.0; do
  n=$((n+1)); name=${ex%%:*}; src="$K/${ex#*:}"
  "$P" "$K/examples/$name.ini" -stub "$STUB" -o "$O/$name.exe" -q && "$P" -t "$O/$name.exe" -q && "$P" -x "$O/$name.exe" "$O/$name" -q \
    && rm "$O/$name/manifest.ini" && diff -r "$O/$name" "$src" >/dev/null || { echo "FAIL example $name round trip"; fail=$((fail+1)); }
done
n=$((n+1)); "$P" "$K/examples/pica.ini" -stub "$O/pica.exe" -o "$O/x.exe" -q 2>/dev/null && { echo "FAIL: stub with payload accepted"; fail=$((fail+1)); }
# a damaged setup is refused (-t), truncated too
n=$((n+1)); cp "$O/pica.exe" "$O/bad.exe"; printf 'X' | dd of="$O/bad.exe" bs=1 seek=200000 conv=notrunc 2>/dev/null
"$P" -t "$O/bad.exe" -q 2>/dev/null && { echo "FAIL: damaged setup accepted"; fail=$((fail+1)); }
n=$((n+1)); head -c 300000 "$O/pica.exe" > "$O/trunc.exe"; "$P" -t "$O/trunc.exe" -q 2>/dev/null && { echo "FAIL: truncated setup accepted"; fail=$((fail+1)); }
echo "packtest: $n checks, $fail failed"
[ $fail -eq 0 ]
