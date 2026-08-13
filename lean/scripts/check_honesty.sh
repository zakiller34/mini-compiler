#!/usr/bin/env bash
#
# Fast, build-free proof-hygiene gate.
#
# This is a pre-commit convenience, NOT the authority. grep cannot see through
# elaboration: `abbrev Trivial := True` followed by `theorem foo : Trivial`
# would sail straight past every pattern below. lean/Hygiene.lean is the real
# check — it inspects elaborated terms and runs as part of `lake build`.
#
# What this catches in exchange for running in milliseconds:
#   1. theorems whose conclusion is literally `True`
#   2. native_decide (adds the ofReduceBool axiom)
#   3. sorry count above the recorded baseline
#   4. a module under MiniCompiler/ that MiniCompiler.lean does not import
#
# (4) is the one that matters historically: RevealFunctions.lean failed to
# compile for an entire release cycle because nothing imported it, so `lake
# build` never elaborated it and stayed green.

set -euo pipefail
cd "$(dirname "$0")/.."

fail=0

# Code only: drop `--` comment lines and backtick-quoted spans. Without this the
# checks below fire on this repository's own prose about vacuous theorems, which
# is a neat illustration of why Hygiene.lean and not grep is the authority.
code_lines() {
    grep -rn --include='*.lean' '' MiniCompiler MiniCompiler.lean \
        | sed 's/`[^`]*`//g' \
        | grep -vE '^[^:]*:[0-9]+:[[:space:]]*--'
}

if code_lines | grep -E ':[[:space:]]*True[[:space:]]*:=|,[[:space:]]*True[[:space:]]*:=|→[[:space:]]*True[[:space:]]*:=|∨[[:space:]]*True'; then
    echo "FAIL: theorem with a trivially-true conclusion (see above)"
    fail=1
fi

if code_lines | grep 'native_decide'; then
    echo "FAIL: native_decide adds the ofReduceBool axiom"
    fail=1
fi

# Count `sorry` in term position only. A plain `\bsorry\b` also matches the
# word in docstrings and comments, which is exactly where this repository now
# discusses its own proof debt at length.
n=$(grep -rn --include='*.lean' -E '(:=[[:space:]]*sorry[[:space:]]*$|^[[:space:]]*sorry[[:space:]]*$)' MiniCompiler \
    | grep -vcE ':[0-9]+:[[:space:]]*--' || true)
base=$(cat SORRY_BASELINE)
if [ "$n" -gt "$base" ]; then
    echo "FAIL: sorry count $n exceeds baseline $base"
    echo "      State the proposition and add the name to sorryAllowlist in Hygiene.lean."
    fail=1
elif [ "$n" -lt "$base" ]; then
    echo "NOTE: sorry count dropped to $n — lower SORRY_BASELINE to lock it in."
fi

while IFS= read -r f; do
    m=${f%.lean}
    m=${m//\//.}
    if ! grep -q "^import ${m}\$" MiniCompiler.lean; then
        echo "FAIL: $m is not imported by MiniCompiler.lean"
        fail=1
    fi
done < <(find MiniCompiler -name '*.lean' | sort)

if [ "$fail" -eq 0 ]; then
    echo "proof hygiene (grep pass): ok — ${n} sorry, baseline ${base}"
fi
exit "$fail"
