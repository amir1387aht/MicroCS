# Security model

MicroCS scripts are trusted *less* than firmware but run in the same address space.
The goal is robustness against buggy or hostile scripts, not isolation against an
attacker with physical/debug access.

| Mechanism | What it guarantees | Status |
|---|---|---|
| Memory safety of the VM | scripts cannot access raw memory: bounds-checked arrays/strings, no pointers, `unsafe` rejected | tested under ASan/UBSan and with the source fuzzer; not formally verified |
| Heap cap (`cfg.heap_limit`) + pool heap | a script cannot exhaust system RAM; OOM ends the run with `MCS_ERR_MEMORY` (no leak since Phase 2) | tested |
| Stack/frame limits | deep recursion → catchable StackOverflowException | tested |
| Time / step budgets (`mcs_set_limits`) | runaway loops are aborted; scripts cannot catch the abort | tested |
| Hook + `mcs_request_abort` | watchdog feeding, Ctrl-C, external kill | tested |
| Module whitelisting | only modules the host opens exist in C#; HAL classes exist only for non-NULL ops; `cfg.stdlib` removes library groups | by construction |
| VFS confinement | normalised paths cannot leave mounted roots; RDONLY and NOEXEC mounts | tested |
| Atomic uploads | interrupted `put` never leaves a half-written script | tested (RAM, LittleFS) |
| Image validation | magic/version/bounds; canonical overflow-checked varints, entry/function/parameter metadata, nonempty bodies and checked debug records; constant, global, local-slot and upvalue operands; closure captures; branch targets on instruction boundaries. **Not** verified: stack balance and operand types | 1.2; fuzzed with `tools/fuzz.py --image` (load + disassemble) |
| Signed images / authenticated shell | — | **planned**: the shell has no authentication; do not expose it on an untrusted link |

Because stack balance is not verified, a hand-crafted image can still corrupt the VM when
it *runs*: treat `.mcsb` files like firmware (build them with `mcs`, ship them over a
trusted channel). Compiler output is balanced, but its temporary operand peak can exceed a
configured stack even when its local slots fit. The v2 runtime checks growing
stack writes and reports a catchable limit error; see
[the validation scope and limitations](development/runtime-stack-capacity.md).
This does not establish complete VM/native memory safety or untrusted-image safety.

Recommendations: give each untrusted script a step/time budget, mount system scripts
read-only, put user uploads on a NOEXEC mount if they should only be data, and disable
the shell (`MCS_ENABLE_SHELL=0`) in production builds unless the link is trusted.

## v2 work-in-progress loader metadata checks

The v2 branch tightens malformed metadata rejection without changing image
versions 1–4 or the public API. See [the precise compatibility and test
contract](development/image-loader-metadata.md). These checks do **not** verify
stack balance, initialized locals, operand types, or every control-flow path.
Handcrafted/untrusted images remain unsupported for execution. The image fuzzer
still loads and disassembles only. Neither a passing metadata fixture nor a
signature establishes a sandbox or an independently audited security protocol.
