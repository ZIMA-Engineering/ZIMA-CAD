from __future__ import annotations

import configparser
from dataclasses import dataclass, field
from pathlib import Path

from zima_cad.paths import app_path, ensure_application_directories


@dataclass(frozen=True)
class ApplicationSettings:
    language: str = "cs"
    config_path: Path = Path("config/config.ini")
    materials_path: Path = Path("config/materials")
    templates_path: Path = Path("config/templates")
    localization_path: Path = Path("config/localization")
    units: dict[str, str] = field(default_factory=dict)


def default_config_path() -> Path:
    startup_config = Path.cwd() / "config.ini"
    if startup_config.exists():
        return startup_config
    return app_path("config", "config.ini")


def load_application_settings(config_path: Path | None = None) -> ApplicationSettings:
    ensure_application_directories()
    resolved_config_path = config_path or default_config_path()
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(resolved_config_path, encoding="utf-8-sig")

    language = config.get("Application", "Language", fallback="cs").strip() or "cs"
    materials_path = resolve_config_path(
        resolved_config_path,
        config.get("Paths", "Materials", fallback="config/materials"),
    )
    templates_path = resolve_config_path(
        resolved_config_path,
        config.get("Paths", "Templates", fallback="config/templates"),
    )
    localization_path = resolve_config_path(
        resolved_config_path,
        config.get("Paths", "Localization", fallback="config/localization"),
    )
    units = dict(config["Units"]) if config.has_section("Units") else {}
    return ApplicationSettings(
        language=language,
        config_path=resolved_config_path,
        materials_path=materials_path,
        templates_path=templates_path,
        localization_path=localization_path,
        units=units,
    )


def resolve_config_path(config_path: Path, path_text: str) -> Path:
    path = Path(path_text.strip())
    if path.is_absolute():
        return path
    return (config_path.parent / path).resolve()
