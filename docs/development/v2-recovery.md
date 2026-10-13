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
