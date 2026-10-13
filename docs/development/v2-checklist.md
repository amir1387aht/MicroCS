# MicroCS v2 implementation and release gates

Status: work in progress. No v2 release is authorized by this checklist until **all** gates below have evidence. Existing README roadmap items remain unfinished; no checkmark is implied by a symbol or stub.

## Recovery and compatibility contract

- Base: `d3def1ff534c59a6726037ccf31f61535647e857` (current main, including gallery geometry repair).
- Dedicated branch: `feat/microcs-v2`; no existing user checkout was changed.
- Latest published version observed: v1.14.0. No new tag chosen yet.
- Preserve append-only public APIs/opcodes, language/stdlib, image v1 compatibility, source/image/XIP execution, feature flags, min/lowram/64-KB profiles, board/HAL/network/filesystem/thread behavior, shell commands and persistent REPL state.
- Inventory inputs: `evidence/v2-baseline/compatibility-files.txt`; `include/mcs.h`, `include/MicroCS.h`, `include/mcs_config.h`, `include/profiles/`, `docs/LANGUAGE.md`, `docs/STDLIB.md`, `docs/BYTECODE.md`, port/build definitions. Detailed API/behavior mapping is **pending**.
- No third-party sources vendored; optional LVGL/TFT remain off until implemented and off in release builds thereafter.

## Dependency-ordered coverage matrix

| Workstream | Acceptance evidence required | State / risks |
|---|---|---|
| Recovery | clean branch, coherent WIP commits, local binary patches, remote journal/raw results, fetch/tree verification | branch created; checkpoint journal maintained |
| Baseline | identical v1/v2 configs, test logs; compiler/REPL/load/XIP/heap/flash maps; host and MCU timing distributions | 85 host source/image/XIP tests pass; performance baseline and full matrix pending |
| Studio lexical model | highlighting/completion/signatures/hover agree on expression vs text; both verbatim prefixes, nested strings/comments, escapes, formats, incomplete inputs | quote-counting completion defect confirmed by inspection; implementation next |
| Studio broader assistance | scope/types/imports/native API coverage, placeholders, responsive cached model; serial, echo, cancellation, undo, long files, Unicode, offline/capability/visual regressions | existing formatter present; multiline literal preservation requires audit |
| Core/compiler/REPL optimization | profiling-led changes; before/after time/RAM/flash; semantics, OOM, cancellation/lifetimes, portability | pending; no performance claim |
| LVGL 8/9 | pinned real upstream matrix; coverage for enabled public families; adapter, version/link/config discovery, lifecycle/input/flush/files/fonts/drawing; all build routes; examples/docs | absent today; 8.4/9.4 docs retrieved; root pages alone do not prove API compatibility |
| TFT_eSPI / bridge | actual platform/setup support; standalone + LVGL8/9; drawing/text/sprites/touch/transactions/DMA; errors/capabilities and lifecycle tests | absent today; Arduino-oriented upstream, target-specific DMA restrictions |
| Runtime image safety | stack/control-flow/type/operand verification in all loader/XIP/disassembly routes; malicious images, sanitizers/fuzz/OOM | current security docs explicitly lack stack/type validation |
| Auth/signed artifacts | reviewed protocol, key management/replay/rate limits, fail closed, all shell/storage/jobs/debug/update paths, atomic installs/rollback/recovery; security off compatible | pending; independent review unavailable in this environment |
| Production board security | exact-board trust chain/key storage/debug/download restrictions/provisioning dry runs; residual-risk matrix | no connected hardware; irreversible actions require separate explicit confirmation |
| Arduino Wi-Fi / MQTT | real implementation, target/config lifecycle and capability tests, examples/docs | pending |
| RP2 PIO / I2S / DMA streaming | cancellation, ownership/GC, real target build + hardware evidence | pending |
| USB mass storage | VFS concurrency/permissions/power-loss/security and real board evidence | pending |
| CAN FD / event async-await | payload correctness, GC/cancellation and hardware/host parity | pending |
| Debugger / VS Code | functional source mapping/protocol/extension, access policy, UI tests/docs | pending |
| ROM-resident metadata | equivalent semantics, pointer/alignment portable; flash/RAM profiles | pending |
| Documentation / benchmarks | measured reproducible distributions/raw data/assets, complete setup/API/support/migration/security docs, all examples compiled/run, generated docs current | pending; don't replace old claims with unmeasured v2 claims |
| CI / release | entire required matrix green on exact head; reconcile main; finished scope; squash PR; main checks; verified release artifacts, optional graphics off | BLOCKED by unfinished scope and validation |

## Baseline evidence and environment

`make -j2 mcs` and `sh tests/run_tests.sh ./mcs`: **85 passed, 0 failed** on base. Logs are committed under `evidence/v2-baseline/`.
`node tests/studio/test_highlight.js`, `node tests/studio/test_capabilities.js`, `python3 tests/test_template_metadata.py`: pass, including Arduino capability manifest packaging.

Host: Linux sandbox; GCC 11.5.0, Node 24.14.1, Python 3.13.14. No CMake, .NET, ARM GCC or physical boards found during initial inspection. These are missing validation tools, not permission to waive gates. Host/simulator tests do not constitute hardware validation.

## Authoritative research and next actions

- [LVGL 8.4 docs](https://docs.lvgl.io/8.4/) and [LVGL 9.4 docs](https://docs.lvgl.io/9.4/): research inputs, **not** implemented or established latest upstream versions.
- [TFT_eSPI upstream](https://github.com/Bodmer/TFT_eSPI): Arduino/configuration constraints and processor/interface-specific DMA support; pin dependency before validation.
- Read exact upstream headers/migration and target vendor security references before designing those integrations. No custom cryptography, production test keys, silent fallback or source exfiltration.
- First focused improvement: shared Studio lexical contexts, tested through actual UI; retain working gallery and echo behavior.
- Measure baseline editor latency with source/hash, Node/browser versions and repeatable cold/cached conditions before performance claims.
- Continue full host gates, cross-toolchain setup, profiling and dependency coverage after first milestone. Hardware/security review remain explicit release blockers where required.
