#!/bin/sh
# MicroCS test runner: runs every tests/*.cs from source, as a compiled
# bytecode image (.mcsb) and as the same image executed in place (--xip),
# comparing stdout+stderr with tests/*.out.
MCS=${1:-./mcs}
case "$MCS" in /*) ;; *) MCS="$(pwd)/$MCS" ;; esac
cd "$(dirname "$0")" || exit 1
TMP=${TMPDIR:-/tmp}/mcs_tests.$$
mkdir -p "$TMP"
pass=0; fail=0
# per-test CLI options: a first line of the form  // args: --sim --ramfs 65536
test_args() { head -n 1 "$1" | sed -n 's|^// args: *||p'; }
for t in t*.cs; do
    base=${t%.cs}
    exp=$base.out
    [ -f "$exp" ] || { echo "SKIP $t (no $exp)"; continue; }
    opts=$(test_args "$t")
    case "$base" in *gc_stress*) opts="--heap 196608 --stack 256" ;; esac
    "$MCS" $opts "$t" > "$TMP/src.txt" 2>&1
    if cmp -s "$TMP/src.txt" "$exp"; then pass=$((pass+1)); echo "PASS $t";
    else fail=$((fail+1)); echo "FAIL $t"; diff "$TMP/src.txt" "$exp" | head -10; fi
    # bytecode image round trip (compile, then run the image)
    if "$MCS" -c "$t" -o "$TMP/$base.mcsb" 2>/dev/null; then
        "$MCS" $opts "$TMP/$base.mcsb" > "$TMP/img.txt" 2>&1
        if cmp -s "$TMP/img.txt" "$exp"; then pass=$((pass+1)); echo "PASS $t (image)";
        else fail=$((fail+1)); echo "FAIL $t (image)"; diff "$TMP/img.txt" "$exp" | head -10; fi
        # same image executed in place (mcs_exec_image_xip)
        "$MCS" --xip $opts "$TMP/$base.mcsb" > "$TMP/xip.txt" 2>&1
        if cmp -s "$TMP/xip.txt" "$exp"; then pass=$((pass+1)); echo "PASS $t (xip)";
        else fail=$((fail+1)); echo "FAIL $t (xip)"; diff "$TMP/xip.txt" "$exp" | head -10; fi
    else fail=$((fail+1)); echo "FAIL $t (compile)"; fi
done
# compile-error diagnostics
for t in err_*.cs; do
    [ -f "$t" ] || continue
    exp=${t%.cs}.out
    "$MCS" $(test_args "$t") "$t" > "$TMP/err.txt" 2>&1
    if cmp -s "$TMP/err.txt" "$exp"; then pass=$((pass+1)); echo "PASS $t"; else fail=$((fail+1)); echo "FAIL $t"; diff "$TMP/err.txt" "$exp" | head -5; fi
done
rm -rf "$TMP"
echo "----"
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
