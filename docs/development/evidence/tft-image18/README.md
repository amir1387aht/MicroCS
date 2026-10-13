# Checked real-native images — checkpoint18

DrawImage added to actual Tft and TftSprite: exact row-major array/List values,
whole-window bounds,1..4096 dimensions and fixed4096-pixel maximum. Entire input
validated before any write, strict depth1 bits/depth4 indices/depth8/16 RGB565.
Real mutable upstream RAM pushImage for8/16, explicit derived dispatch (not a
virtual base call),64-word stack chunks and byte-swap restoration; actual native
drawPixel for1/4 avoids packed odd-row assumptions. No native image allocation,
VLA, custom enumerator, callback, retained pointer or PROGMEM overload. Physical
path needs Begin, LVGL detached; actual synchronous transaction, no IO ack claim.
Bounded synchronous work is NOT a measured hardware Stop-latency guarantee.

131556/131556 C++ harness checks pass in EACH frozen normal/GC-stress sanitizer
variant (ASan/UBSan/leaks). Additional C# contract assertions exercise every pixel
of129x3 images in all four depths, odd rows,64/128 boundaries, Lists/subwindows,
zero/last colors, late-invalid-value/type rejection without partial drawing,
wrong kind/length, bounds, disposed/unbegun owners and4096 work-limit rejection.
The C# internal assertion count is not included in the C++ harness total.
Existing quota/OOM/scopes/teardown tests pass; physical IO guard aborts real init.
Five frozen real ESP32 standalone/LVGL8/LVGL9/touch/OFF cross-builds pass with
actual source/config/pin/artifact hashes; no board execution or physical IO proof.

Legal append-only TFT.Image capability prevents earlier firmware offering a new
method it lacks. Missing/false/legacy/offline filters, negative OFF and positive
allocation-free native probes pass. Host85 source/image/XIP, config/package,
formatter/runtime equivalence,69 scope assertions and215 C# templates/216total
pass. Real local-browser editor help, gallery geometry and full serial Studio
pass. Initial source-image command mistakenly used unsupported --compile (exit64;
raw usage output preserved); authoritative CLI -c retry succeeded separately.
No source correction or waived test was needed for that invocation error.

Full TFT API/fonts/files/DMA/copy/rotation/readback/platform/hardware coverage,
LVGL full scope, broader core/Studio/security and independent review remain
release blockers. No merge/tag/release or irreversible device operation.
