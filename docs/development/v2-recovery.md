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
