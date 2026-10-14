# CAN FD implementation handoff (frozen source)

Commit: 865ce88522cb1a76c34053449f27e5ab4f4ff85f
Tree: 7e00a37c563b8771e7505e67b278d880c96afccb
Base: b4f670a2a778b92322bf868cba300f9b767df893
Branch/worktree: work/v2-mission-can-fd / /data/v2-mission/can-fd

Exactly eight owned files: include/mcs_hal.h, include/mcs_capabilities.def,
modules/hal/mcs_hal_lib.c, modules/hal/mcs_hal_sim.c, tests/can_fd/run.py,
tests/can_fd/test_can_fd.c, docs/CAN_FD.md, examples/can_fd/loopback.cs.
Sole capability addition CAN.FD is appended after all old entries. Parent's
TFT.SpriteScroll is intentionally absent here: reviewer must keep both additions
after existing entries when resolving the coordinated EOF conflict.

## Implemented

Separate 72-byte CAN FD frame; classic frame stays 16 bytes/data offset 7 and
existing callback offsets are unchanged. Five optional callbacks appended to
mcs_hal_t: no old-size binary ABI promise. Complete callback set gates Hal.Has;
per-bus queries also require backend support. CANFD introspection/pure frame
class remain available for explicit unsupported errors rather than fake fallback.

Canonical DLC payload lengths 0..8,12,16,20,24,32,48,64. Application lengths 0..64
are copied and zero-padded; original input unmodified. Range/type checks before
narrowing, ID11/29, BRS capability, RX-only ESI, no RTR, native RX metadata
validated without clamping. Native synchronous buffers must be copied.

Actual software loopback bus0 FD FIFO copies all64 bytes independently of
classic FIFO. Complete-set callback gating and queue0 disable supported.
Exclusive VM acquisition, duplicate/busy/failing opens do not acquire/steal;
close/detach/reattach/free handle owned resources exactly once. Native state
finalizer does not invoke script. Explicit Close failure retains owner/callback
for retry. Teardown close failure is documented best-effort. HAL table/context
must outlive attachment; serialize shared simulator operations.

FD event8 preserves existing BLE7. Close removes owned FD bus global/deferred
notifications; perbus generations discard already-held old notifications.
Classic/user/otherbus event order preserved. GC/rooted callback/self-close and
old-state GC after new attachment covered. End-of-source/Stop deliberately do
not auto-close persistent-session FD opens; explicit Close or detach/free needed.

## Test/evidence interpretation

Dedicated source/native/trusted compiler-generated image matrix includes
GC stress, compilerOFF, XIPoff, compact, INT64, min, lowram, min+HAL, queue0 and
HALoff. All host semantic tests explicitly allocate heap256KiB/stack512, NOT a
minimum-MCU RAM claim. SUPEROPS0 uses documented MCS_IMAGE_NO_OPT fixture only.
No malformed/mutated image execution, no physical CAN bus traffic, provisioning,
custom crypto, release publication or protected integration.

Prior sanitizer08 eleven variants passed before final detached-global/checked
userdata guards; default09 current code passed512. Exact frozen SHA validation
is recorded separately in exact-865ce88-* logs and build/can-fd-exact-865ce88
provenance.json. Do not substitute earlier dirty-source passes for exact SHA.

Original failures preserved (not suppressed): initial test API/enum/build
corrections; classic-fixture event isolation; compilerOFF unused helpers; local
sanitizer shared-library lookup fixed with recorded LD_LIBRARY_PATH; FD7 collided
with BLE7 (fixed FD8, dedicated BLE dispatch regression); min+HAL SUPEROPS0 cannot
load optimized image, so documented NoOpt applicability tested instead. Exact
host run passed85 before an incorrect shell path in the orchestration command;
correct Python shell entry point is rerun under distinct remainder log (all gates
pass). Initial example invocation omitted required --sim and correctly had no HAL;
separate example-sim log uses documented --sim and passes. Original logs retained.

Footprint10 same-flag cc -std=gnu99 -Os -DMCS_USER_CONFIG=0 measurements compare
archived b4 base with final production code: default HAL text55329->61635,
lowram55343->61617, sim10800->11493; disabled min HAL2418->2433/sim48 unchanged;
queue0 sim10800 unchanged. Host hal table544->584; sim7408->8000 or7408 queue0.
These are host objects/structures, not physical-target linked flash/RAM claims.

## Final validation and backup

Final exact-head matrix/status and checksummed immutable backup location are
added in FINAL-865ce88.json after all running commands finish. Reviewer approval
is required. Parent owns remote WIP checkpoint/tree verification; this local
commit is not a remote preservation claim. No push/tag/merge/release performed.
