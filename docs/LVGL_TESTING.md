# LVGL validation and remaining work

The production binding consumes one existing host dependency. The fixture in
`tests/lvgl` deliberately acts as that host: it builds the explicitly supplied
upstream checkout, initializes a headless display and supplies native locking,
allocation and VM ownership callbacks. No test dependency is bundled in MicroCS.

## Reproduce

```sh
# Explicit opt-in host source repair; LVGL 9 is left unchanged.
python3 tools/patch_lvgl8_font.py /path/to/lvgl-8.x-or-9.x --apply
python3 tests/lvgl/test_font_writer.py /path/to/lvgl-8.x-or-9.x
make lvgl-test LVGL_DIR=/path/to/lvgl-8.x-or-9.x LVGL_SANITIZE=ON
# or:
cmake -S tests/lvgl -B build/lvgl -DLVGL_HOST_DIR=/path/to/lvgl -DSANITIZE=ON
cmake --build build/lvgl --target test_lvgl -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/lvgl --verbose > build/lvgl/sanitizer.log 2>&1
python3 tests/lvgl/check_sanitizer_log.py --lvgl-version v8.4.0 build/lvgl/sanitizer.log
```

Use a clean build directory per upstream checkout/configuration. `SANITIZE=ON`
instruments both MicroCS and LVGL with ASan/UBSan and frame pointers. The fixture
has a fail-fast LVGL assertion handler; it does not turn native LVGL OOM into
recoverable exceptions. For minimal widgets, add `-DMINIMAL_WIDGETS=ON`. Add
`-DMICROCS_LVGL_INCLUDE=lvgl.h` to exercise the explicit header spelling and
`'-DCMAKE_C_FLAGS=-DMCS_ENABLE_HAL=0 -DMCS_ENABLE_DRIVERS=0'` for independent use.

The fixture tests real host-driven tick/handler rendering and host-owned widget survival; private subtree cleanup; 100 VM
reruns; parent/native deletion, alias validity and stale slot reuse; copied label
text across GC; checked/value/color/geometry methods; callback rooting; bounded
queue overflow, registration replacement and stale notifications; wrong-thread
rejection; callback exceptions; VM allocation-failure injection in both script
execution and attachment registration; native attachment allocation failure;
and real pthread event production racing with repeated VM teardown. Native
events and all GUI accesses serialize through the host mutex. No native event
executes a VM.

## Version matrix

Local ASan/UBSan runs cover these exact upstream tags (details recorded below):

| Upstream tag | Role |
| --- | --- |
| v8.0.2 | Early 8.x API/readiness and event compatibility |
| v8.1.0 | Empty-dropdown normalized-string/null-native regression |
| v8.4.0 | Later 8.x representative |
| v9.0.0 | Early 9.x void-returning event registration |
| v9.2.2 | Intermediate 9.x representative |
| v9.3.0 | Later 9.x changes and decompression-disabled configuration |
| v9.6.0 | Newer 9.x public-header/layout compatibility |

The workflow `.github/workflows/lvgl.yml` additionally schedules every published
minor family represented by tags v8.2.0, v8.3.11, v9.1.0,
v9.4.0 and v9.5.0, plus minimal-widget builds on v8.4.0/v9.2.2/v9.6.0.
**A configured CI matrix is not a passed CI result.** Future minor/patch releases
and vendor forks still need regression checks against their actual headers and
configuration. The wrapper accepts majors 8/9; it does not expose their entire
API surface. See [coverage](LVGL.md#c-api-and-coverage).

### LVGL 8.0 host teardown limitation

The stock v8.0.2 `lv_disp_remove` removes the display from its global list before
deleting its screens; screen deletion can no longer find its display and the
screen-count loop fails to finish in this fixture. The test skips **only** that
upstream display-removal operation and clears native fixed-pool state using
`lv_deinit`. Wrapper object/VM cleanup, host-ancestor deletion and all other
checks still run. The wrapper itself never removes host displays. This skip is
not represented as a passed display teardown test.

## Measurements and scope

The fixture prints actual native attachment storage bytes, userdata handle bytes,
VM tracked bytes for its active test application, and CPU time for 10,000 position
updates from C# versus direct native calls. Script timing includes compilation;
when instrumented, both sides carry sanitizer overhead. They are reproducibility
checks, **not MCU performance promises or a clean native-call overhead benchmark**.

Default capacities on the tested x86-64 ABI use **1,832 bytes** of native attachment (1,824 with Grid/Line disabled)
storage, plus one LVGL-owned container and the actual LVGL widgets/text/styles.
Handle payloads are **8 bytes**, excluding VM userdata headers and GC metadata.
The active fixture application reports **8,845 bytes VM tracked storage**;
that is not a minimum heap requirement or total device RAM. Do not add pool use
to device heap use when the pool already comes from that heap.

LVGL 8 uses its own fixed 256 KiB test allocator; LVGL 9 uses its native libc
allocator. ASan sees the LVGL 9 native allocations; it does not individually poison
freed suballocations inside LVGL 8's static pool. Do not describe this as complete
native LVGL 8 allocator fault coverage.

## Not established / not release-ready claims

- Physical ESP32-C2/C3 or other target operation, display/input behavior and GUI
  task lock policy; firmware packaging hooks are not board certification.
- An exhaustive run of every patch release or arbitrary vendor configuration.
- Recovery from **all LVGL native allocator failures**; upstream constructors,
  text and styles can assert/assume allocation success. Only the wrapper's own
  attachment allocation and VM allocation paths have injected failure coverage.
- Full-library TSan or a complete native memory/latency audit. The real pthread
  test plus ASan/UBSan is not a substitute for TSan or MCU scheduler testing.
- Combined device-native/LVGL/VM memory totals or rendering performance on real
  hardware. No framebuffer allocation is attributed to the VM pool.
- Complete API coverage beyond the 25 documented kinds and 443 native-call
  operation families; retained array/descriptor APIs, shared resource handles,
  custom drawing/animation callbacks and many object-returning methods remain
  unwrapped. Some new widget kinds currently expose creation/common APIs only. File images/fonts do not imply arbitrary descriptor support.

These gaps must remain visible in review; do not merge on the basis of a demo
or rename this work a full LVGL API binding.

## Recorded local checks — initial implementation

The default Unix build (`cc` / GNU 11.5.0, `-O2 -g`) and untouched main at
`b930916e4a7325ecfee643735885f0c6464c264e` both reported these `size` section totals:
text 822,033 bytes, data 1,564 bytes and BSS 30,664 bytes. The disabled binding
object has no undefined symbols; the VM object's `.text` bytes match baseline.
This is a host build comparison, not an MCU flash/latency guarantee. The disabled
capability row is now compiled out, not merely set to false; no polling/GUI work
is added. The new strict disabled proof compares both VM and capability machine
code, structure size, extension slots and startup memory against that baseline.

Default script regressions: 106 passed, 0 failed. Studio language and formatter
checks passed, including preservation/idempotence across 222 C# templates.
Template execution: 222 C# templates, 223 total entries, 77 snippets all passed.
Docs/wiki/config generation checks passed. Arduino `--lvgl` packaging succeeded;
this was not a board build. All core/module translation units passed `-Werror`
syntax checks with LVGL enabled, compiler/malloc/HAL/drivers disabled. The enabled
missing-header and missing-host-target diagnostics, explicit header spelling,
nested-header discovery and unsupported-major rejection were checked locally.

## Asset and disabled-build extension checks

Real RAMFS-backed LVGL drive tests load the upstream checkout's binary font
fixture and a generated 2x2 binary image, then render through the host handler.
They exercise transient script strings/GC, missing resources, native font names,
font inheritance, parent destruction, explicit dispose and VM teardown over 20
VMs. Read/seek/tell/EOF, open-limit exhaustion, read-only access, root confinement,
permanent drive disable and close-before-VFS-destruction are checked. No copied
third-party font binary is added to MicroCS. A missing upstream fixture is a
configuration failure rather than a silent test skip.

The extension tests caught LVGL 9.0's binary-image info callback accepting an
unsuccessful file open; the wrapper now also rejects zero-dimension headers.
They also exposed an important font configuration gap: a compressed font can
load successfully with `LV_USE_FONT_COMPRESSED=0`, yet LVGL 9.3's software glyph
renderer dereferences a null bitmap. The wrapper now destroys/rejects that file
font before it is applied. Full fixture builds explicitly enable decompression
for the upstream compressed font; minimal builds turn it off and test rejection.
A separate 9.3 full-widget CI job (`-DNO_COMPRESSED_FONTS=ON`) renders
with the host setting off after the
rejected font leaves the default font intact. The fixture also executes the shipped button and asset examples against
the real attached GUI and populated drive when the required widgets/HAL are
enabled, checking the expected panel or caught-error cleanup. No rendering or
font test is silently skipped to make the matrix pass. The bounded bridge uses 432 native bytes
on the tested x86-64 ABI (440 with v9.6.0); this is caller-owned host storage,
not per-VM allocation. Owned font pointers increase attachment storage to 1,824
bytes. These counts apply only to the stated host configuration.

To reproduce the disabled proof without LVGL installed:

```sh
sh tests/lvgl/test_disabled.sh /path/to/pre-feature-checkout
```

Local GNU 11.5.0 result: identical VM/capability `.text`, VM size 2,680 bytes,
9 extension slots, startup tracked memory 1,082 bytes and 11 objects; no LVGL
runtime symbols. No MCU timing claim is inferred from this host result.

## API inventory and backlog

```sh
python3 tools/lvgl_api_inventory.py /path/to/lvgl --json build/lvgl-api-inventory.json
```

The JSON lists discovered public-looking header declarations and maps explicit
C# operations to their native counterparts. **It is a review inventory, not an
exact preprocessed ABI or a full-coverage certificate.** Private-named headers
and internal-suffixed helpers are excluded; macros, types, generated declarations
and function-pointer typedefs are not counted. Disabled feature declarations
are included. Newer LVGL's `include/` and older `src/` layouts are both scanned.
CI uploads an inventory per compatibility tag.

`binding-operation` means only that a constrained operation in the documented
wrapper represents some of that function's behavior, not that all its arguments,
selectors, callbacks or variants are exposed. `host-owned` marks explicitly
identified initialization/scheduling entrypoints. `unwrapped-review` is the
remaining backlog, **not** a claim that every missing API is unsafe or host-only.
A native function called internally for bookkeeping does not count as a public
C# wrapper. This makes incomplete coverage visible rather than hiding it behind
an LVGL 8/9 compatibility claim. Full API binding remains unfinished.

### Full-binding acceptance work still required

The inventory is only the starting point. Closing the requested full-API goal
requires a reviewed decision for each public function/type/macro across both
majors and feature configurations, not automatically exposing raw C signatures:

1. Wrap the remaining safe widget methods, getters, layout/scroll/flex/grid,
   style selectors, flags/states, groups/focus and theme operations with checked
   handles, explicit ranges and usable enums/value types.
2. Design separate owned handles for shared styles, animations/timelines,
   chart/canvas buffers and image/font descriptors. Establish copying/pinning,
   dependency lifetimes, native deletion, cancellation and VM teardown semantics.
3. Marshal event payloads and version-specific structures without storing stack
   pointers or invoking the VM from the GUI thread. Keep bounded queues/overflow
   behavior explicit; do not add background work to disabled or unattached VMs.
4. Identify genuinely host-owned initialization/scheduling/display/input APIs
   separately from merely unimplemented safe APIs. Do not classify the latter
   as unsafe to inflate reported coverage.
5. Add per-operation semantic, feature-off, version, GC, native-delete, resource
   replacement, callback and failure tests plus docs/Studio examples. Generate
   configured/preprocessed API comparisons before calling coverage exhaustive.

Until that work is implemented and validated, this is a constrained binding,
not a completed full-LVGL C# API. No merge or release should imply otherwise.

## Native-call expansion validation

The reviewed manifest generates 443 operation families and 227 enum constants.
The fixture checks each family's version/config availability and ID resolution,
wrong arity for parameterized entries, every available scalar style setter/getter,
and constant availability. These generated checks do **not** establish exhaustive
semantics for every operation. Separate assertions exercise cached IDs, RGB
styles, geometry, flags/state, percentage encoding, invalid selectors/pointers,
reparent-cycle rejection, clean/stale invalidation, flex, extended slider ranges,
copied Dropdown/Roller options, TextArea input, Table cells/capacity/bounds,
Led/Spinbox behavior, Keyboard target deletion and queued native event types.

Text snapshot tests inject host snapshot allocation failure and every VM
allocation failure on a native-only string path. They check that native snapshot
storage is freed and GUI locks are released before VM OOM recovery. The new
shipped native-API example executes in the enabled fixture with real rendering.
Boundary regressions also reject native integer-overflow spans and Spinbox
values/steps/format reductions that cannot fit the configured digit count.
This caught and now prevents an LVGL 9.0 formatter stack underflow. The fixture
reports 10,000 native versus name/cached-ID `Call` updates (script times include
compilation); these host/sanitized figures are not MCU timing guarantees.

Minimal fixtures now also disable Flex/Grid/Led/Line/Tileview, and verify that
all guarded API families/constants are unavailable without link references.

The wrapper uses primitive exported cache API declarations when the LVGL 9.6
umbrella omits the documented `lv_image_cache_drop` prototype; no private layout
is accessed and `LV_USE_PRIVATE_API` is not enabled. Local compatibility helpers
isolate upstream's deprecated-but-exported generic flag/List APIs rather than
suppressing diagnostics globally. The table-cell-control rename is bridged from
LVGL 9.3 onward; removed LVGL 8 filtered-color setters are not exposed.

Native-call support improves coverage, but does not close the full-binding
acceptance work above. In particular, native constructors with retained buffers
or returned internal subobjects require additional owned-resource design.


## Copied-buffer expansion and debugging

Grid tracks and Line points are copied into lazy native allocations, not pinned
VM arrays. Tests cover input mutation/GC, repeated replacement, clear, native
and parent deletion, stale handles, wrong threads/kinds/types/capacities, native
buffer allocation failure and every VM allocation-failure position during a
buffer-owning application. Native memory accounting must return to zero after
VM teardown. Minimal fixtures reject these methods without referencing the
native Grid/Line implementation.

The Grid audit found unchecked track indexing in LVGL 8.0. Cell APIs, scalar
cell-placement styles, grid shrink and reparenting are now validated against
owned tracks; stateful cell selectors are rejected. Fraction 100 is rejected
because it collides with the native end sentinel. Line/grid buffer alignment
uses aligned raw storage rather than reinterpreting a typed VM/point array.
Pending-delete cleanup scans occur only when native deletion has marked work.
A regression also caught that `Dispatch(0)` previously skipped all reaping; it
now retires resources without consuming queued events.
Image offset/rotation/scale/pivot/antialias calls are tested with both native
name spellings, and real file-backed images render after transforms.

These tests are not exhaustive native allocator recovery, asynchronous-device
rendering certification, or full API coverage. The host must preserve the
binding's owned buffer/style contracts; image/font/renderer costs remain native.


### LVGL 8 compressed-font repair: no sanitizer exception

Stock LVGL 8's compressed-font bit writer shifts the signed negative value
`~bit_mask` left. The former fixture explicitly allowed the resulting diagnostic;
**that exception has been removed**. `tools/patch_lvgl8_font.py --apply` makes
one reviewed change in the explicitly supplied test/firmware dependency:

```c
out[byte_pos] &= ((~(uint32_t)bit_mask) << bit_pos);
```

This preserves the bitstream writer's original prefix/suffix semantics while
performing complement and shift in unsigned arithmetic. The actual extracted
writer is exercised for 1/2/3/4/8-bit values, valid offsets across three bytes,
every representable input value, all 256 destination-byte patterns and canaries:
**322,560 checks per LVGL 8 checkout**, under non-recovering UBSan. Real binary
compressed-font loading/rendering remains enabled in the full fixtures.

The repair is explicit and idempotent; no production build invokes it. Unknown
LVGL 8 source forms fail without modification; LVGL 9 is unchanged. Firmware
hosts must review/commit/rebuild their dependency or use a correctly fixed native
library. Updating MicroCS alone does not alter an already compiled LVGL binary.
Unit tests cover opt-in behavior, exact replacement, repeated application and
unknown/duplicate input rejection. CI applies the fix to its disposable native
checkout and runs the writer test before compiling the host library.

CI captures verbose output with non-recovering UBSan. `check_sanitizer_log.py`
now rejects **every** UBSan runtime diagnostic and ASan/leak report, including
the previously allowed exact font warning. No compiler/runtime check is disabled.
Full local patched-8.0.2/8.4.0 and unmodified-9.0/9.2/9.3/9.6 fixtures pass with
no sanitizer reports; 8.0's previously documented display-removal skip remains.

## Retained-text and selected-string expansion

The 10 new families cover owned accepted-character filters and dropdown titles/
symbols, copied getters, Dropdown/Roller selected strings, TextArea alignment,
and Chart pressed-point queries. Null resets owned strings. Empty filters are
normalized to null because early LVGL 8 otherwise rejects all characters while
later minors accept all. Chart no-point sentinels are normalized to -1.

Tests verify VM string lifetime/GC, repeated replacement, empty/null reset,
filtered input, native allocation failure preserving old state, selected-string
snapshot OOM, exact text-bound output versus overlong rejection (no truncation),
wrong/stale handles, native-parent deletion plus Dispatch(0), and every VM
allocation-failure position. Counted/poisoned native allocations return to zero.
LVGL 9.5+ uses public static setters to avoid duplicate native string copies.
Other native allocator OOM assertions remain the host's responsibility, not a
newly promised recovery guarantee.

The new example executes with real rendering and appears in Studio. Default
local fixtures report 686 copied-buffer/text checks; minimal widgets report 13.
Lazy records share the existing list and session size remains 1832 x64 bytes
(default) / 1824 with all retained-buffer/text features disabled. Disabled-build
VM memory, machine-code and runtime-symbol proofs remain separate from enabled
costs. No physical MCU timing or complete API coverage is claimed.

New image-offset calls require nonzero intrinsic image dimensions, preventing
the empty-image modulo-by-zero in stock LVGL 8.0. Native offset normalization
still follows the host version (8.0 reduces offsets modulo source dimensions).


Unicode auditing found that native `lv_textarea_add_char` consumes packed UTF-8
bytes, while the previous wrapper validated/passed raw Unicode scalar numbers.
The wrapper now explicitly encodes BMP and supplementary scalars and copies
through native text insertion. Tests check accented Latin, Persian and emoji
text plus surrogate/NUL rejection. No pointer to stack text is retained.


## LVGL 8.1 empty-dropdown regression

After `lv_dropdown_clear_options`, LVGL 8.1 stores a null options pointer but
`lv_dropdown_get_options` returns an empty string. Its selected-string getter
still calls `strlen` on that internal null pointer. Checking only the public
getter for null therefore does not protect the native call. The generated
Dropdown/Roller selected-text path now short-circuits both null and empty options
before invoking the native selected-string getter.

The copied-buffer fixture clears a dropdown repeatedly and checks zero options,
both valid empty-options representations (null on early versions, empty string
on others), an empty selected snapshot, and survival of an earlier copied
selection after disposal/GC. The v8.1.0 failure was reproduced with fatal UBSan
before this guard and passes with it; the sanitizer auditor has no exceptions.


## ButtonMatrix copied-map expansion

The copied-buffer fixture tests map/text/pointer alignment and GC independence,
control-array copying, selection aliases/NONE/bounds, exact 256-entry and
configured-text-byte boundaries, invalid shapes/types/rows/NUL/terminators,
wrapper native OOM preserving old state, wrong-thread/kind/stale handles, repeated
replacement/shrink, and cleanup after host parent deletion or VM allocation-fault
unwinding. Native selected state is cleared before replacing maps on both majors.
The example is executed by the asset fixture. `BUTTONMATRIX_ONLY=ON` disables the
other exposed optional widgets, ensuring the retained-buffer helpers compile and
work without Grid, Line, TextArea or Dropdown. CI tests that configuration on
v8.1.0 and v9.6.0, in addition to the normal/minimal matrix. A scheduled CI job is
not a passed result; consult the PR's actual latest run.
