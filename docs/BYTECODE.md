# Bytecode and the `.mcsb` image format

MicroCS compiles C# to a compact **stack bytecode**. The same bytecode is either executed
right after compiling, or serialised into a portable **image** (`.mcsb`) that a device can
run without the compiler (`MCS_ENABLE_COMPILER=0`).

```sh
./mcs -c app.cs -o app.mcsb          # image file
./mcs -C app.cs -n app_image -o app_image.h -s   # const C array, -s strips line info
./mcs -d app.cs                      # disassemble source
./mcs -d app.mcsb                    # validate + disassemble an image
```

## Image layout (version 2)

All header integers are **little-endian**; instruction operands inside `code` are
**big-endian** (`hi, lo`).

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `MCSB` |
| 4 | 1 | version — `2` (the loader also accepts `1`) |
| 5 | 1 | flags: bit0 `MCS_INT64`, bit1 `MCS_ENABLE_FLOAT`, bit2 `MCS_FLOAT_DOUBLE` of the producer |
| 6 | 2 | reserved (0) |
| 8 | 4 | `gcount` — number of global names |
| 12 | … | `gcount` × string — global names (re-linked to VM global slots at load) |
| … | … | main function (recursive *function record*) |

**String** = `u16 length` (`0xFFFF` = null) + bytes (UTF-8, no terminator).

**Function record**

| Field | Type |
|---|---|
| name, source | string, string |
| arity, min_arity, upvalue_count, flags | u8 ×4 |
| max_slots | u16 |
| has_param_types (+ `arity` × u8 conversion codes) | u8 |
| const_count, then constants | u32, tagged values |
| code_len, code | u32, bytes |
| line_count, then `(pc, line)` pairs | u32, u32×2 each (0 when stripped) |

Constant tags: `0` null · `1` false · `2` true · `3` int (i64) · `4` float (f64 bits) ·
`5` char (u32) · `6` string · `7` function record (nested, depth ≤ 64).

An image with floating-point constants is rejected by a VM built with `MCS_ENABLE_FLOAT=0`;
a 64-bit-int image loads on a 32-bit-int VM (constants are truncated).

## Loader validation

`mcs_load_image` / `mcs_exec_image` reject an image unless:

* magic, version, every length and count are within the buffer (`corrupt bytecode image`);
* every opcode is known and its operands fit inside `code`;
* constant operands are in range and of the right kind (strings for names, functions for `CLOSURE`);
* global operands are in range (they are re-linked to the VM's global slots);
* local-slot operands are `< max_slots` (`FOR_ITER` uses two slots), upvalue operands are
  `< upvalue_count`, and `CLOSURE` captures reference valid locals / upvalues;
* every branch target (`JUMP*`, `JF_*`, `LOOP`, `TRY`, `ARGC_JUMP`, `FOR_ITER`) lands on an
  instruction boundary inside the function.

> [!WARNING]
> The loader does **not** verify stack balance or operand types. An image crafted by hand can
> still misbehave at run time. Only run images produced by `mcs`/`mcs_compile_image`, or
> authenticate them (signing is planned — see [SECURITY.md](SECURITY.md)).
> `tools/fuzz.py --image` mutates real images and checks that loading + disassembling never
> crashes under ASan/UBSan.

## Instruction set

Operand legend: `k16` constant index · `g16` global · `s8` local slot · `u8` upvalue ·
`o16` unsigned jump offset (relative to the next instruction) · `i8` signed byte ·
`n8`/`n16` count · `c8` conversion code · `argc8` argument count.

| # | Opcode | Operands | Effect |
|---:|---|---|---|
| 0 | `CONST` | k16 | push `consts[k16]` |
| 1 | `NULL` | — | push null |
| 2 | `TRUE` | — | push true |
| 3 | `FALSE` | — | push false |
| 4 | `INT8` | i8 | push small int `i8` |
| 5 | `POP` | — | drop top |
| 6 | `DUP` | — | duplicate top |
| 7 | `DUP2` | — | duplicate top two |
| 8 | `SWAP` | — | swap top two |
| 9 | `ROT` | n8 | rotate top `n` values |
| 10 | `GET_LOCAL` | s8 | push `slots[s8]` |
| 11 | `SET_LOCAL` | s8 | `slots[s8]` = top (keeps value) |
| 12 | `GET_UPVAL` | u8 | push captured variable `u8` |
| 13 | `SET_UPVAL` | u8 | store captured variable `u8` |
| 14 | `GET_GLOBAL` | g16 | push global `g16` |
| 15 | `SET_GLOBAL` | g16 | store global `g16` (keeps value) |
| 16 | `GET_FIELD` | k16 | `obj.name` (name = `consts[k16]`, inline-cached) |
| 17 | `SET_FIELD` | k16 | `obj.name = v` (inline-cached) |
| 18 | `GET_INDEX` | — | `a[i]` |
| 19 | `SET_INDEX` | — | `a[i] = v` |
| 20 | `ADD` | — | `+` (numbers, string concat, operator overloads) |
| 21 | `SUB` | — | `-` |
| 22 | `MUL` | — | `*` |
| 23 | `DIV` | — | `/` (int fast path) |
| 24 | `MOD` | — | `%` (int fast path) |
| 25 | `NEG` | — | unary `-` |
| 26 | `BAND` | — | `&` |
| 27 | `BOR` | — | `|` |
| 28 | `BXOR` | — | `^` |
| 29 | `BNOT` | — | `~` |
| 30 | `SHL` | — | `<<` |
| 31 | `SHR` | — | `>>` |
| 32 | `USHR` | — | `>>>` |
| 33 | `NOT` | — | `!` |
| 34 | `EQ` | — | `==` |
| 35 | `NE` | — | `!=` |
| 36 | `LT` | — | `<` |
| 37 | `LE` | — | `<=` |
| 38 | `GT` | — | `>` |
| 39 | `GE` | — | `>=` |
| 40 | `INC_LOCAL` | s8 i8 | `slots[s8] += i8` (loop counters) |
| 41 | `JUMP` | o16 | `pc += o16` |
| 42 | `JUMP_IF_FALSE` | o16 | pop; jump if false |
| 43 | `JUMP_IF_TRUE` | o16 | pop; jump if true |
| 44 | `JUMP_IF_FALSE_KEEP` | o16 | jump if top is false (no pop) — `&&` |
| 45 | `JUMP_IF_TRUE_KEEP` | o16 | jump if top is true (no pop) — `||` |
| 46 | `JUMP_IF_NULL_KEEP` | o16 | jump if top is null — `?.` |
| 47 | `JUMP_IF_NOT_NULL_KEEP` | o16 | jump if top is not null — `??` |
| 48 | `LOOP` | o16 | `pc -= o16` (backward jump, safepoint) |
| 49 | `ARGC_JUMP` | n8 o16 | skip default-argument code if `argc >= n8` |
| 50 | `CALL` | argc8 | call value under `argc` arguments |
| 51 | `INVOKE` | k16 argc8 | call method `consts[k16]` on receiver with `argc` |
| 52 | `SUPER_INVOKE` | k16 argc8 | `base.M(...)` |
| 53 | `CLOSURE` | k16 + 2×upvalues | create closure of `consts[k16]`; then 2 bytes (is_local, index) per upvalue |
| 54 | `CLOSE_UPVAL` | — | close captured local and pop |
| 55 | `RETURN` | — | return top |
| 56 | `RETURN_NULL` | — | return null |
| 57 | `CLASS` | k16 flags8 | declare class `consts[k16]`, flags |
| 58 | `INHERIT` | — | set base class |
| 59 | `IMPLEMENTS` | k16 | add interface `consts[k16]` |
| 60 | `FIELD` | k16 k16b | declare field `consts[k16]` with default `consts[k16b]` |
| 61 | `METHOD` | k16 flags8 | add method |
| 62 | `STATIC` | k16 flags8 | add static member |
| 63 | `GETTER` | k16 flags8 | add property getter |
| 64 | `SETTER` | k16 flags8 | add property setter |
| 65 | `ARRAY` | n16 | build array from `n16` stack values |
| 66 | `NEW_ARRAY` | c8 | `new T[n]` with element conversion `c8` |
| 67 | `CONV` | c8 | numeric conversion `c8` (casts, typed stores) |
| 68 | `TOSTR` | — | `ToString()` for interpolation |
| 69 | `TOSTR_FMT` | — | format with alignment/format spec |
| 70 | `CONCAT` | n8 | concatenate `n8` strings |
| 71 | `IS` | k16 | `is T` |
| 72 | `AS` | k16 | `as T` |
| 73 | `CAST` | k16 | `(T)x` with check |
| 74 | `FOR_ITER` | s8 o16 | foreach step over `slots[s8]` (collection) / `slots[s8+1]` (index); exit `o16` |
| 75 | `TRY` | o16 | push handler at `pc + o16` |
| 76 | `END_TRY` | — | pop handler |
| 77 | `THROW` | — | throw top |
| 78 | `SET_LOCAL_POP` | s8 | `slots[s8]` = pop  *(v2)* |
| 79 | `SET_GLOBAL_POP` | g16 | global `g16` = pop  *(v2)* |
| 80 | `JF_EQ` | o16 | pop b, a; jump `o16` unless `a == b`  *(v2)* |
| 81 | `JF_NE` | o16 | … unless `a != b`  *(v2)* |
| 82 | `JF_LT` | o16 | … unless `a < b`  *(v2)* |
| 83 | `JF_LE` | o16 | … unless `a <= b`  *(v2)* |
| 84 | `JF_GT` | o16 | … unless `a > b`  *(v2)* |
| 85 | `JF_GE` | o16 | … unless `a >= b`  *(v2)* |

Opcodes are **append-only**: new instructions get new numbers at the end and bump
`IMG_VERSION`; existing numbers never change meaning, so old images keep loading.

## Execution model

* **Value stack + frames.** Each call frame owns a window `slots[0..max_slots)` of the value
  stack; slot 0 is the callee/receiver, then the parameters, then locals.
* **Dispatch** uses computed `goto` (`MCS_COMPUTED_GOTO=1`, GCC/Clang) or a `switch`.
* **Safepoints** — calls, backward jumps (`LOOP`), allocating instructions — are the only
  points where the GC may run and where the host hook, time/step budgets and
  `mcs_request_abort` are checked. Native code may therefore hold unrooted temporaries.
* **Inline caches**: `GET_FIELD`/`SET_FIELD` keep a per-function `(class, slot)` cache
  indexed by the constant operand (`MCS_FIELD_CACHE`).
* **Fused branches** (`JF_*`) compare two ints/floats in place; any other operand types take
  the generic comparison (operator overloads, strings) without popping first.
