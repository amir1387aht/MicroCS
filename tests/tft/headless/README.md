# Real upstream RAM runtime tests — test-only Arduino boundary

Run explicitly with an unchanged external TFT_eSPI checkout at the inspected pin:

```sh
python3 tools/test_tft_headless.py --tft-root /absolute/path/TFT_eSPI --sanitize
python3 tools/test_tft_headless.py --tft-root /absolute/path/TFT_eSPI --sanitize \
  --gc-stress --build-dir build/tft-headless-stress
```

The test compiles and links the **actual, unchanged external TFT_eSPI.cpp**
(which includes actual upstream Sprite.cpp), actual MicroCS adapter, full VM
and modules. No replacement TFT header, sprite implementation or pre-rendered
pixel expectations substitute for the library. Dependencies are never downloaded
by this test. Exact pin and clean upstream checkout are checked.

`Arduino.h`, `SPI.h`, `Print.h` here are **test-only compatibility boundaries**:
minimal String/Print functionality and program-memory reads in ordinary host RAM.
Typed pointer constants are widened for the host pointer size; this does not prove
production flash ABI/other fonts. GLCD only, no smooth/filesystem fonts, PSRAM,
touch, transactions/DMA or actual platform registers. Every physical GPIO/SPI,
delay and unsupported Serial/random operation aborts, never returns fake success.
The harness invokes actual upstream init in a separate process and requires an
abort with the physical-IO error prefix, proving the guard is enabled. A sprite
operation accidentally reaching GPIO or SPI therefore cannot pass.

Native script assertions cover all1/4/8/16bit formats, odd bit/nibble widths,
checked row-major image arrays/Lists across64-word chunks and odd-width rows,
late bad-value/type rejection without partial drawing, bounds/size/work-limit checks,
strict bit/palette colors, parent-wide bitmap mapping, palette getter/setter,
GLCD text for low-bit sprites and invalid
inputs/depth/bounds, default quota/count, shared parent pin, GC-only parent
retention, idempotent Dispose and reuse. Native C++ tests additionally check all
65536 RGB565 words against actual16bit storage and native8bit RGB332 quantization,
20 undisposed VM teardown cycles, error/Stop teardown, actual foreground
source/image shell runs,24 repeated normal/error/Stop scopes, nested/preexisting
and foreign-VM ownership, OOM-before-GC scope rollback, persistent machine exec,
allocation-free capability
probes and measured VM allocation balance. A linker calloc fault injector forces
actual upstream palette and subsequent pixel OOM; a VM allocator fault targets the native sprite object
after reservation, followed by GC/reuse. No native library source is rewritten to
manufacture either allocation failure. ASan/UBSan/leak detection must be enabled
by the caller/environment with suitable compiler sanitizer libraries installed.

This is stronger **native RAM/runtime** evidence than arithmetic or cross-link
alone. It is **not** physical panel/readback/color/touch/OOM/PSRAM evidence, not
an Arduino-core hardware simulation, not a supported production Linux TFT port,
and not proof every upstream public API or MCU ABI is bound/tested. Real ESP32
Arduino standalone/LVGL8/LVGL9/touch/OFF cross-builds remain independent gates.
