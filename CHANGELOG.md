# Changelog

All notable changes. Versions follow `MCS_VERSION_*` in `include/mcs.h`.

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
