# Quad packing experiment

The CPU portion of `rg_text_gpu_queue_quads` expands each 48-byte quad into
four 36-byte vertices and six 32-bit indices. The experiment compares the
original loop from `cf3c982`, the current portable C loop, an SSE2 intrinsic
kernel, and handwritten SSE2 assembly using the Windows x64 calling convention.
It does not measure layout, texture upload, GPU execution, or total frame time.

## Reproduce

From an x64 Visual Studio developer prompt with `RG_CORE_DIR` and `SDL3_DIR`
set as described in the README:

```bat
build.bat bench_gpu_pack
build.bat bench_gpu_pack --verify-only
```

The assembly is in `src/asm/rg_text_gpu_pack_quads_x64.asm`. The portable C path
is the default. Enable the intrinsic path with `#define RG_TEXT_GPU_USE_SSE2 1` before
including `rg_text_gpu.h`, or set `RG_TEXT_GPU_USE_SSE2=1` in the environment
when using `build.bat` application/device targets.

For the Windows x64 assembly path, define `RG_TEXT_GPU_USE_ASM=1` instead and
assemble/link the MASM source once into your application. `build.bat` does this
automatically when that environment variable is `1`. The C and intrinsic paths
remain header-only. Neither optimized path changes the public API or geometry;
only one may be enabled at a time. Both require baseline SSE2, with no runtime
CPU dispatch or AVX requirement.

```bat
set RG_TEXT_GPU_USE_ASM=1
build.bat example --smoke-test
build.bat test_gpu_pack_asm
```

On an x86-64 system with GCC or Clang and SDL3 development files, the C/SSE2
comparison can also be built without the Windows assembly:

```sh
cc -std=c99 -O2 -Wall -Wextra -Werror -I "$RG_CORE_DIR/src" \
  $(pkg-config --cflags sdl3) benchmarks/bench_gpu_pack.c \
  $(pkg-config --libs sdl3) -lm -o bench_gpu_pack
./bench_gpu_pack
```

## Method

Each binary checks identical output bytes and counters before timing. It tests
append offsets, capacity clipping, zero/null inputs, unaligned buffers, signed
zero, infinities, and NaN payloads. The public API has separate packing contract
tests, including protected-page boundaries on Windows. Output hashes are
checked after every timed trial, outside the measured interval.

Seven trials alternate method order and report median, minimum, and maximum.
Each trial warms the buffers first. Small cases reuse one buffer; the streaming
case rotates through 64 disjoint input/output sets totaling 108 MiB. All kernels
run on the same PC in the same process, with the same compiler settings and data.
Build optimized without link-time optimization; record CPU, compiler, power
settings, and any externally applied affinity alongside results. Compare methods
within one run, not absolute times collected on different PCs.

The Windows build uses MSVC `/std:c11 /O2 /W4 /WX` and no `/GL`. Timing is
informational, never a CI performance threshold. CI runs `--verify-only`.

## Observed results (2026-09-22)

Intel Core i7-12700KF, Windows x64, SDL3 3.4.10. These final runs inherited
logical-CPU-0 affinity from the launching process; power settings were unchanged
and not recorded. Two MSVC 19.44.35219 runs gave the following ranges of batch
medians (microseconds). These ranges span the two run medians, not confidence
intervals:

| Quads / buffer sets | Original | Portable C | SSE2 intrinsics | Assembly |
| --- | ---: | ---: | ---: | ---: |
| 1,656 / 1 | 14.40–14.42 | 12.01–12.34 | 8.27–8.74 | 6.67–7.18 |
| 8,192 / 1 | 78.54–81.49 | 68.76–72.42 | 52.27–57.44 | 44.80–47.99 |
| 8,192 / 64 | 115.21–118.90 | 112.57–122.92 | 94.27–106.07 | 97.90–104.96 |

For 1,656 cached quads, assembly takes about half the original loop's time,
and 18–19% less time than the intrinsic path in these MSVC runs. The C default
also improves the cached batches. The rotating-buffer case shows much less
benefit and no consistent assembly advantage over intrinsics.

Compiler choice changes the result. With Clang 19.1.5 on the same PC, the
1,656-quad medians were 13.48 / 9.72 / 6.57 / 6.56 microseconds respectively:
intrinsics and assembly effectively tied. At one quad, the intrinsic path
was slightly faster than assembly with both compilers. This is why the fast
paths are explicit options, and why an assembly win is not assumed for other
compilers, CPUs, or workloads.

[Raw results](../benchmarks/results/quad_packing_i7_12700kf.txt) include all
batch sizes, per-trial minima/maxima, compiler versions, and final checksums.
