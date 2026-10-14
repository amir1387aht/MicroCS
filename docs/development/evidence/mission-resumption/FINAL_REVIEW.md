# Round-two independent review — APPROVE local disposable candidate only

## Exact candidate and provenance
- Final tested candidate: b4f670a2a778b92322bf868cba300f9b767df893.
- Tree: cd227a96f8a8852e8eefe9ba3df8f2bb4ecbfb68.
- Reviewer branch/check-out: work/v2-parallel-review, /data/v2-parallel/review; clean after testing.
- First-round immutable candidate: f338f64f93e561888ce3edd41e99578033dd3d55. Its report/logs/package evidence were not overwritten; its exact backup is /data/backups/v2-parallel-review/f338f64f93e561888ce3edd41e99578033dd3d55 (checksum inventory and fresh bundle-clone restore passed).
- Added reviewed owner commits: docs 25183a5476e73b0cfe9c1699f35bd69d5a8d4f1e; Studio CI 19533874c1428f988f6b916334fb9df1b1156881; preview CI 1cccd209c02873251c09792eebc634a3808a3070 plus correction 85bdad8adc8c6a5d02b009f4e7208c6e877db8f7.
- Local cherry-pick IDs: 737768d (docs), 1b008b9 (Studio CI), b68c0a2 (preview CI), b4f670a (correction). No owner implementation fixes were made by reviewer; owner corrected the finding in its lane.
- Only four combined files differ from first round: ci.yml, preview.yml, tests/test_preview_workflow.py, docs/V2_PREVIEW.md. Independently compared each current blob ID to exact approved owner head: all identical. Relative documentation links and both composed YAML documents validate (combined-static-provenance.log).

## Finding, correction and independent follow-up checks
Initial preview CI 1cccd209 was REQUEST CHANGES: root host .cs/.out fixtures consumed by retained tests/run_tests.sh were omitted from pull_request.paths, so ordinary/error regression changes could skip preview validation. Existing eight owner tests passed but missed that input class (preview-initial-decision.log and preview-initial-reproduced-tests.log).

Owner 85bdad8 added tests/*.cs and tests/*.out plus positive ordinary/error source/output assertions. Independent parsed YAML/path checks now cover those exact counterexamples and representative host/Studio/verifier/docs inputs. Retained exact checkout, read-only permissions, no persisted credentials, host/Studio gates, one smoke and upload remain unchanged; duplicate smoke removed. Quoted packager-returned bundle is checked against git HEAD and runtime version before checksums/smoke/upload. Independent shims under GitHub-equivalent Bash demonstrate exact argv with spaces and failure13/17/19 propagation for package/verifier/checksum respectively (preview-corrected-independent.log). No success-mask or release action introduced.

Studio CI independent parsed workflow minus exactly new named EOL step equals original parent workflow; no other gates/triggers/setup change. Exact extracted command propagates intentional Node23 failure and real browser39/source108 pass (studio-workflow-independent.log, studio-real-extracted-step.log, studio-source.log). The exact composed step also passes (combined-exact-studio-ci-step.log). Expected nonzero negative controls are intentional evidence, not unreported failed suites.

## Actual final-candidate testing
All 28 command groups returned0 on b4f670a; commands in run-combined.sh, return codes in combined-results.tsv, raw outputs in combined-*.log. Full matrix under shared network-tests.lock:
- make -j2 mcs (incremental build; runtime sources unchanged from tested first round); trusted host85 passed,0 failed.
- Template metadata and generator check; verifier14, packager8, workflow8 tests.
- Shell, readline echo and wiki checks.
- Source108; lexical, scope, highlight, capabilities, project, project-file, compiler/runtime formatter.
- Own EOL key/byte/selection/dirty/diagnostic/no-op/redo/native/text/tab/budget regression; owner actual Chromium39; completion-context; editor/gallery/project/status layout.
- Project browser, portable-project browser and full serial Studio all pass.

Executed the exact committed preview assembly run block locally under GitHub-equivalent Bash: fresh three-archive host/Studio/source package verifies with expected b4f670a HEAD/runtime and retained sha256sum (actual-workflow-assembly.log). Actual extracted trusted host version/Unicode execution and source header/offline module smoke pass (actual-extracted-host.log). Actual extracted offline Studio project import/download, exact CRLF, layout, no network/page errors pass (actual-extracted-browser.log). Own full EOL key regression also passes actual distributed assets (actual-extracted-review-eol.log); ignored runner changes only HTML input path, never assertions.

Bundle: /data/v2-parallel/review/dist/preview/MicroCS-2.0.0-preview.1-gb4f670a2a778. Identical package copy retained under round2/packages for backup. Local browser dependency uses existing NODE_PATH modules; no npm install/download, artifact upload or remote GitHub Actions run performed. Remote checkout/install/service behavior and complete remote job matrix are NOT attested.

## Backup and boundaries
Final immutable backup destination: /data/backups/v2-parallel-review/b4f670a2a778b92322bf868cba300f9b767df893. Contains reviewer history bundle, exact tracked tree, cumulative binary patch from canonical d6cc1e8, raw logs/handoffs/packages snapshot, provenance and SHA256SUMS. Consult that directory's bundle-verification.log/fresh-restore.log for local restore evidence. Backups never overwrite previous snapshot directories.

Inherited limits remain explicit: raw undo is separate Ctrl/Cmd+Alt+Z (+Shift redo) for bounded contiguous same-display changes, NOT universal native/arbitrarily-old/interleaved/cross-tab mixed-EOL repair. Mac/OS-global shortcut availability, other browsers and broad accessibility/performance not proven. Verifier is offline byte/metadata consistency only; no publisher authentication, safe-archive/content-to-Git-tree/capability proof. Separate smoke harness executes only our own freshly assembled trusted package, never hostile bytecode/exploit tests.

No unresolved review blocker in stated local scope. NO protected-branch merge, push, tag, upload, release or provisioning is authorized; explicit user approval plus exact-head required remote CI remain necessary. Protected refs were read, not edited; observations in protected-refs-observed.log are not claimed to prove an unavailable initial snapshot comparison. Global NOTES not touched. Any subsequent changes require new exact-head review/tests/backups.
