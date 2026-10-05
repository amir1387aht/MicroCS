# Architecture

```
            C# source ──► lexer ─► parser (AST in an arena) ─► single-pass compiler ─┐
                                                       (MCS_ENABLE_COMPILER)          │
            .mcsb image (mcs -c / -C, or mcs_compile_image) ─────────────────────────┴─►
                     bytecode functions ─► stack VM (computed goto) ─► stdlib natives
                                             │  mark & sweep GC at safepoints
     public API include/mcs.h  ──────────────┴───── extension slots (mcs_set_ext)
         │                 │                     │                      │
   modules/fs        modules/hal           modules/sched          modules/shell
   VFS + backends    board ops table       polled job table       boot + protocol
```

## Core (src/) — Phase 1, extended compatibly
* **Lexer → parser → compiler**: tokens and AST live in one arena that is freed after
  compilation (also on out-of-memory, since Phase 2). The compiler is single-pass per
  function and emits stack bytecode with line tables. Function-compiler states (~4.5 KB
  each on 32-bit) are recycled between functions since Phase 2.
* **VM**: value stack + call frames, computed-goto dispatch, exception handler stack,
  upvalues for closures. *Safepoints* (calls, backward jumps, allocating opcodes) are the
  only places where the GC runs and where the host hook and execution limits are checked.
* **GC**: precise mark & sweep. Allocation never collects; it only requests a collection
  that the next safepoint performs, so native code can hold unrooted temporaries.
* **Images**: `.mcsb` = serialised functions/constants/classes, versioned (`IMG_VERSION`);
  Phase 2 added no opcodes, so images are unchanged.

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
  LittleFS (optional); C# `File`, `Directory`, `Path`. See FILESYSTEM.md.
* `hal`: one `mcs_hal_t` function table per board; NULL entries hide the C# class.
  Simulator backend for tests. See HAL.md.
* `sched`: fixed table of jobs (file or delegate), polled by the host. See SCHEDULER.md.
* `shell`: boot sequence and line protocol over any byte transport. See STANDALONE.md.

## Threading model
One VM = one thread. The scheduler and shell never create threads; the host drives them
from its loop or RTOS task. Several VMs may exist in one process (state is per VM; the
only process-wide state is the LittleFS file-handle table and C caches documented there).

## Invariants for contributors
* Do not change existing public structs/functions; append new API.
* Opcodes are append-only; bump `IMG_VERSION` if the image format changes.
* Never trigger GC inside `mcs_realloc`; new VM roots must be marked in `mark_roots`.
* `.out` files define behaviour; new language features should match .NET output
  (`tools/verify_dotnet.sh`).
