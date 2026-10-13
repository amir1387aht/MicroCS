# MicroCS v2 recovery journal

## Checkpoint 1 — baseline and compatibility gates

Branch: `feat/microcs-v2`. Base: `d3def1ff534c59a6726037ccf31f61535647e857`.
Remote commit SHA: resolve branch with `git ls-remote` after checkpoint (journal is committed in the same checkpoint; it cannot contain its own hash).

Changes: release-blocking living checklist, baseline build/test logs, tracked compatibility file inventory, source TODO inventory. No core/runtime changes.
Tests: host build and 85 source/image/XIP tests passed; Studio highlighting/capability and template metadata passed. Environment: GCC 11.5.0, Node 24.14.1, Python 3.13.14, Linux; .NET/CMake/ARM GCC absent; no hardware/security reviewer.
Running jobs: none. Failures: no baseline test failures; optional tool discovery commands exit nonzero for absent tools. Full CI/performance/hardware coverage not established.
Next: implement shared lexical model for Studio interpolation completion and regressions; audit formatter literal preservation.
Local recovery files: `/data/backups/v2-baseline-build.log`, `v2-baseline-tests.log`, `v2-todo-inventory.txt`, `MICROCS_V2_HANDOFF.md`; timestamped binary patch after commit; `/data/backups/NOTES.md` tracks exact remote SHA.
Restore: clone remote branch, verify SHA/tree, read this journal/checklist, recreate dependencies, run last quick suite. Do not apply a cumulative patch to a tree already containing it.

## Checkpoint 2 — shared Studio lexical contexts

Changes: cached, offset-preserving lexer with 32-level recursion cap; completion,
parameter help, hover and highlighting share interpolation contexts. Handles
both verbatim prefixes, nested strings/comments, escaped braces, incomplete
expressions, ternaries, alignment and format sections. Literal/comment contexts
suppress suggestions and clear stale signature popups. Added Node + actual
offline browser regressions and CI steps; updated Studio documentation.

Tests passed: `node tests/studio/test_lexical.js`, `node tests/studio/test_highlight.js`,
`NODE_PATH=/data/node_modules CHROME=$(command -v chromium) node tests/studio/test_completion_context.js`.
One initial test used the offset after the dot instead of the dot; corrected the
fixture (not the implementation). Browser screenshot: local
`build/studio-context/interpolation-completion.png`.

Running job: `make -j2 test`, output `/data/backups/v2-host-test.log`; results
pending at this checkpoint. Full browser serial suite and geometry next.
No runtime/API/image changes. Formatter multiline-literal audit remains next.
Release remains blocked by checklist. Remote branch/base unchanged; resolve
remote SHA after push. Local staged and cumulative patches in `/data/backups/`.

## Checkpoint 3 — data-preserving formatter

Changes: formatter masks entire literals (not expression holes), preserves exact
multiline/incomplete literal and comment lines, and retains each LF/CRLF/CR
separator. Existing Shift+Alt+F command remains; no unfinished UI added.
Node regressions include Unicode, blank/trailing-space payloads, interpolation,
mixed endings, idempotence and actual compiler/runtime output equality for
regular-verbatim and both interpolated-verbatim prefixes. CI runs these tests.

Tests: lexical/highlighting/formatter tests pass; `test_format.js ./mcs` compiles
and runs each original/formatted pair with byte-identical stdout. The full
Studio fake-serial suite (previous lexical checkpoint) reports ALL PASSED.
Gallery geometry passed all 20 viewport/capability combinations plus keyboard,
light-theme and empty-search checks. Chrome for Testing 153.0.8010.12.
Full host `make -j2 test` still running at this checkpoint; see local log.
Next: rerun full Studio with formatter change, commit raw logs; improve baseline
benchmark harness (existing best-of measurements subtract independently timed
startup and do not verify execution errors/output).
Release remains blocked. Local staged/cumulative patches and exact fetched
remote SHA are recorded in `/data/backups/NOTES.md` after push.

## Checkpoint 4 — checked benchmark harness and completed host/browser gates

Changes: a new host raw-sample harness and runner (not a core optimization).
Each sample checks status/output; custom allocator tracks compiler+VM requested
peak/retained bytes and enforces full teardown. Separate init, compile opt/noopt,
source, image/XIP opt/noopt and cold/persistent REPL phases. Runner captures
exact source/harness/workload hashes, dirty state, build/toolchain/CPU/OS,
host link map/size, every sample and median/p10/p90 distributions. No subtracting
independent timings or fake hardware claims. Negative harness tests reject
bad output/source/count/missing files/NULs. Make/CI integration and docs added.

Completed gates: `make -j2 test` passed (source/image/XIP, config/profile headers,
capabilities, TinyFS power-cut tests, POSIX/no-OS threading, BLE simulation and
flag builds, shell/echo/network/server/lifecycle, examples/lowram); full Studio
fake-serial suite passed before AND after formatter change; gallery tests pass;
`make bench-v2-test` passes. Exact logs are committed under
`evidence/v2-host-gates/`. Baseline main has no runtime/core code changes so far.

The lowram test regenerated a tracked image only because its source-name path
changed; both versions and a binary diff were saved locally, then that sole
generated file was restored from the pre-test content. No user work discarded.
Checkpoint-3 first push returned HTTP 502 before creating tree; retry succeeded;
fetch verified local/remote tree equality.

Running: `make -j2 check` -> `/data/backups/v2-full-check.log` (full GC-stress,
feature/alternate-profile and doc matrix). Next: record CLEAN baseline source
measurements using this harness, validate raw data and checkpoint evidence;
compiler/VM profiling remains pending, as do all graphics/security/TODO gates.
No speedup/release claim. Local patches/NOTES updated after remote push.

## Checkpoint 5 — clean-source baseline raw evidence

Runtime base measured in a clean detached worktree at d3def1ff534c59a6726037ccf31f61535647e857.
Command: `python3 bench/v2/run.py --repo /data/MicroCS-v1-baseline --out /data/raw/microcs-v1-host-baseline --repetitions 25`.
1000 checked raw samples (4 workloads × 10 phases × 25); no duplicate composite
keys, missing/null samples or failed outputs. All 40 median summaries independently
cross-checked with pandas groupby. Profiler warning verified=true constant is
expected (failed runs rejected). Metadata/source hashes and small raw/provenance/
summary/logs committed under `evidence/v1-host-baseline/`; binary/link map local.

IMPORTANT: capture overlapped host gate compilation. It is preliminary evidence
of measurement/functional correctness, not an isolated performance budget or
speedup comparison. Re-run idle before optimizing/setting budgets. No performance
improvement claimed. Exact harness remote checkpoint is 997db043ea62c5a8b2c444cb6e5dfb56f9175173;
metadata local harness commit has the same verified tree but different author
identity from GitHub MCP. Content hashes pin what actually compiled.
Running full `make -j2 check` remains in `/data/backups/v2-full-check.log`.
Next: finish full gates; idle benchmark baseline; profiling/optimization;
remaining scoped Studio and graphics/security/roadmap implementation. Release
blocked. Branch/base unchanged; exact fetched SHA/patch updated in local NOTES.

## Checkpoint 6 — profiled keyword lookup optimization (WIP)

Profiled unchanged main compiler with gprof: 1000 compile-only runs × 1200
assignments; raw source/output/profile committed under evidence/compiler-profile.
Short/coarse profile only; no timing promise. Full-table keyword classification
was a source-confirmed hot-path candidate. Reordered immutable keyword table
by first byte with 27 byte-sized boundaries; scans only a matching group.
No token enum/API/image changes, extra dynamic memory or library dependency.

Tests: exhaustive lexer C unit test checks every keyword enum exactly once,
escaped/case/prefix/suffix/contextual and Unicode identifiers; passes. Default
source/image/XIP suite: 85 passed, 0 failed. Added lexer-test to host/ASan targets;
added compiler-heavy benchmark fixture and docs. Performance/RAM/flash evaluation
and exact-head full gates must still pass before calling this an accepted win.

Full make-check returned success, but its last alternate build overlapped this
source edit by ~4 seconds (log vs source mtimes); do NOT treat that mixed-snapshot
run as exact-head release evidence. It will be rerun in a separate frozen
checkpoint checkout. A cancellation attempt reported infrastructure error after
the process had exited; status confirms exit 0, no lingering gate job.
Checkpoint-5 fetch initially differed only because text payload normalized CSV
CRLF to LF; asserted numeric/raw content equality, normalized the intended local
CSV and reverified tree equality. Original raw evidence preserved in /data/raw.
Draft PR #43: https://github.com/amir1387aht/MicroCS/pull/43 ; 89 CI jobs were
queued/running/some green on checkpoint 5; no merge/release allowed.
Next: isolated before/after sample runs, frozen-checkout full checks/sanitizers,
updated CI observation; all graphics/security/roadmap gates remain pending.

## Checkpoint 7 — isolated comparable host measurements

Frozen remote candidate 8936b6d8a3a81d048733d7d750eb411521728273 and clean base
main measured sequentially before starting any other local gate builds; same
frozen harness/workloads/GCC/config. 1250 verified samples per version, 2500 total.
50 groups per version; all median/sample-key/count/nullable checks pass via
pandas plus independent sorted midpoint. Only mcs_lexer.c hash differs across
core/module/header inputs. All requested peak/retained and image sizes unchanged;
host ELF text +56 B, data/BSS unchanged. Compiler-heavy medians: compile opt
-5.6%, noopt -11.8%, source compile+run -11.0% in THIS host capture. Opposing
runtime movements are retained (collections image +1.36%, strings +0.70%,
calls +0.67%); no universal speedup or MCU extrapolation. Full raw/metadata/
summary/analysis/comparison spec and limitations remotely committed under
`evidence/isolated-host-comparison/`. Local binary/link maps under /data/raw.

Exact snapshot gates now isolated in /data/MicroCS-v2-gates (make -j2 check ->
/data/backups/v2-frozen-check.log) and /data/MicroCS-v2-sanitizers (ASan target).
Initial ASan attempt failed loading libasan.so.6, NOT a code test failure.
System GCC library files exist but SONAME symlink lookup was absent; created
user-owned /data/toolchains/sanitizer-libs links to actual GCC libasan/ubsan and
restarted with LD_LIBRARY_PATH and UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.
Exact command/output in /data/backups/v2-frozen-asan-retry.log; still running at
checkpoint. Don't waive the gate. Earlier mixed-snapshot make-check log is not
release evidence. No graphics/security/hardware review scope completed.
Draft PR #43 remains unmergeable by policy; observe CI on each NEW head.
Next: finish these frozen gates, fuzz source/image, inspect CI failure/pending
jobs, test remaining editor semantics and implement unfinished coverage.

## Checkpoint 8 — frozen host gates and actual-keyboard boundary correction

Frozen runtime checkpoint 8936b6d8a3a81d048733d7d750eb411521728273:
- `make -j2 check`: PASSED on a separate unchanged-source checkout (all config/
  flags, GC stress, docs/generators, and all eleven 85-test alternate suites).
- ASan/UBSan target: PASSED after restoring missing runtime SONAME lookup locally;
  no weakened sanitizer options. Initial missing-library failure retained.
- ASan/UBSan source fuzz 400, seed 131026: 0 findings.
- ASan/UBSan image loader/disassembler fuzz 400, seed 131026: 0 findings.
  Mutated images were NOT executed; this does not prove stack/type verification.

Visual screenshot review found the new test was setting textarea values without
redrawing source. Corrected the test to refresh the real editor. Added real
keyboard typing tests rather than only calling suggest(): these exposed a caret
boundary defect when the outer auto-paired quote is lexed as an unfinished
nested string. Insertion context BEFORE a literal opener must be code, not the
character-to-the-right's string context. Corrected via binary lookup of literal
starts; same cache/depth limits retained. Node boundary, highlighting, formatter
and actual-keyboard completion regressions pass for Console and Console members.
All browser input, not a mocked language service. Full serial Studio suite being
rerun with this JS-only correction; no C/runtime change since frozen checkpoint.
A debug probe first used nonexistent /usr/bin/chromium; re-inspection used the
actual command-v path and reproduced the bug before fixing it.

Reviewed gallery mobile dark / desktop light screenshots: useful controls and
nonoverlap; code preview remains visible on desktop (hidden deliberately on phone).
New interpolation screenshot now redraws source before capture. Last CI sample
on de89cbc head had 89 jobs: 15 success, 16 in progress, 58 queued, no completed
failures. This is NOT green CI for this new head; fetch/check exact head after push.
Completed raw gate logs under evidence/v2-frozen-gates. Local patch/remote-tree
verification in NOTES after push. Main untouched; draft #43; no tag/merge/release.
All remaining LVGL/TFT, security, broad core/REPL, Studio scope/import/navigation,
board/peripheral TODOs, API/docs/example and hardware/review gates remain pending.


## Checkpoint 9 — final Studio newline insertion regression

Corrected insertion context at a line-comment or incomplete ordinary-string end
immediately BEFORE CR/LF: assistance remains suppressed until entering code on
the following line. Standalone CR terminates line comments too; highlighting and
semantic masking agree. A reusable newline regex avoids repeatedly searching
the remaining document separately for absent CR characters. Regressions cover
CR, LF and CRLF. No C runtime change since frozen checkpoint 8936b6d.

Final shared-lexer, highlighting, formatter/compiler-output, offline real-browser
completion/signature/hover/keyboard and full fake-Web-Serial Studio suite pass.
Final serial log is retained under evidence/v2-frozen-gates; this is host/UI
validation, NOT a connected-board test. Frozen full check, sanitizers and fuzz
evidence from checkpoint 8 remains applicable only to that documented snapshot.

Recovery: checkpoint payload, intended staged binary patch and cumulative binary
patch in /data/backups; exact remote SHA/tree verification recorded in NOTES.
Draft PR #43 documents partial scope. Main unchanged; no merge/tag/release.
Exact final-head CI still needs observation; all LVGL/TFT, security, broader
compiler/REPL and Studio assistance, remaining roadmap and hardware/review gates
remain unfinished. Next: implement remaining scoped workstreams with actual
upstream/header/platform research and obtain exact-board/display/touch validation.


## Checkpoint 10 — real LVGL 8.4/9.4 first rendering/lifetime integration

Continued implementation after the premature partial handoff. No scope deferred
as complete. Real pinned upstream sources cloned outside repository:
8.4.0 4495f428630cc1741bd8bfd977f080e8460e8e8d;
9.4.0 c016f72d4c125098287be5e83c0f1abed4706ee5.
CMake 3.31.6 installed locally under /data/toolchains/cmake-python, GCC 11.5.0.
Dependency source/target/prebuilt selection is explicit; major/version/config
checked; no silent download/vendor source/config rewrite. Graphics OFF remains
dependency-free; conditional VM extension slot adds no off-mode VM storage.

Implemented actual native RGB565 partial display rendering, stride/alignment,
pointer driver/hit testing, bounded interaction event queue, generation-safe
object handles and repeated dispose/GC/VM teardown. 56 common scalar style
setters generated from real headers; versioned declaration-candidate inventory
explicitly marks unbound APIs. Config-gated constructors do NOT imply complete
widget API coverage. Text/value/range and common object subset functional.
Idle/hook polling services GUI without executing script callbacks from LVGL;
backend failures are reported, no fake flush success. Synchronous sink only.
Shell file scopes dispose new GUI after all results. Docs expose restrictions.

Passed BOTH real versions: 662 native rendering/color/lifetime checks each,
basic/pointer scripts expected output, 85 existing language tests each. Both
disabled-slider variants pass; v9 variant additionally uses color-depth 32,
8-byte stride and 64-byte alignment with true RGB565 pixel checks. Seven build
contract cases per version pass, including missing headers/config/library/
target, major mismatch, modules-off and dependency-free graphics-off. Normal
graphics-off make build and 85 language tests pass; generators/config parity
and CI YAML syntax pass. Raw logs under evidence/lvgl-first-render/.

Found/fixed real upstream differences: v8 stringifies unquoted LV_CONF_PATH;
v9 expects quoted; upstream cache option policies needed scoped CMP0077/0126.
v8 custom allocator without GC has NO lv_deinit: reject that config explicitly
instead of leaving unresettable global state. Host test v8 arena is 256 KiB,
not a claimed embedded footprint. Capability macro normally tests feature name;
LVGL explicitly maps to LvGui without constructing library resources.
Initial failure logs preserved locally; no waived tests.

Next: sanitized graphics runs, forced VM OOM/exception/Stop scope tests, all
constructors/styles functional coverage, remaining typed APIs/assets/input/
thread/async drivers and vendor build routes; implement real TFT_eSPI bridge.
LVGL wrapper remains WIP/incomplete; security/broader compiler/Studio/roadmap
and hardware/review gates remain unfinished. All release/merge actions blocked.

## Checkpoint 11 — expanded real LVGL widget API and allocation recovery

128 header-derived common widget calls plus six explicit VM-owned retained-text
calls, all 28 advertised constructors and all 56 scalar styles read back against
both real versions. One shared GUI owner pin replaces per-handle pins: 40 live
handles no longer exhaust the default 16-pin budget; owner survives child-only
reachability then finalizes after last handle is collected. Native child classes
are inferred from LVGL descriptors. Negative/unknown state masks are rejected.
Retained string buffers live until native destruction completes, not merely its
DELETE event; replacements/deletion/Dispose/VM teardown free VM-accounted memory.
Public guarded handle-constructor allocation errors are propagated as exceptions,
and new native widgets are rolled back on wrapper failure. Fault injection warms
the lazy Create member before arming, to test the handle allocation rather than
an unrelated dispatch allocation. Both versions: 796 native checks, no failures;
basic/pointer/widgets/constructors golden outputs and disabled variants passed.
Config/header generation checks and Node lexical/highlight tests passed. Earlier
checkpoint10 frozen native/basic/pointer ASan/UBSan passed; newly expanded source
still requires a separate frozen sanitizer run. CI now runs graphics sanitizers.
All logs reflect actual tested configurations, not MCU/hardware coverage.

TFT upstream inspected at commit 16e37595040eac69cd628e4bffb56fc30cad6299:
library manifest 2.5.43 but header reports 2.5.44; do NOT invent a v2.5.43 tag.
No TFT implementation or hardware claim yet. All other v2/release blockers remain.

## Checkpoint 12 — actual Arduino TFT_eSPI and LVGL8/9 bridge

Implemented OFF-by-default Arduino Tft class with actual upstream TFT_eSPI,
not mock headers: VM-accounted native object, singleton ownership, Begin/Dispose,
rotation/dimensions, bounded integer RGB565 primitives and compiled-font text.
TOUCH_CS-gated calibration uses actual upstream setTouch/getTouch; missing support
raises. No hardware initialization from capability probes. No silent dependency
fetch/setup rewrite/unsupported-platform fallback. Standalone host enablement
reports the required Arduino port. Driver threads explicitly rejected while WIP.

Actual LVGL backend pins its native TFT owner until detach, rejects live-GUI
dispose/detach and attached rotation/calibration changes. Partial RGB565 transport
copies at most64 native-endian pixels to mutable RAM, respects padded/unaligned
rows, rejects bad windows/overflow, restores swap state and finishes synchronous
SPI transactions. Real RAM pushImage overload used, not width-sized PROGMEM VLA.
TFT_eSPI has no per-transfer IO acknowledgement: consumption is not a hardware
presence/electrical success claim. No DMA/async falsely advertised.

Actual ESP32 Arduino2.0.17, espressif32 platform6.10.0, Xtensa GCC8.4 cross-built
and linked standalone, LVGL8.4, LVGL9.4 and TOUCH_CS+LVGL9.4. Final five-environment
matrix additionally includes graphics OFF with only MicroCS as a dependency.
Fixture wiring is NOT a hardware recommendation and nothing was flashed.
Pinned upstream TFT commit16e375... manifest2.5.43/header2.5.44 discrepancy documented.
Raw final logs and artifact SHA/section-size/source/pin provenance are recorded
under evidence/tft-first-bridge after the complete final matrix. Static section
RAM excludes all runtime VM/upstream heap/stack, so it is not peak MCU RAM.

Transport ASan/UBSan contract passed (unaligned input, padding, chunk boundaries,
source immutability, invalid windows/overflow and sink error propagation).
Arduino packaging now includes generated LVGL .inc files, explicit --lvgl major
and --tft-espi dependencies; no third-party source/config bundled. These packaging
contracts pass, not a claim the Arduino IDE route has been physically validated.
C++ graphics-OFF translation has no symbols/dependencies; unsupported host enable
fails clearly. Default host85 tests, allocation-free capabilities, generators,
213-template metadata, formatter/runtime, real browser completion/graphics API
help and all gallery geometry states passed. Fixed generated TOC underscores
revealed by real docs-check, not a waived check. Full host make check is recorded
separately when complete. Native/constructor/widget/style/owned-text/OOM tests
for frozen checkpoint11 ec8b3a5... passed ASan/UBSan on both real LVGL versions;
all91 remote CI jobs on that checkpoint succeeded. New TFT CI gates are opt-in
modules, not default/release enablement; the exact new CI head must pass too.

Added four capability-gated LVGL label/button and TFT standalone/bridge templates,
with real headless LVGL hit-test regression on both versions. Hardware-absent
firmware exits with an explicit hint, not fabricated rendering. All templates
remain ordinary formatted C# files and generated bundle parity is checked.

No full-wrapper/full-v2 completion claimed. TFT sprites/assets/fonts/readback/DMA,
other platforms/hardware, broader LVGL and Studio semantics, core/roadmap/security
still incomplete. Independently reproduced Studio sibling-method/closed-block
local/type leakage; next increment will build cached scope-aware declarations.
No merge, tag, release, provisioning or irreversible device actions performed.

## Checkpoint 13 — scoped Studio semantics and capability protocol compatibility

Reproduced/fixed siblings' locals/parameters and closed block shadow types leaking
into completion/inference. Cached lexical structural scopes plus declaration
visibility intervals now handle method/constructor/typed or implicit lambda
parameters, for/foreach/catch scopes (including unbraced statements), local
functions, enclosing fields and incomplete bodies. Expressions inside string
interpolation still resolve the containing code scope. Ref/out/params modifiers
are stripped for type inference, not documentation. Semicolon-only records do
not accidentally own a later function's scope. Unknown types stay unknown.

Found/fixed literal-boundary bugs: semicolons inside string initializers no longer
truncate inference; commas/brackets/whitespace inside literal defaults do not
split parameters or erase class-method defaults. Shared lexer powers masks;
documentation initialization moved after lexer setup to avoid a TDZ dependency.
Tft constructor help now uses real new() syntax, not a nonexistent static factory.
Direct scope assertions and actual editor completion/signature/hover tests pass;
full host-backed fake-WebSerial Studio suite passes after the protocol correction.
Node host cache-latency samples use a recorded35KB/401-function fixture, not a
browser/MCU/universal latency claim. Parser is still WIP: not all declaration forms,
patterns, project/import/namespace semantics, nav or diagnostics are implemented.

The full serial suite exposed a regression introduced at checkpoint12: capability
name TFT_eSPI contains an underscore, invalid in protocol v1's existing strict
Studio parser. Whole reports fell back to compiler-only legacy capabilities and
hid supported templates. Renamed wire capability to legal TFT; internal compile
flag remains MCS_ENABLE_TFT_ESPI and class remains Tft. Entire manifest vocabulary
now tested against existing client grammar, including no allocations from probes.
No incompatible protocol parser/version change or weakened fallback was used.
All five actual embedded variants rebuilt and linked after correction; fresh
artifact/source/pin hashes include the capability manifest. Earlier raw evidence
remains historical, with the defect/follow-up explicitly noted instead of hidden.

Full host make check completed successfully on checkpoint12 C source: all native,
profile/config/default/GC-stress and11alternate85-script suites passed. Its only
unintended tracked output was examples/lowram/node_image.h's six-byte debug source
path prefix difference (../../examples vs examples); saved its diff and restored
only that generated file. Current capability-fix native/config/packaging gates
and software tests separately passed; exact final release still needs all gates.
No broad security/hardware/full-v2 completion claimed. No merge/tag/release.

Remote CI on checkpoint12:91success/1failure (full Studio browser job), consistent with the newly reproduced protocol vocabulary defect. This was fixed, all local serial/editor/vocabulary and rebuilt embedded gates pass; new checkpoint13 CI must complete independently. No failed CI result was called green.

## Checkpoint 14 — bounded actual TFT_eSprite RAM support

Added native8/16-bit single-frame sprites, shared parent owner pin, explicit
count/requested pixel-byte quotas (defaults16/65536), distinct upstream pixel
heap reporting, stale/invalid-depth/read-bounds errors and parent disposal guard.
Actual upstream virtual drawing dispatch is used for primitives/compiled font
text; RAM ReadPixel and physical synchronous/color-key pushes are distinguished.
Push requires begun parent/LVGL detached. No1/4-bit palette fallback: upstream
4-bit automatic palette allocation has an unchecked calloc-failure write and
must be safely adapted before exposing it. No native pointers or DMA exposed.

Added actual firmware RAM/lifetime contract (source/header parity checked),
TFT.Sprite capability, generated API/help and guarded formatted template214.
155924 shared allocation arithmetic checks pass with ASan/UBSan; capability,
packaging/config/docs and actual editor completion gates pass. Standalone,
LVGL8,LVGL9,touch-LVGL9 and graphics-OFF all cross-link with the unchanged exact
pinned upstream dependencies. Artifact/source/pin hashes and raw outputs are in
`evidence/tft-sprites14/`; static sections are not peak/runtime RAM measurements.
Native firmware assertions have not been executed on a physical board.

Previous remote head652dfbce684523970562bfc8d511f9ed4cd0bae3 has92/92 CI success;
main stilld3def1ff534c59a6726037ccf31f61535647e857 after a fresh fetch. Full host
`make -j2 check` is running with log `/data/backups/v2-sprites14-full-host.log`;
full serial Studio runs to `/data/backups/v2-sprites14-full-studio.log`. Neither
running result is called passed here. Next: finish these gates, then make real
upstream headless RAM/lifetime/fault-injection tests reproducible. Prototype
uses actual unchanged generic TFT source and a test-only Arduino boundary that
aborts hardware IO; it is not a substitute for the real ESP32 cross-builds or
hardware. Full sprite/API/LVGL/core/security/roadmap scope remains unfinished.

Recovery patch `/data/backups/v2-checkpoint-14-staged.patch`, cumulative base patch
and notes are maintained; GitHub commit containing this entry is checkpoint14.
No merge, tag, release, provisioning or irreversible device operation.

## Checkpoint 15 — real upstream sprite RAM/GC/OOM runtime evidence

Made the prototype reproducible with actual unchanged pinned TFT_eSPI.cpp/
Sprite.cpp, MicroCS adapter/fullVM and a test-only generic Arduino boundary.
Physical GPIO/SPI/delay/unsupported IO aborts, never returns fake success; actual
upstream init in a separate process must abort as a guard self-test. No replacement
TFT headers/algorithms/vendor sources or production host-port claim. Typed pointer
constants are widened for the host boundary; GLCD only, not MCU flash/PSRAM ABI.

131356 checks pass in EACH normal and MCS_GC_STRESS variant under ASan/UBSan/leak
checks, including every16bit word/native8bit quantization, embedded actual C#
RAM/lifetime assertions,20 undisposed VM teardown cycles, error/Stop VM teardown,
allocation-free capability probes, injected actual upstream calloc OOM and
VM-native-object allocation panic after reservation; rollback/reuse and measured
VM allocation balance pass. Added explicit CI gates alongside, not instead of,
real ESP32 Arduino five-way cross-link gates. Raw logs/source/toolchain hashes in
`evidence/tft-headless15/`. Initial missing test pin-map declaration and manual
retry's absent sanitizer library path are preserved as diagnosed failures, not
hidden/passed. Full script/physical/font/API/platform coverage remains pending.

Full host makecheck launched on checkpoint14 completed successfully: native,
default/config/profile/GC-stress and11alternate85-script suites. Full serial
Studio also passed. Saved generated node_image.h6byte debug-path drift patch
(`/data/backups/v2-sprites14-generated-node-image.patch`) and restored ONLY that
file after gates finished. No active baseline test jobs remain. New checkpoint14
CI was76success/16running at last check; exact final statuses require a fresh query.

Remote predecessor d3198cbd279bf8ace871c3c74378d7ef5a936e55 was fetched/tree-verified;
main remainsd3def1ff534c59a6726037ccf31f61535647e857. Recovery patch
`/data/backups/v2-checkpoint-15-staged.patch` and cumulative patches/NOTES maintained.
Next: foreground TFT cleanup (currently only VM teardown/explicit Dispose proven),
then safely adapt upstream palette OOM before exposing depth4; fuller sprite,
LVGL/assets/fonts/core/Studio/security/roadmap/hardware gates remain unfinished.
No merge/tag/release/provisioning or irreversible device action.

## Checkpoint 16 — deterministic foreground TFT/sprite scope cleanup

Added monotonic64-bit nonwrapping ownership markers and VM-owner checks; optional
public C begin/end scopes. Shell run/.run/source/image/boot-file foreground
execution ends LVGL scope first, then releases a newly created TFT and ALL its
sprites on normal/error/Stop/OOM outcomes. Reachable wrappers become disposed;
partially initialized sprites link before panicking allocations, so reservations
are released before GC. Existing/nested outer/foreign VM owners are preserved.
TFT scope end returns-1 without disposal when a live GUI uses the backend; shell
reports an unexpected blocked cleanup rather than pretending success. Ordinary
REPL, machine exec and deliberate embedding/app-image persistence are preserved.
Scheduled callbacks cannot retain foreground-owned displays after file return.

Real unchanged generic upstream+VM tests pass131509 checks in EACH normal and
GC-stress sanitizer/leak variant. Added24 repeated normal/error/Stop scopes,
nested/preexisting/foreign VM checks, failed constructor scope cleanup before GC,
actual repeated source and image file shell runs, invalidation/reuse and persistent
machine exec. Five actual ESP32 Arduino variants (standalone/LVGL8/LVGL9/touch/OFF)
all cross-link again with artifact/source/pin hashes; firmware contains actual
live-GUI refusal/post-scope assertions but has NOT been executed on a board.
Host85 language/source/image/XIP suite, capability/package/config/docs/template
and arithmetic gates pass; OFF C++ has no symbols/dependencies and non-Arduino
explicit enable fails clearly. Evidence `evidence/tft-scope16/`; no hardware/full
API/production security/v2 completion claim. Full earlier checkpoint14 host gates
remain historical evidence, not an exact-head all-profile claim for new scope code.

Checkpoint14 d3198cbd279bf8ace871c3c74378d7ef5a936e55 has92/92 successful remoteCI
checks. Predecessor checkpoint15 ec1d8399ef39dc22d272b8e8921696dce4e87c23 was fetched
and local/remote trees verified; its latest CI snapshot70success/19running/3queued,
not called green yet. Main stilld3def1ff534c59a6726037ccf31f61535647e857. All local
scope test jobs completed. Recovery staged patch `/data/backups/v2-checkpoint-16-staged.patch`,
cumulative binary base patch and NOTES maintained. Next safely bind palettes/1/4
bit colors (upstream unchecked palette calloc), then fuller sprite/LVGL assets/
fonts/build routes/core/Studio/security/roadmap/hardware scope. No merge/release.

## Checkpoint 17 — safe native palettes/bitmaps and compatible capabilities

Added exact1/4-bit depths alongside8/16. Inspected upstream createPalette's
unchecked calloc write; implemented a small real TFT_eSprite subclass that checks
its protected native32byte palette before actual upstream createSprite/default
palette initialization. The real native destructor frees it even after pixel OOM;
no library source rewrite/replacement. Quota now includes palette, padded rows,
off-screen pixel. Strict1bit/4bit index ranges prevent silent native truncation;
RGB5658/16 inputs stay unchanged. Added raw ReadValue, actual palette get/set,
parent-wide BitmapColors (explicitly NOT per-sprite), reject equal colors instead
of native inversion, and0-only1bit transparency (native ignores other keys).
Arguments are checked before physical push/begun gate; no rejected-call IO.

Real upstream native+VM sanitizer tests pass131555 checks in EACH normal and
GC-stress variant, adding palette/default entry/index roundtrips, bit/nibble odd
widths, actual packed C# primitives/GLCD and quota/error cases. Forced actual
palette calloc OOM and pixel OOM AFTER palette allocation both balance VM/native
lifetime reservations and permit reuse. Shared allocation arithmetic311804 checks
pass normally/ASan/UBSan. Five actual ESP32 standalone/LVGL8/LVGL9/touch/OFF variants
cross-link with refreshed artifact/source/pin hashes. Earlier initial tests/matrix
preceded new capability entries; final BOTH native variants and ALL five embedded
variants reran after changes, avoiding mixed-source proof. Evidence in
`evidence/tft-packed17/`, no physical/PSRAM/production host/full API validation.

Found/avoided new-template compatibility ambiguity: older TFT.Sprite firmware
lacks packed APIs. Appended legal wire TFT.Palette/TFT.Bitmap and require both in
new RAM-only example instead of assuming class availability proves depth APIs.
Legacy/offline/false/missing capability filtering, all-vocabulary grammar, OFF
negative probes and allocation-free positive probes pass. Header packaging includes
native guard without vendor headers. Generated API/help,215 formatted templates,
host85 source/image/XIP/config/docs/package, actual editor/geometry and full
serial Studio pass. Scope/native pending-child safety from checkpoint16 preserved.

Remote predecessor9c2fee8e1e2b3c0a895c3db13ab22d781175665c fetched/tree-verified;
main remainsd3def1ff534c59a6726037ccf31f61535647e857. Latest snapshots: checkpoint15
90success/2running; checkpoint16 91success/1running, no failures so far but NOT
called green until complete. No active local gate jobs remain. Staged recovery
patch `/data/backups/v2-checkpoint-17-staged.patch`, cumulative base patch/NOTES
maintained. Next fuller sprite/image/font APIs, LVGL assets/builds/core/Studio/
security/roadmap; all remaining release/hardware/review gates still block merge.
No merge/tag/release/provisioning or irreversible device action.
