from __future__ import annotations

import configparser
import io
import json
from pathlib import Path
from typing import Any

from zima_cad.drawing_format import load_native_geometry
from zima_cad.model import (
    CombineMode,
    EntityKind,
    PartDocument,
    SketchRole,
    ZimaEntity,
    default_document_settings,
)
from zima_cad.sketch_model import (
    GeometryType,
    SketchGeometry,
    SketchModel,
    SketchPoint,
)
from zima_cad.sketch_geometry import (
    center_arc_points,
    ellipse_points,
    elliptical_arc_points,
)
from zima_cad.versioned_io import write_text_versioned


TEMPLATE_TYPES = {".frmz": "drawing_format", ".tblz": "title_block"}
TEMPLATE_PENS = {"WHITE", "GREEN", "YELLOW"}


def _strip_template_text_outlines(model: SketchModel) -> None:
    """Keep semantic text anchors; outlines are regenerated only if needed."""
    removed_points = {
        point_id
        for point_id, point in model.points.items()
        if point.attributes.get("text_role") == "outline_point"
    }
    removed_geometry = {
        geometry_id
        for geometry_id, geometry in model.geometry.items()
        if geometry.attributes.get("text_role") == "outline"
    }
    for geometry_id in removed_geometry:
        model.geometry.pop(geometry_id, None)
    for point_id in removed_points:
        model.points.pop(point_id, None)
    model.constraints = {
        constraint_id: constraint
        for constraint_id, constraint in model.constraints.items()
        if not any(
            point_id in removed_points
            for point_id in constraint.point_ids
        )
        and not any(
            reference_id in removed_geometry
            for reference_id in constraint.reference_ids
        )
    }
    model.dimensions = {
        dimension_id: dimension
        for dimension_id, dimension in model.dimensions.items()
        if not any(
            point_id in removed_points
            for point_id in dimension.point_ids
        )
    }


def _parser(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig") as stream:
        parser.read_file(stream)
    return parser


def _parser_data(parser: configparser.ConfigParser) -> dict[str, dict[str, str]]:
    return {
        section: dict(parser.items(section))
        for section in parser.sections()
        if section not in {"Sketch", "FrameGeometry", "Geometry"}
    }


def _legacy_model(
    parser: configparser.ConfigParser,
    section: str,
    coordinate_width: float,
) -> SketchModel:
    geometry, _pens = load_native_geometry(parser, section)
    model = SketchModel()
    point_index = 0
    geometry_index = 0

    def point(x: float, y: float, **attributes: Any) -> str:
        nonlocal point_index
        point_index += 1
        point_id = f"p{point_index}"
        model.add_point(SketchPoint(
            point_id, round(x, 12), round(y, 12), attributes=attributes
        ))
        return point_id

    for item in geometry:
        pen = str(item.get("pen", "GREEN")).upper()
        if pen not in TEMPLATE_PENS:
            raise ValueError(f"Unsupported drawing pen: {pen}")
        kind = str(item["kind"])
        if kind == "text":
            point(
                coordinate_width - float(item["x"]),
                float(item["y"]),
                text_group=f"text:{point_index + 1}",
                text_role="anchor",
                text_value=str(item["text"]),
                text_height=float(item["height"]),
                text_horizontal=str(item.get("align", "left")),
                text_vertical="bottom",
                text_angle=0.0,
                text_flip=True,
                text_color=pen.lower(),
                text_font="osifont",
                pen=pen,
            )
            continue
        geometry_index += 1
        geometry_id = f"g{geometry_index}"
        if kind == "line":
            points = (
                point(coordinate_width - float(item["x1"]), float(item["y1"])),
                point(coordinate_width - float(item["x2"]), float(item["y2"])),
            )
            model.add_geometry(SketchGeometry(
                geometry_id, GeometryType.SEGMENT, points, {"pen": pen}
            ))
        elif kind == "circle":
            centre = point(coordinate_width - float(item["x"]), float(item["y"]))
            model.add_geometry(SketchGeometry(
                geometry_id,
                GeometryType.CIRCLE,
                (centre,),
                {"radius": float(item["radius"]), "pen": pen},
            ))
    return model


def _field_code(parser: configparser.ConfigParser, section: str) -> str:
    text = parser.get(section, "Text", fallback="").strip()
    if text:
        return text
    parameter = parser.get(section, "Parameter", fallback="").strip()
    source = parser.get(section, "Source", fallback="").strip()
    return f"&{parameter or source}"


def _add_title_block_fields(
    model: SketchModel,
    parser: configparser.ConfigParser,
    coordinate_width: float,
) -> None:
    existing = {
        str(point.attributes.get("template_field_id", ""))
        for point in model.points.values()
    }
    point_index = 1
    while f"field{point_index}" in model.points:
        point_index += 1
    for section in parser.sections():
        if not section.startswith("Field."):
            continue
        field_id = section.removeprefix("Field.").strip()
        if not field_id or field_id in existing:
            continue
        x = parser.getfloat(section, "X")
        y = parser.getfloat(section, "Y")
        width = parser.getfloat(section, "BoxWidth", fallback=0.0)
        height = parser.getfloat(section, "BoxHeight", fallback=0.0)
        align = parser.get(section, "Align", fallback="left").lower()
        vertical = parser.get(
            section, "VerticalAlign", fallback="baseline"
        ).lower()
        box_x = coordinate_width - x - width if width > 0.0 else coordinate_width - x
        anchor_x = box_x + (
            width if align == "left" else width * 0.5 if align == "center" else 0.0
        )
        anchor_y = y + (
            height if vertical == "top"
            else height * 0.5 if vertical == "center"
            else 0.0
        )
        pen = parser.get(section, "Pen", fallback="GREEN").upper()
        point_id = f"field{point_index}"
        point_index += 1
        model.add_point(SketchPoint(
            point_id,
            anchor_x,
            anchor_y,
            attributes={
                "text_group": f"field:{field_id}",
                "text_role": "anchor",
                "text_value": _field_code(parser, section),
                "text_height": parser.getfloat(section, "Height"),
                "text_horizontal": align,
                "text_vertical": vertical,
                "text_angle": 0.0,
                "text_flip": True,
                "text_color": pen.lower(),
                "text_font": "osifont",
                "pen": pen,
                "template_field_id": field_id,
                "template_field_box_width": width,
                "template_field_box_height": height,
                "template_coordinate_width": coordinate_width,
            },
        ))


def load_drawing_template(
    path: Path,
    *,
    template_type: str | None = None,
) -> PartDocument:
    path = Path(path)
    template_type = template_type or TEMPLATE_TYPES.get(path.suffix.lower())
    if template_type is None:
        raise ValueError(f"Unsupported drawing template: {path.suffix}")
    parser = _parser(path)
    required = "Format" if template_type == "drawing_format" else "TitleBlock"
    if not parser.has_section(required):
        raise ValueError(f"The {path.suffix} file must contain [{required}].")
    geometry_section = "FrameGeometry" if template_type == "drawing_format" else "Geometry"
    if template_type == "drawing_format":
        sheet_format = parser.get("Format", "SheetFormat").upper()
        paper_width, paper_height = {
            "A4": (210.0, 297.0), "A3": (297.0, 420.0),
            "A2": (420.0, 594.0), "A1": (594.0, 841.0),
            "A0": (841.0, 1189.0),
        }[sheet_format]
        coordinate_width = (
            paper_width if parser.get("Format", "Orientation").lower() == "portrait"
            else paper_height
        )
    else:
        coordinate_width = parser.getfloat("TitleBlock", "Width")
    if parser.has_section("Sketch"):
        model = SketchModel.from_dict(json.loads(parser.get("Sketch", "Data")))
    else:
        model = _legacy_model(parser, geometry_section, coordinate_width)
    if template_type == "title_block":
        _add_title_block_fields(model, parser, coordinate_width)
        _add_repeat_region_helpers(model, parser)
    _strip_template_text_outlines(model)

    settings = default_document_settings()
    settings.update({
        "type": template_type,
        "template_coordinate_system": "bottom_right",
        "template_coordinate_width": f"{coordinate_width:.12g}",
        "template_sections": json.dumps(_parser_data(parser), ensure_ascii=False),
    })
    root = ZimaEntity(path.stem, EntityKind.PART, combine_mode=CombineMode.NONE)
    container = ZimaEntity(
        "Frame" if template_type == "drawing_format" else "Title block",
        EntityKind.CONTAINER,
        combine_mode=CombineMode.NONE,
        parameters={"container_type": "SKETCH"},
    )
    sketch = ZimaEntity(
        "Frame geometry" if template_type == "drawing_format" else "Title-block geometry",
        EntityKind.SKETCH,
        combine_mode=CombineMode.NONE,
        parameters={
            "plane": "xy",
            "profile": "entities",
            "sketch_data": json.dumps(model.to_dict(), ensure_ascii=False),
            "external_references": "[]",
            "unit": "mm",
            "role": SketchRole.PROFILE.value,
            "template_editor": "true",
        },
    )
    container.add_child(sketch)
    root.add_child(container)
    document = PartDocument(document_settings=settings, root=root)
    document.source_file_path = path.resolve()
    document.regeneration_required = False
    return document


def create_empty_drawing_template(
    template_type: str,
    name: str,
) -> PartDocument:
    """Create a blank editable frame or title-block template."""
    if template_type == "drawing_format":
        coordinate_width = 297.0
        sections = {
            "Format": {
                "SchemaVersion": "3",
                "Name": name,
                "SheetFormat": "A4",
                "Orientation": "portrait",
                "DocumentType": "any",
            },
            "Frame": {
                "LeftMargin": "20",
                "RightMargin": "10",
                "TopMargin": "10",
                "BottomMargin": "10",
                "Color": "#FFFFFF",
                "LineWidth": "0.7",
            },
            "TitleBlock": {"Enabled": "no"},
        }
    elif template_type == "title_block":
        coordinate_width = 180.0
        sections = {
            "TitleBlock": {
                "SchemaVersion": "3",
                "Name": name,
                "Width": "180",
                "Height": "60",
                "Anchor": "bottom-right",
            },
        }
    else:
        raise ValueError(f"Unsupported drawing template type: {template_type}")
    settings = default_document_settings()
    settings.update({
        "type": template_type,
        "template_coordinate_system": "bottom_right",
        "template_coordinate_width": f"{coordinate_width:.12g}",
        "template_sections": json.dumps(sections, ensure_ascii=False),
    })
    root = ZimaEntity(name, EntityKind.PART, combine_mode=CombineMode.NONE)
    container = ZimaEntity(
        "Frame" if template_type == "drawing_format" else "Title block",
        EntityKind.CONTAINER,
        combine_mode=CombineMode.NONE,
        parameters={"container_type": "SKETCH"},
    )
    model = SketchModel()
    sketch = ZimaEntity(
        "Frame geometry" if template_type == "drawing_format"
        else "Title-block geometry",
        EntityKind.SKETCH,
        combine_mode=CombineMode.NONE,
        parameters={
            "plane": "xy",
            "profile": "entities",
            "sketch_data": json.dumps(model.to_dict(), ensure_ascii=False),
            "external_references": "[]",
            "unit": "mm",
            "role": SketchRole.PROFILE.value,
            "template_editor": "true",
        },
    )
    container.add_child(sketch)
    root.add_child(container)
    document = PartDocument(document_settings=settings, root=root)
    document.regeneration_required = False
    return document


def template_sketch(document: PartDocument) -> ZimaEntity:
    sketch = next((
        child
        for container in document.history_objects()
        for child in container.children
        if child.kind == EntityKind.SKETCH
    ), None)
    if sketch is None:
        raise ValueError("Drawing template has no editable sketch.")
    return sketch


def _native_geometry(model: SketchModel) -> list[tuple[str, str]]:
    entities, _dimensions = model.to_editor_data()
    points = {
        str(item["id"]): item for item in entities if item.get("type") == "point"
    }
    result: list[tuple[str, str]] = []
    counts = {"Line": 0, "Circle": 0, "Text": 0}

    def number(value: Any) -> str:
        return f"{float(value):.12g}"

    def add_polyline(polyline: list[tuple[float, float]], pen: str) -> None:
        for first, second in zip(polyline, polyline[1:]):
            counts["Line"] += 1
            result.append((f"Line{counts['Line']:03d}", ", ".join(map(str, (
                number(first[0]), number(first[1]),
                number(second[0]), number(second[1]), pen,
            )))))

    for item in entities:
        if item.get("repeat_region_id"):
            continue
        if item.get("text_role") in {"outline", "outline_point"}:
            continue
        if item.get("template_field_id"):
            continue
        if item.get("type") == "point" and item.get("text_role") == "anchor":
            counts["Text"] += 1
            pen = str(item.get("pen", item.get("text_color", "GREEN"))).upper()
            pen = pen if pen in TEMPLATE_PENS else "GREEN"
            value = str(item.get("text_value", "")).replace(",", " ")
            result.append((f"Text{counts['Text']:03d}", ", ".join((
                value, number(item.get("x", 0.0)), number(item.get("y", 0.0)),
                number(item.get("text_height", 2.5)), pen,
                str(item.get("text_horizontal", "left")).upper(),
            ))))
            continue
        kind = item.get("type")
        pen = str(item.get("pen", "GREEN")).upper()
        if pen not in TEMPLATE_PENS:
            raise ValueError(f"Unsupported drawing pen: {pen}")
        point_ids = list(item.get("point_ids", ()))
        if kind in ("segment", "construction") and len(point_ids) == 2:
            first, second = points[point_ids[0]], points[point_ids[1]]
            counts["Line"] += 1
            result.append((f"Line{counts['Line']:03d}", ", ".join((
                number(first["x"]), number(first["y"]),
                number(second["x"]), number(second["y"]), pen,
            ))))
        elif kind == "circle" and len(point_ids) == 1:
            centre = points[point_ids[0]]
            counts["Circle"] += 1
            result.append((f"Circle{counts['Circle']:03d}", ", ".join((
                number(centre["x"]), number(centre["y"]),
                number(item.get("radius", 0.0)), pen,
            ))))
        elif kind in ("arc", "ellipse", "elliptical_arc", "spline"):
            raw = [
                (float(points[point_id]["x"]), float(points[point_id]["y"]))
                for point_id in point_ids if point_id in points
            ]
            sampled: list[tuple[float, float]]
            if kind == "arc" and len(raw) >= 3:
                sampled = list(center_arc_points(
                    raw[0], raw[1], raw[2],
                    clockwise=bool(item.get("clockwise", False)),
                ))
            elif kind == "ellipse" and len(raw) >= 3:
                sampled = list(ellipse_points(raw[0], raw[1], raw[2]))
            elif kind == "elliptical_arc" and len(raw) >= 5:
                sampled = list(elliptical_arc_points(
                    raw[0], raw[1], raw[2], raw[3], raw[4],
                    clockwise=bool(item.get("clockwise", False)),
                ))
            else:
                # The canonical spline remains in [Sketch].  The native
                # compatibility layer uses its control polygon so drawings
                # never silently omit it when rendered by the lightweight
                # format canvas.
                sampled = raw
            add_polyline(sampled, pen)
    return result


def _serialize(document: PartDocument) -> str:
    template_type = str(document.document_settings.get("type", ""))
    if template_type not in TEMPLATE_TYPES.values():
        raise ValueError("Document is not a drawing template.")
    raw_sections = json.loads(str(document.document_settings["template_sections"]))
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    for section, values in raw_sections.items():
        parser[section] = {str(key): str(value) for key, value in values.items()}
    header = "Format" if template_type == "drawing_format" else "TitleBlock"
    parser[header]["SchemaVersion"] = "3"
    model = SketchModel.from_dict(json.loads(str(template_sketch(document).parameters["sketch_data"])))
    if template_type == "title_block":
        _store_title_block_fields(parser, model)
        _store_repeat_regions(parser, model)
    parser["Sketch"] = {"Data": json.dumps(model.to_dict(), ensure_ascii=False, separators=(",", ":"))}
    geometry_section = "FrameGeometry" if template_type == "drawing_format" else "Geometry"
    parser[geometry_section] = dict(_native_geometry(model))
    buffer = io.StringIO()
    parser.write(buffer)
    return buffer.getvalue().rstrip() + "\n"


def _add_repeat_region_helpers(
    model: SketchModel,
    parser: configparser.ConfigParser,
) -> None:
    """Expose persisted repeat regions as non-printing sketch helpers."""
    next_point = 1
    next_geometry = 1

    def point_id() -> str:
        nonlocal next_point
        while f"p{next_point}" in model.points:
            next_point += 1
        result = f"p{next_point}"
        next_point += 1
        return result

    def geometry_id() -> str:
        nonlocal next_geometry
        while f"g{next_geometry}" in model.geometry:
            next_geometry += 1
        result = f"g{next_geometry}"
        next_geometry += 1
        return result

    for section in parser.sections():
        if not section.startswith("RepeatRegion."):
            continue
        region_id = section.removeprefix("RepeatRegion.").strip() or "Region"
        if any(
            str(geometry.attributes.get("repeat_region_id", "")) == region_id
            for geometry in model.geometry.values()
        ):
            continue
        x = parser.getfloat(section, "X")
        y = parser.getfloat(section, "Y")
        width = parser.getfloat(section, "Width")
        height = parser.getfloat(section, "Height")
        corners = ((x, y), (x + width, y), (x + width, y + height), (x, y + height))
        point_ids = []
        for px, py in corners:
            existing = next((
                point.point_id for point in model.points.values()
                if abs(point.x - px) <= 1.0e-9 and abs(point.y - py) <= 1.0e-9
            ), None)
            if existing is None:
                existing = point_id()
                model.add_point(SketchPoint(existing, px, py, construction=True))
            point_ids.append(existing)
        for index in range(4):
            helper_id = geometry_id()
            model.add_geometry(SketchGeometry(
                helper_id,
                GeometryType.SEGMENT,
                (point_ids[index], point_ids[(index + 1) % 4]),
                attributes={
                    "repeat_region_id": region_id,
                    "repeat_region_kind": parser.get(section, "Kind", fallback="bom"),
                    "repeat_region_direction": parser.get(section, "Direction", fallback="up"),
                    "repeat_region_step": parser.getfloat(section, "Step", fallback=height),
                    "role": "repeat_region",
                },
            ))


def _store_repeat_regions(
    parser: configparser.ConfigParser,
    model: SketchModel,
) -> None:
    groups: dict[str, list[SketchGeometry]] = {}
    for geometry in model.geometry.values():
        region_id = str(geometry.attributes.get("repeat_region_id", ""))
        if region_id:
            groups.setdefault(region_id, []).append(geometry)
    for section in tuple(parser.sections()):
        if section.startswith("RepeatRegion."):
            parser.remove_section(section)
    for region_id, geometry in groups.items():
        point_ids = {
            point_id for item in geometry for point_id in item.point_ids
            if point_id in model.points
        }
        points = [model.points[point_id] for point_id in point_ids]
        if not points:
            continue
        sample = geometry[0].attributes
        x0, x1 = min(point.x for point in points), max(point.x for point in points)
        y0, y1 = min(point.y for point in points), max(point.y for point in points)
        parser[f"RepeatRegion.{region_id}"] = {
            "Kind": str(sample.get("repeat_region_kind", "bom")),
            "X": f"{x0:.12g}",
            "Y": f"{y0:.12g}",
            "Width": f"{x1 - x0:.12g}",
            "Height": f"{y1 - y0:.12g}",
            "Direction": str(sample.get("repeat_region_direction", "up")),
            "Step": f"{float(sample.get('repeat_region_step', y1 - y0)):.12g}",
        }


def _store_title_block_fields(
    parser: configparser.ConfigParser,
    model: SketchModel,
) -> None:
    stored_field_ids = {
        str(point.attributes.get("template_field_id", ""))
        for point in model.points.values()
        if str(point.attributes.get("template_field_id", ""))
    }
    # The declarative Field.* sections are authoritative on load: every
    # section missing from [Sketch] is recreated as a field anchor.  Saving
    # an editor deletion must therefore remove both representations, or the
    # supposedly deleted text (for example VERSION) returns on reopen.
    for section in tuple(parser.sections()):
        if not section.startswith("Field."):
            continue
        field_id = section.removeprefix("Field.").strip()
        if field_id and field_id not in stored_field_ids:
            parser.remove_section(section)
    for point in model.points.values():
        field_id = str(point.attributes.get("template_field_id", ""))
        if not field_id:
            continue
        section = f"Field.{field_id}"
        if not parser.has_section(section):
            parser.add_section(section)
        values = parser[section]
        align = str(point.attributes.get("text_horizontal", "left"))
        vertical = str(point.attributes.get("text_vertical", "baseline"))
        width = float(point.attributes.get("template_field_box_width", 0.0))
        height = float(point.attributes.get("template_field_box_height", 0.0))
        values["X"] = f"{point.x - (width if align == 'left' else width * 0.5 if align == 'center' else 0.0):.12g}"
        values["Y"] = f"{point.y - (height if vertical == 'top' else height * 0.5 if vertical == 'center' else 0.0):.12g}"
        values["Height"] = f"{float(point.attributes.get('text_height', 2.5)):.12g}"
        values["Align"] = align
        values["VerticalAlign"] = vertical
        values["Pen"] = str(point.attributes.get("pen", "GREEN")).upper()
        text = str(point.attributes.get("text_value", "")).strip()
        if text:
            values["Text"] = text
            values.pop("Parameter", None)
            values.pop("Source", None)


def save_drawing_template(document: PartDocument, path: Path) -> None:
    suffix = Path(path).suffix.lower()
    expected = {
        "drawing_format": ".frmz",
        "title_block": ".tblz",
    }.get(str(document.document_settings.get("type", "")))
    if suffix != expected:
        raise ValueError(f"Drawing template must use {expected}.")
    template_type = str(document.document_settings.get("type", ""))
    validator = lambda candidate: load_drawing_template(
        candidate, template_type=template_type
    )
    write_text_versioned(Path(path), _serialize(document), validator=validator)
    document.source_file_path = Path(path).resolve()
