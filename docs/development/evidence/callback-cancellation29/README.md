# Nested method callback cancellation — checkpoint29

`mcs_invoke` re-arms nested request/step/time/hook cancellation exactly as
`mcs_call_internal` already did. A native bridge ignoring its return cannot
make Stop catchable or let script tails resume. No API/opcode/image changes.

The benign trusted-source cancellation regression on checkpoint28 produced
5,208 checks / 48 failed status/output assertions (24 method-callback paths),
without memory corruption or sanitizer errors. This is not the earlier stack
bug reproducer, handcrafted bytecode or a vulnerability reproduction.

Fixed sanitizer matrix: 14 configurations, all zero failures. Nine fixtures
include optimized/unoptimized method callbacks, source/copy/XIP, all four abort
reasons, captured-local/GC preservation, clean stack/frame/handler/run-depth,
valid calls afterward and allocator teardown. Default and most variants have
5,208 checks; compiler-off/lowram/MCU 3,560; superops-off 2,670; min 1,846.
Fresh host source/image/XIP sanitizer suite: 85 passed, zero failed.

Initial full `make check` failed the UDP lifecycle assertion; raw failure is
retained. No production/test assertion was changed or suppressed. Unchanged
source targeted lifecycle retry passed, then the **entire** full gate exited 0,
including all 11 alternate configurations, each 85/85. The failure's underlying
cause has not been established; it must not be relabeled a confirmed fixed bug.
Only the generated lowram header was restored after successful exit; binary
comparison proves the debug pathname/NUL-inclusive length is its only change.

Fresh actual LVGL8.4.0/9.4.0: each 887 native checks + five goldens; actual pinned
TFT_eSPI: 131,557 sprite/VM checks, GC stress/sanitizers, 20 undisposed teardowns
and a physical-IO boundary guard. LVGL8 decoder workaround remains explicit and
build-local, not an upstream edit. Optional graphics remain OFF for release.

Predecessor checkpoint28's 92 exact-head CI jobs completed successfully. This is
not new checkpoint29 CI, physical hardware validation, independent review or
full v2 readiness. Broader API/OOM/verifier/graphics/Studio/core/roadmap/security
work and release gates remain blocking. No merge, tag, release or provisioning.
