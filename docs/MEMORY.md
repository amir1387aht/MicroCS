# Memory budgets and Studio reports

## What the counters mean

`mem` (machine shell) and `.mem` (REPL) report snapshots without forcing a GC.
The original `heap ... bytes in use, peak ...` line is retained for older clients.
New firmware also reports:

- `vm_limit`: the VM allocator's logical allocation limit, not physical capacity.
- `pool`: actual aligned fixed-pool capacity, occupied bytes including block
  headers/alignment, successful-allocation high-water mark, total free bytes and
  largest allocatable contiguous payload. This includes the VM allocation itself,
  collector scratch, runtime VFS source/read buffers, RAMFS and thread heaps when
  they share the pool. Free bytes alone do not guarantee a large allocation fits.
- `device_heap`: optional board HAL telemetry. ESP32 reports the ESP-IDF
  `MALLOC_CAP_8BIT` heap: allocated + free capacity, allocated, free, SDK minimum
  free and largest free block. It includes SDK/drivers/task allocations and the
  **entire reserved VM pool**, even when much of that pool is unused. Do not add
  device usage to pool usage. It is not total chip SRAM: static data and memory
  outside that capability heap are not measured. SDK minimum-free aggregates
  regional low-water figures; it is not a synchronized whole-device peak.

Studio shows these scopes separately, with exact bytes and unavailable labels
for older/custom firmware. The snapshot is refreshed after workflow completion
(including failed builds/OOM), explicit shell commands and the storage Refresh
button. It is not a live foreground-task profiler. Rebuild firmware and refresh
Studio to get the new protocol; no device capacity is inferred from a VM peak.

## A hard 40 KiB application pool

The 32-bit `oled-budget` CI job runs the representative two-file source Project
in `tests/projects/oled_menu` with 8-byte numeric alignment, real U8g2, a simulated 128x32 SSD1306, three
fonts, source checks, 20 Project restarts and GPIO redraw callbacks. It uses:

```c
static uint8_t heap[40 * 1024];
mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
cfg.heap = heap;
cfg.heap_size = sizeof heap;
cfg.stack_slots = 128;
cfg.max_frames = 16;
```

These are explicit application resource limits, **not** a reduced build profile.
Compiler, libraries, display/fonts, diagnostics and numeric precision are not
removed. Public stack/frame defaults are unchanged. Deeper/larger applications
may require larger limits and more RAM; 40 KiB is not a guarantee for arbitrary
C# source, the user's unavailable original scripts, or all firmware RAM.
The gate counts source/read buffers in the physical pool and checks that shutdown
returns its usage to zero. It does not measure C call stacks, the static runtime
struct, native SDK allocations or actual ESP32 hardware behavior.

For the ESP-IDF **example** firmware set these menuconfig options under MicroCS:

```text
CONFIG_MICROCS_VM_HEAP_KB=40
CONFIG_MICROCS_VM_STACK_SLOTS=128
CONFIG_MICROCS_VM_MAX_FRAMES=16
```

Use a flash filesystem for this budget: a RAM filesystem consumes the same pool.
Example defaults stay 192 KiB / 1024 slots / 64 frames for compatibility. The
configured pool is a ceiling; preserving driver reserves may make it smaller.
Startup prints the actual allocated pool and ceiling and fails closed if it
cannot allocate a usable pool. A failed pool must never silently enable malloc.
Custom `app_main` code must set the same runtime fields and check `heap != NULL`
before starting the runtime; example menuconfig settings do not modify custom
entrypoints. Rebuild after changing them. Wi-Fi/BLE reserves and task stacks
remain additional device costs, not part of the 40 KiB VM pool.

## Where the reductions come from

- Tokens store source spans rather than redundant numeric payloads and use
  indexed 32-record pages. Declaration boundaries release consumed pages, so
  parsing does not require a large contiguous token array or keep every token
  beside the full AST. Interpolation and speculative lookahead keep valid tokens.
- AST leaves omit children, mutually exclusive payloads share storage, and arenas
  use natural numeric alignment and reuse small tails. Language precision remains
  selected by the existing build configuration.
- Completed methods release bytecode/constant/line-buffer slack before their
  siblings compile; growth can fall back to exact required capacity.
- Idle compilation temporarily returns large value-stack and call-frame
  reservations and disposable field/method caches. Configured execution capacity
  is restored before execution; live/reentrant stacks are never moved. Small
  reservations stay in place to avoid restoration fragmentation.
- Internal symbol tables use string pointers instead of full tagged keys. User
  Dictionary semantics are unchanged. Optional inline caches fail gracefully
  under memory pressure. Pool best-fit placement preserves larger holes.

Memory exhaustion is still a valid bounded-resource error, not evidence that
all free bytes were usable. Compare pool free **and largest block**, the requested
allocation, and device heap separately when diagnosing it.
