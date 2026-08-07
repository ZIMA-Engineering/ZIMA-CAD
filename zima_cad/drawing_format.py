from __future__ import annotations

import configparser
from pathlib import Path

from zima_cad.drawing_style import load_drawing_style


def load_native_geometry(
    parser: configparser.ConfigParser,
    section: str,
) -> tuple[list[dict], dict]:
    pens = load_drawing_style()["pens"]
    geometry: list[dict] = []
    if not parser.has_section(section):
        return geometry, pens
    for key, raw_value in parser.items(section):
        values = [value.strip() for value in raw_value.split(",")]
        kind = key.rstrip("0123456789").lower()
        if kind == "line" and len(values) == 5:
            pen_name = values[4].upper()
            if pen_name not in pens:
                raise ValueError(f"Unsupported drawing pen: {values[4]}")
            geometry.append({
                "kind": "line",
                "x1": float(values[0]),
                "y1": float(values[1]),
                "x2": float(values[2]),
                "y2": float(values[3]),
                "pen": pen_name,
            })
        elif kind == "circle" and len(values) == 4:
            pen_name = values[3].upper()
            if pen_name not in pens:
                raise ValueError(f"Unsupported drawing pen: {values[3]}")
            geometry.append({
                "kind": "circle",
                "x": float(values[0]),
                "y": float(values[1]),
                "radius": float(values[2]),
                "pen": pen_name,
            })
        elif kind == "text" and len(values) in (5, 6):
            pen_name = values[4].upper()
            if pen_name not in pens:
                raise ValueError(f"Unsupported drawing pen: {values[4]}")
            geometry.append({
                "kind": "text",
                "text": values[0],
                "x": float(values[1]),
                "y": float(values[2]),
                "height": float(values[3]),
                "pen": pen_name,
                "align": (
                    values[5].lower() if len(values) == 6
                    else "center" if section == "FrameGeometry"
                    else "left"
                ),
            })
        else:
            raise ValueError(f"Invalid drawing geometry entry: {key}")
    return geometry, pens


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
    if document_type not in {"part", "assembly", "any"}:
        raise ValueError(f"Unsupported document type: {document_type}")

    def number(section: str, key: str, fallback: float) -> float:
        value = parser.getfloat(section, key, fallback=fallback)
        if value < 0.0:
            raise ValueError(f"{section}.{key} cannot be negative")
        return value

    geometry, frame_pens = load_native_geometry(parser, "FrameGeometry")

    schema_version = parser.getint("Format", "SchemaVersion", fallback=1)
    return {
        "schema_version": schema_version,
        "coordinate_system": (
            "bottom_right" if schema_version >= 3 else "bottom_left"
        ),
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
            "geometry": geometry,
            "pens": frame_pens,
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
