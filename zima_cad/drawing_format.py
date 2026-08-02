from __future__ import annotations

import configparser
from pathlib import Path


def load_drawing_format(path: Path) -> dict:
    """Load and validate a ZIMA frame and title-block definition."""
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig") as stream:
        parser.read_file(stream)
    if not parser.has_section("Format") or not parser.has_section("Frame"):
        raise ValueError("The .frmz file must contain [Format] and [Frame] sections.")

    sheet_format = parser.get("Format", "SheetFormat").upper()
    if sheet_format not in {"A4", "A3", "A2", "A1", "A0"}:
        raise ValueError(f"Unsupported drawing sheet format: {sheet_format}")
    orientation = parser.get("Format", "Orientation").lower()
    if orientation not in {"portrait", "landscape"}:
        raise ValueError(f"Unsupported drawing orientation: {orientation}")
    document_type = parser.get("Format", "DocumentType").lower()
    if document_type not in {"part", "assembly"}:
        raise ValueError(f"Unsupported document type: {document_type}")

    def number(section: str, key: str, fallback: float) -> float:
        value = parser.getfloat(section, key, fallback=fallback)
        if value < 0.0:
            raise ValueError(f"{section}.{key} cannot be negative")
        return value

    return {
        "schema_version": parser.getint("Format", "SchemaVersion", fallback=1),
        "name": parser.get("Format", "Name", fallback=path.stem),
        "sheet_format": sheet_format,
        "orientation": orientation,
        "document_type": document_type,
        "frame": {
            "left": number("Frame", "LeftMargin", 20.0),
            "right": number("Frame", "RightMargin", 10.0),
            "top": number("Frame", "TopMargin", 10.0),
            "bottom": number("Frame", "BottomMargin", 10.0),
            "color": parser.get("Frame", "Color", fallback="#FFFFFF"),
            "line_width": number("Frame", "LineWidth", 0.7),
        },
        "title_block": {
            "enabled": parser.getboolean("TitleBlock", "Enabled", fallback=True),
            "width": number("TitleBlock", "Width", 180.0),
            "height": number("TitleBlock", "Height", 45.0),
            "color": parser.get("TitleBlock", "Color", fallback="#4DD811"),
            "line_width": number("TitleBlock", "LineWidth", 0.25),
            "variant": parser.get("TitleBlock", "Variant", fallback=document_type),
            "bom_enabled": parser.getboolean("TitleBlock", "BomEnabled", fallback=False),
        },
    }
