# Security model

MicroCS scripts are trusted *less* than firmware but run in the same address space.
The goal is robustness against buggy or hostile scripts, not isolation against an
attacker with physical/debug access.

| Mechanism | What it guarantees | Status |
|---|---|---|
| Memory safety of the VM | scripts cannot access raw memory: bounds-checked arrays/strings, no pointers, `unsafe` rejected | Phase 1 design; tested, not formally verified |
| Heap cap (`cfg.heap_limit`) + pool heap | a script cannot exhaust system RAM; OOM ends the run with `MCS_ERR_MEMORY` (no leak since Phase 2) | tested |
| Stack/frame limits | deep recursion → catchable StackOverflowException | tested |
| Time / step budgets (`mcs_set_limits`) | runaway loops are aborted; scripts cannot catch the abort | tested |
| Hook + `mcs_request_abort` | watchdog feeding, Ctrl-C, external kill | tested |
| Module whitelisting | only modules the host opens exist in C#; HAL classes exist only for non-NULL ops; `cfg.stdlib` removes library groups | by construction |
| VFS confinement | normalised paths cannot leave mounted roots; RDONLY and NOEXEC mounts | tested |
| Atomic uploads | interrupted `put` never leaves a half-written script | tested (RAM, LittleFS) |
| Image validation | magic/version/bounds checks in the loader | Phase 1; fuzzing planned |
| Signed images / authenticated shell | — | **planned**: the shell has no authentication; do not expose it on an untrusted link |

Recommendations: give each untrusted script a step/time budget, mount system scripts
read-only, put user uploads on a NOEXEC mount if they should only be data, and disable
the shell (`MCS_ENABLE_SHELL=0`) in production builds unless the link is trusted.
