# Bounded first-use registration recovery

This regression covers trusted ordinary C#/host registrations with an allocator
restored after a deliberately selected allocation failure. It is not a hostile
input test or a guarantee of generic OOM transactions, built-in initialization,
image safety, authentication, or target-device reliability.

## Runtime changes and invariants

- A queued registration stays linked at its original position while it is
  applied. Its `applying` marker hides only that active entry from recursive
  resolution. On unwind the marker is cleared and the original panic target and
  error code are restored. No detached node is lost or reinserted out of order.
  Successful resolution rescans ownership after nested calls before freeing.
- Native host classes are globally published only after inheritance and ROM/
  eager member registration finish. A failure while building this class leaves
  its original queued registration retryable. A failure allocating an instance
  after publication is a distinct, tested case: the complete class stays usable.
- A partial module may already expose an earlier constant. An internal pending
  flag forces remaining registrations to complete before returning even that
  cached member, so later constant precedence is not bypassed on retry.
- The pending flag uses a spare bit in the existing `layout_shared` byte rather
  than a new per-class field. All readers/writers were audited: new-class sets
  zero; `own_layout` tests/clears only SHARED_LAYOUT; shared inheritance sets only
  SHARED_LAYOUT; class destruction tests only SHARED_LAYOUT; queued module apply
  sets PENDING_REGS; member lookup tests PENDING_REGS; completed resolution clears
  PENDING_REGS only when no matching queue entries remain, including active ones.
  The original shared-layout meaning is preserved when both bits are set.
- A module flag is set before member/constant mutation. Failed application keeps
  it set; a nested resolution must not clear it while an outer node is active.
  Failure before obtaining a module has no partial member publication to guard.
  Ordinary classes with no pending flag keep their cached path without scanning
  the pending queue. The test includes legitimate repeated misses and repeated
  cached property reads; this is not a performance benchmark.
- External lookup's private error guard restores its exact entry `gc_pause` and
  root count, including positive pause and pre-existing roots. Nested lookup
  under an existing panic target still propagates the original error. Locals
  consumed after `longjmp` are initialized before `setjmp` and not modified;
  the private guard's result is volatile.

## Regression boundaries

`test_lazy_recovery.c` independently tracks physical live pointers and bytes,
ignoring allocator old-size hints. It exercises native class metadata vs instance
failure, same-VM reuse, external and nested lookup guards, existing userdata,
GC and normal member behavior. Physical allocation counts are checked at VM
teardown, with no test cleanup masking leaks.

`test_lazy_module_recovery.c` and its paired notes are copied byte-for-byte from
frozen tests-only commit 35e758f733e5de0219fef7d668538cedbac89058.
They retain their sustained-denial semantics, original baseline failures,
physical-pointer-before-cleanup checks, registration-order/constant/overload
checks and documented eager-mode coverage gaps. Passing check totals can differ
from baseline because the test follows different successful/error branches.

## Preserved development failures

Original first-use class recovery failed with MissingMember after allocator
restoration and an actual outstanding allocation. Early fixes did not finish
partially published module constants before cached reads; unchanged independent
module tests caught this. All original failure logs remain outside this file in
the lane's immutable handoff/backup evidence.

An owner regression initially assumed its third allocation was class metadata.
Source inspection and the failure showed it was instance allocation after a
complete class publication. That failure was retained. The third-allocation
case remains, now requiring a complete published class and no constructor call;
a separate second-allocation case requires unpublished class metadata and a
successful same-VM retry. No independent regression was weakened to make the
implementation pass.

Independent review and combined real LVGL 8/9 group regression approval are
separate gates. These changes do not complete an original v2 phase.
