# Optional TFT_eSPI — WIP real Arduino adapter

`MCS_ENABLE_TFT_ESPI=0` by default. Default/release builds need no TFT headers,
library, setup, C++ object, or VM extension slot. Enabling it requires the Arduino
port, the actual external TFT_eSPI library, an explicit board setup and a
single-threaded VM. No source is vendored, fetched by a normal build, or silently
rewritten. Host/plain CMake/non-Arduino builds fail clearly when explicitly
requesting this adapter; a C header alone is not a linked implementation.

The currently inspected upstream **commit** is
`16e37595040eac69cd628e4bffb56fc30cad6299`. Its manifest says 2.5.43 while
`TFT_ESPI_VERSION` says 2.5.44. No `v2.5.43` tag exists at inspection time; the
commit is the reproducibility pin, not an invented release tag.

## Firmware setup

Choose the display/controller, dimensions, pins, SPI speed and fonts in your
firmware's TFT_eSPI setup, applied identically to the library and application.
The wrapper does not own or rewrite upstream `User_Setup.h`. TFT_eSPI's normal
`tft_setup.h` or global `USER_SETUP_LOADED`/setup flags can do this. Wrong pins,
voltage, SPI sharing and unsupported controllers are hardware concerns: this
adapter cannot discover or certify them. It owns one native TFT instance at a
time; do not concurrently drive the same display through another C++ instance.

- PlatformIO: explicit `lib_deps` for MicroCS and TFT_eSPI; global
  `-DMCS_ENABLE_TFT_ESPI=1`, `-DMCS_ENABLE_THREADS=0`, plus the board setup.
- Arduino library packaging: `python3 tools/make_arduino.py --tft-espi` adds the
  optional dependency and config define. Supply your own global TFT setup.
- LVGL requires a separately linked real **8.4.0 or 9.4.0**, matching global
  `MCS_LVGL_MAJOR`, `MCS_ENABLE_LVGL=1` and consistent external `lv_conf.h`.
  `make_arduino.py --lvgl 8`/`9` selects the major but does not generate or rewrite
  LVGL's configuration. LVGL8 requires native RGB565/no swap and deinit support.
  Its builtin allocator must be budgeted separately from MicroCS's heap.

The opt-in test project `tests/tft/platformio.ini` pins the ESP32 platform 6.10.0,
uses actual upstream libraries, and builds standalone, LVGL8, LVGL9 and
LVGL9+TOUCH_CS variants. Its ILI9341 pinout is a **cross-build fixture**, not a
universal safe wiring recommendation. Do not flash it without reviewing hardware.

```sh
# Dependencies cloned explicitly outside MicroCS; no default-build downloads.
export MICROCS_SOURCE_ROOT="$PWD"
export MICROCS_TFT_ROOT=/absolute/path/TFT_eSPI
export MICROCS_LVGL8_ROOT=/absolute/path/lvgl8
export MICROCS_LVGL9_ROOT=/absolute/path/lvgl9
pio run -d tests/tft -e tft-standalone -e tft-lvgl8 -e tft-lvgl9 -e tft-touch-lvgl9
```

## Available script surface

```csharp
var d = new Tft();               // allocates a native driver in the VM-accounted heap
try {
    d.Begin();                  // idempotent; initializes actual hardware
    d.Rotation = 1;             // 0..3; set before attaching LVGL
    d.Fill(0x0000);             // all drawing colors are RGB565 integers, not RGB888
    d.DrawPixel(10, 10, 0xF800);
    d.DrawLine(0, 0, 30, 30, 0x07E0);
    d.DrawRect(10, 40, 30, 20, 0xFFFF);
    d.FillRect(50, 40, 30, 20, 0x001F);
    d.DrawCircle(100, 100, 12, 0xFFFF);
    d.FillCircle(130, 100, 12, 0x07E0);
    d.TextColor(0xFFFF, 0x0000);
    d.TextSize(1);              // 1..7
    d.DrawText("MicroCS", 8, 8, 1); // font number 1..8; absent fonts raise
    Console.WriteLine(d.Width);
} finally {
    d.Dispose();               // idempotent; no retained script pointers
}
```

Coordinates are -32767..32767, rectangle dimensions 1..4096, circle radius
0..4096, colors 0..65535. Integers only: no silent narrowing from float/bool.
Text is consumed synchronously, <=4096 bytes without embedded NUL. Negative or
out-of-range arguments raise; operations on disposed/unbegun objects raise.
The native TFT object is destroyed before its VM-accounted allocation is freed.
Capability `TFT` means this linked class is available, **not** that a physical
display exists. Capability queries never initialize the display.

## Bounded native RAM sprites

`TFT.Sprite` advertises the linked `TftSprite` class, not connected hardware.
`new TftSprite(owner, width, height, depth = 16)` owns one actual upstream
TFT_eSprite/frame. Exact depths **8 and 16** are currently implemented. Depth8
accepts RGB565 script colors but upstream converts pixels to RGB332; ReadPixel
returns the resulting quantized RGB565. No silent rounding of unsupported depths.
One live parent may own up to `MCS_TFT_SPRITE_COUNT` sprites (default16); all share
**one** parent GC pin, not one pin each. Dispose parent is rejected until children
are disposed/collected. A surviving child roots the owner during GC (not past explicit foreground scope
closure); VM teardown finalizes
newer children before the older parent. Dispose is idempotent, stale calls raise.

`MCS_TFT_SPRITE_BYTES` (default65536) bounds aggregate **requested native pixel
allocation**, including upstream's one off-screen pixel: `(width*height+1)*2`
for16bit and `width*height+1` for8bit. This is not a total process RAM limit:
allocator metadata/fragmentation, stack, and native driver objects are extra.
Driver/sprite objects use the VM allocator, while upstream pixel calloc/PSRAM
is outside the VM heap; SpriteBytes reports its reserved request separately.
Reservations are published before potentially panicking VM allocations and
rolled back on failed native pixel allocation/finalization. Native pixel OOM
raises; this does not establish hardware OOM behavior without board execution.

```csharp
var display = new Tft();
var sprite = new TftSprite(display, 32, 16); // RAM operations need no Begin
try {
    sprite.Fill(0x001F);
    sprite.DrawPixel(0, 0, 0xF800);
    Console.WriteLine(sprite.ReadPixel(0, 0)); // RAM RGB565, not physical readback
    display.Begin();                         // now initializes physical hardware
    sprite.Push(10, 10);                      // synchronous, no DMA
    sprite.PushTransparent(10, 30, 0x001F);
} finally {
    sprite.Dispose();                        // children before parent
    display.Dispose();
}
```

Sprites expose Width, Height, ColorDepth, Fill, ReadPixel, Push,
PushTransparent, and the same validated DrawPixel/Line/Rect/FillRect/Circle/
FillCircle/TextColor/TextSize/DrawText as Tft. Drawing dispatch uses actual
upstream virtual sprite methods, not physical init/rotation/pushImage through
a base pointer. ReadPixel rejects out-of-bounds coordinates instead of returning
upstream's ambiguous sentinel. Push requires a begun parent with LVGL detached;
color-key conversion/byte-order restoration are actual upstream behavior.

Depth1/4, palettes, multiple frames, resize/rotation, scroll, sprite-to-sprite
and rotated pushes are **not bound yet**. The inspected upstream4bit automatic
palette allocator writes without checking calloc failure, so exposing it without
a safe ownership/allocation adapter would be a known OOM defect. No fabricated
palette success or replacement library is used. The opted-in real firmware
fixture embeds `tests/tft/sprite_contract.cs` with RAM/lifetime/invalid-argument
assertions; source/header parity and cross-link are checked. **Those assertions
have not been executed on a board**. Host arithmetic sanitizers test only the
shared allocation calculation. A separate explicit real-library headless RAM
route is documented below; it does not replace the actual Arduino cross-builds.

## Foreground file lifetime versus persistent REPL/embedding

Shell `run`/interactive `.run` (including source or image files and boot-file
execution) closes a TFT created **inside that foreground file scope**, together
with all its sprites, after normal return, exception, allocation panic or Stop.
Reachable wrappers are invalidated and later calls raise ObjectDisposedException;
rooting a child does not override explicit foreground closure. A partially
constructed sprite is linked before panicking allocations, so ending the scope
can release its reservation without waiting for GC. New runs can own a fresh TFT.
Scheduled callbacks cannot retain a foreground-owned display after file return;
create deliberately persistent firmware owners through embedding outside scopes.

Existing owners created before a scope are preserved. Ordinary REPL lines and
machine `exec` intentionally remain persistent, as do direct embedding/app-image
lifetimes; dispose explicitly or free the VM. No regression that destroys a Tft
between `var d = new Tft();` and a later REPL `d.Begin()` line is introduced.

Embedding can call `uint64_t mark = mcs_tft_scope_begin(vm)` before execution and
`mcs_tft_scope_end(vm, mark)` on **every** result. This generation marker detects
new owners without comparing possibly recycled allocation addresses. End the
matching LVGL scope **first**. TFT end returns0 for completed/no-new-owner/foreign
VM cases; it returns-1 **without disposing the TFT or its children** if a GUI still
uses its backend. The shell uses GUI-then-TFT ordering and reports an unexpected
blocked cleanup instead of fake success. Generation exhaustion raises instead
of wrapping. Tests distinguish repeated source/image file closure, nested and
preexisting owners, foreign VMs, OOM-before-GC rollback, and persistent exec.
The real firmware fixture includes live-LVGL refusal and post-scope invalidation
assertions, but these physical-backend assertions are cross-built, not board-run.

## LVGL bridge and lifetime

After `Begin`, optional touch calibration and rotation, call `d.AttachLVGL()`
**before** constructing `new LvGui(d.Width, d.Height)`. Create/render the GUI,
then dispose it before `d.DetachLVGL()`/`d.Dispose()`. Attach pins the owner
once, so GC cannot free its backend while a GUI uses it. Explicit detach
releases that pin; VM teardown also frees the native instance. Dispose/detach
are rejected while this VM still owns a live GUI. Rotation/calibration changes
are rejected while attached. `AttachLVGL`/`DetachLVGL` are not present in a
standalone build without LVGL.

The real native-RGB565 flush preserves stride, clips by rejecting an invalid
window, copies bounded 64-pixel chunks to mutable RAM, and uses TFT_eSPI's actual
RAM `pushImage` overload. It does not use the PROGMEM overload's width-sized
stack buffer or mutate/retain the LVGL buffer. SPI transactions finish before
LVGL receives flush-ready; swap-byte state is restored, including transport
failure. No DMA/asynchronous completion is advertised. TFT_eSPI's drawing APIs
return no per-transfer IO acknowledgement: returning from a call establishes
synchronous consumption, **not** successful electrical transfer or display
presence. Invalid transport geometry is reported as a backend error.

With upstream `TOUCH_CS`, `SetTouch(xOffset, xRange, yOffset, yRange, flags)`
accepts the five values from TFT_eSPI's board-specific calibration. Values are
0..65535 (nonzero ranges), flags 0..7. No invented universal calibration. Set
before attach; only calibrated native touch supplies the real LVGL pointer
callback. Without TOUCH_CS, SetTouch raises NotSupportedException; script-fed
LvGui.Input remains available. Touch uses polling and no script callback inside
LVGL; other input types remain unimplemented.

## Explicit real-library headless RAM gate

`python3 tools/test_tft_headless.py --tft-root /absolute/path/TFT_eSPI --sanitize`
compiles the actual unchanged pinned TFT_eSPI.cpp/real upstream Sprite.cpp and
MicroCS adapter/VM. The test-only generic Arduino String/Print/program-memory
boundary is not a replacement TFT implementation or production Linux port.
Physical GPIO/SPI/delay/unsupported IO aborts, never returns fake success; an
actual upstream init subprocess must abort as a harness guard self-test.
See [test boundary details](../tests/tft/headless/README.md). The GC-stress variant
uses `--gc-stress --build-dir build/tft-headless-stress`.

RAM tests cover every16-bit RGB565 value/native8-bit quantization, actual script
primitives/GLCD text, parent/child GC/Dispose,20 undisposed VM cycles, error/Stop,
allocation-free probes, actual native calloc OOM and VM-native-object allocation
panic with quota rollback/reuse. Allocator injection does not alter upstream
source. ASan/UBSan/leak detection and VM allocation accounting are independent
checks. This generic host RAM gate is **not** ESP32/PSRAM/MCU flash ABI, physical
panel/readback/touch/IO acknowledgement, full font/API or production host support.
Exact completed results belong to the recovery evidence, not an assumption that
a test script or cross-linked firmware executed successfully.

## Validation and unfinished scope

A real ESP32 Arduino core/compiler/library standalone compile+link passed;
bridge/touch cross-build results are recorded separately in recovery evidence.
Cross-compilation is not physical execution. The shared RGB565 transport has
ASan/UBSan tests for unaligned/padded buffers, partial chunks, ownership,
overflow/window rejection and sink-failure propagation. These are transport
unit tests, not a mocked TFT library substituted for real cross-builds.

This is a functional subset, **not complete v2 TFT support**. Complete sprite
depths/operations, general image/font assets, filesystem fonts, other drawing families, native readback,
DMA/async, bus sharing/latency policy, other upstream platforms and hardware
lifecycle validation remain release blockers. Hardware OOM/IO/touch behavior
must be audited on each advertised board. Security/full v2 completion is not
implied by this optional driver.
