from __future__ import annotations

import configparser
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from zima_cad.paths import app_path, ensure_application_directories


PATH_KEYS = ("Materials", "Templates", "Formats", "Localization")
_UNSET = object()


@dataclass(frozen=True)
class StartupContext:
    working_directory: Path
    document_path: Path | None = None
    local_config_path: Path | None = None


@dataclass(frozen=True)
class ApplicationSettings:
    language: str = "cs"
    config_path: Path = Path("config/config.ini")
    base_config_path: Path = Path("config/config.ini")
    local_config_path: Path | None = None
    materials_path: Path = Path("config/materials")
    templates_path: Path = Path("config/templates")
    formats_path: Path = Path("config/formats")
    localization_path: Path = Path("config/localization")
    part_template_path: Path = Path("config/templates/start_part.prtz")
    path_sources: dict[str, Path] = field(default_factory=dict)
    units: dict[str, str] = field(default_factory=dict)


def base_config_path() -> Path:
    return app_path("config", "config.ini").resolve()


def default_config_path() -> Path:
    local_config = (Path.cwd() / "config.ini").resolve()
    return local_config if local_config.is_file() else base_config_path()


def resolve_startup_context(
    arguments: Iterable[str],
    current_directory: Path | None = None,
) -> tuple[StartupContext, list[str]]:
    cwd = (current_directory or Path.cwd()).resolve()
    explicit_working_directory: Path | None = None
    target: Path | None = None
    qt_arguments: list[str] = []
    args = list(arguments)
    index = 0
    while index < len(args):
        argument = args[index]
        if argument in {"--working-directory", "-w"}:
            if index + 1 >= len(args):
                raise ValueError(f"{argument} requires a directory")
            explicit_working_directory = _absolute_path(args[index + 1], cwd)
            index += 2
            continue
        if argument.startswith("--working-directory="):
            explicit_working_directory = _absolute_path(
                argument.split("=", 1)[1],
                cwd,
            )
            index += 1
            continue
        if not argument.startswith("-") and target is None:
            target = _absolute_path(argument, cwd)
        else:
            qt_arguments.append(argument)
        index += 1

    document_path: Path | None = None
    if target is not None and target.is_dir():
        target_directory = target
    elif target is not None:
        document_path = target
        target_directory = target.parent
    else:
        target_directory = cwd

    working_directory = (explicit_working_directory or target_directory).resolve()
    if not working_directory.is_dir():
        raise ValueError(f"Working directory does not exist: {working_directory}")

    local_config = working_directory / "config.ini"
    return (
        StartupContext(
            working_directory=working_directory,
            document_path=document_path,
            local_config_path=local_config if local_config.is_file() else None,
        ),
        qt_arguments,
    )


def load_application_settings(
    local_config_path: Path | None | object = _UNSET,
) -> ApplicationSettings:
    ensure_application_directories()
    base_path = base_config_path()
    if local_config_path is _UNSET:
        default_path = default_config_path()
        local_config_path = default_path if default_path != base_path else None
    local_path = (
        local_config_path.resolve()
        if isinstance(local_config_path, Path)
        and local_config_path.resolve() != base_path
        and local_config_path.is_file()
        else None
    )
    base = _read_config(base_path)
    local = _read_config(local_path) if local_path is not None else None

    language, _ = _layered_value(
        base,
        local,
        "Application",
        "Language",
        "cs",
        base_path,
        local_path,
    )
    language = language.strip() or "cs"

    resolved_paths: dict[str, Path] = {}
    path_sources: dict[str, Path] = {}
    path_defaults = {
        "Materials": "materials",
        "Templates": "templates",
        "Formats": "formats",
        "Localization": "localization",
    }
    for path_name in PATH_KEYS:
        path_text, source = _layered_value(
            base,
            local,
            "Paths",
            path_name,
            path_defaults[path_name],
            base_path,
            local_path,
        )
        resolved_paths[path_name] = resolve_config_path(source, path_text)
        path_sources[path_name] = source

    units = dict(base["Units"]) if base.has_section("Units") else {}
    if local is not None and local.has_section("Units"):
        units.update(
            {
                key: value
                for key, value in local["Units"].items()
                if value.strip()
            }
        )

    part_template_text, _ = _layered_value(
        base,
        local,
        "Templates",
        "Part",
        "start_part.prtz",
        base_path,
        local_path,
    )
    templates_directory = resolved_paths["Templates"]
    part_template = Path(part_template_text.strip().replace("\\", "/"))
    if not part_template.is_absolute():
        part_template = templates_directory / part_template

    active_path = local_path or base_path
    return ApplicationSettings(
        language=language,
        config_path=active_path,
        base_config_path=base_path,
        local_config_path=local_path,
        materials_path=resolved_paths["Materials"],
        templates_path=resolved_paths["Templates"],
        formats_path=resolved_paths["Formats"],
        localization_path=resolved_paths["Localization"],
        part_template_path=part_template.resolve(),
        path_sources=path_sources,
        units=units,
    )


def resolve_config_path(config_path: Path, path_text: str) -> Path:
    portable_text = path_text.strip().replace("\\", "/")
    path = Path(portable_text)
    if path.is_absolute():
        return path.resolve()
    return (config_path.parent / path).resolve()


def portable_config_path(path_text: str) -> str:
    return path_text.strip().replace("\\", "/")


def _absolute_path(path_text: str, cwd: Path) -> Path:
    path = Path(path_text).expanduser()
    return (path if path.is_absolute() else cwd / path).resolve()


def _read_config(path: Path | None) -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    if path is not None:
        config.read(path, encoding="utf-8-sig")
    return config


def _layered_value(
    base: configparser.ConfigParser,
    local: configparser.ConfigParser | None,
    section: str,
    key: str,
    fallback: str,
    base_path: Path,
    local_path: Path | None,
) -> tuple[str, Path]:
    if local is not None:
        local_value = local.get(section, key, fallback="").strip()
        if local_value:
            return local_value, local_path or base_path
    return base.get(section, key, fallback=fallback), base_path
