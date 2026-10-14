# Independent TFT ScrollRect review

Decision: APPROVE this bounded feature slice for local disposable integration testing. No full-v2, board-execution, remote CI, protected integration or release approval.

Owner exact commit b7c5650bc0e537b53472b9438344e0f70f193932; tree b2ec2d7b3f7ce72e855ee3ba858233d8f421356b. Reviewer candidate bbdf56db987ecee9345f69b8f23d1d855134e0bb; tree 1575126885a4be314150d34af43c5de12d0cf1e6. Owner code unchanged; reviewer adds only dedicated oracle .cpp/.py. Base b4f670a2a778b92322bf868cba300f9b767df893.

Reviewed runtime validation before mutation, real upstream calls, packed-depth overlap traversal, cap append, docs/examples/contracts and retained initial-failure evidence. No blocker found in this bounded contract.

Independently reproduced tools/test_tft_headless.py on clean pinned real upstream 16e37595040eac69cd628e4bffb56fc30cad6299: normal and combined ASan/UBSan + GC-stress each 131558 checks, example 11 trusted source/copied/XIP checks, physical-IO guard negative control. Independently authored oracle: 840 immutable snapshot cases, 64680 pixel comparisons, 263079 checks normal and sanitized GC, all PASS. Includes four depths, overlap/displacements/whole/interior/thin/edge rectangles, independent fill conversion, invalid-input nonmutation, custom palette/bitmap, trusted images and lifetime/error/Stop recovery. Committed runner repeated sanitized GC PASS. Raw logs retained without overwritten failures.

Commands: python3 -B tools/test_tft_headless.py --tft-root /data/deps/TFT_eSPI-upstream --build-dir build/review-tft-scroll/normal; same with --sanitize --gc-stress and sanitized-gc output; python3 -B tests/tft/reviewer_scroll_oracle.py <matching-build> [--sanitize --gc-stress]. Sanitizer runtime /data/toolchains/sanitizer-libs, detect_leaks=1/halt_on_error=1; UBSan halt_on_error=1/print_stacktrace=1. git diff --check PASS.

Limitations: five ESP32 crosslink artifacts/provenance are owner evidence inspected, not independently rebuilt here; no physical controller/display/IO run, async/DMA/viewport/rotation/other graphics completeness claim. Work cap4096 and displacement/fit restrictions retained. Native call synchronous; Stop not mid-call preemption. Original mission phases remain incomplete. Capability EOF integration must preserve CAN.FD as well as TFT.SpriteScroll and all preexisting IDs.
