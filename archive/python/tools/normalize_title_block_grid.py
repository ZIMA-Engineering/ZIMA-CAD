from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from zima_cad.drawing_template import (
    load_drawing_template,
    save_drawing_template,
    template_sketch,
)
from zima_cad.sketch_model import (
    GeometryType,
    SketchConstraint,
    SketchDimension,
    SketchGeometry,
    SketchModel,
    SketchPoint,
)


TOLERANCE = 1.0e-6


def numeric_id(value: str) -> int:
    digits = "".join(character for character in value if character.isdigit())
    return int(digits or 0)


def coordinate_key(point: tuple[float, float]) -> tuple[int, int]:
    return (
        round(point[0] / TOLERANCE),
        round(point[1] / TOLERANCE),
    )


def on_segment(
    point: tuple[float, float],
    first: tuple[float, float],
    second: tuple[float, float],
) -> bool:
    dx, dy = second[0] - first[0], second[1] - first[1]
    length = math.hypot(dx, dy)
    if length <= TOLERANCE:
        return False
    cross = abs(
        (point[0] - first[0]) * dy - (point[1] - first[1]) * dx
    ) / length
    if cross > TOLERANCE:
        return False
    dot = (
        (point[0] - first[0]) * dx + (point[1] - first[1]) * dy
    )
    return -TOLERANCE <= dot <= dx * dx + dy * dy + TOLERANCE


def segment_intersections(
    first: tuple[tuple[float, float], tuple[float, float]],
    second: tuple[tuple[float, float], tuple[float, float]],
) -> set[tuple[float, float]]:
    a, b = first
    c, d = second
    ab = (b[0] - a[0], b[1] - a[1])
    cd = (d[0] - c[0], d[1] - c[1])
    denominator = ab[0] * cd[1] - ab[1] * cd[0]
    if abs(denominator) <= TOLERANCE:
        return {
            point
            for point in (a, b, c, d)
            if on_segment(point, a, b) and on_segment(point, c, d)
        }
    offset = (c[0] - a[0], c[1] - a[1])
    first_factor = (
        offset[0] * cd[1] - offset[1] * cd[0]
    ) / denominator
    second_factor = (
        offset[0] * ab[1] - offset[1] * ab[0]
    ) / denominator
    if not (
        -TOLERANCE <= first_factor <= 1.0 + TOLERANCE
        and -TOLERANCE <= second_factor <= 1.0 + TOLERANCE
    ):
        return set()
    return {(
        a[0] + first_factor * ab[0],
        a[1] + first_factor * ab[1],
    )}


def normalized_segment(
    first: tuple[float, float],
    second: tuple[float, float],
) -> tuple[tuple[float, float], tuple[float, float]]:
    if abs(second[0] - first[0]) <= TOLERANCE:
        x = round(((first[0] + second[0]) * 0.5) / TOLERANCE) * TOLERANCE
        return (x, first[1]), (x, second[1])
    if abs(second[1] - first[1]) <= TOLERANCE:
        y = round(((first[1] + second[1]) * 0.5) / TOLERANCE) * TOLERANCE
        return (first[0], y), (second[0], y)
    return first, second


def normalize(path: Path, first_grid_geometry: int) -> tuple[int, int, int]:
    document = load_drawing_template(path, template_type="title_block")
    sketch = template_sketch(document)
    model = SketchModel.from_dict(
        json.loads(str(sketch.parameters["sketch_data"]))
    )
    grid_geometry = {
        geometry_id: geometry
        for geometry_id, geometry in model.geometry.items()
        if geometry.geometry_type == GeometryType.SEGMENT
        and numeric_id(geometry_id) >= first_grid_geometry
    }
    original_grid_point_ids = {
        point_id
        for geometry in grid_geometry.values()
        for point_id in geometry.point_ids
    }
    raw_segments = {
        geometry_id: normalized_segment(
            model.points[geometry.point_ids[0]].position(),
            model.points[geometry.point_ids[1]].position(),
        )
        for geometry_id, geometry in grid_geometry.items()
        if len(geometry.point_ids) == 2
    }
    split_positions: dict[str, set[tuple[float, float]]] = {
        geometry_id: {first, second}
        for geometry_id, (first, second) in raw_segments.items()
        if math.dist(first, second) > TOLERANCE
    }
    segment_items = list(raw_segments.items())
    for index, (first_id, first_segment) in enumerate(segment_items):
        if first_id not in split_positions:
            continue
        for second_id, second_segment in segment_items[index + 1:]:
            if second_id not in split_positions:
                continue
            intersections = segment_intersections(
                first_segment,
                second_segment,
            )
            split_positions[first_id].update(intersections)
            split_positions[second_id].update(intersections)

    existing_by_coordinate: dict[tuple[int, int], list[str]] = {}
    for point_id in original_grid_point_ids:
        existing_by_coordinate.setdefault(
            coordinate_key(model.points[point_id].position()), []
        ).append(point_id)
    anchor_position = (
        max(position[0] for segment in raw_segments.values() for position in segment),
        min(position[1] for segment in raw_segments.values() for position in segment),
    )
    anchor_key = coordinate_key(anchor_position)
    canonical_by_coordinate: dict[tuple[int, int], str] = {}
    next_point_index = max(map(numeric_id, model.points), default=0) + 1

    def point_id_for(position: tuple[float, float]) -> str:
        nonlocal next_point_index
        key = coordinate_key(position)
        if key in canonical_by_coordinate:
            return canonical_by_coordinate[key]
        existing = sorted(
            existing_by_coordinate.get(key, ()),
            key=lambda point_id: (
                0 if key == anchor_key and point_id == "p92" else 1,
                numeric_id(point_id),
                point_id,
            ),
        )
        if existing:
            point_id = existing[0]
            point = model.points[point_id]
            point.x, point.y = position
        else:
            while f"p{next_point_index}" in model.points:
                next_point_index += 1
            point_id = f"p{next_point_index}"
            next_point_index += 1
            model.points[point_id] = SketchPoint(
                point_id, position[0], position[1]
            )
        canonical_by_coordinate[key] = point_id
        return point_id

    next_geometry_index = max(map(numeric_id, model.geometry), default=0) + 1
    replacement_geometry: dict[str, SketchGeometry] = {}
    for geometry_id, geometry in grid_geometry.items():
        segment = raw_segments.get(geometry_id)
        if segment is None or geometry_id not in split_positions:
            continue
        first, second = segment
        dx, dy = second[0] - first[0], second[1] - first[1]
        denominator = dx * dx + dy * dy
        ordered = sorted(
            split_positions[geometry_id],
            key=lambda point: (
                (point[0] - first[0]) * dx
                + (point[1] - first[1]) * dy
            ) / denominator,
        )
        pieces = [
            (piece_first, piece_second)
            for piece_first, piece_second in zip(ordered, ordered[1:])
            if math.dist(piece_first, piece_second) > TOLERANCE
        ]
        for piece_index, (piece_first, piece_second) in enumerate(pieces):
            if piece_index == 0:
                replacement_id = geometry_id
            else:
                while f"g{next_geometry_index}" in model.geometry:
                    next_geometry_index += 1
                replacement_id = f"g{next_geometry_index}"
                next_geometry_index += 1
            replacement_geometry[replacement_id] = SketchGeometry(
                replacement_id,
                GeometryType.SEGMENT,
                (
                    point_id_for(piece_first),
                    point_id_for(piece_second),
                ),
                dict(geometry.attributes),
            )
    for geometry_id in grid_geometry:
        model.geometry.pop(geometry_id, None)
    model.geometry.update(replacement_geometry)

    point_remap = {
        point_id: point_id_for(model.points[point_id].position())
        for point_id in original_grid_point_ids
    }
    kept_constraints: dict[str, SketchConstraint] = {}
    for constraint_id, constraint in model.constraints.items():
        if original_grid_point_ids.intersection(constraint.point_ids):
            continue
        kept_constraints[constraint_id] = constraint
    model.constraints = kept_constraints

    kept_dimensions: dict[str, SketchDimension] = {}
    for dimension_id, dimension in model.dimensions.items():
        remapped = tuple(
            point_remap.get(point_id, point_id)
            for point_id in dimension.point_ids
        )
        if dimension.point_ids and all(
            point_id in original_grid_point_ids
            for point_id in dimension.point_ids
        ):
            continue
        kept_dimensions[dimension_id] = SketchDimension(
            dimension_id,
            dimension.dimension_type,
            dimension.value,
            remapped,
            dimension.driving,
            dict(dimension.attributes),
        )
    model.dimensions = kept_dimensions

    used_points = {
        point_id
        for geometry in model.geometry.values()
        for point_id in geometry.point_ids
    } | {
        point_id
        for dimension in model.dimensions.values()
        for point_id in dimension.point_ids
    } | {
        point_id
        for constraint in model.constraints.values()
        for point_id in constraint.point_ids
    }
    for point_id in original_grid_point_ids:
        if point_id not in used_points and point_remap.get(point_id) != point_id:
            model.points.pop(point_id, None)

    next_constraint_index = max(
        map(numeric_id, model.constraints), default=0
    ) + 1
    for geometry in replacement_geometry.values():
        first = model.points[geometry.point_ids[0]]
        second = model.points[geometry.point_ids[1]]
        constraint_type = (
            "horizontal"
            if abs(second.y - first.y) <= TOLERANCE
            else "vertical"
            if abs(second.x - first.x) <= TOLERANCE
            else None
        )
        if constraint_type is None:
            continue
        constraint_id = f"c{next_constraint_index}"
        next_constraint_index += 1
        model.constraints[constraint_id] = SketchConstraint(
            constraint_id,
            constraint_type,
            geometry.point_ids,
            attributes={"owner_geometry_id": geometry.geometry_id},
        )

    anchor_id = point_id_for(anchor_position)
    model.dimensions[f"coordinate:{anchor_id}:x"] = SketchDimension(
        f"coordinate:{anchor_id}:x",
        "coordinate_x",
        anchor_position[0],
        (anchor_id,),
    )
    model.dimensions[f"coordinate:{anchor_id}:y"] = SketchDimension(
        f"coordinate:{anchor_id}:y",
        "coordinate_y",
        anchor_position[1],
        (anchor_id,),
    )
    grid_point_ids = {
        point_id
        for geometry in replacement_geometry.values()
        for point_id in geometry.point_ids
    }
    columns: dict[int, str] = {}
    rows: dict[int, str] = {}
    for point_id in sorted(grid_point_ids, key=numeric_id):
        point = model.points[point_id]
        columns.setdefault(round(point.x / TOLERANCE), point_id)
        rows.setdefault(round(point.y / TOLERANCE), point_id)
    next_dimension_index = max(
        map(numeric_id, model.dimensions), default=0
    ) + 1
    for key, point_id in sorted(columns.items()):
        if key == anchor_key[0]:
            continue
        point = model.points[point_id]
        dimension_id = f"grid{next_dimension_index}"
        next_dimension_index += 1
        model.dimensions[dimension_id] = SketchDimension(
            dimension_id,
            "distance_x",
            anchor_position[0] - point.x,
            (point_id, anchor_id),
            attributes={"locked": True},
        )
    for key, point_id in sorted(rows.items()):
        if key == anchor_key[1]:
            continue
        point = model.points[point_id]
        dimension_id = f"grid{next_dimension_index}"
        next_dimension_index += 1
        model.dimensions[dimension_id] = SketchDimension(
            dimension_id,
            "distance_y",
            point.y - anchor_position[1],
            (anchor_id, point_id),
            attributes={"locked": True},
        )

    model.validate()
    sketch.parameters["sketch_data"] = json.dumps(
        model.to_dict(), ensure_ascii=False
    )
    save_drawing_template(document, path)
    return (
        len(replacement_geometry),
        len(model.constraints),
        len(model.dimensions),
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--first-grid-geometry", type=int, default=23)
    arguments = parser.parse_args()
    counts = normalize(
        arguments.path.resolve(),
        arguments.first_grid_geometry,
    )
    print(
        f"grid_segments={counts[0]} constraints={counts[1]} "
        f"dimensions={counts[2]}"
    )


if __name__ == "__main__":
    main()
