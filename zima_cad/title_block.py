from __future__ import annotations

import configparser
from pathlib import Path

from zima_cad.drawing_format import load_native_geometry


def _load_fields(
    parser: configparser.ConfigParser,
    pens: dict,
) -> list[dict]:
    fields: list[dict] = []
    for section in parser.sections():
        if not section.startswith("Field."):
            continue
        field_id = section.removeprefix("Field.").strip()
        if not field_id:
            raise ValueError("A title-block field must have an identifier.")
        pen_name = parser.get(section, "Pen", fallback="GREEN").upper()
        if pen_name not in pens:
            raise ValueError(f"Unsupported drawing pen: {pen_name}")
        parameter = parser.get(section, "Parameter", fallback="").strip()
        source = parser.get(section, "Source", fallback="").strip()
        if not parameter and not source:
            raise ValueError(
                f"{section} must define either Parameter or Source."
            )
        fields.append({
            "kind": "field",
            "id": field_id,
            "parameter": parameter,
            "source": source or f"user_parameter.{parameter}",
            "default": parser.get(section, "Default", fallback=""),
            "format": parser.get(section, "Format", fallback=""),
            "box_width": parser.getfloat(section, "BoxWidth", fallback=0.0),
            "box_height": parser.getfloat(section, "BoxHeight", fallback=0.0),
            "align": parser.get(section, "Align", fallback="left").lower(),
            "vertical_align": parser.get(
                section, "VerticalAlign", fallback="baseline"
            ).lower(),
            "offset_y": parser.getfloat(section, "OffsetY", fallback=0.0),
            "x": parser.getfloat(section, "X"),
            "y": parser.getfloat(section, "Y"),
            "height": parser.getfloat(section, "Height"),
            "pen": pen_name,
            "editable": parser.getboolean(section, "Editable", fallback=True),
            "write_back": parser.getboolean(
                section, "WriteBack", fallback=True
            ),
        })
    return fields


def load_title_block(path: Path) -> dict:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig") as stream:
        parser.read_file(stream)
    if not parser.has_section("TitleBlock"):
        raise ValueError("The .tblz file must contain a [TitleBlock] section.")
    width = parser.getfloat("TitleBlock", "Width")
    height = parser.getfloat("TitleBlock", "Height")
    if width <= 0.0 or height <= 0.0:
        raise ValueError("Title-block dimensions must be positive.")
    geometry, pens = load_native_geometry(parser, "Geometry")
    fields = _load_fields(parser, pens)
    schema_version = parser.getint(
        "TitleBlock", "SchemaVersion", fallback=1
    )
    return {
        "schema_version": schema_version,
        "coordinate_system": (
            "bottom_right" if schema_version >= 3 else "bottom_left"
        ),
        "name": parser.get("TitleBlock", "Name", fallback=path.stem),
        "width": width,
        "height": height,
        "anchor": parser.get(
            "TitleBlock", "Anchor", fallback="bottom-right"
        ).lower(),
        "geometry": geometry,
        "fields": fields,
        "pens": pens,
    }
