# Changelog

All notable changes. Versions follow `MCS_VERSION_*` in `include/mcs.h`.

## 1.3.0 — small-MCU release

Focus: RAM and flash on weak microcontrollers, without changing script behaviour.
Guide: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md).

### Memory & speed (all measured, emulated Cortex-M unless noted)
- **`MCS_COMPACT_VALUES`** (default on for 32-bit targets): values packed to 4-byte alignment,
  12 instead of 16 bytes with double/int64 payloads. On Cortex-M0 + newlib-nano this removed a
  `memcpy` call per value copy: the demo image runs in **4.56 M instead of 18.2 M instructions**.
- **Execute-in-place images**: `mcs_exec_image_xip()` runs bytecode from the image buffer
  (flash) instead of copying it to the heap; `mcs --xip`; `MCS_ENABLE_XIP`.
- VM baseline after `mcs_new` on Cortex-M: **50 136 B → 25 688 B** (M0, full stdlib). Interned-string
  set stores keys only, a global's slot lives in its name string (no global index table),
  built-in exception classes share one field layout, hash tables start at `MCS_TABLE_MIN_CAP` (4).
- **Compact `Dictionary`/`HashSet` index**: 1-, 2- or 4-byte position slots instead of full
  key/value entries; `HashSet` stores no values array; `Remove` no longer rehashes. Host:
  700-string `HashSet` 104 → 58 KB, 100-entry `Dictionary<int,int>` 12.1 → 4.3 KB.
- Compiler RAM: smaller AST nodes and tokens (24 bytes on 32-bit), token array grows by the
  observed density instead of doubling. On-device compile + run of the demo on the M4:
  pool peak **131 528 B → 90 480 B**, compile instructions −12 %.
- Lexer keyword lookup by length + first character, cheaper primitive-type check.
- Host (`make bench`, same machine, same session as 1.2.0): run times unchanged, `mcs_new`
  43 → 30 µs, demo compile 201 → 140 µs, demo heap peak 116 438 → 87 510 B.

### New options and profiles
- `profiles/mcs_profile_lowram.h` for 32–64 KB RAM parts (images only, single-precision
  floats → 8-byte values, small VM limits, 4 KB first GC, 4-byte pool alignment).
- `MCS_ENABLE_LINQ` (LINQ operators + `Enumerable`, ~12 KB flash on M0), `MCS_POOL_ALIGN`,
  `MCS_TABLE_MIN_CAP`, `MCS_ERROR_SIZE`, `MCS_MAX_PINS`, `MCS_COMPACT_VALUES`, `MCS_ENABLE_XIP`.
- Cortex-M targets `m0-lowram` (64 KB RAM, 40 KB pool) and `m0-node` (the new
  `examples/lowram` firmware, 48 KB RAM, 32 KB pool) in `make cm-check`.
- `tools/cm_emu.py --profile N` (hottest functions) and `--callers SYMBOL`.

### Library
- LINQ operators on `Dictionary`, `HashSet`, `Stack` and `Queue` (`Where`, `Select`, `Skip`,
  `Take`, `Last`, `Average`, `GroupBy`, … — previously only lists/arrays had the full set).
- `HashSet`, `Stack` and `Queue` are accepted wherever a sequence is: `string.Join`,
  `string.Concat`, `new List<T>(…)`, `AddRange`, `Concat`, `Zip`, `SequenceEqual`, `new Stack/Queue(…)`
  (previously `string.Join(",", set)` printed the type name).
- `HashSet.IsSupersetOf`, `SetEquals`, `SymmetricExceptWith`.

### Fixes
- `base(msg)` into a lazily registered native constructor (e.g. `class E : IOException`) failed
  with "does not contain .ctor".
- `class X : IOException` was treated as an interface by the `I`+uppercase heuristic.

### Docs, examples, tests
- New: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md), [examples/lowram/](examples/lowram/).
- New test `t14_low_resource.cs` (exception subclasses of built-ins, globals, string churn,
  dictionary index widths, collection interop) — byte-identical to .NET 8.
- Every script test now also runs as an XIP image (46 runs); 104 C unit checks (XIP images,
  300 globals, intern churn, shared layouts, dictionary index); `make check` adds the lowram
  profile and five alternative configurations that run the whole suite.

## 1.2.0

### Language
- **Tuples**: literals `(1, "a")`, named elements `(x: 1, y: 2)`, tuple types `(int x, int y)` in
  locals, fields, parameters, `out` parameters, return types and generic arguments
  (`Dictionary<(int, int), string>`); `Item1..Item7`; structural `==`/`!=`, hashing and `ToString()`.
- Tuple element **name projection** (`var t = (a, b); t.a`) and names that survive later
  assignments, `out` parameters, tuple-typed fields/properties and object initializers.
- **Deconstruction**: `var (a, b) = t;`, `(int a, var b) = t;`, swaps `(a, b) = (b, a)` without
  allocation, `_` discards, `foreach (var (k, v) in dict)`.
- **Index from end and ranges**: `x[^1]`, `x[1..^1]`, `x[..3]`, `x[2..]` on strings, arrays and lists.
- **Patterns in `is`**: relational, `and`, `or`, `not` (`t is < 0 or > 100`); `and` also in switch
  arms and `case` labels.
- `sizeof(builtin)`, `checked(...)` / `unchecked(...)` expressions, `static` local functions with
  tuple return types; `.HasValue` / `.Value` / `.GetValueOrDefault()` on nullable values.
- Declarations used as embedded statements (`while (c) int i = 0;`) are now a compile error (as in C#).

### Library
- LINQ `Zip` (tuple and selector forms) and `Chunk`; `Stack.TryPop` / `TryPeek`,
  `Queue.TryDequeue` / `TryPeek`.
- Generated API reference `docs/STDLIB.md` (`tools/gen_stdlib_doc.py`).

### Performance
- Image format **v2** with superinstructions: `SET_LOCAL_POP`, `SET_GLOBAL_POP` and fused
  compare-and-branch `JF_EQ/NE/LT/LE/GT/GE`. v1 images still load.
- Per-site inline cache for field access (`MCS_FIELD_CACHE`), exact-arity call fast path,
  integer `/` `%` fast path.
- Token array freed right after parsing. Host: fib −20 %, loop −22 %; Cortex-M4 demo −17 %
  instructions; on-device compile peak 163 KB → 132 KB.

### Fixes
- Unbounded growth of the string intern table with many temporary strings (eventually OOM).
- Stack-slot leak when an unbraced loop/if body introduced locals (`out var`, deconstruction temps).
- Lexer: `1..3` is a range, not `1.` followed by `.3`.

### Hardening & tooling
- Image loader validates local/upvalue operands, closure captures and that every branch lands
  on an instruction boundary.
- `mcs -d file.mcsb` disassembles images (`mcs_disassemble_image`).
- `tools/fuzz.py` mutation fuzzer (source and `--image` modes), clean under ASan/UBSan;
  `make asan-test` runs the whole suite with sanitizers; GitHub Actions CI.
- `.NET 8` parity check extended to `t11`, `t13`, `examples/tour.cs`.
- New examples (`blink.cs`, `sensor_logger.cs`, `tour.cs`) run by `make test`.

## 1.1.0 — Phase 2
- Virtual filesystem (RAM, POSIX, LittleFS) with C# `File` / `Directory` / `Path`.
- HAL with C# `GPIO`, `UART`, `I2C`, `SPI`, `ADC`, `PWM` + simulator board.
- Job scheduler, standalone shell with UART upload protocol, execution limits.
- `ref` / `out` / `in`, `case` patterns with `when`.
- Cortex-M0/M4/M33 reference port verified in an emulator. Memory fixes in the compiler.

## 1.0.0 — Phase 1
- Lexer, parser, single-pass compiler, stack VM with computed goto, precise GC, stdlib,
  bytecode images, CLI/REPL.
