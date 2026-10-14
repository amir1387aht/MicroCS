# MicroCS 2.0.0-preview.1 — scoped preview candidate

This is a **development preview**, not a finished v2, a stable release or a
production-security claim. The preview may ship before every roadmap feature is
complete. Its package and runtime identify themselves as `2.0.0-preview.1`;
the compatible bytecode image formats remain versions 1–4.

## What the first preview includes

- Linux x86-64 compiler, REPL and simulated board using the default host build.
- Offline MicroCS Studio: capability-aware templates; lexical/scoped assistance;
  explicit multi-file source projects and source navigation; portable plaintext
  `.microcsproj` save/reopen; tested canonical source preservation; adaptive editor
  panels and keyboard/pointer resizing.
- Complete tracked source, build definitions, tests, existing examples and
  documentation in a separate source archive.
- Defensive runtime/loader metadata and nested cancellation fixes already on the
  v2 branch. These fixes do not establish a complete untrusted-bytecode verifier.

## Deliberately not a supported preview promise

No ready-to-flash firmware is included in this first package. Cross-build results
are not physical hardware testing. Existing ports remain available in source,
but exact-board validation is separate. Optional LVGL/TFT are experimental,
**OFF by default**, need their documented external dependencies, and are not
bundled as complete production graphics support. BLE/network/port-specific
capabilities are determined by the actual board/configuration, not by this label.

Signed/authenticated installation, production board trust chains, full bytecode
verification, full graphics/assets/DMA/async/platform coverage and the unfinished
roadmap are deferred. Studio is not a .NET SDK/full semantic compiler; imports,
assets, automatic project saving, full accessibility/performance and arbitrarily
old mixed-EOL/EOL-only editor undo remain incomplete. Those omissions are not
represented as implemented or validated.

## Safe use and limitations

Use trusted source and images produced by the compatible MicroCS compiler.
Do not execute handcrafted or untrusted `.mcsb` images. Scripts share the host
address space: **this is not a sandbox**. The shell has no authentication; do not
expose it on an untrusted connection. Apply step/time/heap limits as defense in
depth, not an isolation guarantee. Project snapshots contain plaintext source,
including unsaved text; keep secrets out of shared snapshots. SHA-256 checksums
identify bytes but do not authenticate their publisher or replace signing.

Workflow Linux builds use Ubuntu 24.04; local candidates record their packaging
environment in the manifest. This is not a minimum-libc or reproducible-build
attestation. Other
Linux distributions/glibc versions, other CPU architectures, Windows and macOS
are not binary-compatibility promises. Build from source for other environments.
Studio editing is browser-based; board Web Serial requires a supported desktop
Chrome/Edge browser. Responsive layout does not give phones unsupported serial.

## Use the candidate

1. Verify `SHA256SUMS` against the supplied archives and `manifest.json`.
2. Extract `*-linux-x64.tar.gz`; run `./mcs --version`, then
   `./mcs --repl --sim` in its directory. The simulator is not a physical board.
3. Extract `*-studio.zip`; open `index.html` in its directory. All required
   JavaScript/CSS/template files are local. Board connection is optional for
   editing and project-file import/export.
4. Extract `*-source.tar.gz` to build/test or inspect the implementation.

For a read-only check from a trusted source checkout that includes the verifier,
see [offline bundle verification](PREVIEW_VERIFICATION.md). The tool accepts
independently obtained expected commit/version identifiers; it does not extract
archives or execute their contents. A successful check is not signing, proof of
source provenance, safe-content certification or release approval.

Do not replace production firmware or stable installations based solely on this
preview label. Keep the stable version available for rollback; Studio source
projects are independent plaintext files, not firmware provisioning.

## Artifact and publication policy

For changes after a tested candidate, follow the
[integration and revalidation checklist](PREVIEW_INTEGRATION.md).

`tools/package_preview.py` requires a clean committed checkout, a preview label
matching the committed runtime version, all required offline Studio modules and,
when included, a matching-version Linux x86-64 host executable. Archives record
commit/tree/epoch, a manifest and checksums. Existing output packages are never
silently overwritten; only tracked source is included. Package assembly does not
publish, tag, upload or change GitHub's latest release.

The **v2 Preview artifacts (no publication)** workflow has read-only repository
permissions. It builds/tests and uploads temporary Actions candidate artifacts.
The normal **Release** workflow rejects feature refs and preview version strings;
it must not refresh stable v1 assets with v2 feature-branch code. Neither workflow
turns missing hardware/security/feature evidence into a stable-v2 claim.

Before public preview publication: exact-head required CI must be green, candidate
archives/checksums and extracted host/Studio must pass smoke tests, the notes and
support/deferred-feature matrix must be reviewed, and publication must be explicitly
approved. Use a **GitHub prerelease**, not latest/stable, targeting the exact tested
commit and a unique `v2.0.0-preview.N` tag. Do not clobber v1 tags/assets or provision
boards. No tag or public release has been created merely by preparing this preview.

Authoritative implementation/support details are in the source distribution:
`docs/SECURITY.md`, `docs/LVGL.md`, `docs/TFT_ESPI.md`, `tools/studio/README.md` and
`docs/development/v2-checklist.md`. The original full-v2 checklist remains the
stable/full-scope goal, not a requirement to implement every feature before a
truthfully scoped development preview.
