# Embedding MicroCS

## 1. Create a VM
```c
mcs_config_t cfg; mcs_config_default(&cfg);
static uint8_t heap[160 * 1024]; static mcs_pool_t pool;
mcs_pool_init(&pool, heap, sizeof heap);
cfg.realloc_fn = mcs_pool_realloc; cfg.alloc_ud = &pool;   /* or NULL = malloc */
cfg.write_fn = uart_write;     /* Console output            */
cfg.ticks_fn = millis;         /* Environment.TickCount, limits, scheduler */
cfg.delay_fn = delay_ms;       /* Thread.Sleep              */
cfg.hook_fn  = poll_hook;      /* called at safepoints; return non-zero to abort */
cfg.heap_limit = sizeof heap - 2048;   /* GC is tuned to stay below this */
cfg.stack_slots = 256; cfg.max_frames = 48;
cfg.stdlib = MCS_LIB_ALL;      /* or MCS_LIB_CORE | MCS_LIB_COLLECTIONS ... */
mcs_vm_t* vm = mcs_new(&cfg);
```
Measured heap right after `mcs_new` (emulated Cortex-M0, 1 KB = 1024 B): **25.1 KB** with
the default config and the full stdlib, **13.0 KB** with the lowram profile,
`MCS_LIB_CORE | MCS_LIB_COLLECTIONS` and no FS/HAL/scheduler modules. A 64-bit host needs
about 44 KB for the default config. Budget for it, then add the script's live data; small
targets: [LOW_RESOURCE.md](LOW_RESOURCE.md).

## 2. Run code
| Call | Input |
|---|---|
| `mcs_exec_source(vm, name, src)` | C# source (needs `MCS_ENABLE_COMPILER`) |
| `mcs_exec_image(vm, img, len)` | `.mcsb` image, e.g. a const array in flash (code is copied to the heap) |
| `mcs_exec_image_xip(vm, img, len)` | same, but bytecode is executed in place: `img` must stay valid until `mcs_free` |
| `mcs_exec_auto(vm, name, buf, len)` | either, detected by the image magic |
| `mcs_exec_file(vm, vfs, path)` | file from the VFS (honours NOEXEC mounts) |
| `mcs_call(vm, "App.Loop", argc, argv, &res)` | call a script function from C |

Results: `MCS_OK`, `MCS_ERR_COMPILE`, `MCS_ERR_RUNTIME` (uncaught exception),
`MCS_ERR_MEMORY`, `MCS_ERR_BYTECODE`, `MCS_ERR_ABORTED`. The message is in `mcs_last_error(vm)`.

## 3. Limits and abort
```c
mcs_limits_t lim = { .time_ms = 500, .steps = 1000000 };
mcs_set_limits(vm, &lim);       /* per top-level run; 0 = unlimited */
if (r == MCS_ERR_ABORTED) switch (mcs_abort_reason(vm)) { case MCS_ABORT_TIME: ... }
```
Budgets are checked at safepoints (every `MCS_HOOK_INTERVAL` backward jumps/calls), so a
tight loop is stopped within one interval; straight-line code has no overhead.
`MCS_ABORT_REQUEST` comes from `mcs_request_abort(vm)` (callable from an ISR or the hook), `MCS_ABORT_HOOK` from the hook.

## 4. Modules
```c
#include "mcs_vfs.h"  /* mcs_vfs_init, mcs_vfs_mount, mcs_fs_open_lib  */
#include "mcs_hal.h"  /* mcs_hal_open_lib                               */
#include "mcs_sched.h"/* mcs_sched_init, mcs_sched_open_lib, mcs_sched_poll */
#include "mcs_shell.h"/* mcs_shell_init, mcs_shell_boot, mcs_shell_run  */
```
The structs (`mcs_vfs_t`, `mcs_hal_t`, `mcs_sched_t`, `mcs_shell_t`) are owned by the host
and must outlive the VM. `ports/cortex-m/main.c` and `ports/unix/main.c` show complete
wiring. Leaving a module out: do not call its `*_open_lib`, or build with its flag = 0.

## 5. Native functions
```c
static mcs_value_t led(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    hal_led((int)mcs_to_int(vm, argv[0])); return mcs_null();
}
static const mcs_reg_t board[] = { MCS_FN("Led", led, 1), MCS_REG_END };
mcs_register_module(vm, "Board", board);           /* C#: Board.Led(1); */
```
`mcs_bind.h` has one-line wrappers (`MCS_WRAP_V_I(w_led, hal_led)`). Raise errors with
`mcs_raise(vm, "IOException", ...)` (any built-in or script exception class name); protect
temporaries with `mcs_push_root/mcs_pop_root` if you call back into the VM; keep script
values in C with `mcs_pin/mcs_unpin`. Register tables must be `static const`.

## 6. Build profiles
`-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_mcu.h"'` (or `lowram`, `tiny`, `embedded`,
`linux`) selects a coherent set of `MCS_ENABLE_*` flags; any flag can still be overridden
with `-D`. All five profiles are built with `-Werror` by `make check`.

| Profile | For | Compiler | Notes |
|---|---|:---:|---|
| `linux` | hosts | ✅ | 64-bit ints, big stacks |
| `embedded` | ≥ 256 KB RAM (e.g. SF32LB525) | ✅ | shell, FS, HAL, scheduler |
| `mcu` | 64–160 KB RAM | — | images only, all modules |
| `lowram` | 24–64 KB RAM | — | single floats (8-byte values), small limits, 4-byte pool alignment |
| `tiny` | smallest flash | — | no float, no `Dictionary`, no modules |

A complete small-RAM integration is in [examples/lowram/](../examples/lowram/).
