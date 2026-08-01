#!/usr/bin/env bash
# Copyright (c) 2026 Shantanu Agarwal
# SPDX-License-Identifier: AGPL-3.0-only
#
# Answers one question: are the errors VS Code is showing me REAL?
#
# clangd in an editor can be wrong in ways the compiler never is — a stale
# index, a compile_commands.json that predates a rename, a file with no entry
# in any database at all. When that happens it does not fail quietly; it
# reports a wall of confident, specific, completely fictional errors
# ("'memory' file not found", "no member named 'bench' in namespace 'vc'").
#
# This runs the SAME clangd the editor runs, headlessly, over every
# translation unit in the tree, and prints only what actually fails. Clean
# output means the tree is fine and the editor needs a restart
# (Command Palette -> "clangd: Restart language server"). Failures here are
# real and will fail the build too.
#
# Usage:  scripts/check_clangd.sh [file ...]     (no args = every TU)
set -uo pipefail

cd "$(dirname "$0")/.."
readonly ROOT="$PWD"
readonly DEBUG_DB="build/debug"
readonly BENCH_DB="build/bench"

CLANGD="${CLANGD:-$(command -v clangd || true)}"
if [[ -z "$CLANGD" ]]; then
    echo "error: no clangd on PATH. VS Code's clangd extension uses the same" >&2
    echo "       binary; set CLANGD=/path/to/clangd to override." >&2
    exit 1
fi

# Both databases must exist, or a "clean" run would just be a run over
# nothing — the silently-green failure this script exists to catch.
missing=0
for db in "$DEBUG_DB" "$BENCH_DB"; do
    if [[ ! -f "$db/compile_commands.json" ]]; then
        echo "error: $db/compile_commands.json missing. Create it with:" >&2
        echo "       cmake --preset ${db#build/}" >&2
        missing=1
    fi
done
[[ $missing -eq 0 ]] || exit 1

# Collect the files to check: every entry in the debug database, plus the
# bench sources, which are Release-only and so appear in neither.
declare -a FILES
if [[ $# -gt 0 ]]; then
    for f in "$@"; do FILES+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"); done
else
    while IFS= read -r line; do FILES+=("$line"); done < <(
        python3 -c "
import json, sys
with open('$DEBUG_DB/compile_commands.json') as fh:
    for e in json.load(fh):
        print(e['file'])
"
    )
    for f in "$ROOT"/bench/*.cpp; do [[ -e "$f" ]] && FILES+=("$f"); done
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "error: no files to check — is $DEBUG_DB/compile_commands.json empty?" >&2
    exit 1
fi

echo "clangd: $("$CLANGD" --version | head -1)"
echo "checking ${#FILES[@]} translation units..."

failed=0
for f in "${FILES[@]}"; do
    out=$("$CLANGD" --check="$f" --check-locations=0 2>&1)
    # Parse the summary line rather than trusting the exit code, which stays 0
    # for a TU with diagnostics. A MISSING summary means clangd itself fell
    # over (no compile command, crash) — that is a failure, not a pass.
    if [[ "$out" =~ All\ checks\ completed,\ ([0-9]+)\ error ]]; then
        n="${BASH_REMATCH[1]}"
        [[ "$n" == "0" ]] && continue
        echo
        echo "FAIL (${n} errors): ${f#"$ROOT"/}"
        grep -E "^E\[|error:" <<<"$out" | head -8
    else
        echo
        echo "FAIL (clangd produced no result): ${f#"$ROOT"/}"
        tail -4 <<<"$out"
    fi
    failed=$((failed + 1))
done

echo
if [[ $failed -eq 0 ]]; then
    echo "OK — ${#FILES[@]} translation units, 0 errors."
    echo "Anything VS Code is still showing is stale: restart clangd"
    echo "(Command Palette -> 'clangd: Restart language server')."
else
    echo "$failed of ${#FILES[@]} translation units FAILED — these are real."
fi
exit $((failed > 0))
