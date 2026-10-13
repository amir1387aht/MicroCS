# Evidence-grade v2 host benchmark harness

This is measurement infrastructure, **not a claim that v2 is complete or faster**.
Existing historical benchmarks/raw evidence remain unchanged until equivalent
v2 measurements and all release gates are finished.

```sh
make bench-v2-test
python3 bench/v2/run.py --out build/v2-host --repetitions 25
# Compare a clean historical checkout using the SAME harness, compiler and flags:
python3 bench/v2/run.py --repo /path/to/clean-v1-checkout --out build/v1-host --repetitions 25
```

The destination must not exist: evidence is never overwritten. Python 3,
a C99/GNU99 host compiler, `size`, and a GNU-compatible linker are required.
Default flags are `-O2 -std=gnu99 -Wall -Wextra -Werror`; explicit `--cc` and
`--cflags` select a configuration. The runner links every current core/module
source, with default feature configuration (no artificial feature removal).
`make bench-v2-test` uses the Makefile's current flags instead and tests the
harness rather than publishing performance evidence.

## Workloads and checked outputs

- `compiler`: 1200 repeated assignments, output 1200; compile-heavy fixture.
- `calls`: fib(18) = 2584, recursive calls/dispatch.
- `dispatch`: sum(i % 7, i=0..99999), integer loop/modulo.
- `collections`: 200 Point instances in a List, method calls and iteration;
  expected sum is independently computed from `(i % 10)^2 + (i % 3)^2`.
- `strings`: StringBuilder, Split and Parse, 690 characters and sum 19900.

Every execution must return MCS_OK and print exactly its checked `.out`; errors,
truncated output, embedded source NULs, invalid repetitions, missing files and
allocator teardown leaks fail the run. No timing result is accepted merely
because a program finished. These mathematical fixtures are **not** a .NET
reference comparison, which remains a separate required gate.

## Measurement definitions

Each workload produces one JSONL row per phase/sample. Phases: VM init;
optimized/unoptimized compile-to-image; source compile+run; optimized/unoptimized
image load+run; optimized/unoptimized XIP load+run; cold/warm REPL preparation+
evaluation. All phases except warm REPL use a **fresh VM** per sample. VM creation
is included only in vm_init; no independently measured startup is subtracted.
Teardown and output comparisons are outside the timed interval (output callback
work itself remains timed consistently). Warm REPL retains a variable and VM
across samples; allocator peak is reset to current used bytes before each
warm sample, while GC peak/collection counts are VM-lifetime cumulative.
The cold REPL entry evaluates `21 * 2`; warm evaluates `bench_counter + 1`
after declaring `bench_counter = 41` outside timing. Cold and warm therefore
measure representative entry modes, not identical expressions; do not advertise
their ratio as a pure cache speedup.

CLOCK_MONOTONIC values are **host wall microseconds**, not MCU cycles/instruction
counts. Fresh VM does not imply flushed CPU/OS caches. Fixed phase order and all
repetitions are retained; there is no discarded warmup or best-of headline.
Summary CSV reports median, p10/p90 (linear interpolation), min/max and maximum
requested memory across repetitions. No stability threshold or speed target is
inferred from a single machine/run. Benchmark budgets are still pending.

Custom realloc accounts requested VM/GC/compiler bytes, including VM allocation,
not just GC. It excludes malloc metadata/alignment, external source/expected
buffers and copied image storage. `requested_retained_bytes` is before teardown,
without forced GC, **not** a leak; every VM must return tracked usage to zero
at teardown. Image size is separately recorded. This instrumentation changes
allocator/IO costs; apply it identically on both versions. It is not RSS, a
small-MCU pool minimum, compiler stack high-water, or real hardware RAM/flash.
The host ELF `size.txt` and link map are preserved but are not MCU flash sizes.

## Evidence files

- `metadata.json`: commit/tree/dirty status, actual core/header content hashes,
  harness/workload/expected hashes, build argv, compiler/OS/CPU/Python versions,
  timing/memory/cache/security/optional-feature definitions, binary/raw hashes.
- `samples.jsonl`: every checked sample, including iteration number and memory.
- `summary.csv`: generated exclusively from persisted raw samples with an
  independent median cross-check; never manually edit it.
- `build.log`, per-workload stderr logs, `host.map`, `size.txt`, executable.

Commit small raw/provenance/summary/log evidence remotely at each milestone;
keep binaries/link maps in backups or published artifacts as appropriate.
A source checkout with nonempty dirty status needs its content hashes/patch
before it can be reproduced; prefer clean recorded commits for comparisons.

Not covered yet: M0/M4/ESP/RP2 profile/cycle/flash/stack measurements, shell
cancellation, realistic network/peripheral workloads, optional LVGL/TFT and
security overhead, .NET comparisons, and production hardware behavior. These
remain explicit v2 gates, not implied by host success.
