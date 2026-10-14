# Trusted lazy-module recovery regression

This **tests-only** change intentionally fails on core baseline
`b4f670a2a778b92322bf868cba300f9b767df893`. It is not a runtime repair or a
claim that the baseline handles first-use allocation failure correctly.
The parent owns the core fix; rerun these unchanged tests against that exact
checkpoint before approving recovery.

Build from the repository root (a dedicated output name avoids other suites):

```sh
mkdir -p build/lazy-module-tests
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -DMCS_USER_CONFIG=0 \
  -Iinclude -Isrc src/*.c modules/*/*.c \
  tests/c/test_lazy_module_recovery.c -lm -o build/lazy-module-tests/recovery
build/lazy-module-tests/recovery
```

## Contract and evidence

- Two ordinary static registration scenarios: module functions in two ROM
  tables plus two constant groups; and a constants-only module with three
  groups. Interleaved sibling modules must remain intact. Repeated constant
  names and overload signatures verify the no-fault precedence/order.
- Both a source global lookup and a native callback's nested public
  `mcs_get_global` execute inside a source run. No target module is prewarmed.
  A native arm/disarm pair measures only the first-use source window, not
  compilation, setup, retry, diagnostics or teardown. GC configuration can
  change allocations at safepoints inside that window.
- Each reached allocation index uses a **fresh, identically prepared VM**.
  A successful no-fault control determines the finite count; a sentinel at
  that count must remain unhit and succeed. Logs distinguish observed,
  denied and unhit windows. The window is bounded to 128 allocations.
- After an injected OOM, the allocator is restored **in C**, then the same VM
  performs three retries with GC between rounds. Existing integers/arrays,
  nested sibling lookup, values/functions, constant precedence and exact
  overload counts must remain correct. Whitebox checks inventory the pending
  node order and attached ROM tables to detect lost/duplicated work even if
  eventual lookup happens to return the right value.
- The physical-pointer tracker ignores allocator `old_size` hints. VM
  destruction must leave **zero pointers and zero requested bytes before any
  harness cleanup**. Original results/errors and outstanding addresses/sizes
  are printed first; only then does the harness reclaim reported leftovers
  so subsequent independent cases can finish. That diagnostic cleanup is not
  recovery proof, and post-cleanup sanitizer cleanliness cannot substitute
  for the zero-outstanding assertions.

Expected baseline behavior: lazy defaults and GC-stress expose recovery
failures; `MCS_LAZY_REGS=0` still exposes pending constants recovery failures;
`MCS_LAZY_CLASSES=0` and both-eager configurations pass their normal semantic
controls. Eager controls are **not equivalent lazy allocation coverage**.
Every fixed-core applicable case must pass without outstanding allocations.

Useful configurations add `-DMCS_GC_STRESS=1`, `-DMCS_LAZY_CLASSES=0`,
`-DMCS_LAZY_REGS=0`, or both eager flags. ASan/UBSan builds may add
`-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie`.
This is a host test with a recorded 256 KiB heap limit and 512 stack slots,
not a minimum-device-memory claim. It requires source compilation support.

The finite sweep supports only these trusted registration operations' recovery
contracts. It is not general OOM transaction safety, fault fuzzing, malformed
image testing or a security guarantee. No network or physical traffic occurs.
