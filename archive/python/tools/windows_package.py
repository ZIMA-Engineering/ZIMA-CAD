#!/usr/bin/env python3
"""Validate ZIMA-CAD Windows runtime and release ZIP archives.

The checks in this module deliberately use only the Python standard library so
they can run from the Conda environment used to create a Windows package.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import re
import stat
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


RUNTIME_MAX_MEMBER_LENGTH = 140
BUILD_MAX_MEMBER_LENGTH = 180

_BUILD_ROOT_RE = re.compile(
    r"^ZIMA-CAD-WINDOWS-\d{4}\.\d{2}\.\d{2}(?:-[A-Za-z0-9._-]+)?$"
)
_WINDOWS_RESERVED_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *(f"COM{number}" for number in range(1, 10)),
    *(f"LPT{number}" for number in range(1, 10)),
}
_WINDOWS_INVALID_CHARACTERS = frozenset('<>:"|?*')
_REQUIRED_RUNTIME_FILES = (
    "python.exe",
    "Scripts/conda-unpack.exe",
    "Library/bin/openblas.dll",
    "Library/bin/libblas.dll",
    "Library/bin/libcblas.dll",
)
_REQUIRED_OPENBLAS_METADATA = (
    "conda-meta/libblas-*_openblas.json",
    "conda-meta/libcblas-*_openblas.json",
    "conda-meta/liblapack-*_openblas.json",
    "conda-meta/libopenblas-*.json",
)
_FORBIDDEN_RUNTIME_PREFIXES = (
    "include/",
    "Library/include/",
    "Library/lib/cmake/",
    "Library/lib/pkgconfig/",
    "Library/share/doc/",
    "Library/share/man/",
    "Library/lib/qt6/qml/Qt/test/",
)
_FORBIDDEN_RUNTIME_FILES = {".zima-conda-unpacked"}
# conda-pack may emit this package-owned notice more than once when two Conda
# packages install it. Extraction intentionally keeps the last copy. No other
# duplicate is accepted.
_ALLOWED_CONDA_PACK_DUPLICATES = {"Library/README.txt"}


class PackageValidationError(RuntimeError):
    """Raised when a Windows package violates a release invariant."""


@dataclass(frozen=True)
class ValidationReport:
    archive: Path
    kind: str
    entry_count: int
    uncompressed_bytes: int
    longest_member: str
    longest_member_length: int
    sha256: str


def _validate_windows_member_name(name: str) -> None:
    if not name:
        raise PackageValidationError("ZIP contains an empty member name")
    if "\\" in name:
        raise PackageValidationError(
            f"ZIP member must use forward slashes: {name!r}"
        )

    path = PurePosixPath(name)
    raw_parts = name.rstrip("/").split("/")
    if path.is_absolute() or not raw_parts or raw_parts[0] == "":
        raise PackageValidationError(f"ZIP member is absolute: {name!r}")
    if any(part in {"", ".", ".."} for part in raw_parts):
        raise PackageValidationError(
            f"ZIP member contains an unsafe path component: {name!r}"
        )

    for part in raw_parts:
        if part.endswith((" ", ".")):
            raise PackageValidationError(
                f"ZIP member is not representable on Windows: {name!r}"
            )
        if any(character in _WINDOWS_INVALID_CHARACTERS for character in part):
            raise PackageValidationError(
                f"ZIP member contains a Windows-invalid character: {name!r}"
            )
        basename = part.split(".", 1)[0].upper()
        if basename in _WINDOWS_RESERVED_NAMES:
            raise PackageValidationError(
                f"ZIP member uses a reserved Windows name: {name!r}"
            )


def _runtime_prefix(kind: str, file_names: set[str]) -> tuple[str, str | None]:
    if kind == "runtime":
        return "", None

    roots = {name.split("/", 1)[0] for name in file_names}
    if len(roots) != 1:
        raise PackageValidationError(
            "Windows build must contain exactly one top-level directory"
        )
    root = next(iter(roots))
    if not _BUILD_ROOT_RE.fullmatch(root):
        raise PackageValidationError(
            "Windows build root must be named "
            f"ZIMA-CAD-WINDOWS-YYYY.MM.DD[-suffix], got {root!r}"
        )
    return f"{root}/runtime/windows/python/", root


def _require_runtime_contract(file_names: set[str], prefix: str) -> None:
    missing_files = [
        f"{prefix}{relative}"
        for relative in _REQUIRED_RUNTIME_FILES
        if f"{prefix}{relative}" not in file_names
    ]
    if missing_files:
        raise PackageValidationError(
            "Windows runtime is missing required files: "
            + ", ".join(missing_files)
        )

    lower_names = {name.lower() for name in file_names}
    missing_metadata = []
    for pattern in _REQUIRED_OPENBLAS_METADATA:
        full_pattern = f"{prefix}{pattern}".lower()
        if not any(
            fnmatch.fnmatchcase(name, full_pattern) for name in lower_names
        ):
            missing_metadata.append(full_pattern)
    if missing_metadata:
        raise PackageValidationError(
            "Windows runtime is not pinned to the OpenBLAS provider; missing "
            "metadata matching: "
            + ", ".join(missing_metadata)
        )

    prefix_length = len(prefix)
    runtime_names = (
        name[prefix_length:]
        for name in file_names
        if name.startswith(prefix)
    )
    for relative_name in runtime_names:
        if relative_name in _FORBIDDEN_RUNTIME_FILES:
            raise PackageValidationError(
                "Finalized runtime state leaked into archive: " + relative_name
            )
        if relative_name.lower().endswith(".pdb"):
            raise PackageValidationError(
                f"Development-only PDB leaked into runtime: {relative_name}"
            )
        if any(
            relative_name.startswith(forbidden)
            for forbidden in _FORBIDDEN_RUNTIME_PREFIXES
        ):
            raise PackageValidationError(
                "Development-only tree leaked into runtime: " + relative_name
            )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_archive(
    archive_path: str | Path,
    kind: str,
    *,
    check_crc: bool = True,
    max_member_length: int | None = None,
) -> ValidationReport:
    """Validate a Windows ZIP and return its reproducibility metadata."""

    archive = Path(archive_path).resolve()
    if kind not in {"runtime", "build"}:
        raise ValueError(f"Unsupported package kind: {kind!r}")
    if not archive.is_file():
        raise PackageValidationError(f"Archive not found: {archive}")

    length_limit = max_member_length
    if length_limit is None:
        length_limit = (
            RUNTIME_MAX_MEMBER_LENGTH
            if kind == "runtime"
            else BUILD_MAX_MEMBER_LENGTH
        )

    try:
        with zipfile.ZipFile(archive) as package:
            infos = package.infolist()
            if not infos:
                raise PackageValidationError("ZIP archive is empty")

            exact_names: set[str] = set()
            windows_names: set[str] = set()
            longest = ""
            uncompressed_bytes = 0
            for info in infos:
                name = info.filename
                _validate_windows_member_name(name)
                if info.flag_bits & 0x1:
                    raise PackageValidationError(
                        f"Encrypted ZIP member is unsupported: {name}"
                    )
                unix_mode = info.external_attr >> 16
                if unix_mode and stat.S_ISLNK(unix_mode):
                    raise PackageValidationError(
                        f"Symbolic links are unsupported in Windows ZIPs: {name}"
                    )
                is_allowed_duplicate = (
                    kind == "runtime"
                    and name in _ALLOWED_CONDA_PACK_DUPLICATES
                )
                if name in exact_names and not is_allowed_duplicate:
                    raise PackageValidationError(
                        f"Duplicate ZIP member: {name!r}"
                    )
                exact_names.add(name)
                windows_name = name.rstrip("/").casefold()
                if windows_name in windows_names and not is_allowed_duplicate:
                    raise PackageValidationError(
                        "Case-insensitive Windows path collision: " + name
                    )
                windows_names.add(windows_name)
                if len(name) > len(longest):
                    longest = name
                uncompressed_bytes += info.file_size

            if len(longest) > length_limit:
                raise PackageValidationError(
                    f"Longest ZIP member has {len(longest)} characters; "
                    f"limit is {length_limit}: {longest}"
                )

            file_names = {
                info.filename for info in infos if not info.is_dir()
            }
            prefix, build_root = _runtime_prefix(kind, file_names)
            _require_runtime_contract(file_names, prefix)

            if build_root is not None:
                for relative in ("main.py", "zima-cad.bat"):
                    required = f"{build_root}/{relative}"
                    if required not in file_names:
                        raise PackageValidationError(
                            f"Windows build is missing {required}"
                        )

            if check_crc:
                bad_member = package.testzip()
                if bad_member is not None:
                    raise PackageValidationError(
                        f"CRC check failed for ZIP member: {bad_member}"
                    )
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        if isinstance(error, PackageValidationError):
            raise
        raise PackageValidationError(
            f"Cannot read ZIP archive {archive}: {error}"
        ) from error

    return ValidationReport(
        archive=archive,
        kind=kind,
        entry_count=len(infos),
        uncompressed_bytes=uncompressed_bytes,
        longest_member=longest,
        longest_member_length=len(longest),
        sha256=_sha256(archive),
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--kind", choices=("runtime", "build"), required=True)
    parser.add_argument(
        "--skip-crc",
        action="store_true",
        help="skip the full payload CRC test (never use for a release)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        report = validate_archive(
            args.archive,
            args.kind,
            check_crc=not args.skip_crc,
        )
    except PackageValidationError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"OK: {report.kind} archive {report.archive}")
    print(f"Entries: {report.entry_count}")
    print(f"Uncompressed bytes: {report.uncompressed_bytes}")
    print(
        "Longest member: "
        f"{report.longest_member_length} {report.longest_member}"
    )
    print(f"SHA-256: {report.sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
