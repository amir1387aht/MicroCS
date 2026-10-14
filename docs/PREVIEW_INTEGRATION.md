# Integrating changes after a preview candidate

Use this checklist for changes proposed after `2.0.0-preview.1`. A previously
verified candidate remains an immutable reference, not proof for a later commit.
This procedure prepares a candidate; it does not authorize publication.

## Separate implementation from integration

- Start each task from a recorded commit, with a dedicated branch and worktree.
- Assign ownership before editing. Coordinate shared interfaces through messages;
  do not edit another task's working tree or cherry-pick unfinished work.
- Keep unrelated contributions, generated artifacts and release changes out of a
  task unless they are necessary and reviewed.
- Each owner supplies an exact commit, changed-file list, commands, raw test
  results, known limitations and any initial failures. A clean diff is not a test.
- An independent reviewer examines the diff and reproduces representative tests.
  Required fixes go back to the owner, rather than being silently changed during
  review.

## Assemble a disposable integration candidate

1. Record the protected branch heads and candidate commit/tree before starting.
   Leave the published or previously tested candidate and its artifacts intact.
2. Create a separate integration branch/worktree. Integrate only exact, reviewed
   task commits there; do not merge to `main` as part of testing.
3. Resolve shared source changes deliberately. Regenerate derived files from the
   combined sources instead of choosing an older generated bundle wholesale. For
   Studio templates, run `python3 tools/studio/gen_templates.py`, then its
   `--check` mode. Preserve templates already present on the integration branch.
4. Run task-specific tests and the relevant existing regression suites on the
   combined tree. Run port-binding tests sequentially, or in genuinely separate
   network environments. Different worktrees alone do not isolate network ports.
5. Review failures without suppressing assertions. Record the cause, correction
   and successful rerun; do not describe an earlier commit's tests as new evidence.
6. Commit the combined result and record its exact commit/tree. Any subsequent
   change invalidates claims that the preceding test run covered that change.

## Revalidate packages and publication scope

- Confirm version labels, supported features, deferred work and release notes
  match the actual combined implementation. Do not infer hardware validation from
  simulator tests or cross-compilation.
- Require all applicable remote CI on the exact proposed head. Read the complete
  check list; one successful job is not aggregate CI success.
- Package from a clean, committed checkout using `tools/package_preview.py` and
  the matching host binary. Use a new output directory/identity; do not overwrite
  previous candidate archives.
- Verify all supplied checksums and manifest provenance. Checksums establish byte
  consistency, not publisher authentication or safe archive contents.
- Smoke-test the actual extracted trusted candidate: host version/execution and
  offline Studio module loading, project import/export and source preservation.
  Testing a development checkout alone is not testing the distributed package.
- Review the final support/deferred-feature matrix and independent-review result.
  Keep any remaining blockers explicit, even when CI is green.

## Approval boundary

Local test merges, successful CI and candidate artifacts do **not** authorize a
merge to `main`, a tag, a GitHub Release or device provisioning. Obtain explicit
approval for those actions. A preview release must target the exact approved
commit with a unique preview tag and be marked prerelease, not latest/stable.
Do not replace stable v1 assets or reuse an existing release identity for changed
bytes. Follow the [preview policy](V2_PREVIEW.md) for scope and publication gates.
