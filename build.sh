#!/usr/bin/env bash
# Linux x86-64 build/test entry point. Run from any working directory.
set -euo pipefail

fail() { printf 'build.sh: %s\n' "$*" >&2; exit 1; }
caller_dir=$PWD
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
target=${1:-test}
if (($#)); then shift; fi

usage() {
    cat <<'EOF'
Usage: ./build.sh [target] [program arguments]

  test, test_text            Strict C99 runtime tests, assertions on and off
  test_ci                   CPU correctness, benchmark verification, GPU builds
  test_sanitize             ASan + UBSan runtime and C/SSE2 packing tests
  test_fuzz                 Two bounded libFuzzer runs (FUZZ_CC defaults to clang)
  test_baker                Baker self-test and Linux integration checks
  test_baker_sanitize       The same baker checks with ASan + UBSan
  test_gpu_compile          GPU CPU-side checks (no device)
  test_gpu_pack             Portable C packing tests
  test_gpu_pack_sse2        SSE2 packing tests
  test_gpu_device_build     Compile the GPU pixel-readback test
  test_gpu_device [backend] Compile shaders and run pixel readback (default vulkan)
  shaders                   Compile HLSL to SPIR-V using shadercross or DXC
  example_build             Compile the example
  example [--smoke-test]    Compile shaders, build, and run the example
  example_smoke             Run the example's offscreen rendering smoke test
  rg_text_bake              Build the baker and run its self-test
  bench, bench_build        Run/build layout benchmark (arguments forwarded)
  bench_gpu_pack            Run packing benchmark (arguments forwarded)
  bench_gpu_pack_build      Build packing benchmark (C and SSE2, no assembly)
  test_release              All correctness checks, including Vulkan and smoke

Environment:
  CC                        Compiler executable or path (default cc)
  RG_CORE_DIR               rg_core checkout (default ../rg_core)
  RG_TEXT_BUILD_DIR         Build root (default build/linux); adds compiler name
  CPPFLAGS CFLAGS LDFLAGS LDLIBS  Extra flags (quotes/backslashes supported)
  PKG_CONFIG                pkg-config executable (default pkg-config)
  PKG_CONFIG_PATH            SDL3 / FreeType / HarfBuzz package search path
  SHADERCROSS_EXE            shadercross executable or path
  DXC_EXE                   Direct DXC executable (used if SHADERCROSS_EXE unset)
  RG_TEXT_TEST_FONT         Inter Medium 4.1 TTF for baker integration checks
  RG_TEXT_GPU_USE_SSE2=1     Opt in for application/device builds
  FUZZ_CC                   Clang executable for test_fuzz (default clang)
  RG_TEXT_FUZZ_SECONDS       Duration per fuzzer (default 30)

Explicit targets fail when a dependency is missing. test_ci does not execute a
GPU device or compile shaders; test_release requires shadercross/DXC and a working
Vulkan/display environment. SDL_GPU_DRIVER defaults to vulkan for the example.
EOF
}

case "$target" in help|-h|--help) usage; exit 0 ;; esac
find_executable() {
    local requested=$1 found
    if [[ "$requested" == */* && "$requested" != /* ]]; then
        requested="$caller_dir/$requested"
    fi
    found=$(command -v -- "$requested") || return 1
    [[ "$found" == /* ]] || found="$caller_dir/$found"
    [[ -x "$found" && ! -d "$found" ]] || return 1
    printf '%s\n' "$found"
}
# Parse flag lists without eval, command substitution, or glob expansion. This
# also preserves the escaped spaces emitted by pkg-config for custom prefixes.
split_flags() {
    local -n result=$1
    local input=$2 word='' state=plain character following active=0 index
    result=()
    for ((index = 0; index < ${#input}; ++index)); do
        character=${input:index:1}
        case "$state" in
            single)
                if [[ "$character" == "'" ]]; then state=plain; else word+=$character; fi ;;
            double)
                case "$character" in
                    '"') state=plain ;;
                    '\')
                        following=${input:index+1:1}
                        case "$following" in
                            '$'|'`'|'"'|'\') word+=$following; ((index += 1)) ;;
                            *) word+=$character ;;
                        esac ;;
                    *) word+=$character ;;
                esac ;;
            plain)
                case "$character" in
                    "'") state=single; active=1 ;;
                    '"') state=double; active=1 ;;
                    '\')
                        ((index + 1 < ${#input})) || fail 'Trailing backslash in compiler/package flags.'
                        ((index += 1)); word+=${input:index:1}; active=1 ;;
                    [[:space:]])
                        if ((active)); then result+=("$word"); word=''; active=0; fi ;;
                    *) word+=$character; active=1 ;;
                esac ;;
        esac
    done
    [[ "$state" == plain ]] || fail 'Unmatched quote in compiler/package flags.'
    if ((active)); then result+=("$word"); fi
}
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || fail 'This runner currently supports Linux x86-64.'
[[ ${RG_TEXT_GPU_USE_ASM:-0} != 1 ]] || fail 'RG_TEXT_GPU_USE_ASM uses the Windows x64 ABI and is unsupported on Linux; use portable C or SSE2.'
compiler=$(find_executable "${CC:-cc}") || fail "Compiler not found: ${CC:-cc}"
[[ -f ${RG_CORE_DIR:-"$script_dir/../rg_core"}/src/rg_defs.h ]] || fail 'rg_core not found; set RG_CORE_DIR to the repository root.'
core_dir=$(cd -- "${RG_CORE_DIR:-"$script_dir/../rg_core"}" && pwd)
build_root=${RG_TEXT_BUILD_DIR:-"$script_dir/build/linux"}
mkdir -p -- "$build_root"
build_root=$(cd -- "$build_root" && pwd)
compiler_tag=${compiler##*/}
compiler_tag=${compiler_tag//[^[:alnum:]._-]/_}
out_dir="$build_root/$compiler_tag"
mkdir -p -- "$out_dir"
split_flags cppflags "${CPPFLAGS:-}"
split_flags cflags "${CFLAGS:-}"
split_flags ldflags "${LDFLAGS:-}"
split_flags ldlibs "${LDLIBS:-}"
common=(-std=c99 -pedantic-errors -O2 -Wall -Wextra -Werror -I "$core_dir/src")
sanitize=(-O1 -g -fno-omit-frame-pointer -fno-sanitize-recover=all -fsanitize=address,undefined)
app_pack=()
[[ ${RG_TEXT_GPU_USE_SSE2:-0} != 1 ]] || app_pack=(-DRG_TEXT_GPU_USE_SSE2=1)
package_cflags=()
package_libs=()
pkg_config=${PKG_CONFIG:-pkg-config}
if [[ -n ${RG_TEXT_TEST_FONT:-} && "$RG_TEXT_TEST_FONT" != /* ]]; then
    RG_TEXT_TEST_FONT="$caller_dir/$RG_TEXT_TEST_FONT"
fi
cd -- "$script_dir"

packages() {
    pkg_config=$(find_executable "$pkg_config") || fail "pkg-config not found: ${PKG_CONFIG:-pkg-config}"
    "$pkg_config" --exists "$@" || fail "Missing development packages: $* (check PKG_CONFIG_PATH)."
    local raw flags flag include_pending=0
    raw=$("$pkg_config" --cflags "$@")
    split_flags flags "$raw"
    package_cflags=()
    for flag in "${flags[@]}"; do
        if ((include_pending)); then
            package_cflags+=(-isystem "$flag")
            include_pending=0
        elif [[ "$flag" == -I ]]; then
            include_pending=1
        elif [[ "$flag" == -I* ]]; then
            package_cflags+=(-isystem "${flag#-I}")
        else
            package_cflags+=("$flag")
        fi
    done
    ((include_pending == 0)) || fail 'pkg-config returned -I without an include directory.'
    raw=$("$pkg_config" --libs "$@")
    split_flags package_libs "$raw"
}

compile() {
    local name=$1 source=$2
    shift 2
    printf 'Building %s (%s)\n' "$name" "${compiler##*/}"
    "$compiler" "${common[@]}" "${cppflags[@]}" "${cflags[@]}" \
        "${package_cflags[@]}" "$@" "$source" "${ldflags[@]}" \
        "${package_libs[@]}" "${ldlibs[@]}" -lm -o "$out_dir/$name"
}

sanitizer_environment() {
    export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:strict_string_checks=1:exitcode=86}
    export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1:exitcode=87}
}

test_text() {
    package_cflags=() package_libs=()
    compile test_text tests/test_text.c -UNDEBUG
    "$out_dir/test_text"
    compile test_text_release tests/test_text.c -DNDEBUG
    "$out_dir/test_text_release"
}

test_gpu_compile() {
    packages sdl3
    compile test_text_gpu tests/test_text_gpu.c "${app_pack[@]}" -UNDEBUG
    "$out_dir/test_text_gpu"
    compile test_text_gpu_release tests/test_text_gpu.c "${app_pack[@]}" -DNDEBUG
    "$out_dir/test_text_gpu_release"
}

test_gpu_pack() {
    packages sdl3
    local suffix=${1:-} defines=()
    [[ "$suffix" != _sse2 ]] || defines=(-DRG_TEXT_GPU_USE_SSE2=1)
    compile "test_text_gpu_pack$suffix" tests/test_text_gpu_pack.c "${defines[@]}" -UNDEBUG
    "$out_dir/test_text_gpu_pack$suffix"
    compile "test_text_gpu_pack${suffix}_release" tests/test_text_gpu_pack.c "${defines[@]}" -DNDEBUG
    "$out_dir/test_text_gpu_pack${suffix}_release"
}

bench_build() {
    package_cflags=() package_libs=()
    compile bench_text benchmarks/bench_text.c
}

bench_gpu_pack_build() {
    packages sdl3
    compile bench_gpu_pack benchmarks/bench_gpu_pack.c
}

test_gpu_device_build() {
    packages sdl3
    compile test_text_gpu_device tests/test_text_gpu_device.c "${app_pack[@]}"
}

example_build() {
    packages sdl3
    compile hello_text examples/hello_text.c "${app_pack[@]}"
}

shaders() {
    local shader_compiler backend stage profile extension
    if [[ -n ${SHADERCROSS_EXE:-} ]]; then
        shader_compiler=$(find_executable "$SHADERCROSS_EXE") || fail "SDL_shadercross not found: $SHADERCROSS_EXE"
        backend=shadercross
    elif [[ -n ${DXC_EXE:-} ]]; then
        shader_compiler=$(find_executable "$DXC_EXE") || fail "DXC not found: $DXC_EXE"
        backend=dxc
    elif shader_compiler=$(find_executable shadercross); then
        backend=shadercross
    elif shader_compiler=$(find_executable dxc); then
        backend=dxc
    else
        fail 'HLSL compiler not found; set SHADERCROSS_EXE or DXC_EXE, or add shadercross/dxc to PATH.'
    fi
    printf 'Compiling SPIR-V with %s\n' "$shader_compiler"
    mkdir -p shaders/Compiled/SPIRV
    for extension in vert frag; do
        stage=vertex profile=vs_6_0
        [[ "$extension" != frag ]] || { stage=fragment; profile=ps_6_0; }
        if [[ "$backend" == shadercross ]]; then
            "$shader_compiler" "shaders/rg_text.$extension.hlsl" -s HLSL -d SPIRV \
                -t "$stage" -e main -o "shaders/Compiled/SPIRV/rg_text.$extension.spv"
        else
            "$shader_compiler" -spirv -fspv-flatten-resource-arrays \
                -fspv-preserve-bindings -fspv-preserve-interface -T "$profile" -E main \
                "shaders/rg_text.$extension.hlsl" -Fo "shaders/Compiled/SPIRV/rg_text.$extension.spv"
        fi
        [[ -s "shaders/Compiled/SPIRV/rg_text.$extension.spv" ]] || fail "Empty SPIR-V shader: $extension"
    done
}

test_gpu_device() {
    shaders
    test_gpu_device_build
    "$out_dir/test_text_gpu_device" "${1:-vulkan}"
}

example() {
    shaders
    example_build
    SDL_GPU_DRIVER=${SDL_GPU_DRIVER:-vulkan} "$out_dir/hello_text" "$@"
}

rg_text_bake() {
    packages harfbuzz freetype2
    # POSIX filesystem APIs in the tool require GNU11; the runtime remains C99.
    compile rg_text_bake tools/rg_text_bake.c -std=gnu11 "$@"
    "$out_dir/rg_text_bake" --self-test
}

test_baker() {
    [[ -n ${RG_TEXT_TEST_FONT:-} && -f ${RG_TEXT_TEST_FONT:-} ]] || fail 'Set RG_TEXT_TEST_FONT to the Inter Medium 4.1 TTF.'
    rg_text_bake "$@"
    package_cflags=() package_libs=()
    compile test_bake_output tests/test_bake_output.c "$@"
    bash tests/test_baker_linux.sh "$out_dir/rg_text_bake" "$RG_TEXT_TEST_FONT" "$out_dir/baker-output"
}

test_baker_sanitize() {
    local out_dir="$out_dir/baker-sanitize"
    mkdir -p -- "$out_dir"
    sanitizer_environment
    test_baker "${sanitize[@]}"
}

test_sanitize() {
    local out_dir="$out_dir/sanitize"
    mkdir -p -- "$out_dir"
    sanitizer_environment
    package_cflags=() package_libs=()
    compile test_text tests/test_text.c "${sanitize[@]}" -UNDEBUG
    "$out_dir/test_text"
    compile test_text_release tests/test_text.c "${sanitize[@]}" -DNDEBUG
    "$out_dir/test_text_release"
    packages sdl3
    compile test_text_gpu_pack tests/test_text_gpu_pack.c "${sanitize[@]}" -UNDEBUG
    "$out_dir/test_text_gpu_pack"
    compile test_text_gpu_pack_sse2 tests/test_text_gpu_pack.c "${sanitize[@]}" -DRG_TEXT_GPU_USE_SSE2=1 -UNDEBUG
    "$out_dir/test_text_gpu_pack_sse2"
}

test_fuzz() {
    local compiler compiler_tag version out_dir seconds=${RG_TEXT_FUZZ_SECONDS:-30}
    compiler=$(find_executable "${FUZZ_CC:-clang}") || fail 'libFuzzer requires Clang; install clang or set FUZZ_CC.'
    version=$("$compiler" --version)
    [[ "$version" == *clang* ]] || fail 'FUZZ_CC must select Clang with libFuzzer support.'
    [[ "$seconds" =~ ^[1-9][0-9]*$ ]] || fail 'RG_TEXT_FUZZ_SECONDS must be a positive integer.'
    compiler_tag=${compiler##*/}
    compiler_tag=${compiler_tag//[^[:alnum:]._-]/_}
    out_dir="$build_root/$compiler_tag/fuzz"
    mkdir -p -- "$out_dir"
    sanitizer_environment
    package_cflags=() package_libs=()
    compile fuzz_rgfont tests/fuzz_rgfont.c "${sanitize[@]}" -fsanitize=fuzzer
    compile fuzz_utf8 tests/fuzz_utf8.c "${sanitize[@]}" -fsanitize=fuzzer
    local run_dir kind extra
    run_dir=$(mktemp -d "$out_dir/run.XXXXXX")
    for kind in rgfont utf8; do
        mkdir -p -- "$run_dir/crashes/$kind"
        cp -R tests/fuzz_corpus "$run_dir/corpus-$kind"
        extra=()
        [[ "$kind" != rgfont ]] || extra=(-dict=tests/rgfont_fuzzer.dict)
        "$out_dir/fuzz_$kind" "$run_dir/corpus-$kind" "${extra[@]}" \
            -max_total_time="$seconds" -timeout=5 -artifact_prefix="$run_dir/crashes/$kind/"
    done
}

test_ci() {
    test_text
    test_gpu_compile
    test_gpu_pack
    test_gpu_pack _sse2
    bench_build
    "$out_dir/bench_text" --verify-only
    bench_gpu_pack_build
    "$out_dir/bench_gpu_pack" --verify-only
    test_gpu_device_build
    example_build
    printf 'Linux CPU correctness and GPU compilation passed. Device rendering was not run.\n'
}

test_release() {
    test_ci
    test_sanitize
    test_fuzz
    test_baker
    test_baker_sanitize
    test_gpu_device
    example --smoke-test
    printf 'All Linux release correctness checks passed, including Vulkan rendering.\n'
}

case "$target" in
    test|test_text) test_text ;;
    test_ci) test_ci ;;
    test_release) test_release ;;
    test_sanitize) test_sanitize ;;
    test_fuzz) test_fuzz ;;
    test_baker) test_baker ;;
    test_baker_sanitize) test_baker_sanitize ;;
    test_gpu|test_gpu_compile) test_gpu_compile ;;
    test_gpu_pack) test_gpu_pack ;;
    test_gpu_pack_sse2) test_gpu_pack _sse2 ;;
    test_gpu_pack_asm) fail 'The assembly packer uses the Windows x64 ABI; use test_gpu_pack_sse2 on Linux.' ;;
    test_gpu_device_build) test_gpu_device_build ;;
    test_gpu_device) test_gpu_device "$@" ;;
    shaders) shaders ;;
    example_build) example_build ;;
    example) example "$@" ;;
    example_smoke) example --smoke-test ;;
    rg_text_bake) rg_text_bake ;;
    bench_build) bench_build ;;
    bench) bench_build; (cd -- "$caller_dir"; "$out_dir/bench_text" "$@") ;;
    bench_gpu_pack_build) bench_gpu_pack_build ;;
    bench_gpu_pack) bench_gpu_pack_build; "$out_dir/bench_gpu_pack" "$@" ;;
    *) fail "Unknown target: $target (see ./build.sh --help)." ;;
esac
