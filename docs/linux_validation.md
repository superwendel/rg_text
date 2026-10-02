# Linux validation and performance

The Linux entrypoint targets x86-64 with GCC or Clang. The runtime remains
strict C99; the optional baker uses GNU11 for its filesystem APIs. Portable C
is the default packing path, and SSE2 remains an explicit option. The Windows
assembly implementation is not part of the Linux build.

## Reproduce correctness checks

Install a C toolchain, Bash, `pkg-config`, and SDL3 development files. Full
validation also needs Clang's sanitizer/libFuzzer runtimes, FreeType and
HarfBuzz development files, and a Vulkan driver with a working SDL video
environment. The baker integration script uses standard GNU utilities and
`flock` from util-linux. Dependencies installed under a custom prefix can be
selected with `PKG_CONFIG_PATH` and the platform's library search path.

```sh
CC=gcc ./build.sh test_ci
CC=clang ./build.sh test_ci
CC=clang ./build.sh test_sanitize
FUZZ_CC=clang ./build.sh test_fuzz
```

`test_ci` runs runtime and CPU packing checks with assertions enabled and
disabled, verifies both benchmarks, and compiles the device test and example.
It requires no GPU. Sanitizers cover runtime and portable/SSE2 packing,
including Linux protected-page boundaries. The two fuzzers each run for 30
seconds by default; `RG_TEXT_FUZZ_SECONDS` overrides that duration. Build
products, copied fuzz corpora, crash inputs, and baker outputs stay under
`build/linux/<compiler>/`.

The baker fixture is Inter Medium 4.1 from the
[Inter 4.1 release](https://github.com/rsms/inter/releases/tag/v4.1). Extract
`extras/ttf/Inter-Medium.ttf` and verify this SHA256 before running tests:

```text
97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872
```

```sh
export RG_TEXT_TEST_FONT=/path/to/Inter-Medium.ttf
./build.sh test_baker
CC=clang ./build.sh test_baker_sanitize
```

These checks cover round-trip parsing and atlas size, deterministic output,
kerning/no-kerning consistency, large selections, rejected inputs, lock
contention and retry, output preservation, and rollback when the final metrics
replacement fails. Windows-pinned rasterization goldens are not enabled on
Linux; structural checks tolerate font-library version differences.

For rendering, provide SDL_shadercross through `SHADERCROSS_EXE`, or Microsoft
[DXC](https://github.com/microsoft/DirectXShaderCompiler/releases) through
`DXC_EXE`. The DXC fallback preserves SPIR-V bindings and shader interfaces;
both paths compile the same HLSL files. An explicitly selected compiler must
exist. With neither override, the runner looks for shadercross, then dxc.

```sh
export DXC_EXE=/path/to/dxc
./build.sh test_gpu_device vulkan
./build.sh example_smoke
RG_TEXT_GPU_USE_SSE2=1 ./build.sh test_gpu_device vulkan
RG_TEXT_GPU_USE_SSE2=1 ./build.sh example_smoke
CC=clang FUZZ_CC=clang ./build.sh test_release
```

The release target includes CPU checks, sanitizers, fuzzers, normal and
sanitized baker integration, device pixel readback, and example smoke testing.
Missing prerequisites fail explicitly. A build-only result is not evidence of
rendering correctness. Existing hosted Linux CI uses the CPU, sanitizer,
fuzzer, and baker targets; device execution is a local release check.

## Reproduce performance comparisons

```sh
./build.sh bench
./build.sh bench path/to/baked.font
./build.sh bench_gpu_pack
python3 tools/bench_linux.py --baseline HEAD --cc gcc --cc clang \
  --cpu 0 --font path/to/baked.font --output build/results/linux-run-1
```

The comparison driver requires Python 3 and `taskset`. Choose a CPU permitted
by your current affinity mask; omitting `--cpu` selects the lowest permitted
CPU. Choose a new output directory for each run. `--suite layout` or
`--suite packing` restricts a tuning experiment; both run by default.

The driver builds the selected Git baseline and current runtime with the
**same current benchmark harness and rg_core checkout**, verifies all binaries,
then runs three baseline/current pairs with alternating process order. Each
binary reports seven warmed samples. Flags are fixed at strict C99 and `-O2`,
without LTO, fast-math, or a native CPU target. Environment compiler flags are
recorded but not applied by this comparison driver; use `build.sh` for custom
build experiments.

Output includes raw logs, exact commands, compiler and dependency versions,
source hashes and patch, CPU/cache/OS information, affinity and power settings,
and a copy of the optional baked metrics input. Local logs retain the paths
needed to reproduce the run; published results normalize those paths. Builds
finish before timing; run comparisons with other CPU/GPU-intensive work stopped.
System power settings are never changed.

Layout covers short/full ASCII, absent kerning, large sparse tables with pair
hits and misses, Unicode, multiline center/right alignment, and capacity-one
output. The optional font adds real-asset short/full cases. Indexed results,
measurement, and output bounds are checked against linear lookup outside the
timed intervals. Timed calls remain observable through a dynamic function
pointer; checksum publication is outside timing.

Packing covers 1, 16, 1,656, and 8,192 quads plus a 108 MiB rotating-buffer
case. It checks exact bytes and counters, including unaligned buffers, signed
zero and NaN payloads. This measures CPU vertex/index expansion, not uploads,
GPU time, or whole-frame performance. The historical "original" loop is
context; optimization decisions use the pre-change runtime as the baseline.

For this pass, retain candidates only with a target improvement of at least
5% exceeding observed variation, and no repeatable regression above 3% in
other representative workloads under either compiler. Timing is informational
and is not a CI pass/fail threshold. Different CPUs in the existing Windows
results preclude attributing timing differences to the operating system.

## Observed results — 2026-10-02

Host: Linux Mint 22.1, kernel 6.8.0-60, Intel Core i3-1005G1 (2 cores / 4
threads). Timings were pinned to logical CPU 0; the `powersave` governor,
`balance_performance` EPP and enabled turbo were left unchanged. Local versions
were GCC 13.3.0, Clang 19.1.1 and SDL 3.2.18. CI retains its separate pinned
SDL/core versions. The sibling rg_core checkout was left unchanged, including
its existing modifications; dependency revision and header hashes are recorded.

No runtime optimization was retained. Every file under `src/` is byte-for-byte
identical to revision `5331db7dee83338dacbf8f2e7b90d69acb60bac1`.
The final changes add Linux build/test support, benchmark coverage, and
documentation without changing shipped layout or packing code. This source
identity is stronger evidence against a regression introduced by this change
than noisy timing comparisons alone.

A Linux/GCC SSE2 candidate replaced cast/byte-shift expressions with
unpack/shuffle operations. It removed stack spills and improved cached
workloads under GCC 13 with the baseline CPU target. A precommit audit compared
baseline and candidate in the same process, using identical noinline adapters
compiled in separate translation units without LTO. Each trial paired both
versions, reversed their order on alternate trials, and retained unchanged C
implementations as controls. All four configurations (`-O2`, `-O3`, and each
with `-march=native`) passed exact-byte and counter verification and completed
three runs of seven paired trials.

The native target already avoided the original spills; the rewrite added
shuffle instructions there. With `-O3 -march=native`, the 1,656-quad case slowed
in all three runs (4.3%, 11.7%, and 4.6%; median across 21 pairs: 5.3%). Other
native results were mixed, and tiny/streaming workloads remained noisy. The
candidate failed the no-regression criterion and was removed. Earlier
cross-process speedups are not claims about the final library.

Representative layout baselines, medians of three run medians, are shown
below in microseconds per call. They use full output capacity. These compiler
measurements were collected in separate intervals and are not a controlled
compiler ranking.

| Workload | GCC | Clang |
| --- | ---: | ---: |
| Dense ASCII, 16 bytes | 0.972 | 0.588 |
| Dense ASCII, 2,000 bytes | 117.483 | 93.418 |
| Sparse Unicode with kerning hits, 2,000 bytes | 70.480 | 45.917 |
| Baked Inter, 2,000 ASCII bytes | 113.504 | 59.370 |

Checked dense-glyph indexing and kerning-range rejection were investigated and
removed. Despite large ASCII/miss-case gains, the combined shortcuts repeatedly
regressed sparse Unicode cases and failed the balanced-workload acceptance
rule. Final layout code is identical to the pre-change runtime.

[GCC measurements](../benchmarks/results/linux_i3_1005g1_gcc.txt) and
[Clang measurements](../benchmarks/results/linux_i3_1005g1_clang.txt) contain
baseline logs, normalized commands, compiler/dependency/source hashes, and
verification checksums. The GCC report also records the rejected SSE2
candidate's paired audit samples and unchanged C controls. Full experimental
snapshots remain local under ignored `build/`; rejected patches and
machine-local environment details are excluded from the published reports.

## Validation on this host

The final Clang release gate passed: strict C99 checks with assertions on/off,
ASan/UBSan with leak detection, both 30-second fuzz campaigns (about 1.9 million
runs each), regular/sanitized baker integration, Vulkan pixel readback and
example smoke. The final GCC CPU and sanitizer gates also passed, followed by
pixel-readback and example smoke tests of the GCC SSE2 path. Leak detection
remained enabled for the sanitizer runs.

Rendering was explicitly checked with the Intel ICD: Intel UHD Graphics ICL
GT1, Mesa 25.2.8, Vulkan API 1.4.318. Both portable C and SSE2 passed pixel tests
and example smoke. Shaders were built using official Microsoft DXC 1.9.2609.5
and passed `spirv-val --target-env vulkan1.0`; the shadercross executable was
not available locally. The DXC archive SHA256 was
`96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1`.
The baker used FreeType 2.13.2 and HarfBuzz 8.3.0 with the pinned Inter fixture.

Full local logs are retained under `build/linux-validation/`; generated
artifacts and downloaded dependencies are excluded from version control.
Windows execution and the updated hosted CI jobs were not run on this Linux
host.
