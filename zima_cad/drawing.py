from __future__ import annotations

import configparser
import json
from math import atan2, cos, degrees, hypot, radians, sin, sqrt
from pathlib import Path
from typing import Any
from uuid import uuid4

from OCC.Core.HLRAlgo import HLRAlgo_Projector
from OCC.Core.HLRBRep import HLRBRep_Algo, HLRBRep_HLRToShape
from OCC.Core.gp import gp_Ax2, gp_Dir, gp_Pnt
from zima_cad.viewer_mesh import (
    ViewerMesh,
    combine_viewer_meshes,
    edge_visible_in_display,
    silhouette_segments,
)

from PySide6.QtCore import (
    QEasingCurve,
    QPoint,
    QPointF,
    QRectF,
    Qt,
    QVariantAnimation,
    Signal,
)
from PySide6.QtGui import (
    QColor,
    QMouseEvent,
    QPainter,
    QPen,
    QPolygonF,
    QWheelEvent,
)
from PySide6.QtWidgets import (
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
from zima_cad.localization import tr
from zima_cad.model import PartDocument
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
    """Classify exact visible/hidden edges and return drawing-space curves."""
    wireframe = _project_polylines_raw(wire_polylines, orientation)
    valid_shapes = [
        shape for shape in shapes
        if shape is not None and not shape.IsNull()
    ]
    if not valid_shapes:
        wireframe = _center_projected_groups(wireframe)[0]
        return {
            "polylines": wireframe,
            "hidden_polylines": [],
            "wireframe_polylines": wireframe,
            "auxiliary_polylines": [],
        }
    try:
        horizontal, _vertical, depth = projection_axes(orientation)
        coordinate_system = gp_Ax2(
            gp_Pnt(0.0, 0.0, 0.0),
            gp_Dir(*depth),
            gp_Dir(*horizontal),
        )
        algorithm = HLRBRep_Algo()
        for shape in valid_shapes:
            algorithm.Add(shape)
        algorithm.Projector(HLRAlgo_Projector(coordinate_system))
        algorithm.Update()
        algorithm.Hide()
        result = HLRBRep_HLRToShape(algorithm)

        def projected_edges(shape: Any) -> list[list[list[float]]]:
            if shape is None or shape.IsNull():
                return []
            return [
                [[float(x), float(y)] for x, y, _z in edge.points]
                for edge in triangulate_shape(shape).edges
                if len(edge.points) >= 2
            ]

        visible = projected_edges(result.VCompound())
        hidden = projected_edges(result.HCompound())
        outlines = projected_edges(result.OutLineVCompound())
        auxiliary = projected_edges(result.Rg1LineVCompound())
        visible, hidden, wireframe, outlines, auxiliary = _center_projected_groups(
            visible,
            hidden,
            wireframe,
            outlines,
            auxiliary,
        )
        if not visible:
            visible = wireframe
        return {
            "polylines": visible,
            "hidden_polylines": hidden,
            "wireframe_polylines": wireframe,
            "auxiliary_polylines": auxiliary,
        }
    except Exception:
        # Open or otherwise unsupported topology remains usable as wireframe.
        wireframe = _center_projected_groups(wireframe)[0]
        return {
            "polylines": wireframe,
            "hidden_polylines": [],
            "wireframe_polylines": wireframe,
            "auxiliary_polylines": [],
        }


def shaded_projection(
    meshes: list[tuple[ViewerMesh, str]],
    orientation: str | dict,
) -> list[dict[str, Any]]:
    """Project model triangles for shaded technical drawing views."""
    horizontal, vertical, depth_axis = projection_axes(orientation)
    records: list[dict[str, Any]] = []
    all_points: list[list[float]] = []
    for mesh, color in meshes:
        positions = mesh.triangle_positions
        normals = mesh.triangle_normals
        for offset in range(0, len(positions), 9):
            polygon: list[list[float]] = []
            depths: list[float] = []
            normal = [0.0, 0.0, 0.0]
            for vertex in range(3):
                point = tuple(positions[offset + vertex * 3 + axis] for axis in range(3))
                polygon.append([
                    sum(horizontal[axis] * point[axis] for axis in range(3)),
                    sum(vertical[axis] * point[axis] for axis in range(3)),
                ])
                depths.append(sum(depth_axis[axis] * point[axis] for axis in range(3)))
                for axis in range(3):
                    normal[axis] += normals[offset + vertex * 3 + axis] / 3.0
            facing = abs(sum(normal[axis] * depth_axis[axis] for axis in range(3)))
            records.append({
                "points": polygon,
                "depth": sum(depths) / 3.0,
                "color": color,
                "brightness": 0.58 + 0.42 * min(1.0, facing),
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
    scale = float(view.get("scale", 1.0))
    center_x = float(view.get("x", 0.0))
    center_y = float(view.get("y", 0.0))
    points = [point for line in view.get("polylines", []) for point in line]
    if not points:
        bounds = {"left": center_x, "right": center_x, "bottom": center_y, "top": center_y}
    else:
        xs = [center_x - float(point[0]) * scale for point in points]
        ys = [center_y + float(point[1]) * scale for point in points]
        bounds = {"left": min(xs), "right": max(xs), "bottom": min(ys), "top": max(ys)}
    view["bounds"] = bounds
    return bounds


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
    """Move one drawing view to an absolute paper-space position."""
    view = next(
        (
            candidate for candidate in sheet.get("views", [])
            if str(candidate.get("id", "")) == str(view_id)
        ),
        None,
    )
    if view is None:
        return False
    delta_x = float(x) - float(view.get("x", 0.0))
    delta_y = float(y) - float(view.get("y", 0.0))
    view["x"] = float(x)
    view["y"] = float(y)
    update_view_bounds(view)
    for dimension in sheet.get("dimensions", []):
        references = (dimension.get("first"), dimension.get("second"))
        if not any(
            isinstance(reference, dict)
            and str(reference.get("view_id", "")) == str(view_id)
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
    viewDeleteRequested = Signal(str)
    viewMoveFinished = Signal()

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
        self._dimension_tool_active = False
        self._dimension_references: list[dict] = []
        self._dimension_cursor_sheet: tuple[float, float] | None = None
        self._dragged_view_id: str | None = None
        self._drag_start_sheet: tuple[float, float] | None = None
        self._drag_view_start: tuple[float, float] | None = None

    def set_dimension_tool(self, active: bool) -> None:
        self._dimension_tool_active = bool(active)
        self._dimension_references = []
        self._dimension_cursor_sheet = None
        if active:
            self.setCursor(Qt.CursorShape.CrossCursor)
            self.setFocus()
        else:
            self.unsetCursor()
        self.update()

    def set_format_definition(self, definition: dict | None) -> None:
        self._format_definition = definition
        self.update()

    def set_sheet(self, sheet: dict, *, fit: bool = True) -> None:
        self._sheet = sheet
        self._pending_view = None
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

    def animate_fit_sheet(self, duration_ms: int = 650) -> None:
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
        painter.fillRect(self.rect(), QColor("#000000"))
        width, height = self.sheet_size()
        lower_right = self._screen_point(0.0, 0.0)
        upper_left = self._screen_point(width, height)
        painter.setPen(cosmetic_pen(QColor("#4DD811")))
        painter.drawRect(QRectF(
            min(upper_left.x(), lower_right.x()),
            min(upper_left.y(), lower_right.y()),
            abs(lower_right.x() - upper_left.x()),
            abs(lower_right.y() - upper_left.y()),
        ))
        self._draw_format(painter)
        self._draw_origin_indicator(painter)
        for view in self._sheet.get("views", []):
            view_id = str(view.get("id", ""))
            color = (
                QColor("#00D1FF") if view_id == self._selected_view_id
                else QColor("#FF8C00") if view_id == self._hovered_view_id
                else QColor("#FFFFFF")
            )
            self._draw_view(painter, view, color)
            if view_id in {self._selected_view_id, self._hovered_view_id}:
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
        if self._pending_view is not None and self._cursor_sheet_position is not None:
            preview = dict(self._pending_view)
            preview["x"], preview["y"] = self._cursor_sheet_position
            self._draw_view(painter, preview, QColor("#4DD811"))

    def _format_point(self, x_from_left: float, y_from_bottom: float) -> QPointF:
        width, _height = self.sheet_size()
        return self._screen_point(width - x_from_left, y_from_bottom)

    def _draw_format(self, painter: QPainter) -> None:
        definition = self._format_definition
        if not definition:
            return
        width, height = self.sheet_size()
        frame = definition["frame"]
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

    def _draw_origin_indicator(self, painter: QPainter) -> None:
        origin = self._screen_point(15.0, 15.0)
        axis_length = 24.0
        color = QColor("#4DD811")
        painter.setPen(cosmetic_pen(color))
        painter.setBrush(color)
        x_end = QPointF(origin.x() - axis_length, origin.y())
        y_end = QPointF(origin.x(), origin.y() - axis_length)
        painter.drawLine(origin, x_end)
        painter.drawLine(origin, y_end)
        painter.drawPolygon(QPolygonF((
            x_end,
            QPointF(x_end.x() + 6.0, x_end.y() - 3.5),
            QPointF(x_end.x() + 6.0, x_end.y() + 3.5),
        )))
        painter.drawPolygon(QPolygonF((
            y_end,
            QPointF(y_end.x() - 3.5, y_end.y() + 6.0),
            QPointF(y_end.x() + 3.5, y_end.y() + 6.0),
        )))
        painter.drawEllipse(origin, 2.5, 2.5)
        painter.drawText(QPointF(x_end.x() - 11.0, x_end.y() + 4.0), "X")
        painter.drawText(QPointF(y_end.x() + 5.0, y_end.y() + 4.0), "Y")

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
        if display_style == "no_hidden":
            display_style = "wireframe"
        if display_style in {"shaded", "shaded_edges"}:
            # Antialiasing adjacent filled triangles creates hairline cracks.
            painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
            painter.setPen(Qt.PenStyle.NoPen)
            for triangle in view.get("shaded_triangles", []):
                base = QColor(str(triangle.get("color", "#B9C2CC")))
                brightness = float(triangle.get("brightness", 1.0))
                fill = QColor(
                    min(255, round(base.red() * brightness)),
                    min(255, round(base.green() * brightness)),
                    min(255, round(base.blue() * brightness)),
                )
                points = [
                    self._screen_point(
                        center_x - float(point[0]) * scale,
                        center_y + float(point[1]) * scale,
                    )
                    for point in triangle.get("points", [])
                ]
                if len(points) == 3:
                    painter.setBrush(fill)
                    painter.drawPolygon(QPolygonF(points))
            painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            if display_style == "shaded":
                if str(view.get("auxiliary_edges", "hidden")) == "visible":
                    draw_polylines(
                        view.get("auxiliary_polylines", []),
                        cosmetic_pen(color),
                    )
                return
        if display_style == "wireframe":
            draw_polylines(
                view.get("polylines", []),
                cosmetic_pen(color),
            )
            if str(view.get("auxiliary_edges", "hidden")) == "visible":
                draw_polylines(
                    view.get("auxiliary_polylines", []),
                    cosmetic_pen(color),
                )
            return

        draw_polylines(view.get("polylines", []), cosmetic_pen(color))
        if str(view.get("auxiliary_edges", "hidden")) == "visible":
            draw_polylines(
                view.get("auxiliary_polylines", []),
                cosmetic_pen(color),
            )
        if display_style != "hidden_line":
            return
        hidden_style = str(view.get("hidden_lines", "dimmed"))
        if hidden_style == "none":
            return
        hidden_color = QColor("#808080")
        hidden_pen = cosmetic_pen(hidden_color)
        if hidden_style == "dimmed":
            hidden_pen.setStyle(Qt.PenStyle.DashLine)
        draw_polylines(view.get("hidden_polylines", []), hidden_pen)

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

    @staticmethod
    def _view_line_source(view: dict) -> str:
        # ZIMA's wire display follows the model convention: visible topology
        # and silhouettes only, with rear edges removed by the common HLR
        # projection. The raw wireframe remains cached for compatibility but
        # is not the interactive drawing geometry.
        return "polylines"

    @staticmethod
    def _sheet_geometry_point(view: dict, point: list) -> tuple[float, float]:
        scale = float(view.get("scale", 1.0))
        return (
            float(view.get("x", 0.0)) - float(point[0]) * scale,
            float(view.get("y", 0.0)) + float(point[1]) * scale,
        )

    def _resolve_dimension_segment(
        self, reference: dict
    ) -> tuple[tuple[float, float], tuple[float, float]] | None:
        view = self._view_by_id(str(reference.get("view_id", "")))
        if view is None:
            return None
        source = str(reference.get("source", "polylines"))
        polylines = view.get(source, [])
        polyline_index = int(reference.get("polyline", -1))
        segment_index = int(reference.get("segment", -1))
        if not 0 <= polyline_index < len(polylines):
            return None
        polyline = polylines[polyline_index]
        if not 0 <= segment_index < len(polyline) - 1:
            return None
        return (
            self._sheet_geometry_point(view, polyline[segment_index]),
            self._sheet_geometry_point(view, polyline[segment_index + 1]),
        )

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

    def _dimension_segment_at(self, position: QPointF) -> dict | None:
        best: tuple[float, dict] | None = None
        for view in self._sheet.get("views", []):
            source = self._view_line_source(view)
            for polyline_index, polyline in enumerate(view.get(source, [])):
                for segment_index in range(len(polyline) - 1):
                    first = self._screen_point(*self._sheet_geometry_point(
                        view, polyline[segment_index]
                    ))
                    second = self._screen_point(*self._sheet_geometry_point(
                        view, polyline[segment_index + 1]
                    ))
                    distance = self._point_segment_distance(
                        position, first, second
                    )
                    reference = {
                        "view_id": str(view.get("id", "")),
                        "source": source,
                        "polyline": polyline_index,
                        "segment": segment_index,
                    }
                    if distance <= 8.0 and (best is None or distance < best[0]):
                        best = distance, reference
        return best[1] if best is not None else None

    def _draw_dimension_selection(self, painter: QPainter) -> None:
        pen = cosmetic_pen(QColor("#FFD400"))
        painter.setPen(pen)
        for reference in self._dimension_references:
            segment = self._resolve_dimension_segment(reference)
            if segment is not None:
                painter.drawLine(
                    self._screen_point(*segment[0]),
                    self._screen_point(*segment[1]),
                )

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
        painter.setPen(cosmetic_pen(yellow))
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
        half_width = max(2.0, arrow_length / 3.0)
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
        painter.setPen(cosmetic_pen(yellow))
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

    def wheelEvent(self, event: QWheelEvent) -> None:
        before = self._sheet_point(event.position())
        factor = 1.15 if event.angleDelta().y() > 0 else 1.0 / 1.15
        self._pixels_per_mm = max(0.05, min(100.0, self._pixels_per_mm * factor))
        after_screen = self._screen_point(*before)
        self._pan += event.position() - after_screen
        self.update()
        event.accept()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._last_mouse = event.position().toPoint()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._dimension_tool_active:
            if len(self._dimension_references) < 2:
                reference = self._dimension_segment_at(event.position())
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
                    self.update()
                event.accept()
                return
            sheet_position = self._sheet_point(event.position())
            dimension = {
                "id": str(uuid4()),
                "type": "parallel_distance",
                "first": dict(self._dimension_references[0]),
                "second": dict(self._dimension_references[1]),
                "placement": [sheet_position[0], sheet_position[1]],
                "color": "#FFD400",
            }
            self._sheet.setdefault("dimensions", []).append(dimension)
            self._dimension_references = []
            self._dimension_cursor_sheet = None
            self.dimensionCreated.emit()
            self.update()
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._pending_view is not None:
            x, y = self._sheet_point(event.position())
            self.placementRequested.emit(x, y)
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton:
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
            self.update()
        elif self._pending_view is not None:
            self._cursor_sheet_position = self._sheet_point(event.position())
            self._update_projection_preview()
            self.update()
        else:
            view = self._view_at(event.position())
            hovered = str(view.get("id", "")) if view else None
            if hovered != self._hovered_view_id:
                self._hovered_view_id = hovered
                self.update()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton and self._panning:
            self._panning = False
            self.unsetCursor()
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
        if event.button() == Qt.MouseButton.LeftButton and self._pending_view is None:
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
        direction = (
            "right" if abs(dx) >= abs(dy) and dx < 0.0
            else "left" if abs(dx) >= abs(dy)
            else "top" if dy > 0.0
            else "bottom"
        )
        variant = variants.get(direction)
        if isinstance(variant, dict):
            self._pending_view.update(variant)
        if direction in {"left", "right"}:
            self._cursor_sheet_position = (
                self._cursor_sheet_position[0],
                float(parent[1]),
            )
        else:
            self._cursor_sheet_position = (
                float(parent[0]),
                self._cursor_sheet_position[1],
            )


class DrawingWorkspace(QWidget):
    changed = Signal()
    activeSheetChanged = Signal()
    viewPlaced = Signal(str)
    viewDoubleClicked = Signal(str)
    insertViewRequested = Signal()
    projectViewRequested = Signal(str)

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
        self.canvas.dimensionCreated.connect(self._store)
        self.canvas.viewDeleteRequested.connect(self._delete_view)
        self.canvas.viewMoveFinished.connect(self._store)
        self._pending_view: dict | None = None
        self.formats_directory = Path("config/formats")

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
        self.family_instance_combo = QComboBox()
        self.family_instance_combo.currentIndexChanged.connect(
            self._change_family_instance
        )
        self._family_instances: list[str] = []
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
        bottom.addWidget(QLabel("Měřítko:"))
        bottom.addWidget(self.default_scale_numerator_spin)
        bottom.addWidget(QLabel(":"))
        bottom.addWidget(self.default_scale_spin)
        bottom.addWidget(QLabel("Formát:"))
        bottom.addWidget(self.format_combo)
        bottom.addWidget(self.add_format_button)
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
        self._load_active_format()

    def set_document(self, document: PartDocument | None) -> None:
        self.document = document
        self.sheets = drawing_sheets(document) if document is not None else []
        self.active_sheet_index = min(
            int(document.document_settings.get("active_sheet", "0"))
            if document is not None else 0,
            max(0, len(self.sheets) - 1),
        )
        self._refresh_controls(fit=True)

    def active_sheet(self) -> dict | None:
        if 0 <= self.active_sheet_index < len(self.sheets):
            return self.sheets[self.active_sheet_index]
        return None

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
        self.canvas.set_sheet(sheet, fit=fit)

    def _load_active_format(self) -> None:
        sheet = self.active_sheet()
        format_name = str(sheet.get("format_template", "")) if sheet else ""
        if not format_name:
            self.canvas.set_format_definition(None)
            return
        path = Path(format_name)
        if not path.is_absolute():
            path = self.formats_directory / path
        try:
            self.canvas.set_format_definition(load_drawing_format(path))
        except (OSError, ValueError, configparser.Error):
            self.canvas.set_format_definition(None)

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
        try:
            stored_path = path.resolve().relative_to(self.formats_directory)
        except ValueError:
            stored_path = path.resolve()
        sheet["format_template"] = str(stored_path)
        sheet["format"] = definition["sheet_format"]
        sheet["orientation"] = definition["orientation"]
        sheet["document_type"] = definition["document_type"]
        self._refresh_controls(fit=True)
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
        sheet["format"] = value
        sheet["orientation"] = "portrait" if value == "A4" else "landscape"
        sheet.pop("format_template", None)
        self.canvas.set_format_definition(None)
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
        self.canvas.update()
        self._store()

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
        view.pop("projection_direction", None)
        if isinstance(parent_position, (list, tuple)) and len(parent_position) == 2:
            if projection_direction in {"left", "right"}:
                y = float(parent_position[1])
            elif projection_direction in {"top", "bottom"}:
                x = float(parent_position[0])
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
