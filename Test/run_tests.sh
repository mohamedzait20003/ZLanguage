#!/usr/bin/env bash
#
# Z language test runner.
#
#   Test/run_tests.sh [filter]
#
# Suites:
#   Test/codegen/*.z  + .expected        compile, run, compare stdout
#   Test/sema/*.z     + .expected-error  must FAIL to compile, and the
#                                        diagnostic must contain the expected text
#
# `filter` is an optional substring; only matching test names run.
# Exits non-zero if any test fails, so it is usable as a CI gate.
#
# Environment:
#   ZOPT="-O0 -O2"   optimisation levels to run the codegen suite at.
#                    Every level must produce byte-identical output — that
#                    equivalence is the correctness check on the pass
#                    pipeline. Defaults to all four levels.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$ROOT/build/testwork"

ZC="$ROOT/build/zc.exe"
[ -x "$ZC" ] || ZC="$ROOT/build/zc"

if [ ! -x "$ZC" ]; then
    echo "error: compiler not found at $ZC"
    echo "build it first:  cmake --build build"
    exit 1
fi

if [ -f "$ROOT/build/llvm_bin_dir.txt" ]; then
    LLVM_BIN="$(tr -d '\r\n' < "$ROOT/build/llvm_bin_dir.txt")"

    if [ -n "$LLVM_BIN" ]; then
        if command -v cygpath >/dev/null 2>&1; then
            LLVM_BIN="$(cygpath -u "$LLVM_BIN")"
        fi
        PATH="$LLVM_BIN:$PATH"
        export PATH
    fi
fi

if ! "$ZC" --dump-tokens "$ROOT/Test/codegen/m0_hello.z" >/dev/null 2>&1; then
    echo "error: $ZC cannot run (missing shared libraries?)"
    echo "if zc links the shared libLLVM, LLVM's bin directory must be on PATH"
    exit 1
fi

FILTER="${1:-}"
OPT_LEVELS="${ZOPT:--O0 -O1 -O2 -O3}"

pass=0
fail=0
skip=0
failures=()

rm -rf "$WORK"
mkdir -p "$WORK"

note_pass() {
    pass=$((pass + 1))
    printf '  ok    %s\n' "$1"
}

note_fail() {
    fail=$((fail + 1))
    failures+=("$1")
    printf '  FAIL  %s\n' "$1"
    printf '%s\n' "$2" | sed 's/^/          /'
}

# Compile and run at one optimisation level; stdout must match .expected.
run_at_opt() {
    local src="$1" expected="$2" name="$3" optflag="$4"
    local exe="$WORK/$(basename "${src%.z}")${optflag}.exe"

    if ! "$ZC" "$optflag" "$src" -o "$exe" >"$WORK/compile.log" 2>&1; then
        note_fail "$name $optflag" "compilation failed:
$(cat "$WORK/compile.log")"
        return
    fi

    # Compiled programs emit CRLF on Windows; .expected files are stored as LF.
    # Normalise both so the suite behaves the same on either platform.
    local actual
    actual="$("$exe" 2>&1 | tr -d '\r')"

    local want
    want="$(tr -d '\r' < "$expected")"

    if [ "$actual" = "$want" ]; then
        note_pass "$name $optflag"
    else
        note_fail "$name $optflag" "output mismatch (- want, + got):
$(diff <(printf '%s\n' "$want") <(printf '%s\n' "$actual") | head -30)"
    fi
}

# A codegen test runs once per optimisation level. Identical output across all
# of them is what proves the pass pipeline is semantics-preserving for this
# program; a mismatch at one level only means the emitted IR relied on
# something the optimiser is entitled to change.
run_output_test() {
    local src="$1" expected="$2" name="$3"

    for optflag in $OPT_LEVELS; do
        run_at_opt "$src" "$expected" "$name" "$optflag"
    done
}

# Compilation must fail, and the message must contain the expected text.
# Substring rather than exact match: this pins which diagnostic fired without
# making every wording tweak a test failure.
run_error_test() {
    local src="$1" expected="$2" name="$3"
    local out

    if out="$("$ZC" "$src" -o "$WORK/unexpected.exe" 2>&1)"; then
        note_fail "$name" "expected compilation to fail, but it succeeded"
        return
    fi

    out="$(printf '%s' "$out" | tr -d '\r')"

    local want
    want="$(tr -d '\r' < "$expected")"

    if printf '%s' "$out" | grep -qF -- "$want"; then
        note_pass "$name"
    else
        note_fail "$name" "wrong diagnostic
expected to contain: $want
actual:              $out"
    fi
}

# suite <label> <directory> <expected-extension> <runner>
suite() {
    local label="$1" dir="$2" ext="$3" runner="$4"
    local found=0

    printf '\n%s\n' "$label"

    for src in "$ROOT/$dir"/*.z; do
        [ -e "$src" ] || continue

        local name
        name="$(basename "${src%.z}")"

        if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
            continue
        fi

        found=1
        local expected="${src%.z}.$ext"

        if [ ! -f "$expected" ]; then
            skip=$((skip + 1))
            printf '  skip  %s (no %s)\n' "$name" "$(basename "$expected")"
            continue
        fi

        "$runner" "$src" "$expected" "$name"
    done

    [ "$found" -eq 0 ] && printf '  (none)\n'
    return 0
}

printf 'compiler:    %s\n' "$ZC"
printf 'opt levels:  %s\n' "$OPT_LEVELS"

suite "codegen (per optimisation level)" "Test/codegen" "expected"       run_output_test
suite "sema (must fail)"                 "Test/sema"    "expected-error" run_error_test

printf '\n----------------------------------------\n'
printf '%d passed, %d failed' "$pass" "$fail"
[ "$skip" -gt 0 ] && printf ', %d skipped' "$skip"
printf '\n'

if [ "$fail" -gt 0 ]; then
    printf '\nfailed:\n'
    for f in "${failures[@]}"; do
        printf '  %s\n' "$f"
    done
    exit 1
fi

exit 0
