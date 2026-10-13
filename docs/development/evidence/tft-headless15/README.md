# Real upstream native sprite RAM — checkpoint15

Actual pinned upstream TFT_eSPI.cpp (including real Sprite.cpp), actual adapter
and full VM compiled with strict warnings for our sources. Test-only Arduino
boundary aborts physical IO; no TFT source/header/implementation replacement.
Both normal and GC-stress variants passed131356 checks with ASan/UBSan/leaks:
all65536 native16bit storage values/native8bit quantization, actual C# fixture,
shared pin/GC/disposal,20 undisposed VM cycles, error/Stop VM teardown,
allocation-free probes, injected actual native calloc OOM, injected VM native
object allocation panic, reservation rollback/reuse and measured VM balance.
Separate actual upstream init must abort(SIGABRT), proving no fake IO success.
These are native generic host RAM results, NOT hardware/ESP32/PSRAM/production
host-port/full-font/physical readback/DMA validation. Reproduction is in
`tests/tft/headless/README.md` and `tools/test_tft_headless.py`.

Recorded the initial missing test pin-mapping declaration build error and a
manual retry's missing sanitizer SONAME path. Both were diagnosed/fixed; complete
reproducible runs with correct boundary/installed sanitizer libraries pass.
No failed result was relabeled passed. The shared boundary explicitly widens
typed host pointer constants and uses GLCD only; no MCU flash ABI claim.

Completed checkpoint14 full host makecheck: default/native/config/profile/GC
stress and11alternate85-script suites pass. Full host-backed fake-WebSerial
Studio passes. Regenerated node_image.h debug path differs by6bytes only;
saved its diff and restored only that generated file, not unrelated changes.
Foreground TFT script-scope cleanup is the next increment: these tests establish
VM teardown cleanup, not automatic foreground disposal before that is implemented.
