from __future__ import annotations

import configparser
import copy
import json
import numpy as np
from fractions import Fraction
from math import acos, atan2, ceil, cos, degrees, floor, hypot, radians, sin, sqrt, tan
from pathlib import Path
from typing import Any
from uuid import uuid4

from zima_cad.animation import ANIMATION_DURATION_MS
from zima_cad.viewer_mesh import (
    ViewerMesh,
    X_AXIS_COLOR,
    Y_AXIS_COLOR,
    combine_viewer_meshes,
    edge_visible_in_display,
    silhouette_segments,
)
from zima_cad.viewer_data import ARROW_HALF_ANGLE_DEGREES

from PySide6.QtCore import (
    QEasingCurve,
    QPoint,
    QPointF,
    QRectF,
    QTimer,
    Qt,
    QVariantAnimation,
    Signal,
)
from PySide6.QtGui import (
    QColor,
    QFont,
    QFontMetricsF,
    QImage,
    QMouseEvent,
    QPainter,
    QPen,
    QPicture,
    QPolygonF,
    QTransform,
    QWheelEvent,
)
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMenu,
    QMessageBox,
    QPushButton,
    QTabBar,
    QVBoxLayout,
    QWidget,
)

from zima_cad.drawing_format import load_drawing_format
from zima_cad.drawing_style import drawing_font_family, load_drawing_style
from zima_cad.localization import tr
from zima_cad.model import PartDocument
from zima_cad.title_block import load_title_block, resolve_title_block_text
from zima_cad.viewer_mesh import triangulate_shape


SHEET_FORMATS: dict[str, tuple[float, float]] = {
    "A4": (297.0, 210.0),
    "A3": (420.0, 297.0),
    "A2": (594.0, 420.0),
    "A1": (841.0, 594.0),
    "A0": (1189.0, 841.0),
}


def cosmetic_pen(
    color: QColor,
    style: Qt.PenStyle = Qt.PenStyle.SolidLine,
) -> QPen:
    """Create a one-pixel workspace pen independent of drawing zoom."""
    pen = QPen(color, 1.0, style)
    pen.setCosmetic(True)
    return pen


def drawing_scale_text(scale: float) -> str:
    ratio = Fraction(max(float(scale), 1.0e-9)).limit_denominator(1000)
    return f"M{ratio.numerator}:{ratio.denominator}"


class UpwardComboBox(QComboBox):
    """Combo box whose popup is anchored above the field."""

    def showPopup(self) -> None:
        super().showPopup()

        def position_above() -> None:
            popup = self.view().window()
            anchor = self.mapToGlobal(QPoint(0, 0))
            popup.move(anchor.x(), anchor.y() - popup.height())

        # Qt determines the popup size during showPopup(), so reposition it
        # once that geometry has been applied.
        QTimer.singleShot(0, position_above)


def default_sheet(index: int = 1) -> dict:
    return {
        "id": str(uuid4()),
        "name": f"List {index}",
        "format": "A4",
        "default_scale_numerator": 1.0,
        "default_scale": 1.0,
        "orientation": "portrait",
        "projection_method": "first_angle",
        "views": [],
        "dimensions": [],
        "title_block_values": {},
    }


def drawing_sheets(document: PartDocument) -> list[dict]:
    try:
        sheets = json.loads(document.document_settings.get("drawing_sheets", "[]"))
    except (TypeError, ValueError, json.JSONDecodeError):
        sheets = []
    if not isinstance(sheets, list) or not sheets:
        sheets = [default_sheet()]
    for sheet in sheets:
        sheet_format = str(sheet.get("format", "A4"))
        sheet["orientation"] = (
            "portrait" if sheet_format == "A4" else "landscape"
        )
        sheet.setdefault("default_scale_numerator", 1.0)
        sheet.setdefault("default_scale", 1.0)
        sheet.setdefault("projection_method", "first_angle")
        sheet.setdefault("dimensions", [])
        sheet.setdefault("title_block_values", {})
    return sheets


def store_drawing_sheets(document: PartDocument, sheets: list[dict]) -> None:
    document.document_settings["drawing_sheets"] = json.dumps(
        sheets, ensure_ascii=False, separators=(",", ":")
    )


def projection_axes(
    orientation: str | dict,
) -> tuple[tuple[float, float, float], ...]:
    """Return the drawing-space horizontal, vertical and depth axes."""
    # Keep drawing projections aligned with the native viewer: +X appears on
    # the left in the front/top/isometric views.
    named_camera = orientation if isinstance(orientation, dict) else None
    orientation_name = str(
        named_camera.get("standard", "") if named_camera else orientation
    )
    camera_angles = {
        "isometric": (215.264, -45.0, 0.0),
        "default": (215.264, -45.0, 0.0),
        "front": (180.0, -90.0, 0.0),
        "back": (0.0, -90.0, 0.0),
        "left": (-90.0, -90.0, 0.0),
        "right": (90.0, -90.0, 0.0),
        "top": (180.0, 0.0, 0.0),
        "bottom": (180.0, 180.0, 0.0),
    }
    yaw_degrees, pitch_degrees, roll_degrees = (
        (
            float(named_camera.get("yaw_degrees", 215.264)),
            float(named_camera.get("pitch_degrees", -45.0)),
            float(named_camera.get("roll_degrees", 0.0)),
        )
        if named_camera and not orientation_name
        else camera_angles.get(orientation_name, camera_angles["front"])
    )
    yaw = radians(yaw_degrees)
    pitch = radians(pitch_degrees)
    roll = radians(roll_degrees)
    horizontal = (
        cos(roll) * cos(yaw) - sin(roll) * cos(pitch) * sin(yaw),
        -cos(roll) * sin(yaw) - sin(roll) * cos(pitch) * cos(yaw),
        sin(roll) * sin(pitch),
    )
    vertical = (
        sin(roll) * cos(yaw) + cos(roll) * cos(pitch) * sin(yaw),
        -sin(roll) * sin(yaw) + cos(roll) * cos(pitch) * cos(yaw),
        -cos(roll) * sin(pitch),
    )
    depth = (
        sin(pitch) * sin(yaw),
        sin(pitch) * cos(yaw),
        cos(pitch),
    )
    return horizontal, vertical, depth


def projected_view_orientation(
    parent_orientation: str | dict,
    placement_direction: str,
    projection_method: str,
) -> dict[str, float]:
    """Rotate a projected view relative to its parent drawing view."""

    horizontal, vertical, depth = projection_axes(parent_orientation)
    direction = str(placement_direction)
    direction_components = {
        "right": (1.0, 0.0),
        "top_right": (sqrt(0.5), sqrt(0.5)),
        "top": (0.0, 1.0),
        "top_left": (-sqrt(0.5), sqrt(0.5)),
        "left": (-1.0, 0.0),
        "bottom_left": (-sqrt(0.5), -sqrt(0.5)),
        "bottom": (0.0, -1.0),
        "bottom_right": (sqrt(0.5), -sqrt(0.5)),
    }
    right_component, top_component = direction_components.get(
        direction, direction_components["right"]
    )
    if projection_method != "third_angle":
        right_component = -right_component
        top_component = -top_component
    tangent = tuple(
        -top_component * horizontal[axis]
        + right_component * vertical[axis]
        for axis in range(3)
    )
    axes = (
        tuple(
            -right_component * depth[axis]
            - top_component * tangent[axis]
            for axis in range(3)
        ),
        tuple(
            -top_component * depth[axis]
            + right_component * tangent[axis]
            for axis in range(3)
        ),
        tuple(
            right_component * horizontal[axis]
            + top_component * vertical[axis]
            for axis in range(3)
        ),
    )

    pitch = degrees(acos(max(-1.0, min(1.0, axes[2][2]))))
    if abs(sin(radians(pitch))) <= 1.0e-9:
        yaw = degrees(atan2(-axes[0][1], axes[0][0]))
        roll = 0.0
    else:
        yaw = degrees(atan2(axes[2][0], axes[2][1]))
        roll = degrees(atan2(axes[0][2], -axes[1][2]))
    return {
        "yaw_degrees": yaw,
        "pitch_degrees": pitch,
        "roll_degrees": roll,
    }


def projection_placement_vector(direction: str) -> tuple[float, float]:
    """Return a unit sheet-space ray for an eight-way projected view."""
    diagonal = sqrt(0.5)
    return {
        "right": (-1.0, 0.0),
        "top_right": (-diagonal, diagonal),
        "top": (0.0, 1.0),
        "top_left": (diagonal, diagonal),
        "left": (1.0, 0.0),
        "bottom_left": (diagonal, -diagonal),
        "bottom": (0.0, -1.0),
        "bottom_right": (-diagonal, -diagonal),
    }.get(str(direction), (-1.0, 0.0))


def _center_projected_groups(
    *groups: list[list[list[float]]],
) -> tuple[list[list[list[float]]], ...]:
    points = [point for group in groups for line in group for point in line]
    if not points:
        return tuple([] for _group in groups)
    center_x = (min(point[0] for point in points) + max(point[0] for point in points)) * 0.5
    center_y = (min(point[1] for point in points) + max(point[1] for point in points)) * 0.5
    return tuple(
        [
            [[float(x) - center_x, float(y) - center_y] for x, y in line]
            for line in group
        ]
        for group in groups
    )


def _merge_projected_segments(
    segments: list[list[list[float]]],
) -> list[list[list[float]]]:
    """Remove zero/duplicate segments and stitch non-branching chains."""

    def key(point: list[float]) -> tuple[float, float]:
        return round(float(point[0]), 7), round(float(point[1]), 7)

    unique: list[list[list[float]]] = []
    seen = set()
    for segment in segments:
        if len(segment) < 2 or key(segment[0]) == key(segment[-1]):
            continue
        segment_key = tuple(sorted((key(segment[0]), key(segment[-1]))))
        if segment_key in seen:
            continue
        seen.add(segment_key)
        unique.append([list(segment[0]), list(segment[-1])])

    endpoint_counts: dict[tuple[float, float], int] = {}
    for segment in unique:
        for point in (segment[0], segment[-1]):
            point_key = key(point)
            endpoint_counts[point_key] = endpoint_counts.get(point_key, 0) + 1

    unused = set(range(len(unique)))
    chains: list[list[list[float]]] = []
    while unused:
        index = unused.pop()
        chain = list(unique[index])
        extended = True
        while extended:
            extended = False
            for at_start in (False, True):
                endpoint = chain[0] if at_start else chain[-1]
                endpoint_key = key(endpoint)
                if endpoint_counts.get(endpoint_key) != 2:
                    continue
                match = next((
                    candidate
                    for candidate in unused
                    if endpoint_key in {
                        key(unique[candidate][0]),
                        key(unique[candidate][-1]),
                    }
                ), None)
                if match is None:
                    continue
                unused.remove(match)
                segment = unique[match]
                other = (
                    segment[-1]
                    if key(segment[0]) == endpoint_key
                    else segment[0]
                )
                if at_start:
                    chain.insert(0, other)
                else:
                    chain.append(other)
                extended = True
                break
        chains.append(chain)
    return chains


def project_polylines(
    polylines: list[list[tuple[float, float, float]]],
    orientation: str | dict,
) -> list[list[list[float]]]:
    return _center_projected_groups(
        _project_polylines_raw(polylines, orientation)
    )[0]


def _project_polylines_raw(
    polylines: list[list[tuple[float, float, float]]],
    orientation: str | dict,
) -> list[list[list[float]]]:
    horizontal, vertical, _depth = projection_axes(orientation)
    result: list[list[list[float]]] = []
    for polyline in polylines:
        projected: list[list[float]] = []
        for x, y, z in polyline:
            u = horizontal[0] * x + horizontal[1] * y + horizontal[2] * z
            v = vertical[0] * x + vertical[1] * y + vertical[2] * z
            projected.append([float(u), float(v)])
        if len(projected) >= 2:
            result.append(projected)
    return result


def technical_projection(
    shapes: list[Any],
    wire_polylines: list[list[tuple[float, float, float]]],
    orientation: str | dict,
) -> dict[str, list[list[list[float]]]]:
    """Compatibility wrapper using our renderer mesh, never OCCT HLR."""

    meshes = [
        triangulate_shape(shape)
        for shape in shapes
        if shape is not None and not shape.IsNull()
    ]
    if meshes:
        return renderer_projection(meshes, orientation)
    wireframe = _center_projected_groups(
        _project_polylines_raw(wire_polylines, orientation)
    )[0]
    return {
        "polylines": wireframe,
        "hidden_polylines": [],
        "wireframe_polylines": wireframe,
        "auxiliary_polylines": [],
    }


def renderer_projection(
    meshes: list[ViewerMesh],
    orientation: str | dict,
) -> dict[str, list[list[list[float]]]]:
    """Project renderer-owned triangles and edges into vector drawing data."""

    mesh = combine_viewer_meshes(tuple(meshes))
    horizontal, vertical, depth_axis = projection_axes(orientation)

    def project(point) -> tuple[float, float, float]:
        return tuple(
            sum(axis[index] * point[index] for index in range(3))
            for axis in (horizontal, vertical, depth_axis)
        )

    triangles = []
    positions = mesh.triangle_positions
    for offset in range(0, len(positions), 9):
        points = tuple(project(tuple(
            positions[offset + vertex * 3 + index]
            for index in range(3)
        )) for vertex in range(3))
        triangles.append((
            points,
            min(point[0] for point in points),
            max(point[0] for point in points),
            min(point[1] for point in points),
            max(point[1] for point in points),
        ))

    diagonal = sqrt(sum(
        (mesh.bounds_max[index] - mesh.bounds_min[index]) ** 2
        for index in range(3)
    ))
    # Edge polylines share the surface triangulation nodes, so only a small
    # floating-point tolerance is required for coplanar depth comparisons.
    depth_tolerance = max(diagonal * 2.0e-5, 1.0e-7)

    def visible(point) -> bool:
        u, v, depth = project(point)
        front = None
        for triangle, min_u, max_u, min_v, max_v in triangles:
            if not (min_u <= u <= max_u and min_v <= v <= max_v):
                continue
            ax, ay = triangle[0][:2]
            bx, by = triangle[1][:2]
            cx, cy = triangle[2][:2]
            denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(denominator) <= 1.0e-12:
                continue
            wa = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / denominator
            wb = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / denominator
            wc = 1.0 - wa - wb
            if min(wa, wb, wc) < -1.0e-7:
                continue
            hit = sum(weight * vertex[2] for weight, vertex in zip(
                (wa, wb, wc), triangle
            ))
            front = hit if front is None else max(front, hit)
        return front is None or depth >= front - depth_tolerance

    topology_polylines = [
        edge.points
        for edge in mesh.edges
        if edge_visible_in_display(edge, "wire")
    ]
    silhouette_polylines = list(silhouette_segments(mesh, depth_axis))
    candidates = [*topology_polylines, *silhouette_polylines]
    # Preserve closed topology polylines (for example cylinder circles); the
    # chain merger intentionally operates only on two-point silhouette pieces.
    wireframe = [
        *_project_polylines_raw(topology_polylines, orientation),
        *_merge_projected_segments(
            _project_polylines_raw(silhouette_polylines, orientation)
        ),
    ]
    visible_lines: list[list[list[float]]] = []
    hidden_lines: list[list[list[float]]] = []
    sampling_step = max(diagonal / 160.0, 1.0e-7)

    for polyline in candidates:
        for first, second in zip(polyline, polyline[1:]):
            length = sqrt(sum(
                (second[index] - first[index]) ** 2 for index in range(3)
            ))
            count = max(1, min(256, int(length / sampling_step) + 1))
            states = []
            for sample in range(count):
                factor = (sample + 0.5) / count
                midpoint = tuple(
                    first[index]
                    + (second[index] - first[index]) * factor
                    for index in range(3)
                )
                states.append(visible(midpoint))
            start = 0
            while start < count:
                state = states[start]
                end = start + 1
                while end < count and states[end] == state:
                    end += 1
                segment = []
                for sample in (start, end):
                    factor = sample / count
                    point = tuple(
                        first[index]
                        + (second[index] - first[index]) * factor
                        for index in range(3)
                    )
                    u, v, _depth = project(point)
                    segment.append([u, v])
                (visible_lines if state else hidden_lines).append(segment)
                start = end

    auxiliary = _project_polylines_raw([
        edge.points
        for edge in mesh.edges
        if edge.topology_role in {"tangent", "periodic_tangent"}
    ], orientation)
    # A silhouette and a real topology edge can project onto the same line
    # while receiving opposite visibility classifications.  A hidden copy
    # must never be dashed underneath the visible edge.
    projected_tolerance = max(diagonal * 2.0e-5, 1.0e-7)
    visible_segments = [
        (line[index], line[index + 1])
        for line in visible_lines
        for index in range(len(line) - 1)
    ]

    def covered_by_visible(point: list[float]) -> bool:
        for first, second in visible_segments:
            dx = second[0] - first[0]
            dy = second[1] - first[1]
            length_squared = dx * dx + dy * dy
            if length_squared <= 1.0e-20:
                continue
            fraction = max(0.0, min(1.0, (
                (point[0] - first[0]) * dx + (point[1] - first[1]) * dy
            ) / length_squared))
            nearest_x = first[0] + fraction * dx
            nearest_y = first[1] + fraction * dy
            if hypot(point[0] - nearest_x, point[1] - nearest_y) <= projected_tolerance:
                return True
        return False

    hidden_lines = [
        line for line in hidden_lines
        if not all(covered_by_visible(point) for point in (
            line[0],
            [
                (line[0][0] + line[-1][0]) * 0.5,
                (line[0][1] + line[-1][1]) * 0.5,
            ],
            line[-1],
        ))
    ]
    visible_lines = _merge_projected_segments(visible_lines)
    hidden_lines = _merge_projected_segments(hidden_lines)
    center_points = [
        point
        for group in (visible_lines, hidden_lines, wireframe, auxiliary)
        for line in group
        for point in line
    ]
    projection_center = (
        [
            (min(point[0] for point in center_points)
             + max(point[0] for point in center_points)) * 0.5,
            (min(point[1] for point in center_points)
             + max(point[1] for point in center_points)) * 0.5,
        ]
        if center_points else [0.0, 0.0]
    )
    visible_lines, hidden_lines, wireframe, auxiliary = _center_projected_groups(
        visible_lines, hidden_lines, wireframe, auxiliary
    )
    return {
        "polylines": visible_lines or wireframe,
        "hidden_polylines": hidden_lines,
        "wireframe_polylines": wireframe,
        "auxiliary_polylines": auxiliary,
        "projection_center": projection_center,
    }


def shaded_projection(
    meshes: list[tuple[ViewerMesh, str | dict[str, str]]],
    orientation: str | dict,
) -> list[dict[str, Any]]:
    """Project model triangles for shaded technical drawing views."""
    horizontal, vertical, depth_axis = projection_axes(orientation)
    light = (0.25, -0.35, 0.902)
    light_length = sqrt(sum(component * component for component in light))
    light = tuple(component / light_length for component in light)
    records: list[dict[str, Any]] = []
    all_points: list[list[float]] = []
    for mesh, colors in meshes:
        positions = mesh.triangle_positions
        normals = mesh.triangle_normals
        for triangle_index, offset in enumerate(range(0, len(positions), 9)):
            polygon: list[list[float]] = []
            depths: list[float] = []
            vertex_brightness: list[float] = []
            for vertex in range(3):
                point = tuple(positions[offset + vertex * 3 + axis] for axis in range(3))
                polygon.append([
                    sum(horizontal[axis] * point[axis] for axis in range(3)),
                    sum(vertical[axis] * point[axis] for axis in range(3)),
                ])
                depths.append(sum(depth_axis[axis] * point[axis] for axis in range(3)))
                normal = tuple(
                    normals[offset + vertex * 3 + axis] for axis in range(3)
                )
                camera_normal = (
                    sum(horizontal[axis] * normal[axis] for axis in range(3)),
                    sum(vertical[axis] * normal[axis] for axis in range(3)),
                    sum(depth_axis[axis] * normal[axis] for axis in range(3)),
                )
                normal_length = sqrt(sum(value * value for value in camera_normal))
                if normal_length > 1.0e-12:
                    camera_normal = tuple(
                        value / normal_length for value in camera_normal
                    )
                diffuse = max(0.0, sum(
                    camera_normal[axis] * light[axis] for axis in range(3)
                ))
                vertex_brightness.append(0.42 + 0.58 * diffuse)
            records.append({
                "points": polygon,
                "depth": sum(depths) / 3.0,
                "vertex_depths": depths,
                "color": (
                    colors.get(
                        mesh.triangle_owner_ids[triangle_index], "#B9C2CC"
                    )
                    if isinstance(colors, dict)
                    else colors
                ),
                "brightness": sum(vertex_brightness) / 3.0,
                "vertex_brightness": vertex_brightness,
            })
            all_points.extend(polygon)
    if not all_points:
        return records
    center_x = (min(p[0] for p in all_points) + max(p[0] for p in all_points)) * 0.5
    center_y = (min(p[1] for p in all_points) + max(p[1] for p in all_points)) * 0.5
    for record in records:
        record["points"] = [[p[0] - center_x, p[1] - center_y] for p in record["points"]]
    records.sort(key=lambda record: float(record["depth"]))
    return records


def model_visible_projection(
    meshes: list[ViewerMesh],
    orientation: str | dict,
) -> list[list[list[float]]]:
    """Project the same depth-tested visible edges used by the 3D model."""
    mesh = combine_viewer_meshes(tuple(meshes))
    horizontal, vertical, depth_axis = projection_axes(orientation)

    def project(point):
        return (
            sum(horizontal[i] * point[i] for i in range(3)),
            sum(vertical[i] * point[i] for i in range(3)),
            sum(depth_axis[i] * point[i] for i in range(3)),
        )

    triangles = []
    p = mesh.triangle_positions
    for offset in range(0, len(p), 9):
        points = tuple(project(tuple(p[offset + v * 3 + i] for i in range(3))) for v in range(3))
        xs, ys = [x[0] for x in points], [x[1] for x in points]
        triangles.append((points, min(xs), max(xs), min(ys), max(ys)))

    def visible(point) -> bool:
        u, v, depth = project(point)
        front = None
        for tri, min_x, max_x, min_y, max_y in triangles:
            if not (min_x <= u <= max_x and min_y <= v <= max_y):
                continue
            ax, ay = tri[0][0], tri[0][1]
            bx, by = tri[1][0], tri[1][1]
            cx, cy = tri[2][0], tri[2][1]
            denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(denominator) <= 1e-12:
                continue
            wa = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / denominator
            wb = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / denominator
            wc = 1.0 - wa - wb
            if min(wa, wb, wc) < -1e-7:
                continue
            hit = wa * tri[0][2] + wb * tri[1][2] + wc * tri[2][2]
            front = hit if front is None else max(front, hit)
        diagonal = sqrt(sum((mesh.bounds_max[i] - mesh.bounds_min[i]) ** 2 for i in range(3)))
        return front is None or depth >= front - max(diagonal * 1e-5, 1e-7)

    candidates = [
        edge.points for edge in mesh.edges
        if edge_visible_in_display(edge, "wire")
    ]
    candidates.extend(silhouette_segments(mesh, depth_axis))
    lines = []
    for polyline in candidates:
        for first, second in zip(polyline, polyline[1:]):
            midpoint = tuple((first[i] + second[i]) * 0.5 for i in range(3))
            if visible(midpoint):
                a, b = project(first), project(second)
                lines.append([[a[0], a[1]], [b[0], b[1]]])
    return _center_projected_groups(lines)[0]


def update_view_bounds(view: dict) -> dict:
    """Cache the axis-aligned geometry bounds in sheet coordinates."""
    border_margin = 1.0
    scale = float(view.get("scale", 1.0))
    center_x = float(view.get("x", 0.0))
    center_y = float(view.get("y", 0.0))
    extent = view.get("model_extent")
    if isinstance(extent, (list, tuple)) and len(extent) == 2:
        half_width = max(0.0, float(extent[0])) * scale * 0.5 + border_margin
        half_height = max(0.0, float(extent[1])) * scale * 0.5 + border_margin
        bounds = {
            "left": center_x - half_width,
            "right": center_x + half_width,
            "bottom": center_y - half_height,
            "top": center_y + half_height,
        }
    else:
        bounds = {"left": center_x, "right": center_x, "bottom": center_y, "top": center_y}
    view["bounds"] = bounds
    return bounds


def mesh_projection_extent(
    mesh: ViewerMesh,
    orientation: str | dict,
) -> list[float]:
    horizontal, vertical, _depth = projection_axes(orientation)
    points = [
        tuple(mesh.triangle_positions[offset + axis] for axis in range(3))
        for offset in range(0, len(mesh.triangle_positions), 3)
    ]
    if not points:
        points = [point for edge in mesh.edges for point in edge.points]
    if not points:
        return [0.0, 0.0]
    projected = [
        (
            sum(horizontal[axis] * point[axis] for axis in range(3)),
            sum(vertical[axis] * point[axis] for axis in range(3)),
        )
        for point in points
    ]
    return [
        max(point[0] for point in projected) - min(point[0] for point in projected),
        max(point[1] for point in projected) - min(point[1] for point in projected),
    ]


def delete_drawing_view(sheet: dict, view_id: str) -> set[str]:
    """Delete a view, its projected descendants and dependent dimensions."""
    removed = {str(view_id)}
    views = list(sheet.get("views", []))
    changed = True
    while changed:
        changed = False
        for view in views:
            candidate_id = str(view.get("id", ""))
            if (
                candidate_id not in removed
                and str(view.get("parent_view_id", "")) in removed
            ):
                removed.add(candidate_id)
                changed = True
    sheet["views"] = [
        view for view in views
        if str(view.get("id", "")) not in removed
    ]

    def dimension_uses_removed_view(dimension: dict) -> bool:
        return any(
            isinstance(reference, dict)
            and str(reference.get("view_id", "")) in removed
            for reference in (
                dimension.get("first"), dimension.get("second")
            )
        )

    sheet["dimensions"] = [
        dimension for dimension in sheet.get("dimensions", [])
        if not dimension_uses_removed_view(dimension)
    ]
    return removed


def move_drawing_view(
    sheet: dict,
    view_id: str,
    x: float,
    y: float,
) -> bool:
    """Move a view within its projection DOF and carry its descendants."""
    views = list(sheet.get("views", []))
    by_id = {
        str(candidate.get("id", "")): candidate for candidate in views
    }
    view = by_id.get(str(view_id))
    if view is None:
        return False

    target_x, target_y = float(x), float(y)
    parent = by_id.get(str(view.get("parent_view_id", "")))
    if parent is not None:
        direction = str(view.get("projection_direction", ""))
        valid_directions = {
            "right", "top_right", "top", "top_left",
            "left", "bottom_left", "bottom", "bottom_right",
        }
        if direction not in valid_directions:
            dx = float(view.get("x", 0.0)) - float(parent.get("x", 0.0))
            dy = float(view.get("y", 0.0)) - float(parent.get("y", 0.0))
            direction = (
                "right" if abs(dx) >= abs(dy) and dx < 0.0
                else "left" if abs(dx) >= abs(dy)
                else "top" if dy > 0.0
                else "bottom"
            )
            view["projection_direction"] = direction
        ray_x, ray_y = projection_placement_vector(direction)
        parent_x = float(parent.get("x", 0.0))
        parent_y = float(parent.get("y", 0.0))
        distance = (
            (target_x - parent_x) * ray_x
            + (target_y - parent_y) * ray_y
        )
        target_x = parent_x + distance * ray_x
        target_y = parent_y + distance * ray_y

    delta_x = target_x - float(view.get("x", 0.0))
    delta_y = target_y - float(view.get("y", 0.0))
    moved_ids = {str(view_id)}
    changed = True
    while changed:
        changed = False
        for candidate in views:
            candidate_id = str(candidate.get("id", ""))
            if (
                candidate_id not in moved_ids
                and str(candidate.get("parent_view_id", "")) in moved_ids
            ):
                moved_ids.add(candidate_id)
                changed = True
    for moved_id in moved_ids:
        moved = by_id.get(moved_id)
        if moved is None:
            continue
        moved["x"] = float(moved.get("x", 0.0)) + delta_x
        moved["y"] = float(moved.get("y", 0.0)) + delta_y
        caption_position = moved.get("caption_position")
        if (
            isinstance(caption_position, list)
            and len(caption_position) == 2
        ):
            caption_position[0] = float(caption_position[0]) + delta_x
            caption_position[1] = float(caption_position[1]) + delta_y
        update_view_bounds(moved)

    for dimension in sheet.get("dimensions", []):
        references = (dimension.get("first"), dimension.get("second"))
        if not any(
            isinstance(reference, dict)
            and str(reference.get("view_id", "")) in moved_ids
            for reference in references
        ):
            continue
        placement = dimension.get("placement")
        if isinstance(placement, list) and len(placement) >= 2:
            dimension["placement"] = [
                float(placement[0]) + delta_x,
                float(placement[1]) + delta_y,
            ]
    return True


def parallel_dimension_geometry(
    first: tuple[tuple[float, float], tuple[float, float]],
    second: tuple[tuple[float, float], tuple[float, float]],
    placement: tuple[float, float],
) -> dict[str, Any] | None:
    """Construct a perpendicular distance dimension between parallel lines."""
    (ax, ay), (bx, by) = first
    (cx, cy), (dx, dy) = second
    first_length = hypot(bx - ax, by - ay)
    second_length = hypot(dx - cx, dy - cy)
    if first_length <= 1e-9 or second_length <= 1e-9:
        return None
    ux, uy = (bx - ax) / first_length, (by - ay) / first_length
    vx, vy = (dx - cx) / second_length, (dy - cy) / second_length
    if abs(ux * vy - uy * vx) > 1e-3:
        return None
    if ux * vx + uy * vy < 0.0:
        vx, vy = -vx, -vy
    ux, uy = (ux + vx) * 0.5, (uy + vy) * 0.5
    direction_length = hypot(ux, uy)
    ux, uy = ux / direction_length, uy / direction_length
    nx, ny = -uy, ux
    signed_distance = (cx - ax) * nx + (cy - ay) * ny
    if abs(signed_distance) <= 1e-9:
        return None
    tangent = placement[0] * ux + placement[1] * uy
    first_offset = tangent - (ax * ux + ay * uy)
    second_offset = tangent - (cx * ux + cy * uy)
    first_point = (ax + first_offset * ux, ay + first_offset * uy)
    second_point = (cx + second_offset * ux, cy + second_offset * uy)
    first_anchor = min(first, key=lambda point: hypot(
        point[0] - first_point[0], point[1] - first_point[1]
    ))
    second_anchor = min(second, key=lambda point: hypot(
        point[0] - second_point[0], point[1] - second_point[1]
    ))
    return {
        "first_point": first_point,
        "second_point": second_point,
        "first_anchor": first_anchor,
        "second_anchor": second_anchor,
        "direction": (nx, ny),
        "extension_direction": (ux, uy),
        "distance": abs(signed_distance),
    }


class DrawingCanvas(QWidget):
    placementRequested = Signal(float, float)
    viewSelected = Signal(str)
    viewDoubleClicked = Signal(str)
    insertViewRequested = Signal()
    projectViewRequested = Signal(str)
    dimensionCreated = Signal()
    dimensionToolCancelled = Signal()
    dimensionStatusChanged = Signal(str)
    viewDeleteRequested = Signal(str)
    viewMoveFinished = Signal()
    titleBlockFieldDoubleClicked = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMinimumSize(320, 240)
        self._sheet: dict = default_sheet()
        self._pixels_per_mm = 2.0
        self._pan = QPointF()
        self._panning = False
        self._last_mouse = QPoint()
        self._pending_view: dict | None = None
        self._cursor_sheet_position: tuple[float, float] | None = None
        self._fit_animation: QVariantAnimation | None = None
        self._hovered_view_id: str | None = None
        self._selected_view_id: str | None = None
        self._format_definition: dict | None = None
        self._format_picture: QPicture | None = None
        self._format_picture_key: tuple | None = None
        self._title_block_definition: dict | None = None
        self._title_block_context: dict = {}
        self._title_block_picture: QPicture | None = None
        self._title_block_picture_key: tuple | None = None
        self._title_block_field_screen_bounds: dict[str, QRectF] = {}
        self._hovered_title_block_field_id: str | None = None
        self._selected_title_block_field_id: str | None = None
        self._lineweight_preview = False
        self._dimension_tool_active = False
        self._dimension_references: list[dict] = []
        self._dimension_hover_reference: dict | None = None
        self._dimension_candidate_cycle: tuple[dict, ...] = ()
        self._dimension_candidate_index = -1
        self._dimension_cursor_sheet: tuple[float, float] | None = None
        self._dimension_middle_timer = QTimer(self)
        self._dimension_middle_timer.setSingleShot(True)
        self._dimension_middle_timer.timeout.connect(
            self._commit_preview_dimension
        )
        self._dragged_view_id: str | None = None
        self._drag_start_sheet: tuple[float, float] | None = None
        self._drag_view_start: tuple[float, float] | None = None
        self._caption_screen_bounds: dict[str, QRectF] = {}
        self._selected_caption_view_id: str | None = None
        self._dragged_caption_view_id: str | None = None
        self._drag_caption_start_sheet: tuple[float, float] | None = None
        self._drag_caption_position_start: tuple[float, float] | None = None
        self._view_render_data: dict[str, tuple[ViewerMesh, dict[str, str]]] = {}
        self._view_edge_reference_ids: dict[
            str, dict[tuple[str, int], str]
        ] = {}
        self._runtime_view_geometry: dict[str, dict[str, Any]] = {}
        self._shaded_image_cache: dict[str, tuple[tuple[float, float], QImage, QPointF]] = {}

    def set_view_render_data(
        self,
        view_id: str,
        mesh: ViewerMesh,
        colors: dict[str, str] | None = None,
        edge_reference_ids: dict[tuple[str, int], str] | None = None,
    ) -> None:
        view_id = str(view_id)
        self._view_render_data[view_id] = (mesh, dict(colors or {}))
        self._view_edge_reference_ids[view_id] = dict(
            edge_reference_ids or {}
        )
        view = self._view_by_id(view_id)
        if view is None and self._pending_view is not None:
            if str(self._pending_view.get("id", "")) == view_id:
                view = self._pending_view
        if view is not None:
            self._prepare_view_geometry(view_id, view, mesh, dict(colors or {}))
        self.update()

    def _prepare_view_geometry(
        self,
        view_id: str,
        view: dict,
        mesh: ViewerMesh,
        colors: dict[str, str] | None = None,
    ) -> None:
        orientation = view.get("orientation", "isometric")
        view["model_extent"] = mesh_projection_extent(mesh, orientation)
        update_view_bounds(view)
        geometry = renderer_projection([mesh], orientation)
        geometry["shaded_triangles"] = shaded_projection(
            [(mesh, dict(colors or {}))], orientation
        )
        self._runtime_view_geometry[str(view_id)] = geometry
        self._shaded_image_cache.pop(str(view_id), None)

    def copy_view_render_data(self, source_id: str, target_id: str) -> None:
        data = self._view_render_data.get(str(source_id))
        if data is not None:
            self.set_view_render_data(
                str(target_id),
                data[0],
                data[1],
                self._view_edge_reference_ids.get(str(source_id)),
            )

    def clear_view_render_data(self) -> None:
        self._view_render_data.clear()
        self._view_edge_reference_ids.clear()
        self._runtime_view_geometry.clear()
        self._shaded_image_cache.clear()
        self.update()

    def topology_at(
        self,
        position: QPointF,
    ) -> dict | None:
        """Pick real source-model topology through a drawing-view camera."""
        view = self._view_at(position)
        if view is None:
            return None
        view_id = str(view.get("id", ""))
        data = self._view_render_data.get(view_id)
        if data is None:
            return None
        mesh, _colors = data
        horizontal, vertical, _depth = projection_axes(
            view.get("orientation", "isometric")
        )
        scale = float(view.get("scale", 1.0))
        center_x = float(view.get("x", 0.0))
        center_y = float(view.get("y", 0.0))
        best: tuple[float, Any] | None = None
        for edge in mesh.edges:
            if edge.element_kind != "edge":
                continue
            projected = []
            for point in edge.points:
                u = sum(horizontal[axis] * point[axis] for axis in range(3))
                v = sum(vertical[axis] * point[axis] for axis in range(3))
                projected.append(self._screen_point(
                    center_x - u * scale,
                    center_y + v * scale,
                ))
            for index in range(len(projected) - 1):
                distance = self._point_segment_distance(
                    position, projected[index], projected[index + 1]
                )
                if distance <= 8.0 and (best is None or distance < best[0]):
                    best = distance, edge
        if best is None:
            return None
        edge = best[1]
        return {
            "view_id": view_id,
            "topology_kind": "edge",
            "owner_id": edge.owner_id,
            "edge_index": edge.edge_index,
        }

    def set_dimension_tool(self, active: bool) -> None:
        self._dimension_tool_active = bool(active)
        self._dimension_references = []
        self._dimension_hover_reference = None
        self._dimension_candidate_cycle = ()
        self._dimension_candidate_index = -1
        self._dimension_cursor_sheet = None
        if active:
            self.setCursor(Qt.CursorShape.ArrowCursor)
            self.setFocus()
            self.dimensionStatusChanged.emit(
                tr("drawing.dimension.status.select_first")
            )
        else:
            self._dimension_middle_timer.stop()
            self.unsetCursor()
            self.dimensionStatusChanged.emit("")
        self.update()

    def set_format_definition(self, definition: dict | None) -> None:
        self._format_definition = definition
        self._format_picture = None
        self._format_picture_key = None
        self.update()

    def set_lineweight_preview(self, enabled: bool) -> None:
        self._lineweight_preview = bool(enabled)
        self._format_picture = None
        self._format_picture_key = None
        self._title_block_picture = None
        self._title_block_picture_key = None
        self.update()

    def set_title_block_definition(self, definition: dict | None) -> None:
        self._title_block_definition = definition
        self._title_block_picture = None
        self._title_block_picture_key = None
        self._title_block_field_screen_bounds = {}
        self._hovered_title_block_field_id = None
        self._selected_title_block_field_id = None
        self.update()

    def set_title_block_context(self, context: dict | None) -> None:
        self._title_block_context = dict(context or {})
        self._title_block_picture = None
        self._title_block_picture_key = None
        self.update()

    def _drawing_pen(
        self,
        color: QColor,
        width_mm: float,
        style: Qt.PenStyle = Qt.PenStyle.SolidLine,
    ) -> QPen:
        if not self._lineweight_preview:
            return cosmetic_pen(color, style)
        pen = QPen(color, max(0.5, float(width_mm) * self._pixels_per_mm), style)
        pen.setCapStyle(Qt.PenCapStyle.FlatCap)
        return pen

    def set_sheet(self, sheet: dict, *, fit: bool = True) -> None:
        self._sheet = sheet
        self._pending_view = None
        self._hovered_title_block_field_id = None
        self._selected_title_block_field_id = None
        self._dragged_view_id = None
        self._drag_start_sheet = None
        self._drag_view_start = None
        available_view_ids = {
            str(view.get("id", "")) for view in sheet.get("views", [])
        }
        if self._selected_view_id not in available_view_ids:
            self._selected_view_id = None
        if self._hovered_view_id not in available_view_ids:
            self._hovered_view_id = None
        if fit:
            self.fit_sheet()
        self.update()

    def sheet_size(self) -> tuple[float, float]:
        width, height = SHEET_FORMATS.get(
            str(self._sheet.get("format", "A4")), SHEET_FORMATS["A4"]
        )
        if self._sheet.get("orientation") == "portrait":
            return height, width
        return width, height

    def fit_sheet(self) -> None:
        width, height = self.sheet_size()
        margin = 36.0
        self._pixels_per_mm = max(
            0.01,
            min(
                max(1.0, self.width() - margin * 2.0) / width,
                max(1.0, self.height() - margin * 2.0) / height,
            ),
        )
        self._pan = QPointF()
        self.update()

    @staticmethod
    def _renderer_display_mode(view: dict) -> str:
        return {
            "wireframe": "wire",
            "hidden_line": "hidden_edges",
            "no_hidden": "no_hidden",
            "shaded_edges": "shaded_with_edges",
            "shaded": "shaded",
        }.get(str(view.get("display_style", "no_hidden")), "no_hidden")

    def animate_fit_sheet(
        self,
        duration_ms: int = ANIMATION_DURATION_MS,
    ) -> None:
        width, height = self.sheet_size()
        margin = 36.0
        target_scale = max(
            0.01,
            min(
                max(1.0, self.width() - margin * 2.0) / width,
                max(1.0, self.height() - margin * 2.0) / height,
            ),
        )
        if self._fit_animation is not None:
            self._fit_animation.stop()
        start_scale = self._pixels_per_mm
        start_pan = QPointF(self._pan)
        animation = QVariantAnimation(self)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setDuration(max(1, int(duration_ms)))
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)

        def apply_progress(raw_progress) -> None:
            progress = float(raw_progress)
            self._pixels_per_mm = (
                start_scale + (target_scale - start_scale) * progress
            )
            self._pan = start_pan * (1.0 - progress)
            self.update()

        def finished() -> None:
            if self._fit_animation is animation:
                self._fit_animation = None

        animation.valueChanged.connect(apply_progress)
        animation.finished.connect(finished)
        self._fit_animation = animation
        animation.start()

    def begin_placement(self, view: dict) -> None:
        self._pending_view = view
        view_id = str(view.get("id", ""))
        render_data = self._view_render_data.get(view_id)
        if render_data is not None:
            self._prepare_view_geometry(
                view_id, view, render_data[0], render_data[1]
            )
        self.setFocus()
        self.update()

    def cancel_placement(self) -> None:
        self._pending_view = None
        self._cursor_sheet_position = None
        self.unsetCursor()
        self.update()

    def _paper_origin(self) -> QPointF:
        width, height = self.sheet_size()
        return QPointF(
            self.width() * 0.5 + width * self._pixels_per_mm * 0.5 + self._pan.x(),
            self.height() * 0.5 + height * self._pixels_per_mm * 0.5 + self._pan.y(),
        )

    def _screen_point(self, x: float, y: float) -> QPointF:
        origin = self._paper_origin()
        return QPointF(
            origin.x() - x * self._pixels_per_mm,
            origin.y() - y * self._pixels_per_mm,
        )

    def _sheet_point(self, point: QPointF) -> tuple[float, float]:
        origin = self._paper_origin()
        return (
            (origin.x() - point.x()) / self._pixels_per_mm,
            (origin.y() - point.y()) / self._pixels_per_mm,
        )

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        drawing_font = QFont(painter.font())
        drawing_font.setFamily(drawing_font_family())
        painter.setFont(drawing_font)
        painter.fillRect(self.rect(), QColor("#000000"))
        width, height = self.sheet_size()
        lower_right = self._screen_point(0.0, 0.0)
        upper_left = self._screen_point(width, height)
        boundary_color = load_drawing_style()["workspace"][
            "paper_boundary_color"
        ]
        painter.setPen(cosmetic_pen(QColor(str(boundary_color))))
        painter.drawRect(QRectF(
            min(upper_left.x(), lower_right.x()),
            min(upper_left.y(), lower_right.y()),
            abs(lower_right.x() - upper_left.x()),
            abs(lower_right.y() - upper_left.y()),
        ))
        self._draw_format(painter)
        self._draw_title_block(painter)
        self._draw_origin_indicator(painter)
        for view in self._sheet.get("views", []):
            view_id = str(view.get("id", ""))
            color = (
                QColor("#00D1FF") if view_id == self._selected_view_id
                else QColor("#FF8C00") if view_id == self._hovered_view_id
                else QColor("#FFFFFF")
            )
            geometry = self._runtime_view_geometry.get(view_id)
            if geometry is not None:
                rendered_view = dict(view)
                rendered_view.update(geometry)
                self._draw_view(painter, rendered_view, color)
        self._paint_annotations(painter)
        if self._pending_view is not None and self._cursor_sheet_position is not None:
            preview = dict(self._pending_view)
            preview["x"], preview["y"] = self._cursor_sheet_position
            geometry = self._runtime_view_geometry.get(
                str(self._pending_view.get("id", ""))
            )
            if geometry is not None:
                preview.update(geometry)
                self._draw_view(painter, preview, QColor("#FFFFFF"))
            self._draw_view_bounds(painter, preview, QColor("#4DD811"))

    def _paint_annotations(self, painter: QPainter) -> None:
        self._caption_screen_bounds.clear()
        for view in self._sheet.get("views", []):
            if bool(view.get("show_caption", False)):
                self._draw_view_caption(painter, view)
        for view in self._sheet.get("views", []):
            view_id = str(view.get("id", ""))
            if view_id not in {self._selected_view_id, self._hovered_view_id}:
                continue
            color = (
                QColor("#00D1FF") if view_id == self._selected_view_id
                else QColor("#FF8C00")
            )
            self._draw_view_bounds(painter, view, color)
        for dimension in self._sheet.get("dimensions", []):
            self._draw_dimension(painter, dimension)
        self._draw_dimension_selection(painter)
        if (
            len(self._dimension_references) == 2
            and self._dimension_cursor_sheet is not None
        ):
            self._draw_dimension(
                painter,
                {
                    "first": self._dimension_references[0],
                    "second": self._dimension_references[1],
                    "placement": list(self._dimension_cursor_sheet),
                },
            )

    def _draw_view_caption(self, painter: QPainter, view: dict) -> None:
        bounds = update_view_bounds(view)
        center_x = (bounds["left"] + bounds["right"]) * 0.5
        stored_position = view.get("caption_position")
        position = (
            (float(stored_position[0]), float(stored_position[1]))
            if isinstance(stored_position, (list, tuple))
            and len(stored_position) == 2
            else (center_x, bounds["top"] + 1.0)
        )
        anchor = self._screen_point(*position)
        font = QFont(painter.font())
        font.setPixelSize(max(1, round(5.0 * self._pixels_per_mm)))
        font.setBold(True)
        painter.setFont(font)
        view_id = str(view.get("id", ""))
        painter.setPen(cosmetic_pen(QColor(
            "#00D1FF"
            if view_id == self._selected_caption_view_id
            else "#FFFFFF"
        )))
        name = str(view.get("name", "")).strip()
        text = "\n".join(filter(None, (
            name,
            drawing_scale_text(float(view.get("scale", 1.0))),
        )))
        metrics = painter.fontMetrics()
        lines = text.splitlines()
        text_width = max(
            (metrics.horizontalAdvance(line) for line in lines),
            default=1,
        )
        text_height = metrics.lineSpacing() * max(1, len(lines))
        text_rect = QRectF(
            anchor.x() - text_width * 0.5 - 2.0,
            anchor.y() - text_height - 2.0,
            text_width + 4.0,
            text_height + 2.0,
        )
        self._caption_screen_bounds[view_id] = text_rect.adjusted(-3, -3, 3, 3)
        painter.drawText(
            text_rect,
            Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
            text,
        )

    def _caption_at(self, position: QPointF) -> str | None:
        return next((
            view_id for view_id, bounds in reversed(
                tuple(self._caption_screen_bounds.items())
            )
            if bounds.contains(position)
        ), None)

    def _format_point(self, x_from_left: float, y_from_bottom: float) -> QPointF:
        width, _height = self.sheet_size()
        return self._screen_point(width - x_from_left, y_from_bottom)

    def _draw_format(self, painter: QPainter) -> None:
        definition = self._format_definition
        if not definition:
            return
        width, height = self.sheet_size()
        frame = definition["frame"]
        geometry = frame.get("geometry", [])
        if geometry:
            geometry_point = (
                self._screen_point
                if definition.get("coordinate_system") == "bottom_right"
                else self._format_point
            )
            cache_key = (
                id(definition),
                self.width(),
                self.height(),
                round(self._pixels_per_mm, 6),
                round(self._pan.x(), 3),
                round(self._pan.y(), 3),
                self._lineweight_preview,
            )
            if (
                self._format_picture_key == cache_key
                and self._format_picture is not None
            ):
                painter.drawPicture(0, 0, self._format_picture)
                return
            picture = QPicture()
            format_painter = QPainter(picture)
            format_painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
            format_painter.setFont(painter.font())
            original_font = format_painter.font()
            pens = frame.get("pens", {})
            for entity in geometry:
                pen_definition = pens[str(entity["pen"])]
                pen = self._drawing_pen(
                    QColor(str(pen_definition["color"])),
                    float(pen_definition["width"]),
                )
                format_painter.setPen(pen)
                if entity["kind"] == "line":
                    format_painter.drawLine(
                        geometry_point(float(entity["x1"]), float(entity["y1"])),
                        geometry_point(float(entity["x2"]), float(entity["y2"])),
                    )
                elif entity["kind"] == "text":
                    font = QFont(original_font)
                    font.setPixelSize(max(
                        1,
                        round(float(entity["height"]) * self._pixels_per_mm),
                    ))
                    format_painter.setFont(font)
                    position = geometry_point(
                        float(entity["x"]), float(entity["y"])
                    )
                    text = str(entity["text"])
                    if entity.get("align") == "center":
                        bounds = QFontMetricsF(font).tightBoundingRect(text)
                        position.setX(
                            position.x() - (bounds.left() + bounds.right()) * 0.5
                        )
                    format_painter.drawText(position, text)
            format_painter.end()
            self._format_picture = picture
            self._format_picture_key = cache_key
            painter.drawPicture(0, 0, picture)
            return
        left = float(frame["left"])
        right = width - float(frame["right"])
        bottom = float(frame["bottom"])
        top = height - float(frame["top"])
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(cosmetic_pen(QColor(str(frame["color"]))))
        painter.drawRect(QRectF(
            self._format_point(left, bottom),
            self._format_point(right, top),
        ).normalized())

        block = definition["title_block"]
        if not block["enabled"]:
            return
        block_width = min(float(block["width"]), right - left)
        block_height = min(float(block["height"]), top - bottom)
        block_left = right - block_width
        block_top = bottom + block_height
        painter.setPen(cosmetic_pen(QColor(str(block["color"]))))
        painter.drawRect(QRectF(
            self._format_point(block_left, bottom),
            self._format_point(right, block_top),
        ).normalized())
        for y_offset in (10.0, 20.0, 30.0):
            if y_offset < block_height:
                painter.drawLine(
                    self._format_point(block_left, bottom + y_offset),
                    self._format_point(right, bottom + y_offset),
                )
        split = block_left + block_width * 0.62
        painter.drawLine(
            self._format_point(split, bottom),
            self._format_point(split, min(block_top, bottom + 30.0)),
        )
        upper_split = block_left + block_width * 0.78
        painter.drawLine(
            self._format_point(upper_split, bottom + 30.0),
            self._format_point(upper_split, block_top),
        )

    def _draw_title_block(self, painter: QPainter) -> None:
        definition = self._title_block_definition
        if not definition:
            return
        block_width = float(definition["width"])
        cache_key = (
            id(definition),
            self.width(),
            self.height(),
            round(self._pixels_per_mm, 6),
            round(self._pan.x(), 3),
            round(self._pan.y(), 3),
            self._lineweight_preview,
            self._hovered_title_block_field_id,
            self._selected_title_block_field_id,
        )
        if (
            self._title_block_picture_key == cache_key
            and self._title_block_picture is not None
        ):
            painter.drawPicture(0, 0, self._title_block_picture)
            return

        picture = QPicture()
        block_painter = QPainter(picture)
        block_painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        block_painter.setFont(painter.font())
        original_font = block_painter.font()
        pens = definition.get("pens", {})
        bom_rows = list(self._title_block_context.get("head_rows", ()))
        bom_region = next((
            region for region in definition.get("repeat_regions", ())
            if str(region.get("kind", "")) == "bom"
        ), None)
        self._title_block_field_screen_bounds = {}
        bottom_right_coordinates = (
            definition.get("coordinate_system") == "bottom_right"
        )

        def point(x: float, y: float) -> QPointF:
            if bottom_right_coordinates:
                return self._screen_point(x, y)
            return self._screen_point(block_width - x, y)

        def entity_in_region(entity: dict, region: dict | None) -> bool:
            if region is None:
                return False
            x0 = float(region["x"])
            y0 = float(region["y"])
            x1 = x0 + float(region["width"])
            y1 = y0 + float(region["height"])
            coordinates = [
                (entity.get("x"), entity.get("y")),
                (entity.get("x1"), entity.get("y1")),
                (entity.get("x2"), entity.get("y2")),
            ]
            present = [
                (float(x), float(y)) for x, y in coordinates
                if x is not None and y is not None
            ]
            return bool(present) and all(
                x0 - 1.0e-6 <= x <= x1 + 1.0e-6
                and y0 - 1.0e-6 <= y <= y1 + 1.0e-6
                for x, y in present
            )

        def bom_context(row: dict | None) -> dict:
            context = dict(self._title_block_context)
            if isinstance(row, dict):
                context["bom_row"] = row
                for key in (
                    "parameters",
                    "parameter_values",
                    "parameter_aliases",
                    "file_stem",
                ):
                    if key in row:
                        context[key] = row[key]
            return context

        def draw_box_text(
            text: str,
            *,
            x: float,
            y: float,
            width: float,
            height: float,
            text_height: float = 2.5,
            pen_name: str = "GREEN",
            align: str = "left",
            vertical_align: str = "center",
            offset_y: float = 0.0,
            color_override: QColor | None = None,
        ) -> None:
            pen_definition = pens[pen_name]
            block_painter.setPen(self._drawing_pen(
                color_override or QColor(str(pen_definition["color"])),
                float(pen_definition["width"]),
            ))
            font = QFont(original_font)
            font.setPixelSize(max(1, round(text_height * self._pixels_per_mm)))
            block_painter.setFont(font)
            if bottom_right_coordinates:
                rectangle = QRectF(
                    point(x + width, y + height), point(x, y)
                ).normalized()
            else:
                top_left = point(x, y + height)
                rectangle = QRectF(
                    top_left.x(), top_left.y(),
                    width * self._pixels_per_mm,
                    height * self._pixels_per_mm,
                )
            metrics = QFontMetricsF(font)
            ink_bounds = metrics.tightBoundingRect(text)
            if align == "right":
                text_x = rectangle.right() - ink_bounds.right()
            elif align == "center":
                text_x = rectangle.center().x() - (
                    ink_bounds.left() + ink_bounds.right()
                ) * 0.5
            else:
                text_x = rectangle.left() - ink_bounds.left()
            if vertical_align == "top":
                baseline_y = rectangle.top() - ink_bounds.top()
            elif vertical_align == "bottom":
                baseline_y = rectangle.bottom() - ink_bounds.bottom()
            else:
                baseline_y = rectangle.center().y() - (
                    ink_bounds.top() + ink_bounds.bottom()
                ) * 0.5
            baseline_y -= offset_y * self._pixels_per_mm
            block_painter.drawText(QPointF(text_x, baseline_y), text)

        def draw_sketch_text(text: str, entity: dict) -> QRectF:
            anchor_x = float(entity.get("anchor_x", entity.get("x", 0.0)))
            anchor_y = float(entity.get("anchor_y", entity.get("y", 0.0)))
            anchor = point(anchor_x, anchor_y)
            font = QFont(str(entity.get("font", "osifont")))
            font.setPixelSize(1000)
            metrics = QFontMetricsF(font)
            ink = metrics.tightBoundingRect(text)
            # ISO/CAD text height is the capital-letter height.  Scaling by
            # this particular string's ink bounds made equal-height labels
            # visibly different whenever accents or descenders were present.
            scale = max(float(entity.get("height", 2.5)), 0.01) / max(
                metrics.capHeight(), 1.0
            )
            angle = radians(float(entity.get("angle", 0.0)))
            x_sign = -1.0 if bool(entity.get("flip", False)) else 1.0
            x_local = (x_sign * cos(angle) * scale, x_sign * sin(angle) * scale)
            y_local = (sin(angle) * scale, -cos(angle) * scale)
            x_screen = point(anchor_x + x_local[0], anchor_y + x_local[1])
            y_screen = point(anchor_x + y_local[0], anchor_y + y_local[1])
            transform = QTransform(
                x_screen.x() - anchor.x(), x_screen.y() - anchor.y(),
                y_screen.x() - anchor.x(), y_screen.y() - anchor.y(),
                anchor.x(), anchor.y(),
            )
            x_offset = {
                "left": -ink.left(), "center": -(ink.left() + ink.right()) * 0.5,
                "right": -ink.right(),
            }.get(str(entity.get("align", "left")), -ink.left())
            y_offset = {
                "bottom": -ink.bottom(),
                "middle": -(ink.top() + ink.bottom()) * 0.5,
                "center": -(ink.top() + ink.bottom()) * 0.5,
                "top": -ink.top(), "baseline": 0.0,
            }.get(str(entity.get("vertical_align", "bottom")), -ink.bottom())
            origin = QPointF(x_offset, y_offset)
            block_painter.save()
            block_painter.setFont(font)
            block_painter.setTransform(transform, combine=True)
            block_painter.drawText(origin, text)
            block_painter.restore()
            return transform.mapRect(ink.translated(origin)).normalized()

        for entity in definition.get("geometry", []):
            pen_definition = pens[str(entity["pen"])]
            block_painter.setPen(self._drawing_pen(
                QColor(str(pen_definition["color"])),
                float(pen_definition["width"]),
            ))
            if entity["kind"] == "line":
                block_painter.drawLine(
                    point(float(entity["x1"]), float(entity["y1"])),
                    point(float(entity["x2"]), float(entity["y2"])),
                )
            elif entity["kind"] == "circle":
                center = point(float(entity["x"]), float(entity["y"]))
                radius = float(entity["radius"]) * self._pixels_per_mm
                block_painter.drawEllipse(center, radius, radius)
            elif entity["kind"] == "text":
                context = bom_context(bom_rows[0]) if (
                    bom_rows and entity_in_region(entity, bom_region)
                ) else self._title_block_context
                draw_sketch_text(
                    resolve_title_block_text(
                        entity,
                        context=context,
                        sheet=self._sheet,
                    ),
                    entity,
                )
        if bom_region is not None and len(bom_rows) > 1:
            direction = str(bom_region.get("direction", "up"))
            step = float(bom_region.get("step", 0.0)) or float(
                bom_region.get("height", 0.0)
            )
            direction_vector = {
                "up": (0.0, step),
                "down": (0.0, -step),
                "left": (step, 0.0),
                "right": (-step, 0.0),
            }.get(direction, (0.0, step))
            region_entities = [
                entity for entity in definition.get("geometry", [])
                if entity_in_region(entity, bom_region)
            ]
            for row_index, row in enumerate(bom_rows[1:], start=1):
                offset_x = direction_vector[0] * row_index
                offset_y = direction_vector[1] * row_index
                context = bom_context(row)
                for entity in region_entities:
                    pen_definition = pens[str(entity["pen"])]
                    block_painter.setPen(self._drawing_pen(
                        QColor(str(pen_definition["color"])),
                        float(pen_definition["width"]),
                    ))
                    if entity["kind"] == "line":
                        block_painter.drawLine(
                            point(float(entity["x1"]) + offset_x,
                                  float(entity["y1"]) + offset_y),
                            point(float(entity["x2"]) + offset_x,
                                  float(entity["y2"]) + offset_y),
                        )
                    elif entity["kind"] == "circle":
                        center = point(
                            float(entity["x"]) + offset_x,
                            float(entity["y"]) + offset_y,
                        )
                        radius = float(entity["radius"]) * self._pixels_per_mm
                        block_painter.drawEllipse(center, radius, radius)
                    elif entity["kind"] == "text":
                        shifted = dict(entity)
                        shifted["anchor_x"] = float(
                            entity.get("anchor_x", entity.get("x", 0.0))
                        ) + offset_x
                        shifted["anchor_y"] = float(
                            entity.get("anchor_y", entity.get("y", 0.0))
                        ) + offset_y
                        draw_sketch_text(
                            resolve_title_block_text(
                                entity, context=context, sheet=self._sheet
                            ),
                            shifted,
                        )
        for field in definition.get("fields", []):
            pen_definition = pens[str(field["pen"])]
            field_id = str(field["id"])
            highlight_color = (
                QColor("#00D1FF")
                if field_id == self._selected_title_block_field_id
                else QColor("#FF8C00")
                if field_id == self._hovered_title_block_field_id
                else None
            )
            block_painter.setPen(self._drawing_pen(
                highlight_color or QColor(str(pen_definition["color"])),
                float(pen_definition["width"]),
            ))
            value = self._title_block_field_value(field)
            if "anchor_x" in field:
                bounds = draw_sketch_text(value, field)
                self._title_block_field_screen_bounds[str(field["id"])] = bounds
                continue
            box_width = float(field.get("box_width", 0.0))
            box_height = float(field.get("box_height", 0.0))
            if box_width > 0.0 and box_height > 0.0:
                first = point(
                    float(field["x"]),
                    float(field["y"]),
                )
                second = point(
                    float(field["x"]) + box_width,
                    float(field["y"]) + box_height,
                )
                self._title_block_field_screen_bounds[str(field["id"])] = (
                    QRectF(first, second).normalized()
                )
                draw_box_text(
                    value,
                    x=float(field["x"]), y=float(field["y"]),
                    width=box_width, height=box_height,
                    text_height=float(field["height"]),
                    pen_name=str(field["pen"]),
                    align=str(field.get("align", "left")),
                    vertical_align=str(field.get("vertical_align", "center")),
                    offset_y=float(field.get("offset_y", 0.0)),
                    color_override=highlight_color,
                )
            else:
                font = QFont(original_font)
                font.setPixelSize(max(
                    1, round(float(field["height"]) * self._pixels_per_mm)
                ))
                block_painter.setFont(font)
                anchor = point(float(field["x"]), float(field["y"]))
                metrics = QFontMetricsF(font)
                text_width = max(
                    metrics.horizontalAdvance(value),
                    float(field["height"]) * self._pixels_per_mm,
                )
                text_height = max(
                    metrics.height(),
                    float(field["height"]) * self._pixels_per_mm,
                )
                align = str(field.get("align", "left"))
                left = (
                    anchor.x() - text_width
                    if align == "right"
                    else anchor.x() - text_width * 0.5
                    if align == "center"
                    else anchor.x()
                )
                self._title_block_field_screen_bounds[str(field["id"])] = (
                    QRectF(left, anchor.y() - text_height, text_width, text_height)
                    .normalized()
                )
                block_painter.drawText(
                    point(float(field["x"]), float(field["y"])),
                    value,
                )
        block_painter.end()
        self._title_block_picture = picture
        self._title_block_picture_key = cache_key
        painter.drawPicture(0, 0, picture)

    def _title_block_field_value(self, field: dict) -> str:
        return resolve_title_block_text(
            field,
            context=self._title_block_context,
            sheet=self._sheet,
        )

    def _draw_origin_indicator(self, painter: QPainter) -> None:
        # Drawing coordinates use the lower-right paper corner as zero:
        # positive X runs left and positive Y runs up.  Keep both axes on the
        # corresponding sheet-frame edges, matching the model-space triad.
        origin = self._screen_point(0.0, 0.0)
        axis_length = 40.0
        arrow_length = 10.0
        arrow_half_width = arrow_length * tan(radians(
            ARROW_HALF_ANGLE_DEGREES
        ))
        x_color = QColor.fromRgbF(*X_AXIS_COLOR, 1.0)
        y_color = QColor.fromRgbF(*Y_AXIS_COLOR, 1.0)
        x_end = QPointF(origin.x() - axis_length, origin.y())
        y_end = QPointF(origin.x(), origin.y() - axis_length)

        painter.setPen(cosmetic_pen(x_color))
        painter.setBrush(x_color)
        painter.drawLine(origin, x_end)
        painter.drawPolygon(QPolygonF((
            x_end,
            QPointF(
                x_end.x() + arrow_length,
                x_end.y() - arrow_half_width,
            ),
            QPointF(
                x_end.x() + arrow_length,
                x_end.y() + arrow_half_width,
            ),
        )))
        painter.drawText(QPointF(x_end.x() + 6.0, x_end.y() - 5.0), "X")

        painter.setPen(cosmetic_pen(y_color))
        painter.setBrush(y_color)
        painter.drawLine(origin, y_end)
        painter.drawPolygon(QPolygonF((
            y_end,
            QPointF(
                y_end.x() - arrow_half_width,
                y_end.y() + arrow_length,
            ),
            QPointF(
                y_end.x() + arrow_half_width,
                y_end.y() + arrow_length,
            ),
        )))
        painter.drawText(QPointF(y_end.x() - 14.0, y_end.y() + 5.0), "Y")

        painter.setPen(cosmetic_pen(QColor("#FF8C00")))
        painter.setBrush(QColor("#FF8C00"))
        painter.drawEllipse(origin, 2.5, 2.5)

    def _draw_depth_shaded_surface(
        self,
        painter: QPainter,
        view: dict,
    ) -> None:
        """Rasterize a drawing view with interpolated lighting and a Z-buffer."""
        view_id = str(view.get("id", ""))
        scale = float(view.get("scale", 1.0))
        pixel_scale = scale * self._pixels_per_mm
        cache_key = (pixel_scale, float(len(view.get("shaded_triangles", []))))
        cached = self._shaded_image_cache.get(view_id)
        if cached is None or cached[0] != cache_key:
            triangles = view.get("shaded_triangles", [])
            projected: list[tuple[list[tuple[float, float]], list[float], list[float], QColor]] = []
            all_points: list[tuple[float, float]] = []
            for triangle in triangles:
                source_points = triangle.get("points", [])
                if len(source_points) != 3:
                    continue
                points = [
                    (float(point[0]) * pixel_scale, -float(point[1]) * pixel_scale)
                    for point in source_points
                ]
                brightnesses = triangle.get("vertex_brightness", [])
                if not isinstance(brightnesses, list) or len(brightnesses) != 3:
                    brightnesses = [float(triangle.get("brightness", 1.0))] * 3
                depths = triangle.get("vertex_depths", [])
                if not isinstance(depths, list) or len(depths) != 3:
                    depths = [float(triangle.get("depth", 0.0))] * 3
                projected.append((
                    points,
                    [float(value) for value in brightnesses],
                    [float(value) for value in depths],
                    QColor(str(triangle.get("color", "#B9C2CC"))),
                ))
                all_points.extend(points)
            if not all_points:
                return
            minimum_x = floor(min(point[0] for point in all_points)) - 2
            minimum_y = floor(min(point[1] for point in all_points)) - 2
            maximum_x = ceil(max(point[0] for point in all_points)) + 2
            maximum_y = ceil(max(point[1] for point in all_points)) + 2
            width = max(1, maximum_x - minimum_x + 1)
            height = max(1, maximum_y - minimum_y + 1)
            pixels = np.zeros((height, width, 4), dtype=np.uint8)
            depth_buffer = np.full(
                (height, width), -np.inf, dtype=np.float32
            )
            for points, brightnesses, depths, base in projected:
                local = [
                    (point[0] - minimum_x, point[1] - minimum_y)
                    for point in points
                ]
                ax, ay = local[0]
                bx, by = local[1]
                cx, cy = local[2]
                denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
                if abs(denominator) <= 1.0e-12:
                    continue
                left = max(0, floor(min(ax, bx, cx)))
                right = min(width - 1, ceil(max(ax, bx, cx)))
                top = max(0, floor(min(ay, by, cy)))
                bottom = min(height - 1, ceil(max(ay, by, cy)))
                sample_y, sample_x = np.mgrid[
                    top:bottom + 1, left:right + 1
                ]
                sample_x = sample_x.astype(np.float32) + 0.5
                sample_y = sample_y.astype(np.float32) + 0.5
                first = (
                    (by - cy) * (sample_x - cx)
                    + (cx - bx) * (sample_y - cy)
                ) / denominator
                second = (
                    (cy - ay) * (sample_x - cx)
                    + (ax - cx) * (sample_y - cy)
                ) / denominator
                third = 1.0 - first - second
                depth = (
                    first * depths[0]
                    + second * depths[1]
                    + third * depths[2]
                )
                depth_region = depth_buffer[top:bottom + 1, left:right + 1]
                mask = (
                    (first >= -1.0e-7)
                    & (second >= -1.0e-7)
                    & (third >= -1.0e-7)
                    & (depth >= depth_region)
                )
                if not np.any(mask):
                    continue
                depth_region[mask] = depth[mask]
                brightness = (
                    first * brightnesses[0]
                    + second * brightnesses[1]
                    + third * brightnesses[2]
                )
                pixel_region = pixels[top:bottom + 1, left:right + 1]
                for channel, component in enumerate((
                    base.red(), base.green(), base.blue()
                )):
                    values = np.clip(
                        np.rint(component * brightness), 0, 255
                    ).astype(np.uint8)
                    pixel_region[..., channel][mask] = values[mask]
                pixel_region[..., 3][mask] = 255
            image = QImage(
                pixels.data,
                width,
                height,
                int(pixels.strides[0]),
                QImage.Format.Format_RGBA8888,
            ).copy()
            offset = QPointF(float(minimum_x), float(minimum_y))
            cached = (cache_key, image, offset)
            self._shaded_image_cache[view_id] = cached
        center = self._screen_point(
            float(view.get("x", 0.0)),
            float(view.get("y", 0.0)),
        )
        painter.drawImage(center + cached[2], cached[1])

    def _draw_view(self, painter: QPainter, view: dict, color: QColor) -> None:
        scale = float(view.get("scale", 1.0))
        center_x = float(view.get("x", 0.0))
        center_y = float(view.get("y", 0.0))

        def draw_polylines(polylines: list, pen: QPen) -> None:
            painter.setPen(pen)
            for polyline in polylines:
                points = [
                    self._screen_point(
                        center_x - float(point[0]) * scale,
                        center_y + float(point[1]) * scale,
                    )
                    for point in polyline
                ]
                if len(points) >= 2:
                    painter.drawPolyline(QPolygonF(points))

        display_style = str(view.get("display_style", "no_hidden"))
        visible_pen = self._drawing_pen(color, 0.50)
        auxiliary_pen = self._drawing_pen(color, 0.25)
        if display_style in {"shaded", "shaded_edges"}:
            self._draw_depth_shaded_surface(painter, view)
            if display_style == "shaded":
                if str(view.get("auxiliary_edges", "hidden")) == "visible":
                    draw_polylines(
                        view.get("auxiliary_polylines", []),
                        auxiliary_pen,
                    )
                return
        if display_style == "wireframe":
            draw_polylines(
                view.get("wireframe_polylines", view.get("polylines", [])),
                visible_pen,
            )
            if str(view.get("auxiliary_edges", "hidden")) == "visible":
                draw_polylines(
                    view.get("auxiliary_polylines", []),
                    auxiliary_pen,
                )
            return

        if display_style == "no_hidden":
            draw_polylines(view.get("polylines", []), visible_pen)
            if str(view.get("auxiliary_edges", "hidden")) == "visible":
                draw_polylines(
                    view.get("auxiliary_polylines", []),
                    auxiliary_pen,
                )
            return

        if display_style == "hidden_line":
            hidden_style = str(view.get("hidden_lines", "dimmed"))
            if hidden_style != "none":
                hidden_pen = self._drawing_pen(QColor("#808080"), 0.25)
                if hidden_style == "dimmed":
                    hidden_pen.setStyle(Qt.PenStyle.DashLine)
                # Hidden geometry is the underlay.  Drawing visible edges
                # afterwards guarantees that coincident rear edges cannot
                # shine through the visible outline as dashes.
                draw_polylines(view.get("hidden_polylines", []), hidden_pen)
            draw_polylines(view.get("polylines", []), visible_pen)
            if str(view.get("auxiliary_edges", "hidden")) == "visible":
                draw_polylines(
                    view.get("auxiliary_polylines", []),
                    auxiliary_pen,
                )
            return

        draw_polylines(view.get("polylines", []), visible_pen)
        if str(view.get("auxiliary_edges", "hidden")) == "visible":
            draw_polylines(
                view.get("auxiliary_polylines", []),
                auxiliary_pen,
            )

    def _draw_view_bounds(self, painter: QPainter, view: dict, color: QColor) -> None:
        bounds = update_view_bounds(view)
        first = self._screen_point(bounds["left"], bounds["bottom"])
        second = self._screen_point(bounds["right"], bounds["top"])
        pen = cosmetic_pen(color, Qt.PenStyle.CustomDashLine)
        pen.setDashPattern([8.0, 4.0])
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRect(QRectF(first, second).normalized())

    def _view_by_id(self, view_id: str) -> dict | None:
        return next(
            (
                view for view in self._sheet.get("views", [])
                if str(view.get("id", "")) == view_id
            ),
            None,
        )

    def _resolve_dimension_segment(
        self, reference: dict
    ) -> tuple[tuple[float, float], tuple[float, float]] | None:
        view = self._view_by_id(str(reference.get("view_id", "")))
        if view is None:
            return None
        if str(reference.get("topology_kind", "")) == "edge":
            data = self._view_render_data.get(str(reference.get("view_id", "")))
            if data is None:
                return None
            mesh, _colors = data
            owner_id, edge_index = self._dimension_runtime_edge(reference)
            edge = next((
                edge for edge in mesh.edges
                if edge.owner_id == owner_id
                and edge.edge_index == edge_index
            ), None)
            segment_index = int(reference.get("segment", -1))
            if edge is None or not 0 <= segment_index < len(edge.points) - 1:
                return None
            projected = self._edge_sheet_points(view, edge)
            return (
                projected[segment_index],
                projected[segment_index + 1],
            )
        return None

    def _dimension_runtime_edge(self, reference: dict) -> tuple[str, int]:
        view_id = str(reference.get("view_id", ""))
        stable_reference = str(reference.get("topology_reference", ""))
        if stable_reference:
            resolved = next((
                key
                for key, value in self._view_edge_reference_ids.get(
                    view_id, {}
                ).items()
                if value == stable_reference
            ), None)
            if resolved is not None:
                return str(resolved[0]), int(resolved[1])
            return "", -1
        return (
            str(reference.get("owner_id", "")),
            int(reference.get("edge_index", -1)),
        )

    def _edge_sheet_points(
        self, view: dict, edge: Any
    ) -> list[tuple[float, float]]:
        horizontal, vertical, _depth = projection_axes(
            view.get("orientation", "isometric")
        )
        scale = float(view.get("scale", 1.0))
        center_x = float(view.get("x", 0.0))
        center_y = float(view.get("y", 0.0))
        geometry = self._runtime_view_geometry.get(str(view.get("id", "")), {})
        projection_center = geometry.get("projection_center", (0.0, 0.0))
        projection_center_x = float(projection_center[0])
        projection_center_y = float(projection_center[1])
        return [
            (
                center_x - (
                    sum(horizontal[axis] * point[axis] for axis in range(3))
                    - projection_center_x
                ) * scale,
                center_y + (
                    sum(vertical[axis] * point[axis] for axis in range(3))
                    - projection_center_y
                ) * scale,
            )
            for point in edge.points
        ]

    @staticmethod
    def _point_segment_distance(
        point: QPointF, first: QPointF, second: QPointF
    ) -> float:
        dx, dy = second.x() - first.x(), second.y() - first.y()
        length_squared = dx * dx + dy * dy
        if length_squared <= 1e-12:
            return hypot(point.x() - first.x(), point.y() - first.y())
        ratio = max(0.0, min(1.0, (
            (point.x() - first.x()) * dx + (point.y() - first.y()) * dy
        ) / length_squared))
        nearest = QPointF(first.x() + ratio * dx, first.y() + ratio * dy)
        return hypot(point.x() - nearest.x(), point.y() - nearest.y())

    def _dimension_segment_candidates(self, position: QPointF) -> tuple[dict, ...]:
        candidates: list[tuple[float, dict]] = []
        view = self._view_at(position)
        if view is not None:
            view_id = str(view.get("id", ""))
            data = self._view_render_data.get(view_id)
            if data is not None:
                mesh, _colors = data
                for edge in mesh.edges:
                    if edge.element_kind != "edge":
                        continue
                    projected = [
                        self._screen_point(*point)
                        for point in self._edge_sheet_points(view, edge)
                    ]
                    for index in range(len(projected) - 1):
                        distance = self._point_segment_distance(
                            position, projected[index], projected[index + 1]
                        )
                        if distance <= 8.0:
                            stable_reference = self._view_edge_reference_ids.get(
                                view_id, {}
                            ).get((edge.owner_id, edge.edge_index))
                            if not stable_reference:
                                continue
                            candidates.append((distance, {
                                "view_id": view_id,
                                "topology_kind": "edge",
                                "owner_id": edge.owner_id,
                                "edge_index": edge.edge_index,
                                "segment": index,
                                "topology_reference": stable_reference,
                            }))
        candidates.sort(key=lambda item: item[0])
        unique: list[dict] = []
        seen: set[tuple[str, str, int]] = set()
        for _distance, reference in candidates:
            key = (
                str(reference.get("view_id", "")),
                str(reference.get("topology_reference", "")),
                0,
            )
            if key not in seen:
                seen.add(key)
                unique.append(reference)
        return tuple(unique)

    def _dimension_segment_at(self, position: QPointF) -> dict | None:
        candidates = self._dimension_segment_candidates(position)
        return candidates[0] if candidates else None

    def _emit_dimension_status(self) -> None:
        if not self._dimension_tool_active:
            self.dimensionStatusChanged.emit("")
            return
        if len(self._dimension_references) == 2:
            self.dimensionStatusChanged.emit(
                tr("drawing.dimension.status.place")
            )
            return
        reference = self._dimension_hover_reference
        if reference is None:
            key = (
                "drawing.dimension.status.select_second"
                if self._dimension_references
                else "drawing.dimension.status.select_first"
            )
            self.dimensionStatusChanged.emit(tr(key))
            return
        view = self._view_by_id(str(reference.get("view_id", ""))) or {}
        view_name = str(
            view.get("name") or reference.get("view_id", "")
        )
        self.dimensionStatusChanged.emit(tr(
            "drawing.dimension.status.edge_candidate",
            edge=int(reference.get("edge_index", -1)) + 1,
            view=view_name,
            current=max(1, self._dimension_candidate_index + 1),
            total=max(1, len(self._dimension_candidate_cycle)),
        ))

    def _dimension_reference_lines(
        self, reference: dict
    ) -> list[tuple[tuple[float, float], tuple[float, float]]]:
        view = self._view_by_id(str(reference.get("view_id", "")))
        data = self._view_render_data.get(str(reference.get("view_id", "")))
        if view is None or data is None:
            return []
        mesh, _colors = data
        owner_id, edge_index = self._dimension_runtime_edge(reference)
        edge = next((
            edge for edge in mesh.edges
            if edge.owner_id == owner_id
            and edge.edge_index == edge_index
        ), None)
        if edge is None:
            return []
        points = self._edge_sheet_points(view, edge)
        return list(zip(points, points[1:]))

    def _draw_dimension_selection(self, painter: QPainter) -> None:
        if self._dimension_hover_reference is not None:
            painter.setPen(cosmetic_pen(QColor("#FF8C00")))
            for first, second in self._dimension_reference_lines(
                self._dimension_hover_reference
            ):
                painter.drawLine(
                    self._screen_point(*first), self._screen_point(*second)
                )
        painter.setPen(cosmetic_pen(QColor("#00D1FF")))
        for reference in self._dimension_references:
            for first, second in self._dimension_reference_lines(reference):
                painter.drawLine(
                    self._screen_point(*first), self._screen_point(*second),
                )

    def _commit_preview_dimension(self) -> None:
        if (
            not self._dimension_tool_active
            or len(self._dimension_references) != 2
            or self._dimension_cursor_sheet is None
        ):
            return
        dimension = {
            "id": str(uuid4()),
            "type": "parallel_distance",
            "first": dict(self._dimension_references[0]),
            "second": dict(self._dimension_references[1]),
            "placement": list(self._dimension_cursor_sheet),
            "color": "#FFD400",
        }
        self._sheet.setdefault("dimensions", []).append(dimension)
        self._dimension_references = []
        self._dimension_hover_reference = None
        self._dimension_cursor_sheet = None
        self.dimensionCreated.emit()
        self._emit_dimension_status()
        self.update()

    def _draw_dimension(self, painter: QPainter, dimension: dict) -> None:
        first_reference = dimension.get("first")
        second_reference = dimension.get("second")
        placement = dimension.get("placement")
        if not isinstance(first_reference, dict) or not isinstance(second_reference, dict):
            return
        if not isinstance(placement, (list, tuple)) or len(placement) != 2:
            return
        if first_reference.get("view_id") != second_reference.get("view_id"):
            return
        first = self._resolve_dimension_segment(first_reference)
        second = self._resolve_dimension_segment(second_reference)
        if first is None or second is None:
            return
        geometry = parallel_dimension_geometry(
            first, second, (float(placement[0]), float(placement[1]))
        )
        if geometry is None:
            return

        yellow = QColor("#FFD400")
        painter.setPen(self._drawing_pen(yellow, 0.25))
        first_point = geometry["first_point"]
        second_point = geometry["second_point"]
        for anchor, point in (
            (geometry["first_anchor"], first_point),
            (geometry["second_anchor"], second_point),
        ):
            vx, vy = point[0] - anchor[0], point[1] - anchor[1]
            length = hypot(vx, vy)
            if length > 1e-9:
                ux, uy = vx / length, vy / length
                start = (anchor[0] + ux, anchor[1] + uy)
                end = (point[0] + ux * 1.5, point[1] + uy * 1.5)
                painter.drawLine(
                    self._screen_point(*start), self._screen_point(*end)
                )
        first_screen = self._screen_point(*first_point)
        second_screen = self._screen_point(*second_point)
        painter.drawLine(first_screen, second_screen)

        dx = second_screen.x() - first_screen.x()
        dy = second_screen.y() - first_screen.y()
        screen_length = hypot(dx, dy)
        if screen_length <= 1e-9:
            return
        ux, uy = dx / screen_length, dy / screen_length
        px, py = -uy, ux
        arrow_length = max(5.0, 3.0 * self._pixels_per_mm)
        half_width = arrow_length * tan(radians(
            ARROW_HALF_ANGLE_DEGREES
        ))
        painter.setBrush(yellow)
        painter.drawPolygon(QPolygonF((
            first_screen,
            QPointF(first_screen.x() + ux * arrow_length + px * half_width,
                    first_screen.y() + uy * arrow_length + py * half_width),
            QPointF(first_screen.x() + ux * arrow_length - px * half_width,
                    first_screen.y() + uy * arrow_length - py * half_width),
        )))
        painter.drawPolygon(QPolygonF((
            second_screen,
            QPointF(second_screen.x() - ux * arrow_length + px * half_width,
                    second_screen.y() - uy * arrow_length + py * half_width),
            QPointF(second_screen.x() - ux * arrow_length - px * half_width,
                    second_screen.y() - uy * arrow_length - py * half_width),
        )))

        view = self._view_by_id(str(first_reference.get("view_id", "")))
        view_scale = max(float(view.get("scale", 1.0)) if view else 1.0, 1e-9)
        value = float(geometry["distance"]) / view_scale
        text = f"{value:.2f}".rstrip("0").rstrip(".")
        midpoint = QPointF(
            (first_screen.x() + second_screen.x()) * 0.5,
            (first_screen.y() + second_screen.y()) * 0.5,
        )
        angle = degrees(atan2(dy, dx))
        if angle > 90.0:
            angle -= 180.0
        elif angle < -90.0:
            angle += 180.0
        painter.save()
        painter.translate(midpoint)
        painter.rotate(angle)
        font = painter.font()
        font.setPixelSize(max(8, round(3.5 * self._pixels_per_mm)))
        painter.setFont(font)
        bounds = painter.fontMetrics().boundingRect(text).adjusted(-3, -1, 3, 1)
        bounds.moveCenter(QPoint(0, -max(3, round(1.2 * self._pixels_per_mm))))
        painter.fillRect(bounds, QColor("#000000"))
        painter.setPen(self._drawing_pen(yellow, 0.25))
        painter.drawText(bounds, Qt.AlignmentFlag.AlignCenter, text)
        painter.restore()

    def _view_at(self, position: QPointF) -> dict | None:
        x, y = self._sheet_point(position)
        candidates = []
        for view in self._sheet.get("views", []):
            bounds = update_view_bounds(view)
            if bounds["left"] <= x <= bounds["right"] and bounds["bottom"] <= y <= bounds["top"]:
                area = (bounds["right"] - bounds["left"]) * (bounds["top"] - bounds["bottom"])
                candidates.append((area, view))
        return min(candidates, key=lambda item: item[0])[1] if candidates else None

    def _title_block_field_at(self, position: QPointF) -> str | None:
        definition = self._title_block_definition or {}
        editable_ids = {
            str(field.get("id", ""))
            for field in definition.get("fields", [])
            if bool(field.get("editable", True))
        }
        return next((
            field_id
            for field_id, bounds in self._title_block_field_screen_bounds.items()
            if field_id in editable_ids
            and bounds.adjusted(-3.0, -3.0, 3.0, 3.0).contains(position)
        ), None)

    def wheelEvent(self, event: QWheelEvent) -> None:
        before = self._sheet_point(event.position())
        factor = 1.0 / 1.15 if event.angleDelta().y() > 0 else 1.15
        self._pixels_per_mm = max(0.05, min(100.0, self._pixels_per_mm * factor))
        after_screen = self._screen_point(*before)
        self._pan += event.position() - after_screen
        self.update()
        event.accept()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if (
            event.button() == Qt.MouseButton.MiddleButton
            and self._dimension_tool_active
        ):
            if len(self._dimension_references) == 2:
                self._dimension_cursor_sheet = self._sheet_point(event.position())
                self._dimension_middle_timer.start(
                    QApplication.doubleClickInterval()
                )
            event.accept()
            return
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._last_mouse = event.position().toPoint()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._dimension_tool_active:
            if len(self._dimension_references) < 2:
                reference = self._dimension_hover_reference
                if reference is not None and reference not in self._dimension_references:
                    if (
                        self._dimension_references
                        and reference["view_id"]
                        != self._dimension_references[0]["view_id"]
                    ):
                        event.accept()
                        return
                    candidate = [*self._dimension_references, reference]
                    if len(candidate) == 2:
                        first = self._resolve_dimension_segment(candidate[0])
                        second = self._resolve_dimension_segment(candidate[1])
                        if (
                            first is None
                            or second is None
                            or parallel_dimension_geometry(
                                first, second, self._sheet_point(event.position())
                            ) is None
                        ):
                            event.accept()
                            return
                    self._dimension_references = candidate
                    self._emit_dimension_status()
                    self.update()
                event.accept()
                return
            event.accept()
            return
        if event.button() == Qt.MouseButton.RightButton and self._dimension_tool_active:
            candidates = self._dimension_segment_candidates(event.position())
            if candidates:
                if candidates != self._dimension_candidate_cycle:
                    self._dimension_candidate_cycle = candidates
                    self._dimension_candidate_index = 0
                else:
                    self._dimension_candidate_index = (
                        self._dimension_candidate_index + 1
                    ) % len(candidates)
                self._dimension_hover_reference = candidates[
                    self._dimension_candidate_index
                ]
                self._emit_dimension_status()
                self.update()
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._pending_view is not None:
            x, y = self._sheet_point(event.position())
            self.placementRequested.emit(x, y)
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton:
            field_id = self._title_block_field_at(event.position())
            if field_id is not None:
                self._selected_title_block_field_id = field_id
                self._selected_view_id = None
                self._selected_caption_view_id = None
                self.viewSelected.emit("")
                self.update()
                event.accept()
                return
            self._selected_title_block_field_id = None
            caption_view_id = self._caption_at(event.position())
            self._selected_caption_view_id = caption_view_id
            if caption_view_id is not None:
                view = self._view_by_id(caption_view_id)
                if view is not None:
                    bounds = update_view_bounds(view)
                    stored = view.get("caption_position")
                    start_position = (
                        (float(stored[0]), float(stored[1]))
                        if isinstance(stored, (list, tuple)) and len(stored) == 2
                        else (
                            (bounds["left"] + bounds["right"]) * 0.5,
                            bounds["top"] + 1.0,
                        )
                    )
                    self._dragged_caption_view_id = caption_view_id
                    self._drag_caption_start_sheet = self._sheet_point(
                        event.position()
                    )
                    self._drag_caption_position_start = start_position
                    self._selected_view_id = None
                    self.setCursor(Qt.CursorShape.ClosedHandCursor)
                    self.viewSelected.emit(caption_view_id)
                    self.update()
                    event.accept()
                    return
            view = self._view_at(event.position())
            self._selected_view_id = str(view.get("id", "")) if view else None
            if view is not None:
                self._dragged_view_id = self._selected_view_id
                self._drag_start_sheet = self._sheet_point(event.position())
                self._drag_view_start = (
                    float(view.get("x", 0.0)),
                    float(view.get("y", 0.0)),
                )
                self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.viewSelected.emit(self._selected_view_id or "")
            self.update()
            event.accept()
            return
        if event.button() == Qt.MouseButton.RightButton:
            field_id = self._title_block_field_at(event.position())
            if field_id is not None:
                self._selected_title_block_field_id = field_id
                self._selected_view_id = None
                self.update()
                menu = QMenu(self)
                properties_action = menu.addAction(
                    tr("drawing.title_block_field.text_properties")
                )
                if menu.exec(event.globalPosition().toPoint()) == properties_action:
                    self.titleBlockFieldDoubleClicked.emit(field_id)
                event.accept()
                return
            view = self._view_at(event.position())
            menu = QMenu(self)
            if view is None:
                action = menu.addAction(tr("drawing.command.insert_view"))
                if menu.exec(event.globalPosition().toPoint()) == action:
                    self.insertViewRequested.emit()
            else:
                action = menu.addAction(tr("drawing.command.create_projection"))
                if menu.exec(event.globalPosition().toPoint()) == action:
                    self.projectViewRequested.emit(str(view.get("id", "")))
            event.accept()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._panning:
            current = event.position().toPoint()
            self._pan += current - self._last_mouse
            self._last_mouse = current
            self.update()
        elif (
            self._dragged_caption_view_id is not None
            and self._drag_caption_start_sheet is not None
            and self._drag_caption_position_start is not None
            and event.buttons() & Qt.MouseButton.LeftButton
        ):
            current = self._sheet_point(event.position())
            view = self._view_by_id(self._dragged_caption_view_id)
            if view is not None:
                view["caption_position"] = [
                    self._drag_caption_position_start[0]
                    + current[0] - self._drag_caption_start_sheet[0],
                    self._drag_caption_position_start[1]
                    + current[1] - self._drag_caption_start_sheet[1],
                ]
                self.update()
        elif (
            self._dragged_view_id is not None
            and self._drag_start_sheet is not None
            and self._drag_view_start is not None
            and event.buttons() & Qt.MouseButton.LeftButton
        ):
            current = self._sheet_point(event.position())
            move_drawing_view(
                self._sheet,
                self._dragged_view_id,
                self._drag_view_start[0]
                + current[0] - self._drag_start_sheet[0],
                self._drag_view_start[1]
                + current[1] - self._drag_start_sheet[1],
            )
            self.update()
        elif self._dimension_tool_active:
            self._dimension_cursor_sheet = self._sheet_point(event.position())
            candidates = self._dimension_segment_candidates(event.position())
            if candidates != self._dimension_candidate_cycle:
                self._dimension_candidate_cycle = candidates
                self._dimension_candidate_index = 0 if candidates else -1
            self._dimension_hover_reference = (
                candidates[self._dimension_candidate_index]
                if candidates and self._dimension_candidate_index >= 0
                else None
            )
            self._emit_dimension_status()
            self.update()
        elif self._pending_view is not None:
            self._cursor_sheet_position = self._sheet_point(event.position())
            self._update_projection_preview()
            self.update()
        else:
            field_id = self._title_block_field_at(event.position())
            if field_id != self._hovered_title_block_field_id:
                self._hovered_title_block_field_id = field_id
                self.update()
            view = self._view_at(event.position())
            hovered = (
                str(view.get("id", ""))
                if view is not None and field_id is None
                else None
            )
            if hovered != self._hovered_view_id:
                self._hovered_view_id = hovered
                self.update()

    def leaveEvent(self, event) -> None:
        if self._hovered_title_block_field_id is not None:
            self._hovered_title_block_field_id = None
            self.update()
        super().leaveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton and self._panning:
            self._panning = False
            self.unsetCursor()
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._dragged_caption_view_id is not None
        ):
            self._dragged_caption_view_id = None
            self._drag_caption_start_sheet = None
            self._drag_caption_position_start = None
            self.unsetCursor()
            self.viewMoveFinished.emit()
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._dragged_view_id is not None
        ):
            self._dragged_view_id = None
            self._drag_start_sheet = None
            self._drag_view_start = None
            self.unsetCursor()
            self.viewMoveFinished.emit()
            event.accept()

    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:
        if (
            event.button() == Qt.MouseButton.MiddleButton
            and self._dimension_tool_active
        ):
            self._dimension_middle_timer.stop()
            self.set_dimension_tool(False)
            self.dimensionToolCancelled.emit()
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._pending_view is None:
            field_id = self._title_block_field_at(event.position())
            if field_id is not None:
                self._selected_title_block_field_id = field_id
                self.update()
                self.titleBlockFieldDoubleClicked.emit(field_id)
                event.accept()
                return
            view = self._view_at(event.position())
            if view is not None:
                view_id = str(view.get("id", ""))
                self._selected_view_id = view_id
                self.viewDoubleClicked.emit(view_id)
                self.update()
                event.accept()
                return
        super().mouseDoubleClickEvent(event)

    def keyPressEvent(self, event) -> None:
        if (
            event.key() == Qt.Key.Key_Delete
            and self._pending_view is None
            and not self._dimension_tool_active
            and self._selected_view_id
        ):
            self.viewDeleteRequested.emit(self._selected_view_id)
            event.accept()
            return
        if event.key() == Qt.Key.Key_Escape and self._dimension_tool_active:
            if self._dimension_references:
                self._dimension_references = []
                self._dimension_cursor_sheet = None
                self._emit_dimension_status()
                self.update()
            else:
                self.set_dimension_tool(False)
                self.dimensionToolCancelled.emit()
            event.accept()
            return
        if event.key() == Qt.Key.Key_Escape and self._pending_view is not None:
            self.cancel_placement()
            event.accept()
            return
        super().keyPressEvent(event)

    def _update_projection_preview(self) -> None:
        if self._pending_view is None or self._cursor_sheet_position is None:
            return
        variants = self._pending_view.get("projection_variants")
        parent = self._pending_view.get("parent_position")
        if not isinstance(variants, dict) or not isinstance(parent, (list, tuple)):
            return
        dx = self._cursor_sheet_position[0] - float(parent[0])
        dy = self._cursor_sheet_position[1] - float(parent[1])
        directions = (
            "right", "top_right", "top", "top_left",
            "left", "bottom_left", "bottom", "bottom_right",
        )
        angle = atan2(dy, -dx)
        direction = directions[round(angle / (3.141592653589793 / 4.0)) % 8]
        variant = variants.get(direction)
        if isinstance(variant, dict):
            previous_direction = str(
                self._pending_view.get("projection_direction", "")
            )
            self._pending_view.update(variant)
            if direction != previous_direction:
                view_id = str(self._pending_view.get("id", ""))
                render_data = self._view_render_data.get(view_id)
                if render_data is not None:
                    self._prepare_view_geometry(
                        view_id,
                        self._pending_view,
                        render_data[0],
                        render_data[1],
                    )
        ray_x, ray_y = projection_placement_vector(direction)
        distance = dx * ray_x + dy * ray_y
        self._cursor_sheet_position = (
            float(parent[0]) + distance * ray_x,
            float(parent[1]) + distance * ray_y,
        )


class DrawingWorkspace(QWidget):
    changed = Signal()
    activeSheetChanged = Signal()
    viewPlaced = Signal(str)
    viewDoubleClicked = Signal(str)
    insertViewRequested = Signal()
    projectViewRequested = Signal(str)
    titleBlockFieldDoubleClicked = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.document: PartDocument | None = None
        self.sheets: list[dict] = []
        self.active_sheet_index = 0
        self.canvas = DrawingCanvas(self)
        self.canvas.placementRequested.connect(self._place_pending_view)
        self.canvas.viewDoubleClicked.connect(self.viewDoubleClicked.emit)
        self.canvas.insertViewRequested.connect(self.insertViewRequested.emit)
        self.canvas.projectViewRequested.connect(self.projectViewRequested.emit)
        self.canvas.titleBlockFieldDoubleClicked.connect(
            self.titleBlockFieldDoubleClicked.emit
        )
        self.canvas.dimensionCreated.connect(self._store)
        self.canvas.viewDeleteRequested.connect(self._delete_view)
        self.canvas.viewMoveFinished.connect(self._store)
        self._pending_view: dict | None = None
        self.formats_directory = Path("config/formats")
        self.title_blocks_directory = self.formats_directory

        self.sheet_tabs = QTabBar()
        self.sheet_tabs.setExpanding(False)
        self.sheet_tabs.currentChanged.connect(self._select_sheet)
        add_button = QPushButton("+")
        remove_button = QPushButton("−")
        add_button.setFixedWidth(34)
        remove_button.setFixedWidth(34)
        add_button.clicked.connect(self.add_sheet)
        remove_button.clicked.connect(self.remove_sheet)
        self.format_combo = QComboBox()
        self.format_combo.addItems(SHEET_FORMATS)
        self.format_combo.currentTextChanged.connect(self._change_format)
        self.add_format_button = QPushButton(tr("drawing.command.add_format"))
        self.add_format_button.clicked.connect(self._choose_format)
        self.remove_format_button = QPushButton(
            tr("drawing.command.remove_format")
        )
        self.remove_format_button.clicked.connect(self._remove_format)
        self.add_title_block_button = QPushButton(
            tr("drawing.command.add_title_block")
        )
        self.add_title_block_button.clicked.connect(self._choose_title_block)
        self.remove_title_block_button = QPushButton(
            tr("drawing.command.remove_title_block")
        )
        self.remove_title_block_button.clicked.connect(self._remove_title_block)
        self.projection_method_combo = QComboBox()
        self.projection_method_combo.addItem(
            tr("drawing.projection.first_angle"), "first_angle"
        )
        self.projection_method_combo.addItem(
            tr("drawing.projection.third_angle"), "third_angle"
        )
        self.projection_method_combo.currentIndexChanged.connect(
            self._change_projection_method
        )
        self.family_instance_combo = UpwardComboBox()
        self.family_instance_combo.currentIndexChanged.connect(
            self._change_family_instance
        )
        self._family_instances: list[str] = []
        self._title_block_context: dict = {}
        self.lineweight_combo = UpwardComboBox()
        self.lineweight_combo.addItem(
            tr("drawing.lineweight.hairline"), False
        )
        self.lineweight_combo.addItem(
            tr("drawing.lineweight.preview"), True
        )
        self.lineweight_combo.currentIndexChanged.connect(
            self._change_lineweight_mode
        )
        self.default_scale_numerator_spin = QDoubleSpinBox()
        self.default_scale_spin = QDoubleSpinBox()
        for spin in (
            self.default_scale_numerator_spin,
            self.default_scale_spin,
        ):
            spin.setRange(1.0, 1000.0)
            spin.setDecimals(0)
            spin.setSingleStep(1.0)
            spin.setValue(1.0)
            spin.valueChanged.connect(self._change_default_scale)

        bottom = QHBoxLayout()
        bottom.setContentsMargins(6, 3, 6, 3)
        bottom.addWidget(self.sheet_tabs, 1)
        bottom.addWidget(remove_button)
        bottom.addWidget(add_button)
        bottom.addSpacing(16)
        bottom.addWidget(QLabel(tr("drawing.lineweight.mode")))
        bottom.addWidget(self.lineweight_combo)
        bottom.addWidget(QLabel("Měřítko:"))
        bottom.addWidget(self.default_scale_numerator_spin)
        bottom.addWidget(QLabel(":"))
        bottom.addWidget(self.default_scale_spin)
        bottom.addWidget(QLabel("Formát:"))
        bottom.addWidget(self.format_combo)
        bottom.addWidget(self.add_format_button)
        bottom.addWidget(self.remove_format_button)
        bottom.addWidget(self.add_title_block_button)
        bottom.addWidget(self.remove_title_block_button)
        bottom.addWidget(QLabel(tr("drawing.projection_method")))
        bottom.addWidget(self.projection_method_combo)
        bottom.addWidget(QLabel(tr("drawing.family_table")))
        bottom.addWidget(self.family_instance_combo)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self.canvas, 1)
        layout.addLayout(bottom)

    def set_formats_directory(self, directory: Path) -> None:
        self.formats_directory = directory.resolve()
        self.title_blocks_directory = self.formats_directory
        self._load_active_format()
        self._load_active_title_block()

    def set_document(self, document: PartDocument | None) -> None:
        self.document = document
        self.canvas.clear_view_render_data()
        self.sheets = drawing_sheets(document) if document is not None else []
        self.active_sheet_index = min(
            int(document.document_settings.get("active_sheet", "0"))
            if document is not None else 0,
            max(0, len(self.sheets) - 1),
        )
        self._refresh_controls(fit=True)

    def set_view_render_data(
        self,
        view_id: str,
        mesh: ViewerMesh,
        colors: dict[str, str] | None = None,
        edge_reference_ids: dict[tuple[str, int], str] | None = None,
    ) -> None:
        self.canvas.set_view_render_data(
            view_id, mesh, colors, edge_reference_ids
        )

    def set_title_block_context(self, context: dict | None) -> None:
        self._title_block_context = dict(context or {})
        self._apply_title_block_context()

    def _apply_title_block_context(self) -> None:
        context = dict(self._title_block_context)
        context["sheet_index"] = self.active_sheet_index
        context["sheet_count"] = len(self.sheets)
        sheet = self.active_sheet()
        context["local_parameters"] = dict(
            sheet.get("title_block_values", {}) if sheet is not None else {}
        )
        self.canvas.set_title_block_context(context)

    def copy_view_render_data(self, source_id: str, target_id: str) -> None:
        self.canvas.copy_view_render_data(source_id, target_id)

    def active_sheet(self) -> dict | None:
        if 0 <= self.active_sheet_index < len(self.sheets):
            return self.sheets[self.active_sheet_index]
        return None

    def active_title_block_field(self, field_id: str) -> dict | None:
        definition = self.canvas._title_block_definition
        if definition is None:
            return None
        return next((
            dict(field)
            for field in definition.get("fields", [])
            if str(field.get("id", "")) == str(field_id)
        ), None)

    def set_title_block_local_values(self, values: dict[str, str]) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        stored = dict(sheet.get("title_block_values", {}))
        stored.update({str(key): str(value) for key, value in values.items()})
        sheet["title_block_values"] = stored
        self._apply_title_block_context()
        self._store()

    def set_family_instances(self, instances: list[str]) -> None:
        self._family_instances = list(dict.fromkeys(
            str(item).strip() for item in instances if str(item).strip()
        ))
        self._refresh_family_instance_combo()

    def _refresh_family_instance_combo(self) -> None:
        sheet = self.active_sheet()
        selected = str(sheet.get("family_instance", "")) if sheet else ""
        self.family_instance_combo.blockSignals(True)
        self.family_instance_combo.clear()
        for instance in self._family_instances:
            self.family_instance_combo.addItem(instance, instance)
        index = self.family_instance_combo.findData(selected)
        self.family_instance_combo.setCurrentIndex(
            index if index >= 0 else (0 if self._family_instances else -1)
        )
        self.family_instance_combo.setEnabled(bool(self._family_instances))
        self.family_instance_combo.blockSignals(False)
        if sheet is not None and self.family_instance_combo.currentIndex() >= 0:
            sheet["family_instance"] = str(
                self.family_instance_combo.currentData()
            )

    def _refresh_controls(self, *, fit: bool = False) -> None:
        self.sheet_tabs.blockSignals(True)
        while self.sheet_tabs.count():
            self.sheet_tabs.removeTab(0)
        for sheet in self.sheets:
            self.sheet_tabs.addTab(str(sheet.get("name", "List")))
        self.sheet_tabs.setCurrentIndex(self.active_sheet_index)
        self.sheet_tabs.blockSignals(False)
        sheet = self.active_sheet()
        if sheet is None:
            return
        self.format_combo.blockSignals(True)
        self.format_combo.setCurrentText(str(sheet.get("format", "A4")))
        self.format_combo.blockSignals(False)
        self.remove_format_button.setEnabled(bool(sheet.get("format_definition")))
        self.remove_title_block_button.setEnabled(
            bool(sheet.get("title_block_definition"))
        )
        self.default_scale_numerator_spin.blockSignals(True)
        self.default_scale_spin.blockSignals(True)
        numerator = float(sheet.get("default_scale_numerator", 1.0))
        denominator = float(sheet.get("default_scale", 1.0))
        self.default_scale_numerator_spin.setValue(numerator)
        self.default_scale_spin.setValue(denominator)
        self.default_scale_numerator_spin.blockSignals(False)
        self.default_scale_spin.blockSignals(False)
        self.projection_method_combo.blockSignals(True)
        self.projection_method_combo.setCurrentIndex(max(
            0,
            self.projection_method_combo.findData(
                str(sheet.get("projection_method", "first_angle"))
            ),
        ))
        self.projection_method_combo.blockSignals(False)
        self._refresh_family_instance_combo()
        self._load_active_format()
        self._load_active_title_block()
        self._apply_title_block_context()
        self.canvas.set_sheet(sheet, fit=fit)

    def _load_active_format(self) -> None:
        sheet = self.active_sheet()
        definition = sheet.get("format_definition") if sheet else None
        self.canvas.set_format_definition(
            copy.deepcopy(definition) if isinstance(definition, dict) else None
        )

    def _choose_format(self) -> None:
        file_name, _selected_filter = QFileDialog.getOpenFileName(
            self,
            tr("drawing.file.select_format"),
            str(self.formats_directory),
            tr("drawing.file.filter.format"),
        )
        if not file_name:
            return
        path = Path(file_name)
        try:
            definition = load_drawing_format(path)
        except (OSError, ValueError, configparser.Error) as exc:
            QMessageBox.warning(self, tr("drawing.file.invalid_format"), str(exc))
            return
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet["format_source_name"] = path.name
        sheet["format_definition"] = copy.deepcopy(definition)
        sheet["format"] = definition["sheet_format"]
        sheet["orientation"] = definition["orientation"]
        sheet["document_type"] = definition["document_type"]
        self._refresh_controls(fit=True)
        self._store()

    def _load_active_title_block(self) -> None:
        sheet = self.active_sheet()
        definition = sheet.get("title_block_definition") if sheet else None
        self.canvas.set_title_block_definition(
            copy.deepcopy(definition) if isinstance(definition, dict) else None
        )

    def _choose_title_block(self) -> None:
        file_name, _selected_filter = QFileDialog.getOpenFileName(
            self,
            tr("drawing.file.select_title_block"),
            str(self.title_blocks_directory),
            tr("drawing.file.filter.title_block"),
        )
        if not file_name:
            return
        path = Path(file_name)
        try:
            definition = load_title_block(path)
        except (OSError, ValueError, configparser.Error) as exc:
            QMessageBox.warning(
                self, tr("drawing.file.invalid_title_block"), str(exc)
            )
            return
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet["title_block_source_name"] = path.name
        sheet["title_block_definition"] = copy.deepcopy(definition)
        self.canvas.set_title_block_definition(definition)
        self.remove_title_block_button.setEnabled(True)
        self._store()

    def _remove_format(self) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet.pop("format_definition", None)
        sheet.pop("format_source_name", None)
        sheet.pop("document_type", None)
        self.canvas.set_format_definition(None)
        self.remove_format_button.setEnabled(False)
        self._store()

    def _remove_title_block(self) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet.pop("title_block_definition", None)
        sheet.pop("title_block_source_name", None)
        self.canvas.set_title_block_definition(None)
        self.remove_title_block_button.setEnabled(False)
        self._store()

    def _store(self) -> None:
        if self.document is None:
            return
        store_drawing_sheets(self.document, self.sheets)
        self.document.document_settings["active_sheet"] = str(self.active_sheet_index)
        self.changed.emit()

    def _select_sheet(self, index: int) -> None:
        if not 0 <= index < len(self.sheets):
            return
        self.active_sheet_index = index
        self._load_active_format()
        self._load_active_title_block()
        self._apply_title_block_context()
        self.canvas.set_sheet(self.sheets[index], fit=True)
        self._store()
        self.activeSheetChanged.emit()

    def add_sheet(self) -> None:
        self.sheets.append(default_sheet(len(self.sheets) + 1))
        self.active_sheet_index = len(self.sheets) - 1
        self._refresh_controls(fit=True)
        self._store()

    def remove_sheet(self) -> None:
        if len(self.sheets) <= 1:
            return
        self.sheets.pop(self.active_sheet_index)
        self.active_sheet_index = min(self.active_sheet_index, len(self.sheets) - 1)
        self._refresh_controls(fit=True)
        self._store()

    def remove_sheets(self, sheet_ids: set[str]) -> None:
        if not sheet_ids:
            return
        remaining = [
            sheet for sheet in self.sheets
            if str(sheet.get("id", "")) not in sheet_ids
        ]
        if len(remaining) == len(self.sheets):
            return
        self.sheets = remaining or [default_sheet()]
        self.active_sheet_index = min(
            self.active_sheet_index,
            len(self.sheets) - 1,
        )
        self._refresh_controls(fit=True)
        self._store()
        self.activeSheetChanged.emit()

    def _change_format(self, value: str) -> None:
        sheet = self.active_sheet()
        if sheet is None or value not in SHEET_FORMATS:
            return
        had_frame = bool(sheet.get("format_definition"))
        sheet["format"] = value
        sheet["orientation"] = "portrait" if value == "A4" else "landscape"
        if had_frame:
            matching_frame = self.formats_directory / f"ZE-{value}.frmz"
            if matching_frame.is_file():
                try:
                    definition = load_drawing_format(matching_frame)
                except (OSError, ValueError, configparser.Error):
                    sheet.pop("format_definition", None)
                    sheet.pop("format_source_name", None)
                else:
                    sheet["format_definition"] = copy.deepcopy(definition)
                    sheet["format_source_name"] = matching_frame.name
            else:
                sheet.pop("format_definition", None)
                sheet.pop("format_source_name", None)
        self._load_active_format()
        self._apply_title_block_context()
        self.remove_format_button.setEnabled(bool(sheet.get("format_definition")))
        self.canvas.set_sheet(sheet, fit=True)
        self._store()

    def _change_default_scale(self, _value: float) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet["default_scale_numerator"] = (
            self.default_scale_numerator_spin.value()
        )
        sheet["default_scale"] = self.default_scale_spin.value()
        scale = self.default_scale_numerator_spin.value() / max(
            self.default_scale_spin.value(), 0.001
        )
        for view in sheet.get("views", []):
            if str(view.get("scale_mode", "sheet")) == "sheet":
                view["scale"] = scale
                update_view_bounds(view)
        self._apply_title_block_context()
        self.canvas.update()
        self._store()

    def _change_lineweight_mode(self, _index: int) -> None:
        self.canvas.set_lineweight_preview(
            bool(self.lineweight_combo.currentData())
        )

    def _change_projection_method(self, _index: int) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet["projection_method"] = str(
            self.projection_method_combo.currentData()
        )
        self._store()

    def _change_family_instance(self, _index: int) -> None:
        sheet = self.active_sheet()
        instance = self.family_instance_combo.currentData()
        if sheet is None or instance is None:
            return
        sheet["family_instance"] = str(instance)
        self._store()

    def begin_view_placement(self, view: dict) -> None:
        pending = dict(view)
        sheet = self.active_sheet()
        numerator = (
            float(sheet.get("default_scale_numerator", 1.0))
            if sheet else 1.0
        )
        denominator = float(sheet.get("default_scale", 1.0)) if sheet else 1.0
        if str(pending.get("scale_mode", "sheet")) == "sheet":
            pending["scale"] = numerator / max(denominator, 0.001)
        self._pending_view = pending
        self.canvas.begin_placement(pending)

    def _place_pending_view(self, x: float, y: float) -> None:
        sheet = self.active_sheet()
        if sheet is None or self._pending_view is None:
            return
        view = dict(self._pending_view)
        parent_position = view.get("parent_position")
        projection_direction = str(view.get("projection_direction", ""))
        view.pop("projection_variants", None)
        view.pop("parent_position", None)
        if isinstance(parent_position, (list, tuple)) and len(parent_position) == 2:
            parent_x = float(parent_position[0])
            parent_y = float(parent_position[1])
            ray_x, ray_y = projection_placement_vector(projection_direction)
            distance = (x - parent_x) * ray_x + (y - parent_y) * ray_y
            x = parent_x + distance * ray_x
            y = parent_y + distance * ray_y
        view["x"] = x
        view["y"] = y
        update_view_bounds(view)
        sheet.setdefault("views", []).append(view)
        self._pending_view = None
        self.canvas.cancel_placement()
        self._store()
        # Rebind the active sheet after the first placement as well.  A plain
        # repaint can leave a freshly created, previously empty sheet showing
        # only its old canvas state until the document is reopened.
        self.canvas.set_sheet(sheet, fit=False)
        self.viewPlaced.emit(str(view.get("id", "")))

    def find_view(self, view_id: str) -> dict | None:
        sheet = self.active_sheet()
        if sheet is None:
            return None
        return next(
            (view for view in sheet.get("views", []) if str(view.get("id", "")) == view_id),
            None,
        )

    def _delete_view(self, view_id: str) -> None:
        sheet = self.active_sheet()
        if sheet is None or self.find_view(view_id) is None:
            return
        removed = delete_drawing_view(sheet, view_id)
        if self.canvas._selected_view_id in removed:
            self.canvas._selected_view_id = None
        if self.canvas._hovered_view_id in removed:
            self.canvas._hovered_view_id = None
        self.canvas.update()
        self._store()

    def update_view(self, view_id: str, values: dict) -> bool:
        view = self.find_view(view_id)
        if view is None:
            return False
        view.update(values)
        update_view_bounds(view)
        self.canvas.update()
        self._store()
        return True

    def fit_sheet(self) -> None:
        self.canvas.fit_sheet()

    def animate_fit_sheet(self) -> None:
        self.canvas.animate_fit_sheet()
