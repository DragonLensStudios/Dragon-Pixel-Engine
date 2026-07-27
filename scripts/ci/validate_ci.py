#!/usr/bin/env python3
"""Validate Dragon Pixel Engine GitFlow events and CTest JUnit evidence."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


PERMANENT_BRANCHES = frozenset({"main", "develop"})
TEMPORARY_BRANCH_PREFIXES = ("feature/", "release/", "hotfix/")


class ValidationError(ValueError):
    """Raised when CI input does not satisfy a required contract."""


def _is_temporary_branch(branch: str) -> bool:
    return any(branch.startswith(prefix) and len(branch) > len(prefix) for prefix in TEMPORARY_BRANCH_PREFIXES)


def _is_gitflow_branch(branch: str) -> bool:
    return branch in PERMANENT_BRANCHES or _is_temporary_branch(branch)


def validate_gitflow_event(event_name: str, ref_name: str = "", head_ref: str = "", base_ref: str = "") -> None:
    """Require the documented GitFlow branch relationships for a workflow event."""

    if event_name in {"push", "workflow_dispatch"}:
        if not _is_gitflow_branch(ref_name):
            raise ValidationError(
                f"{event_name} ref '{ref_name or '<empty>'}' is not main, develop, feature/*, release/*, or hotfix/*."
            )
        return

    if event_name == "pull_request":
        if not head_ref or not base_ref:
            raise ValidationError("pull_request validation requires both head and base branch names.")

        if base_ref == "develop" and _is_temporary_branch(head_ref):
            return
        if base_ref == "main" and (
            (head_ref.startswith("release/") and len(head_ref) > len("release/"))
            or (head_ref.startswith("hotfix/") and len(head_ref) > len("hotfix/"))
        ):
            return

        raise ValidationError(
            f"Pull request relationship '{head_ref}' -> '{base_ref}' is not allowed by the GitFlow handbook."
        )

    raise ValidationError(f"Workflow event '{event_name or '<empty>'}' is not supported by this CI policy.")


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _nonnegative_integer(element: ET.Element, attribute: str) -> int:
    value = element.attrib.get(attribute, "0")
    try:
        result = int(value)
    except ValueError as error:
        raise ValidationError(
            f"JUnit element '{_local_name(element.tag)}' has non-integer {attribute}='{value}'."
        ) from error
    if result < 0:
        raise ValidationError(
            f"JUnit element '{_local_name(element.tag)}' has negative {attribute}='{value}'."
        )
    return result


def validate_junit(path: Path, minimum_tests: int = 1) -> int:
    """Require readable JUnit evidence with enough tests and no failures or errors."""

    if minimum_tests < 1:
        raise ValidationError("minimum_tests must be at least 1.")
    if not path.is_file():
        raise ValidationError(f"JUnit evidence file does not exist: {path}")

    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as error:
        raise ValidationError(f"JUnit evidence is not readable XML: {path}: {error}") from error

    root_name = _local_name(root.tag)
    if root_name not in {"testsuite", "testsuites"}:
        raise ValidationError(f"JUnit root must be testsuite or testsuites, got '{root_name}'.")

    test_cases = [element for element in root.iter() if _local_name(element.tag) == "testcase"]
    if len(test_cases) < minimum_tests:
        raise ValidationError(
            f"JUnit evidence contains {len(test_cases)} test case(s); at least {minimum_tests} are required."
        )

    suites = [element for element in root.iter() if _local_name(element.tag) in {"testsuite", "testsuites"}]
    reported_failures = any(_nonnegative_integer(suite, "failures") > 0 for suite in suites)
    reported_errors = any(_nonnegative_integer(suite, "errors") > 0 for suite in suites)
    case_failures = any(
        _local_name(child.tag) in {"failure", "error"}
        for test_case in test_cases
        for child in test_case
    )
    if reported_failures or reported_errors or case_failures:
        raise ValidationError("JUnit evidence reports at least one failed or errored test case.")

    return len(test_cases)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    gitflow = commands.add_parser("gitflow", help="validate a GitHub Actions event against GitFlow")
    gitflow.add_argument("--event-name", required=True)
    gitflow.add_argument("--ref-name", default="")
    gitflow.add_argument("--head-ref", default="")
    gitflow.add_argument("--base-ref", default="")

    junit = commands.add_parser("junit", help="validate CTest JUnit evidence")
    junit.add_argument("path", type=Path)
    junit.add_argument("--minimum-tests", type=int, default=1)
    return parser


def main(arguments: list[str] | None = None) -> int:
    args = _parser().parse_args(arguments)
    try:
        if args.command == "gitflow":
            validate_gitflow_event(args.event_name, args.ref_name, args.head_ref, args.base_ref)
            print("GitFlow event relationship is valid.")
        else:
            test_count = validate_junit(args.path, args.minimum_tests)
            print(f"JUnit evidence is valid: {test_count} test case(s), zero failures, zero errors.")
    except ValidationError as error:
        print(f"CI validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
