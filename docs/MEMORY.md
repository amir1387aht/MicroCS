# Memory budgets and Studio reports

## What the counters mean

`mem` (machine shell) and `.mem` (REPL) report snapshots without forcing a GC.
The original `heap ... bytes in use, peak ...` line is retained for older clients.
New firmware also reports:

- `vm_limit`: the VM allocator's logical allocation limit, not physical capacity.
- `stack` / `frames`: live entries, currently allocated capacity, peak live entries
  and configured maximum. Idle snapshots normally show zero live entries, not
  zero capacity. Capacity bytes are already included in the VM/pool totals.
- `pool`: actual currently committed, aligned pool capacity, occupied bytes including block
  headers/alignment, successful-allocation high-water mark, total free bytes and
  largest allocatable contiguous payload. This includes the VM allocation itself,
  collector scratch, runtime VFS source/read buffers, RAMFS and thread heaps when
  they share the pool. Free bytes alone do not guarantee a large allocation fits.
- `pool_backing`: optional automatic-pool backing bytes committed, configured
  backing-byte ceiling, and region count. Backing includes region descriptors and
  alignment padding, so it is slightly larger than usable `pool` capacity. The
  ceiling is not preallocated and is not a promise that native RAM is available.
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
// Leave stack_slots/max_frames at their defaults: these are ceilings.
```

Only the application pool is bounded; stack and frame limits use ordinary
profile defaults. This is **not** a reduced build profile.
Compiler, libraries, display/fonts, diagnostics and numeric precision are not
removed. Public stack/frame defaults are unchanged. Deeper/larger applications
may require more RAM and eventually larger safety ceilings; 40 KiB is not a guarantee for arbitrary
C# source or all firmware RAM.
The gate counts source/read buffers in the physical pool and checks that shutdown
returns its usage to zero. It does not measure C call stacks, the static runtime
struct, native SDK allocations or actual ESP32 hardware behavior.

For the ESP-IDF **example** firmware set these menuconfig options under MicroCS:

```text
CONFIG_MICROCS_VM_HEAP_KB=40
CONFIG_MICROCS_VM_STACK_SLOTS=0
CONFIG_MICROCS_VM_MAX_FRAMES=0
```

Use a flash filesystem for this budget: a RAM filesystem consumes the same pool.
The example keeps its 192 KiB ceiling and uses zero (profile defaults) for
stack/frame ceilings: 1024 / 64 with the default full profile, 512 / 64 with
the embedded profile. Existing profile-specific defaults are preserved. The
configured pool is a ceiling, not an upfront allocation. The ESP32 example uses
an automatic multi-region provider and prints the ceiling and native reserve at
startup. It requests internal byte-addressable blocks only when existing pool
space cannot satisfy a request. Each request preserves a 32 KiB base native
headroom, enabled Wi-Fi/BLE reserves, and an 8 KiB contiguous-block margin.
Allocator overhead is checked after acquisition too. These samples do not reserve
RAM against asynchronous SDK allocations; test actual peripheral peak workloads.
A failed growth request returns OOM; there is no unbounded fallback.
Custom `app_main` code must opt into the same provider (see below); menuconfig
settings alone do not change custom entrypoints. Wi-Fi/BLE reserves and task
stacks remain outside the VM pool. Static BLE buffers also consume static SRAM.

## Automatic bounded regions (all ports)

On malloc-backed targets, no region addresses are required:

```c
mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
cfg.heap = NULL;
cfg.heap_size = 192 * 1024;  /* cap on the SUM of backing allocations */
/* configure transport, time, filesystem and board as usual */
```

This selects the portable malloc/free region provider, not unbounded per-object
malloc. Existing static-buffer configurations remain valid and unchanged. A
bare-metal target with no allocator must keep its static buffer or supply a safe
provider; MicroCS never probes SRAM addresses or assumes linker-reserved memory
is free. The generic C allocator cannot query native-driver headroom: on such
ports choose a conservative cap or supply a reserve-aware provider.

ESP32 applications should use the provider already wired into the example:

```c
static mcs_esp32_heap_t backing; /* mcs_port_esp32.h */
mcs_esp32_heap_init(&backing, native_reserve_bytes);
cfg.heap = NULL;
cfg.heap_size = 192 * 1024;
cfg.heap_provider = &backing.provider;
```

`native_reserve_bytes` includes base headroom and configured radio reserves.
The ESP32 provider uses `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, not DMA-only,
instruction-only or automatically selected PSRAM. PSRAM policy needs an explicit
capability-aware provider and validation of native users' requirements.

`MCS_ENABLE_POOL_REGIONS` defaults to `MCS_ENABLE_MALLOC`; allocator-free boards
using a custom provider must enable it explicitly. Constrained static-only
reference builds can disable it without changing the static pool or language.

The portable `mcs_pool_provider_t` has acquire/release callbacks. Acquire receives
minimum/preferred backing sizes and returns the actual size. It may choose a
smaller block that still satisfies the minimum; the core checks its bounds.
Callbacks and context must outlive the runtime, must return distinct owned blocks,
and must not re-enter the VM. Calls use the runtime's existing shared-pool lock
when workers are active. `mcs_pool_init_dynamic()` also supports direct C users.

Growth prefers 16 KiB chunks, increasing only to fit a larger individual request.
Both persistent and temporary allocations share the same bounded free list.
There is no recursive GC inside allocation: existing VM safepoints retain their
rooting guarantees. Realloc may move/copy a block between regions; failed growth
preserves the old allocation. Every single allocation must fit within ONE region.
The cap includes region metadata/alignment and live RAMFS/worker allocations;
thread stacks and SDK allocations remain separate costs.

Entirely empty owned regions are released at application replacement, after
workers stop and the old VM is freed; live RAM filesystem regions remain. Direct
users can call `mcs_pool_trim()` at an idle/synchronized boundary. Shutdown frees
all owned regions after all clients are destroyed. There is no periodic scan,
per-free region scan, moving collection, raw-address discovery, or promise of zero
allocation-path overhead. Static buffers are never freed by the runtime.

`make firmware-heap-test` covers automatic backing caps, cross-region realloc,
misalignment, adjacent regions that must not merge, randomized allocation/OOM,
repeated runtime replacement with persistent files, cold-start fault injection,
shutdown release, and the ESP32 reserve-aware provider with a fake SDK allocator.

## Automatic application replacement on Run

Studio Run now asks the complete runtime to unload the previous application
before checking its replacement. Shell `run` and `project` also replace the old
application automatically. This releases timer/GPIO callbacks, encoder pin
reservations, scheduled work, native display resources and all old class/global
references—not just the top-level app variable. Files, RAM filesystem contents,
serial transport/baud and the fixed physical pool survive. It is not an MCU reset.
See [application lifecycle](STANDALONE.md#application-replacement-versus-compile-checks).

`tests/projects/oled_encoder_repeat` captures the updated settings/encoder app.
`test_oled_app_replace` performs **2,000 upload/newapp/check/run cycles** with real
U8g2 and the three fonts in the same **64 KiB, 32-bit pool**. The script contains no
manual Dispose/Stop/global clearing. Checks verify unchanged-source reruns,
released encoder pins/timers, non-destructive standalone checks and zero pool use
on shutdown. `test_app_replace` additionally covers RAM-file preservation,
queued protocol commands, restored native bindings and OOM/retry recovery.
These are host simulations, not physical ESP32 validation.

## Rebuilding while the previous application is live

A first run is not a sufficient memory test. GPIO and hardware-timer callbacks
can keep the previous app instance, methods and display alive after its top-level
script finishes. A compile-only check must preserve those callbacks and must not
execute the replacement script to reclaim the old state.

`tests/projects/oled_repeat` contains the larger reported menu application,
including file enumeration and both callback types. The separate
`oled-repeat-test` CI step uses a **64 KiB 32-bit application pool**, real U8g2 and
all three selected fonts. It uploads both files, changes an actual string literal
and source size, checks them, starts the Project, and exercises GPIO/timer callbacks
for **2,000 cycles in the same VM**, with **7,500 extra pool-allocated bytes** kept
live. A short whitespace-only run is not sufficient to expose late fragmentation. Checks assert that no display/timer side effects occur during a build,
that the prior app is not replaced, and that warm retained usage stays bounded.
Shutdown must return pool usage to zero. This is a host simulation, not a physical
ESP32 measurement; it does not claim this larger app fits the smaller 40 KiB gate.

The compiler retains class/member signatures for resolution but defers ordinary
method, constructor and operator bodies until their bytecode is emitted. Each
body's syntax tree is then released before the next method. This reduces both
peak overlap with the live app and long-lived allocations interleaved with
short-lived syntax trees. Full parsing, line/column diagnostics, nested closures,
default arguments and named-tuple information remain available. Failed builds
and allocator failures release the body workspace without stopping old callbacks.

Short-lived source buffers, dependency traversal lists, tokens and compiler arenas
use the high end of the **same physical pool**; retained VM objects use low-address
first-fit placement. This separates lifetimes instead of scattering retained
bytecode among syntax buffers. There is no second heap, hidden malloc fallback or
fixed scratch reservation. All VM compiler allocations still count against the VM
allocation limit; VFS scratch and per-block headers still count in physical pool
usage. Idle file entry collects unreachable prior-run objects before its first
source read, rather than waiting until the compiler starts.

`mcs_config_t.temporary_realloc_fn` optionally supplies the compiler-scratch
allocator, sharing `alloc_ud` and the configured overhead/accounting convention.
It defaults to the ordinary allocator for custom allocators; `mcs_new()` selects
`mcs_pool_realloc_temporary` when the ordinary allocator is `mcs_pool_realloc`.
Wrappers around the pool may set it explicitly. The complete firmware runtime
uses synchronized wrappers for both allocation directions when threads are active;
worker VMs use their own pools and high-end VFS scratch.

An OOM snapshot is taken after compiler cleanup. Free bytes and largest free block
in that snapshot are not necessarily what was available at the failed request.
The regression verifies callback preservation, bounded warm usage, complete pool
recovery at shutdown, and mixed-direction realloc/OOM cleanup.

## Where the reductions come from

- Tokens store source spans rather than redundant numeric payloads and use
  indexed 32-record pages. Each token is 12 bytes on 32-bit targets rather than
  16; parser error columns are recovered from the original source with string
  prefixes and the existing 4,095-column cap preserved. Declaration boundaries release consumed pages, so
  parsing does not require a large contiguous token array or keep every token
  beside the full AST. Interpolation and speculative lookahead keep valid tokens.
- AST leaves omit children, mutually exclusive payloads share storage, and arenas
  use natural numeric alignment and reuse small tails. Language precision remains
  selected by the existing build configuration.
- Completed methods release bytecode/constant/line-buffer slack before their
  siblings compile; growth can fall back to exact required capacity.
- Value slots start at 32 (or a smaller configured maximum), and call frames
  start at one. Both grow geometrically only when needed, bounded by the existing
  profile/configured maxima. Optional growth slack falls back to exact demand if
  it cannot fit. Deep expressions check their pushes too; they do not rely on an
  estimated fixed operand margin. No manual sizing/rebuild loop is needed for
  normal execution within the default ceilings.
- Live growth rebases frames, open captures and all suspended interpreter views.
  Native arguments stay in separately rooted, stable pool blocks throughout
  callbacks, including constructors. Those blocks are counted in memory usage
  and cleaned up after OOM unwinding; they are not moved to an uncounted C VLA.
- Idle compilation trims execution capacity back to 32 slots / one frame and
  discards disposable field/method caches; execution never eagerly restores the
  maximum. Capacity is retained during execution to avoid shrink/grow churn.
- Internal symbol tables use string pointers instead of full tagged keys. User
  Dictionary semantics are unchanged. Optional inline caches fail gracefully
  under memory pressure. Pool best-fit placement preserves larger holes.

Memory exhaustion is still a valid bounded-resource error, not evidence that
all free bytes were usable. Compare pool free **and largest block**, the requested
allocation, and device heap separately when diagnosing it.

## Limits are safety ceilings, not promises of free memory

The full profile still permits up to 1024 value slots and 64 call frames;
the embedded profile permits 512 / 64. Increasing demand consumes additional
pool RAM automatically. Reaching a configured execution ceiling raises a
catchable `StackOverflowException`; exhausting the allocator reports an OOM and
unwinds safely. Dynamic storage cannot guarantee that an arbitrarily large
application fits a 40 KiB pool. Studio reports observed peaks so an exceptional
need to raise a safety ceiling is visible, not something users must guess.

## Automatic recovery of empty regions under allocation pressure

Free bytes across regions are not one contiguous allocation. For example, a
58,332-byte backing pool can have about 49 KiB free but a largest free block of
only about 16 KiB. A new 32 KiB request cannot span those regions, and a native
reserve may prevent acquiring more backing while the empty regions remain held.

The dynamic pool now first tries its existing free blocks and normal growth.
If growth fails, it returns **wholly empty** regions to their owning provider
and retries acquisition once for the requested size. This can recycle multiple
small cached regions into a larger native allocation, provided the native
allocator can supply it. The provider still enforces its reserve, contiguous
margin and total backing ceiling. Free capacity/committed region counts may fall
following a failed recovery because the empty regions were returned to native
RAM; live data and pointers remain valid.

There is no GC, relocation of unrelated live objects, or merging across native
allocation ownership boundaries. Normal successful allocations/frees retain
regions as before; recovery scans/releases happen only on the failed-growth
path. A region containing even one live allocation cannot be released. Native
fragmentation, live-region fragmentation and genuine RAM exhaustion can still
cause OOM; this is not a promise that every free byte can satisfy every object.

The ESP32 provider regression reconstructs four regions totalling 58,332 bytes,
then exercises a 32 KiB persistent or temporary request with 41,900 native bytes
free against a 41 KiB reserve. With an idealized recoverable native allocator,
it releases three empty regions and succeeds without touching a 9,000-byte live
allocation or lowering the reserve. Forced native allocation failure checks that
recovery terminates and the live data and remaining allocator still work.
A separate cap-limited moving-realloc regression checks preservation of the old
contents. The tests run on the host; native fragmentation on physical hardware
still needs validation. The old allocator fails the new recovery regression.

### Strict full-capacity and native-fragmentation checks

`test_pool_pressure` executes the production pool and ESP32 provider over a
bounded native arena with actual address-ordered splitting/coalescing, exact
free/largest queries, and guard bytes around every native allocation. This
replaces optimistic largest-block assumptions for these additional checks.

- Exhaust a 64 KiB backing cap with tiny live allocations until **all usable pool
  bytes are occupied**, including per-object allocator headers, and check that
  further allocations fail without crossing the cap. Payload capacity remains
  lower than backing: region descriptors and object headers are real overhead.
- Reclaim empty regions and satisfy a larger contiguous request when the native
  free blocks can actually coalesce. Verify an injected native allocation failure
  is bounded and leaves unrelated live data and the remaining allocator intact.
- Deliberately interleave live native buffers between regions. Verify the larger
  request still fails when native holes cannot coalesce, without overwriting,
  moving, or freeing those buffers. Free total is not a guarantee of one block.
- Run 48,000 deterministic randomized persistent/temporary allocation, realloc,
  free and failure-injection operations across four seeds, checking retained
  payloads, native canaries, pool/backing accounting and reserve/cap invariants.
- Verify complete provider teardown restores the entire native arena, not merely
  VM accounting counters. Both strict provider fixtures are included in
  `make firmware-heap-test`, sanitizer CI and the 32-bit memory job.

The host arena and bookkeeping are test instrumentation, not a model of their
own firmware footprint. Passing these checks does not measure ESP-IDF native
fragmentation, BLE peak load or the user's actual app on a physical ESP32-C2.
No RAM reserve is removed and no promise is made to allocate all physical RAM.
