# WIP real TFT first bridge evidence

Final complete five-environment ESP32 Arduino/TFT_eSPI/LVGL8/LVGL9/touch/default-OFF cross-compile+link logs and SHA/section-size/source/pin provenance. No hardware execution, physical display or touch validation was performed. Static section RAM excludes runtime VM/upstream heap and stack. Firmware hashes are build artifacts, not a portable bit-for-bit reproducibility guarantee. Source files/flags/configs/pins are recorded for reproducible validation.

TFT header2.5.44/manifest2.5.43 mismatch is real at pinned commit16e375...; no invented tag. No third-party sources committed here. Transport ASan/UBSan tests prove bounded pixel consumption, not electrical IO. Frozen checkpoint11 LVGL sanitizer logs are clearly separate from newly added TFT code. Browser/formatter/capability/packaging/doc gates passed. Full current host check remains separately logged and its result must not be inferred from these subset logs.

Reproduction: follow docs/TFT_ESPI.md; `make tft-transport-test`; `python3 tests/test_template_metadata.py`; explicit opt-in PlatformIO matrix in tests/tft/platformio.ini; `tools/record_tft_build.py` requires actual successful logs/artifacts and verifies dependency pins.

Checkpoint13 follow-up: the initially chosen TFT_eSPI capability name violated existing protocol v1 client grammar. It was replaced by legal `TFT`; full-vocabulary regression and full serial Studio tests now cover this. Initial cross-build logs/hashes remain unchanged as historical build evidence, not a statement that checkpoint12 had no protocol bug. Fixed-head cross-build/artifact evidence lives in the later studio-scope-protocol directory.
