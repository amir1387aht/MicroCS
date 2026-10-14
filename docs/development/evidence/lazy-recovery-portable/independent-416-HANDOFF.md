# Independent unchanged-test validation of exact parent core

Source checkpoint: 416f6ced54912628041ed76c74bebd8b369f7d09
Tree: d1641af035ec9ef8f8ab42f5474364a1867e9a0d
Disposable worktree: /data/v2-mission/lazy-module-validation
Branch: work/v2-mission-lazy-module-validation
No source or test edits/cherry-picks made. Parent owns all core changes.

Both B test files match 35e758f byte-for-byte:
06bb90d9b57fcddaf098b20b734268a065ea5ba3d728f5941aa2c785db921b99  .c
d1095920fa579d5f54b91351ee10dffa07ba5d3455bf2618b8b6b5ec4afde474  .md

Read parent's HANDOFF and tests/c/test_lazy_recovery.md reader/writer, linked
ownership, nested applying-marker, partial publication and pending-bit audit
before validation. This lane reruns the independent B module/const regression;
owner's class/guard tests and full core independent approval are separate gates.

Command (same runner, positive environment and five flags as exact baseline):
LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 /data/v2-mission/logs/lazy-module-tests/run_matrix.py \
  --out build/lazy-module-independent-416f6ce --sanitize

Each individual binary must exit0, all no-fault/unhit controls pass, and no
OUTSTANDING/FAIL record may appear. Physical pointers/bytes must be zero before
manual diagnostic cleanup, not just sanitizer-clean after cleanup. Exact flags,
source/compiler hashes and binary statuses are in per-run provenance.json;
RESULTS.json records independently observed outcomes, not copied parent counts.

Preserved limitations: sustained allocation denial from a reached index until
C restores it, not single-shot failure; module-first mixed eager-regs preparation
has0 first-use allocation sites and is not swept (constants-only is not equivalent
coverage); eager classes/both-eager provide semantic controls only. No general
OOM transaction/builtin/security/physical-device/full-v2 guarantee.

Original35 expected-fail baseline and its immutable backup remain untouched at
/data/backups/v2-mission-lazy-module-tests/35e758f733e5de0219fef7d668538cedbac89058.
Parent confirms remote baseline checkpoint wip/v2-lazy-recovery-tests at
b8d68fdc68cb3b92ff18a80ded94379440b24c40; this lane did not push anything.
CAN865 and all prior snapshots unchanged. No protected merge/tag/release.
