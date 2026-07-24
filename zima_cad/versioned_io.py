from __future__ import annotations

import configparser
import os
import shutil
import tempfile
from pathlib import Path
from typing import Callable


PathWriter = Callable[[Path], None]
PathValidator = Callable[[Path], None]


def validate_ini_file(path: Path) -> None:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    with path.open("r", encoding="utf-8-sig") as stream:
        config.read_file(stream)


def versioned_write(
    target_path: Path,
    writer: PathWriter,
    validator: PathValidator | None = None,
) -> Path | None:
    """Safely replace a file and archive its previous contents as ``name.N``."""
    target = _absolute_path(target_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=target.parent,
        prefix=f".{target.name}.",
        suffix=".tmp",
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    archive_path: Path | None = None

    try:
        writer(temporary_path)
        if validator is not None:
            validator(temporary_path)
        if target.exists():
            if not target.is_file():
                raise IsADirectoryError(target)
            archive_path = _archive_existing_file(target)
        os.replace(temporary_path, target)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        if archive_path is not None:
            archive_path.unlink(missing_ok=True)
        raise

    return archive_path


def write_text_versioned(
    target_path: Path,
    text: str,
    *,
    encoding: str = "utf-8",
    validator: PathValidator | None = None,
) -> Path | None:
    def writer(temporary_path: Path) -> None:
        temporary_path.write_text(text, encoding=encoding, newline="\n")

    return versioned_write(target_path, writer, validator)


def next_archive_path(target_path: Path) -> Path:
    target = _absolute_path(target_path)
    prefix = f"{target.name}."
    highest_version = 0
    if target.parent.is_dir():
        for candidate in target.parent.iterdir():
            if not candidate.is_file() or not candidate.name.startswith(prefix):
                continue
            suffix = candidate.name[len(prefix) :]
            if suffix.isdigit():
                highest_version = max(highest_version, int(suffix))
    return target.with_name(f"{target.name}.{highest_version + 1}")


def _archive_existing_file(target: Path) -> Path:
    while True:
        archive = next_archive_path(target)
        try:
            with target.open("rb") as source, archive.open("xb") as destination:
                shutil.copyfileobj(source, destination)
            shutil.copystat(target, archive)
            return archive
        except FileExistsError:
            continue
        except Exception:
            archive.unlink(missing_ok=True)
            raise


def _absolute_path(path: Path) -> Path:
    return Path(os.path.abspath(path.expanduser()))
