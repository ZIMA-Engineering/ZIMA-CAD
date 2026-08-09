from __future__ import annotations

import configparser
from pathlib import Path
import re

from zima_cad.drawing_format import load_native_geometry


TITLE_BLOCK_TOKEN_PATTERN = re.compile(
    r"&([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
)


def title_block_field_text(field: dict) -> str:
    """Return the complete text template represented by a field."""
    text = str(field.get("text", ""))
    if text:
        return text
    parameter = str(field.get("parameter", "")).strip()
    if parameter:
        return f"&{parameter}"
    source = str(field.get("source", "")).strip()
    return f"&{source}" if source else ""


def title_block_tokens(text: str) -> tuple[str, ...]:
    """Return unique field tokens in their written order."""
    return tuple(dict.fromkeys(
        match.group(1) for match in TITLE_BLOCK_TOKEN_PATTERN.finditer(text)
    ))


def title_block_token_scope(token: str) -> str:
    """Classify a token by the document which owns its editable value."""
    if token.startswith(("drawing.", "local.")):
        return "drawing"
    if token.startswith(("document.", "sheet.")):
        return "system"
    return "model"


def resolve_title_block_text(
    field: dict,
    *,
    context: dict,
    sheet: dict,
) -> str:
    """Resolve every token embedded in one title-block text field."""
    template = title_block_field_text(field)
    field_format = str(field.get("format", ""))

    def value(token: str) -> str:
        if token == "document.file_stem":
            return str(context.get("file_stem", ""))
        if token == "sheet.format":
            return str(sheet.get("format", ""))
        if token == "sheet.position":
            index = int(context.get("sheet_index", 0)) + 1
            count = max(1, int(context.get("sheet_count", 1)))
            pattern = field_format or "{index}/{count}"
            return pattern.format(index=index, count=count)
        if token == "sheet.scale":
            numerator = int(round(float(
                sheet.get("default_scale_numerator", 1)
            )))
            denominator = int(round(float(sheet.get("default_scale", 1))))
            pattern = field_format or "{numerator}:{denominator}"
            return pattern.format(
                numerator=numerator,
                denominator=denominator,
            )
        if token.startswith(("drawing.", "local.")):
            key = token.split(".", 1)[1]
            return str(context.get("local_parameters", {}).get(key, ""))
        if token.startswith("model."):
            token = token.removeprefix("model.")
        elif token.startswith("user_parameter."):
            token = token.removeprefix("user_parameter.")
        return str(context.get("parameters", {}).get(token, ""))

    resolved = TITLE_BLOCK_TOKEN_PATTERN.sub(
        lambda match: value(match.group(1)),
        template,
    )
    return resolved if resolved.strip() else str(field.get("default", ""))


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
        text = parser.get(section, "Text", fallback="").strip()
        if not text and not parameter and not source:
            raise ValueError(
                f"{section} must define Text, Parameter or Source."
            )
        field_text = text or f"&{parameter or source}"
        fields.append({
            "kind": "field",
            "id": field_id,
            "parameter": parameter,
            "source": source or (
                f"user_parameter.{parameter}" if parameter else ""
            ),
            "text": field_text,
            "tokens": title_block_tokens(field_text),
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
    geometry_x = [
        float(value)
        for entity in geometry
        for value in (
            (entity.get("x1"), entity.get("x2"))
            if entity.get("kind") == "line"
            else (entity.get("x"),)
        )
        if value is not None
    ]
    geometry_y = [
        float(value)
        for entity in geometry
        for value in (
            (entity.get("y1"), entity.get("y2"))
            if entity.get("kind") == "line"
            else (entity.get("y"),)
        )
        if value is not None
    ]
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
        "content_origin": (
            min(geometry_x, default=0.0),
            min(geometry_y, default=0.0),
        ),
        "fields": fields,
        "pens": pens,
    }
