from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


class UbuntuContainerDefinitionTests(unittest.TestCase):
    def test_provisions_native_build_prerequisites(self) -> None:
        dockerfile = (REPOSITORY_ROOT / "scripts/ci/Dockerfile.ubuntu24").read_text(
            encoding="utf-8"
        )

        for package in ("build-essential", "gawk", "python3-venv"):
            with self.subTest(package=package):
                self.assertRegex(dockerfile, rf"(?m)^\s*{package}\s*\\$")


if __name__ == "__main__":
    unittest.main()
