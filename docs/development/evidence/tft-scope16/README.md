# Foreground TFT scope cleanup — checkpoint16

Actual native generic upstream RAM+VM tests pass131509 checks in EACH normal
and GC-stress ASan/UBSan/leak variant, including previous exhaustive colors/OOM
and new24 repeated normal/error/Stop scopes, nested/preexisting/foreign VM cases,
failed native-object constructor cleanup BEFORE GC, actual repeated source/image
file shell runs, stale wrapper rejection/reuse and persistent machine exec.
Scope owner generations never wrap; children are linked before panicking
allocations and released before native parent. LVGL must close first; live GUI
refusal is included in the real Arduino firmware fixture, cross-built ONLY.
No physical GUI/backend ordering execution is claimed by the generic RAM tests.

All five real ESP32 Arduino standalone/LVGL8/LVGL9/touch/OFF configurations
compile/link; actual artifact/config/source/pin hashes in cross-matrix.json.
Host85 source/image/XIP suite, capabilities/package/config/docs/template and
155924 allocation-arithmetic assertions pass; OFF C++ has no native symbols
or third-party dependencies, unsupported non-Arduino enable fails explicitly.
Full host suite on earlier checkpoint14 already passed, not relabeled as an
exact-head full-suite result after this new scope increment. Exact-head CI must
complete independently. Checkpoint14 has92/92 remoteCI success; checkpoint15
was70success/19running/3queued at the latest check, not called green yet.

REPL/machine exec/direct embedding retain deliberate persistent ownership;
foreground file cleanup does not destroy preexisting owners. Scheduled callbacks
must not assume ownership beyond file scope. Not full TFT/LVGL/security/v2 scope
or hardware evidence; no merge/tag/release or irreversible device operation.
