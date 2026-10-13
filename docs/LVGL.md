# Optional LVGL — v2 work in progress

**Not a complete wrapper or a validated production/hardware integration yet.**
This checkpoint implements real upstream headless rendering and a common object,
scalar-style, text/value and bounded input/event subset. Remaining API families,
version-specific assets/drawing/fonts/files/animations/groups, multiple displays,
async/DMA sinks, TFT bridge and vendor packaging are still release blockers.
Nothing here advertises those as implemented.

## Dependency and build contract

LVGL is **OFF by default**, unbundled, never silently downloaded. The two test
pins are upstream **8.4.0** (`4495f428630cc1741bd8bfd977f080e8460e8e8d`)
and **9.4.0** (`c016f72d4c125098287be5e83c0f1abed4706ee5`). Other minor/patch
versions fail explicitly until their contracts are validated. These are tested
pins, not a claim that they are the newest available releases.

```sh
# Download only as an explicit user/build action, outside this repository.
git clone --depth 1 --branch v8.4.0 https://github.com/lvgl/lvgl.git ../lvgl8
cmake -S . -B build/gui8 -DMICROCS_LVGL=ON -DMICROCS_LVGL_MAJOR=8 \
  -DMICROCS_LVGL_ROOT="$PWD/../lvgl8" \
  -DMICROCS_LVGL_CONFIG="$PWD/tests/lvgl/lv_conf8.h" -DMICROCS_CONFIG=NONE
cmake --build build/gui8 --target mcs test_lvgl -j2
build/gui8/test_lvgl
build/gui8/mcs tests/lvgl/basic.cs

# v9: select --branch v9.4.0, MAJOR=9, lv_conf9.h, a separate build directory.
# Make delegates to the same real CMake path; no second library implementation.
make lvgl-test LVGL_MAJOR=8 LVGL_DIR="$PWD/../lvgl8" \
  LVGL_CONFIG="$PWD/tests/lvgl/lv_conf8.h"
```

Replace the **test-only** config with your own `lv_conf.h` for firmware. Test
configs must not be presented as a board/wiring recipe: v8's simulated tests
reserve a **256-KiB upstream allocator arena**, separately from MicroCS's heap.
LVGL allocations outside the VM and display buffer must be budgeted separately.

Plain CMake / embedding selects one of:

- `MICROCS_LVGL_ROOT`: explicit real upstream source root; library sources are
  built and linked by upstream CMake.
- `MICROCS_LVGL_TARGET`: an already-created firmware CMake target, with explicit
  `MICROCS_LVGL_INCLUDE_DIR` or root. It wins over source building.
- `MICROCS_LVGL_LIBRARY`: existing prebuilt library + explicit include directory.

Every mode requires `MICROCS_LVGL_MAJOR` and an existing
`MICROCS_LVGL_CONFIG`. Configuration is not rewritten. No system/Arduino search
that could silently select an unintended ABI is performed. Headers-only,
missing files/targets, major mismatch and unsupported versions fail with setup
instructions. The same version/config must be used by wrapper and library;
**prebuilt library ABI cannot be inferred from headers alone**. Such builds
require library provenance/config consistency and aren't runtime-ABI-certified.

`#define MCS_ENABLE_LVGL 1` in a discovered config enables CMake dependency
resolution unless explicitly overridden by `MICROCS_LVGL=OFF`. For a manual
build, compile `modules/lvgl/mcs_lvgl.c`, add real headers/library and matching
`MCS_LVGL_MAJOR` and `LV_CONF_PATH`. **v8 stringifies the unquoted path token;
v9 expects a quoted C string literal.** CMake handles this difference.

Current restrictions fail explicitly: one single-threaded MicroCS-owned LVGL
instance, real OS threads disabled; v8 native RGB565 (`LV_COLOR_DEPTH=16`, no
`LV_COLOR_16_SWAP`) and teardown support (`LV_MEM_CUSTOM=0` unless a validated
GC integration is supplied). Do not enable a v8 custom allocator without GC:
upstream does not implement `lv_deinit` there. v9 uses a native RGB565 display
regardless of its default color depth; buffer alignment/stride are preserved.

Host/plain CMake is being tested. RP2/STM32 embedding inherits the CMake route
but is **not validated hardware support**. ESP-IDF component, Zephyr, Arduino
and PlatformIO packaging, locks/thread affinity and release-matrix validation
are unfinished. An explicit ESP-IDF LVGL request currently fails instead of
silently omitting a dependency.

## Script contract and actual implemented behavior

```cs
using (var gui = new LvGui(320, 240)) {
    var root = gui.Screen;
    var title = root.Create("label");
    title.Text = "MicroCS + real LVGL";
    title.SetPos(10, 10);
    title.SetStyle("text_color", 0x20A0FF);
    var bar = root.Create("bar");
    bar.SetSize(200, 20);
    bar.SetPos(10, 50);
    bar.SetRange(0, 100);
    bar.Value = 65;
    gui.Pump(40); // explicit elapsed ms; native RGB565 rendering
}
```

- `LvGui(width, height)`: dimensions 1..4096, partial draw buffer of up to 16
  lines, one owned display and pointer device. External already-initialized
  LVGL is rejected, never stolen/reinitialized/deinitialized.
- `Screen`, `Backend` (headless or configured RGB565 sink), `FlushCount`,
  `DroppedEvents`; static `Version` comes from the actual pinned headers.
- `Pump(elapsedMs)`: 0..1000, real tick increment and timer handler. Rendering
  is synchronous. Flush-ready is signalled after every sink return, including
  failure, so a failed sink doesn't deadlock rendering. Failure is an
  `IOException`, **not success**.
- Idle chaining services real LVGL while `Thread.Sleep` runs. Host/runtime hook
  polling also services long-running scripts. Embedders call
  `mcs_lvgl_poll(vm)` from the owner VM hook/main loop. It is nonblocking apart
  from the explicitly synchronous backend; backend errors abort hook/idle work.
- `Input(x,y,pressed)` drives the actual pointer input device/hit testing when
  no pointer backend is configured. No fake physical touch capability.
- `PollEvent()` returns `[LvObject, eventCode]` or null. No script execution
  occurs in LVGL callbacks. The queue is bounded by `MCS_LVGL_EVENTS`; overflow
  increments `DroppedEvents`. Deleting an object drops its stale queued events.
- `LvObject.Create(name)`: config-gated real constructors for object, label,
  button, image, arc, bar, slider, switch, checkbox, dropdown, roller, textarea,
  table, chart, canvas, line, list, menu, keyboard, spinbox, led, calendar,
  tileview, spangroup, animimg, tabview, win and spinner. Special v8/v9
  constructors are adapted explicitly. **Constructible is not complete
  widget-specific API coverage**; disabled/unbound families throw
  `NotSupportedException`, never create a substitute widget.
- `Text` on label/textarea/checkbox copies into real LVGL-owned text.
  `Value` / `SetRange` on slider/bar/arc call the real widget functions.
- `SetSize`, `SetPos`, `Width`, `Height`, `Kind`, `Parent`, `ChildCount`,
  `Child(index)`, `Clean`, `Delete`, `AddState`, `ClearState`, `SendEvent`.
  Active screen deletion is rejected; dispose its GUI instead.
- `SetStyle(property, value[, selector])`: header-derived common scalar
  setters only; property names use upstream suffixes such as `bg_color`,
  `pad_left`, `radius`. RGB is 0xRRGGBB; bool 0/1; opacity 0..255; coordinate
  styles -32767..32767. Pointer/asset/enum/registered-layout styles need typed
  ownership wrappers and currently throw `NotSupportedException`.

Handle slots are bounded by `MCS_LVGL_OBJECTS`, never expose native pointers,
and include deletion generations. LVGL delete notifications invalidate every
wrapper even after ancestor deletion, slot reuse, GUI disposal or repeat runs.
One shared owner pin keeps the GUI alive while any handle exists (not one scarce pin per widget). Dispose is idempotent.
GC/VM finalization closes live input/display resources before freeing VM buffers.
Shell file scopes close GUIs created during foreground runs after any result,
including exceptions, OOM and Stop. Other embedders explicitly use
`mcs_lvgl_scope_begin/end`; an older retained GUI is not silently disposed.

### Common widget methods and retained text

`object.Call("method_suffix", arguments...)` dispatches **128 generated common
widget functions** from the pinned real public headers (including inline APIs),
with exact receiver kind, arity, scalar/bool/copying-string types and bounds.
The versioned JSON inventory lists exact names/signatures; unknown, disabled or
unowned pointer/asset methods raise, never return dummy success. For example:

```cs
var spin = gui.Screen.Create("spinbox");
spin.Call("set_range", 0, 100);
spin.Call("set_step", 5);
spin.Call("set_value", 10);
spin.Call("increment");
Console.WriteLine(spin.Call("get_value")); // real upstream value: 15
```

Upstream readback/clamping is preserved (for example, default LED brightness
77 clamps to 80). Nullable C string getters return null or a new VM string.
Resource counts are bounded: chart points 1..512, chart divisions 0..512,
textarea maximum 0..65535, spinbox digits 1..9 and valid separator/ranges,
calendar year/month/day 1..9999 / 1..12 / 1..31. This calendar bound does not
promise calendar-date validation such as rejecting February 31.

Additional **ownership-aware** calls copy into VM-accounted storage for
retaining upstream APIs: dropdown `set_text` / string `set_symbol`, textarea
`set_accepted_chars` / `set_password_bullet`, label/checkbox `set_text_static`.
No pointer to an unrooted temporary script string escapes. Replacements release
old storage; deletion releases it only **after** native destruction, not during
DELETE callbacks. Repeated GC/static-text/teardown needs sanitizer coverage on
each changed snapshot. Typed images/fonts/canvas/chart-series, enums and complex
assets remain unbound until their complete ownership contracts are implemented.
Native child widgets are classified using real upstream class descriptors,
not a guessed script variable type.

## Native driver backend

Before constructing a GUI, install a long-lived `mcs_lvgl_backend_t` with
`mcs_lvgl_set_backend(vm, &backend)`. NULL gives an actual headless renderer.
`flush(ud,x,y,width,height,pixels,stride,bytes)` must **synchronously consume**
native-endian RGB565; `bytes=stride*height`, padding is not visible pixels.
No pointer may be retained and no VM/GUI API may be reentered from the backend.
Return 0 on success or a negative I/O failure. Optional `read_pointer` supplies
coordinates/pressed state with the same result convention; script injection is
rejected while it owns input. Replacing a live GUI's backend is rejected.

Async/DMA requires a different completion/lifetime contract and is **unbound**
at this checkpoint. A compiled sink, bus or LVGL capability does not prove
that any external display or touch controller is physically present.

## Coverage and validation status

`docs/development/lvgl-coverage.json` records real upstream public-header
hashes, lexical declaration candidates and explicit bound/unbound scalar styles.
This is a candidate inventory, **not** exhaustive preprocessed ABI coverage.
Unbound APIs remain unsupported. Regenerate/check using:

```sh
python3 tools/gen_lvgl.py --v8 ../lvgl8 --v9 ../lvgl9 --check
```

Tests link both real libraries and exercise rendering sinks, odd widths, repeated
teardown/GC, stale handle reuse, external LVGL protection, bounded queues,
wrong-widget errors and real pointer hit testing. Both disabled-widget variants,
v9 color-depth 32 / padded stride / 64-byte alignment, dependency failures,
forced VM draw-buffer OOM, exception and execution-budget Stop scope teardown
pass. All 56 scalar style setters have real native readback tests. A frozen
first-render snapshot passed ASan/UBSan with leak detection on both real versions;
newer widget/retained-text/shared-owner changes require their own frozen run. Upstream assertion/OOM behavior,
prebuilt ABI provenance and driver latency need further validation; this is not
a production OOM/security/thread-safety guarantee.
