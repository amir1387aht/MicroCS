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
`NODE_PATH=/data/node_modules CHROME=/usr/bin/chromium node tests/studio/test_completion_context.js`.
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
