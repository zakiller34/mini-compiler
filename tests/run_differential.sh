#!/usr/bin/env bash
#
# Differential test: for every .mc program, the interpreter and the compiled
# binary must agree.
#
# For statically-typed programs the two paths share only the lexer, parser and
# type checker; everything after that is independent, so disagreement localises
# the fault to one of the twenty compiler passes. Under --dyn (the phase8 corpus)
# they additionally share shrink, uniquify, reveal_functions and cast_insert, so
# a fault inside those four cannot be localised this way — see main.cpp.
#
# This is how `tests/programs/phase6/two_args.mc` was found to miscompile —
# `add(20, 22)` returned 40 for the entire life of Phase 6, through two
# subsequent phases and a full unit-test suite, because nothing ever compared
# the two execution paths across every program.
#
# Usage: tests/run_differential.sh [build-dir]      (default: build)

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD="${1:-build}"
MC="$BUILD/src/mc"
RUNTIME="$BUILD/src/libmc_runtime.a"
TRAPPED_ERROR=255

if [ ! -x "$MC" ]; then
    echo "error: $MC not found — build first" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0

for f in tests/programs/phase*/*.mc; do
    # Phase 8 programs are written in the annotation-free dynamic language.
    dyn=""
    case "$f" in *phase8*) dyn="--dyn" ;; esac
    stdin_file="${f%.mc}.input"
    [ -f "$stdin_file" ] || stdin_file=/dev/null

    if ! "$MC" $dyn "$f" -o "$tmp/o.s" 2>"$tmp/err"; then
        echo "FAIL $f: compile error"; sed 's/^/      /' "$tmp/err"; fail=$((fail+1)); continue
    fi
    if ! gcc -o "$tmp/o" "$tmp/o.s" "$RUNTIME" -lm 2>"$tmp/err"; then
        echo "FAIL $f: assemble/link error"; sed 's/^/      /' "$tmp/err"; fail=$((fail+1)); continue
    fi

    "$tmp/o" <"$stdin_file" >/dev/null 2>&1
    compiled=$?

    interp_out=$("$MC" -i $dyn "$f" <"$stdin_file" 2>/dev/null)
    interp_status=$?

    # A trapped dynamic type error exits 255 on both paths and prints no value.
    if [ "$interp_status" -eq "$TRAPPED_ERROR" ]; then
        expected=$TRAPPED_ERROR
    else
        value=$(printf '%s' "$interp_out" | tail -1)
        case "$value" in
            ''|*[!0-9-]*) echo "FAIL $f: interpreter printed non-numeric '$value'"; fail=$((fail+1)); continue ;;
        esac
        # The process exit status is the value truncated to a byte.
        expected=$(( (value % 256 + 256) % 256 ))
    fi

    if [ "$compiled" -eq "$expected" ]; then
        pass=$((pass+1))
    else
        echo "FAIL $f: compiled exited $compiled, interpreter implies $expected"
        fail=$((fail+1))
    fi
done

echo "differential: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
