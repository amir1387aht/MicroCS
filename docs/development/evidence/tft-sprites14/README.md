# Bounded actual TFT sprites — checkpoint14

All five actual ESP32 Arduino/TFT_eSPI/LVGL configurations compile and link.
Provenance JSON hashes actual firmware artifacts, source/config and upstream pins;
PIO static RAM excludes runtime heap/stack. This is not physical execution.
155924 shared allocation arithmetic assertions pass under ASan/UBSan; not native
TFT behavior by themselves. Real Studio completion and docs/config/package/capability
gates pass. Wire TFT.Sprite preserves existing protocol grammar and appends its
capability ID. The embedded native RAM/lifetime contract compiles to a2470byte
image with the actual host compiler; firmware source/header parity is checked.
No board execution or physical image/readback/DMA/IO success is claimed.

Full host make check and full serial Studio were launched on this source; their
final results belong to the next checkpoint rather than being invented here.
The previous checkpoint13 exact head has92/92 successful remoteCI checks.
Headless real-library native RAM testing is being researched separately; it is
not part of this checkpoint's reproducible test route yet.
