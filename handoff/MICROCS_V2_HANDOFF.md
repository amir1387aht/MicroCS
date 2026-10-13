# MicroCS v2 — implementation handoff for the next agent

## Mission and authorization

You are the next engineering agent for **https://github.com/amir1387aht/MicroCS** (owner `amir1387aht`, default branch `main`). Deliver a substantial, complete MicroCS v2 upgrade: a faster, smaller core/compiler/REPL; optional LVGL 8/9 and TFT_eSPI integrations; a much smarter Studio; stronger optional security; an audited roadmap; complete documentation and fresh reproducible benchmarks.

**Think deeply and search enough before designing or changing anything.** Read the actual current repository, relevant past changes, and authoritative upstream documentation. Do not guess API support, copy obsolete snippets, invent measurements, or mark unfinished work complete. Work autonomously through implementation, debugging, tests, backups, a PR, and a successful release. Do not stop at a plan or a scaffold.

The user authorizes WIP branch pushes and a v2 PR, then squash merge and publish a v2 release **only after all acceptance gates are satisfied**. The earlier template-layout repair was explicitly authorized as a direct-main fix; that exception does **not** mean push this entire v2 rewrite directly to main. Never remove or break existing features to obtain better numbers. Do not claim that any engineering effort can guarantee literally zero future bugs; interpret “no bugs accepted” as no known unresolved release-blocking defects, no placeholders, no silently incomplete behavior, and strong reproducible evidence.

## Start safely; recover state before working

1. Inspect `git status`, branches, recent commits, remote main, open PRs, README, CHANGELOG, CONTRIBUTING, SECURITY, `docs/`, build integrations, all config generators, `bench/`, and tests. Fetch the latest main; **do not assume this handoff's SHA is still current**.
2. Create a clean dedicated checkout and `feat/microcs-v2` (or a unique equivalent) from current main. Preserve unrelated user changes. Never run `reset --hard`, `clean -fd`, or blanket staging in a checkout containing work you did not create.
3. A sandbox reset occurred during preparation. `/data/MicroCS` had unrelated staged changes and was deliberately left untouched. The safe preparation checkout was `/data/MicroCS-v2-prep`; it may disappear in a future reset. Remote state is authoritative.
4. Last pre-layout main was `d8986bc187a289bd3572cd6f2de428feec633ff0` (PR #42). The layout fix is tracked on backup branch `backup/template-gallery-layout-v2-prep`; confirm the final main commit and CI status separately. Do not regress these recent fixes:
   - PR #39: ESP BLE menuconfig/target gating and HTTP response handling.
   - PR #40: persistent web-server examples, network cleanup on finish/error/Stop, responsive cancellation, interpolation highlighting.
   - PR #41: allocation-free capabilities, capability-filtered templates and explicit requirements, expanded BLE examples/options/reset.
   - PR #42: one input echo in Studio, correct machine-vs-REPL echo ownership and preservation of input whitespace.
   - Template gallery repair: four rows (header, capability status, expandable body, footer), usable preview and responsive controls, `tests/studio/test_gallery_layout.js` geometry regressions.
5. Inventory actual LVGL/display code before extending it. The preparation checkout had no dedicated `ports/lvgl` directory; the README still lists LVGL bindings as planned. Do not assume a completed wrapper exists because the user mentioned a port.
6. Write a living implementation checklist with acceptance criteria, dependency order, source links, risks, compatibility inventory, test coverage, and completion evidence. Audit current TODOs; do not rely on this handoff to be exhaustive.

## Mandatory checkpoints — every good step, not just the end

After every meaningful working improvement (passing fix, build, test milestone, documentation milestone):

- Commit only your intended changes on the v2 branch and push a WIP checkpoint. Keep commits coherent and recoverable even if the final PR is squash-merged.
- Save a binary patch for uncommitted intended changes, and a cumulative binary patch against the recorded main base. Store patches and small recovery artifacts under `/data/backups/` with unique timestamps. For your clean dedicated checkout, a typical cumulative checkpoint is `git diff --binary <recorded-base-sha> > /data/backups/v2-<UTC-timestamp>.patch` after committing. Before committing, include staged changes with `git diff --cached --binary`; untracked files are not backed up by an unstaged diff.
- Update `/data/backups/NOTES.md` AND a remotely committed recovery journal (for example `docs/development/v2-recovery.md`). Record remote branch/SHA, base SHA, what changed, exact tests/results, running commands/jobs, failures, next steps, patch names, and toolchain/dependency versions. Redact credentials, user Wi-Fi passwords, private keys, tokens, and personal data.
- Back up benchmark raw data, scripts, and design decisions remotely, not just local files. `/data` and local patches alone are not guaranteed to survive a reset. Keep a remote checkpoint at every good step.
- Verify pushes by fetching the branch and comparing trees. GitHub MCP `push_files` can use a file-backed JSON payload; keep it under 1 MiB and split large changes into uniquely named chunks. Read the current tool schema; never assume an old helper survived.
- On reset: reclone the last remote checkpoint, read the recovery journal, verify SHA, restore only missing patches, recreate dependencies from pinned reproducible commands, rerun the last quick suite, then continue. Do not blindly apply the same cumulative patch twice.

## Phase 1 — evidence and compatibility contract

Establish baselines BEFORE optimizing:

- Inventory public C APIs, C# classes/methods/properties, supported language semantics, feature flags/profiles, image formats/loaders, shell protocol, scheduler/thread behavior, board ports, HAL/drivers, filesystems, radio/network APIs, and optional library integrations.
- Capture current test outputs, .NET comparisons, failure diagnostics, image/source/XIP behavior, heap/OOM/stack limits, shell responsiveness, compiler peak memory, flash sizes and link maps. Preserve old firmware/project build recipes.
- Measure compile time, source compile+run, optimized/unoptimized images, XIP, VM init, cold/warm REPL evaluation, dispatch/calls, strings/collections/classes, allocation/GC pressure, cancellation latency and realistic peripherals/network workloads.
- Use identical boards/toolchains/configurations when comparing v1 and v2. Include min/low-RAM profiles, M0/M4, host, representative ESP/RP2 targets and other supported build families. A speed win at the cost of unreported flash/RAM growth is not a win.
- Agree on evidence-based performance budgets from the baseline. Seek broad improvements in speed, peak/retained RAM and flash; report trade-offs per profile. Do not invent a fixed “2× faster” target without evidence.
- Keep existing default behavior and APIs. If a genuinely unavoidable image-format change is needed, maintain old loading where feasible; otherwise document exact compatibility boundaries and deterministic migration before adoption. Never silently execute incompatible images or drop supported semantics.

## Phase 2 — optimized core, compiler and REPL

Profile first, then implement focused improvements with before/after measurements. Candidate areas to investigate, not mandates: parser/compiler allocation lifetime, compact symbols/metadata/constant pools, ROM-resident class metadata, lookup/cache invalidation, dispatch and instruction fusion, stack/value representation, GC roots and allocation hot paths, loader/XIP validation, compiler optimizer peak memory, REPL incremental state and error recovery, shell polling and cancellation.

Requirements:

- Preserve the entire existing language, stdlib, native APIs, configuration profiles, REPL persistent state, diagnostics/line locations and correct runtime behavior. No replacing complete features with stubs or disabling them in benchmark builds to look smaller.
- Keep low-RAM and 64-KB-flash profiles viable. Avoid making a fast-host assumption (alignment, pointer width, endianness, computed goto, floating point, libc) part of the portable contract.
- Maintain correct lifetime/GC rooting for callbacks, closures and native objects; deterministic disposal on success, exception, OOM, Stop and VM reset.
- Keep Studio/shell commands and Stop usable during long loops, server accept/read, HTTP/TLS, Wi-Fi/BLE operations and background work. Repeated executions must not leak sockets, listeners, handles or callbacks.
- Any optimized path must match the unoptimized/reference semantics. Test overflow/conversions, exception behavior, ref/out, closures, patterns, arrays/ranges, threading and invalid input. Add a minimal reproducer for each discovered bug.

## Phase 3 — complete optional LVGL v8 and v9 integration

Research the supported LVGL 8.x and 9.x releases, migration notes and APIs at https://docs.lvgl.io/8.4/ and https://docs.lvgl.io/9.4/ (verify current official versions). Inspect how existing optional u8g2/other modules implement discovery, config, lifecycle and project packaging.

- Introduce an explicit optional module, OFF by default and OFF in GitHub release firmware/runtime builds. Do not vendor LVGL source, compiled third-party blobs, or fetch it silently. CI may fetch pinned dependencies for tests without committing them.
- Support selecting LVGL 8 or 9 during the build, with one stable MicroCS C#/native wrapper behavior where upstream capabilities overlap. Use a documented compatibility adapter, not a fake API on one version.
- Write a versioned coverage inventory against the supported upstream headers: objects and all enabled widget families; layout/style/theme/color; fonts/text/images/assets; events; input devices/groups/focus; displays/buffers/flush; animations/timers; filesystem integration; custom drawing and supported settings. The user wants a complete wrapper, not a few demo widgets. Explain genuinely version-specific, optional or private APIs explicitly; never advertise unsupported features as implemented. Each exposed supported API needs a functional implementation, docs and tests.
- Handle ABI/version mismatch, missing headers/libraries/config headers, `lv_conf.h`, compile-time widget/font settings, include search order, prebuilt targets versus source paths, link dependencies, C/C++ integration and transitive flags. Header discovery alone does not supply a linked LVGL library.
- Provide explicit version/root/include/library/config options using existing naming conventions. Search only sensible documented locations; allow user-specified paths with highest precedence. Detect versions from actual headers and fail with actionable instructions. Never guess an installed version, silently substitute 9 for 8, or rewrite the user's LVGL config.
- Integrate every applicable build route: Make/CMake, ESP-IDF/menuconfig, RP2, STM32, Zephyr/Kconfig, Arduino packaging, PlatformIO and embedding examples. Unsupported combinations must fail clearly or be disabled with honest capability reports.
- Design ownership-safe wrappers: stale/deleted object rejection, callback rooting, async flushing/DMA completion, thread affinity/locking, timer ticks and handler scheduling, bounded event queues, pixel-format/endian compatibility, touch input, asset lifetime, cleanup after script Stop and repeated runs.
- Include simulator/headless test backends and usable examples: basic screen, layouts/styles, common widgets, events/input, animation, images/fonts/files, multiple screens/displays where supported, touch and dashboard, LVGL with a direct HAL display driver and LVGL with TFT_eSPI. Do not block the shell while servicing GUI work.
- Report capabilities truthfully through the existing manifest and template requirements. A compiled LVGL module does not prove a physical display/touch controller is attached.
- Test both real upstream versions (not only mocked headers), representative color/buffer/configuration variants, disabled-widget builds, invalid config, missing dependencies, resource failures and lifetime errors. Mark compile-only, simulated and real-hardware results separately.

## Phase 4 — optional TFT_eSPI wrapper

Research https://github.com/Bodmer/TFT_eSPI and the upstream setup/configuration/architecture constraints.

- Optional and OFF by default/release. Do not vendor TFT_eSPI source. Support explicit paths, user setup selection and appropriate project dependency declarations without silently rewriting global library configuration.
- Provide a complete documented MicroCS wrapper for supported drawing, text/fonts, images, sprites/framebuffers, color/pixel formats, rotation, readback, touch, transactions and DMA/async operations where the chosen backend actually supports them. Unsupported operations must report capability or clear errors, never return fake success.
- Usable independently from LVGL. Also provide an opt-in LVGL display/touch bridge with flush completion, rotation, pixel ordering and safe scheduling/lifetimes for both LVGL versions.
- Respect actual supported platforms. TFT_eSPI commonly depends on Arduino APIs; do not assume it runs unchanged on a native ESP-IDF/Zephyr/STM32 project. Build a sound adapter where appropriate or document unsupported configurations and provide another LVGL HAL display backend. Do not imply “all ports” if only Arduino builds were validated.
- Test standalone and LVGL-backed combinations, missing setup/driver paths, compile-only matrix, simulator behavior and physical display/touch/DMA when hardware is available. Provide wiring/setup guides and representative full examples.

## Phase 5 — smarter, more complete Studio

Preserve the current lightweight browser/Web Serial workflow. Use VS2022 as inspiration, not a promise to reproduce an entire IDE or add heavy/cloud dependencies unnecessarily.

- Explicit regression: completion while typing `$"{Con` must suggest `Console`; members inside `$"{Console.` must work. Cover regular/verbatim/interpolated strings, `$@"` and `@$"`, escaped braces, nested interpolation expressions, nested strings/comments, multiline incomplete input, format/alignment sections and correct distinction between text and expression context.
- Build a shared resilient lexical/syntactic model for highlighting, completions, signature help, hover and diagnostics. Make semantic assistance scope-aware: locals/parameters, inferred types, class members, overloads, native APIs, namespaces where supported, project files and imports. Do not suggest unsupported .NET APIs as if MicroCS implements them.
- Expand useful snippets with placeholders/navigation, context-aware filtering, documentation links, parameter help and actionable diagnostics. Add relevant navigation/refactoring helpers only when correct and tested; no decorative UI buttons with unfinished behavior.
- Make help/templates/completion capability-aware without claiming an external peripheral is connected merely because a bus exists. Keep offline browsing, legacy firmware handling and runtime guard checks.
- Audit existing editor, completion, find/replace, indentation/brackets, undo/redo, long files, Unicode/line endings, reconnect/boot/reset, uploads, input echo, cancellation, console/plotter and template gallery behavior. Every found bug gets a regression test.
- Test keyboard access, focus, responsive dialog geometry, dark/light themes, desktop/phone/short viewport, zoom and long descriptions/requirements. Functional DOM tests alone do not catch collapsed layouts: inspect rendered screenshots and assert nonoverlap and useful visible areas.
- Keep parsing/completion responsive on larger files; debounce/cache incrementally, bound memory, avoid blocking serial handling, and measure latency. No source-code exfiltration, cloud completion requirement or secret logging by default.

## Phase 6 — optional security that has a credible threat model

Read current `docs/SECURITY.md` and `SECURITY.md` carefully. Current docs explicitly say scripts share firmware address space, shell authentication is planned, and handcrafted bytecode is not fully stack/type verified. Fix the underlying issues, not just the wording.

The user specifically wants protection even when someone can reach UART/debug ports. **Authentication inside MicroCS alone cannot stop an attacker who can replace firmware, read keys from flash, or use an unlocked debug interface.** Do not promise “unhackable.” Define tiers and a platform-specific support matrix:

1. Runtime hardening and privilege confinement.
2. Authenticated shell/Studio sessions and signed script/bytecode/update artifacts.
3. Production hardware-backed boot integrity, confidentiality where needed, protected key storage, debug/download lockdown and anti-rollback, on boards that actually support them.

Research primary platform security docs (ESP-IDF secure boot/flash encryption/JTAG/UART download security; the exact STM32/RP2/Zephyr/Arduino target's boot trust, debug protection and key-storage capabilities). Review cryptographic design against established libraries and protocols; do not invent cryptography.

Required design/implementation:

- Optional security configuration, defaults documented. When enabled, fail closed on absent keys, unsupported production requirements, failed verification, malformed images, failed authentication, rollback and provisioning errors. Unsupported boards must not silently fall back to an unsecured “secure” mode.
- Verify image stack/control-flow/type safety and operand/lifetime bounds sufficiently to reject malicious artifacts before execution, including loader/disassembler and XIP routes. Use negative tests and fuzzing to prove rejection without memory corruption. A signature does not replace parser safety.
- Authenticated sessions with reviewed key management, challenge/replay protection, suitable rate limiting and bounded parsing. Identify every access route: UART/USB shell, REPL, machine protocol, uploads/read/delete/format, jobs/autostart, network consoles, debugger, firmware update/recovery, filesystem/USB mass storage and native APIs. No bypass command or alternate boot path.
- Signed executable artifacts/updates with versioned canonical formats, trust anchors, key rotation/revocation, anti-rollback where possible, atomic installs, power-loss recovery and useful failure diagnostics that do not leak secrets. Clarify whether source scripts must be signed/disabled/compiled through a trusted path in secure mode.
- Separate VM security policy from board boot/security provisioning. Provide secure Studio/tooling flows, permission policy for HAL/network/files/threads, secret redaction and secure teardown of sessions. Keep development mode explicit and visible.
- Protected bootloader and recovery/update chain; available hardware debug/download restrictions and secure key storage. Document residual risks (fault injection, chip decapsulation, external flash, physical denial of service, unsupported silicon) rather than invent universal protection.
- **Never burn eFuses, irreversibly lock JTAG/SWD/UART/bootloader, erase keys, provision production secrets or disable recovery autonomously.** Such operations require explicit user confirmation and exact-board guidance. Use disposable fixtures/dry runs; never commit signing keys or credentials. Exposing a mock test key as production default is forbidden.
- Test auth bypasses, replay, tampering, downgrade/rollback, interruption/power cuts, parser floods, resource exhaustion, traversal/symlinks as relevant, image corruption, OOM, debug/console policy and recovery. Require an independent security review for new protocol/trust-chain design where available; if unavailable, state that limitation and do not call it audited.

Security-on may intentionally restrict unauthorized operations; this is a documented access policy, not removal of APIs. Security-off must retain existing workflows. Tests must cover both and prove there is no silent downgrade.

## Phase 7 — audit and complete the remaining TODOs

At preparation time README listed: Arduino Wi-Fi and MQTT; RP2 PIO bindings and PIO I²S/DMA streaming; USB mass storage; CAN FD payloads; async/await over hardware events; source-level debugger and VS Code extension; signed images/authenticated shell; ROM-resident metadata/LVGL. Read the current repository/issues before assuming this is the complete or still-current list.

Create a coverage/ownership/test matrix for each. Implement them properly, including platform support, config, docs/examples, resource cleanup and capability reporting. Do not tick a box merely because an API symbol or a demo exists. Integrate debugger access with security policy, USB storage with VFS consistency/concurrency and permissions, and async/thread/DMA behavior with cancellation and GC safety. Do not silently defer requested scope. If a requirement is impossible without hardware or a policy decision, record the exact blocker and ask a focused question; do not publish v2 as fully complete while knowingly leaving required features unfinished.

## Phase 8 — fresh benchmarks and complete documentation

- Replace obsolete headline benchmark tables/graphics/results with a new reproducible v2 suite and freshly measured numbers. Preserve v1 baselines/raw results and Git history for honest comparisons; retiring old published benchmarks does not mean deleting regression coverage or evidence.
- Record commit hashes, versions, CPU/board/clocks, compiler flags, enabled features/security settings, OS/emulator, timing methods, repetitions, warm/cold conditions, median/spread, code/image size and peak/retained RAM. Compare equivalent behavior/configurations and verify outputs. Distinguish wall time, MCU cycles and emulator instruction counts; no extrapolated “real hardware” numbers.
- Include core/compiler/REPL and realistic workloads, small-MCU footprint, optional module overhead and security overhead. Label unavailable hardware results as unavailable. Regenerate assets from data, not hand-edited claims.
- Rewrite README for the actual v2 capabilities: accurate status tables, setup, supported boards, optional components, build selection, minimal and full examples, security tiers and links. Existing stale “BLE not yet” status entries must be reconciled with implemented code.
- Update all relevant docs: language/API reference, configurations and generators, porting/build paths, shell/REPL/images, networking/BLE, LVGL8/9/TFT, Studio, troubleshooting, security/provisioning/recovery, migration and performance. Documentation should be complete, task-oriented and consistent, with supported/unsupported behavior explicit.
- Ensure every documented example compiles/runs in its intended profile and all Studio templates have correct requirements and guards. Keep release libraries disabled as requested and clearly explain how users opt in.

## Acceptance gates and final release workflow

Run fast targeted tests after each change and full gates at milestones. Read actual Makefile/workflows rather than blindly invoking obsolete targets. Useful existing commands include `make mcs`, `sh tests/run_tests.sh ./mcs`, `make test`, `make check`, `make asan-test`, `make ble-test`, `make u8g2-test`, `make cm-check`, `make mcu-bench`, and Studio browser/metadata/highlighting/template tests. Use compiler sanitizers, feature/profile matrix, source/image fuzzing and .NET reference comparisons. Regenerate/check stdlib/config/template/wiki docs through the existing generators.

Extend CI to cover both LVGL versions, TFT standalone and bridge configurations, security on/off and failure cases, compiler/image/REPL regressions, new TODO features, cancellation/resource lifetimes and browser semantic/visual regressions. Ensure disabled optional modules add no dependency/link requirement and are absent from release binaries. CI compile success is not hardware validation.

Before release:

1. Finish the coverage checklist; resolve every known regression, incomplete required feature, missing test and documentation gap. Report genuine environment/hardware/security-review limitations precisely. If they block the requested production guarantee, keep release blocked rather than claim success.
2. Save/push a checkpoint and raw evidence. Fetch current main, reconcile changes without discarding others, rerun affected gates.
3. Open a clearly scoped **MicroCS v2** PR. Include architecture changes, full compatibility statement, benchmark evidence, support matrices, security threat model, test results, release-default configuration and residual limitations.
4. Watch **every** required CI job. No merge while anything is pending/failed. Diagnose with job steps/annotations and local reproduction if full logs are inaccessible. Treat known toolchain/network flakiness as a reason to rerun the failed job, not to waive checks. After any change, require green checks for the exact new head.
5. Once all gates are genuinely satisfied, squash-merge the PR, verify main CI and produce the versioned release according to `.github/workflows/release.yml`. Determine the proper version/tag from current releases; do not overwrite a published tag. Check release binaries/manifests and keep LVGL/TFT_eSPI off. Publish only verified artifacts and honest release notes. If access is insufficient, state the exact pending action instead of claiming publication.
6. **Open the repository and final PR/release at the end** using the available browser if appropriate; links alone are not a claim that you opened it. Respect browser/tool restrictions and never create unnecessary authentication sessions.
7. Final handoff: short outcome-first message with PR, merge/release links, benchmark summary, test evidence and any real limitations. Keep recovery notes and branch/patch references usable for another sandbox reset.

## Done means done

All existing features remain usable; all requested supported additions have real implementations; optional components and security are correctly configurable; unsupported hardware behavior is explicit; builds/docs/examples agree; performance improvements are measured rather than advertised; shell/Studio remain responsive; no known release-blocking bugs remain; the exact release commit passes the full gates; backups survive a reset; the PR is merged and the verified v2 release is published. Do not substitute an attractive README, API stubs, fabricated benchmarks or a green subset of CI for this definition.
