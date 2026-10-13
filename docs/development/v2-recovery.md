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

## Checkpoint18 — bounded checked images, not full TFT completion

Added DrawImage on both native classes: whole-canvas rectangle,<=4096 pixels,
exact array/List length and all integer/depth colors validated before any writes.
Derived mutable pushImage8/16 uses64word chunks/restores swap; native drawPixel
1/4 avoids packed odd-width pitfalls. No extra heap/retained pointers/callbacks,
no VLA/PROGMEM path. Physical requires Begin/LVGL detached and synchronous write
transaction; no physical ack or measured Stop-latency claim. New TFT.Image is a
legal append-only capability, guarding new RAM-only image template;216 templates.

Frozen native131556 C++ checks EACH normal/GC-stress ASan/UBSan/leaks pass plus
C# per-pixel129x3 all-depth/List/window/type/range/rejection-atomicity tests.
Five final real embedded cross-links pass; source/config/pin/artifact provenance
saved. Host85 source/image/XIP,69 scoped declarations, formatter/runtime,215 C#
templates, capability/config/package/docs and actual browser editor/gallery/full
serial Studio pass. Initial unsupported --compile command exit64 preserved; -c
retry source-image files pass. Evidence `evidence/tft-image18/` has raw logs,
source hashes and cross provenance. All local jobs complete; no source waived.

Remote predecessor7409877dd10f820eb26b7a276bb08b72e11effd8 fetched; main remains
d3def1ff534c59a6726037ccf31f61535647e857. CI snapshot cp15 91success/1running,
cp17 86success/6running, no failure, NOT reported green yet. Staged checkpoint18
patch and cumulative base patch/NOTES maintained; push/fetch/tree verification
next. More sprite operations/assets, LVGL, core/Studio/security/roadmap/hardware/
independent review remain blocking; no merge/tag/release/provisioning. Next safe
bounded sprite copy (avoid upstream odd4bit padding/overlap hazards), then other
full-scope work. No active local test jobs remain.

## Checkpoint19 — checked sprite copies and exact image work boundaries

CopyTo distinct live RAM sprites via actual readPixel/readPixelValue/drawPixel,
exact upstream-supported depth pairs16->16/8,8->8,4->4,1->1. Whole source must fit
at nonnegative coordinates,<=4096pixels; no clipping/self-overlap/transparency.
Destination palettes preserved; no callbacks/allocation/pointers retained. Avoid
upstream memcpy-overlap/odd4bit packed-row assumptions, no fast-copy/DMA claim.
Append-only TFT.SpriteCopy capability and guarded RAM-only template;217templates.

Native131557 C++ checks EACH normal/GC-stress ASan/UBSan/leaks pass; additional
C# pixel assertions for all five supported pairs/all eleven rejected pairs,
129x3 odd-row/source/outside/palette preservation, null/wrong-class/self/stale/
bounds errors. Exact4096-pixel image and copy accepted, every copied pixel checked;
4097 both rejected before writes. Five actual embedded cross-links with coherent
source/config/pin/artifact hashes pass. Host85 source/image/XIP,69 scoped assertions,
formatter/runtime,216 C#templates, capability/config/package/source-image/docs,
actual browser editor/gallery/full serial Studio pass. Raw evidence and hashes
`evidence/tft-copy19/`. Normal source/adapter fixture unchanged after runtime run;
only docs/Studio tests metadata followed, no mixed-source matrix claim.

Remote predecessor16ab100feaa3a70e88e9c1825828395b503376d7 fetched/tree-verified;
main unchangedd3def1ff534c59a6726037ccf31f61535647e857. Latest cp18 CI64success/
19running/9queued, no failure, NOT called green. All targeted local gates complete;
ONE broader make -j2 check still RUNNING as cmd_695c127bb79b4274881cfe37a6b7,
log `/data/backups/v2-tft-copy19-full-check.log`, not claimed passed; preserve source
freeze until it exits. Generated lowram node_image may differ by debug path only;
inspect and restore ONLY that generated file after completion, never blanket-reset.
Staged checkpoint19/cumulative base patches and NOTES maintained; push/treeverify
next. Continue actual LVGL built-in static fonts after full-check freeze ends;
full TFT/LVGL/core/Studio/security/roadmap/hardware/review scope still blocks merge,
tag and release. No provisioning or irreversible device action.

## Checkpoint20 — completed broader host/profile gate after19

The only local running job recorded in19, make -j2 check
(cmd_695c127bb79b4274881cfe37a6b7), EXIT0. Frozen production source stayed unchanged
through final completion; docs/recovery bookkeeping did not alter test inputs.
All default native/profile/config/GC-stress/feature-flag/ports-template checks and
11 alternate configurations EACH85/85 scripts pass. Raw full output and exact
source hashes `evidence/host-full20/`. This updates previously pending gate status;
not a new TFT hardware/LVGL/fullrelease or optimization performance claim.

Generated lowram node_image path diff backed up under
/data/backups/v2-copy19-generated-node-image.patch; exact binary validation proves
only encoded filename length60->48 and removal of six-byte ../../ prefix,
1601->1595 bytes, all others equal. Initial raw-length assumption assertion failed
while researching encoding (not a source/test failure); corrected encoded-length
proof recorded. Restored ONLY generated node_image toHEAD after job complete.
No other user/source reset. All local jobs now completed, worktree clean before
journal/evidence step. cp15 and17 public CI confirmed92/92 successful each; cp18/19
not yet called green. Remote predecessor3a602faded2c94a80b9e84847111d6d81a1c1b85
fetched/tree-verified, main stilld3def1ff534c59a6726037ccf31f61535647e857.

Staged checkpoint20 patch/cumulative base patch and NOTES maintained; push/tree
verification next. Continue actual statically owned LVGL built-in font lookup/
selection/readback with actual public8.4/9.4 declarations; fullscope/hardware/
independent security review still block merge, tag and release. No irreversible
device actions, provisioning or secret material generated.

## Checkpoint21 — real static fonts and actual compressed-decoder defect fixed

New allocation-free/no-init LvGui.HasFont, LvObject.SetFont(name[,selector]) and
native FontHeight; append-only LVGL.Font guards new template218total. Actual27
public built-in declarations EACHversion generated from unchanged exact pins,
version/config gated; Default actualLV_FONT_DEFAULT; static firmware lifetime,
no script-owned bytes/raw pointers/dynamic/files/custom font binding. Native
pointer/style readback and exact script metrics, actual rendering, all disabled/
version-specific branches, selector/arity/type/NUL/length/stale/GC tests added.

Final default8/default9 EACH835 native checks; all27fonts8/9 EACH887, ASan/UBSan/
leaks enabled. All five script goldens EACHconfiguration match. Expanded all8
caught genuine upstream8.4 compressed decoder UB (~bit_mask negative leftshift)
with original stack trace preserved; release/v8.4 research contains same site.
Explicit/OFF-default ROOT-only MICROCS_LVGL8_FONT_DECODER_FIX verifies exact source
SHA310ec9757ce6e5196a9246b4aa7d8e6a0209c89dc8c07c34f6209efdc7443a72, writes
build-local ONLYdecoder unit with unsigned-mask cast + linked version marker,
replaces one target source, does not touch/vend external files/config. Fixed unit
SHA1b508ed5748d95cb94d9a67cf8630ea9a4fcebaa46608b83bb668e550198ecb8.
Adapter failcloses compiled compression without marker/version. Exactunit byte
proof, unchangedpins, no-fix/wrong-major/foreign/hash/missing-marker failures and
2359296 unsigned-mask arithmetic cases pass. Actual compressed8 rendering now
passes, no sanitizer suppression/font removal/fake implementation. Other-build/
prebuilt compresseddecoder integration/dynamic font assets still unvalidated.

Initial standalone fonts.cs HAL assumption failed MissingMemberException in
CMake NONE (Hal disabled); corrected ONLYfixture, defaults preserved, finalgoldens
pass. Initial contract librarypath wrong, failedtrace retained; actual lib/liblvgl.a
retry passes. Distinguish these fixture/invocation errors from actual decoder UB.
Default8 early native pass preceded extra invalid-name cases; final all configs
rebuilt/retested coherently. Source/generated/test hashes in
`evidence/lvgl-fonts21/` plus all raw logs/failurelogs/cross/decoder provenance.

Five real ESP32 standalone/LVGL8/LVGL9/touch/OFF cross-links with embeddedfont
assertions pass; compile/link ONLY, no hardware run. Actual TFT131557 checks EACH
normal/GC-stress sanitizer variant preserve depths/images/copies/scopes/OOM.
Host85 source/image/XIP,69scopes,formatter/runtime,217C#templates/218total,
OFF/config/package, old7buildcontracts/version, exactgenerator/docs and actual
browser editor/gallery/fullserial Studio pass. NewCI pinned-generator/allfonts/
contracts steps configured/YAMLchecked, notyetremotecompleted. Prior18/19/20
CIconfirmed92/92success EACH. Last fullhost/profile gate20 on19source remains
lastbroadfullsuite, notcalled21pass. All localjobs complete; no activecommands.

Remote predecessor76bdfab5ab5d4169529ec0d4c53358c0b1f0a7cb tree-verified; main
unchangedd3def1ff534c59a6726037ccf31f61535647e857. Staged21/cumulativebasepatches
and NOTES maintained; push/fetch/tree verify next. Continue malicious-bytecode
stack/control-flow/type safety and remaining whole-scope LVGL/TFT/Studio/core/
security/roadmap; hardware/independent review unavailable still block release.
No merge/tag/release/provisioning/irreversible device actions or secrets.

## Checkpoint22 — image metadata/varints (WIP; broad gates running)

Predecessor9d07f501d66da041245afbddd25b4c593ee2097b remains tree-verified;
main/base d3def1ff534c59a6726037ccf31f61535647e857 unchanged. Targeted common
v1-v4 load/copy/XIP/disassembly metadata validation added: canonical/nonoverflow
varints, wide-slot-before-cast, arity/parameter types/flags/entry invariants,
nonempty code and boolean capture tags; bitmap count avoids clen+8 overflow.
NO full stack/type/initialized-local/fallthrough/exception-flow verifier; trusted
compiler-image warning retained. No sandbox/auth/signing/audit/board security claim.

Frozen original baseline409 checks passes after independent actual-size tracker;
104 malformed LOADS accepted, NEVER executed. Original sum-of-hints tracker
reported128 subsequent balance failures; cause eight old-size hints zero for
allocated one-byte zero-arity type arrays, not retained physical allocations.
Initial prototype compile errors and original tracker failure preserved/recorded;
fixed rejection prevents those allocations. Initialfixed721 checks passes.
Expanded metadata default/GC-stress/compiler-OFF2641 each and corrected float32
2641 pass ASan/UBSan/leaks; remaining matrix pending at preparation. Standalone
CMake/CTest passed; host85 source/image/XIP passed within ongoing fullmakecheck.
ActualLVGL8/9 default835/allfont887 EACH plus fivegoldens EACHconfig passed.
ActualTFT131557 normal and GCstress pass; five actualESP32 crosslinks pass,
artifact/pin hashes recorded. GCC11.5 sameflags -Os hostbytecode object text
13025->13193bytes, data/bss0; NOTMCU/runtime/performance improvement evidence.

In-flight initial matrix nominalfloat32 used nonexistent MCS_FLOAT32=1, which
is duplicate default, NOTfloat32 evidence. Runner corrected to documented
MCS_FLOAT_DOUBLE=0, separate correctedfloat32 run passes. Keep initial log,
consolidate eight real initial configs + corrected run only when all finish.
InitialStudiofull test EXIT2 jobs.cfg click unstable/detached timeout preserved;
not waived; diagnose/retest, do not call Studio gate passed.

Running/frozen production source: fullcheck cmd_26bb97c2f5fc4f9a970d73da7941;
metadata initialmatrix cmd_83ead7b8d16d43e49f2e0f1cd514; separate sanitizedhost85
then400source+400load/disasmimage fuzz cmd_a490822bf9b74c8091ca680657af. All other
jobs exited successfully except Studio initial timeout. Do not change production
sources until frozen fullcheck exits; do not claim fullmatrix/broad/fuzz pass yet.
Evidence: development/evidence/image-metadata22/. Staged/cumulativepatches and
/data/backups/NOTES.md maintained. Continue fullgates, Studio diagnosis, then
stack/control-flow/type safety and every remaining fullscope requirement.
No merge/tag/release/provisioning/irreversible security/device operations.

## Checkpoint23 — recovery after workspace snapshot rollback

Remote22 ae58878427d84c7ce1c06f07a2ccfe9f961f863b safe; exacthead CI now92/92
success. Prior computer fullmakecheck EXIT0 (all11 alternate85/85), final nine
REAL metadata sanitizer artifacts2641x8/min2113, sanitizedhost85,400source+
400load/disasmimage fuzz0findings, unchangedStudioretryALLPASSED and gallery/editor
passes were observed, but before their full rawlogs could be committed, the
computer reverted to an older snapshot on user continuation. Do NOT imply those
lost rawlogs were restored. Surviving normalized toolresults saved remotely in
evidence/recovery23/, alongside honest fresh provenance. Existing remotely
committed22 logs/manifests and previous checkpoints remain intact.

First23 preparation command encountered missing files in stale checkout; no
commit or remote push occurred from that failed attempt. Empty23 payload was
NOT pushed. Preserved existing /data/MicroCS and /data/MicroCS-v2-work; saved
own stale-journal delta to /data/backups/post-reset-owned-journal.patch. Fresh
clone /data/MicroCS-v2-recovered at exact remote22 is the ONLY active checkout.
Fresh make -j2 mcs image-metadata-test:2641/2641; source/image/XIP85/85 pass.
GCC11.5/Node24/Python3.13 still present. Restored pinned CMake3.31.6 and
clangformat18.1.8 (both now /data/toolchains/cmake-python), PlatformIO6.1.18,
Playwright-core1.58.2 and exact clean upstream LVGL8/LVGL9/TFT commits. PIO
framework/toolchain caches still need restoration for later crossbuilds.
All recoveryjobs exited. No code changed versus22, fullscope/release remains
blocked; exactheadCIgreen is not whole-task completion or security verification.
Continue debug-record arithmetic and full stack/type/controlflow safety plus
remaining LVGL/TFT/Studio/core/security/roadmap. Keep fresh logs remotely after
each step. No merge/tag/release/provisioning/irreversible device operations.

## Checkpoint24 — checked debug-record counts/PCs/line arithmetic (WIP)

Recovery23 REMOTE0fb1257ec7c40fdd0c88996d2bccee25f7aae2ca tree-verified;
onlyactivecheckout /data/MicroCS-v2-recovered; preserved stalecheckouts untouched.
Frozen load-only debug baseline177 checks passes while accepting40 malformed
loads; NEVER executed. This is acceptance/wrap/truncation evidence, NOT a claim
that UBSan reported signed overflow/memory corruption. Added bounds before
record-count multiplication/allocation, wide PC narrowing/addition and signed
line addition. Fixed v1/v2 PCs must be nondecreasing and<=clen; v3/v4 deltas
checked againstremainingPCrange/currentline unsigned32range. Valid equal/end
PCs (optimizer trailinglinecompat), zero/full32bitline andnegative line deltas
remain accepted. Same validation even withlinesstorageOFF. Format/API/opcodes
unchanged; stack/type/fallthrough/initializedlocals/exceptionflow still missing.

Expanded defaultmetadata/debug2961 checks plus host85 source/image/XIP pass;
defaultmetadata ASan/UBSan/leaks2961 pass; standaloneCMake/CTest pass;
actualTFT131557 native checks/sanitizers pass with real unchanged upstream and
physicalIOguard. All local tool/config/headerpins unchanged/recreated. Sameflags
GCC11.5 -Os hostbytecodeobject text13193->13312bytes, data/bss0; not MCUflash or
speed evidence. Python/CIYAML parse/sourcewhitespace checked.

RUNNING productionfrozen: nine-config metadata sanitizer matrix
cmd_32d95995fd4c4fafb9f28149db37; restored realfiveESP32crossbuild matrix
cmd_d62c0b67ef19419fb6feda4bdfbe. Otherjobs EXIT0. No fullnine/cross claim yet;
no source changes until all compiling runs exit. Freshlogs backed remotely in
`evidence/debug-records24/`, baselinefixture retained, staged/cumulativepatches
and NOTES maintained. Continue completedgates, then fullstack/type/controlflow
verification and remainingfullscope. No merge/tag/release/deviceprovisioning.

## Checkpoint25 — complete debug24 gates + host-only stack-flow research

Predecessor24 REMOTE0e2184e2ee13a849e62be152e05406ffa1a373de tree-verified;
exactheadCI92/92success. New production source UNCHANGED versus24. All nine
metadata/debug sanitizer configs completed:2961 EACH default/GC/compilerOFF/
XIPOFF/compact/INT64/float32/noFLOAT; min2369 (no disassembly). Real32bit five
ESP32 crosslinks pass after caches restored, actual artifacts/pins recorded.
Freshsanitizedhost85 +400source/400LOAD-DISASM-only image fuzz(seed2401)0findings.
ActualLVGL8/9 all27fonts EACH887checks +fivegoldens EACH pass ASAN/UBSAN/leaks
(with explicit unchanged8decodercompat fix). FullStudio/gallery/editorpass,
no timeout/assertion bypass. All localgate jobs exited; no activecommands.

Standalone HOST-ONLY experimental dense-PC stack-flow probe added UNDERTESTS,
NEVER linked/invoked by production. It models base/fused effects, local/capture
height bounds, joins/loops/fallthrough, call/return and TRY normal/catch/saved
height/handler contexts. Real compiler27tests+217templates xopt/-O0 =488images,
1346 function visits accepted under ASAN/UBSAN/leaks; 11selftest expectations
and3bounded fileerror paths pass with actualloader, NO images executed. Native
strict-Werror, CMake/CTest(2/2), sourcehashes/rawlogs/corpus hashes preserved.
Selftest EXPLICITLY accepts invalid class-type INHERIT with balanced heights to
expose knownTYPEGAP; NEVERexecute it. No fake fullverification/securitypass.

Densehost calloc storage NOTVMquota/MCU eligible, no dynamictype/lifetime proof;
compactbounded storage/OOM/time/GC/32bit, captured-slot lifetime, initialized
values/types, real evaluationpeaks/safecallreservations, profile/hostilecorpora,
typeguards and independentreview still required. Production trusted-compiler
warning remains. CI adds explicitly-named research step, newheadCIpending.
Evidence `evidence/stack-flow25/`, design/repro `image-stack-flow-research.md`.
Staged/cumulativepatches +NOTES kept; push/fetch/treeverify next. Continue actual
stack capacity/peak safety (including source/copy/XIP), compactverification and
remaining fullscope. No merge/tag/release/provisioning/irreversible actions.

## Checkpoint26 — defensive stack append guards (WIP, not full v2 completion)

Recovered remote25 e9caed00d8fda6db43e63352c2157a5c05aad498 exactly; main
remains d3def1ff534c59a6726037ccf31f61535647e857. New dedicated checkout
/data/MicroCS-v2-next, clean baseline worktree /data/MicroCS-v2-baseline.
No old checkout overwritten. Baseline host85 passed. No pre-fix corruption
reproducer was run and no hostile bytecode executed; that requested baseline
is explicitly NOT established by this continuation.

Production mcs_vm.c now checks growing register-stack writes, fused/global/index
expansions, default/params/helper appends and saved-handler exception capacity.
Frame/C-call reservations use remaining-slot integer comparisons, avoiding
out-of-object pointer construction. C calls reject negative argc, nonzero argc
with NULL argv and oversized counts before argument reads/receiver append.
Failed mcs_invoke restores saved handler count. Native returns reuse freed
receiver/argument slots; APIs/opcodes/images remain unchanged. Not a complete
stack/type/initialization/lifetime verifier; untrusted execution unsupported.

Fresh patched host85 ASan/UBSan/leaks passed. New post-fix trusted-source resource
suite passed all14 REAL variants: default/GCstress/XIPoff/sourceOPT/switch/compact/
INT64/float32/noFLOAT each4752 checks; compilerOFF/lowram/MCU each3240;
superopsOFF2442; min1686. Seven sources, eight capacities, source/copy/XIP,
catch/closed captured local/native callback, invalid C arguments, result alias,
near-margin native return, direct Stop/recovery, collection and physical
allocator teardown. Not an exhaustive append-path/OOM/cancellation proof.
No mutation/fuzz execution added. Test runner hashes sources/headers/fixtures.
Actual unchanged pinned TFT host-native sanitizer regression131557 passes
plus20undisposed teardowns/physicalIOguard, NOT hardware. GCC11.5 sameflags
-Os host VM object text57735->59945 B, data/bss0; MCUfootprint/performance not
established. New docs link check passed; CI YAML parsed; whitespace checked.

Preserved an initial sanitizer launch failure caused by missing SONAME aliases
in system GCC directory; local aliases under /data/toolchains/sanitizer-libs
resolved it without suppression. Pinned clangformat18.1.8/CMake3.31.6 restored
under /data/toolchains/format-cmake. Runtime source is frozen while compiling.

STILL RUNNING: full makecheck cmd_3f9f49b4cf70440fab027d681bc2;
corrected LVGL8/9allfont sanitizer/goldens cmd_18d73955ac104d7fa93532bd68ed;
corrected Studio browser/node/fullserial cmd_a1caa8a6aaaa471f955947c1ed54.
LVGL initial command used two nonexistent golden filenames; native8passed887
and first2goldens before command failure, NOT a full8/9 pass. Retain initial
log /data/stack-lvgl.log. Studio initial command lacked CHROME and could not
launch its bundled-browser path; rerun uses installed sandbox Chromium with
no assertions removed. Retain /data/stack-studio-node.log. Do not label running
jobs passed. Checked cp25combinedstatus contains0contexts/pending; paged check
read establishes92total and ONEsuccess, not an all92 pass.

SEPARATE UNCOMMITTED research delta: corrected BIN_LKS/LIS operand mappings,
new trusted-only/arithmetic runner. Sanitized load-only488trusted compilerimages/
1346functions and fourarithmetic images passed; no production integration, no
handcrafted/fileerror/selftest run in this continuation. Keep out of26payload;
checkpoint separately with docs/logs. Backups under /data/backups include staged
and cumulative binary patches. Complete remaining gates, checked benchmark
comparison, API/native/OOM/boundary coverage, crosslinks, fullverifier and every
original fullscope workstream. Hardware/reviewer gaps remain release blockers.
No merge/tag/release/provisioning/security-lock/device operations.

## Checkpoint27 — isolated probe operand correction + completed graphics/Studio

Predecessor26 REMOTE1c919bf118609cdc95f572dbe1e64e7b9cf958de; fetched and
verified local/remote tree9164db936bd37a6cc437eb20ebc0b493a0b2ee6b equal.
Production runtime/source/header files UNCHANGED versus26. Isolated HOST-ONLY
probe fixes BIN_LKS local operands1/4 and BIN_LIS1/3. New arithmetic generator
proves relevant fused opcodes actually occur in optimized disassembly and uses
large signed immediates/1024constant function. Old research probe falsely
rejects BOTH benign optimized compilerimages (sanitized LOAD ONLY); corrected
probe accepts all4 opt/noopt images. Fresh sanitized positive corpus488/488,
1346functions also passes; --trusted-only explicitly skips handcrafted selftests
and malformed-file checks. Those skipped checks/negative initialization are
NOT complete. No bytecode execution, type/initialization/lifetime/MCU-budget
proof or production integration. CI adds arithmetic positive compatibility.

Both actual pinned LVGL8/9allfonts now EACH887 native sanitizer checks +all5
basic/pointer/constructors/widgets/fonts goldens pass. Exact clean externalpins
and explicit8build-local decoder fix retained. Initial command failure with
nonexistent golden filenames preserved; corrected command uses actual repo
files, no changed assertions. ActualTFT26 evidence remains valid since production
unchanged. FullStudio corrected browser/serial/node/template suite passed;
gallery/editor scopes and dark/light geometries included. Visually inspected
phone390offline dark and desktop1400light screenshots: useful list/preview and
footer, no overlap. Initial missing-CHROME launch failure preserved. Fresh
preinstalled tools: Playwright1.63.0/Chrome153.0.8010.12, not earlier1.58.2;
actualNode/version/source/config/dependency hashes recorded in evidence.

Evidence development/evidence/stack-arithmetic27/ includes raw completed logs,
trusted corpus hashes, arithmetic/baseline results, build recipe and provenance.
Fullmakecheck cmd_3f9f49b4cf70440fab027d681bc2 STILL RUNNING (featureflag builds
observed passing; no whole-check claim). Its only generated tracked change is
examples/lowram/node_image.h; preserve until successful exit, then verify/restore
ONLY that generated path. Checked benchmark comparison/crosslinks/exactnewCI
and broaderstack/OOM/API/handler coverage remain pending, alongside original
fullscope. No merge/release/provisioning; hardware/reviewer release blockers
not waived. Save staged/cumulativepatches, push/fetch/treeverify then continue.

## Checkpoint28 — completed full host gates + checked stack-guard comparison

Predecessor27 REMOTE3b994b28b1bd83343178cd31197107e78cd6be09 treeverified
50581e4b31cfac144900fe9412053168a31abbac. Production source/header/runtime
UNCHANGED versus26. Fullmakecheck cmd_3f9f49b4cf70440fab027d681bc2 EXIT0:
host/native/config/profile/GCstress/feature flags/ports and all11 alternate
configurations EACH85/85 pass. Exact raw full log now committed, not relabeled
historical evidence. Only generated lowramheader changed during makecheck;
backup kept, binary comparison proves ONLY ../../ debugpath->relativepath and
60->48 NUL-inclusive length change,1601->1595B; allotherbytes equal. Restored
ONLY that path AFTER fullgate successful exit; no other changes discarded.
Initial comparison assumed string length excluded NUL and assertion stopped;
corrected against actual bytes, no acceptance assertion waived.

Benchmark job cmd_5014da56038444c6811d39b45e75 waited for fullgate completion
and ran identical current harness/flags against clean remote25baseline and
fixedproduction source:1250 checked samples EACH, fiveworkloads x10phases x25.
Every sample output/result and physical allocator teardown checked. Raw samples,
commands/header/source/harness/binary hashes, flags/environment/linksize,
pergroup median/p10/p90 and comparison stored in evidence/stack-benchmarks28/.
No deliberate localcompiler/test jobs competed during samples. Profiled bothraw
files:1250rows x11cols, no duplicates/nulls; compositeworkload/phase/iteration
key,25eachgroup. Constantverified=1 is expected, not a quality error. Initial
pandasread_json precision truncated decimaldigits and exactmedian crosscheck
stopped; precise_float=True restores original JSON precision. Independent
statistics.median of rawrecords now equals EVERY pandasgroup median. No raw
sample edited, dropped or remeasured to improve results.

Requested peak/retained memory and image sizes equal in all50 matchedgroups.
GCC11.5 sameflags -Os VMobject text57735->59945B, data/bss0. Timings retained
perworkload/phase; sequential baseline-then-fixed order/sharedhost effects mean
no universal speedup/statistically-established causal overhead claim. Not MCU
flash/timing/32bit/physicalperipheral evidence. Benchrunner optional-module
provenance wording updated: integrations exist but remain OFF for these flags;
no production or measured behavior changed.

Completed runtime14-config, host85sanitizers, actualTFT131557, LVGL8+9EACH887+
fivegoldens, Studio full/editor/gallery and research488+4 LOADONLY evidence
remains valid since production frozen. All local gate jobs EXIT0; no active
jobs at checkpoint preparation. ExactnewheadCI not established green; PR43
still OPEN/DRAFT/UNMERGED, main unchanged. NewC-call/native/OOM/cancellation/
exact-boundary and32bitcrosslink completeness, fulltype/controlflow/localinit/
lifetime verifier, all original unfinishedgraphics/Studio/core/security/roadmap,
actualhardware/independentreview and release gates remain BLOCKING. No hostile
bytecode executed, pre-fixcorruption reproduced, merge/tag/release/provisioning
or irreversible actions. Push/fetch/treeverify; update PR latest status without
removing historical notes. Local backups/NOTES retained, remote journal/evidence
is recovery source, not disposable local paths.

Checkpoint28 packaging note: generated benchmark CSV uses standard CRLF and
profiler text has a terminal blank line. Whitespace check initially reported
those raw-output formats; bounded .gitattributes rules preserve byte-exact raw
CSV/report evidence, analogous to existing raw-log rules. No sample/report was
rewritten to appear cleaner; source/documentation whitespace checks still apply.

## Checkpoint29 — nested method-callback Stop propagation

Predecessor28 REMOTE b1c5a07716290f7b436eff902cf9525376a2cdfe, tree verified
37f9a676201f6b031fe7c1e75b4a008aa135f7e5. Its 92 exact-head CI jobs are now
completed SUCCESS; recorded separately, not misattributed to this new source.
`mcs_invoke` now re-arms pending cancellation through a shared helper also used
by `mcs_call_internal`. Native code ignoring a nested API return must not swallow
request/steps/time/hook abort or make it catchable. No API/opcode/image changes.

Two new trusted-source method-callback fixtures add optimized/unoptimized,
source/copy/XIP, all four reasons, captured-local/GC, clean frame/stack/handler/
run-depth, subsequent valid call and complete teardown checks. Benign baseline
on28:5208 checks/48 failed output/status assertions, NO sanitizer errors or
memory-corruption reproduction. Fixed14-config sanitizer matrix EXIT0; most
5208 checks, compiler-off/lowram/MCU3560, superops-off2670, min1846, ALL0fail.
Fresh host85 sanitizer EXIT0. Actual LVGL8+9 EACH887+fivegoldens, TFT131557+
20undisposedteardowns/GCstress/physicalIOguard, all completed EXIT0. Sources
frozen for validation; exact hashes/configs/dependency pins/commands retained.

Initial fullmakecheck failed unchanged UDP lifecycle assertion; raw log retained,
no assertion suppressed. Targeted unchanged-source retry EXIT0, then WHOLE
fullmakecheck retry EXIT0 including all11 alternateconfigurations EACH85/85.
Root cause of initial failure NOT established; do not claim it was fixed.
Generatedlowramheader1601->1595B only debugpathname ../../->relative and prefix
60->48(NULinclusive), allotherbytes equal; backedup/restoredONLYthatpath AFTER
successful wholegate exit. Runtime/source docs validated. Evidence in
`docs/development/evidence/callback-cancellation29/`; backups/cumulative+staged
patches and NOTES retained locally. Push/fetch/treeverify then record remoteSHA.

Uncommitted Studio project/nav work is NOT included in29; validate/push separately
as30. Full original v2 work, broad API/OOM/peak/verifier/security/graphics/Studio/
core/roadmap/benchmarks/hardware/independentreview gates remain blocking. No
hostile bytecode execution, merge/tag/release/provisioning or irreversible action.

## Checkpoint30 — real Studio session projects, assistance and source navigation

Predecessor29 REMOTE e6f5e299f98161014bd5dc671bac27018a9efcfb treeverified
6efc269536224d2c5398f309f3f6d7624bd11463; main unchanged d3def1ff... . Runtime
and actual host binary byte-identical to29; no relabeled new runtime gate.

Implemented explicit listed open-C# membership/entry-last source bundles, bounded
per-document cross-file types/enums/rootfunctions/member assistance, exact source
name F12 navigation and inherited/overloaded definition locations with explicit
choice. Foreign locals/params/localfunctions remain scoped. Session project uses
original dirty texts without uploading/saving originals; later tabs excluded,
closedmember blocks subsetexecution. ShiftF5 invokes REAL on-devicecompiler via
serialized unique128bit temporarysource; collision/Compiler=0/duplicate-run guards,
success/error/Stop cleanup and explicit disconnected-cleanup warning. Ordinary
F5/selection/save unchanged. Actualsnapshot compiler/runtime frames map to
original files; exceptionmessage fake :line cannot outrank exact projectframe.
Stale/closed/older snapshots do not silently navigate into changed code.

New source53model/bundle assertions include actual optimized/noopt source/copy/XIP
execution, initialized/inherited fields/overloads, cache invalidation/removal,
ambiguity/scopes/payload/CRLF/Unicode/maps and bounded malformed-source rejection.
Actual Chromium projectbrowser28 PASS checks wire fakeWebSerial to REALhostshell:
UIcreate/dirtycloseconfirmation, completion/signature/hover/F12, success/errors/
Stoprecovery, originals/unrelated tabs, maps, trueCompiler=0, collisions/concurrency
and cleanup. Initial navigation/streamederror defects fixed and regressiontested;
harness wrongMapmethod/signatureselector/prepaintinput timing/toolpath errors
preserved/corrected without weakening assertions. Failure cleanup now terminates
host and Chromium; no lingering owned test loop.

32longnames found sharedmodal text horizontaloverflow. Bounded flexmodal and
scrollable/wrapped membership now pass24 useful-area/nonoverlap/viewport/cancel
assertions, desktop1400x860/phone390x844/short667x375 x dark/light. Visually inspected
three exactPNG screenshots, preserved remotely as self-contained rasterSVG,
not fabricated drawings. Broader longfilename footer/status audit remains pending.
FULLexisting Studio realserial/browser/editor/gallery/lexical/scope/formatter,
217C#templates/218total/77snippets, capabilities/metadata/package and wiki47links
ALL completed EXIT0 on frozen finalsources. New actualproject/model/layout CIsteps
added. Source/config/environment/runtime/screenshot hashes, raw failures and
completed logs in `docs/development/evidence/studio-project30/`.

Projectlimits32complete.csfiles/256KiUTF16/512KiBUTF8; membership/runmap SESSIONONLY.
No persistent manifest, dynamic imports/SDK/assets/newnamespace resolver/full
semantic engine or latency improvement claim. Existing runtime namespace/using
semantics unchanged; compiler remains authority. Checkpoint28CI92/92success,
sample29CI total92/onequeued, NOT aggregateallgreen; exact30CI pending its ownrun.
Local staged/cumulativebinary patches/NOTES prepared; push/fetch/treeverify then
record REMOTE30SHA. PR remains DRAFT/OPEN/UNMERGED; main/optionalgraphicsOFF/release
restrictions retained. Full original core/graphics/Studio/security/verifier/
roadmap/benchmarks/hardware/independentreview scope STILL BLOCKING. No merge/tag/
release/provisioning, hostile bytecode execution or sourcecloudexfiltration.

## Checkpoint31 — portable Studio snapshots, raw source preservation and status

Predecessor30 remote e42924aa3fbe01774f8e892a08cd668239750ab6 treeverified
fec3cd400e7378d7b8b816220591b774181e9ccb; exact30CI all92 loaded jobs nowSUCCESS.
Main unchanged d3def1ff... . No runtime/C/opcode/API/image/security changes;
actual mcs executable byteidentical to29. Prior runtime gates not relabeled.

Added strict portable plaintext `.microcsproj` v1 download/reopen including
unfinished unsaved drafts; new local IDs/tabs/no device binding, confirmation,
full prevalidation/fatalUTF8/2MiJSON/32members/256KiUTF16/relativelabel/schema guards.
Fresh offline browser restores memberorder/entry/text/F12 from downloaded file.
No auto-save/cloud/URLfetch/execute/originalclean/devicewrite/signing/.csproj/SDK.
Canonical source/display offset mapping fixes observed CRLF/CR literal loss on
untouched activation/export; raw formatting/paste/replace/move/copy tested,
recent browserownedundo/redo restored by bounded128/2Mi sidecache. Diagnostic
staleness compares exact rawsnapshot; ambiguous CR-only compilerline mapping
refused. Old undo beyondcache and sameview EOL-only edits STILL release gaps.

56portablemodel+80sourceassertions pass; final portablebrowser46(actualhostsource
output matches unchanged CLI, tempcleanup), existing project53/browser28,
modal24/status90 plus full existing Studio/serial/editor/completion/gallery/
scope69/lexer/highlight/formatter/caps/metadata/217C#218total77snippets/echo/wiki47
allcompletedEXIT0 on frozen finalproductionhashes. Source/timing/harness/geometry
initial failures retained and explained, no assertion suppression. Sourcehist
size/entrycaps and exactrawdiagnostic refusal directly checked. Tool Node24.14.1,
Playwright1.63.0/Chromium153, explicit NODE_PATH package location corrected.

Bounded long-label status/footer fixes pass desktop/phone/short dark/light; local
header/tabs scroll instead of widening apptrack. Exact PNG rasterSVG evidence
visually inspected, not drawings. Phone main-editor sidebar usability remains
NEXT, not claimed fixed by status geometry. Detailed logs/downloads/hash/tool/CI
provenance in evidence/studio-portable31. Local staged/cumulativebinarypatches+
NOTES prepared; push/fetch/treeverify before remoteSHA record. New31CI independent.
PR remains OPEN/DRAFT/UNMERGED. Full original scope/release gates STILL BLOCKING;
no merge/tag/release/provisioning or hostile-image execution. Continue work.
