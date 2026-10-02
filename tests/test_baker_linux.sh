#!/usr/bin/env bash
# Linux baker integration, including output locking and commit rollback.
# The test_bake_output executable must be alongside the baker executable.
set -euo pipefail
shopt -s nullglob

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 BAKER TEST_FONT OUTPUT_DIR" >&2
    exit 2
fi

baker=$(realpath -- "$1")
font=$(realpath -- "$2")
validator="$(dirname -- "$baker")/test_bake_output"
if [[ ! -x "$baker" || ! -x "$validator" || ! -f "$font" ]]; then
    echo 'The baker, adjacent test_bake_output executable, and test font are required.' >&2
    exit 2
fi
mkdir -p -- "$3"
work=$(mktemp -d -- "$3/baker.XXXXXXXX")
work=$(realpath -- "$work")
printf 'Baker integration artifacts: %s\n' "$work"

fail() {
    printf 'FAIL: %s\nArtifacts: %s\n' "$1" "$work" >&2
    exit 1
}

expect_failure() {
    local log=$1 expected=$2 status=0
    shift 2
    "$@" > "$log.stdout" 2> "$log" || status=$?
    if [[ $status -ne 1 ]] || ! grep -Fq -- "$expected" "$log"; then
        cat -- "$log" >&2
        fail "Expected exit 1 and diagnostic '$expected'; got exit $status"
    fi
}

assert_clean() {
    local base=$1
    local leftovers=( "$base".rgba.tmp.* "$base".font.tmp.* "$base".rgba.rollback.* )
    if [[ ${#leftovers[@]} -ne 0 || -e "$base.rg_text_bake.lock" ]]; then
        fail "Baker left temporary files or its lock for $base"
    fi
}

for case_name in first second no-kerning wide truncated locked preserved rollback rollback-new; do
    mkdir -- "$work/$case_name"
done

"$baker" --self-test
first="$work/first/inter_medium_16"
second="$work/second/inter_medium_16"
no_kerning="$work/no-kerning/inter_medium_16"
wide="$work/wide/inter_medium_16"
"$baker" "$font" "$first" 16 32-126 1 256
"$baker" "$font" "$second" 16 32-126 1 256
"$baker" --no-kerning "$font" "$no_kerning" 16 32-126 1 256
expect_failure "$work/wide/rejected.stderr" \
    'Kerning extraction is limited to 512 supported glyphs' \
    "$baker" "$font" "$work/wide/rejected" 16 32-2047 1 512
[[ ! -e "$work/wide/rejected.font" && ! -e "$work/wide/rejected.rgba" ]] ||
    fail 'Rejected kerning workload created final outputs'
assert_clean "$work/wide/rejected"
"$baker" --no-kerning "$font" "$wide" 16 32-2047 1 512
"$validator" "$first"
"$validator" "$no_kerning" --no-kerning
"$validator" "$first" --compare-no-kerning "$no_kerning"
"$validator" "$wide" --no-kerning-large
cmp -- "$first.font" "$second.font"
cmp -- "$first.rgba" "$second.rgba"
for base in "$first" "$second" "$no_kerning" "$wide"; do
    assert_clean "$base"
done

cp -- "$first.font" "$work/truncated/inter_medium_16.font"
atlas_size=$(stat --format='%s' -- "$first.rgba")
head --bytes="$((atlas_size - 1))" -- "$first.rgba" > "$work/truncated/inter_medium_16.rgba"
"$validator" "$work/truncated/inter_medium_16" --expect-invalid-atlas

# Hold the exact lock the baker uses; no timing race or background bake is needed.
locked="$work/locked/inter_medium_16"
cp -- "$first.font" "$locked.font"
cp -- "$first.rgba" "$locked.rgba"
exec {lock_fd}> "$locked.rg_text_bake.lock"
flock --exclusive --nonblock "$lock_fd"
expect_failure "$work/locked/rejected.stderr" 'Failed to acquire the output lock' \
    "$baker" "$font" "$locked" 16 32-126 1 256
cmp -- "$first.font" "$locked.font"
cmp -- "$first.rgba" "$locked.rgba"
[[ -f "$locked.rg_text_bake.lock" ]] || fail "Contending baker removed another writer's lock"
if flock --exclusive --nonblock "$locked.rg_text_bake.lock" true; then
    fail "Contending baker released another writer's lock"
fi
flock --unlock "$lock_fd"
exec {lock_fd}>&-
rm -- "$locked.rg_text_bake.lock"
assert_clean "$locked"
"$baker" "$font" "$locked" 16 32-126 1 256
cmp -- "$first.font" "$locked.font"
cmp -- "$first.rgba" "$locked.rgba"
assert_clean "$locked"

# Opening an invalid font fails after lock acquisition and temporary-file creation.
preserved="$work/preserved/inter_medium_16"
cp -- "$first.font" "$preserved.font"
cp -- "$first.rgba" "$preserved.rgba"
printf 'invalid font\n' > "$work/preserved/invalid.ttf"
expect_failure "$work/preserved/rejected.stderr" 'Failed to open TrueType/OpenType font' \
    "$baker" "$work/preserved/invalid.ttf" "$preserved" 16 32-126 1 256
cmp -- "$first.font" "$preserved.font"
cmp -- "$first.rgba" "$preserved.rgba"
assert_clean "$preserved"

# A metrics directory allows atlas replacement but forces the second rename to fail.
# Distinct old atlas bytes ensure this detects a missing rollback, even for the same font.
rollback="$work/rollback/inter_medium_16"
printf 'previous atlas bytes\n' > "$work/rollback/previous.rgba"
cp -- "$work/rollback/previous.rgba" "$rollback.rgba"
mkdir -- "$rollback.font"
printf 'directory marker\n' > "$rollback.font/marker"
expect_failure "$work/rollback/rejected.stderr" 'Failed to replace metrics' \
    "$baker" "$font" "$rollback" 16 32-126 1 256
cmp -- "$work/rollback/previous.rgba" "$rollback.rgba"
[[ -f "$rollback.font/marker" ]] || fail 'Failed metrics replacement modified the existing directory'
assert_clean "$rollback"

# The same commit failure must remove the new atlas when no prior atlas existed.
rollback_new="$work/rollback-new/inter_medium_16"
mkdir -- "$rollback_new.font"
expect_failure "$work/rollback-new/rejected.stderr" 'Failed to replace metrics' \
    "$baker" "$font" "$rollback_new" 16 32-126 1 256
[[ ! -e "$rollback_new.rgba" && -d "$rollback_new.font" ]] ||
    fail 'Rollback did not restore the absence of a prior atlas'
assert_clean "$rollback_new"

echo 'rg_text Linux baker integration tests passed'
