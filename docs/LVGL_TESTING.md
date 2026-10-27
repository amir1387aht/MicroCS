# LVGL validation and remaining work

The production binding consumes one existing host dependency. The fixture in
`tests/lvgl` deliberately acts as that host: it builds the explicitly supplied
upstream checkout, initializes a headless display and supplies native locking,
allocation and VM ownership callbacks. No test dependency is bundled in MicroCS.

## Reproduce

```sh
make lvgl-test LVGL_DIR=/path/to/lvgl-8.x-or-9.x LVGL_SANITIZE=ON
# or:
cmake -S tests/lvgl -B build/lvgl -DLVGL_HOST_DIR=/path/to/lvgl -DSANITIZE=ON
cmake --build build/lvgl --target test_lvgl -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/lvgl --output-on-failure
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
| v8.4.0 | Later 8.x representative |
| v9.0.0 | Early 9.x void-returning event registration |
| v9.2.2 | Intermediate 9.x representative |
| v9.6.0 | Newer 9.x public-header/layout compatibility |

The workflow `.github/workflows/lvgl.yml` additionally schedules every published
minor family represented by tags v8.1.0, v8.2.0, v8.3.11, v9.1.0, v9.3.0,
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

Default capacities on the tested x86-64 ABI use **1,824 bytes** of native attachment
storage, plus one LVGL-owned container and the actual LVGL widgets/text/styles.
Handle payloads are **8 bytes**, excluding VM userdata headers and GC metadata.
The active fixture application reports around **8.6 KB VM tracked storage**;
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
- API coverage beyond the nine documented kinds and their listed operations;
  most public methods, complex widgets, shared styles, animations and layouts
  remain unwrapped. File images/fonts do not imply arbitrary descriptor support.

These gaps must remain visible in review; do not merge on the basis of a demo
or rename this work a full LVGL API binding.

## Recorded local checks

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

Local ASan/UBSan/leak runs with these extensions passed on v8.0.2 (628 checks;
upstream display removal still skipped), v8.4.0/v9.0.0/v9.2.2/v9.6.0 (629 each),
and v9.2.2 minimal-widget/no-HAL/no-drivers (627). The tests caught LVGL 9.0's
binary-image info callback accepting an unsuccessful file open; the wrapper now
also rejects zero-dimension headers. The bounded bridge uses 432 native bytes
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
