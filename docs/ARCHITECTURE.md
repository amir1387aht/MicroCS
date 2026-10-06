# Architecture

<p align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/architecture-dark.svg">
  <img alt="MicroCS architecture diagram" src="../assets/architecture-light.svg" width="900">
</picture>
</p>

<details><summary>Detailed diagram (Mermaid source)</summary>

```mermaid
flowchart TB
    subgraph front["Front end — optional (MCS_ENABLE_COMPILER)"]
        direction LR
        L[lexer] --> P["parser<br/>AST in arena<br/>desugaring"] --> C["single-pass<br/>compiler"]
    end
    IMG[".mcsb image"] --> LD["loader +<br/>validator"]
    C --> F(("functions<br/>+ constants"))
    LD --> F
    F --> VM["VM: value stack, frames,<br/>computed-goto dispatch,<br/>inline caches"]
    VM <--> GC["precise mark & sweep<br/>(safepoints only)"]
    VM <--> STD["stdlib natives"]
    VM <--> API["public API include/mcs.h<br/>+ extension slots"]
    API --- FS[modules/fs] & HAL[modules/hal] & SCH[modules/sched] & SH[modules/shell] & RT[modules/runtime]
```

</details>

## Core (`src/`)
* **Lexer → parser → compiler.** The lexer produces a token array on the VM heap; the parser
  builds an AST in an arena and the token array is **freed as soon as parsing ends** (the
  AST points into the source text). Many newer features are *desugared* in the parser into
  calls of a hidden `__rt` module (tuples, deconstruction, `^`/ranges) or into existing
  nodes (`is` patterns → switch expressions), so the compiler and VM stay small. The
  compiler is single-pass per function, emits stack bytecode with line tables and recycles
  function-compiler states. The arena is freed after compilation, also on out-of-memory.
* **Peephole optimisation** happens at emit time: a store followed by `POP` becomes
  `SET_LOCAL_POP`/`SET_GLOBAL_POP`; conditions in `if`/`while`/`for`/`?:` that are a
  comparison become one fused `JF_*` compare-and-branch. Jump patching invalidates the
  peephole state so no rewrite crosses a branch target.
* **VM.** Value stack + call frames, computed-goto dispatch, exception handler stack,
  upvalues for closures. Hot paths: int/float arithmetic and comparisons, exact-arity
  closure calls, positive-divisor `/` `%`, and **per-site field caches** (`MCS_FIELD_CACHE`:
  each function has a `(class, slot)` cache indexed by the field-name constant; the GC marks
  the cached classes). *Safepoints* (calls, backward jumps, allocating opcodes) are the only
  places where the GC runs and where the host hook and execution limits are checked.
* **GC.** Precise mark & sweep. Allocation never collects; it only requests a collection
  that the next safepoint performs, so native code can hold unrooted temporaries. The weak
  string-intern table is rehashed in place when it fills with tombstones and shrunk after a
  collection when it is mostly empty.
* **Images.** `.mcsb` = serialised functions/constants with global names re-linked at load
  time; versioned (`IMG_VERSION` 2, v1 still accepted) and validated. See [BYTECODE.md](BYTECODE.md).
  With **execute in place** (`mcs_exec_image_xip`) a function's `code` points into the image
  buffer; such functions are flagged `FN_XIP` (a runtime-only flag, masked out of image
  flags) and carry a `gmap` (image global index → VM global slot) that `GET/SET_GLOBAL`
  go through, because the image's global indices cannot be patched in flash.

## Memory layout (what costs RAM)
* **Values** are a type tag plus an 8- or 4-byte payload. With `MCS_COMPACT_VALUES` (default
  on 32-bit) the struct is packed to 4-byte alignment: 12 bytes with a double/int64 payload,
  8 bytes with single floats and 32-bit ints. See [LOW_RESOURCE.md](LOW_RESOURCE.md).
* **Strings** are always interned. The intern set (`mcs_strset_t`) is weak and holds one
  pointer per slot; the GC sweep turns dead entries into tombstones. A string that names a
  global stores the global's slot + 1 in `obj.aux` (padding in the object header), so
  name → slot lookups need no separate table.
* **Classes**: built-in exception classes share their base's field layout
  (`layout_shared`) until a subclass adds a field (copy on write). Member tables start at
  `MCS_TABLE_MIN_CAP` entries; native members are materialised lazily from ROM tables.
* **Dictionary/HashSet**: insertion-ordered `keys[]`/`vals[]` arrays plus an open-addressing
  index of positions whose slot width (1, 2 or 4 bytes) follows the index capacity;
  `HashSet` storage is keys-only (`DICT_KEYS_ONLY` in `obj.aux`). `Remove` keeps insertion
  order by shifting the arrays and renumbering index slots.
* **Compiler**: tokens are 24 bytes and AST nodes keep literal payloads in a union; the
  token array grows by the observed token density and is freed after parsing.
* **Library.** Builtin classes are described by `mcs_reg_t` tables; with `MCS_LAZY_REGS`
  a class's natives are only materialised on first use. With `MCS_LAZY_CLASSES` (default)
  the classes themselves — built-in exceptions, collections, `Math`, and the classes that
  modules register — are created the first time a script or C code names them: the global
  slot holds a lazy marker until then, constants live in ROM (`mcs_const_t`,
  `mcs_register_consts`), and a VM starts in 1.7–8 KB on a 32-bit MCU. Optional parts of the
  library (`MCS_ENABLE_STRING_EXTRA`, `MCS_ENABLE_ARRAY_EXTRA`, `MCS_ENABLE_STACK_QUEUE`,
  `MCS_ENABLE_CONVERT`, `MCS_ENABLE_DIAGNOSTICS`, …) are compile-time switches; the `min`
  profile turns them off to fit 64 KB of flash. [STDLIB.md](STDLIB.md) is generated
  from these tables.

## Phase 2 core additions (all append-only to `mcs.h`)
| API | Purpose |
|---|---|
| `mcs_set_ext / mcs_get_ext` | per-VM slots (VFS, HAL, SCHED, SHELL, 2 user) so modules need no globals |
| `mcs_set_limits`, `mcs_abort_reason`, `mcs_steps_used` | time/step budgets per top-level run |
| `mcs_fail(vm, code, fmt, ...)` | hosts/modules record + report an error outside script execution |
| `mcs_exec_auto`, `mcs_is_image` | run a buffer that is either source or an image |
| `mcs_features()` | runtime query of compiled-in features (`MCS_FEAT_*`) |

## Modules (modules/)
Modules include only public headers, are wrapped in their `MCS_ENABLE_*` flag and are
optional at link time. Each one registers C# classes with `mcs_register_module`/
`mcs_define_class` and stores its context in an extension slot.

* `fs`: `mcs_vfs_t` mount table (≤4 mounts, longest prefix, RDONLY/NOEXEC flags, path
  normalisation that cannot escape `/`), backends RAM (quota), POSIX (host directory),
  LittleFS and YAFFS2 (optional, not bundled); C# `File`, `Directory`, `Path`. The raw flash
  layer `mcs_flash_t` (`include/mcs_flash.h`) describes NOR/NAND chips, ships generic SPI
  NOR / SPI NAND drivers and adapts to `lfs_config` / `yaffs_dev`. See FILESYSTEM.md.
* `hal`: one `mcs_hal_t` function table per board; NULL entries hide the C# class.
  ISR-safe event ring + callback dispatch at safe points, simulator backend for tests.
  Vendor bindings live in `ports/` (STM32, ESP32, RP2, Zephyr, Arduino). See HAL.md.
* `sched`: fixed table of jobs (file or delegate), polled by the host. See SCHEDULER.md.
* `shell`: boot sequence, interactive C# REPL and line protocol over any byte transport.
* `runtime`: wires VM + pool heap + filesystem + HAL + scheduler + shell into a complete
  firmware (`mcs_runtime_run`). See STANDALONE.md.

## Threading model
One VM = one thread. The scheduler and shell never create threads; the host drives them
from its loop or RTOS task. Several VMs may exist in one process (state is per VM; the
only process-wide state is the LittleFS file-handle table and C caches documented there).

## Invariants for contributors
* Do not change existing public structs/functions; append new API.
* Opcodes are append-only; bump `IMG_VERSION` if the image format changes, and extend the
  loader's operand validation for new operand kinds.
* Never trigger GC inside `mcs_realloc`; new VM roots must be marked in `mark_roots`.
* `.out` files define behaviour; new language features should match .NET output
  (`tools/verify_dotnet.sh`).
