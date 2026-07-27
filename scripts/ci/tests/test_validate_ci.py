from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


CI_SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CI_SCRIPT_DIRECTORY))

from validate_ci import ValidationError, validate_gitflow_event, validate_junit  # noqa: E402


class GitFlowValidationTests(unittest.TestCase):
    def test_push_and_dispatch_accept_documented_branch_names(self) -> None:
        branches = (
            "main",
            "develop",
            "feature/ci-gitflow-integration",
            "release/1.0.0",
            "hotfix/publication-crash",
        )
        for event_name in ("push", "workflow_dispatch"):
            for branch in branches:
                with self.subTest(event_name=event_name, branch=branch):
                    validate_gitflow_event(event_name, ref_name=branch)

    def test_push_rejects_historical_or_incomplete_branch_names(self) -> None:
        for branch in ("codex/work", "topic/work", "feature/", "release/", "hotfix/", ""):
            with self.subTest(branch=branch):
                with self.assertRaises(ValidationError):
                    validate_gitflow_event("push", ref_name=branch)

    def test_pull_request_accepts_documented_relationships(self) -> None:
        relationships = (
            ("feature/editor-library-modularization", "develop"),
            ("release/1.0.0", "develop"),
            ("hotfix/1.0.1", "develop"),
            ("release/1.0.0", "main"),
            ("hotfix/1.0.1", "main"),
        )
        for head_ref, base_ref in relationships:
            with self.subTest(head_ref=head_ref, base_ref=base_ref):
                validate_gitflow_event("pull_request", head_ref=head_ref, base_ref=base_ref)

    def test_pull_request_rejects_invalid_relationships(self) -> None:
        relationships = (
            ("develop", "main"),
            ("feature/work", "main"),
            ("main", "develop"),
            ("feature/work", "release/1.0.0"),
            ("", "develop"),
        )
        for head_ref, base_ref in relationships:
            with self.subTest(head_ref=head_ref, base_ref=base_ref):
                with self.assertRaises(ValidationError):
                    validate_gitflow_event("pull_request", head_ref=head_ref, base_ref=base_ref)

    def test_unknown_event_is_rejected(self) -> None:
        with self.assertRaises(ValidationError):
            validate_gitflow_event("schedule", ref_name="develop")


class JUnitValidationTests(unittest.TestCase):
    def _write(self, contents: str) -> Path:
        temporary = tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".xml", delete=False)
        self.addCleanup(Path(temporary.name).unlink, missing_ok=True)
        with temporary:
            temporary.write(contents)
        return Path(temporary.name)

    def test_accepts_nested_passing_suites(self) -> None:
        path = self._write(
            """<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="2" failures="0" errors="0">
  <testsuite name="native" tests="1" failures="0" errors="0">
    <testcase name="native-core"/>
  </testsuite>
  <testsuite name="managed" tests="1" failures="0" errors="0">
    <testcase name="managed-contracts"><system-out>passed</system-out></testcase>
  </testsuite>
</testsuites>
"""
        )
        self.assertEqual(validate_junit(path, minimum_tests=2), 2)

    def test_rejects_missing_malformed_or_unknown_xml(self) -> None:
        missing = Path(tempfile.gettempdir()) / "dpe-ci-evidence-does-not-exist.xml"
        malformed = self._write("<testsuite>")
        unknown = self._write("<report><testcase name='unexpected'/></report>")
        for path in (missing, malformed, unknown):
            with self.subTest(path=path):
                with self.assertRaises(ValidationError):
                    validate_junit(path)

    def test_rejects_empty_or_below_minimum_evidence(self) -> None:
        empty = self._write('<testsuite tests="0" failures="0" errors="0"/>')
        one_test = self._write(
            '<testsuite tests="1" failures="0" errors="0"><testcase name="only"/></testsuite>'
        )
        with self.assertRaises(ValidationError):
            validate_junit(empty)
        with self.assertRaises(ValidationError):
            validate_junit(one_test, minimum_tests=2)

    def test_rejects_failures_and_errors(self) -> None:
        documents = (
            '<testsuite tests="1" failures="1" errors="0"><testcase name="failed"><failure/></testcase></testsuite>',
            '<testsuite tests="1" failures="0" errors="1"><testcase name="errored"><error/></testcase></testsuite>',
            '<testsuite tests="1" failures="0" errors="0"><testcase name="inconsistent"><failure/></testcase></testsuite>',
        )
        for document in documents:
            with self.subTest(document=document):
                with self.assertRaises(ValidationError):
                    validate_junit(self._write(document))

    def test_rejects_invalid_counts_and_minimum(self) -> None:
        invalid = self._write('<testsuite tests="1" failures="many"><testcase name="test"/></testsuite>')
        negative = self._write('<testsuite tests="1" errors="-1"><testcase name="test"/></testsuite>')
        for path in (invalid, negative):
            with self.subTest(path=path):
                with self.assertRaises(ValidationError):
                    validate_junit(path)
        with self.assertRaises(ValidationError):
            validate_junit(self._write('<testsuite><testcase name="test"/></testsuite>'), minimum_tests=0)


if __name__ == "__main__":
    unittest.main()
