# Runtime stack capacity — v2 WIP

This is defensive resource-limit enforcement for trusted compiler output, **not
an untrusted-image verifier or sandbox**. Public APIs, opcodes, image versions
and configured stack sizes are unchanged. Large valid expressions still work
when the configured stack is adequate.

## Implementation contract

The compiler's `max_slots` describes local slots, not the temporary operand
peak. Frame entry alone therefore cannot establish room for every evaluation.
The interpreter now checks remaining capacity before each growing write,
including fused integer/global/index paths and atomic two-slot expansions.
Checks use integer remaining-slot arithmetic rather than constructing a pointer
past the allocated stack. Failure saves interpreter registers and follows the
existing catchable `StackOverflowException` path.

Helper paths check zero-extra `params` packing, default arguments and statement
field expansion. Exception unwinding skips a handler whose saved stack cannot
hold its exception, without creating an invalid pointer or recursively throwing
another overflow. Native return/constructor paths normally replace previously
occupied receiver/argument slots rather than grow the stack; their existing
balanced-call contract is retained.

C calls reject negative counts, a nonzero count with NULL arguments, and counts
that do not fit, **before reading the argument array or appending a receiver**.
The existing call margin and conservative frame reservation policy remain.
Failed method invocation also restores the saved handler count. A successful
result may alias an input argument; validation does not clear successful inputs.

## Functional validation

`python3 tools/test_stack_capacity.py --sanitize` creates images with the real
compiler, then tests source, copied-image and XIP execution in separate VMs.
XIP storage remains valid until every referencing VM is freed. No handcrafted
or mutated image is executed, and the runner must not be used against an
unpatched runtime to reproduce memory corruption.

Nine trusted-source cases (seven capacity cases plus two method-invocation
cancellation cases) cover ordinary literal/local/global arithmetic and
sequence/string indexing tails, catch-and-continue, captured-local restoration,
and nested native callbacks. Eight stack capacities exercise shortages and
adequate room. Each run checks output or controlled failure, clean stack/frame/
handler/run-depth state, collection, a valid C call afterward and complete
allocator teardown. C API tests separately check invalid argument contracts,
large counts with a short argument array, result aliasing, a native return near
the existing margin boundary, and Stop followed by valid calls.

Nested `mcs_invoke` now re-arms pending cancellation just like nested `mcs_call`.
A native bridge cannot swallow Stop merely by ignoring its C API return value;
script catch blocks must not turn request, step, time or hook cancellation into
normal execution. The new method-callback tests check all four reasons with
optimized/unoptimized trusted images, copied/XIP/source paths, captured-local
survival, clean runtime state and successful calls after cancellation. Fake
clock ticks make time limits deterministic; there is no wall-time timing claim.

The runner records the exact source/header hashes, compiler commands, fixture
hashes and completed configurations. Default, GC stress, compiler OFF, XIP OFF,
superops OFF, source optimization ON, switch dispatch/cache OFF, compact values,
INT64, real float32, float OFF, min, lowram and MCU profiles are explicit variants.
Compiler-OFF profiles consume compiler-generated fixtures; superops-OFF profiles
consume only unoptimized images. Source optimization OFF is the actual default,
not a second mislabeled default build.

This is not an exhaustive opcode-path, OOM, callback-contract or handler-state
proof. Boundary tests do not establish every instruction's exact peak. Remaining
validation includes broader API/native/cancellation/OOM coverage, exact-head CI,
32-bit crosslinks and MCU measurements. Full v2 scope and release gates remain
separate, including independent review and actual hardware.

## Cost and evidence

The checkpoint evidence folder records same-flags host object sizes and checked
benchmark distributions. Per-write checks intentionally add code and may add
runtime overhead; there is no universal speedup claim. Host ELF sections are
not MCU flash, and host wall time is not hardware timing. A future bounded
compiler-derived peak reservation is a separate optimization, not permission
to omit runtime checks or trust arbitrary image metadata.

### Completed host comparison (checkpoint28)

Fresh full `make check` passes on the unchanged guarded production sources,
including all eleven alternate configurations with 85 cases each. The generated
lowram image differs only by its relative debug source path and corresponding
NUL-inclusive string length; all other bytes are equal. Only that generated
file was restored, after the successful gate exit.

[Raw measurements and provenance](evidence/stack-benchmarks28/) contain 1,250
checked samples per version (five workloads × ten phases × 25 repetitions),
recorded after the local compilation/test jobs ended. Baseline is the clean
checkpoint25 tree; fixed source hashes correspond to the guarded runtime in
checkpoint26, unchanged through later documentation/research checkpoints.
Every sample checks output and teardown. Independent raw-record median checks
agree with the [50 matched workload/phase groups](evidence/stack-benchmarks28/comparison.csv).
Requested peak/retained bytes and image sizes match in each group.

The same-flags GCC11.5 `-Os` host VM object has 57,735 bytes of text before and
59,945 after, with zero data/bss. This is a safety/code-size trade-off, not an MCU
measurement. Timing deltas are retained per workload/phase with p10/p90 rather
than pooled into a universal performance claim. Baseline was measured before
fixed in one sequential session, not randomized; shared-host scheduling, thermal
and ordering effects remain limitations. There is no statistically established
causal overhead or broad speedup claim. These results do not replace the full
original host/MCU/peripheral performance and release requirements.
