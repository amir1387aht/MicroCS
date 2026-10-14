# Studio Phase5 bounded editable snippets — frozen handoff

Commit: 32cba4bd496f971d9885d6c5b16eb1bcee7fdd20
Tree: 73222c07330255f4ad40940b081c6b817e2a6259
Parent: b4f670a2a778b92322bf868cba300f9b767df893
Branch: work/v2-mission-studio-snippets
Checkout: /data/v2-mission/studio-snippets (clean)
Ownership: exactly snippets.js, studio.js, unique README section, new test_snippets.js and test_snippets_browser.js. No runtime, CSS, generator, CI or other lane changes.

## Behavior and reproduction

Type `for` (also fully typed label, not only prefix), select its snippet, Tab. First default `3` selected. Replace with `5`, Tab to body, Shift+Tab returns bound. Tab after last field goes to final caret. Enter/Escape commits bytes and exits; Enter does not add newline until next press. Tab wins over popup/indent while fields active.

Unique fields1..32, numeric visitation order,16Ki template/32Ki output/256 indent bound; final $0 required for fields; no mirrors/choices/transforms. Empty/adjacent/UTF16/escaped defaults covered. One normalized snapshot max1Mi plus bounded fields. Larger docs insert valid defaults but decline navigation visibly. Raw existing CRLF/CR/LF and multiline literal bytes preserved; exact clipboard paste remains source. No implicit save/run/network; ordinary local draft behavior unchanged.

Navigation invalidates on outside selection/edit, native/raw undo/redo, readonly, IME, blur and tab/last-tab closure. Own last-tab repro failed before one-line App.activate fix: last-tab-before-fix.log. First77 labels/order retained;10 useful defaults compile source/copy/XIP.

Exact snippet label exception justified by reviewer's independently preserved b4 `for`/`while` diagnosis; normal member/class/keyword/local exact-label exclusions/ranking unchanged. Minimal shared accept guard explicitly requested by reviewer: integer0<=ctx.start<=ctx.end<=viewlen and collapsed caret exactly ctx.end. Stale rejection only hides popup/cancels unsafe field session, no source/selection/dirty/diagnostic/history replacement. Own baseline trusted-source loss captured in stale-context-baseline-clean.log; candidate tests cover preservation and valid acceptance.

## Tests and commands

Node syntax check4 JS files; git diff --check and staged diff --check passed. `make -j2 mcs` built trusted host; no runtime edits. Sealed logs refer to same final committed source/test blobs; additional committed-unit/source and final benchmarks run after commit.

- `node tests/studio/test_snippets.js ./mcs`:219 assertions (145 grammar/session/catalog + trusted default/filled source/copy/XIP). sealed-unit.log and committed-32cba4b-unit.log.
- `node tests/studio/test_source.js`:108. sealed-test_source.log and committed-32cba4b-source.log.
- Scope69, lexical, highlight, capabilities, project53(actual source/copy/XIP), project-file56, formatter/compiler outputs: sealed-test_*.log.
- Real Chromium snippet209 incl compiler-filled source/copy/XIP, actual keys, stale ordinary/snippet popups, rawEOL, Unicode, cancellation/nativeundo/IME/readOnly/lasttab/bounds: sealed-test_snippets_browser.log.
- Explicit EOL history39: sealed-test_source_eol_history_browser.log.
- Real scoped completion/signatures/hover: sealed-corrected-test_completion_context.log.
- Full project-file browser46: sealed-corrected-test_project_file_browser.log (IMPORTANT earlier unexplained failure below).
- Full WebSerial Studio89: sealed-corrected-test_studio.log.
- Editor layout59, status90, project dialog24, gallery full: sealed-corrected-test_*layout.log.
- `node tests/studio/test_templates.js ./mcs`:217 C# templates/218 total/77 snippets all passed: sealed-test_templates.log.
- Additional retained exact default fixtures/images/source/copy/XIP outputs: default-fixtures-32cba4b/.

All browser/full/port gates use `flock /data/v2-parallel/network-tests.lock`, NODE_PATH=/vercel/sandbox/node_modules, CHROME=$(command -v chromium), OUT separate log paths. Functional invocation has no baseline mode and rejects BENCH_BASELINE; benchmark is separately labeled BENCH_ONLY.

## Failures and unresolved evidence — NOT a clean all-runs-pass claim

1. compiler-initial.log: test initially used unsupported --compile CLI; corrected to existing -c, tests pass. Original failed log retained.
2. shared-guard-browser.log: interruption replay duplicated identical test block causing duplicate const SyntaxError. Preserved failed log and pre-fix script test_snippets_browser-duplicate-recovered.js; removed only duplicate block; shared-guard-recovered.log207 passed.
3. last-tab-before-fix.log: genuine retained-session lifecycle bug reproduced, fixed by explicit App.activate cancel; final209 includes regression.
4. sealed-test_completion_contexts.log: my invocation typo used nonexistent plural filename; correct untouched test completion_context passes in sealed-corrected log. No suppression.
5. **UNEXPLAINED:** frozen-final-test_project_file_browser.log had one failure `actual replace-all retains CRLF/CR/LF across multiple changed lines`. Original failure did not capture actual raw bytes, selection or focus, so cannot retroactively supply those. No assertion/test weakening and no claimed flake classification. Unchanged retry project-unchanged-retry.log passed46; later sealed-corrected full46 passed. These do not establish cause.
   - focused-replace-probe.js/log:80 baseline and80 candidate exact unsynchronized open/CtrlH/fill/click trials; another80+80 with rAF boundary.320 trials zero failures, same lock/env. Event/raw/selection/focus diagnostic capture for any failure enabled. No false claim of original cause.
   - full-project-diagnostic-runner.js and full-project-diag-{1,0}-{1,2}.log:4 full original harness runs,2 baseline2 candidate. Baseline injects exact b4 studio.js AND snippets.js. Same original assertions preserved, only read-only event/snapshot diagnostic added; no sleeps/retries inside assertions. All4 exits0; RAW_REPLACE_DIAGNOSTIC records actual raw bytes/codepoints, selection/focus/tab/find/events for each. full-project-diag-summary.log retained.
   - Approval must assess this unresolved evidence; do not hide it behind successful repeats or universal compatibility claims.

## Exact-commit raw timing evidence (no invented speedup/budget)

latency-final-32cba4b/{baseline,candidate}/latency-{baseline,candidate}.json and literal fixture.cs copies, plus latency-final-*-32cba4b.log. Candidate SHA exact32cba4b; baseline both modules exactb4. Same fixed64037 UTF16/2005-line fixture (mixedEOL literal, Unicode), insertion/output hashes and final selection match. Chromium153.0.8010.12, Playwright1.63.0, Node24.14.1. Five cold first-acceptance fresh pages and20 warm accepts each; candidate separately40 actual Tab/ShiftTab handlers and40 active-field input handlers. New field navigation has NO baseline comparison.

Host shared, no affinity/idle guarantee. Metadata actual host load/process names before/after; no make/compiler names observed during these samples, not a guarantee of isolation. Synchronous handler timing only, queued paint/serial responsiveness and files larger than this fixture unmeasured. Raw samples/provenance supplied without speedup or arbitrary latency target claim. Earlier preliminary latency/ remains historical, final provenance is latency-final-32cba4b only.

## Recovery and authority

Immutable snapshot path: /data/backups/v2-parallel-studio/32cba4bd496f971d9885d6c5b16eb1bcee7fdd20/
Includes complete history bundle, one-commit format/binary patches, cumulative base binary patch, exact source copies, raw logs/failures/fixtures, checksums and apply/bundle verification. Parent must preserve remote WIP exact tree and fetch/tree-verify; this lane does not push itself.

Independent approval pending. Slice is NOT full Phase5/fullStudio/fullv2 completion. No protected main/preview integration, merge/tag/release/publication or remote CI execution performed. Old06cadc4/19533874 histories and backups untouched.
