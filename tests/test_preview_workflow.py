#!/usr/bin/env python3
"""Stdlib regression contracts for preview.yml; not a generic YAML validator."""
import fnmatch
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/preview.yml"


def workflow_steps(text):
    # Recognize the workflow's existing literal step/run-block layout. Fail
    # explicitly if it changes; YAML syntax/schema validation is separate.
    starts = list(re.finditer(r"^      - (?:name|uses): .+$", text, re.M))
    return [text[item.start():starts[index + 1].start() if index + 1 < len(starts) else len(text)]
            for index, item in enumerate(starts)]


def run_block(step):
    marker = "        run: |\n"
    if marker not in step:
        raise AssertionError("expected a literal multiline run block")
    lines = step.split(marker, 1)[1].splitlines()
    if not all(not line or line.startswith("          ") for line in lines):
        raise AssertionError("unexpected run block layout")
    return textwrap.dedent("\n".join(lines)).strip() + "\n"


class PreviewWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding="utf-8")
        cls.steps = workflow_steps(cls.text)

    def step(self, label):
        matches = [step for step in self.steps if label in step.splitlines()[0]]
        self.assertEqual(len(matches), 1, f"expected exactly one {label} step")
        return matches[0]

    def test_read_only_exact_checkout(self):
        permissions = self.text.split("permissions:\n", 1)[1].split("\n\n", 1)[0]
        self.assertEqual(permissions.strip(), "contents: read")
        checkout = self.step("actions/checkout@v4")
        self.assertIn("persist-credentials: false", checkout)
        self.assertIn("ref: ${{ github.event.pull_request.head.sha || github.sha }}", checkout)
        self.assertIn("fetch-depth: 1", checkout)
        self.assertIn("  workflow_dispatch:", self.text)
        self.assertIn("  pull_request:", self.text)
        self.assertNotIn("pull_request_target:", self.text)
        self.assertNotIn("continue-on-error:", self.text)
        self.assertNotIn("if: always()", self.text)

    def test_deliberate_path_coverage(self):
        paths = re.findall(r"^      - ['\"]?([^'\"\n]+)['\"]?$", self.text, re.M)
        self.assertEqual(len(paths), len(set(paths)))
        required = ["tools/verify_preview.py", "tests/test_verify_preview.py", "tests/test_preview_workflow.py",
                    "tools/package_preview.py", "tools/release_policy.py", "tests/test_preview_package.py",
                    "docs/PREVIEW_VERIFICATION.md", "docs/V2_PREVIEW.md", "docs/SECURITY.md",
                    "tools/studio/source.js", "tools/studio/templates/example/template.cs",
                    "tests/studio/test_source.js", "tests/studio/test_preview_package_browser.js",
                    "include/mcs.h", "include/mcs_capabilities.def", "src/mcs_runtime.c",
                    "modules/runtime/example.c", "ports/unix/main.c", "config/mcs_user_config.h",
                    "Makefile", "CMakeLists.txt", "library.json", "LICENSE", "CHANGELOG.md", "SECURITY.md",
                    "tests/run_tests.sh", "tests/t01_basics.cs", "tests/t01_basics.out",
                    "tests/err_syntax.cs", "tests/err_syntax.out",
                    "tests/test_shell.py", "tests/test_readline_echo.py",
                    "tools/wiki_sync.py", ".github/workflows/preview.yml", ".github/workflows/release.yml"]
        for path in required:
            with self.subTest(path=path):
                self.assertTrue(any(fnmatch.fnmatchcase(path, pattern) for pattern in paths), path)
        self.assertNotIn("**", paths, "avoid an accidental repository-wide trigger")

    def test_regression_gates_before_assembly(self):
        gate = self.step("Preview policy,")
        run = run_block(gate)
        for path in ("test_preview_package.py", "test_verify_preview.py", "test_preview_workflow.py"):
            self.assertIn(f"python3 tests/{path}", run)
        self.assertLess(self.steps.index(gate), self.steps.index(self.step("Assemble and verify")))
        self.assertIn("make -j2 mcs", run_block(self.step("Build default host")))
        studio = run_block(self.step("Studio source/project"))
        self.assertIn("node tests/studio/test_source.js", studio)
        self.assertIn("python3 tools/studio/gen_templates.py --check", studio)

    def test_single_smoke_after_verification_before_upload(self):
        assembly = self.step("Assemble and verify")
        smoke = self.step("Extracted candidate host smoke and offline Studio")
        upload = self.step("actions/upload-artifact@v4")
        self.assertLess(self.steps.index(assembly), self.steps.index(smoke))
        self.assertLess(self.steps.index(smoke), self.steps.index(upload))
        run = run_block(smoke)
        self.assertEqual(run.count("python3 tests/test_preview_package.py --bundle"), 1)
        self.assertEqual(run.count("node tests/studio/test_preview_package_browser.js"), 1)
        self.assertEqual(self.text.count("node tests/studio/test_preview_package_browser.js"), 1)
        self.assertIn("if-no-files-found: error", upload)
        self.assertIn("path: dist/preview/MicroCS-*/*", upload)
        for forbidden in ("gh release", "gh api", "git push", "git tag", "releases/latest", "contents: write"):
            self.assertNotIn(forbidden, self.text)

    def run_assembly(self, package_status=0, verify_status=0, checksum_status=0):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "include").mkdir()
            (root / "include/mcs.h").write_text('#define MCS_VERSION_STRING "2.0.0-preview.1"\n')
            bundle = root / "own bundle directory"
            bundle.mkdir()
            commands = root / "commands"
            commands.mkdir()
            log = root / "events.jsonl"
            shim = f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
with open(os.environ["EVENT_LOG"], "a") as out:
    out.write(json.dumps([name, *sys.argv[1:]]) + "\\n")
if name == "git":
    print("a" * 40)
    sys.exit(0)
if name == "sha256sum":
    sys.exit(int(os.environ["CHECKSUM_STATUS"]))
if sys.argv[1] == "tools/package_preview.py":
    status = int(os.environ["PACKAGE_STATUS"])
    if status == 0:
        print(os.environ["BUNDLE"])
    sys.exit(status)
if sys.argv[1] == "tools/verify_preview.py":
    sys.exit(int(os.environ["VERIFY_STATUS"]))
sys.exit(99)
'''
            for command in ("git", "python3", "sha256sum"):
                path = commands / command
                path.write_text(shim)
                path.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"], EVENT_LOG=str(log),
                       BUNDLE=str(bundle), PACKAGE_STATUS=str(package_status), VERIFY_STATUS=str(verify_status),
                       CHECKSUM_STATUS=str(checksum_status))
            result = subprocess.run(["bash", "-c", run_block(self.step("Assemble and verify"))],
                                    cwd=root, env=env, capture_output=True, text=True, timeout=10)
            events = [json.loads(line) for line in log.read_text().splitlines()]
            return result, events, str(bundle)

    def test_assembly_pins_actual_head_runtime_and_bundle(self):
        result, events, bundle = self.run_assembly()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events, [
            ["git", "rev-parse", "HEAD"],
            ["python3", "tools/package_preview.py", "--version", "2.0.0-preview.1", "--host-binary", "./mcs", "--out", "dist/preview"],
            ["python3", "tools/verify_preview.py", bundle, "--expected-commit", "a" * 40, "--expected-version", "2.0.0-preview.1"],
            ["sha256sum", "--check", "SHA256SUMS"],
        ])

    def test_package_failure_stops_verification(self):
        result, events, _ = self.run_assembly(package_status=13)
        self.assertEqual(result.returncode, 13)
        self.assertEqual([event[0] for event in events], ["git", "python3"])

    def test_verification_failure_stops_checksum_and_smoke(self):
        result, events, _ = self.run_assembly(verify_status=17)
        self.assertEqual(result.returncode, 17)
        self.assertEqual([event[0] for event in events], ["git", "python3", "python3"])

    def test_checksum_failure_propagates(self):
        result, events, _ = self.run_assembly(checksum_status=19)
        self.assertEqual(result.returncode, 19)
        self.assertEqual(events[-1], ["sha256sum", "--check", "SHA256SUMS"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
