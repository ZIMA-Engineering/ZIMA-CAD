from __future__ import annotations

import configparser
from pathlib import Path


class Translator:
    def __init__(self) -> None:
        self.language = "en"
        self.fallback_language = "en"
        self.translations: dict[str, str] = {}
        self.fallback_translations: dict[str, str] = {}

    def load(self, localization_path: Path, language: str) -> None:
        metadata = read_ini(localization_path / "metadata.ini")
        self.language = language
        self.fallback_language = metadata.get(
            "Localization", "FallbackLanguage", fallback="en"
        )
        language_file = metadata.get("Files", language, fallback=f"{language}.ini")
        fallback_file = metadata.get(
            "Files",
            self.fallback_language,
            fallback=f"{self.fallback_language}.ini",
        )
        self.translations = read_translations(localization_path / language_file)
        self.fallback_translations = read_translations(localization_path / fallback_file)

    def text(self, key: str, **values) -> str:
        text = self.translations.get(key, self.fallback_translations.get(key, key))
        return text.format(**values) if values else text


def read_ini(path: Path) -> configparser.ConfigParser:
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(path, encoding="utf-8-sig")
    return config


def read_translations(path: Path) -> dict[str, str]:
    config = read_ini(path)
    if not config.has_section("Translations"):
        return {}
    return dict(config["Translations"])


translator = Translator()


def configure_localization(localization_path: Path, language: str) -> None:
    translator.load(localization_path, language)


def tr(key: str, **values) -> str:
    return translator.text(key, **values)
