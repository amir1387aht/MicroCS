# LVGL — optional C# wrapper over a host-owned GUI

MicroCS is a portable C99 C# subset compiler/VM, not .NET. This optional
binding consumes **the firmware's existing LVGL 8.x or 9.x**. It is off by
default (`MCS_ENABLE_LVGL=0`), independent of HAL/drivers, and bundles no LVGL.
There is no version bump or release associated with this feature.

## Version compatibility versus API coverage

The version policy accepts major **8 and 9**, with minor/patch releases handled
by a small spelling/API bridge (`include/mcs_lvgl_compat.h`). Other majors fail
at compile time. This is not a claim that every past/future minor or arbitrary
vendor fork was tested, or that every LVGL API is exposed. The checked release
matrix and remaining validation gaps live in [LVGL testing](LVGL_TESTING.md).
The exact header, library binary, configuration and compiler ABI must match.
Consume the same host build target; adding a header path alone cannot guarantee
that an independently built binary has the same `lv_conf.h` or layout. There is
no reliable runtime check for every possible configuration/ABI mismatch.

The bridge covers LVGL 8 `lv_btn_create`, object deletion/state APIs and the
LVGL 9.0 void-returning event-registration API. For LVGL 8 (including early minors),
a registered display is the readiness check. No wrapper changes `lv_conf.h`,
font settings, color depth, precision or the host allocator.

## Firmware responsibilities

Before attachment, firmware must initialize LVGL and a working display, draw
buffers, flush callback, input devices, tick source and task/timer handling.
Those resources remain host-owned. The wrapper **never** calls `lv_init`,
registers a display/input device, allocates framebuffers, increments ticks,
calls `lv_timer_handler`, starts a task, or installs VM idle polling.

Choose a live host root on the desired display. Attachment adds one transparent,
full-size **script-owned container** under it. All script objects are created
inside this private subtree. Root layout/coordinates still obey normal LVGL
layout rules; reserve an appropriate root region in the native UI. Do not move
host-owned widgets into this subtree, reparent script widgets outside it, or
remove the binding's native deletion observers. No raw pointer is exposed to C#.
Host display/root/widgets survive VM destruction. Deleting a host ancestor
natively deletes the container and invalidates script descendants.

### Attachment

Include `mcs_lvgl.h` with the feature enabled. The config has no defaults for
threading or allocation; the host must deliberately supply them:

```c
/* Run from mcs_runtime_cfg_t.setup after the host GUI is initialized.
 * app_gui_lock/unlock use the SAME mutex as all native GUI operations and
 * the GUI task. app_on_vm_thread must identify this VM's owner thread.
 * app_gui_alloc/free return aligned native memory (not VM scratch storage). */
static void setup(mcs_vm_t* vm, void* user) {
    struct app* app = user;
    mcs_lvgl_config_t cfg = {
        .root = app->script_root, .ctx = app,
        .lock = app_gui_lock, .unlock = app_gui_unlock,
        .is_vm_thread = app_on_vm_thread,
        .alloc = app_gui_alloc, .free = app_gui_free,
        .fonts = NULL, .font_count = 0
    };
    int rc = mcs_lvgl_attach(vm, &cfg);
    if (rc != MCS_LVGL_OK) {
        /* Surface rc and reject this application start; do not run GUI scripts.
         * If registration OOM occurred, destroy/recreate this VM. */
        app_report_gui_attach_failure(app, rc);
    }
}
```

This is integration pseudocode: the application supplies `struct app`, callbacks
and failure policy. Match the runtime's actual setup callback signature (see
`mcs_runtime_cfg_t`); a direct embedder calls `mcs_lvgl_attach(vm,&cfg)` before
execution. Do not initialize the screen in a C# template.

The VM must be idle on its owner thread during attachment. Attach only once per
VM (`ALREADY_ATTACHED` on a second call). Config is copied, but the context and
callbacks must outlive the VM. A nonempty native font table, its names and fonts
must also outlive the VM (the table is borrowed, not copied). `INVALID`, `NOT_READY` and `NO_MEMORY` distinguish
contract errors, absent display/root and wrapper initialization failure.
Registration runs under a VM OOM recovery guard; on registration failure the
container and attachment storage are freed, but discard the VM because a subset
of API declarations may already exist. No VM allocation occurs while holding
the GUI mutex.

## Synchronization and events

Every native GUI operation, including host deletion, must use the same lock as
the binding. The host GUI handler must serialize with it too (respect the host
LVGL OS integration's existing internal locking; do not deadlock by taking a
nonrecursive mutex twice). A genuinely single-threaded host may supply no-op
lock/unlock functions, but must still supply an owner-thread check. Calling C#
GUI methods on a worker VM or wrong thread is rejected. All other VM access,
GC, attachment and destruction must also obey MicroCS's single-owner contract.

Native events only enqueue fixed-size slot/generation records while holding the
GUI lock. They **never call a VM**, allocate, retain `lv_event_t*`, or unpin on a
GUI thread. The script calls `Lvgl.Dispatch(maxEvents)` explicitly on its VM
thread, between 0 and `MCS_LVGL_EVENT_QUEUE`. The default queue holds 16 records;
full queues drop the newest notification and increment saturated
`Lvgl.DroppedEvents`. Dispatch consumes at most the requested records, including
stale ones. Delegates take **zero arguments**; read current widget state in the
callback (events do not snapshot historical values).

Each object supports **one** subscription: `OnClicked(action)`,
`OnChanged(action)` or `OnEvent(eventCode, action)` replaces the old one; `ClearEvent()` / a null callback
removes it. Delegates are pinned until replacement, disposal, VM teardown, or
the next VM-side reap/dispatch after native deletion. Queued events are checked at dequeue and rejected by both object and subscription generations, even when slots/native
pointers are reused. Dispatch is non-reentrant; callback exceptions/abort stop
that dispatch and propagate through normal VM handling. Remaining queued events
stay bounded; VM teardown discards them before destroying pins/objects. No old
VM callback survives runtime application replacement. Host deletion just after
a valid dequeue can invalidate a handle before its callback runs; subsequent
widget operations still perform the same locked validity checks.

## C# API and coverage

`Lvgl.Ready` means the attachment's private subtree still exists. It is separate
from firmware capability `Lvgl`, which only means the API was attached/registered.
`Lvgl.Supports("Label")` checks the widget's **actual compile-time setting**.
Names are case-sensitive; unknown kinds return false. Disabled widgets produce
`NotSupportedException` and no unresolved references.

| Kind for `new LvObject(kind[, parent])` | Specific operations | LVGL configuration |
| --- | --- | --- |
| `Object` | Base container | Core |
| `Label` | `Text` (copied read/write) | `LV_USE_LABEL` |
| `Button` | Base/event operations; add a child Label for its text | v8 `LV_USE_BTN`, v9 `LV_USE_BUTTON` |
| `Slider` | `Value`, range 0–100, no animation | `LV_USE_SLIDER` |
| `Bar` | `Value`, range 0–100, no animation | `LV_USE_BAR` |
| `Checkbox` | `Text` (copied read/write), `Checked` | `LV_USE_CHECKBOX` |
| `Switch` | `Checked` | `LV_USE_SWITCH` |
| `Arc` | `Value`, range 0–100, no animation | `LV_USE_ARC` |
| `Image` | `Source` (write-only LVGL file path) | v8 `LV_USE_IMG`, v9 `LV_USE_IMAGE` |
| `Dropdown` | copied options, selection, popup controls via `Call` | `LV_USE_DROPDOWN` |
| `Roller` | copied options, selection, visible rows via `Call` | `LV_USE_ROLLER` |
| `TextArea` | `Text`, cursor/password/selection/input via `Call` | `LV_USE_TEXTAREA` |
| `Table` | copied cells, dimensions/controls via `Call`; bounded growth | `LV_USE_TABLE` |
| `Led` | color, brightness, on/off/toggle via `Call` | `LV_USE_LED` |
| `Spinbox` | `Text`, value/range/digits/step via `Call` | `LV_USE_SPINBOX` |
| `Chart` | type/range/point-count/update controls via `Call`; series handles not yet exposed | `LV_USE_CHART` |
| `Canvas` | creation/lifetime/common styles only; pixel buffer/drawing APIs pending | `LV_USE_CANVAS` |
| `Keyboard` | mode/popovers and checked TextArea linkage via `Call` | `LV_USE_KEYBOARD` |
| `ButtonMatrix` | button controls/width/text via `Call`; custom retained maps pending | v8 `LV_USE_BTNMATRIX`, v9 `LV_USE_BUTTONMATRIX` |
| `Line` | inversion/common styles; retained point arrays pending | `LV_USE_LINE` |
| `List` | creation/lifetime/common styles; child-returning add APIs pending | `LV_USE_LIST` |
| `Calendar` | today/shown date via `Call` | `LV_USE_CALENDAR` |
| `ImageButton` | creation/lifetime/common styles; retained state images pending | v8 `LV_USE_IMGBTN`, v9 `LV_USE_IMAGEBUTTON` |
| `Spinner` | creation (v8 defaults 1000 ms / 60°), lifetime/common styles | `LV_USE_SPINNER` |
| `Tileview` | creation/lifetime/common styles; returned tile handles pending | `LV_USE_TILEVIEW` |

All objects expose `IsAlive`, idempotent `Dispose()`, `Center()`,
`SetPosition(x,y)`, `SetSize(width,height)`, `SetBackground(rgb24)`,
`SetTextColor(rgb24)`, `SetFont(nameOrPath)`, `Call(nameOrId, ...)`,
`OnEvent(eventCode, action)`, `OnClicked(action)`, `OnChanged(action)` and `ClearEvent()`.
Positions are -32767..32767; sizes 0..32767; colors 0..0xFFFFFF. Kind-incompatible
properties throw. No screen wrapper, arbitrary native-object wrapping,
host reparenting, display/input setup or pointer-as-integer access is exposed.
Checked native reparenting accepts only live script handles and rejects cycles.
File images and binary fonts are supported as described below. The native-call
surface adds scalar style setters/getters, flex, grid-cell settings, scrolling,
geometry/flags/state and many widget methods. Shared style handles, retained
array/descriptor resources, standalone animation callbacks and most custom
rendering/driver APIs are **not exposed yet**. Inline styles use LVGL's object-owned storage. Text and
image-path setters use LVGL copying APIs, never retained script-string pointers.
There is no arbitrary descriptor, native pointer or script byte-array image API.
See the reproducible API inventory in [testing](LVGL_TESTING.md#api-inventory-and-backlog);
version compatibility does **not** mean full API coverage.

```csharp
if (!Lvgl.Ready || !Lvgl.Supports("Label") || !Lvgl.Supports("Button"))
    throw new Exception("Host GUI or required widgets unavailable");
var button = new LvObject("Button");
button.SetSize(110, 40);
button.Center();
var text = new LvObject("Label", button);
text.Text = "Click me";
text.Center();
button.OnClicked(() => { text.Text = "Clicked"; });
// Call Lvgl.Dispatch(16) from your existing application loop.
// The host continues to own rendering and ticking.
```

## Checked native API dispatch

The [generated native-call reference](LVGL_NATIVE_API.md) lists the exact
allowlist, version aliases, feature guards and argument bounds. It currently
contains **421 operation families** and **224 named constants**, not every LVGL
function. Neither a raw symbol name nor an integer can invoke arbitrary native
code or serve as a native pointer.

```csharp
var slider = new LvObject("Slider");
slider.Call("lv_slider_set_range", -50, 150);
int setValue = Lvgl.Api("lv_slider_set_value");
slider.Call(setValue, 125, Lvgl.Constant("LV_ANIM_OFF"));
int current = slider.Call("lv_slider_get_value");
slider.Call("lv_obj_set_width", Lvgl.Percent(80));
slider.Call("lv_obj_set_style_radius", 8, 0);
```

`Call` accepts a native-name string or a cached `Lvgl.Api(name)` ID, plus up to
six arguments. Name resolution uses a binary search of static read-only names.
The numeric path avoids name lookup; IDs are build-specific and **must not be
persisted or shared across firmware versions**. `HasApi(name)` checks whether a
reviewed family exists in this host major/configuration, not whether it applies
to every widget or whether all native optional rendering features are enabled.
Wrong arity/type/range, wrong widget kind, stale handles, unknown APIs and
unsupported config/version branches fail before an unsafe native call.

No hundreds-entry per-VM API/constant registry is built. One native `Call` method
uses generated explicit C calls, compile-time feature guards and a fixed stack
argument union. `Constant(name)` / `HasConstant(name)` resolve native enum values
from static read-only tables rather than copying all constants into VM classes.
Table placement in flash versus RAM is determined by the target/linker; the
wrapper does not certify every MCU memory map. This trades
some **enabled flash** and explicit-call validation cost for bounded VM memory;
it does not claim zero enabled CPU/flash cost. `Percent(-1000..1000)` encodes the
host's LVGL coordinate format; avoid hardcoding v8/v9 percentage bit patterns.

Text inputs must be strings and use copying native setters. Text getters take a
bounded native snapshot under the GUI lock, release the lock, then create the
VM string through an OOM-protected cleanup boundary. No borrowed native pointer
survives unlock. Both native snapshot allocation failure and VM allocation
failure release the snapshot. `MCS_LVGL_MAX_TEXT_BYTES` defaults to 4096; table
cell growth is capped by `MCS_LVGL_MAX_TABLE_CELLS` (default 256). Resource limits
are consistent across translation units. No snapshot buffer is reserved while
idle; an explicit getter allocates only its actual text size plus terminator.

Only script-owned object handles can be arguments. Reparenting cannot introduce
a cycle or escape into a host root. Keyboard TextArea targets must have the
correct widget kind and are detached automatically before native/parent/VM-side
target deletion (LVGL 8 otherwise retains that pointer). Native dropdown popups
may use LVGL's overlay layer, but remain owned and destroyed by their native
widget; no host widget is wrapped or deleted. Internal widget children are
native allocations and are not each counted as an explicit script handle.

`OnEvent` exposes the documented pressed/pressing/press-lost, short/long/repeat
click, clicked/released, scroll begin/end, focus/defocus/leave, value-changed,
ready and cancel notifications, using `Lvgl.Constant("LV_EVENT_...")`. The same
single pin, bounded queue and generation checks apply. These are **deferred
zero-argument notifications**, not synchronous draw/delete hooks, input vetoes,
or retained `lv_event_t` payloads. Deletion/draw events are rejected.

The manifest `tools/lvgl_api_schema.json` is the source of the generated C,
reference and availability/style smoke cases. Run `python3 tools/gen_lvgl_api.py`
after a reviewed schema change; CI checks generated files for drift. Generation
is not a safety review: retained-pointer APIs, object-returning functions,
custom callbacks and buffer lifetimes require dedicated owned handles rather
than adding untyped integer/array arguments to this table.

## Fonts, images and LVGL filesystem paths

`obj.SetFont("Default")` selects `LV_FONT_DEFAULT`. Other names resolve through
`mcs_lvgl_config_t.fonts`, an optional array of `{name, const lv_font_t*}` entries.
These native fonts are borrowed and never destroyed by MicroCS. A name with a
second-character colon, e.g. `S:/fonts/ui.fnt`, instead loads a native **binary
font** using LVGL 8 `lv_font_load` or LVGL 9 `lv_binfont_create`. Each successful
file load belongs to that widget's slot; replacement frees the previous owned
font. Parent font inheritance works normally. Native deletion defers font
release until the deleted subtree has finished its destructors. Missing drives,
files and load failures throw; failed loads preserve the old selection.
Compressed file fonts are rejected with `NotSupportedException` when the host
sets `LV_USE_FONT_COMPRESSED=0`; loading alone does not prove glyph support.
The wrapper never changes the host setting. Registered native fonts remain the
host's responsibility, including their feature/fallback compatibility.

`image.Source = "S:/images/logo.bin"` requires an enabled Image widget, a
registered LVGL drive and a decoder able to read the file header with nonzero
size. Failure preserves the old source. Header acceptance does not guarantee
that every pixel can subsequently be decoded from a damaged/truncated file.
The host enables/configures PNG/JPEG/etc. decoders if needed. LVGL 8/9 binary
image formats differ: convert assets for the host's version, color depth and
configuration. Font assets must also match the host loader. Use trusted assets;
this wrapper does not harden native decoders against malicious files.

Source replacement and image deletion invalidate that source's LVGL cache,
never the entire host cache. This can evict a shared file's cached pixels used
by another widget (it can reload); no host widget is deleted. Font/image buffers
are LVGL/native allocations, not pinned VM strings. There is no automatic drive
registration or resource polling. Paths also work with a drive the firmware
already registered independently of MicroCS's VFS.

### Optional bounded MicroCS VFS adapter

With both `MCS_ENABLE_LVGL=1` and `MCS_ENABLE_FS=1`, include `mcs_lvgl_fs.h`.
Register **once per host LVGL lifetime**, not per VM:

```c
static mcs_lvgl_fs_t gui_assets; /* zeroed; persists until host lv_deinit */
/* AFTER host lv_init and VFS mount setup, under the host GUI lock: */
int rc = mcs_lvgl_fs_register(&gui_assets, &app_vfs, 'S', "/assets");
/* S:/fonts/ui.fnt now reads VFS /assets/fonts/ui.fnt. Check rc. */
```

The existing VFS can use RAMFS, flash or another mounted backend with read/seek
support. No C# calls, VM allocation, whole-file buffer or filesystem cache is
added in the adapter. Open files have a fixed `MCS_LVGL_FS_MAX_FILES` limit
(default 4); exhausting it fails the open. The adapter does not require LVGL 8 `LV_USE_USER_DATA`. It is read-only, with no
write/directory callbacks; read sizes, positions and nonnegative seek offsets
are limited to INT32_MAX. Normalize-before-prefix prevents `..` from escaping
the configured VFS root; backend symlink/security behavior remains host policy.

All driver operations, registration and disable must share the GUI lock, including
native LVGL font/image use. Lock order is **GUI then VFS**. Never call into GUI
while holding the VFS lock. Serialize mount changes with asset operations; keep
VFS, mounts, backend and lock valid while assets can access them.

LVGL 8/9 register a driver pointer and have no public unregister operation.
After destroying widgets/fonts and releasing relevant decoder caches, call
`mcs_lvgl_fs_disable(&gui_assets)` under the GUI lock before destroying the VFS.
This closes open files and makes subsequent requests fail; it does **not** free
or unregister driver storage. Keep `gui_assets` valid until host `lv_deinit`.
A disabled bridge cannot be rebound within that LVGL lifetime. Only after host
shutdown may its storage be cleared/reused. VM teardown never disables a host
shared drive. This lifetime rule is mandatory even when every VM has stopped.

Bar/Slider numeric setters use -32767..32767, including range endpoints,
to avoid older native rendering arithmetic overflowing on full-int32 spans.
Spinbox values/ranges use -999999999..999999999 and explicitly assigned steps
use 1..100000000; these conservative cross-major limits also protect native
step multiplication and increment/decrement arithmetic. Spinbox values/ranges
must also fit the configured digit count (initially 5); steps may not exceed
the highest displayed digit. Shrinking the format rejects an existing value or
step that would not fit, avoiding an LVGL 9.0 formatter buffer underflow.
Formatting is tracked by the wrapper; the host must not independently mutate
a script-owned Spinbox's format. See the generated
reference for each operation's exact accepted bounds.

## Lifetime, memory and failure policy

Handles contain a slot and generation, not a native pointer. Script aliases
share the same handle. Parent/native deletion invalidates descendants; generation
exhaustion retires slots rather than reviving stale handles. The default maximum
is 32 live script widgets per VM (plus the private container). Set
`MCS_LVGL_MAX_OBJECTS` and `MCS_LVGL_EVENT_QUEUE` consistently for every translation
unit to change capacities. Repeated creation/disposal reuses bounded slots.

Widgets are **application/VM-owned, not GC-owned**: collecting a handle does not
delete a visible widget. Keep a handle if you need to dispose early. `Dispose`
deletes its subtree under the host lock; finalizers never call LVGL. Before VM
object/pin teardown, `mcs_free` automatically deletes all script-owned widgets,
removes all native registrations by deleting the private subtree, unpins delegates
and frees native attachment storage. Runtime stop/rerun/application replacement
uses that path; custom embedders must destroy the old VM on app replacement.
A failed/aborted execution leaves the VM's application resources until explicit
disposal or normal VM/application teardown; it does not reset the host GUI.

Attachment allocates one fixed native storage block through the supplied host
allocator. `mcs_lvgl_storage_size()` gives its exact size. VM userdata, delegates,
compilation and pin tables consume VM memory; LVGL objects, copied text/styles,
display buffers and OS state consume **host/LVGL memory**. These are not all in
the MicroCS pool. Never add pool usage to device-heap usage when the pool is
backed by that device heap: it would double-count physical RAM. No heap defaults
or native reserve policies are changed by the wrapper.

Wrapper native allocation, object/pin limits and VM OOM have recovery/cleanup
paths. **LVGL itself is not generally recoverable from native allocator OOM**:
some constructors and style/string operations assert or assume successful
allocation, depending on upstream release/configuration. The binding cannot
turn every such failure into a C# exception. Provision native headroom and follow
the host LVGL allocator/assert policy; tests do not establish arbitrary LVGL
native-OOM recovery. Do not use longjmp-based LVGL assertions across held locks.

## Disabled-build overhead contract

With `MCS_ENABLE_LVGL=0`, no LVGL header/library is required, the native sources
emit no LVGL runtime symbols, and the VM adds no extension slot, state, allocation,
cleanup call, timer, polling or event processing. The capability row itself is
also compiled out. `tests/lvgl/test_disabled.sh BASELINE_CHECKOUT` compares actual
VM/capability machine code, VM structure/extension count and startup memory with
the pre-feature baseline, using identical compiler/configuration. CI repeats it
against the PR base without installing LVGL. This host proof is not a promise
about arbitrary compiler/linker debug sections or every MCU toolchain.

When enabled, binding calls, bounded state, native widgets and asset loading
necessarily cost CPU/RAM. The host's rendering/decoder/allocator costs also
remain. No wrapper can offer all GUI functionality at zero enabled cost.

## Builds

### Plain CMake — existing host target only

Create/link your configured LVGL target **before** adding MicroCS:

```cmake
# Your firmware already defines a configured target: host_lvgl.
set(MICROCS_LVGL ON CACHE BOOL "" FORCE)
set(MICROCS_LVGL_TARGET host_lvgl CACHE STRING "" FORCE)
add_subdirectory(path/to/MicroCS)
target_link_libraries(firmware PRIVATE MicroCS::microcs)
```

The host target must export its include directories and configuration definitions.
`MICROCS_LVGL_DIR` adds an explicit header directory; `MICROCS_LVGL_INCLUDE` sets
an alternate header spelling. It still requires the existing target. The default
target name is `lvgl::lvgl`. No automatic download or second LVGL build occurs.
Modules must be enabled. With plain Make/manual builds, use
`-DMCS_ENABLE_LVGL=1`, matching include/config flags and the **same host library**
in `CFLAGS` / `LDLIBS`; compile the normal `src/*.c` and `modules/*/*.c` sets.
Header discovery tries `<lvgl.h>`, then `<lvgl/lvgl.h>`; `MCS_LVGL_INCLUDE` overrides
both, e.g. `-DMCS_LVGL_INCLUDE='"vendor/lvgl.h"'`.

### Firmware packaging (build hooks, not hardware certification)

- **ESP-IDF:** enable `CONFIG_MICROCS_LVGL`, set
  `CONFIG_MICROCS_LVGL_COMPONENT` to the already configured host component's name
  (default `lvgl__lvgl`). The component must export matching config/includes.
- **Zephyr:** `CONFIG_LVGL=y`, `CONFIG_MICROCS_LVGL=y`; consume Zephyr's existing
  LVGL target and attach in application setup. Supported upstream major required.
- **Arduino:** `python3 tools/make_arduino.py --lvgl`. Install/configure one host
  LVGL 8.x/9.x library. Dependency-discovery glue includes its header but never
  initializes it. No automatic dependency install or version selection occurs.
- **PlatformIO/manual SDKs:** define `MCS_ENABLE_LVGL=1` consistently and make
  your one host LVGL dependency/configuration visible to the MicroCS build.
  Packaging already includes headers/modules; no second copy is bundled.

All these builds still require native host display initialization and attachment.
Physical-device operation, RAM/timing limits and SDK-specific GUI lock behavior
must be validated on the actual firmware; generic host compilation proves none
of those hardware properties.

Official upstream contracts: [LVGL 8.4 objects](https://docs.lvgl.io/8.4/overview/object.html),
[LVGL 9.2 events](https://docs.lvgl.io/9.2/overview/event.html),
[LVGL 9.2 OS integration](https://docs.lvgl.io/9.2/porting/os.html).
