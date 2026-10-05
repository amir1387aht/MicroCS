# MicroCS — Phase 1 Handoff

Audience: the next Claude session (Phase 2). Read this before changing anything.
This file describes the code as it is at the end of Phase 1. It does not propose
a rewrite. Keep changes small and run the checks in §10 after each one.

---

## 1. What the project is

MicroCS (C prefix `mcs_`) is an interpreter for a subset of C#, aimed at
microcontrollers, in the spirit of MicroPython. It is portable C99/gnu99 with no
dependencies beyond libc (and libm when floats are on). The owner is an embedded/UI
engineer who targets **SiFli SF32LB525 with RT‑Thread and LVGL**.

Goals: a usable C# subset, easy C bindings, a choice of features at build time and at
runtime, good speed, a small RAM footprint, and precompiled bytecode that runs from flash.

**Phase 1 status:** the language and standard library work. The CLI/REPL, bytecode
images, embedding API, firmware demo and port template are done. Tests: 15/15 pass.
The RT‑Thread and LVGL ports were **written, then removed on the user's request**
("not needed for now"). They are likely Phase 2 work (see §9).

Version: `MCS_VERSION_STRING "1.0.0"`, image format version `IMG_VERSION 1`.

---

## 2. Architecture

```
source ─► lexer (tokens) ─► parser (AST in an arena) ─► single-pass compiler ─► mcs_function_t (bytecode)
                                                                         └─► image save ("MCSB") ─► image load/relink
                                                                                                  ─► stack VM (run loop)
```

* **Lexer** (`mcs_lexer.c`): turns the whole source into a `toklist_t` up front. It also
  holds the arena allocator (`arena_alloc`, 8 KB chunks taken from the VM allocator).
* **Parser** (`mcs_parser.c`): recursive descent into `node_t` ASTs and `classdecl_t`/
  `funcdecl_t` structures. It uses token lookahead with save/restore (`P->pos`) to tell
  declarations, casts, generics and lambdas apart. It also desugars syntax
  (e.g. `using` → block + try/finally + `x?.Dispose()`).
* **Compiler** (`mcs_compiler.c`): one pass over the AST that emits stack bytecode. It
  tracks static "primitive types" (`PT_*`) and numeric conversions (`CV_*`) so it can
  insert `OP_CONV` for narrowing (byte/short/char wrap) and fold constants. It resolves
  locals, upvalues (clox style) and globals (slot indices). Classes are emitted as
  runtime opcodes (`OP_CLASS/INHERIT/METHOD/FIELD/GETTER/SETTER/STATIC/IMPLEMENTS`).
* **Bytecode utilities** (`mcs_bytecode.c`): `fn_emit` with run-length line info,
  constant de-duplication, the disassembler, and image save/load.
* **VM** (`mcs_vm.c`): a `switch` or computed-goto dispatch loop (`MCS_OPCODES` X-macro in
  `mcs_internal.h`). Call frames, try handlers, overload choice, method lookup, exceptions,
  the hook/abort mechanism, and the public call API.
* **Objects/GC** (`mcs_object.c`): allocation accounting, interning, hash tables, classes,
  lists/dicts, a precise mark-and-sweep GC, the pool allocator, and the lazy ROM member
  tables.
* **Values** (`mcs_value.c`): equality, comparison, `ToString`, number formatting
  (`F2`, `X`, `N0`, `P1`, alignment).
* **Standard library** (`mcs_lib*.c`): registration API plus Object/Exception/primitive
  types/Console/Convert/Math/System, strings and StringBuilder, collections and LINQ.

### Runtime model (key facts)
* `mcs_value_t` = `{uint8_t type; union {bool,int,float,obj*}}`. It is 16 bytes on a
  64-bit host with double floats, and 8 bytes on 32-bit with `MCS_FLOAT_DOUBLE=0`.
  `char` is a separate tag (`MCS_T_CHAR`). There is one integer type for every C# integer
  type; narrowing is done by the compiler with `OP_CONV`.
* Object kinds: STRING, FUNCTION, CLOSURE, UPVALUE, NATIVE, CLASS, INSTANCE, BOUND,
  OVERLOADS, ARRAY, LIST, DICT, USERDATA. **All strings are interned** (the intern table
  is weak and swept by the GC).
* Classes use **copy-down inheritance**: at `mcs_class_inherit` the subclass copies the
  parent's tables (methods, getters, setters, fields, ifaces). Statics are *not* copied;
  lookups walk `super`.
* **Overloads**: methods with the same name form an `MCS_O_OVERLOADS` set, chosen at call
  time by `pick_overload`. The first pass requires an exact arity match for natives, then
  it checks `param_types`. `FN_HAS_PARAMS` marks a `params T[]` final parameter; packing
  happens in `call_closure`.
* **Structs are reference types.** **Generics are erased.**
* **Exceptions** are script instances of classes derived from `Exception`, with fields
  `Message`, `StackTrace`, `InnerException`. `try` pushes an `mcs_handler_t`. `finally` is
  inlined at exits (`unwind_tries`), with catch-all rethrow for the exception path.
  StackTrace is captured at the **first throw site** (`mcs_throw_value`, only if the field
  is still null).
* **Multicast delegates**: `a + b` on callables gives an `MCS_O_BOUND` whose receiver is an
  ARRAY of targets and whose method is the native `mc_invoke` (`mcs_vm.c`). `-` removes the
  last matching target. One remaining target collapses back to the plain value; none
  gives null.
* **`foreach`** works on arrays, lists, dicts (as KeyValuePair), strings, and any object
  whose `GetEnumerator()` returns one of those (`FOR_ITER` re-dispatch through `ip -= 4`).
* **ThenBy**: the VM remembers the last `OrderBy` result in `vm->order_last` and its key
  selectors in `vm->order_spec`. Both are GC roots. ThenBy only chains when called
  directly on that result; otherwise it behaves like OrderBy.

### GC rules (very important)
* Precise mark-and-sweep. Roots: the VM stack, frames, open upvalues, globals and global
  names, `vm->roots[]` (temporary, `MCS_MAX_ROOTS`=32), `vm->pins[]` (`MCS_MAX_PINS`=16),
  `exc_value`, `order_last/order_spec`, `compiling`, builtin classes, pre-interned strings.
* **The GC never runs inside `mcs_realloc`.** Allocation only sets `vm->gc_wanted`.
  Collection happens at **safepoints**: `SAFEPOINT()` (backward `LOOP`, `CALL`, `INVOKE`)
  and `GCPOINT()` (start of `NEW_ARRAY`, `CONCAT`, `CLOSURE`, `ARRAY`, `TOSTR`).
  At those points every live value is on the VM stack.
* So natives may hold unrooted objects in C locals **unless they call back into the
  script** (`lib_call`, `mcs_call_value`, …), because that can reach a safepoint. In that
  case, root the temporaries (`new_rooted` / `mcs_push_root`, then `mcs_pop_root`), or wrap
  short allocation sequences in `vm->gc_pause++/--`.
* Heap limit (`cfg.heap_limit`) is a **hard cap**: going over it panics with
  `MCS_ERR_MEMORY`. To avoid that, `next_gc` is capped at 75 % of the limit (and starts at
  50 %). If the allocator returns NULL, that also panics. Panics use `setjmp`/`longjmp`
  (`vm->panic`).
* `MCS_GC_STRESS=1` requests a GC at every safepoint (torture test). All tests pass in
  this mode.

### Lazy ROM members (`MCS_LAZY_REGS=1`)
`mcs_add_regs` does not create native objects. It records the `const mcs_reg_t[]`
pointer in `cls->rom` (`mcs_rom_t` linked list, in registration order). `mcs_cls_get(vm,
cls, MCS_TAB_METHODS|STATICS|GETTERS|SETTERS, name, &out)` checks the hash table first. On
a miss it pulls the member from the superclass chain (copy-down), then builds entries from
its own ROM chunks, following the eager rules: the first occurrence of a name in a chunk
overrides an inherited entry with the same signature, and later occurrences become
overloads. Results are cached in the class tables; misses are not cached.
This cut baseline RAM from about 94 KB to about 47 KB (64-bit host).

---

## 3. File responsibilities

| File | Lines | Responsibility |
|---|---|---|
| `include/mcs.h` | 288 | **Public API** (values, config, lifecycle, exec, calls, bindings, roots/pins, pool, stats) |
| `include/mcs_config.h` | 119 | Feature flags and limits (all `#ifndef`, can be overridden with `-D`) |
| `include/mcs_bind.h` | 68 | `MCS_WRAP_<ret>_<args>` macros for wrapping C functions; `MCS_CONST_INT/STR` |
| `src/mcs_internal.h` | 405 | Object structs, `mcs_vm` struct, opcode X-macro, `PT_*`/`CV_*`/`EXC_*` enums, internal prototypes |
| `src/mcs_front.h` | 177 | Tokens, arena, `typeref_t`, `node_t` + node kinds, parser/compiler declarations |
| `src/mcs_lexer.c` | 319 | Arena + tokenizer (number suffixes, verbatim/interpolated strings, chars) |
| `src/mcs_parser.c` | 1416 | Parser: types, expressions, statements, class/struct/interface/enum/record declarations, desugaring |
| `src/mcs_compiler.c` | 1681 | AST → bytecode, scopes/locals/upvalues, top-level variable type tracking (`gvars`), patterns, switch, try/finally |
| `src/mcs_bytecode.c` | 439 | Emit helpers, line tables, disassembler, image save/load |
| `src/mcs_vm.c` | 1484 | Run loop, calls/overloads, member access, exceptions, multicast, public exec/call API, lifecycle |
| `src/mcs_object.c` | 703 | Allocator wrapper, objects, interning, tables, classes + lazy ROM lookup, list/dict, GC, pool |
| `src/mcs_value.c` | 452 | Equality/compare/hash, ToString and number formatting |
| `src/mcs_lib.h` | 36 | Private helpers for library code (`NATIVE`, `CHECK`, `ARGN`, `lib_call`, …) |
| `src/mcs_lib.c` | 994 | Registration API, Object/Type, exceptions, numeric types, Console, Convert, Math, Environment/Thread/GC/Debug/Stopwatch/Random, Delegate |
| `src/mcs_lib_str.c` | 482 | `string` instance/static methods, StringBuilder |
| `src/mcs_lib_coll.c` | 1130 | Arrays, List, Dictionary, HashSet/Stack/Queue (instances with hidden field `$items`), LINQ, Enumerable, stable merge sort, ThenBy |
| `ports/unix/main.c` | 242 | CLI: run `.cs`/`.mcsb`, `-c`, `-C`, `-o`, `-n`, `-s`, `-d`, `-e`, REPL, `--heap`, `--stack`, `--stats`, Ctrl‑C. Stubs keep it compiling with any flag combination |
| `ports/template/mcs_port_template.c` | 64 | Firmware skeleton with TODOs (bare metal / FreeRTOS / Zephyr notes) |
| `examples/app.cs`, `examples/firmware_example.c` | | Host-run firmware demo: pool heap, hooks, `MCS_WRAP_*`, `Uart` native class + finalizer, pinned lambda callback, image from `app_image.h`, `mcs_call("App.Loop")` |
| `examples/app_image.h` | | **Generated** by `make example` (`mcs -C`). Do not edit by hand |
| `tests/t01..t06_*.cs` + `.out` | | Basics, OOP, collections/strings, exceptions, functional, GC stress |
| `tests/err_*.cs` + `.out` | | Compile/runtime error messages |
| `tests/run_tests.sh` | 36 | Runs each test from source **and** as a compiled `.mcsb` image, comparing stdout+stderr with `.out` |
| `bench/*.cs`, `bench/*.py` | | fib(30), 10M-iteration loop, 1M objects; compared with CPython |
| `Makefile` | | `all`, `test`, `example`, `size`, `asan` (no libasan in the old sandbox), `clean` |
| `README.md` | | User-facing overview |

---

## 4. Public APIs and interfaces (stable contract)

From `include/mcs.h`:
* Lifecycle: `mcs_config_default`, `mcs_new`, `mcs_free`, `mcs_user_data`.
* Config fields: `realloc_fn`, `write_fn`, `error_fn`, `readline_fn`, `ticks_fn`,
  `delay_fn`, `hook_fn` (returns non-zero to abort), `user_data`, `alloc_ud`,
  `stack_slots`, `max_frames`, `heap_limit`, `stdlib` (`MCS_LIB_CORE|MATH|COLLECTIONS|TEXT|SYSTEM|ALL`).
* Run: `mcs_exec_source` (needs the compiler), `mcs_compile_image` / `mcs_free_image`
  (compiler + save), `mcs_exec_image` (load), `mcs_disassemble_source`.
  * Top-level statements run immediately. If there are none, `static void Main()` is
    invoked (with an empty `string[]` if it takes parameters).
* Calls: `mcs_call(vm, "Func" | "Class.StaticMethod", ...)`, `mcs_call_value`,
  `mcs_invoke` (method on an object), `mcs_new_object` (added at the end of Phase 1;
  `new ClassName(args)` for script or native classes).
* Errors: return `mcs_result_t` (`MCS_OK, MCS_ERR_COMPILE, _RUNTIME, _MEMORY, _BYTECODE,
  _ABORTED`). Also `mcs_last_error`, `mcs_request_abort`, `mcs_raise(vm, "ExcClass", fmt,
  ...)`, `mcs_has_exception`. Calls made **inside a native** (run_depth>0) leave the
  exception pending instead of reporting it.
* Bindings: `mcs_register_module`, `mcs_register_class(const mcs_class_def_t*)`,
  `mcs_module_set`, `mcs_set_global/get_global`, `mcs_register_function`,
  `mcs_userdata`, `mcs_check_userdata`.
* Native signature: `mcs_value_t fn(mcs_vm_t*, mcs_value_t self, int argc, mcs_value_t* argv)`.
  `mcs_reg_t {name, fn, arity(-1 = variadic), kind 'f'|'g'|'s'}` via `MCS_FN/MCS_GET/MCS_SET/MCS_REG_END`.
  Getters/setters in a *statics* table are static properties (`Board.Name`).
* `mcs_class_def_t {name, instance_size, ctor, finalizer, members, statics}`.
  Instance data is zeroed. Methods named `Dispose` work with `using`.
* Values: `mcs_null/bool/int/char/float`, `mcs_is_*`, `mcs_string(_n)`, `mcs_cstr`,
  `mcs_strlen`, `mcs_truthy`, checked conversions `mcs_to_int/float/bool/cstr` (raise on
  a mismatch and return 0 or ""), `mcs_tostring`, arrays/lists (`mcs_new_array`,
  `mcs_new_list`, `mcs_len`, `mcs_index`, `mcs_set_index`, `mcs_list_add`),
  `mcs_get_field/set_field`.
* Memory: `mcs_push_root/pop_root`, `mcs_pin/pinned/unpin`, `mcs_gc`, `mcs_mem_stats`
  (`bytes_in_use, peak_bytes, next_gc, objects, collections`), `mcs_pool_init`,
  `mcs_pool_realloc`.

**Bytecode image format v1** (`mcs_bytecode.c` header comment): `"MCSB"`, u8 version,
u8 flags (bit0 INT64, bit1 FLOAT, bit2 DOUBLE), u16 reserved, then a global-name table,
then the recursive function records. Images from `mcs_compile_image` have an 8-byte size
header *before* the returned pointer (that is why `mcs_free_image` exists; never `free()`
an image yourself). Global operands are indices into the image name table and are
relinked on load.

---

## 5. Design decisions (and why)

1. **Tree → single-pass compiler instead of a typed IL/CLR** (unlike nanoFramework). It
   keeps the code small, compiles on the device, and needs no metadata tables. Static
   type info is used only for conversions and constant folding.
2. **Dynamic values with a C#-shaped surface.** Erased generics, one integer kind,
   reference-type structs. This was traded on purpose for size and simplicity.
3. **Copy-down inheritance + hash tables per class.** O(1) method lookup, no vtable layout
   in the compiler.
4. **Overload choice at runtime** (arity, then `param_types`). It lets natives and script
   methods share the same name tables.
5. **Strings interned everywhere.** Equality is pointer comparison and member names are
   fast keys. The cost is a hash on every string creation.
6. **GC only at safepoints** (changed late in Phase 1). Before, collection could run
   inside an allocation and freed half-built objects (found with `MCS_GC_STRESS`). Do not
   bring that back.
7. **Lazy ROM tables.** RAM grows with the members a script actually uses (the MicroPython
   ROM-table idea).
8. **Bytecode images + compiler-less builds.** Compile on the PC with `mcs -C` and ship a
   const array in flash (the MicroPython `.mpy` idea).
9. **Dict keeps insertion order on remove** (shift and reindex, O(n)) to match .NET
   enumeration order in common cases.
10. **`FirstOrDefault`/`LastOrDefault` default heuristic**: since generics are erased,
    the default is guessed from the type of the first element (int → 0, float → 0.0,
    bool → false, char → '\0', otherwise null).

---

## 6. Important assumptions

* Compiler: GCC/Clang with `-std=gnu99`. `__attribute__`, computed goto and statement
  expressions may appear (computed goto is guarded by `MCS_COMPUTED_GOTO`). MSVC was not
  tested.
* `MCS_INT64=0` by default, so **C# `long` is 32-bit**. With `MCS_INT64=1` all integers
  are 64-bit, so `int` overflow and `>>>` results differ (t01 line 11 differs in that
  build).
* Floats are double by default on the host. `MCS_FLOAT_DOUBLE=0` is meant for MCUs; output
  precision then differs from the `.out` files (expected).
* Strings are UTF-8 and **indexed by byte** (`Length`, `s[i]`, `Substring`).
* Single-threaded VM. One VM per thread. Hooks and natives run on the VM's thread. Any C
  code (e.g. LVGL events) must call into the VM from that same thread.
* The CLI host tool is assumed to have the compiler enabled. Firmware may not.
* Sizes/benchmarks in README are **x86-64 host** numbers. Nothing was measured on ARM or
  on the real SF32LB525 (there was no ARM toolchain or hardware in the sandbox).

---

## 7. Known issues / limitations

Language (not supported): `ref`/`out` (so no `TryParse`/`TryGetValue`; use
`GetValueOrDefault`), named arguments, tuples, `yield`, `async/await`, `goto`,
multi-dimensional arrays, `^` index-from-end, ranges, `when` in `case` labels (it works
in switch *expressions*), reflection, `unsafe`, records only partially (parsed as
classes), inheriting from built-in collections, delegate type declarations (ignored).

Behaviour differences:
* Enums print as numbers. LINQ is eager and returns `List`. `GroupBy` returns a
  Dictionary of lists (`g.Key`, `g.Value`).
* The value of a setter expression is null (`x = obj.Prop = 5` is wrong).
* Local functions must be declared before they are used.
* `Split` with an int argument is treated as options.
* Top-level `using var x = ...` is a plain declaration: **Dispose is not called** at the
  top level (it is inside blocks and methods).
* `using var` inside a `switch` section stops collecting statements at the next `case`.
* Stack trace for the synthesized `<main>` frame shows the class/Main declaration line.
* Missing-`;` error is reported at the next token, not at the end of the previous line.
* Member existence is checked at **runtime** (`MissingMemberException`), not at compile
  time.

Implementation:
* `g_multi_spec` in `mcs_lib_coll.c` is a **static global** used during a ThenBy sort. It
  is not reentrant across several VMs or threads. Moving it into the VM would be a small,
  safe change if needed.
* ROM lookup misses are not cached. Repeated failed lookups (e.g. getter checked before
  method) rescan the ROM tables. This is fine today and a possible optimization.
* `MCS_MAX_PINS` = 16 (now `#ifndef`-configurable), `MCS_MAX_ROOTS` = 32,
  `MCS_MAX_HANDLERS` = 32. `mcs_push_root` panics on overflow.
* Compiling uses RAM: tokens and the AST (`node_t` ≈ 104 B on 64-bit) live until the
  compile ends. For example, a ~30-line file needs ~100 KB peak on the host. On-device
  compiling of large files needs a big heap; prefer images.
* Baseline RAM with the full stdlib and a 256-slot stack: ~46 KB on a 64-bit host.
  Breakdown: class tables ~10 KB, class structs ~9 KB, intern table ~8 KB, global index
  ~4 KB, stack 4 KB. Not measured on 32-bit.
* `MCS_HOOK_INTERVAL` = 1000 backward jumps/calls. Short scripts may never call the hook
  (the firmware demo shows "watchdog fed 0 times").
* The `asan` Makefile target exists but has never run (no libasan available).
* `make size` used the host compiler, because `arm-none-eabi-gcc` was not available.

---

## 8. Things that must NOT be changed (without a deliberate migration)

1. **Public API in `include/mcs.h` and `mcs_bind.h`**: signatures, `mcs_reg_t` layout,
   `mcs_class_def_t` layout, `mcs_value_t` layout, `mcs_result_t` values. User firmware
   depends on them. Add new functions; do not alter existing ones.
2. **Image format v1** and `IMG_VERSION`. Any change to the opcode set/numbering
   (`MCS_OPCODES` order), operand widths, or function record layout **must bump
   `IMG_VERSION`**. The loader rejects other versions. Images live in customer flash.
3. **Opcode order in `MCS_OPCODES`.** Append only (it affects images and the
   computed-goto table).
4. **GC invariant: no collection inside `mcs_realloc`.** Collect only at
   `SAFEPOINT`/`GCPOINT` instruction boundaries. Any new GC root (VM fields holding
   values) must be added to `mark_roots` in `mcs_object.c`.
5. **`mcs_reg_t` tables must stay `static const`** (lazy ROM lookup keeps the pointers).
   Use `mcs_add_regs_eager` for stack-allocated tables (as `small_int_class` does).
6. **Field names `Message`, `StackTrace`, `InnerException`** on `Exception`, and the
   exception class names in `open_exceptions`. The VM and `mcs_raise` look them up by
   name.
7. **Interning invariant**: all `mcs_string_t` are interned. Code compares names by
   pointer (`mcs_table_get_s`).
8. **Pre-interned name range** `s_ctor … s_equals` and builtin class range
   `cls_object … cls_delegate` in `struct mcs_vm`. `mark_roots` walks them as contiguous
   pointer ranges, so add new ones *inside* those ranges or mark them explicitly.
9. **Test expectations** (`tests/*.out`) define the behaviour. If a fix changes output,
   update `.out` only after checking the new output is what real C# would print.
10. The CLI feature stubs in `ports/unix/main.c` keep every flag combination building.
    Keep that working.

---

## 9. What Phase 2 needs to know / likely work

Probably requested next:
* **RT‑Thread port** (it was written and removed; re-create under `ports/rtthread/`):
  * msh command `mcs <file>|-e code|REPL`, with `write_fn` → `rt_kprintf` (chunked),
    `ticks` → `rt_tick_get_millisecond`, `delay` → `rt_thread_mdelay`, and
    `hook` → `rt_thread_yield` + watchdog.
  * Pool heap with an optional linker section (PSRAM on SF32LB52x), DFS file reading,
    `RT_USING_POSIX_STDIO` getchar REPL.
  * SConscript with `MCS_FLOAT_DOUBLE=0`, `MCS_ENABLE_DISASM=0`, plus Kconfig
    `PKG_USING_MICROCS`.
* **LVGL binding** (also written and removed):
  * A `Widget` native class holding an `lv_obj_t*` (the GC does not delete the LVGL
    object; `LV_EVENT_DELETE` clears the handle).
  * `Lv.Screen/Label/Button/Slider/Panel`, `Text`/`Value` properties, `Align`,
    `OnClick/OnChange/OnEvent`.
  * Callbacks are stored in one pinned `List`, and the index is passed as the LVGL event
    user_data.
  * Compatibility macros for v8 and v9 (`lv_scr_act`/`lv_screen_active`,
    `lv_btn_create`/`lv_button_create`, `lv_obj_del`/`lv_obj_delete`).
  * `mcs_new_object` was added to `mcs.h` for this purpose and is kept.
* Measure flash/RAM/speed on the real ARM target (`make size SIZE_CC=arm-none-eabi-gcc`).

Good candidate improvements (keep them separate and testable):
* `out` parameters / `TryParse` / `TryGetValue` (needs a compiler and VM design. Simplest
  option: box the out-variable as an upvalue-like cell).
* Tuples, named arguments, `when` in case labels, top-level `using var` Dispose.
* Compile-time member checks for script classes.
* Lower RAM: smaller table entries, a smaller initial intern table, sizing `mcs_class_t`
  per kind, caching ROM misses.
* `long` as a real 64-bit type while `int` stays 32-bit (needs a value tag or a per-op
  width).
* Make `g_multi_spec` per-VM.

How to work on this code:
* Rebuild with `make` (header dependencies are in the Makefile). If behaviour looks
  impossible, run `make clean` first: stale objects after struct changes caused crashes
  during Phase 1.
* Debugging crashes: there is no gdb/valgrind/asan in the sandbox. Phase 1 used a helper
  that builds with `-g -rdynamic -no-pie` plus a SIGSEGV handler calling `backtrace()` and
  `addr2line`. That helper was in `/tmp` and is not in the zip; recreate it if needed.
* Adding a library method: add an entry to the right `static const mcs_reg_t[]` table.
  Repeated names in the same table become overloads, and the arity must be exact or -1.
  Natives that call back into the script must root their temporaries.
* Adding a global VM value: add it to `struct mcs_vm`, zero-init (memset), mark it in
  `mark_roots`.
* New syntax: parser → (maybe desugar to existing nodes) → compiler. Prefer desugaring
  (as `using` does), so no new opcodes are needed and the image format stays v1.

---

## 10. Verification checklist (run after every change)

```sh
make clean && make                       # must be warning-free (-Wall -Wextra)
make test                                # 15 passed, 0 failed (source + image + errors)
make example                             # firmware demo runs, prints stats
# GC torture: every test must still match its .out
gcc -std=gnu99 -O1 -Iinclude -DMCS_GC_STRESS=1 src/*.c ports/unix/main.c -lm -o /tmp/mcsgc
cd tests && for t in t0*.cs; do /tmp/mcsgc $t > /tmp/o.txt 2>&1; cmp -s /tmp/o.txt ${t%.cs}.out && echo PASS $t || echo FAIL $t; done
# feature-flag builds must compile without warnings
for f in -DMCS_FLOAT_DOUBLE=0 -DMCS_ENABLE_FLOAT=0 -DMCS_ENABLE_COMPILER=0 \
         "-DMCS_ENABLE_DICT=0 -DMCS_ENABLE_LIST=0" -DMCS_LAZY_REGS=0 -DMCS_COMPUTED_GOTO=0 \
         "-DMCS_ENABLE_DISASM=0 -DMCS_ENABLE_LINES=0 -DMCS_ENABLE_BYTECODE_SAVE=0"; do
  gcc -std=gnu99 -Wall -Wextra -Iinclude $f src/*.c ports/unix/main.c -lm -o /tmp/v || echo "BUILD FAIL $f"; done
```
Note: `t06_gc_stress` runs with `--heap 196608 --stack 256` in `run_tests.sh`. Source
mode needs ~148 KB because of compile memory; image mode needs ~125 KB.

## 11. References used for the design
* .NET nanoFramework architecture — https://docs.nanoframework.net/content/architecture/index.html
* MicroPython native modules / .mpy — https://docs.micropython.org/en/latest/develop/natmod.html
* Wren embedding API — https://wren.io/embedding
* Crafting Interpreters (clox): upvalues, string interning, mark-sweep GC
