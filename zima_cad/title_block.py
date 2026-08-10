from __future__ import annotations

import configparser
import json
from pathlib import Path
import re

from zima_cad.drawing_format import load_native_geometry
from zima_cad.sketch_geometry import (
    center_arc_points,
    ellipse_points,
    elliptical_arc_points,
)
from zima_cad.sketch_model import SketchModel


TITLE_BLOCK_TOKEN_PATTERN = re.compile(
    r"&((?:[^\W\d]|_)\w*(?:\.(?:[^\W\d]|_)\w*)*)"
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
    if token.startswith(("document.", "sheet.", "bom.")):
        return "system"
    return "model"


def title_block_parameter_key(token: str, context: dict) -> str:
    """Resolve a displayed/localized parameter name to its stable key."""
    key = str(token)
    if key.startswith("model."):
        key = key.removeprefix("model.")
    elif key.startswith("user_parameter."):
        key = key.removeprefix("user_parameter.")
    aliases = context.get("parameter_aliases", {})
    return str(aliases.get(key, key)) if isinstance(aliases, dict) else key


def resolve_title_block_text(
    field: dict,
    *,
    context: dict,
    sheet: dict,
) -> str:
    """Resolve every token embedded in one title-block text field."""
    template = title_block_field_text(field)
    field_format = str(field.get("format", ""))
    field_locale = str(field.get("locale", "cs")).strip() or "cs"

    def value(token: str) -> str:
        bom_row = context.get("bom_row", {})
        if token == "bom.item_number":
            return str(bom_row.get("item", "")) if isinstance(bom_row, dict) else ""
        if token == "bom.quantity":
            return str(bom_row.get("quantity", "")) if isinstance(bom_row, dict) else ""
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
        key = title_block_parameter_key(token, context)
        localized_values = context.get("parameter_values", {}).get(key, {})
        if isinstance(localized_values, dict):
            if "" in localized_values:
                return str(localized_values.get("", ""))
            if field_locale in localized_values:
                return str(localized_values.get(field_locale, ""))
        return str(context.get("parameters", {}).get(key, ""))

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
    locale = parser.get("TitleBlock", "Locale", fallback="cs").strip() or "cs"
    geometry, pens = load_native_geometry(parser, "Geometry")
    for entity in geometry:
        if entity.get("kind") == "text":
            entity["locale"] = locale
    sketch_model = None
    if parser.has_option("Sketch", "Data"):
        sketch_model = SketchModel.from_dict(json.loads(parser.get("Sketch", "Data")))
        entities, _dimensions = sketch_model.to_editor_data()
        points = {
            str(entity["id"]): entity
            for entity in entities if entity.get("type") == "point"
        }
        geometry = []

        def add_polyline(samples: list[tuple[float, float]], pen: str) -> None:
            for first, second in zip(samples, samples[1:]):
                geometry.append({
                    "kind": "line", "x1": first[0], "y1": first[1],
                    "x2": second[0], "y2": second[1], "pen": pen,
                })

        for entity in entities:
            # Repeat-region geometry is an editor-only boundary marker.  The
            # real table geometry inside it is what gets copied for each BOM
            # row; rendering this marker would cover those lines with its own
            # editor pen.
            if entity.get("repeat_region_id"):
                continue
            if entity.get("text_role") in {"outline", "outline_point"}:
                continue
            if entity.get("template_field_id"):
                continue
            kind = entity.get("type")
            pen = str(entity.get("pen", "GREEN")).upper()
            if pen not in pens:
                raise ValueError(f"Unsupported drawing pen: {pen}")
            if kind == "point" and entity.get("text_role") == "anchor":
                geometry.append({
                    "kind": "text", "text": str(entity.get("text_value", "")),
                    "x": float(entity.get("x", 0.0)),
                    "y": float(entity.get("y", 0.0)),
                    "height": float(entity.get("text_height", 2.5)),
                    "pen": pen,
                    "align": str(entity.get("text_horizontal", "left")),
                    "vertical_align": str(entity.get("text_vertical", "bottom")),
                    "angle": float(entity.get("text_angle", 0.0)),
                    "flip": bool(entity.get("text_flip", False)),
                    "font": str(entity.get("text_font", "osifont")),
                })
                continue
            point_ids = list(entity.get("point_ids", ()))
            raw = [
                (float(points[item]["x"]), float(points[item]["y"]))
                for item in point_ids if item in points
            ]
            if kind in ("segment", "construction") and len(raw) == 2:
                add_polyline(raw, pen)
            elif kind == "circle" and len(raw) == 1:
                geometry.append({
                    "kind": "circle", "x": raw[0][0], "y": raw[0][1],
                    "radius": float(entity.get("radius", 0.0)), "pen": pen,
                })
            elif kind == "arc" and len(raw) >= 3:
                add_polyline(list(center_arc_points(
                    raw[0], raw[1], raw[2],
                    clockwise=bool(entity.get("clockwise", False)),
                )), pen)
            elif kind == "ellipse" and len(raw) >= 3:
                add_polyline(list(ellipse_points(raw[0], raw[1], raw[2])), pen)
            elif kind == "elliptical_arc" and len(raw) >= 5:
                add_polyline(list(elliptical_arc_points(
                    raw[0], raw[1], raw[2], raw[3], raw[4],
                    clockwise=bool(entity.get("clockwise", False)),
                )), pen)
            elif kind == "spline":
                add_polyline(raw, pen)
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
    for entity in geometry:
        if entity.get("kind") == "text":
            entity["locale"] = locale
    fields = _load_fields(parser, pens)
    for field in fields:
        field["locale"] = locale
    if sketch_model is not None:
        anchors = {
            str(point.attributes.get("template_field_id")): point
            for point in sketch_model.points.values()
            if point.attributes.get("template_field_id")
        }
        for field in fields:
            anchor = anchors.get(str(field["id"]))
            if anchor is None:
                continue
            field.update({
                "anchor_x": anchor.x,
                "anchor_y": anchor.y,
                "align": str(anchor.attributes.get("text_horizontal", "left")),
                "vertical_align": str(anchor.attributes.get("text_vertical", "bottom")),
                "height": float(anchor.attributes.get("text_height", field["height"])),
                "angle": float(anchor.attributes.get("text_angle", 0.0)),
                "flip": bool(anchor.attributes.get("text_flip", False)),
                "font": str(anchor.attributes.get("text_font", "osifont")),
            })
    schema_version = parser.getint(
        "TitleBlock", "SchemaVersion", fallback=1
    )
    repeat_regions = []
    for section in parser.sections():
        if not section.startswith("RepeatRegion."):
            continue
        repeat_regions.append({
            "id": section.removeprefix("RepeatRegion.").strip(),
            "kind": parser.get(section, "Kind", fallback="generic").lower(),
            "x": parser.getfloat(section, "X"),
            "y": parser.getfloat(section, "Y"),
            "width": parser.getfloat(section, "Width"),
            "height": parser.getfloat(section, "Height"),
            "direction": parser.get(section, "Direction", fallback="up").lower(),
            "step": parser.getfloat(section, "Step", fallback=0.0),
        })
    return {
        "schema_version": schema_version,
        "coordinate_system": (
            "bottom_right" if schema_version >= 3 else "bottom_left"
        ),
        "name": parser.get("TitleBlock", "Name", fallback=path.stem),
        "locale": locale,
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
        "repeat_regions": repeat_regions,
        "pens": pens,
    }
