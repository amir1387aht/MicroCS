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
