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

## Image layout (version 3)

`mcs -c` writes **optimized** images (superinstructions, see below); `mcs -O0 -c` writes plain
bytecode for a VM built with `MCS_ENABLE_SUPEROPS=0`. Version 3 is a compact encoding of the
same records; the loader still accepts version 1 and 2 images.

Header integers are **little-endian**; `vN` is an unsigned LEB128 varint, `zN` a zig-zag
varint. Instruction operands inside `code` are **big-endian** (`hi, lo`).

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `MCSB` |
| 4 | 1 | version — `3` (the loader also accepts `1` and `2`) |
| 5 | 1 | flags: bit0 `MCS_INT64`, bit1 `MCS_ENABLE_FLOAT`, bit2 `MCS_FLOAT_DOUBLE` of the producer |
| 6 | 2 | reserved (0) |
| 8 | vN | `gcount` — number of global names |
| … | … | `gcount` × string — global names (re-linked to VM global slots at load) |
| … | … | main function (recursive *function record*) |

**String** = `vN v`: `0` = null; odd = reference to the `(v >> 1)`-th string already stored in
the image (names, file names and string constants are stored once); even = a new string of
`(v >> 1) - 1` bytes (UTF-8, no terminator) follows.

**Function record**

| Field | Type |
|---|---|
| name, source | string, string |
| arity, min_arity, upvalue_count, flags | u8 ×4 |
| max_slots | vN |
| has_param_types (+ `arity` × u8 conversion codes) | u8 |
| const_count, then constants | vN, tagged values |
| code_len, code | vN, bytes (kept verbatim so it can execute in place) |
| line_count, then `(pc, line)` pairs | vN, `vN` pc delta + `zN` line delta each (0 when stripped) |

Constant tags: `0` null · `1` false · `2` true · `3` int (`zN`) · `4` float (f64 bits) ·
`5` char (`vN`) · `6` string · `7` function record (nested, depth ≤ 64) · `8` float stored as
f32 bits (used when the value is exactly representable).

Version 1/2 used `u32` counts, `u16`-length strings (`0xFFFF` = null) without sharing, `u64`
ints and `u32` `(pc, line)` pairs. Version 3 images are 25–45 % smaller
([PERFORMANCE.md](PERFORMANCE.md#bytecode-images)).

An image with floating-point constants is rejected by a VM built with `MCS_ENABLE_FLOAT=0`;
a 64-bit-int image loads on a 32-bit-int VM (constants are truncated). An optimized image is
rejected by a VM built with `MCS_ENABLE_SUPEROPS=0` ("image uses superinstructions; rebuild
with MCS_ENABLE_SUPEROPS=1 or compile with -O0").

## Loader validation

`mcs_load_image` / `mcs_exec_image` / `mcs_exec_image_xip` reject an image unless:

* magic, version, every length and count are within the buffer (`corrupt bytecode image`);
* every opcode is known and its operands fit inside `code`;
* constant operands are in range and of the right kind (strings for names, functions for `CLOSURE`,
  including the default-value operand of `FIELD` and the constants of superinstructions);
* global operands are in range (they are re-linked to the VM's global slots);
* local-slot operands are `< max_slots` (`FOR_ITER` uses two slots), upvalue operands are
  `< upvalue_count`, and `CLOSURE` captures reference valid locals / upvalues;
* every branch target (`JUMP*`, `JF_*`, `LOOP`, `TRY`, `ARGC_JUMP`, `FOR_ITER` and the fused
  compare-and-branch superinstructions) lands on an instruction boundary inside the function;
* superinstruction sub-operations (`BX_*` / `JX_*` bytes) are in range.

> [!WARNING]
> The loader does **not** verify stack balance or operand types. An image crafted by hand can
> still misbehave at run time. Only run images produced by `mcs`/`mcs_compile_image`, or
> authenticate them (signing is planned — see [SECURITY.md](SECURITY.md)).
> `tools/fuzz.py --image` mutates real images and checks that loading + disassembling never
> crashes under ASan/UBSan.

## Copy vs execute in place

| | `mcs_exec_image` (copy) | `mcs_exec_image_xip` (execute in place) |
|---|---|---|
| function code | copied to the heap | read from the image buffer |
| global operands | patched to VM slots while copying | looked up through a per-function `gmap` table at run time |
| constants, line tables, class metadata | heap | heap (same) |
| image buffer after the call | may be freed | must stay valid and unchanged until `mcs_free(vm)` |
| CLI | `./mcs app.mcsb` | `./mcs --xip app.mcsb` |

Both paths run the same validation. XIP is meant for images linked into flash; RAM saved is
roughly the bytecode size of the image (measurements in [LOW_RESOURCE.md](LOW_RESOURCE.md)).
The image format is identical; `FN_XIP` (0x08) is a runtime-only function flag and is masked
off when an image's flags are read. `make test` runs every test from source, as a copied
image and as an XIP image. Builds with `MCS_ENABLE_XIP=0` fall back to copying.

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

### Superinstructions (v2)

Emitted only by the image optimizer (`src/mcs_opt.c`), never by the compiler itself. Operand
letters: `L` local slot (u8), `K` constant (k16), `I` signed 8-bit immediate, `S` the value on
top of the stack. `op8` is a `BX_*` operation (`+ - * / % & | ^ << >> >>>`), `c8` a `JX_*`
comparison (`== != < <= > >=`). Every form has an int fast path; any other operand types
(floats, strings, operator overloads) take the same generic code as the plain instructions,
with identical results and exceptions.

| # | Mnemonic | Operands | Effect |
|---:|---|---|---|
| 86 | `BIN_LL` | op8 s8 s8 | push `L[a] op L[b]` |
| 87 | `BIN_LK` | op8 s8 k16 | push `L[a] op K` |
| 88 | `BIN_SL` | op8 s8 | `top = top op L[b]` |
| 89 | `BIN_SK` | op8 k16 | `top = top op K` |
| 90 | `BIN_LLS` | op8 s8 s8 s8 | `L[d] = L[a] op L[b]` |
| 91 | `BIN_LKS` | op8 s8 k16 s8 | `L[d] = L[a] op K` |
| 92 | `BIN_LIS` | op8 s8 i8 s8 | `L[d] = L[a] op I` |
| 93 | `JF_LK` | c8 s8 k16 o16 | jump forward unless `L[a] c K` |
| 94 | `JF_SL` | c8 s8 o16 | pop `x`; jump forward unless `x c L[b]` |
| 95 | `JF_SK` | c8 k16 o16 | pop `x`; jump forward unless `x c K` |
| 96 | `JB_LK` | c8 s8 k16 o16 | jump **back** (safepoint) if `L[a] c K` — rotated loop test |
| 97 | `GET_FIELD_L` | s8 k16 | push `L[s].name` (field cache, `Length`/`Count` fast path) |
| 98 | `RETURN_LOCAL` | s8 | return `L[s]` |
| 99 | `ACC` | op8 s8 | `L[d] = L[d] op pop` |
| 100 | `ACC_ADD` | s8 | `L[d] = L[d] + pop` |
| 101 | `ACC_SUB` | s8 | `L[d] = L[d] - pop` |
| 102 | `SETF_L` | s8 k16 | `L[s].name = pop` (statement; setters run normally) |
| 103 | `GET_INDEX_LL` | s8 s8 | push `L[a][L[i]]` |
| 104 | `SET_INDEX_LL` | s8 s8 | `L[a][L[i]] = pop` (statement; user indexers run normally) |
| 105–115 | `LI_ADD` … `LI_USHR` | s8 i8 | push `L[a] op I` (one opcode per operation) |
| 116–126 | `SI_ADD` … `SI_USHR` | i8 | `top = top op I` |
| 127–132 | `JFLI_EQ` … `JFLI_GE` | s8 i8 o16 | jump forward unless `L[a] c I` |
| 133–138 | `JFSI_EQ` … `JFSI_GE` | i8 o16 | pop `x`; jump forward unless `x c I` |
| 139–144 | `JBLI_EQ` … `JBLI_GE` | s8 i8 o16 | jump back (safepoint) if `L[a] c I` |
| 145–150 | `JFLL_EQ` … `JFLL_GE` | s8 s8 o16 | jump forward unless `L[a] c L[b]` |
| 151–156 | `JBLL_EQ` … `JBLL_GE` | s8 s8 o16 | jump back (safepoint) if `L[a] c L[b]` |

`/` and `%` immediates are only fused for positive divisors (0 and −1 keep the checked path).

### The optimizer

`mcs_optimize` (run by `mcs -c`, `mcs -C` and `mcs_compile_image*`; on-device source only with
`MCS_OPTIMIZE_SOURCE=1`) repeats until nothing changes (≤ 6 passes):

1. **fusion** of the patterns above (e.g. `GET_LOCAL a; INT8 1; SUB` → `LI_SUB a 1`;
   `GET_LOCAL i; GET_LOCAL n; LT; JF` → `JFLL_LT i n`; `x = x + e` → `e; ACC_ADD x` when `e` is
   a pure expression and `x` is not captured by a closure), constant folding of `INT8; CONV
   float`;
2. **jump threading**: a `JUMP` that lands on a return becomes that return;
3. **loop rotation**: when the loop head is a fused compare-and-branch, the back edge
   re-tests the condition (`JBLI_*`, `JBLL_*`, `JB_LK` — "jump back if true") instead of
   `LOOP` + test, so each iteration executes one branch instead of two;
4. **dead-code removal** (unreachable instructions after unconditional jumps/returns);
5. **relayout**: branch offsets (including `TRY` handler offsets) and the line table are remapped.

Only instructions that are not branch targets are fused, so control flow is unchanged;
`make test` runs every test as source (unoptimized), optimized image and XIP image and
compares the output byte for byte (`tests/t17_optimizer.cs` covers every pattern and the
slow paths).

### Run-time caches

* `GET_FIELD`/`SET_FIELD`/`GET_FIELD_L`/`SETF_L`: per-function `(class, slot)` cache.
* `INVOKE`: per-call-site method cache keyed by class (`MCS_FIELD_CACHE`), invalidated when
  any class gains members (`vm->cls_epoch`).
* `CALL` of a script class: cached constructor closure (`new C(...)` skips the lookup).
* `array.Length`, `List.Count`, `string.Length` are read directly.

| Switch | Default | Effect |
|---|---|---|
| `MCS_ENABLE_SUPEROPS` | 1 (0 in `min`) | VM handlers for the table above (~8 KB Thumb code) |
| `MCS_ENABLE_OPTIMIZER` | compiler && superops | `mcs_optimize` in the compiler build |
| `MCS_OPTIMIZE_SOURCE` | 0 | also optimize `mcs_exec_source` (more compile time and RAM) |
| `MCS_FIELD_CACHE` | 1 (0 in `min`) | field / method / constructor caches |

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
