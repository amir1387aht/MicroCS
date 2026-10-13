# Checked real-native sprite copies — checkpoint19

CopyTo uses actual upstream native readPixel/readPixelValue/drawPixel. Supported
pairs16->16/8,8->8,4->4,1->1; whole source must fit distinct live destination,
nonnegative position and4096-pixel maximum. Preserve target palette/bit mapping,
source pixels and outside destination pixels. Reject unsupported pairs, self,
wrong type/null/disposed targets, bounds/work limits before destination writes.
No allocation, callbacks, retained pointers or safepoints mid-call; bounded
synchronous per-pixel work, NOT fast memcpy/DMA/physical transfer/timing proof.
Avoid upstream pushToSprite's overlapping memcpy and odd4bit row assumptions.

131557/131557 C++ harness checks pass in EACH frozen normal/GC-stress ASan/UBSan/
leak variant; additional C# contract assertions check all five supported depth
pairs pixel-by-pixel129x3 (odd bit/nibble rows), all eleven rejected depth pairs,
source/outside preservation, separate source/target palettes, type/self/disposed/
bounds errors. Added exact4096-pixel image/copy acceptance with every destination
pixel checked, and exact4097 rejection. Script internal assertion count is not
included in the C++ harness count. Existing OOM/lifetime/scopes/teardown pass.
Physical IO abort guard passes; no native library rewrite/mock or fake success.

Five actual ESP32 standalone/LVGL8/LVGL9/touch/OFF cross-links pass with source,
config, dependency and ELF/BIN hashes. Host85 source/image/XIP,69 scope assertions,
formatter/runtime,216 C# templates/217total, capability/config/package/source-
image/doc gates and actual local-browser completion/gallery/full serial Studio
pass. Legal append-only TFT.SpriteCopy guards new template on older firmware.

Full make -j2 check is running separately at checkpoint creation, log under
/data/backups/v2-tft-copy19-full-check.log; NOT counted as passed here until exit
and output verification. Previous frozen checkpoint14 full suite remains the
last completed broad host/profile evidence. Full sprite/fonts/files/DMA/platform/
hardware, LVGL/core/Studio/security and independent review still block release.
No merge/tag/release or irreversible device action.
