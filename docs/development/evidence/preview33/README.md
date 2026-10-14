# Scoped v2 preview preparation — checkpoint33 (no publication)

User narrowed immediate target to publishable partial v2. Full stable/roadmap
checklist retained separately; preview gates allow explicit deferred features,
not failed checks or undisclosed security claims. Runtime/library package label
2.0.0-preview.1, CMake numeric core2.0.0. Bytecode formats1–4/API functions unchanged;
no algorithm changes here. Default host compiler/caps confirm TFT=0/LVGL=0.

Strict clean committed tracked-source packaging with matching versions, required
local Studio modules, Linux64 matching host ELF/version/caps, no outputoverwrite,
staged archives+manifest+SHA256SUMS and commit/tree/epoch/environment provenance.
Unit8 tests cover policy stable/main vs preview, clean/matching metadata, offline
module inclusion/checksums/tracked-only sentinels/repeatability/invalidhost/output
rollback. Unit source fixtures are NOT physical board or actual host runtime proof.
Separate readonly Preview workflow builds/tests/extracts and uploads candidate
Actions artifacts; cannot tag/publish/replace latest. Stable Release guards
reject non-mainrefs and prerelease versions before touching stable assets.

Final fullmakecheck EXIT0 incl11altconfig EACH85/85 + native/profile/network/port/
examples; CMake configure/build actualCLI85+shell EXIT0 (root has no CTest tests,
not a CTest claim). All final Studio realserial/UI/editor/completion/projects/
portable/geometry/source/formatter/caps/template217C#218total77snippets gates EXIT0,
48wiki pages. New preview version initially failed stale-doc checker and literal
v1 connection/HTTP length fixtures. Failures retained; updater accepts prerelease
labels without disabling stale detection, stable firmware example pinned separately;
connection asserts exact real binaryversion, HTTP echoes keep exact byte assertions
computed from the version (old33/43/35, new42/52/44). No runtime/network defect or
assertion suppression claimed. First CMake wrapper lacked PYTHONPATH; native tool
path used; initial rg diagnostic exited1 and stopped command before build. Retained.

After whole gate EXIT0, generated lowramheader independently bytechecked: only
encodeddebugpath ../../->relative and length60->48 plus generatorversioncomment.
Original trusted legacy1.14 fixture restored, not whole reset or unchecked discard.
Frozen runtime hashes checked throughout. No actual package from canonical remote
SHA/extracted archive smoke is claimed yet: requires this clean pushed commit,
then fresh canonical worktree. That next validation/evidence will be preserved.

Preview includes Linuxhost/Studio/source only; no readyflashfirmware/hardware
validation, sandbox/signing/authentication/full graphics/roadmap/semantic/undo
promise. Scope/support/safeuse/publication conditions in docs/V2_PREVIEW.md.
Public prerelease/tagging requires exact-headCI+review+separate approval. No merge,
tag, release or irreversible board provisioning performed by this checkpoint.
