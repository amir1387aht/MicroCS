# Tested RAM sprite scrolling checkpoint — review pending

Base: b4f670a2a778b92322bf868cba300f9b767df893. This is a bounded Phase 4 increment from the original mission, not complete TFT support or finished v2. Feature commit and tree are recorded in the external handoff after commit.

Implemented TftSprite.ScrollRect(x,y,w,h,dx,dy,fill), with pre-mutation integer, live-object, whole-fit, color and <=4096-pixel checks. Real pinned upstream handles 8/16-bit moves; overlap-safe native indexed reads/draws handle packed 1/4-bit pixels. No display transfer, allocation, retained pointer, callback or DMA. Zero movement validates and leaves pixels unchanged. Capability TFT.SpriteScroll is appended without reordering prior IDs; graphics-OFF explicitly reports false.

## Final validation

- Normal, ASan/UBSan and GC-stress: each passed 131558/131558 native checks plus the real C# snapshot contract and 20 undisposed teardowns. Exact 4096 accepted; exact 4097 rejected with all 4097 patterned pixels unchanged. Existing 8192 rejection retained.
- Actual example runs in trusted source, copied compiler-image and XIP modes: each build passed 11/11 checks. Real empty HAL is attached; no fake capabilities or physical peripheral callbacks.
- Physical-IO guard aborts actual upstream initialization as expected. TFT upstream is clean at 16e37595040eac69cd628e4bffb56fc30cad6299.
- Five real ESP32 compile/link configurations passed with the final generated fixture: standalone, LVGL8, LVGL9, touch/LVGL9, graphics-OFF. Artifact hashes and dependency pins are in esp32-exact4097-provenance.json. Compile/link ONLY; no board execution/flashing.
- Fresh ordinary host build/tests (85), capability/template metadata, document/wiki generation checks, graphics-OFF capability assertion, allocation arithmetic (311804) and RGB565 transport passed. These earlier host gates did not compile the subsequently added exact4097 fixture; the final three native and five cross-build gates did.
- Same-flags host adapter object comparison is narrowly recorded, not full firmware/MCU footprint, heap/latency or speed evidence.

## Failures and limitations

Initial exact-example runs failed because the test harness lacked Hal; retained raw log documents it. Corrected harness attaches the actual empty HAL and the unchanged example then passed source/copy/XIP. Unsupported generator-help pipe failure is disclosed separately. No suppression/waiver of sanitizer failures.

Independent code/snapshot/lifetime approval is pending. Combined CAN.FD/TFT.SpriteScroll append conflict must preserve every old entry and both additions; no combined approval yet. No new remote-CI claim. Native call cancellation occurs afterward, not midway. Physical display/board latency, complete sprites/fonts/assets/readback/DMA/platform coverage and the broader eight-phase mission remain unfinished. No main merge, tag or release is authorized.
