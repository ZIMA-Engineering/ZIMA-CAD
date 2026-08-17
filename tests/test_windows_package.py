import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.windows_package import (
    PackageValidationError,
    validate_archive,
)


def _runtime_entries(prefix: str = "") -> list[tuple[str, bytes]]:
    return [
        (f"{prefix}python.exe", b"python"),
        (f"{prefix}Scripts/conda-unpack.exe", b"unpack"),
        (f"{prefix}Library/bin/openblas.dll", b"openblas"),
        (f"{prefix}Library/bin/libblas.dll", b"blas"),
        (f"{prefix}Library/bin/libcblas.dll", b"cblas"),
        (
            f"{prefix}conda-meta/libblas-3.11.0-9_build_openblas.json",
            b"{}",
        ),
        (
            f"{prefix}conda-meta/libcblas-3.11.0-9_build_openblas.json",
            b"{}",
        ),
        (
            f"{prefix}conda-meta/liblapack-3.11.0-9_build_openblas.json",
            b"{}",
        ),
        (f"{prefix}conda-meta/libopenblas-0.3.34-build.json", b"{}"),
    ]


class WindowsPackageValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self._temporary_directory.name)

    def tearDown(self) -> None:
        self._temporary_directory.cleanup()

    def _write_zip(
        self,
        name: str,
        entries: list[tuple[str, bytes]],
    ) -> Path:
        archive = self.directory / name
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
            for member, contents in entries:
                package.writestr(member, contents)
        return archive

    def test_accepts_openblas_runtime(self) -> None:
        archive = self._write_zip("runtime.zip", _runtime_entries())

        report = validate_archive(archive, "runtime")

        self.assertEqual(report.entry_count, len(_runtime_entries()))
        self.assertEqual(report.kind, "runtime")
        self.assertEqual(len(report.sha256), 64)

    def test_accepts_single_root_windows_build(self) -> None:
        root = "ZIMA-CAD-WINDOWS-2026.08.17"
        prefix = f"{root}/runtime/windows/python/"
        entries = _runtime_entries(prefix)
        entries.extend(
            [
                (f"{root}/main.py", b"print('ZIMA-CAD')"),
                (f"{root}/zima-cad.bat", b"@echo off"),
            ]
        )
        archive = self._write_zip("build.zip", entries)

        report = validate_archive(archive, "build")

        self.assertEqual(report.kind, "build")

    def test_rejects_runtime_without_openblas_dll(self) -> None:
        entries = [
            entry
            for entry in _runtime_entries()
            if entry[0] != "Library/bin/openblas.dll"
        ]
        archive = self._write_zip("missing-openblas.zip", entries)

        with self.assertRaisesRegex(
            PackageValidationError,
            "openblas.dll",
        ):
            validate_archive(archive, "runtime")

    def test_rejects_mkl_blas_provider_metadata(self) -> None:
        entries = []
        for member, contents in _runtime_entries():
            if member.startswith("conda-meta/libblas-"):
                member = "conda-meta/libblas-3.11.0-8_build_mkl.json"
            entries.append((member, contents))
        archive = self._write_zip("mkl-provider.zip", entries)

        with self.assertRaisesRegex(
            PackageValidationError,
            "OpenBLAS provider",
        ):
            validate_archive(archive, "runtime")

    def test_rejects_development_headers(self) -> None:
        entries = _runtime_entries()
        entries.append(("Library/include/viskores/very-long-header.hxx", b""))
        archive = self._write_zip("headers.zip", entries)

        with self.assertRaisesRegex(
            PackageValidationError,
            "Development-only tree",
        ):
            validate_archive(archive, "runtime")

    def test_rejects_already_finalized_runtime_marker(self) -> None:
        entries = _runtime_entries()
        entries.append((".zima-conda-unpacked", b""))
        archive = self._write_zip("finalized.zip", entries)

        with self.assertRaisesRegex(
            PackageValidationError,
            "Finalized runtime state",
        ):
            validate_archive(archive, "runtime")

    def test_rejects_member_over_runtime_length_budget(self) -> None:
        entries = _runtime_entries()
        entries.append((f"Lib/{'x' * 137}", b""))
        archive = self._write_zip("long-path.zip", entries)

        with self.assertRaisesRegex(PackageValidationError, "limit is 140"):
            validate_archive(archive, "runtime")

    def test_rejects_path_traversal(self) -> None:
        entries = _runtime_entries()
        entries.append(("../outside.txt", b""))
        archive = self._write_zip("traversal.zip", entries)

        with self.assertRaisesRegex(PackageValidationError, "unsafe"):
            validate_archive(archive, "runtime")


if __name__ == "__main__":
    unittest.main()
