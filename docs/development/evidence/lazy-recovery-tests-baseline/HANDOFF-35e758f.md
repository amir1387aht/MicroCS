# Tests-only lazy module recovery handoff — EXPECTED FAIL ON BASELINE

Test commit: 35e758f733e5de0219fef7d668538cedbac89058
Tree: 838897f95202348cea554597b4dea8fbbd2eb8b7
Core remains baseline b4f670a2a778b92322bf868cba300f9b767df893.
Branch/worktree: work/v2-mission-lazy-module-tests / /data/v2-mission/lazy-module-tests
Only tests/c/test_lazy_module_recovery.c and paired .md were committed.
No runtime, HAL, capability, Studio, CI or release files changed. CAN remains frozen.

The source test has positive no-fault controls and explicit unhit sentinel controls;
fault-case failures are intentional regression evidence, not a passing runtime.
Fresh identical VM per fault, finite measured window after native BeginWindow and
before EndWindow (compilation/setup not armed), two ordinary module/constant
registration scenarios, direct source and nested public lookup paths. Injection
is SUSTAINED DENIAL from a selected reached allocation index until C restores it,
NOT a single-shot nth-allocation failure. Restored same-VM retry runs three times
with GC and validates preexisting global/array state, interleaved siblings,
constant precedence/functions, pending node relative order, exact ROM/overload
inventory, repeated use. Physical pointer registry ignores old_size hints.

Passing requires zero outstanding pointers/bytes immediately after mcs_free,
BEFORE diagnostic harness cleanup. Every original error/result and outstanding
address/size is printed before failed assertions; only then are leftovers freed
to permit subsequent independent cases. Sanitizer cleanliness after that cleanup
is NOT recovery proof. The baseline leaves one54-byte pending-node allocation in
many injected cases and loses values/functions or pending materialization.

Scope clarification from reviewer, preserved without amending frozen baseline:
The mixed module case in MCS_LAZY_REGS=0 is NOT swept. Its first module table is
eager and binds the global; following constant tables are eagerly added, so this
exact preparation measures zero first-use allocations. The constants-only case
still measures pending constants and is swept, but it is NOT equivalent mixed
coverage. Alternate const-before-eager-module layering is unmeasured. Eager
classes/both-eager only claim normal semantic controls, no equivalent fault
windows. Source compilation is required. These finite trusted operations do NOT
prove general OOM transaction safety, security, minimum-MCU memory or full-v2.

Evidence:
- baseline-build-01.log: original harness comment/value-union compile errors,
  retained; corrected only test code.
- baseline-02.log: initial expected-failing default without sentinel.
- baseline-matrix-03.log and build/lazy-module-baseline-03: earlier five variants,
  before final sentinel calls. Separate from frozen exact-source evidence.
- baseline-04.log: final-source default expectedFAIL1751/2320; all4nofault and
  all4unhit controls pass,46 reached allocation indices exercised.
- exact-baseline-matrix-05.log / build/lazy-module-exact-baseline-05: exact35e758f
  source, ASan/UBSan+GC/leak-enabled five-config baseline. Actual per-binary exit
  codes/results and commands/hashes are in provenance.json. The collection
  runner exits0 when it collects expected failures; that does NOT mean tests pass.

Exact invocation (all positive flags recorded in per-config provenance):
LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 /data/v2-mission/logs/lazy-module-tests/run_matrix.py \
  --out build/lazy-module-exact-baseline-05 --sanitize

Unchanged tests must be rerun on parent's EXACT core-fix checkpoint in a separate
disposable candidate before approving recovery. This tests-only snapshot itself
is expected-failing, not a core repair or integration/release approval. Parent
owns remote WIP preservation and tree verification. Complete backup metadata
and final actual per-variant outcomes are in FINAL-35e758f.json.
