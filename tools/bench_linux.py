#!/usr/bin/env python3
"""Compare a Git baseline and the working tree with the same Linux benchmarks.

Example: python3 tools/bench_linux.py --baseline HEAD --output build/results/run-1
Only Python's standard library is needed. GCC/Clang, rg_core, and taskset are
external prerequisites; packing also needs pkg-config and SDL3. No system power
settings are changed.
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def capture(command, cwd=None):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def executable(name):
    found = shutil.which(name)
    if not found:
        raise ValueError(f"Required executable was not found: {name}")
    return str(Path(found).absolute())


def git_state(directory, include_status=True):
    state = {"directory": str(directory)}
    for key, args in (
        ("revision", ["rev-parse", "HEAD"]),
        ("status", ["status", "--porcelain=v1", "--untracked-files=normal"]),
    ):
        result = subprocess.run(
            ["git", "-C", str(directory), *args],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        value = result.stdout.strip() if result.returncode == 0 else None
        if key == "status":
            state["dirty"] = None if value is None else bool(value)
            if include_status:
                state[key] = value
        else:
            state[key] = value
        if result.returncode:
            state[key + "_error"] = result.stderr.strip()
    return state


def hashes(directory):
    return {
        str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(directory.rglob("*")) if path.is_file()
    }


def read_optional(path):
    try:
        return Path(path).read_text().strip()
    except OSError as error:
        return {"unavailable": str(error)}


def command_optional(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            return {"unavailable": result.stderr.strip()}
        return result.stdout.strip()
    except OSError as error:
        return {"unavailable": str(error)}


def external_flags(flags):
    """Treat dependency includes as system headers, matching build.sh."""
    result = []
    iterator = iter(flags)
    for flag in iterator:
        if flag == "-I":
            result.extend(["-isystem", next(iterator)])
        elif flag.startswith("-I"):
            result.extend(["-isystem", flag[2:]])
        else:
            result.append(flag)
    return result


def logged(command, destination, cwd):
    with destination.open("w") as log:
        log.write("$ " + shlex.join(command) + "\n\n")
        log.flush()
        result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(
            f"Command exited {result.returncode}; see {destination}: {shlex.join(command)}"
        )


def run(args):
    repository = Path(__file__).resolve().parents[1]
    output = Path(args.output).absolute()
    if output.exists():
        raise ValueError(f"Output directory already exists; choose a new path: {output}")
    if sys.platform != "linux" or platform.machine() != "x86_64":
        raise ValueError("This driver currently supports Linux x86-64.")
    allowed = sorted(os.sched_getaffinity(0))
    cpu = allowed[0] if args.cpu is None else args.cpu
    if cpu not in allowed:
        raise ValueError(f"CPU {cpu} is outside the permitted affinity set: {allowed}")
    taskset = executable("taskset")
    suites = args.suite or ["layout", "packing"]
    if len(set(suites)) != len(suites):
        raise ValueError("Each --suite may be specified only once.")
    compiler_names = args.cc or ["gcc", "clang"]
    compilers = []
    for name in compiler_names:
        path = executable(name)
        tag = re.sub(r"[^A-Za-z0-9_.-]", "_", Path(name).name)
        if any(item["tag"] == tag for item in compilers):
            raise ValueError(f"Compiler names must have distinct basenames: {tag}")
        compilers.append({"tag": tag, "path": path, "version": capture([path, "--version"])})
    core = Path(os.environ.get("RG_CORE_DIR", repository.parent / "rg_core")).resolve()
    if not (core / "src/rg_defs.h").is_file():
        raise ValueError("rg_core not found; set RG_CORE_DIR to its repository root.")
    baseline = capture(
        ["git", "rev-parse", "--verify", "--end-of-options", args.baseline + "^{commit}"],
        cwd=repository,
    )
    sdl_cflags, sdl_libs = [], []
    sdl_metadata = {"status": "not-used", "reason": "Only the layout suite was selected."}
    if "packing" in suites:
        pkg_config = executable(os.environ.get("PKG_CONFIG", "pkg-config"))
        sdl_version = capture([pkg_config, "--modversion", "sdl3"])
        sdl_cflags = external_flags(shlex.split(capture([pkg_config, "--cflags", "sdl3"])))
        sdl_libs = shlex.split(capture([pkg_config, "--libs", "sdl3"]))
        sdl_metadata = {"version": sdl_version, "cflags": sdl_cflags, "libs": sdl_libs}
    font = Path(args.font).resolve() if args.font else None
    if font is not None and not font.is_file():
        raise ValueError(f"Baked font file was not found: {font}")
    power_paths = [
        "/sys/devices/system/cpu/intel_pstate/no_turbo",
        "/sys/devices/system/cpu/cpufreq/boost",
        *[f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/{name}" for name in (
            "scaling_driver", "scaling_governor", "energy_performance_preference",
            "scaling_min_freq", "scaling_max_freq", "scaling_cur_freq",
        )],
    ]
    metadata = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "invocation": [sys.executable, *sys.argv],
        "baseline_requested": args.baseline,
        "baseline_revision": baseline,
        "working_tree": git_state(repository),
        "rg_core": {
            **git_state(core, include_status=False),
            "header_sha256": {
                str(path.relative_to(core)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted((core / "src").rglob("*.h")) if path.is_file()
            },
        },
        "compilers": compilers,
        "sdl": sdl_metadata,
        "system": {
            "platform": {
                "system": platform.system(),
                "release": platform.release(),
                "version": platform.version(),
                "machine": platform.machine(),
            },
            "os_release": read_optional("/etc/os-release"),
            "cpuinfo": read_optional("/proc/cpuinfo"),
            "lscpu": command_optional(["lscpu", "--json"]),
            "lscpu_topology": command_optional(["lscpu", "--extended=CPU,CORE,SOCKET,NODE,CACHE,ONLINE"]),
            "selected_cpu_cache": {
                str(path): read_optional(path)
                for index in sorted(Path(f"/sys/devices/system/cpu/cpu{cpu}/cache").glob("index*"))
                for path in (index / name for name in (
                    "level", "type", "size", "shared_cpu_list", "coherency_line_size",
                ))
            },
        },
        "affinity": {"allowed": allowed, "selected_cpu": cpu, "taskset": taskset},
        "power_settings": {path: read_optional(path) for path in power_paths},
        "environment": {name: os.environ.get(name) for name in (
            "PKG_CONFIG_PATH", "LD_LIBRARY_PATH", "RG_CORE_DIR",
            "CC", "CPPFLAGS", "CFLAGS", "LDFLAGS", "LDLIBS",
        )},
        "flags_policy": "Fixed C99 -O2; no LTO, fast-math, or native-only ISA flags. Environment compiler flags are recorded but not applied.",
        "paired_runs": 3,
        "suites": suites,
        "samples_per_binary_run": 7,
        "font": None if font is None else {
            "path": str(font), "sha256": hashlib.sha256(font.read_bytes()).hexdigest(),
        },
        "commands": [],
        "validation": {},
        "status": "running",
    }
    output.mkdir(parents=True, exist_ok=False)
    # Record all tracked source changes relative to the recorded HEAD, including
    # staged changes. The baseline revision and hashes remain in metadata.json.
    (output / "source.patch").write_bytes(subprocess.check_output(
        ["git", "diff", "--binary", "HEAD", "--", "src"], cwd=repository,
    ))
    metadata["source_patch"] = "source.patch"
    metadata_path = output / "metadata.json"

    def save_metadata():
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")

    def record_run(command, phase, compiler, suite, version, filename, repetition=None):
        metadata["commands"].append({
            "phase": phase, "compiler": compiler, "suite": suite,
            "version": version, "repetition": repetition, "argv": command,
            "log": filename,
        })
        save_metadata()
        print(f"{phase}: {compiler} / {suite} / {version}" + (
            f" / pair {repetition}" if repetition else ""
        ), flush=True)
        logged(command, output / filename, repository)

    save_metadata()
    staging_root = repository / "build"
    staging_root.mkdir(exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="bench-linux-", dir=staging_root) as temporary:
            staging = Path(temporary)
            metadata["temporary_staging"] = str(staging)
            baseline_tree, current_tree = staging / "baseline", staging / "current"
            shutil.copytree(repository / "src", current_tree / "src")
            for name in ("bench_text.c", "bench_gpu_pack.c", "quad_pack_reference.h"):
                source = repository / "benchmarks" / name
                destination = current_tree / "benchmarks" / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
            shutil.copytree(current_tree / "benchmarks", baseline_tree / "benchmarks")
            baseline_files = subprocess.check_output(
                ["git", "ls-tree", "-r", "--name-only", "-z", baseline, "--", "src"],
                cwd=repository,
            ).decode().split("\0")
            for relative in filter(None, baseline_files):
                destination = baseline_tree / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(subprocess.check_output(
                    ["git", "show", baseline + ":" + relative], cwd=repository,
                ))
            metadata["source_sha256"] = {
                "baseline": hashes(baseline_tree / "src"),
                "current": hashes(current_tree / "src"),
                "harness": hashes(current_tree / "benchmarks"),
            }
            # Keep the exact sources used by this run, even after temporary
            # binaries are removed and the working tree continues changing.
            snapshots = output / "sources"
            shutil.copytree(baseline_tree / "src", snapshots / "baseline" / "src")
            shutil.copytree(current_tree / "src", snapshots / "current" / "src")
            shutil.copytree(current_tree / "benchmarks", snapshots / "benchmarks")
            metadata["source_snapshots"] = {
                "baseline": "sources/baseline/src",
                "current": "sources/current/src",
                "harness": "sources/benchmarks",
            }
            font_args = []
            if font is not None:
                font_snapshot = output / "input.font"
                shutil.copyfile(font, font_snapshot)
                font_args = [str(font_snapshot)]
                metadata["font"]["snapshot"] = "input.font"
            binaries = {}
            for compiler in compilers:
                for suite in suites:
                    source = "bench_text.c" if suite == "layout" else "bench_gpu_pack.c"
                    for version, tree in (("baseline", baseline_tree), ("current", current_tree)):
                        binary = tree / f"{compiler['tag']}-{suite}"
                        command = [compiler["path"], "-std=c99", "-pedantic-errors", "-O2",
                                   "-Wall", "-Wextra", "-Werror", "-I", str(core / "src")]
                        if suite == "packing":
                            command.extend(sdl_cflags)
                        command.append(str(tree / "benchmarks" / source))
                        if suite == "packing":
                            command.extend(sdl_libs)
                        command.extend(["-lm", "-o", str(binary)])
                        binaries[compiler["tag"], suite, version] = str(binary)
                        record_run(command, "compile", compiler["tag"], suite, version,
                                   f"{compiler['tag']}-{suite}-{version}-compile.log")

            # Compile everything, then verify everything, before any timed run.
            for (compiler, suite, version), binary in binaries.items():
                command = [taskset, "-c", str(cpu), binary, "--verify-only"]
                if suite == "layout":
                    command.extend(font_args)
                record_run(command, "verify", compiler, suite, version,
                           f"{compiler}-{suite}-{version}-verify.log")
            if "layout" in suites:
                for compiler in compilers:
                    checksums = {}
                    for version in ("baseline", "current"):
                        verification = output / f"{compiler['tag']}-layout-{version}-verify.log"
                        matches = re.findall(
                            r"^Layout and measurement checks passed\. Checksum: ([0-9]+)$",
                            verification.read_text(), re.MULTILINE,
                        )
                        if len(matches) != 1:
                            raise RuntimeError(f"Expected one deterministic layout checksum in {verification}")
                        checksums[version] = matches[0]
                    matched = checksums["baseline"] == checksums["current"]
                    metadata["validation"][compiler["tag"] + "_layout_checksum"] = {
                        **checksums, "matched": matched,
                    }
                    save_metadata()
                    if not matched:
                        raise RuntimeError(
                            f"Layout output changed for {compiler['tag']}: "
                            f"baseline checksum {checksums['baseline']}, current {checksums['current']}"
                        )
            for compiler in compilers:
                for suite in suites:
                    for repetition in range(1, 4):
                        versions = ("baseline", "current") if repetition % 2 else ("current", "baseline")
                        for version in versions:
                            command = [taskset, "-c", str(cpu), binaries[compiler["tag"], suite, version]]
                            if suite == "layout":
                                command.extend(font_args)
                            record_run(command, "measure", compiler["tag"], suite, version,
                                       f"{compiler['tag']}-{suite}-{version}-run-{repetition}.log",
                                       repetition)
        metadata["status"] = "passed"
    except BaseException as error:
        metadata["status"] = "failed"
        metadata["error"] = str(error)
        raise
    finally:
        metadata["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save_metadata()
    print(f"Measurements and metadata saved to {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="HEAD", help="Baseline Git commit/ref (default HEAD)")
    parser.add_argument("--cc", action="append", help="Compiler executable; repeat for multiple (default gcc and clang)")
    parser.add_argument("--suite", action="append", choices=("layout", "packing"),
                        help="Benchmark suite; repeat for both (default layout and packing)")
    parser.add_argument("--cpu", type=int, help="Permitted logical CPU (default lowest allowed)")
    parser.add_argument("--font", help="Optional baked .font file; copied for both revisions")
    parser.add_argument("--output", required=True, help="New directory for metadata and raw logs")
    try:
        run(parser.parse_args())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"bench_linux.py: {error}\n")


if __name__ == "__main__":
    main()
