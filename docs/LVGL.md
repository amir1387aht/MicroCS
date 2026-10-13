# Optional LVGL — v2 work in progress

**Not a complete wrapper or a validated production/hardware integration yet.**
This checkpoint implements real upstream headless rendering and a common object,
scalar-style, text/value and bounded input/event subset. Remaining API families,
version-specific assets/drawing/dynamic fonts/files/animations/groups, multiple displays,
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
  ownership wrappers and throw `NotSupportedException` through SetStyle. Static
  text fonts have the separate checked SetFont binding below.

Handle slots are bounded by `MCS_LVGL_OBJECTS`, never expose native pointers,
and include deletion generations. LVGL delete notifications invalidate every
wrapper even after ancestor deletion, slot reuse, GUI disposal or repeat runs.
One shared owner pin keeps the GUI alive while any handle exists (not one scarce pin per widget). Dispose is idempotent.
GC/VM finalization closes live input/display resources before freeing VM buffers.
Shell file scopes close GUIs created during foreground runs after any result,
including exceptions, OOM and Stop. Other embedders explicitly use
`mcs_lvgl_scope_begin/end`; an older retained GUI is not silently disposed.

### Static built-in fonts

Capability `LVGL.Font` distinguishes this new API from older LVGL firmware.
`LvGui.HasFont(name)` probes **compiled/configured support**, not physical hardware,
and does not initialize LVGL. Names are `"Default"` (actual LV_FONT_DEFAULT) or
exact upstream symbols such as `"lv_font_montserrat_14"`. Missing/disabled or
version-specific names return false; there is no silent font substitution.
Names must be strings <=80bytes without embedded NUL (not coerced numbers/null).

`obj.SetFont(name[, selector])` calls actual lv_obj_set_style_text_font with a
static library/firmware-owned font. Missing fonts raise NotSupportedException;
wrong names/selectors/arity and deleted handles raise. Selectors are integer
0..UINT32_MAX, default0; inherit/native state selection follows actual LVGL.
`obj.FontHeight` returns the current main-part font's **actual native line height**,
not the point size guessed from a name. Firmware's LV_FONT_DEFAULT must remain
valid for the firmware lifetime; changing/freeing an externally owned dynamic
font beneath this adapter is unsupported.

All public built-in font declarations are represented from both pinned headers:
21 Montserrat sizes8..48(step2), compressed Montserrat28, DejaVu16 Persian/Hebrew,
UNSCII8/16, plus **v8-only Montserrat12 subpixel and SimSun16 CJK**, or
**v9-only Source Han Sans SC14/16 CJK**. Each is available only if its real
LV_FONT_* flag is enabled and the upstream configuration/dependencies support it.
Default is additional to these27 declared symbols per version. **LVGL8 compressed decoding
requires the explicit compatibility build below; a compiled font flag alone is
not safe decoder provenance.** The generator
checks unchanged exact pins and header/version/output parity; inventory in
`docs/development/lvgl-fonts.json`. No source/font binaries are vendored/fetched
by ordinary builds and no user lv_conf.h is rewritten. Merely enabling all test
fonts is not a small production flash configuration.

#### Explicit LVGL8 compressed decoder compatibility

Expanded real-font ASan/UBSan tests found a native8.4.0 decoder defect:
`lv_font_fmt_txt.c` shifts the negative promoted `~bit_mask` value. The inspected
release/v8.4 maintenance source still contains that expression. We do **not**
disable UBSan, skip compressed rendering, or rewrite/vend the external library.
When LV_USE_FONT_COMPRESSED is1, the adapter fails compilation without a proven
compatible decoder; merely defining a flag cannot satisfy the required link marker.

For a source ROOT build only, explicitly select:

```sh
cmake -S . -B build/lvgl8 -DMICROCS_LVGL=ON -DMICROCS_LVGL_MAJOR=8 \
  -DMICROCS_LVGL_ROOT=/absolute/path/lvgl8 \
  -DMICROCS_LVGL_CONFIG=/absolute/path/lv_conf.h \
  -DMICROCS_LVGL8_FONT_DECODER_FIX=ON
# Make forwards the same explicit selection:
make lvgl-test LVGL_MAJOR=8 LVGL_DIR=/absolute/path/lvgl8 \
  LVGL_CONFIG=/absolute/path/lv_conf.h LVGL_DECODER_FIX=ON
```

This option is **OFF by default**, rejected on9 or prebuilt/foreign-target modes.
CMake verifies the exact reviewed8.4.0 decoder SHA256
`310ec9757ce6e5196a9246b4aa7d8e6a0209c89dc8c07c34f6209efdc7443a72`, writes a
**build-local copy of just that external unit**, changes only the mask to
`(uint32_t)(uint8_t)(~bit_mask)` before shifting, and appends a linked compatibility
marker. Original external files and user config remain unchanged. No copied
LVGL unit or font binaries are committed to MicroCS. Library source list replaces
only this unit, retaining actual upstream decoding/rendering and configuration.
Build-local `microcs-lvgl-compat/provenance.txt` records original/fixed hashes.
Unknown source hashes fail rather than applying a guessed patch.

The adapter requires the linked marker/version before font selection; a flag
without the unit fails linkage. Tests verify exact copy/cast/marker bytes,
unchanged external checkouts, wrong-version/foreign-target/unknown-hash/no-fix/
missing-marker failures and exhaustive unsigned-mask byte arithmetic, then
render all actual27 fonts under ASan/UBSan/leaks. Non-CMake/prebuilt routes must
supply an equivalently reviewed fixed decoder **and linked marker** explicitly;
that integration is not yet validated. Default uncompressed8 and ordinary9 use
the untouched library. This narrow fix is not a general LVGL decoder/security
or malicious-font guarantee; dynamic file/binary fonts remain unbound.

These bindings retain **no script font bytes**, expose no native pointer, and
never delete a static font. Native style ownership handles the static pointer
through repeated GC/object deletion/display teardown. Dynamic/binary/file font
loading, fallback chains, custom font registration and glyph/drawing output
buffers remain unbound; this is not full font/asset or Unicode layout coverage.
Real default and all27-enabled-font configurations on both versions check native
pointer selection/metrics, configured absence and actual rendering, separately
from physical display and MCU memory/timing evidence.

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
each changed snapshot. Typed images/dynamic fonts/canvas/chart-series, enums and complex
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
pass. All 56 scalar style setters have real native readback tests. Frozen checkpoint `ec8b3a5abcd14160f1a82080669645e9ca7678cd` passed ASan/UBSan
with leak detection on both real versions: 796 native checks plus all four
basic/pointer/widgets/constructors script suites. Upstream assertion/OOM behavior,
prebuilt ABI provenance and driver latency need further validation; this is not
a production OOM/security/thread-safety guarantee.

## Arduino TFT bridge

A separately enabled real synchronous TFT_eSPI backend is described in
[TFT_ESPI.md](TFT_ESPI.md). Cross-build and physical hardware evidence are separate.
