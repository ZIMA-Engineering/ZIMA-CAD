from __future__ import annotations

from array import array
from dataclasses import dataclass
from math import acos, atan2, cos, degrees, hypot, pi, radians, sin, sqrt, tan
import traceback
from typing import Any

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
    QBrush,
    QColor,
    QMatrix4x4,
    QMouseEvent,
    QPainter,
    QPen,
    QPolygonF,
    QSurfaceFormat,
    QVector3D,
    QWheelEvent,
)
from PySide6.QtOpenGL import (
    QOpenGLBuffer,
    QOpenGLFunctions_3_3_Core,
    QOpenGLShader,
    QOpenGLShaderProgram,
    QOpenGLVertexArrayObject,
)
from PySide6.QtOpenGLWidgets import QOpenGLWidget

from OCC.Core.GeomAPI import GeomAPI_Interpolate
from OCC.Core.gp import gp_Pnt
from OCC.Core.TColgp import TColgp_HArray1OfPnt

from zima_cad.sketch_geometry import center_arc_points, evaluate_corner_radius
from zima_cad.viewer_mesh import Point3, ViewerMesh

TopologyKey = tuple[str, int]


def _interpolated_spline_points(
    points: list[tuple[float, float]]
    | tuple[tuple[float, float], ...],
) -> tuple[tuple[float, float], ...]:
    """Sample the same interpolating spline used by the sketch model."""
    if len(points) < 2:
        return tuple(points)
    try:
        poles = TColgp_HArray1OfPnt(1, len(points))
        for index, point in enumerate(points, 1):
            poles.SetValue(
                index,
                gp_Pnt(float(point[0]), float(point[1]), 0.0),
            )
        interpolation = GeomAPI_Interpolate(poles, False, 1.0e-7)
        interpolation.Perform()
        if not interpolation.IsDone():
            return tuple(points)
        curve = interpolation.Curve()
        first_parameter = curve.FirstParameter()
        parameter_span = curve.LastParameter() - first_parameter
        sample_count = max(32, min(256, (len(points) - 1) * 32))
        sampled: list[tuple[float, float]] = []
        for sample_index in range(sample_count + 1):
            parameter = first_parameter + parameter_span * (
                sample_index / sample_count
            )
            sampled_point = curve.Value(parameter)
            sampled.append((sampled_point.X(), sampled_point.Y()))
        return tuple(sampled)
    except (RuntimeError, TypeError, ValueError):
        return tuple(points)


@dataclass(frozen=True)
class LinearDimension:
    key: str
    first_point: Point3
    second_point: Point3
    first_dimension_point: Point3
    second_dimension_point: Point3
    direction: Point3
    leader_anchor: str = "rightmost"
    value_prefix: str = ""
    value_suffix: str = ""
    display_text: str = ""


@dataclass(frozen=True)
class AngularDimension:
    key: str
    vertex: Point3
    first_direction_point: Point3
    second_direction_point: Point3
    arc_point: Point3
    sweep_degrees: float | None = None
    value_prefix: str = ""
    value_suffix: str = "°"


@dataclass(frozen=True)
class RadialDimension:
    key: str
    center: Point3
    radius_point: Point3
    value_prefix: str = "R"
    value_suffix: str = ""
    display_text: str = ""
    arrow_placement: str = "outside"
    diameter: bool = False


GL_COLOR_BUFFER_BIT = 0x00004000
GL_DEPTH_BUFFER_BIT = 0x00000100
GL_DEPTH_TEST = 0x0B71
GL_LEQUAL = 0x0203
GL_LINE_STRIP = 0x0003
GL_MULTISAMPLE = 0x809D
GL_POLYGON_OFFSET_FILL = 0x8037
GL_TRIANGLES = 0x0004

BACKGROUND_VERTEX_SHADER = """
#version 330 core
out float verticalPosition;
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[gl_VertexID];
    gl_Position = vec4(position, 0.999, 1.0);
    verticalPosition = position.y * 0.5 + 0.5;
}
"""

BACKGROUND_FRAGMENT_SHADER = """
#version 330 core
in float verticalPosition;
uniform vec3 bottomColor;
uniform vec3 topColor;
out vec4 fragmentColor;
void main() {
    fragmentColor = vec4(
        mix(bottomColor, topColor, clamp(verticalPosition, 0.0, 1.0)),
        1.0
    );
}
"""

SURFACE_VERTEX_SHADER = """
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 mvp;
uniform mat4 modelView;
out vec3 cameraNormal;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    cameraNormal = normalize(mat3(modelView) * normal);
}
"""

SURFACE_FRAGMENT_SHADER = """
#version 330 core
in vec3 cameraNormal;
uniform vec3 surfaceColor;
out vec4 fragmentColor;
void main() {
    vec3 lightDirection = normalize(vec3(0.25, -0.35, 0.902));
    float diffuse = max(dot(normalize(cameraNormal), lightDirection), 0.0);
    float brightness = 0.42 + 0.58 * diffuse;
    fragmentColor = vec4(surfaceColor * brightness, 1.0);
}
"""

EDGE_VERTEX_SHADER = """
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
}
"""

EDGE_FRAGMENT_SHADER = """
#version 330 core
uniform vec3 edgeColor;
out vec4 fragmentColor;
void main() {
    fragmentColor = vec4(edgeColor, 1.0);
}
"""


@dataclass
class CameraState:
    """Viewer-owned camera state, independent from OCCT presentation classes."""

    yaw_degrees: float = 35.264
    pitch_degrees: float = -45.0
    pan_x: float = 0.0
    pan_y: float = 0.0
    zoom: float = 1.0


class ZimaOpenGLViewer(QOpenGLWidget):
    """Native ZIMA-CAD viewport.

    Geometry, picking and overlays are owned by this widget without an OCCT
    presentation context.
    """

    navigationChanged = Signal(CameraState)
    viewportResized = Signal(int, int, float)
    hoveredEdgeChanged = Signal(str, int)
    selectedEdgeChanged = Signal(str, int)
    hoveredFaceChanged = Signal(str, int)
    selectedFaceChanged = Signal(str, int)
    hoveredPointChanged = Signal(str, int)
    selectedPointChanged = Signal(str, int)
    hoveredPlaneChanged = Signal(str, int)
    selectedPlaneChanged = Signal(str, int)
    hoveredObjectChanged = Signal(str)
    selectedObjectChanged = Signal(str)
    objectDoubleClicked = Signal(str)
    dimensionsDismissRequested = Signal()
    selectionPreviewConfirmed = Signal()
    selectionFilterChanged = Signal(str)
    displayModeChanged = Signal(str)
    sketchPositionClicked = Signal(float, float)
    sketchReferencePositionClicked = Signal(str, float, float)
    sketchReferenceCycleRequested = Signal(object)
    sketchPlacementClicked = Signal(float, float, str, str)
    sketchReferenceHovered = Signal(str)
    sketchCancelCurrentRequested = Signal()
    sketchAlternateCurrentRequested = Signal()
    sketchConfirmCurrentRequested = Signal()
    sketchFinishCurrentRequested = Signal()
    sketchViewClicked = Signal()
    sketchEntitySelected = Signal(str)
    sketchEntityAdditiveSelected = Signal(str)
    sketchEntitiesSelected = Signal(object)
    sketchCornerRadiusSelected = Signal(str, str, str)
    sketchCornerRadiusDragged = Signal(str, float, float, bool)
    sketchDimensionDragged = Signal(str, float, float, bool)
    sketchDimensionSelected = Signal(str)
    sketchDimensionEditRequested = Signal(str)
    sketchDimensionHovered = Signal(str)
    sketchEntityHovered = Signal(str)
    sketchCursorMoved = Signal(float, float)
    sketchConstraintReferenceSelected = Signal(str)
    sketchExternalReferenceSelected = Signal(str)
    sketchDeleteRequested = Signal()
    sketchArcDirectionSelected = Signal(bool)
    rotation_degrees_per_pixel = 0.18

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        surface_format = QSurfaceFormat()
        surface_format.setVersion(3, 3)
        surface_format.setProfile(QSurfaceFormat.OpenGLContextProfile.CoreProfile)
        surface_format.setDepthBufferSize(24)
        surface_format.setSamples(4)
        self.setFormat(surface_format)
        self.camera = CameraState()
        self._background_top = QColor("#3B4654")
        self._background_bottom = QColor("#171B21")
        self._surface_color = QColor("#B9C2CC")
        self._last_mouse_position: QPoint | None = None
        self._middle_press_position: QPoint | None = None
        self._middle_dragged = False
        self._middle_chorded = False
        self._middle_double_clicked = False
        self._mesh: ViewerMesh | None = None
        self._scene_center: Point3 = (0.0, 0.0, 0.0)
        self._scene_radius = 1.0
        self._gl: QOpenGLFunctions_3_3_Core | None = None
        self._background_program: QOpenGLShaderProgram | None = None
        self._surface_program: QOpenGLShaderProgram | None = None
        self._edge_program: QOpenGLShaderProgram | None = None
        self._surface_buffer: QOpenGLBuffer | None = None
        self._edge_buffer: QOpenGLBuffer | None = None
        self._surface_vao: QOpenGLVertexArrayObject | None = None
        self._edge_vao: QOpenGLVertexArrayObject | None = None
        self._background_vao: QOpenGLVertexArrayObject | None = None
        self._surface_vertex_count = 0
        self._face_ranges: tuple[tuple[str, int, int, int], ...] = ()
        self._edge_ranges: tuple[tuple[int, int], ...] = ()
        self._buffers_dirty = False
        self._gpu_ready = False
        self._hovered_edge: TopologyKey | None = None
        self._selected_edge: TopologyKey | None = None
        self._hovered_face: TopologyKey | None = None
        self._selected_face: TopologyKey | None = None
        self._hovered_point: TopologyKey | None = None
        self._selected_point: TopologyKey | None = None
        self._hovered_plane: TopologyKey | None = None
        self._selected_plane: TopologyKey | None = None
        self._hovered_object_id: str | None = None
        self._selected_object_id: str | None = None
        self._interaction_mode = "object"
        self._selection_filter = "all"
        self._display_mode = "shaded_with_edges"
        self._selection_enabled = True
        self._outline_face_highlights = False
        self._object_overlay_mesh: ViewerMesh | None = None
        self._object_overlay_color = QColor.fromRgbF(1.0, 0.48, 0.0)
        self._object_overlay_persistent = False
        self._object_overlay_anchor: Point3 | None = None
        self._selected_reference_owner_id: str | None = None
        self._constraint_reference_owner_ids: frozenset[str] = frozenset()
        self._constraint_reference_edges: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_points: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_planes: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_positions: tuple[Point3, ...] = ()
        self._selected_container_origin_id: str | None = None
        self._selected_container_content_ids: frozenset[str] = frozenset()
        self._cycled_topology_candidate: tuple[str, str, int] | None = None
        self._selection_preview_pending = False
        self._dimensions: tuple[
            LinearDimension | AngularDimension | RadialDimension, ...
        ] = ()
        self._locked_dimension_keys: frozenset[str] = frozenset()
        self._selected_dimension_key: str | None = None
        self._hovered_dimension_key: str | None = None
        self._suppress_next_context_menu = False
        self._sketch_frame: tuple[Point3, Point3, Point3] | None = None
        self._sketch_entities: tuple[dict[str, Any], ...] = ()
        self._sketch_external_references: tuple[dict[str, Any], ...] = ()
        self._sketch_pending_points: tuple[tuple[float, float], ...] = ()
        self._sketch_tool: str | None = None
        self._sketch_preview_position: tuple[float, float] | None = None
        self._sketch_preview_constraint: str | None = None
        self._sketch_selection_mode = False
        self._sketch_constraint_selection_mode = False
        self._sketch_reference_selection_mode = False
        self._sketch_reference_snapping = False
        self._sketch_arc_clockwise: bool | None = None
        self._sketch_arc_last_angle: float | None = None
        self._sketch_arc_accumulated_sweep = 0.0
        self._selected_sketch_entity_id: str | None = None
        self._selected_sketch_entity_ids: frozenset[str] = frozenset()
        self._selected_sketch_corner_radius: tuple[str, str, str] | None = None
        self._hovered_sketch_corner_radius: tuple[str, str, str] | None = None
        self._sketch_box_start: QPointF | None = None
        self._sketch_box_end: QPointF | None = None
        self._sketch_corner_drag_vertex_id: str | None = None
        self._sketch_corner_drag_moved = False
        self._sketch_dimension_drag_key: str | None = None
        self._sketch_dimension_drag_moved = False
        self._preview_sketch_entity_id: str | None = None
        self._hovered_sketch_external_reference_id: str | None = None
        self._sketch_cycle_ids: tuple[str, ...] = ()
        self._sketch_cycle_index = -1
        self._camera_animation: QVariantAnimation | None = None
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)

    def set_background_colors(
        self,
        top: QColor | str,
        bottom: QColor | str,
    ) -> None:
        self._background_top = QColor(top)
        self._background_bottom = QColor(bottom)
        self.update()

    def set_surface_color(self, color: QColor | str) -> None:
        selected = QColor(color)
        if not selected.isValid():
            return
        self._surface_color = selected
        self.update()

    def surface_color(self) -> QColor:
        return QColor(self._surface_color)

    def set_sketch_overlay(
        self,
        frame: tuple[Point3, Point3, Point3] | None,
        entities: list[dict[str, Any]] | tuple[dict[str, Any], ...] = (),
        pending_points: list[tuple[float, float]]
        | tuple[tuple[float, float], ...] = (),
        *,
        selection_mode: bool = False,
        constraint_selection_mode: bool = False,
        selected_entity_id: str | None = None,
        selected_entity_ids: set[str] | frozenset[str] = frozenset(),
        selected_corner_radius: tuple[str, str, str] | None = None,
        external_references: tuple[dict[str, Any], ...]
        | list[dict[str, Any]] = (),
        snap_to_external_references: bool = False,
        sketch_tool: str | None = None,
    ) -> None:
        self._sketch_frame = frame
        self._sketch_entities = tuple(entities)
        previous_pending = self._sketch_pending_points
        self._sketch_pending_points = tuple(pending_points)
        if (
            sketch_tool != "arc"
            or len(self._sketch_pending_points) != 2
            or previous_pending != self._sketch_pending_points
        ):
            self._sketch_arc_clockwise = None
            self._sketch_arc_last_angle = None
            self._sketch_arc_accumulated_sweep = 0.0
        self._sketch_selection_mode = selection_mode
        self._sketch_constraint_selection_mode = (
            constraint_selection_mode
        )
        self._selected_sketch_entity_id = selected_entity_id
        self._selected_sketch_entity_ids = frozenset(selected_entity_ids)
        self._selected_sketch_corner_radius = selected_corner_radius
        self._sketch_external_references = tuple(external_references)
        self._sketch_reference_snapping = snap_to_external_references
        self._sketch_tool = sketch_tool
        if (
            not snap_to_external_references
            and self._hovered_sketch_external_reference_id is not None
        ):
            self._hovered_sketch_external_reference_id = None
            self.sketchReferenceHovered.emit("")
        reference_ids = {
            str(reference.get("id", ""))
            for reference in self._sketch_external_references
        }
        if self._hovered_sketch_external_reference_id not in reference_ids:
            self._hovered_sketch_external_reference_id = None
        if not selection_mode or frame is None:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
        if selection_mode or frame is None or not pending_points:
            self._sketch_preview_position = None
            self._sketch_preview_constraint = None
        self.update()

    def set_sketch_reference_selection_mode(self, enabled: bool) -> None:
        self._sketch_reference_selection_mode = enabled
        if enabled:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
        self.update()

    def center_on_world_point(self, point: Point3) -> None:
        camera_point = self._camera_point(point)
        scale = (
            float(self.height())
            * 0.5
            / max(self._scene_radius, 1e-9)
            * self.camera.zoom
        )
        self.camera.pan_x = -camera_point[0] * scale
        self.camera.pan_y = camera_point[1] * scale
        self.navigationChanged.emit(self.camera)
        self.update()

    def sketch_snap_tolerance(self, pixels: float = 10.0) -> float:
        units_per_pixel = (
            self._scene_radius
            * 2.0
            / max(1.0, float(self.height()))
            / max(self.camera.zoom, 1e-9)
        )
        return max(1.0e-9, float(pixels) * units_per_pixel)

    def reset_camera(self) -> None:
        self.camera = CameraState()
        self._buffers_dirty = True
        self.navigationChanged.emit(self.camera)
        self.update()

    def set_standard_view(self, view_name: str) -> None:
        orientations = {
            "default": (35.264, -45.0),
            "front": (0.0, -90.0),
            "back": (0.0, 90.0),
            "left": (-90.0, -90.0),
            "right": (90.0, -90.0),
            "top": (0.0, 0.0),
            "bottom": (0.0, 180.0),
        }
        if view_name not in orientations:
            raise ValueError(f"Unknown standard view: {view_name}")
        yaw, pitch = orientations[view_name]
        self.camera.yaw_degrees = yaw
        self.camera.pitch_degrees = pitch
        self.camera.pan_x = 0.0
        self.camera.pan_y = 0.0
        self.navigationChanged.emit(self.camera)
        self.update()

    def set_view_normal(self, normal: Point3) -> None:
        nx, ny, nz = normal
        length = sqrt(nx * nx + ny * ny + nz * nz)
        if length <= 1e-12:
            return
        nx, ny, nz = nx / length, ny / length, nz / length
        horizontal = hypot(nx, ny)
        self.camera.yaw_degrees = (
            degrees(atan2(nx, ny)) if horizontal > 1e-12 else 0.0
        )
        self.camera.pitch_degrees = -degrees(atan2(horizontal, nz))
        self.camera.pan_x = 0.0
        self.camera.pan_y = 0.0
        self.camera.zoom = 1.0
        self.navigationChanged.emit(self.camera)
        self.update()

    def animate_view_normal(
        self,
        normal: Point3,
        center_point: Point3 | None = None,
        *,
        duration_ms: int = 1000,
    ) -> None:
        nx, ny, nz = normal
        length = sqrt(nx * nx + ny * ny + nz * nz)
        if length <= 1e-12:
            return
        nx, ny, nz = nx / length, ny / length, nz / length
        horizontal = hypot(nx, ny)
        target_yaw = (
            degrees(atan2(nx, ny)) if horizontal > 1e-12 else 0.0
        )
        target_pitch = -degrees(atan2(horizontal, nz))
        target_zoom = 1.0

        target_center = (
            self._scene_center
            if center_point is None
            else center_point
        )
        relative = tuple(
            target_center[axis] - self._scene_center[axis]
            for axis in range(3)
        )
        yaw = radians(target_yaw)
        pitch = radians(target_pitch)
        yaw_x = cos(yaw) * relative[0] - sin(yaw) * relative[1]
        yaw_y = sin(yaw) * relative[0] + cos(yaw) * relative[1]
        rotated_x = yaw_x
        rotated_y = cos(pitch) * yaw_y - sin(pitch) * relative[2]
        scale = (
            float(self.height())
            * 0.5
            / max(self._scene_radius, 1e-9)
            * target_zoom
        )
        target_pan_x = -rotated_x * scale
        target_pan_y = rotated_y * scale

        self._stop_camera_animation()
        start_yaw = self.camera.yaw_degrees
        yaw_delta = (
            (target_yaw - start_yaw + 180.0) % 360.0
        ) - 180.0
        start_pitch = self.camera.pitch_degrees
        start_pan_x = self.camera.pan_x
        start_pan_y = self.camera.pan_y
        start_zoom = self.camera.zoom
        animation = QVariantAnimation(self)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setDuration(max(1, int(duration_ms)))
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)

        def apply_progress(raw_progress) -> None:
            progress = float(raw_progress)
            self.camera.yaw_degrees = start_yaw + yaw_delta * progress
            self.camera.pitch_degrees = (
                start_pitch
                + (target_pitch - start_pitch) * progress
            )
            self.camera.pan_x = (
                start_pan_x
                + (target_pan_x - start_pan_x) * progress
            )
            self.camera.pan_y = (
                start_pan_y
                + (target_pan_y - start_pan_y) * progress
            )
            self.camera.zoom = (
                start_zoom + (target_zoom - start_zoom) * progress
            )
            self.navigationChanged.emit(self.camera)
            self.update()

        def finish_animation() -> None:
            if self._camera_animation is animation:
                self._camera_animation = None

        animation.valueChanged.connect(apply_progress)
        animation.finished.connect(finish_animation)
        self._camera_animation = animation
        animation.start()

    def _stop_camera_animation(self) -> None:
        animation = self._camera_animation
        self._camera_animation = None
        if animation is not None:
            animation.stop()

    def fit_all(self) -> None:
        self.camera.pan_x = 0.0
        self.camera.pan_y = 0.0
        self.camera.zoom = 1.0
        self._buffers_dirty = True
        self.navigationChanged.emit(self.camera)
        self.update()

    def set_display_mode(self, display_mode: str) -> None:
        if display_mode not in {"wire", "shaded_with_edges", "shaded"}:
            raise ValueError(f"Unknown Viewer display mode: {display_mode}")
        if display_mode == self._display_mode:
            return
        self._display_mode = display_mode
        self.displayModeChanged.emit(display_mode)
        self.update()

    @property
    def display_mode(self) -> str:
        return self._display_mode

    def set_mesh(self, mesh: ViewerMesh | None, *, fit: bool = True) -> None:
        self._mesh = mesh
        self._set_hovered_edge(None)
        self._set_selected_edge(None)
        self._set_hovered_face(None)
        self._set_selected_face(None)
        self._set_hovered_point(None)
        self._set_selected_point(None)
        self._set_hovered_plane(None)
        self._set_selected_plane(None)
        self._buffers_dirty = True
        if mesh is not None and not mesh.is_empty:
            self._scene_center = tuple(
                (mesh.bounds_min[axis] + mesh.bounds_max[axis]) * 0.5
                for axis in range(3)
            )
            diagonal = sqrt(
                sum(
                    (mesh.bounds_max[axis] - mesh.bounds_min[axis]) ** 2
                    for axis in range(3)
                )
            )
            self._scene_radius = max(diagonal * 0.5, 1e-6)
        else:
            self._scene_center = (0.0, 0.0, 0.0)
            self._scene_radius = 1.0
        if fit:
            self.reset_camera()
        else:
            self.update()

    def clear_scene(self) -> None:
        self.set_mesh(None)

    def set_object_overlay(
        self,
        mesh: ViewerMesh | None,
        *,
        selected: bool = False,
        anchor: Point3 | None = None,
    ) -> None:
        self._object_overlay_mesh = mesh
        self._object_overlay_anchor = anchor
        self._object_overlay_persistent = selected
        self._object_overlay_color = QColor.fromRgbF(
            0.0, 0.82, 1.0
        ) if selected else QColor.fromRgbF(1.0, 0.48, 0.0)
        self.update()

    def set_selected_reference_owner(self, owner_id: str | None) -> None:
        self._selected_reference_owner_id = owner_id
        self.update()

    def set_constraint_reference_highlights(
        self,
        *,
        owner_ids: set[str],
        edges: set[TopologyKey],
        points: set[TopologyKey],
        planes: set[TopologyKey],
        positions: set[Point3],
    ) -> None:
        self._constraint_reference_owner_ids = frozenset(owner_ids)
        self._constraint_reference_edges = frozenset(edges)
        self._constraint_reference_points = frozenset(points)
        self._constraint_reference_planes = frozenset(planes)
        self._constraint_reference_positions = tuple(positions)
        self.update()

    def set_selected_container_origin(self, origin_id: str | None) -> None:
        self._selected_container_origin_id = origin_id
        self.update()

    def set_selected_container_contents(
        self,
        owner_ids: set[str] | frozenset[str],
    ) -> None:
        self._selected_container_content_ids = frozenset(owner_ids)
        self.update()

    def set_selection_preview_pending(self, pending: bool) -> None:
        self._selection_preview_pending = pending

    def set_dimensions(
        self,
        dimensions: tuple[
            LinearDimension | AngularDimension | RadialDimension, ...
        ],
    ) -> None:
        self._dimensions = dimensions
        self._locked_dimension_keys &= frozenset(
            dimension.key for dimension in dimensions
        )
        if self._selected_dimension_key not in {
            dimension.key for dimension in dimensions
        }:
            self._selected_dimension_key = None
        if self._hovered_dimension_key not in {
            dimension.key for dimension in dimensions
        }:
            self._hovered_dimension_key = None
        self.update()

    def set_locked_dimension_keys(
        self,
        keys: set[str] | frozenset[str],
    ) -> None:
        self._locked_dimension_keys = frozenset(keys)
        self.update()

    def set_selected_dimension(self, key: str | None) -> None:
        self._selected_dimension_key = (
            key
            if any(dimension.key == key for dimension in self._dimensions)
            else None
        )
        self.update()

    def set_hovered_dimension(self, key: str | None) -> None:
        self._hovered_dimension_key = (
            key
            if any(dimension.key == key for dimension in self._dimensions)
            else None
        )
        self.update()

    def dimension_key_at(
        self,
        position: QPointF,
        tolerance: float = 8.0,
    ) -> str | None:
        def distance_to_segment(
            point: QPointF,
            first: QPointF,
            second: QPointF,
        ) -> float:
            dx = second.x() - first.x()
            dy = second.y() - first.y()
            squared_length = dx * dx + dy * dy
            if squared_length <= 1.0e-12:
                return hypot(
                    point.x() - first.x(),
                    point.y() - first.y(),
                )
            factor = max(
                0.0,
                min(
                    1.0,
                    (
                        (point.x() - first.x()) * dx
                        + (point.y() - first.y()) * dy
                    )
                    / squared_length,
                ),
            )
            return hypot(
                point.x() - (first.x() + factor * dx),
                point.y() - (first.y() + factor * dy),
            )

        for dimension in reversed(self._dimensions):
            geometry = self._dimension_screen_geometry(dimension)
            value_position = geometry.get("value_position")
            if (
                isinstance(value_position, QPointF)
                and abs(position.x() - value_position.x()) <= 60.0
                and abs(position.y() - value_position.y()) <= 16.0
            ):
                return dimension.key
            segments: list[tuple[QPointF, QPointF]] = []
            if geometry.get("radial"):
                segments.append((
                    geometry.get("radial_start", geometry["center"]),
                    geometry["radial_end"],
                ))
                if geometry.get("shoulder_end") is not None:
                    segments.append((
                        geometry["shoulder_start"],
                        geometry["shoulder_end"],
                    ))
            elif geometry.get("angular"):
                arc = geometry.get("arc", ())
                segments.extend(
                    (arc[index - 1], arc[index])
                    for index in range(1, len(arc))
                )
                for endpoint in ("first_dimension", "second_dimension"):
                    if endpoint in geometry:
                        segments.append((
                            geometry["vertex"],
                            geometry[endpoint],
                        ))
            else:
                for first_key, second_key in (
                    ("first", "first_dimension"),
                    ("second", "second_dimension"),
                    ("first_dimension", "second_dimension"),
                    ("first_arrow_base", "first_tail"),
                    ("second_arrow_base", "second_tail"),
                    ("leader_start", "leader_end"),
                ):
                    if first_key in geometry and second_key in geometry:
                        segments.append((
                            geometry[first_key],
                            geometry[second_key],
                        ))
            if any(
                distance_to_segment(position, first, second) <= tolerance
                for first, second in segments
            ):
                return dimension.key
        return None

    def dimension_value_position(self, key: str) -> QPointF | None:
        dimension = next(
            (item for item in self._dimensions if item.key == key),
            None,
        )
        if dimension is None:
            return None
        geometry = self._dimension_screen_geometry(dimension)
        return geometry["value_position"] if geometry is not None else None

    def mesh_is_under_cursor(
        self,
        mesh: ViewerMesh,
        position: QPointF,
    ) -> bool:
        positions = mesh.triangle_positions
        for triangle_index in range(len(mesh.triangle_face_indices)):
            offset = triangle_index * 9
            camera_points = [
                self._camera_point(
                    (
                        positions[offset + vertex * 3],
                        positions[offset + vertex * 3 + 1],
                        positions[offset + vertex * 3 + 2],
                    )
                )
                for vertex in range(3)
            ]
            if self._triangle_weights(
                position,
                *(self._screen_point(point) for point in camera_points),
            ) is not None:
                return True
        return False

    def set_selection_filter(self, selection_filter: str) -> None:
        if selection_filter not in {
            "all", "face", "point", "axis", "plane", "normal",
        }:
            raise ValueError(f"Unknown Viewer selection filter: {selection_filter}")
        if selection_filter == self._selection_filter:
            return
        self._selection_filter = selection_filter
        self._set_hovered_edge(None)
        self._set_hovered_face(None)
        self._set_hovered_point(None)
        self._set_selected_edge(None)
        self._set_selected_face(None)
        self._set_selected_point(None)
        self._set_hovered_plane(None)
        self._set_selected_plane(None)
        self.selectionFilterChanged.emit(selection_filter)

    def set_interaction_mode(self, interaction_mode: str) -> None:
        if interaction_mode not in {"object", "topology"}:
            raise ValueError(
                f"Unknown Viewer interaction mode: {interaction_mode}"
            )
        if interaction_mode == self._interaction_mode:
            return
        self._interaction_mode = interaction_mode
        self._set_hovered_object(None)
        self._set_selected_object(None)
        self._clear_topology_hover()
        self._clear_topology_selection()

    @property
    def selection_filter(self) -> str:
        return self._selection_filter

    def set_selection_enabled(self, enabled: bool) -> None:
        self._selection_enabled = bool(enabled)
        if not enabled:
            self._set_hovered_edge(None)
            self._set_hovered_face(None)
            self._set_hovered_point(None)
            self._set_hovered_plane(None)

    def set_outline_face_highlights(self, enabled: bool) -> None:
        self._outline_face_highlights = bool(enabled)
        self.update()

    def initializeGL(self) -> None:
        self._gl = QOpenGLFunctions_3_3_Core()
        if not self._gl.initializeOpenGLFunctions():
            raise RuntimeError("OpenGL 3.3 core functions are unavailable")
        self._background_program = self._create_program(
            BACKGROUND_VERTEX_SHADER,
            BACKGROUND_FRAGMENT_SHADER,
        )
        self._surface_program = self._create_program(
            SURFACE_VERTEX_SHADER,
            SURFACE_FRAGMENT_SHADER,
        )
        self._edge_program = self._create_program(
            EDGE_VERTEX_SHADER,
            EDGE_FRAGMENT_SHADER,
        )
        self._surface_buffer = QOpenGLBuffer(
            QOpenGLBuffer.Type.VertexBuffer
        )
        self._edge_buffer = QOpenGLBuffer(
            QOpenGLBuffer.Type.VertexBuffer
        )
        self._surface_vao = QOpenGLVertexArrayObject()
        self._edge_vao = QOpenGLVertexArrayObject()
        self._background_vao = QOpenGLVertexArrayObject()
        if not self._surface_buffer.create() or not self._edge_buffer.create():
            raise RuntimeError("Unable to create Viewer OpenGL buffers")
        if (
            not self._surface_vao.create()
            or not self._edge_vao.create()
            or not self._background_vao.create()
        ):
            raise RuntimeError("Unable to create Viewer OpenGL vertex arrays")
        self._gpu_ready = True
        self._buffers_dirty = True
        self.context().aboutToBeDestroyed.connect(
            self._release_graphics_resources
        )

    def resizeGL(self, width: int, height: int) -> None:
        self.viewportResized.emit(
            width,
            height,
            float(self.devicePixelRatioF()),
        )

    def paintGL(self) -> None:
        try:
            self._paint_gl_scene()
        except Exception:
            traceback.print_exc()
            self._gpu_ready = False

    def _paint_gl_scene(self) -> None:
        if not self._gpu_ready or self._gl is None:
            return
        if self._buffers_dirty:
            self._upload_mesh_buffers()
        gl = self._gl
        background = self._background_bottom
        gl.glClearColor(
            background.redF(),
            background.greenF(),
            background.blueF(),
            1.0,
        )
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        self._draw_background()
        mesh = self._mesh
        if mesh is None or mesh.is_empty:
            return
        gl.glEnable(GL_DEPTH_TEST)
        gl.glDepthFunc(GL_LEQUAL)
        gl.glEnable(GL_MULTISAMPLE)
        model_view, mvp = self._camera_matrices()
        if self._display_mode != "wire":
            self._draw_surfaces(model_view, mvp)
        self._draw_edges(
            mvp,
            draw_base_edges=self._display_mode != "shaded",
        )

    def paintEvent(self, event) -> None:
        super().paintEvent(event)
        self._paint_centerlines()
        self._paint_object_highlights()
        self._paint_reference_highlights()
        self._paint_face_highlight_outlines()
        self._paint_planes()
        self._paint_points()
        self._paint_dimensions()
        self._paint_sketch_overlay()
        self._paint_sketch_selection_box()
        self._paint_object_overlay()
        self._paint_edge_labels()

    def _paint_face_highlight_outlines(self) -> None:
        mesh = self._mesh
        if not self._outline_face_highlights or mesh is None:
            return
        highlights = (
            (
                self._hovered_face,
                QColor.fromRgbF(1.0, 0.48, 0.0),
            ),
            (
                self._selected_face,
                QColor.fromRgbF(0.0, 0.82, 1.0),
            ),
        )
        if not any(face is not None for face, _color in highlights):
            return
        positions = mesh.triangle_positions
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for highlighted_face, color in highlights:
            if highlighted_face is None:
                continue
            boundary_counts: dict[
                tuple[Point3, Point3],
                tuple[int, Point3, Point3],
            ] = {}
            for triangle_index, face_index in enumerate(
                mesh.triangle_face_indices
            ):
                owner_id = mesh.triangle_owner_ids[triangle_index]
                if (owner_id, face_index) != highlighted_face:
                    continue
                offset = triangle_index * 9
                points = tuple(
                    tuple(
                        float(positions[offset + vertex * 3 + axis])
                        for axis in range(3)
                    )
                    for vertex in range(3)
                )
                for first, second in (
                    (points[0], points[1]),
                    (points[1], points[2]),
                    (points[2], points[0]),
                ):
                    key = tuple(sorted((first, second)))
                    count, _, _ = boundary_counts.get(
                        key,
                        (0, first, second),
                    )
                    boundary_counts[key] = (count + 1, first, second)
            painter.setPen(QPen(color, 3.0))
            for count, first, second in boundary_counts.values():
                if count == 1:
                    painter.drawLine(
                        self._screen_point(self._camera_point(first)),
                        self._screen_point(self._camera_point(second)),
                    )
        painter.end()

    def keyPressEvent(self, event) -> None:
        if (
            self._sketch_frame is not None
            and event.key() == Qt.Key.Key_Delete
        ):
            self.sketchDeleteRequested.emit()
            event.accept()
            return
        super().keyPressEvent(event)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._stop_camera_animation()
        if (
            self._sketch_frame is not None
            and event.button() == Qt.MouseButton.LeftButton
        ):
            self.sketchViewClicked.emit()
        if (
            event.button() == Qt.MouseButton.RightButton
            and self._sketch_frame is not None
            and self._sketch_reference_selection_mode
            and not (event.buttons() & Qt.MouseButton.MiddleButton)
        ):
            # Reference picking needs deterministic right-click cycling.  Do
            # not depend on Qt creating a context-menu event after the press;
            # suppress that later event so one click advances exactly once.
            self._suppress_next_context_menu = True
            self.sketchReferenceCycleRequested.emit(event.position())
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.RightButton
            and self._sketch_frame is not None
            and not self._sketch_reference_selection_mode
        ):
            self._suppress_next_context_menu = True
            if (
                self._sketch_constraint_selection_mode
                and self._sketch_tool in ("dimension", "midpoint")
            ):
                if (
                    self._sketch_tool == "dimension"
                    and any(
                        dimension.key == "sketch_dimension_preview"
                        for dimension in self._dimensions
                    )
                ):
                    self.sketchAlternateCurrentRequested.emit()
                else:
                    self._cycle_sketch_entity(event.position())
            elif self._sketch_constraint_selection_mode:
                self.sketchAlternateCurrentRequested.emit()
            elif self._sketch_selection_mode:
                corner_radius = self._corner_radius_candidate(
                    event.position()
                )
                if corner_radius is not None:
                    # A right-click directly on the fillet opens its context
                    # menu without requiring a preceding left-click.
                    self.sketchCornerRadiusSelected.emit(*corner_radius)
                    self._suppress_next_context_menu = False
                    super().mousePressEvent(event)
                    return
                candidates = self._sketch_selection_candidates(
                    event.position()
                )
                if len(candidates) > 1:
                    self._cycle_sketch_entity(event.position())
                elif (
                    self._selected_sketch_entity_id is not None
                    or self._selected_sketch_corner_radius is not None
                ):
                    self._suppress_next_context_menu = False
                    super().mousePressEvent(event)
                    return
                else:
                    self._cycle_sketch_entity(event.position())
            else:
                self.sketchCancelCurrentRequested.emit()
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_frame is not None
            and not self._sketch_reference_selection_mode
        ):
            if self._sketch_selection_mode:
                if self._sketch_tool in ("select", "dimension"):
                    dimension_key = self.dimension_key_at(event.position())
                    geometry_candidates = self._sketch_selection_candidates(
                        event.position()
                    )
                    point_ids = {
                        str(entity.get("id", ""))
                        for entity in self._sketch_entities
                        if entity.get("type") == "point"
                    }
                    point_is_candidate = any(
                        candidate in point_ids
                        for candidate in geometry_candidates
                    )
                    if (
                        dimension_key is not None
                        and dimension_key != "sketch_dimension_preview"
                        and not point_is_candidate
                        and (
                            self._sketch_tool == "dimension"
                            or not geometry_candidates
                        )
                    ):
                        self.sketchDimensionSelected.emit(dimension_key)
                        self._sketch_dimension_drag_key = dimension_key
                        self._sketch_dimension_drag_moved = False
                        event.accept()
                        return
                if (
                    not self._sketch_constraint_selection_mode
                    or self._sketch_tool
                    in ("equal", "dimension")
                ):
                    if (
                        not self._sketch_constraint_selection_mode
                        and self._selected_sketch_corner_radius is not None
                    ):
                        drag_vertex = self._corner_radius_drag_candidate(
                            event.position()
                        )
                        if drag_vertex is not None:
                            self._sketch_corner_drag_vertex_id = drag_vertex
                            self._sketch_corner_drag_moved = False
                            event.accept()
                            return
                    corner_radius = self._corner_radius_candidate(
                        event.position()
                    )
                    if corner_radius is not None:
                        self.sketchCornerRadiusSelected.emit(*corner_radius)
                        event.accept()
                        return
                if not self._sketch_constraint_selection_mode:
                    drag_vertex = self._corner_radius_drag_candidate(
                        event.position()
                    )
                    if drag_vertex is not None:
                        self._sketch_corner_drag_vertex_id = drag_vertex
                        self._sketch_corner_drag_moved = False
                        event.accept()
                        return
                candidates = self._sketch_selection_candidates(
                    event.position()
                )
                selected = (
                    candidates[self._sketch_cycle_index]
                    if (
                        tuple(candidates) == self._sketch_cycle_ids
                        and 0 <= self._sketch_cycle_index < len(candidates)
                    )
                    else (candidates[0] if candidates else "")
                )
                if selected:
                    if selected.startswith("reference:"):
                        reference_id = selected.removeprefix("reference:")
                        if self._sketch_tool == "select":
                            self.sketchExternalReferenceSelected.emit(
                                reference_id
                            )
                        else:
                            self.sketchConstraintReferenceSelected.emit(
                                reference_id
                            )
                    elif (
                        event.modifiers()
                        & Qt.KeyboardModifier.ControlModifier
                    ):
                        self.sketchEntityAdditiveSelected.emit(selected)
                    else:
                        self.sketchEntitySelected.emit(selected)
                elif not self._sketch_constraint_selection_mode:
                        self._sketch_box_start = event.position()
                        self._sketch_box_end = event.position()
                self._preview_sketch_entity_id = None
                self._sketch_cycle_ids = ()
                self._sketch_cycle_index = -1
                self.update()
                event.accept()
                return
            local = self._sketch_local_position(event.position())
            if local is not None:
                snapped, reference_id, constraint = (
                    self._sketch_placement_candidate(event.position())
                )
                self.sketchPlacementClicked.emit(
                    *snapped,
                    reference_id or "",
                    constraint or "",
                )
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._selection_enabled
        ):
            if self._interaction_mode == "object":
                if self._selection_preview_pending:
                    self._selection_preview_pending = False
                    if self._object_overlay_mesh is not None:
                        self._object_overlay_persistent = True
                        self._object_overlay_color = QColor.fromRgbF(
                            0.0, 0.82, 1.0
                        )
                    else:
                        self._clear_topology_selection()
                        if self._hovered_point is not None:
                            self._set_selected_point(self._hovered_point)
                        elif self._hovered_plane is not None:
                            self._set_selected_plane(self._hovered_plane)
                        elif self._hovered_edge is not None:
                            self._set_selected_edge(self._hovered_edge)
                    self.selectionPreviewConfirmed.emit()
                    self.update()
                    event.accept()
                    return
                point = self._pick_point(event.position())
                plane = (
                    None
                    if point is not None
                    else self._pick_plane(event.position())
                )
                axis = (
                    None
                    if point is not None or plane is not None
                    else self._pick_axis(event.position())
                )
                self._clear_topology_selection()
                self._set_selected_object(None)
                if point is not None:
                    self._set_selected_point(point)
                elif plane is not None:
                    self._set_selected_plane(plane)
                elif axis is not None:
                    self._set_selected_edge(axis)
                else:
                    owner_id = self._pick_object(event.position())
                    if (
                        owner_id is None
                        and self._selected_object_id is None
                    ):
                        self.selectedObjectChanged.emit("")
                    else:
                        self._set_selected_object(owner_id)
                event.accept()
                return
            if self._cycled_topology_candidate is not None:
                kind, owner_id, element_index = (
                    self._cycled_topology_candidate
                )
                self._cycled_topology_candidate = None
                self._clear_topology_selection()
                {
                    "point": self._set_selected_point,
                    "edge": self._set_selected_edge,
                    "plane": self._set_selected_plane,
                    "face": self._set_selected_face,
                }[kind]((owner_id, element_index))
                event.accept()
                return
            point = self._pick_point(event.position())
            edge = None if point is not None else self._pick_edge(event.position())
            plane = (
                None
                if point is not None or edge is not None
                else self._pick_plane(event.position())
            )
            self._set_selected_point(point)
            self._set_selected_edge(edge)
            self._set_selected_plane(plane)
            self._set_selected_face(
                None
                if point is not None or edge is not None or plane is not None
                else self._pick_face(event.position())
            )
            event.accept()
            return
        if event.button() == Qt.MouseButton.MiddleButton:
            self._last_mouse_position = event.position().toPoint()
            self._middle_press_position = self._last_mouse_position
            self._middle_dragged = False
            self._middle_chorded = bool(
                event.buttons() & Qt.MouseButton.RightButton
            )
            if event.buttons() & Qt.MouseButton.RightButton:
                self._suppress_next_context_menu = True
            self.setFocus()
            event.accept()
            return
        if event.button() == Qt.MouseButton.RightButton:
            if event.buttons() & Qt.MouseButton.MiddleButton:
                self._last_mouse_position = event.position().toPoint()
                self._middle_chorded = True
                self._suppress_next_context_menu = True
                event.accept()
                return
            self._suppress_next_context_menu = False
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if (
            self._object_overlay_mesh is not None
            and not self._object_overlay_persistent
        ):
            self._object_overlay_mesh = None
            self.update()
        if (
            self._last_mouse_position is not None
            and event.buttons() & Qt.MouseButton.MiddleButton
        ):
            position = event.position().toPoint()
            delta = position - self._last_mouse_position
            self._last_mouse_position = position
            if (
                self._middle_press_position is not None
                and (
                    position - self._middle_press_position
                ).manhattanLength()
                > 3
            ):
                self._middle_dragged = True
            if event.buttons() & Qt.MouseButton.RightButton:
                self._middle_chorded = True
                self._suppress_next_context_menu = True
                self.camera.pan_x += float(delta.x())
                self.camera.pan_y += float(delta.y())
            else:
                self.camera.yaw_degrees += (
                    float(delta.x()) * self.rotation_degrees_per_pixel
                )
                self.camera.pitch_degrees += (
                    float(delta.y()) * self.rotation_degrees_per_pixel
                )
            self.navigationChanged.emit(self.camera)
            self.update()
            event.accept()
            return
        if (
            self._sketch_frame is not None
            and self._sketch_selection_mode
            and not self._sketch_reference_selection_mode
        ):
            if (
                self._sketch_dimension_drag_key is not None
                and event.buttons() & Qt.MouseButton.LeftButton
            ):
                local = self._sketch_local_position(event.position())
                if local is not None:
                    self._sketch_dimension_drag_moved = True
                    self.sketchDimensionDragged.emit(
                        self._sketch_dimension_drag_key,
                        local[0],
                        local[1],
                        False,
                    )
                event.accept()
                return
            if (
                self._sketch_corner_drag_vertex_id is not None
                and event.buttons() & Qt.MouseButton.LeftButton
            ):
                local = self._sketch_local_position(event.position())
                if local is not None:
                    self._sketch_corner_drag_moved = True
                    self.sketchCornerRadiusDragged.emit(
                        self._sketch_corner_drag_vertex_id,
                        local[0],
                        local[1],
                        False,
                    )
                event.accept()
                return
            if (
                self._sketch_box_start is not None
                and event.buttons() & Qt.MouseButton.LeftButton
            ):
                self._sketch_box_end = event.position()
                self.update()
                event.accept()
                return
            local = self._sketch_local_position(event.position())
            if local is not None:
                self.sketchCursorMoved.emit(*local)
            candidates = self._sketch_selection_candidates(event.position())
            hovered_corner_radius = (
                self._corner_radius_candidate(event.position())
                if self._sketch_tool
                in ("select", "dimension", "equal")
                else None
            )
            hovered_dimension = (
                self.dimension_key_at(event.position())
                if self._sketch_tool in ("select", "dimension")
                else None
            )
            if hovered_dimension == "sketch_dimension_preview":
                hovered_dimension = None
            if hovered_corner_radius is not None:
                candidates = ()
                hovered_dimension = None
            if hovered_corner_radius != self._hovered_sketch_corner_radius:
                self._hovered_sketch_corner_radius = hovered_corner_radius
                self.update()
            point_ids = {
                str(entity.get("id", ""))
                for entity in self._sketch_entities
                if entity.get("type") == "point"
            }
            point_is_candidate = any(
                candidate in point_ids for candidate in candidates
            )
            if self._sketch_tool == "select" and candidates:
                hovered_dimension = None
            elif (
                self._sketch_tool == "dimension"
                and hovered_dimension
                and not point_is_candidate
            ):
                candidates = ()
            elif self._sketch_tool == "dimension" and point_is_candidate:
                hovered_dimension = None
            if hovered_dimension != self._hovered_dimension_key:
                self._hovered_dimension_key = hovered_dimension
                self.sketchDimensionHovered.emit(hovered_dimension or "")
                self.update()
            if candidates != self._sketch_cycle_ids:
                self._sketch_cycle_ids = candidates
                self._sketch_cycle_index = 0 if candidates else -1
            active_candidate = (
                candidates[self._sketch_cycle_index]
                if candidates and self._sketch_cycle_index >= 0
                else ""
            )
            reference_id = (
                active_candidate.removeprefix("reference:")
                if active_candidate.startswith("reference:")
                else None
            )
            self._preview_sketch_entity_id = (
                active_candidate
                if active_candidate and reference_id is None
                else None
            )
            if reference_id != self._hovered_sketch_external_reference_id:
                self._hovered_sketch_external_reference_id = reference_id
                self.sketchReferenceHovered.emit(reference_id or "")
            self.update()
            super().mouseMoveEvent(event)
            return
        if (
            self._sketch_frame is not None
            and not self._sketch_selection_mode
            and not self._sketch_reference_selection_mode
        ):
            point_candidate = self._sketch_point_candidate(
                event.position()
            )
            point_id = (
                point_candidate[1]
                if point_candidate is not None
                else None
            )
            snapped, reference_id, constraint = (
                self._sketch_placement_candidate(event.position())
            )
            if (
                self._sketch_tool == "arc"
                and len(self._sketch_pending_points) == 2
            ):
                center = self._sketch_pending_points[0]
                start = self._sketch_pending_points[1]
                start_x = start[0] - center[0]
                start_y = start[1] - center[1]
                cursor_x = snapped[0] - center[0]
                cursor_y = snapped[1] - center[1]
                if hypot(cursor_x, cursor_y) > 1.0e-9:
                    cursor_angle = atan2(cursor_y, cursor_x)
                    if self._sketch_arc_last_angle is None:
                        self._sketch_arc_last_angle = atan2(
                            start_y,
                            start_x,
                        )
                    delta = (
                        cursor_angle
                        - self._sketch_arc_last_angle
                        + pi
                    ) % (2.0 * pi) - pi
                    self._sketch_arc_accumulated_sweep += delta
                    self._sketch_arc_last_angle = cursor_angle
                if abs(self._sketch_arc_accumulated_sweep) > 1.0e-4:
                    clockwise = self._sketch_arc_accumulated_sweep < 0.0
                    if clockwise != self._sketch_arc_clockwise:
                        self._sketch_arc_clockwise = clockwise
                        self.sketchArcDirectionSelected.emit(clockwise)
            construction_id = (
                reference_id.split(":", 1)[1]
                if reference_id is not None
                and reference_id.startswith("sketch_geometry:")
                else None
            )
            circle_id = (
                reference_id.split(":", 1)[1]
                if reference_id is not None
                and reference_id.startswith("sketch_circle:")
                else None
            )
            arc_id = (
                reference_id.split(":", 1)[1]
                if reference_id is not None
                and reference_id.startswith("sketch_arc:")
                else None
            )
            hovered_radius = None
            if reference_id is not None and reference_id.startswith(
                "sketch_radius:"
            ):
                radius_id = reference_id.removeprefix("sketch_radius:")
                for first in self._sketch_entities:
                    first_id = str(first.get("id", ""))
                    records = first.get("corner_radii", ())
                    if not isinstance(records, list):
                        continue
                    record = next(
                        (
                            item
                            for item in records
                            if isinstance(item, dict)
                            and str(item.get("id", "")) == radius_id
                        ),
                        None,
                    )
                    if record is not None:
                        hovered_radius = (
                            first_id,
                            str(record.get("other_geometry_id", "")),
                            str(record.get("vertex_id", "")),
                        )
                        break
            if hovered_radius != self._hovered_sketch_corner_radius:
                self._hovered_sketch_corner_radius = hovered_radius
            preview_id = point_id or construction_id or circle_id or arc_id
            if preview_id != self._preview_sketch_entity_id:
                self._preview_sketch_entity_id = preview_id
                # Construction highlighting is visual only. Do not report
                # its ID through the point-hover signal used by Coincident.
                self.sketchEntityHovered.emit(point_id or "")
            if reference_id != self._hovered_sketch_external_reference_id:
                self._hovered_sketch_external_reference_id = reference_id
                self.sketchReferenceHovered.emit(reference_id or "")
            if (
                snapped != self._sketch_preview_position
                or constraint != self._sketch_preview_constraint
            ):
                self._sketch_preview_position = snapped
                self._sketch_preview_constraint = constraint
            self.update()
            super().mouseMoveEvent(event)
            return
        if not self._selection_enabled:
            self._set_hovered_object(None)
            self._set_hovered_edge(None)
            self._set_hovered_face(None)
            self._set_hovered_point(None)
            self._set_hovered_plane(None)
            super().mouseMoveEvent(event)
            return
        if self._interaction_mode == "object":
            point = self._pick_point(event.position())
            plane = (
                None
                if point is not None
                else self._pick_plane(event.position())
            )
            axis = (
                None
                if point is not None or plane is not None
                else self._pick_axis(event.position())
            )
            self._clear_topology_hover()
            self._set_hovered_object(None)
            if point is not None:
                self._set_hovered_point(point)
            elif plane is not None:
                self._set_hovered_plane(plane)
            elif axis is not None:
                self._set_hovered_edge(axis)
            else:
                self._set_hovered_object(
                    self._pick_object(event.position())
                )
            super().mouseMoveEvent(event)
            return
        point = self._pick_point(event.position())
        edge = None if point is not None else self._pick_edge(event.position())
        plane = (
            None
            if point is not None or edge is not None
            else self._pick_plane(event.position())
        )
        self._set_hovered_point(point)
        self._set_hovered_edge(edge)
        self._set_hovered_plane(plane)
        self._set_hovered_face(
            None if point is not None or edge is not None or plane is not None
            else self._pick_face(event.position())
        )
        super().mouseMoveEvent(event)

    def leaveEvent(self, event) -> None:
        if self._hovered_sketch_corner_radius is not None:
            self._hovered_sketch_corner_radius = None
            self.update()
        if self._hovered_dimension_key is not None:
            self._hovered_dimension_key = None
            self.sketchDimensionHovered.emit("")
            self.update()
        if self._hovered_sketch_external_reference_id is not None:
            self._hovered_sketch_external_reference_id = None
            self.sketchReferenceHovered.emit("")
            self.update()
        if self._preview_sketch_entity_id is not None:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
            self.sketchEntityHovered.emit("")
            self.update()
        self._set_hovered_object(None)
        self._set_hovered_edge(None)
        self._set_hovered_face(None)
        self._set_hovered_point(None)
        self._set_hovered_plane(None)
        super().leaveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_dimension_drag_key is not None
        ):
            local = self._sketch_local_position(event.position())
            dimension_was_dragged = self._sketch_dimension_drag_moved
            if local is not None and dimension_was_dragged:
                self.sketchDimensionDragged.emit(
                    self._sketch_dimension_drag_key,
                    local[0],
                    local[1],
                    True,
                )
            self._sketch_dimension_drag_key = None
            self._sketch_dimension_drag_moved = False
            if dimension_was_dragged:
                self.sketchDimensionSelected.emit("")
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_corner_drag_vertex_id is not None
        ):
            local = self._sketch_local_position(event.position())
            if local is not None and self._sketch_corner_drag_moved:
                self.sketchCornerRadiusDragged.emit(
                    self._sketch_corner_drag_vertex_id,
                    local[0],
                    local[1],
                    True,
                )
            self._sketch_corner_drag_vertex_id = None
            self._sketch_corner_drag_moved = False
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_box_start is not None
        ):
            self._sketch_box_end = event.position()
            selected = self._sketch_entities_in_selection_box()
            self._sketch_box_start = None
            self._sketch_box_end = None
            self.sketchEntitiesSelected.emit(selected)
            self.update()
            event.accept()
            return
        if event.button() == Qt.MouseButton.MiddleButton:
            if self._middle_double_clicked:
                self._middle_double_clicked = False
                self._last_mouse_position = None
                self._middle_press_position = None
                self._middle_dragged = False
                self._middle_chorded = False
                event.accept()
                return
            confirm_sketch = (
                self._sketch_frame is not None
                and not self._middle_dragged
                and not self._middle_chorded
            )
            self._last_mouse_position = None
            self._middle_press_position = None
            self._middle_dragged = False
            self._middle_chorded = False
            if confirm_sketch:
                self.sketchConfirmCurrentRequested.emit()
                # The confirmation handler can switch the sketcher into
                # selection mode while the pointer is still over the newly
                # created entity. Do not retain that entity as an orange
                # preview; it becomes hoverable again on the next mouse move.
                self._preview_sketch_entity_id = None
                self._sketch_cycle_ids = ()
                self._sketch_cycle_index = -1
                self.update()
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.RightButton
            and event.buttons() & Qt.MouseButton.MiddleButton
        ):
            self._suppress_next_context_menu = True
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def _paint_sketch_selection_box(self) -> None:
        if self._sketch_box_start is None or self._sketch_box_end is None:
            return
        painter = QPainter(self)
        painter.setPen(QPen(QColor("#43B9FF"), 1.0))
        painter.setBrush(QBrush(QColor(67, 185, 255, 35)))
        painter.drawRect(
            QRectF(self._sketch_box_start, self._sketch_box_end).normalized()
        )
        painter.end()

    def _sketch_entities_in_selection_box(self) -> list[str]:
        if self._sketch_box_start is None or self._sketch_box_end is None:
            return []
        rectangle = QRectF(
            self._sketch_box_start,
            self._sketch_box_end,
        ).normalized()
        if rectangle.width() < 3.0 and rectangle.height() < 3.0:
            return []
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        selected: list[str] = []
        for entity in self._sketch_entities:
            entity_id = str(entity.get("id", ""))
            entity_type = str(entity.get("type", ""))
            if (
                not entity_id
                or entity_type == "construction"
                or entity.get("role") == "construction"
            ):
                continue
            local_points = (
                [point_positions[entity_id]]
                if entity_type == "point" and entity_id in point_positions
                else [
                    point_positions[str(point_id)]
                    for point_id in entity.get("point_ids", ())
                    if str(point_id) in point_positions
                ]
            )
            if local_points and all(
                rectangle.contains(
                    self._screen_point(
                        self._camera_point(self._sketch_world_point(point))
                    )
                )
                for point in local_points
            ):
                selected.append(entity_id)
        return selected

    def consume_context_menu_suppression(self) -> bool:
        suppressed = self._suppress_next_context_menu
        self._suppress_next_context_menu = False
        return suppressed

    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self._middle_double_clicked = True
            if self._sketch_frame is not None:
                self.sketchFinishCurrentRequested.emit()
            else:
                self.dimensionsDismissRequested.emit()
            self._last_mouse_position = None
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_frame is not None
            and self._sketch_tool in ("select", "dimension")
        ):
            dimension_key = self.dimension_key_at(event.position())
            if (
                dimension_key is not None
                and dimension_key != "sketch_dimension_preview"
            ):
                # The first press of a double-click may have armed dimension
                # dragging. Editing and dragging are mutually exclusive;
                # otherwise the following release/move regenerates the
                # overlay and immediately destroys the editor just opened.
                self._sketch_dimension_drag_key = None
                self._sketch_dimension_drag_moved = False
                self.sketchDimensionSelected.emit(dimension_key)
                self.sketchDimensionEditRequested.emit(dimension_key)
                event.accept()
                return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._interaction_mode == "object"
        ):
            self.objectDoubleClicked.emit(
                self._pick_object(event.position())
                or self._selected_object_id
                or ""
            )
            event.accept()
            return
        super().mouseDoubleClickEvent(event)

    def wheelEvent(self, event: QWheelEvent) -> None:
        self._stop_camera_animation()
        wheel_steps = event.angleDelta().y() / 120.0
        if wheel_steps:
            old_zoom = self.camera.zoom
            new_zoom = max(
                1e-4,
                min(1e4, old_zoom * 1.15 ** wheel_steps),
            )
            ratio = new_zoom / old_zoom
            cursor = event.position()
            self.camera.pan_x = (
                cursor.x()
                - self.width() * 0.5
                - (
                    cursor.x()
                    - self.width() * 0.5
                    - self.camera.pan_x
                )
                * ratio
            )
            self.camera.pan_y = (
                cursor.y()
                - self.height() * 0.5
                - (
                    cursor.y()
                    - self.height() * 0.5
                    - self.camera.pan_y
                )
                * ratio
            )
            self.camera.zoom = new_zoom
            self._buffers_dirty = True
            self.navigationChanged.emit(self.camera)
            self.update()
        event.accept()

    def _release_graphics_resources(self) -> None:
        for vao in (
            self._background_vao,
            self._surface_vao,
            self._edge_vao,
        ):
            if vao is not None and vao.isCreated():
                vao.destroy()
        for buffer in (self._surface_buffer, self._edge_buffer):
            if buffer is not None and buffer.isCreated():
                buffer.destroy()
        self._background_vao = None
        self._surface_vao = None
        self._edge_vao = None
        self._surface_buffer = None
        self._edge_buffer = None
        self._background_program = None
        self._surface_program = None
        self._edge_program = None
        self._gpu_ready = False

    def _draw_background(self) -> None:
        gl = self._gl
        program = self._background_program
        vao = self._background_vao
        if gl is None or program is None or vao is None:
            return
        gl.glDisable(GL_DEPTH_TEST)
        vao.bind()
        program.bind()
        program.setUniformValue(
            "bottomColor",
            QVector3D(
                self._background_bottom.redF(),
                self._background_bottom.greenF(),
                self._background_bottom.blueF(),
            ),
        )
        program.setUniformValue(
            "topColor",
            QVector3D(
                self._background_top.redF(),
                self._background_top.greenF(),
                self._background_top.blueF(),
            ),
        )
        gl.glDrawArrays(GL_TRIANGLES, 0, 3)
        program.release()
        vao.release()

    @staticmethod
    def _create_program(
        vertex_source: str,
        fragment_source: str,
    ) -> QOpenGLShaderProgram:
        program = QOpenGLShaderProgram()
        if not program.addShaderFromSourceCode(
            QOpenGLShader.ShaderTypeBit.Vertex,
            vertex_source,
        ):
            raise RuntimeError(program.log())
        if not program.addShaderFromSourceCode(
            QOpenGLShader.ShaderTypeBit.Fragment,
            fragment_source,
        ):
            raise RuntimeError(program.log())
        if not program.link():
            raise RuntimeError(program.log())
        return program

    def _upload_mesh_buffers(self) -> None:
        surface_buffer = self._surface_buffer
        edge_buffer = self._edge_buffer
        if surface_buffer is None or edge_buffer is None:
            return
        mesh = self._mesh
        surface_values = array("f")
        edge_values = array("f")
        edge_ranges: list[tuple[int, int]] = []
        if mesh is not None:
            for offset in range(0, len(mesh.triangle_positions), 3):
                surface_values.extend(
                    mesh.triangle_positions[offset:offset + 3]
                )
                surface_values.extend(
                    mesh.triangle_normals[offset:offset + 3]
                )
            edge_vertex_start = 0
            for edge in mesh.edges:
                display_points = self._display_edge_points(edge)
                for point in display_points:
                    edge_values.extend(point)
                edge_ranges.append(
                    (edge_vertex_start, len(display_points))
                )
                edge_vertex_start += len(display_points)
        surface_data = surface_values.tobytes()
        edge_data = edge_values.tobytes()
        surface_buffer.bind()
        surface_buffer.allocate(surface_data, len(surface_data))
        surface_buffer.release()
        edge_buffer.bind()
        edge_buffer.allocate(edge_data, len(edge_data))
        edge_buffer.release()
        self._surface_vertex_count = len(surface_values) // 6
        self._face_ranges = self._build_face_ranges(mesh)
        self._edge_ranges = tuple(edge_ranges)
        self._buffers_dirty = False

    def _draw_surfaces(
        self,
        model_view: QMatrix4x4,
        mvp: QMatrix4x4,
    ) -> None:
        gl = self._gl
        program = self._surface_program
        buffer = self._surface_buffer
        vao = self._surface_vao
        if (
            gl is None
            or program is None
            or buffer is None
            or vao is None
            or self._surface_vertex_count <= 0
        ):
            return
        vao.bind()
        program.bind()
        program.setUniformValue("modelView", model_view)
        program.setUniformValue("mvp", mvp)
        program.setUniformValue(
            "surfaceColor",
            QVector3D(
                self._surface_color.redF(),
                self._surface_color.greenF(),
                self._surface_color.blueF(),
            ),
        )
        buffer.bind()
        program.enableAttributeArray(0)
        program.setAttributeBuffer(0, 0x1406, 0, 3, 24)
        program.enableAttributeArray(1)
        program.setAttributeBuffer(1, 0x1406, 12, 3, 24)
        gl.glEnable(GL_POLYGON_OFFSET_FILL)
        gl.glPolygonOffset(1.0, 1.0)
        gl.glDrawArrays(GL_TRIANGLES, 0, self._surface_vertex_count)
        gl.glDisable(GL_POLYGON_OFFSET_FILL)
        if self._interaction_mode == "topology":
            gl.glDisable(GL_DEPTH_TEST)
        if not self._outline_face_highlights:
            self._draw_highlighted_face(
                gl,
                program,
                self._hovered_face,
                QVector3D(1.0, 0.48, 0.0),
            )
            self._draw_highlighted_face(
                gl,
                program,
                self._selected_face,
                QVector3D(0.0, 0.82, 1.0),
            )
        if self._interaction_mode == "topology":
            gl.glEnable(GL_DEPTH_TEST)
        program.disableAttributeArray(0)
        program.disableAttributeArray(1)
        buffer.release()
        program.release()
        vao.release()

    @staticmethod
    def _build_face_ranges(
        mesh: ViewerMesh | None,
    ) -> tuple[tuple[str, int, int, int], ...]:
        if mesh is None or not mesh.triangle_face_indices:
            return ()
        ranges: list[tuple[str, int, int, int]] = []
        first_triangle = 0
        current_face = mesh.triangle_face_indices[0]
        current_owner = mesh.triangle_owner_ids[0]
        for triangle_index, (face_index, owner_id) in enumerate(
            zip(
                mesh.triangle_face_indices[1:],
                mesh.triangle_owner_ids[1:],
            ),
            start=1,
        ):
            if face_index == current_face and owner_id == current_owner:
                continue
            ranges.append(
                (
                    current_owner,
                    current_face,
                    first_triangle * 3,
                    (triangle_index - first_triangle) * 3,
                )
            )
            current_face = face_index
            current_owner = owner_id
            first_triangle = triangle_index
        ranges.append(
            (
                current_owner,
                current_face,
                first_triangle * 3,
                (len(mesh.triangle_face_indices) - first_triangle) * 3,
            )
        )
        return tuple(ranges)

    def _draw_highlighted_face(
        self,
        gl: QOpenGLFunctions_3_3_Core,
        program: QOpenGLShaderProgram,
        face: TopologyKey | None,
        color: QVector3D,
    ) -> None:
        if face is None:
            return
        for owner_id, face_index, first_vertex, vertex_count in self._face_ranges:
            if (owner_id, face_index) == face:
                program.setUniformValue("surfaceColor", color)
                gl.glDrawArrays(GL_TRIANGLES, first_vertex, vertex_count)
                return

    def _draw_edges(
        self,
        mvp: QMatrix4x4,
        *,
        draw_base_edges: bool,
    ) -> None:
        gl = self._gl
        program = self._edge_program
        buffer = self._edge_buffer
        vao = self._edge_vao
        if gl is None or program is None or buffer is None or vao is None:
            return
        vao.bind()
        program.bind()
        program.setUniformValue("mvp", mvp)
        buffer.bind()
        program.enableAttributeArray(0)
        program.setAttributeBuffer(0, 0x1406, 0, 3, 12)
        gl.glLineWidth(max(1.0, float(self.devicePixelRatioF())))
        mesh = self._mesh
        for edge, (first_vertex, vertex_count) in zip(
            mesh.edges if mesh is not None else (),
            self._edge_ranges,
        ):
            if edge.element_kind == "centerline":
                continue
            if not draw_base_edges and edge.element_kind == "edge":
                continue
            if edge.element_kind in {"axis", "sketch", "dimension"}:
                gl.glDisable(GL_DEPTH_TEST)
            program.setUniformValue(
                "edgeColor",
                QVector3D(*edge.base_color),
            )
            gl.glDrawArrays(GL_LINE_STRIP, first_vertex, vertex_count)
            if edge.element_kind in {"axis", "sketch", "dimension"}:
                gl.glEnable(GL_DEPTH_TEST)
        self._draw_highlighted_edge(
            gl,
            program,
            self._hovered_edge,
            QVector3D(1.0, 0.48, 0.0),
            3.0,
        )
        self._draw_highlighted_object(
            gl,
            program,
            self._hovered_object_id,
            QVector3D(1.0, 0.48, 0.0),
            3.0,
        )
        self._draw_highlighted_reference(
            gl,
            program,
            self._selected_reference_owner_id,
        )
        self._draw_highlighted_object(
            gl,
            program,
            self._selected_object_id,
            QVector3D(0.0, 0.82, 1.0),
            3.0,
        )
        self._draw_highlighted_edge(
            gl,
            program,
            self._selected_edge,
            QVector3D(0.0, 0.82, 1.0),
            3.0,
        )
        for edge in self._constraint_reference_edges:
            self._draw_highlighted_edge(
                gl,
                program,
                edge,
                QVector3D(0.0, 0.82, 1.0),
                3.0,
            )
        program.disableAttributeArray(0)
        buffer.release()
        program.release()
        vao.release()

    def _draw_highlighted_edge(
        self,
        gl: QOpenGLFunctions_3_3_Core,
        program: QOpenGLShaderProgram,
        edge_key: TopologyKey | None,
        color: QVector3D,
        width: float,
    ) -> None:
        mesh = self._mesh
        if mesh is None or edge_key is None:
            return
        try:
            range_index = next(
                index
                for index, edge in enumerate(mesh.edges)
                if (edge.owner_id, edge.edge_index) == edge_key
            )
        except StopIteration:
            return
        first_vertex, vertex_count = self._edge_ranges[range_index]
        edge = mesh.edges[range_index]
        if edge.element_kind == "centerline":
            return
        if edge.element_kind in {"axis", "sketch", "dimension"}:
            gl.glDisable(GL_DEPTH_TEST)
        program.setUniformValue("edgeColor", color)
        gl.glLineWidth(
            max(width, width * float(self.devicePixelRatioF()))
        )
        gl.glDrawArrays(GL_LINE_STRIP, first_vertex, vertex_count)
        if edge.element_kind in {"axis", "sketch", "dimension"}:
            gl.glEnable(GL_DEPTH_TEST)

    def _draw_highlighted_object(
        self,
        gl: QOpenGLFunctions_3_3_Core,
        program: QOpenGLShaderProgram,
        owner_id: str | None,
        color: QVector3D,
        width: float,
    ) -> None:
        mesh = self._mesh
        if mesh is None or owner_id is None:
            return
        program.setUniformValue("edgeColor", color)
        gl.glLineWidth(max(width, width * float(self.devicePixelRatioF())))
        for edge, (first_vertex, vertex_count) in zip(
            mesh.edges,
            self._edge_ranges,
        ):
            if (
                edge.owner_id != owner_id
                or edge.element_kind not in {"edge", "sketch"}
            ):
                continue
            if edge.element_kind == "sketch":
                gl.glDisable(GL_DEPTH_TEST)
            gl.glDrawArrays(GL_LINE_STRIP, first_vertex, vertex_count)
            if edge.element_kind == "sketch":
                gl.glEnable(GL_DEPTH_TEST)

    def _draw_highlighted_reference(
        self,
        gl: QOpenGLFunctions_3_3_Core,
        program: QOpenGLShaderProgram,
        owner_id: str | None,
    ) -> None:
        mesh = self._mesh
        if mesh is None or owner_id is None:
            return
        program.setUniformValue("edgeColor", QVector3D(0.0, 0.82, 1.0))
        gl.glLineWidth(max(3.0, 3.0 * float(self.devicePixelRatioF())))
        gl.glDisable(GL_DEPTH_TEST)
        for edge, (first_vertex, vertex_count) in zip(
            mesh.edges,
            self._edge_ranges,
        ):
            if edge.owner_id == owner_id and edge.element_kind == "axis":
                gl.glDrawArrays(GL_LINE_STRIP, first_vertex, vertex_count)
        gl.glEnable(GL_DEPTH_TEST)

    def _display_edge_points(self, edge) -> tuple[Point3, ...]:
        if not edge.screen_constant:
            return edge.points
        origin = edge.points[0]
        scale = 1.0 / max(self.camera.zoom, 1e-6)
        return tuple(
            tuple(
                origin[axis] + (point[axis] - origin[axis]) * scale
                for axis in range(3)
            )
            for point in edge.points
        )

    def _display_plane_corners(self, plane) -> tuple[Point3, ...]:
        if not plane.screen_constant:
            return plane.corners
        center = tuple(
            sum(point[axis] for point in plane.corners)
            / len(plane.corners)
            for axis in range(3)
        )
        scale = 1.0 / max(self.camera.zoom, 1e-6)
        return tuple(
            tuple(
                center[axis] + (point[axis] - center[axis]) * scale
                for axis in range(3)
            )
            for point in plane.corners
        )

    def _paint_edge_labels(self) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        labelled_edges = tuple(edge for edge in mesh.edges if edge.label)
        if not labelled_edges:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.TextAntialiasing, True)
        font = painter.font()
        font.setBold(True)
        font.setPointSizeF(max(9.0, font.pointSizeF()))
        painter.setFont(font)
        for edge in labelled_edges:
            key = (edge.owner_id, edge.edge_index)
            color = edge.base_color
            if key == self._hovered_edge:
                color = (1.0, 0.48, 0.0)
            if key == self._selected_edge:
                color = (0.0, 0.82, 1.0)
            if edge.owner_id in {
                self._selected_reference_owner_id,
            } or edge.owner_id in self._selected_container_content_ids:
                color = (0.0, 0.82, 1.0)
            axis_color = QColor.fromRgbF(*color, 1.0)
            painter.setPen(QPen(axis_color, 1.5))
            display_points = self._display_edge_points(edge)
            startpoint = self._screen_point(
                self._camera_point(display_points[0])
            )
            endpoint = self._screen_point(
                self._camera_point(display_points[-1])
            )
            if (
                edge.element_kind == "axis"
                and edge.screen_constant
                and edge.label in {"X", "Y", "Z"}
            ):
                dx = endpoint.x() - startpoint.x()
                dy = endpoint.y() - startpoint.y()
                screen_length = hypot(dx, dy)
                if screen_length > 1e-6:
                    direction_x = dx / screen_length
                    direction_y = dy / screen_length
                    perpendicular_x = -direction_y
                    perpendicular_y = direction_x
                    arrow_length = 10.0
                    half_width = arrow_length * tan(radians(15.0))
                    base_x = endpoint.x() - direction_x * arrow_length
                    base_y = endpoint.y() - direction_y * arrow_length
                    arrow = QPolygonF(
                        [
                            endpoint,
                            QPointF(
                                base_x + perpendicular_x * half_width,
                                base_y + perpendicular_y * half_width,
                            ),
                            QPointF(
                                base_x - perpendicular_x * half_width,
                                base_y - perpendicular_y * half_width,
                            ),
                        ]
                    )
                    painter.setBrush(QBrush(axis_color))
                    painter.drawPolygon(arrow)
                    painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawText(
                QPointF(endpoint.x() + 6.0, endpoint.y() - 5.0),
                edge.label,
            )
        painter.end()

    def _paint_object_overlay(self) -> None:
        mesh = self._object_overlay_mesh
        if mesh is None or not mesh.edges:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(self._object_overlay_color, 3.0))
        for edge in mesh.edges:
            projected = [
                self._screen_point(self._camera_point(point))
                for point in edge.points
            ]
            for index in range(1, len(projected)):
                painter.drawLine(projected[index - 1], projected[index])
        if self._object_overlay_anchor is not None:
            screen = self._screen_point(
                self._camera_point(self._object_overlay_anchor)
            )
            painter.setPen(QPen(self._object_overlay_color, 1.0))
            painter.setBrush(QBrush(self._object_overlay_color))
            painter.drawEllipse(screen, 6.0, 6.0)
        painter.end()

    def _dimension_screen_geometry(
        self,
        dimension: LinearDimension | AngularDimension | RadialDimension,
    ) -> dict[str, Any]:
        if isinstance(dimension, RadialDimension):
            center = self._screen_point(self._camera_point(dimension.center))
            endpoint = self._screen_point(
                self._camera_point(dimension.radius_point)
            )
            dx = endpoint.x() - center.x()
            dy = endpoint.y() - center.y()
            length = hypot(dx, dy)
            if length <= 1.0e-9:
                return {
                    "radial": True,
                    "center": center,
                    "endpoint": endpoint,
                    "radial_end": endpoint,
                    "shoulder_start": endpoint,
                    "shoulder_end": endpoint,
                    "arrow": QPolygonF(),
                    "opposite_arrow": QPolygonF(),
                    "value_position": center,
                }
            ux, uy = dx / length, dy / length
            px, py = -uy, ux
            arrow_length = 10.0
            arrow_half_width = arrow_length * tan(radians(15.0))
            outside = dimension.arrow_placement == "outside"
            base = QPointF(
                endpoint.x() + ux * arrow_length * (1.0 if outside else -1.0),
                endpoint.y() + uy * arrow_length * (1.0 if outside else -1.0),
            )
            arrow = QPolygonF([
                endpoint,
                QPointF(
                    base.x() + px * arrow_half_width,
                    base.y() + py * arrow_half_width,
                ),
                QPointF(
                    base.x() - px * arrow_half_width,
                    base.y() - py * arrow_half_width,
                ),
            ])
            opposite = QPointF(center.x() - dx, center.y() - dy)
            opposite_base = QPointF(
                opposite.x() - ux * arrow_length,
                opposite.y() - uy * arrow_length,
            )
            opposite_arrow = QPolygonF([
                opposite,
                QPointF(
                    opposite_base.x() + px * arrow_half_width,
                    opposite_base.y() + py * arrow_half_width,
                ),
                QPointF(
                    opposite_base.x() - px * arrow_half_width,
                    opposite_base.y() - py * arrow_half_width,
                ),
            ])
            # The radius line ends at the measured arc and the horizontal
            # shoulder begins at the back of the outward-facing arrow.
            radial_end = endpoint
            shoulder_start = base
            shoulder_direction = 1.0 if ux >= 0.0 else -1.0
            shoulder_end = QPointF(
                shoulder_start.x() + shoulder_direction * 36.0,
                shoulder_start.y(),
            )
            return {
                "radial": True,
                "center": center,
                "endpoint": endpoint,
                "radial_start": opposite if dimension.diameter else center,
                "radial_end": radial_end,
                "shoulder_start": shoulder_start,
                "shoulder_end": shoulder_end,
                "arrow": arrow,
                "opposite_arrow": (
                    opposite_arrow if dimension.diameter else QPolygonF()
                ),
                "text_side": (
                    "right" if shoulder_direction > 0.0 else "left"
                ),
                "value_position": QPointF(
                    shoulder_end.x()
                    + (2.0 if shoulder_direction > 0.0 else 0.0),
                    shoulder_end.y(),
                ),
                "text_position": QPointF(
                    shoulder_end.x()
                    + (2.0 if shoulder_direction > 0.0 else 0.0),
                    shoulder_end.y() + 5.0,
                ),
            }
        if isinstance(dimension, AngularDimension):
            def subtract(first: Point3, second: Point3) -> Point3:
                return tuple(
                    first[index] - second[index] for index in range(3)
                )

            def dot(first: Point3, second: Point3) -> float:
                return sum(
                    first[index] * second[index] for index in range(3)
                )

            def normalized(vector: Point3) -> Point3 | None:
                length = sqrt(dot(vector, vector))
                if length <= 1.0e-12:
                    return None
                return tuple(value / length for value in vector)

            vertex = self._screen_point(
                self._camera_point(dimension.vertex)
            )
            first_vector = normalized(
                subtract(dimension.first_direction_point, dimension.vertex)
            )
            second_vector = normalized(
                subtract(dimension.second_direction_point, dimension.vertex)
            )
            if first_vector is None or second_vector is None:
                return {"angular": True, "vertex": vertex, "arc": QPolygonF()}
            projection = dot(first_vector, second_vector)
            plane_second = normalized(
                tuple(
                    second_vector[index]
                    - projection * first_vector[index]
                    for index in range(3)
                )
            )
            if plane_second is None:
                return {"angular": True, "vertex": vertex, "arc": QPolygonF()}
            minor_sweep = acos(max(-1.0, min(1.0, projection)))
            sweep = (
                radians(dimension.sweep_degrees)
                if dimension.sweep_degrees is not None
                else minor_sweep
            )
            hint_vector = subtract(dimension.arc_point, dimension.vertex)
            radius = sqrt(dot(hint_vector, hint_vector))
            projected_first_unit = self._screen_point(
                self._camera_point(
                    tuple(
                        dimension.vertex[index] + first_vector[index]
                        for index in range(3)
                    )
                )
            )
            projected_second_unit = self._screen_point(
                self._camera_point(
                    tuple(
                        dimension.vertex[index] + plane_second[index]
                        for index in range(3)
                    )
                )
            )
            pixels_per_unit = max(
                1.0e-6,
                (
                    hypot(
                        projected_first_unit.x() - vertex.x(),
                        projected_first_unit.y() - vertex.y(),
                    )
                    + hypot(
                        projected_second_unit.x() - vertex.x(),
                        projected_second_unit.y() - vertex.y(),
                    )
                )
                * 0.5,
            )
            radius = max(radius, 22.0 / pixels_per_unit)
            steps = max(12, int(abs(sweep) * 18.0))

            def arc_world(angle: float, distance: float = radius) -> Point3:
                return tuple(
                    dimension.vertex[index]
                    + distance
                    * (
                        first_vector[index] * cos(angle)
                        + plane_second[index] * sin(angle)
                    )
                    for index in range(3)
                )

            arc = QPolygonF([
                self._screen_point(
                    self._camera_point(arc_world(sweep * i / steps))
                )
                for i in range(steps + 1)
            ])
            middle_angle = sweep * 0.5
            value_position = self._screen_point(
                self._camera_point(
                    arc_world(
                        middle_angle,
                        radius + 10.0 / pixels_per_unit,
                    )
                )
            )
            return {
                "angular": True,
                "vertex": vertex,
                "first_dimension": arc[0],
                "second_dimension": arc[-1],
                "arc": arc,
                "value_position": value_position,
            }
        first = self._screen_point(self._camera_point(dimension.first_point))
        second = self._screen_point(self._camera_point(dimension.second_point))
        first_dimension = self._screen_point(
            self._camera_point(dimension.first_dimension_point)
        )
        second_dimension = self._screen_point(
            self._camera_point(dimension.second_dimension_point)
        )
        dx = second_dimension.x() - first_dimension.x()
        dy = second_dimension.y() - first_dimension.y()
        length = hypot(dx, dy)
        if length <= 1e-6:
            direction_end = tuple(
                dimension.first_dimension_point[index]
                + dimension.direction[index]
                for index in range(3)
            )
            projected_direction = self._screen_point(
                self._camera_point(direction_end)
            )
            dx = projected_direction.x() - first_dimension.x()
            dy = projected_direction.y() - first_dimension.y()
            length = hypot(dx, dy)
        if length <= 1e-6:
            dx, dy, length = 1.0, 0.0, 1.0
        direction_x = dx / length
        direction_y = dy / length
        perpendicular_x = -direction_y
        perpendicular_y = direction_x
        arrow_length = 10.0
        arrow_half_width = arrow_length * tan(radians(15.0))
        tail_length = 7.0

        def arrow(
            tip: QPointF,
            outside_sign: float,
        ) -> tuple[QPolygonF, QPointF, QPointF]:
            base_x = tip.x() + direction_x * arrow_length * outside_sign
            base_y = tip.y() + direction_y * arrow_length * outside_sign
            base = QPointF(base_x, base_y)
            tail = QPointF(
                base_x + direction_x * tail_length * outside_sign,
                base_y + direction_y * tail_length * outside_sign,
            )
            return (
                QPolygonF(
                    [
                        tip,
                        QPointF(
                            base_x + perpendicular_x * arrow_half_width,
                            base_y + perpendicular_y * arrow_half_width,
                        ),
                        QPointF(
                            base_x - perpendicular_x * arrow_half_width,
                            base_y - perpendicular_y * arrow_half_width,
                        ),
                    ]
                ),
                base,
                tail,
            )

        first_arrow, first_arrow_base, first_tail = arrow(
            first_dimension,
            -1.0,
        )
        second_arrow, second_arrow_base, second_tail = arrow(
            second_dimension,
            1.0,
        )
        leader_tail = (
            second_tail
            if dimension.leader_anchor == "second"
            else max(
                (first_tail, second_tail),
                key=lambda point: point.x(),
            )
        )
        leader_start = QPointF(leader_tail)
        leader_end = QPointF(leader_start.x() + 30.0, leader_start.y())
        return {
            "first": first,
            "second": second,
            "first_dimension": first_dimension,
            "second_dimension": second_dimension,
            "first_arrow": first_arrow,
            "second_arrow": second_arrow,
            "first_arrow_base": first_arrow_base,
            "second_arrow_base": second_arrow_base,
            "first_tail": first_tail,
            "second_tail": second_tail,
            "leader_start": leader_start,
            "leader_end": leader_end,
            "value_position": QPointF(
                leader_end.x() + 4.0,
                leader_end.y(),
            ),
        }

    def _paint_dimensions(self) -> None:
        if not self._dimensions:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for dimension in self._dimensions:
            color = (
                QColor("#00D1FF")
                if dimension.key == self._selected_dimension_key
                else (
                    QColor("#FF7A00")
                    if dimension.key == self._hovered_dimension_key
                    else (
                        QColor("#9A6A3A")
                        if dimension.key in self._locked_dimension_keys
                        else QColor("#FFF06A")
                    )
                )
            )
            painter.setPen(QPen(color, 1.5))
            painter.setBrush(QBrush(color))
            geometry = self._dimension_screen_geometry(dimension)
            if geometry.get("radial"):
                painter.drawLine(
                    geometry.get("radial_start", geometry["center"]),
                    geometry["radial_end"],
                )
                if geometry.get("shoulder_end") is not None:
                    painter.drawLine(
                        geometry["shoulder_start"],
                        geometry["shoulder_end"],
                    )
                painter.drawPolygon(geometry["arrow"])
                if not geometry["opposite_arrow"].isEmpty():
                    painter.drawPolygon(geometry["opposite_arrow"])
                if dimension.display_text:
                    text_position = geometry.get(
                        "text_position", geometry["value_position"]
                    )
                    if geometry.get("text_side") == "left":
                        text_position = QPointF(
                            geometry["shoulder_end"].x()
                            - painter.fontMetrics().horizontalAdvance(
                                dimension.display_text
                            )
                            - 2.0,
                            text_position.y(),
                        )
                    painter.drawText(
                        text_position,
                        dimension.display_text,
                    )
                continue
            if geometry.get("angular"):
                painter.drawLine(
                    geometry["vertex"],
                    geometry["first_dimension"],
                )
                painter.drawLine(
                    geometry["vertex"],
                    geometry["second_dimension"],
                )
                painter.drawPolyline(geometry["arc"])
                continue
            painter.drawLine(
                geometry["first"],
                geometry["first_dimension"],
            )
            painter.drawLine(
                geometry["second"],
                geometry["second_dimension"],
            )
            painter.drawLine(
                geometry["first_dimension"],
                geometry["second_dimension"],
            )
            painter.drawPolygon(geometry["first_arrow"])
            painter.drawPolygon(geometry["second_arrow"])
            painter.drawLine(
                geometry["first_arrow_base"],
                geometry["first_tail"],
            )
            painter.drawLine(
                geometry["second_arrow_base"],
                geometry["second_tail"],
            )
            painter.drawLine(
                geometry["leader_start"],
                geometry["leader_end"],
            )
            if dimension.display_text:
                painter.drawText(
                    geometry["value_position"],
                    dimension.display_text,
                )
        painter.end()

    def _sketch_world_point(self, point: tuple[float, float]) -> Point3:
        if self._sketch_frame is None:
            return (0.0, 0.0, 0.0)
        origin, x_axis, y_axis = self._sketch_frame
        return tuple(
            origin[index]
            + point[0] * x_axis[index]
            + point[1] * y_axis[index]
            for index in range(3)
        )

    def _sketch_local_position(
        self,
        position: QPointF,
    ) -> tuple[float, float] | None:
        if self._sketch_frame is None:
            return None
        origin, x_axis, y_axis = self._sketch_frame
        screen_origin = self._screen_point(self._camera_point(origin))
        screen_x = self._screen_point(
            self._camera_point(
                tuple(origin[index] + x_axis[index] for index in range(3))
            )
        )
        screen_y = self._screen_point(
            self._camera_point(
                tuple(origin[index] + y_axis[index] for index in range(3))
            )
        )
        ax = screen_x.x() - screen_origin.x()
        ay = screen_x.y() - screen_origin.y()
        bx = screen_y.x() - screen_origin.x()
        by = screen_y.y() - screen_origin.y()
        px = position.x() - screen_origin.x()
        py = position.y() - screen_origin.y()
        determinant = ax * by - ay * bx
        if abs(determinant) <= 1e-12:
            return None
        return (
            (px * by - py * bx) / determinant,
            (ax * py - ay * px) / determinant,
        )

    def _paint_sketch_overlay(self) -> None:
        if self._sketch_frame is None:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        brown = QColor("#9A6A3A")
        yellow = QColor("#FFD740")
        cyan = QColor("#00D1FF")
        dashed = QPen(brown, 1.2, Qt.PenStyle.DashLine)
        dashed.setDashPattern([12.0, 10.0])
        base_centerline_width = 1.2
        highlight_centerline_width = 3.0
        highlight_dash_pattern = (9.0, 4.0, 2.0, 4.0)
        centerline = QPen(
            yellow,
            base_centerline_width,
            Qt.PenStyle.CustomDashLine,
        )
        centerline.setDashPattern(
            [
                value
                * highlight_centerline_width
                / base_centerline_width
                for value in highlight_dash_pattern
            ]
        )
        auxiliary_line = QPen(
            yellow,
            base_centerline_width,
            Qt.PenStyle.CustomDashLine,
        )
        auxiliary_line.setDashPattern([12.0, 10.0])

        def highlighted_centerline(color: QColor) -> QPen:
            pen = QPen(
                color,
                highlight_centerline_width,
                Qt.PenStyle.CustomDashLine,
            )
            pen.setDashPattern(highlight_dash_pattern)
            return pen

        def highlighted_auxiliary(color: QColor) -> QPen:
            pen = QPen(
                color,
                highlight_centerline_width,
                Qt.PenStyle.CustomDashLine,
            )
            pen.setDashPattern([
                12.0 * base_centerline_width
                / highlight_centerline_width,
                10.0 * base_centerline_width
                / highlight_centerline_width,
            ])
            return pen

        painter.setPen(dashed)

        origin = self._screen_point(
            self._camera_point(self._sketch_frame[0])
        )
        extent = float(max(self.width(), self.height()) * 2)

        def infinite_line(first: QPointF, second: QPointF) -> None:
            dx = second.x() - first.x()
            dy = second.y() - first.y()
            length = hypot(dx, dy)
            if length <= 1e-9:
                return
            dx /= length
            dy /= length
            painter.drawLine(
                QPointF(first.x() - dx * extent, first.y() - dy * extent),
                QPointF(first.x() + dx * extent, first.y() + dy * extent),
            )

        for axis_index, axis in enumerate(self._sketch_frame[1:]):
            end = self._screen_point(
                self._camera_point(
                    tuple(
                        self._sketch_frame[0][index] + axis[index]
                        for index in range(3)
                    )
                )
            )
            axis_reference_id = (
                "sketch_axis:x" if axis_index == 0 else "sketch_axis:y"
            )
            painter.setPen(
                highlighted_auxiliary(QColor("#FF7A00"))
                if (
                    axis_reference_id
                    == self._hovered_sketch_external_reference_id
                )
                else dashed
            )
            infinite_line(origin, end)
        origin_hovered = (
            self._hovered_sketch_external_reference_id == "sketch_origin"
        )
        origin_color = QColor("#FF7A00") if origin_hovered else yellow
        painter.setPen(QPen(origin_color, 2.5 if origin_hovered else 2.0))
        painter.setBrush(QBrush(origin_color))
        painter.drawEllipse(
            origin,
            5.0 if origin_hovered else 3.5,
            5.0 if origin_hovered else 3.5,
        )

        for reference in self._sketch_external_references:
            geometry = reference.get("geometry", {})
            if not isinstance(geometry, dict):
                continue
            reference_color = (
                QColor("#FF7A00")
                if (
                    str(reference.get("id", ""))
                    == self._hovered_sketch_external_reference_id
                )
                else (
                    cyan
                    if reference.get("selected")
                    else (
                        QColor("#B34A3C")
                        if reference.get("broken")
                        else brown
                    )
                )
            )
            reference_pen = QPen(
                reference_color,
                (
                    highlight_centerline_width
                    if reference.get("selected")
                    else base_centerline_width
                ),
                Qt.PenStyle.CustomDashLine,
            )
            reference_pen.setDashPattern(
                [
                    value
                    * (
                        base_centerline_width
                        / highlight_centerline_width
                    )
                    if reference.get("selected")
                    else value
                    for value in (12.0, 10.0)
                ]
            )
            painter.setPen(reference_pen)
            painter.setBrush(QBrush(reference_color))
            geometry_type = geometry.get("type")
            if geometry_type == "line":
                point = geometry.get("point", (0.0, 0.0))
                direction = geometry.get("direction", (1.0, 0.0))
                if (
                    isinstance(point, (list, tuple))
                    and isinstance(direction, (list, tuple))
                    and len(point) >= 2
                    and len(direction) >= 2
                ):
                    first = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (float(point[0]), float(point[1]))
                            )
                        )
                    )
                    second = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (
                                    float(point[0]) + float(direction[0]),
                                    float(point[1]) + float(direction[1]),
                                )
                            )
                        )
                    )
                    infinite_line(first, second)
            elif geometry_type == "lines":
                raw_lines = geometry.get("lines", ())
                if isinstance(raw_lines, (list, tuple)):
                    for line_index, raw_line in enumerate(raw_lines):
                        if not isinstance(raw_line, dict):
                            continue
                        source_line_index = int(
                            raw_line.get(
                                "_reference_line_index",
                                line_index,
                            )
                        )
                        line_selected = (
                            reference.get("selected_line_index")
                            == source_line_index
                        )
                        line_hovered = (
                            self._hovered_sketch_external_reference_id
                            == (
                                f"{reference.get('id', '')}"
                                f"::line:{source_line_index}"
                            )
                        )
                        line_color = (
                            QColor("#FF7A00")
                            if line_hovered
                            else cyan
                            if line_selected
                            else reference_color
                        )
                        line_pen = QPen(
                            line_color,
                            (
                                highlight_centerline_width
                                if line_selected or line_hovered
                                else base_centerline_width
                            ),
                            Qt.PenStyle.CustomDashLine,
                        )
                        line_pen.setDashPattern(
                            [
                                value
                                * (
                                    base_centerline_width
                                    / highlight_centerline_width
                                )
                                if line_selected or line_hovered
                                else value
                                for value in (12.0, 10.0)
                            ]
                        )
                        painter.setPen(line_pen)
                        painter.setBrush(QBrush(line_color))
                        point = raw_line.get("point", (0.0, 0.0))
                        direction = raw_line.get(
                            "direction",
                            (1.0, 0.0),
                        )
                        if (
                            not isinstance(point, (list, tuple))
                            or not isinstance(
                                direction,
                                (list, tuple),
                            )
                            or len(point) < 2
                            or len(direction) < 2
                        ):
                            continue
                        first = self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(
                                    (
                                        float(point[0]),
                                        float(point[1]),
                                    )
                                )
                            )
                        )
                        second = self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(
                                    (
                                        float(point[0])
                                        + float(direction[0]),
                                        float(point[1])
                                        + float(direction[1]),
                                    )
                                )
                            )
                        )
                        infinite_line(first, second)
            elif geometry_type == "point":
                point = geometry.get("point", (0.0, 0.0))
                if isinstance(point, (list, tuple)) and len(point) >= 2:
                    screen = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (float(point[0]), float(point[1]))
                            )
                        )
                    )
                    painter.drawEllipse(screen, 4.0, 4.0)
            elif geometry_type == "polyline":
                raw_points = geometry.get("points", ())
                if isinstance(raw_points, (list, tuple)):
                    projected = QPolygonF(
                        [
                            self._screen_point(
                                self._camera_point(
                                    self._sketch_world_point(
                                        (float(point[0]), float(point[1]))
                                    )
                                )
                            )
                            for point in raw_points
                            if isinstance(point, (list, tuple))
                            and len(point) >= 2
                        ]
                    )
                    if len(projected) >= 2:
                        painter.drawPolyline(projected)
            elif geometry_type == "polylines":
                raw_polylines = geometry.get("polylines", ())
                if isinstance(raw_polylines, (list, tuple)):
                    for raw_points in raw_polylines:
                        if not isinstance(raw_points, (list, tuple)):
                            continue
                        projected = QPolygonF(
                            [
                                self._screen_point(
                                    self._camera_point(
                                        self._sketch_world_point(
                                            (
                                                float(point[0]),
                                                float(point[1]),
                                            )
                                        )
                                    )
                                )
                                for point in raw_points
                                if isinstance(point, (list, tuple))
                                and len(point) >= 2
                            ]
                        )
                        if len(projected) >= 2:
                            painter.drawPolyline(projected)

        painter.setPen(QPen(yellow, 2.0))
        painter.setBrush(QBrush(yellow))
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
            and str(entity.get("id", ""))
        }
        sketch_geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") == "segment"
            and str(entity.get("id", ""))
        }
        corner_trim_points: dict[tuple[str, str], tuple[float, float]] = {}
        corner_arcs: list[
            tuple[
                tuple[tuple[float, float], ...],
                str,
                str,
                str,
                bool,
            ]
        ] = []
        for first_id, first_geometry in sketch_geometry_by_id.items():
            records = first_geometry.get("corner_radii", ())
            if not isinstance(records, list):
                continue
            first_ids = tuple(map(str, first_geometry.get("point_ids", ())))
            if len(first_ids) != 2:
                continue
            for record in records:
                if not isinstance(record, dict):
                    continue
                if bool(record.get("suppressed", False)):
                    continue
                second_id = str(record.get("other_geometry_id", ""))
                vertex_id = str(record.get("vertex_id", ""))
                second_geometry = sketch_geometry_by_id.get(second_id)
                second_ids = (
                    tuple(map(str, second_geometry.get("point_ids", ())))
                    if second_geometry is not None
                    else ()
                )
                if (
                    len(second_ids) != 2
                    or vertex_id not in first_ids
                    or vertex_id not in second_ids
                ):
                    continue
                vertex = point_positions.get(vertex_id)
                first_outer = point_positions.get(
                    next(
                        point_id
                        for point_id in first_ids
                        if point_id != vertex_id
                    )
                )
                second_outer = point_positions.get(
                    next(
                        point_id
                        for point_id in second_ids
                        if point_id != vertex_id
                    )
                )
                if (
                    vertex is None
                    or first_outer is None
                    or second_outer is None
                ):
                    continue
                evaluated = evaluate_corner_radius(
                    vertex,
                    first_outer,
                    second_outer,
                    float(record.get("radius", 0.0)),
                )
                if evaluated is None:
                    continue
                corner_trim_points[(first_id, vertex_id)] = (
                    evaluated.first_tangent
                )
                corner_trim_points[(second_id, vertex_id)] = (
                    evaluated.second_tangent
                )
                corner_arcs.append(
                    (
                        evaluated.arc_points,
                        first_id,
                        second_id,
                        vertex_id,
                        bool(record.get("equal_radius_group")),
                    )
                )
        for entity in self._sketch_entities:
            entity_type = str(entity.get("type", ""))
            selected = (
                str(entity.get("id", ""))
                == self._selected_sketch_entity_id
                or str(entity.get("id", ""))
                in self._selected_sketch_entity_ids
                or (
                    entity_type == "point"
                    and self._selected_sketch_corner_radius is not None
                    and str(entity.get("id", ""))
                    == self._selected_sketch_corner_radius[2]
                )
            )
            previewed = (
                str(entity.get("id", ""))
                == self._preview_sketch_entity_id
                or (
                    entity_type == "point"
                    and self._hovered_sketch_corner_radius is not None
                    and str(entity.get("id", ""))
                    == self._hovered_sketch_corner_radius[2]
                )
            )
            raw_points = entity.get("points", ())
            if entity_type == "point" and "id" in entity:
                raw_points = (
                    point_positions.get(str(entity.get("id")), (0.0, 0.0)),
                )
            elif isinstance(entity.get("point_ids"), list):
                raw_points = [
                    corner_trim_points.get(
                        (
                            str(entity.get("id", "")),
                            point_id,
                        ),
                        point_positions[point_id],
                    )
                    for point_id in map(str, entity["point_ids"])
                    if point_id in point_positions
                ]
            if (
                entity_type == "arc"
                and entity.get("arc_mode") == "center"
                and len(raw_points) >= 3
            ):
                raw_points = center_arc_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                    clockwise=bool(entity.get("clockwise", False)),
                )
            elif entity_type == "spline" and len(raw_points) >= 2:
                raw_points = _interpolated_spline_points(
                    tuple(
                        (float(point[0]), float(point[1]))
                        for point in raw_points
                        if isinstance(point, (list, tuple))
                        and len(point) >= 2
                    )
                )
            points = [
                self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(
                            (float(point[0]), float(point[1]))
                        )
                    )
                )
                for point in raw_points
                if isinstance(point, (list, tuple)) and len(point) >= 2
            ]
            if entity_type == "circle" and len(points) == 1:
                local_radius = float(entity.get("radius", 0.0))
                local_center = raw_points[0]
                projected_circle = QPolygonF([
                    self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (
                                    float(local_center[0])
                                    + local_radius * cos(angle),
                                    float(local_center[1])
                                    + local_radius * sin(angle),
                                )
                            )
                        )
                    )
                    for angle in (
                        2.0 * pi * index / 96.0
                        for index in range(97)
                    )
                ])
                circle_pen = QPen(
                    cyan if selected else
                    QColor("#FF7A00") if previewed else yellow,
                    3.0 if selected or previewed else 2.0,
                )
                painter.setPen(circle_pen)
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.drawPolyline(projected_circle)
                painter.setPen(QPen(yellow, 2.0))
                painter.setBrush(QBrush(yellow))
                continue
            is_construction = (
                entity_type == "construction"
                or entity.get("role") == "construction"
            )
            if is_construction and len(points) >= 2:
                construction_line = entity_type == "construction"
                painter.setPen(
                    (
                        highlighted_centerline(cyan)
                        if construction_line
                        else highlighted_auxiliary(cyan)
                    )
                    if selected
                    else (
                        (
                            highlighted_centerline(QColor("#FF7A00"))
                            if construction_line
                            else highlighted_auxiliary(QColor("#FF7A00"))
                        )
                        if previewed
                        else (
                            centerline
                            if construction_line
                            else auxiliary_line
                        )
                    )
                )
                if construction_line:
                    infinite_line(points[0], points[1])
                else:
                    painter.drawPolyline(QPolygonF(points))
                painter.setPen(QPen(yellow, 2.0))
            elif entity_type == "point" and points:
                point_color = (
                    cyan
                    if selected
                    else QColor("#FF7A00") if previewed else yellow
                )
                painter.setPen(QPen(point_color, 2.0))
                painter.setBrush(QBrush(point_color))
                painter.drawEllipse(points[0], 4.0, 4.0)
                painter.setPen(QPen(yellow, 2.0))
                painter.setBrush(QBrush(yellow))
            elif selected and len(points) >= 2:
                painter.setPen(QPen(cyan, 3.0))
                painter.drawPolyline(QPolygonF(points))
                painter.setPen(QPen(yellow, 2.0))
            elif previewed and len(points) >= 2:
                painter.setPen(QPen(QColor("#FF7A00"), 3.0))
                painter.drawPolyline(QPolygonF(points))
                painter.setPen(QPen(yellow, 2.0))
            elif len(points) >= 2:
                painter.setPen(QPen(yellow, 2.0))
                painter.drawPolyline(QPolygonF(points))

        painter.setBrush(Qt.BrushStyle.NoBrush)
        equal_radius_marker_points: list[QPointF] = []
        for (
            raw_arc,
            first_id,
            second_id,
            vertex_id,
            has_equal_radius,
        ) in corner_arcs:
            hovered = self._hovered_sketch_corner_radius == (
                first_id,
                second_id,
                vertex_id,
            )
            selected = (
                first_id in self._selected_sketch_entity_ids
                and second_id in self._selected_sketch_entity_ids
            ) or self._selected_sketch_corner_radius == (
                first_id,
                second_id,
                vertex_id,
            )
            painter.setPen(
                QPen(
                    cyan
                    if selected
                    else QColor("#FF7A00") if hovered else yellow,
                    3.0 if selected or hovered else 2.0,
                )
            )
            arc = QPolygonF(
                [
                    self._screen_point(
                        self._camera_point(self._sketch_world_point(point))
                    )
                    for point in raw_arc
                ]
            )
            painter.drawPolyline(arc)
            if has_equal_radius and arc:
                equal_radius_marker_points.append(arc[len(arc) // 2])
            if selected:
                vertex = point_positions.get(vertex_id)
                if vertex is not None:
                    screen_vertex = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(vertex)
                        )
                    )
                    painter.setPen(QPen(cyan, 1.0, Qt.PenStyle.DashLine))
                    painter.drawLine(arc[0], screen_vertex)
                    painter.drawLine(arc[-1], screen_vertex)
                    painter.drawEllipse(screen_vertex, 3.5, 3.5)
                    painter.setPen(QPen(cyan, 2.0))
                    painter.setBrush(QBrush(cyan))
                    painter.drawEllipse(arc[0], 4.0, 4.0)
                    painter.drawEllipse(arc[-1], 4.0, 4.0)
                    painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(yellow, 2.0))
        painter.setBrush(QBrush(yellow))

        geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") in ("segment", "construction")
            and str(entity.get("id", ""))
        }

        def geometry_screen_line(
            geometry: dict[str, Any],
        ) -> tuple[QPointF, QPointF] | None:
            point_ids = geometry.get("point_ids", ())
            if not isinstance(point_ids, list) or len(point_ids) < 2:
                return None
            local_points = [
                point_positions.get(str(point_id))
                for point_id in point_ids[:2]
            ]
            if any(point is None for point in local_points):
                return None
            return tuple(
                self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(point)
                    )
                )
                for point in local_points
            )

        def normalized_screen_direction(
            first: QPointF,
            second: QPointF,
        ) -> tuple[float, float] | None:
            dx = second.x() - first.x()
            dy = second.y() - first.y()
            length = hypot(dx, dy)
            if length <= 1.0e-9:
                return None
            return dx / length, dy / length

        def draw_perpendicular_symbol(
            constrained_line: tuple[QPointF, QPointF],
            reference_line: tuple[QPointF, QPointF],
        ) -> None:
            constrained_first, constrained_second = constrained_line
            reference_first, reference_second = reference_line
            cx = constrained_second.x() - constrained_first.x()
            cy = constrained_second.y() - constrained_first.y()
            rx = reference_second.x() - reference_first.x()
            ry = reference_second.y() - reference_first.y()
            denominator = cx * ry - cy * rx
            if abs(denominator) <= 1.0e-9:
                return
            offset_x = reference_first.x() - constrained_first.x()
            offset_y = reference_first.y() - constrained_first.y()
            fraction = (offset_x * ry - offset_y * rx) / denominator
            intersection = QPointF(
                constrained_first.x() + fraction * cx,
                constrained_first.y() + fraction * cy,
            )
            # The second endpoint is the point moved by the perpendicular
            # relation. Keep its marker close to that end instead of placing
            # it at a remote line intersection or at the segment midpoint.
            anchor = constrained_second
            constrained_direction = normalized_screen_direction(
                constrained_second,
                constrained_first,
            )
            reference_direction = normalized_screen_direction(
                reference_first,
                reference_second,
            )
            if reference_direction is None:
                return
            if constrained_direction is None or reference_direction is None:
                return
            ux, uy = constrained_direction
            vx, vy = reference_direction
            # Pick the reference direction that keeps the square on the
            # visually clearer side of the constrained endpoint.
            if ux * vy - uy * vx < 0.0:
                vx, vy = -vx, -vy
            size = 10.0
            first_corner = QPointF(
                anchor.x() + ux * size,
                anchor.y() + uy * size,
            )
            square_corner = QPointF(
                first_corner.x() + vx * size,
                first_corner.y() + vy * size,
            )
            second_corner = QPointF(
                anchor.x() + vx * size,
                anchor.y() + vy * size,
            )
            painter.drawPolyline(
                QPolygonF((first_corner, square_corner, second_corner))
            )

        constraint_color = QColor("#7CFF6B")
        constraint_font = painter.font()
        constraint_font.setBold(True)
        painter.setFont(constraint_font)
        painter.setPen(QPen(constraint_color, 2.0))
        markers_by_geometry: dict[str, list[str]] = {
            geometry_id: [] for geometry_id in geometry_by_id
        }
        tangent_contact_ids: set[str] = set()
        midpoint_point_ids: set[str] = set()
        coincident_point_ids: set[str] = set()
        symmetric_point_pairs: list[tuple[str, str]] = []

        def add_marker(geometry_id: str, marker: str) -> None:
            markers = markers_by_geometry.get(geometry_id)
            if markers is not None and marker not in markers:
                markers.append(marker)

        for geometry_id, geometry in geometry_by_id.items():
            constraints = geometry.get("constraints", ())
            if not isinstance(constraints, list):
                continue
            for constraint in constraints:
                if not isinstance(constraint, dict):
                    continue
                constraint_type = str(constraint.get("type", ""))
                if constraint_type == "horizontal":
                    add_marker(geometry_id, "H")
                elif constraint_type == "vertical":
                    add_marker(geometry_id, "V")
                elif constraint_type == "parallel":
                    add_marker(geometry_id, "∥")
                elif constraint_type == "equal_length":
                    add_marker(geometry_id, "=")
                    add_marker(
                        str(constraint.get("geometry_id", "")),
                        "=",
                    )
                elif constraint_type == "tangent":
                    contact_id = str(
                        constraint.get("contact_point_id", "")
                    )
                    if contact_id:
                        tangent_contact_ids.add(contact_id)

        for entity in self._sketch_entities:
            if entity.get("type") != "point":
                continue
            if isinstance(entity.get("curve_attachment"), dict):
                coincident_point_ids.add(str(entity.get("id", "")))
            constraints = entity.get("constraints", ())
            if isinstance(constraints, list):
                if any(
                    isinstance(constraint, dict)
                    and constraint.get("type") == "midpoint"
                    for constraint in constraints
                ):
                    midpoint_point_ids.add(str(entity.get("id", "")))
                if any(
                    isinstance(constraint, dict)
                    and constraint.get("type")
                    in (
                        "coincident",
                        "point_on_line",
                        "point_on_reference",
                    )
                    for constraint in constraints
                ):
                    coincident_point_ids.add(str(entity.get("id", "")))
                for constraint in constraints:
                    if (
                        isinstance(constraint, dict)
                        and constraint.get("type") == "symmetric"
                    ):
                        second_id = str(constraint.get("point_id", ""))
                        if second_id:
                            symmetric_point_pairs.append(
                                (str(entity.get("id", "")), second_id)
                            )

        metrics = painter.fontMetrics()
        marker_spacing = 16.0
        for geometry_id, markers in markers_by_geometry.items():
            if not markers:
                continue
            line = geometry_screen_line(geometry_by_id[geometry_id])
            if line is None:
                continue
            first, second = line
            direction = normalized_screen_direction(first, second)
            if direction is None:
                continue
            dx, dy = direction
            center_x = (first.x() + second.x()) * 0.5
            center_y = (first.y() + second.y()) * 0.5
            for marker_index, label in enumerate(markers):
                along = (
                    marker_index - (len(markers) - 1) * 0.5
                ) * marker_spacing
                painter.drawText(
                    QPointF(
                    center_x
                    + dx * along
                    - metrics.horizontalAdvance(label) * 0.5
                    - dy * 11.0,
                    center_y
                    + dy * along
                    + metrics.ascent() * 0.5
                    + dx * 11.0,
                    ),
                    label,
                )

        for contact_id in tangent_contact_ids:
            local_contact = point_positions.get(contact_id)
            if local_contact is None:
                continue
            contact = self._screen_point(
                self._camera_point(self._sketch_world_point(local_contact))
            )
            painter.drawText(
                QPointF(contact.x() + 7.0, contact.y() - 7.0),
                "T",
            )

        for point_id in midpoint_point_ids:
            local_midpoint = point_positions.get(point_id)
            if local_midpoint is None:
                continue
            midpoint = self._screen_point(
                self._camera_point(self._sketch_world_point(local_midpoint))
            )
            painter.drawText(
                QPointF(midpoint.x() + 7.0, midpoint.y() - 7.0),
                "M",
            )

        for point_id in coincident_point_ids:
            local_point = point_positions.get(point_id)
            if local_point is None:
                continue
            point = self._screen_point(
                self._camera_point(self._sketch_world_point(local_point))
            )
            marker_offset = 7.0 + 14.0 * (
                int(point_id in tangent_contact_ids)
                + int(point_id in midpoint_point_ids)
            )
            painter.drawText(
                QPointF(point.x() + 7.0, point.y() - marker_offset),
                "C",
            )

        for first_id, second_id in symmetric_point_pairs:
            first_local = point_positions.get(first_id)
            second_local = point_positions.get(second_id)
            if first_local is None or second_local is None:
                continue
            midpoint = self._screen_point(
                self._camera_point(
                    self._sketch_world_point(
                        (
                            (first_local[0] + second_local[0]) * 0.5,
                            (first_local[1] + second_local[1]) * 0.5,
                        )
                    )
                )
            )
            painter.drawText(
                QPointF(midpoint.x() + 7.0, midpoint.y() - 7.0),
                "S",
            )

        for arc_point in equal_radius_marker_points:
            painter.drawText(
                QPointF(arc_point.x() + 7.0, arc_point.y() - 7.0),
                "=",
            )

        painter.setPen(QPen(constraint_color, 2.0))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for geometry in geometry_by_id.values():
            constraints = geometry.get("constraints", ())
            if not isinstance(constraints, list):
                continue
            constrained_line = geometry_screen_line(geometry)
            if constrained_line is None:
                continue
            for constraint in constraints:
                if (
                    not isinstance(constraint, dict)
                    or constraint.get("type") != "perpendicular"
                ):
                    continue
                reference_id = str(
                    constraint.get("reference_id", "")
                )
                if reference_id in ("sketch_axis:x", "sketch_axis:y"):
                    axis_end = (
                        (1.0, 0.0)
                        if reference_id == "sketch_axis:x"
                        else (0.0, 1.0)
                    )
                    reference_line = tuple(
                        self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(point)
                            )
                        )
                        for point in ((0.0, 0.0), axis_end)
                    )
                    draw_perpendicular_symbol(
                        constrained_line,
                        reference_line,
                    )
                    continue
                if reference_id:
                    external = next(
                        (
                            item
                            for item in self._sketch_external_references
                            if str(item.get("id", "")) == reference_id
                        ),
                        None,
                    )
                    external_geometry = (
                        external.get("geometry")
                        if isinstance(external, dict)
                        else None
                    )
                    raw_line = None
                    if isinstance(external_geometry, dict):
                        if external_geometry.get("type") == "line":
                            raw_line = external_geometry
                        elif external_geometry.get("type") == "lines":
                            raw_lines = external_geometry.get("lines", ())
                            try:
                                line_index = int(
                                    constraint.get("geometry_index", 0)
                                )
                            except (TypeError, ValueError):
                                line_index = 0
                            if (
                                isinstance(raw_lines, (list, tuple))
                                and 0 <= line_index < len(raw_lines)
                                and isinstance(raw_lines[line_index], dict)
                            ):
                                raw_line = raw_lines[line_index]
                    if isinstance(raw_line, dict):
                        point = raw_line.get("point", ())
                        direction = raw_line.get("direction", ())
                        if (
                            isinstance(point, (list, tuple))
                            and isinstance(direction, (list, tuple))
                            and len(point) >= 2
                            and len(direction) >= 2
                        ):
                            external_line = tuple(
                                self._screen_point(
                                    self._camera_point(
                                        self._sketch_world_point(item)
                                    )
                                )
                                for item in (
                                    (float(point[0]), float(point[1])),
                                    (
                                        float(point[0])
                                        + float(direction[0]),
                                        float(point[1])
                                        + float(direction[1]),
                                    ),
                                )
                            )
                            draw_perpendicular_symbol(
                                constrained_line,
                                external_line,
                            )
                            continue
                reference = geometry_by_id.get(
                    str(constraint.get("geometry_id", ""))
                )
                if reference is None:
                    continue
                reference_line = geometry_screen_line(reference)
                if reference_line is not None:
                    draw_perpendicular_symbol(
                        constrained_line,
                        reference_line,
                    )

        if self._sketch_pending_points:
            pending = [
                self._screen_point(
                    self._camera_point(self._sketch_world_point(point))
                )
                for point in self._sketch_pending_points
            ]
            painter.setPen(QPen(QColor("#FF7A00"), 1.5))
            painter.setBrush(QBrush(QColor("#FF7A00")))
            for point in pending:
                painter.drawEllipse(point, 3.5, 3.5)
            if len(pending) >= 2:
                if self._sketch_tool == "spline":
                    sampled = _interpolated_spline_points(
                        self._sketch_pending_points
                    )
                    painter.drawPolyline(QPolygonF([
                        self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(point)
                            )
                        )
                        for point in sampled
                    ]))
                else:
                    painter.drawPolyline(QPolygonF(pending))
            if self._sketch_preview_position is not None:
                preview = self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(
                            self._sketch_preview_position
                        )
                    )
                )
                if self._sketch_tool == "circle":
                    local_center = self._sketch_pending_points[0]
                    local_radius = hypot(
                        self._sketch_preview_position[0] - local_center[0],
                        self._sketch_preview_position[1] - local_center[1],
                    )
                    projected_circle = QPolygonF([
                        self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(
                                    (
                                        local_center[0]
                                        + local_radius * cos(angle),
                                        local_center[1]
                                        + local_radius * sin(angle),
                                    )
                                )
                            )
                        )
                        for angle in (
                            2.0 * pi * index / 96.0
                            for index in range(97)
                        )
                    ])
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawPolyline(projected_circle)
                elif self._sketch_tool == "arc" and len(pending) == 2:
                    sampled = center_arc_points(
                        self._sketch_pending_points[0],
                        self._sketch_pending_points[1],
                        self._sketch_preview_position,
                        clockwise=bool(self._sketch_arc_clockwise),
                    )
                    arc = QPolygonF([
                        self._screen_point(
                            self._camera_point(self._sketch_world_point(point))
                        )
                        for point in sampled
                    ])
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawPolyline(arc)
                elif self._sketch_tool == "spline":
                    sampled = _interpolated_spline_points(
                        self._sketch_pending_points
                        + (self._sketch_preview_position,)
                    )
                    spline = QPolygonF([
                        self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(point)
                            )
                        )
                        for point in sampled
                    ])
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawPolyline(spline)
                else:
                    painter.drawLine(pending[-1], preview)
                painter.drawEllipse(preview, 3.5, 3.5)
                if self._sketch_preview_constraint is not None:
                    center = QPointF(
                        (pending[-1].x() + preview.x()) * 0.5 + 6.0,
                        (pending[-1].y() + preview.y()) * 0.5 - 6.0,
                    )
                    painter.drawText(
                        center,
                        (
                            "H"
                            if self._sketch_preview_constraint
                            == "horizontal"
                            else "V"
                        ),
                    )
        painter.end()

    def _sketch_entity_candidates(self, position: QPointF) -> tuple[str, ...]:
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
            and str(entity.get("id", ""))
        }
        candidates: list[tuple[int, float, int, str]] = []
        for order, entity in enumerate(self._sketch_entities):
            entity_id = str(entity.get("id", ""))
            if not entity_id:
                continue
            entity_type = str(entity.get("type", ""))
            if entity_type == "point":
                raw_points = (
                    point_positions.get(entity_id, (0.0, 0.0)),
                )
            elif isinstance(entity.get("point_ids"), list):
                raw_points = [
                    point_positions[point_id]
                    for point_id in map(str, entity["point_ids"])
                    if point_id in point_positions
                ]
            else:
                raw_points = entity.get("points", ())
            if (
                entity_type == "arc"
                and entity.get("arc_mode") == "center"
                and len(raw_points) >= 3
            ):
                raw_points = center_arc_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                    clockwise=bool(entity.get("clockwise", False)),
                )
            elif entity_type == "spline" and len(raw_points) >= 2:
                raw_points = _interpolated_spline_points(
                    tuple(
                        (float(point[0]), float(point[1]))
                        for point in raw_points
                        if isinstance(point, (list, tuple))
                        and len(point) >= 2
                    )
                )
            screen_points = [
                self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(
                            (float(point[0]), float(point[1]))
                        )
                    )
                )
                for point in raw_points
                if isinstance(point, (list, tuple)) and len(point) >= 2
            ]
            is_construction = (
                entity_type == "construction"
                or entity.get("role") == "construction"
            )
            if (
                entity_type == "construction"
                and len(screen_points) >= 2
            ):
                dx = screen_points[1].x() - screen_points[0].x()
                dy = screen_points[1].y() - screen_points[0].y()
                length = hypot(dx, dy)
                if length > 1e-9:
                    extent = float(
                        max(self.width(), self.height()) * 4 + length
                    )
                    dx /= length
                    dy /= length
                    center = QPointF(
                        (screen_points[0].x() + screen_points[1].x())
                        * 0.5,
                        (screen_points[0].y() + screen_points[1].y())
                        * 0.5,
                    )
                    screen_points = [
                        QPointF(
                            center.x() - dx * extent,
                            center.y() - dy * extent,
                        ),
                        QPointF(
                            center.x() + dx * extent,
                            center.y() + dy * extent,
                        ),
                    ]
            if entity_type == "point" and screen_points:
                distance = hypot(
                    position.x() - screen_points[0].x(),
                    position.y() - screen_points[0].y(),
                )
                if distance <= 9.0:
                    candidates.append((0, distance, order, entity_id))
                continue
            if entity_type == "circle" and len(screen_points) == 1:
                local_radius = float(entity.get("radius", 0.0))
                centre_id = str(entity.get("point_ids", [""])[0])
                local_center = point_positions.get(centre_id)
                if local_center is None:
                    continue
                circle_points = [
                    self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (
                                    local_center[0]
                                    + local_radius * cos(angle),
                                    local_center[1]
                                    + local_radius * sin(angle),
                                )
                            )
                        )
                    )
                    for angle in (
                        2.0 * pi * index / 96.0
                        for index in range(97)
                    )
                ]
                distance = min(
                    self._point_segment_distance(position, first, second)[0]
                    for first, second in zip(
                        circle_points,
                        circle_points[1:],
                    )
                )
                if distance <= 9.0:
                    candidates.append((1, distance, order, entity_id))
                continue
            entity_distances: list[float] = []
            for first, second in zip(screen_points, screen_points[1:]):
                distance, _fraction = self._point_segment_distance(
                    position,
                    first,
                    second,
                )
                entity_distances.append(distance)
            if entity_distances and min(entity_distances) <= 9.0:
                candidates.append(
                    (
                        2 if is_construction else 1,
                        min(entity_distances),
                        order,
                        entity_id,
                    )
                )
        for distance, vertex_id in self._corner_radius_handle_candidates(
            position
        ):
            candidates.append((0, distance, -1, vertex_id))
        candidates.sort()
        return tuple(candidate[3] for candidate in candidates)

    def _corner_radius_handle_candidates(
        self,
        position: QPointF,
    ) -> list[tuple[float, str]]:
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") == "segment"
        }
        candidates: list[tuple[float, str]] = []
        for first in geometry_by_id.values():
            first_ids = tuple(map(str, first.get("point_ids", ())))
            records = first.get("corner_radii", ())
            if len(first_ids) != 2 or not isinstance(records, list):
                continue
            for record in records:
                if not isinstance(record, dict):
                    continue
                if bool(record.get("suppressed", False)):
                    continue
                second = geometry_by_id.get(
                    str(record.get("other_geometry_id", ""))
                )
                vertex_id = str(record.get("vertex_id", ""))
                second_ids = (
                    tuple(map(str, second.get("point_ids", ())))
                    if second is not None
                    else ()
                )
                if (
                    len(second_ids) != 2
                    or vertex_id not in first_ids
                    or vertex_id not in second_ids
                ):
                    continue
                vertex = point_positions.get(vertex_id)
                first_outer = point_positions.get(
                    next(item for item in first_ids if item != vertex_id)
                )
                second_outer = point_positions.get(
                    next(item for item in second_ids if item != vertex_id)
                )
                if (
                    vertex is None
                    or first_outer is None
                    or second_outer is None
                ):
                    continue
                evaluated = evaluate_corner_radius(
                    vertex,
                    first_outer,
                    second_outer,
                    float(record.get("radius", 0.0)),
                )
                if evaluated is None:
                    continue
                for handle in (
                    evaluated.first_tangent,
                    evaluated.second_tangent,
                ):
                    screen = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(handle)
                        )
                    )
                    distance = hypot(
                        position.x() - screen.x(),
                        position.y() - screen.y(),
                    )
                    if distance <= 9.0:
                        candidates.append((distance, vertex_id))
        return candidates

    def _corner_radius_drag_candidate(
        self,
        position: QPointF,
    ) -> str | None:
        if len(self._selected_sketch_entity_ids) != 2:
            return None
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") == "segment"
        }
        selected_ids = sorted(self._selected_sketch_entity_ids)
        first = geometry_by_id.get(selected_ids[0])
        second = geometry_by_id.get(selected_ids[1])
        if first is None or second is None:
            return None
        first_ids = tuple(map(str, first.get("point_ids", ())))
        second_ids = tuple(map(str, second.get("point_ids", ())))
        shared = set(first_ids) & set(second_ids)
        if len(first_ids) != 2 or len(second_ids) != 2 or len(shared) != 1:
            return None
        vertex_id = next(iter(shared))
        vertex = point_positions.get(vertex_id)
        first_outer = point_positions.get(
            next(point_id for point_id in first_ids if point_id != vertex_id)
        )
        second_outer = point_positions.get(
            next(point_id for point_id in second_ids if point_id != vertex_id)
        )
        if vertex is None or first_outer is None or second_outer is None:
            return None
        handles = [vertex]
        records = first.get("corner_radii", ())
        if isinstance(records, list):
            record = next(
                (
                    item
                    for item in records
                    if isinstance(item, dict)
                    and str(item.get("other_geometry_id", ""))
                    == selected_ids[1]
                    and str(item.get("vertex_id", "")) == vertex_id
                ),
                None,
            )
            if record is not None:
                if not bool(record.get("suppressed", False)):
                    evaluated = evaluate_corner_radius(
                        vertex,
                        first_outer,
                        second_outer,
                        float(record.get("radius", 0.0)),
                    )
                    if evaluated is not None:
                        handles.extend(
                            (
                                evaluated.first_tangent,
                                evaluated.second_tangent,
                            )
                        )
        for handle in handles:
            screen = self._screen_point(
                self._camera_point(self._sketch_world_point(handle))
            )
            if hypot(
                position.x() - screen.x(),
                position.y() - screen.y(),
            ) <= 10.0:
                return vertex_id
        return None

    def _corner_radius_candidate(
        self,
        position: QPointF,
    ) -> tuple[str, str, str] | None:
        point_positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") == "segment"
        }
        nearest: tuple[float, str, str, str] | None = None
        for first_id, first in geometry_by_id.items():
            first_ids = tuple(map(str, first.get("point_ids", ())))
            records = first.get("corner_radii", ())
            if len(first_ids) != 2 or not isinstance(records, list):
                continue
            for record in records:
                if not isinstance(record, dict):
                    continue
                if bool(record.get("suppressed", False)):
                    continue
                second_id = str(record.get("other_geometry_id", ""))
                vertex_id = str(record.get("vertex_id", ""))
                second = geometry_by_id.get(second_id)
                second_ids = (
                    tuple(map(str, second.get("point_ids", ())))
                    if second is not None
                    else ()
                )
                if (
                    len(second_ids) != 2
                    or vertex_id not in first_ids
                    or vertex_id not in second_ids
                ):
                    continue
                vertex = point_positions.get(vertex_id)
                first_outer = point_positions.get(
                    next(item for item in first_ids if item != vertex_id)
                )
                second_outer = point_positions.get(
                    next(item for item in second_ids if item != vertex_id)
                )
                if (
                    vertex is None
                    or first_outer is None
                    or second_outer is None
                ):
                    continue
                evaluated = evaluate_corner_radius(
                    vertex,
                    first_outer,
                    second_outer,
                    float(record.get("radius", 0.0)),
                )
                if evaluated is None:
                    continue
                screen_arc = [
                    self._screen_point(
                        self._camera_point(self._sketch_world_point(point))
                    )
                    for point in evaluated.arc_points
                ]
                distance = min(
                    self._point_segment_distance(position, first, second)[0]
                    for first, second in zip(
                        screen_arc,
                        screen_arc[1:],
                    )
                )
                if distance <= 9.0 and (
                    nearest is None or distance < nearest[0]
                ):
                    nearest = (
                        distance,
                        first_id,
                        second_id,
                        vertex_id,
                    )
        return (
            (nearest[1], nearest[2], nearest[3])
            if nearest is not None
            else None
        )

    def _pick_sketch_entity(self, position: QPointF) -> str | None:
        candidates = self._sketch_entity_candidates(position)
        return candidates[0] if candidates else None

    def _sketch_placement_candidate(
        self,
        position: QPointF,
    ) -> tuple[tuple[float, float], str | None, str | None]:
        local = self._sketch_local_position(position) or (0.0, 0.0)
        nearest_point = self._sketch_point_candidate(position)
        if nearest_point is not None:
            return nearest_point[2], None, None
        if self._sketch_reference_snapping:
            sketch_curve = self._sketch_curve_reference_candidate(position)
            if sketch_curve is not None:
                return sketch_curve[1], sketch_curve[0], None
            sketch_line = self._sketch_line_reference_candidate(position)
            if sketch_line is not None:
                geometry_id, snapped = sketch_line
                reference_id = f"sketch_geometry:{geometry_id}"
                constraint = self._sketch_inferred_direction_constraint(
                    snapped
                )
                if constraint is not None:
                    combined = self._sketch_reference_direction_snap(
                        reference_id,
                        constraint,
                        snapped,
                    )
                    if combined is None:
                        constraint = None
                    else:
                        snapped = combined
                return snapped, reference_id, constraint
            reference = self._sketch_external_reference_candidate(
                position
            )
            if reference is not None:
                reference_id, snapped = reference
                constraint = (
                    self._sketch_inferred_direction_constraint(snapped)
                )
                if constraint is not None:
                    combined = self._sketch_reference_direction_snap(
                        reference_id,
                        constraint,
                        snapped,
                    )
                    if combined is None:
                        constraint = None
                    else:
                        snapped = combined
                return (
                    snapped,
                    reference_id,
                    constraint,
                )

        if self._sketch_pending_points:
            constraint = self._sketch_inferred_direction_constraint(
                local
            )
            if constraint == "horizontal":
                first = self._sketch_pending_points[-1]
                return (local[0], first[1]), None, constraint
            if constraint == "vertical":
                first = self._sketch_pending_points[-1]
                return (first[0], local[1]), None, constraint
        return local, None, None

    def _sketch_curve_reference_candidate(
        self,
        position: QPointF,
    ) -> tuple[str, tuple[float, float]] | None:
        local = self._sketch_local_position(position)
        if local is None:
            return None
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        candidates: list[
            tuple[float, str, tuple[float, float]]
        ] = []
        for circle in self._sketch_entities:
            if circle.get("type") != "circle":
                continue
            ids = tuple(map(str, circle.get("point_ids", ())))
            center = points.get(ids[0]) if ids else None
            radius = float(circle.get("radius", 0.0))
            if center is None or radius <= 1.0e-12:
                continue
            dx, dy = local[0] - center[0], local[1] - center[1]
            length = hypot(dx, dy)
            if length <= 1.0e-12:
                continue
            snapped = (
                center[0] + radius * dx / length,
                center[1] + radius * dy / length,
            )
            screen = self._screen_point(
                self._camera_point(self._sketch_world_point(snapped))
            )
            distance = hypot(position.x() - screen.x(), position.y() - screen.y())
            if distance <= 12.0:
                candidates.append((
                    distance,
                    f"sketch_circle:{circle.get('id', '')}",
                    snapped,
                ))
        for arc in self._sketch_entities:
            if arc.get("type") != "arc":
                continue
            ids = tuple(map(str, arc.get("point_ids", ())))
            if len(ids) != 3 or any(point_id not in points for point_id in ids):
                continue
            sampled = center_arc_points(
                points[ids[0]],
                points[ids[1]],
                points[ids[2]],
                segments=64,
                clockwise=bool(arc.get("clockwise", False)),
            )
            for arc_point in sampled:
                screen = self._screen_point(
                    self._camera_point(self._sketch_world_point(arc_point))
                )
                distance = hypot(
                    position.x() - screen.x(),
                    position.y() - screen.y(),
                )
                if distance <= 12.0:
                    candidates.append((
                        distance,
                        f"sketch_arc:{arc.get('id', '')}",
                        arc_point,
                    ))
        geometry = {
            str(entity.get("id", "")): entity
            for entity in self._sketch_entities
            if entity.get("type") == "segment"
        }
        for first_id, first in geometry.items():
            first_ids = tuple(map(str, first.get("point_ids", ())))
            for record in first.get("corner_radii", ()):
                if not isinstance(record, dict) or bool(record.get("suppressed", False)):
                    continue
                second_id = str(record.get("other_geometry_id", ""))
                vertex_id = str(record.get("vertex_id", ""))
                second = geometry.get(second_id)
                second_ids = tuple(map(str, second.get("point_ids", ()))) if second else ()
                if len(first_ids) != 2 or len(second_ids) != 2 or vertex_id not in first_ids or vertex_id not in second_ids:
                    continue
                vertex = points.get(vertex_id)
                outer_a = points.get(next(item for item in first_ids if item != vertex_id))
                outer_b = points.get(next(item for item in second_ids if item != vertex_id))
                if vertex is None or outer_a is None or outer_b is None:
                    continue
                evaluated = evaluate_corner_radius(vertex, outer_a, outer_b, float(record.get("radius", 0.0)))
                if evaluated is None:
                    continue
                for arc_point in evaluated.arc_points:
                    screen = self._screen_point(self._camera_point(self._sketch_world_point(arc_point)))
                    distance = hypot(position.x() - screen.x(), position.y() - screen.y())
                    if distance <= 12.0:
                        radius_id = str(record.get("id") or f"radius:{first_id}:{second_id}:{vertex_id}")
                        candidates.append((distance, f"sketch_radius:{radius_id}", arc_point))
        if not candidates:
            return None
        _distance, reference_id, snapped = min(candidates, key=lambda item: item[0])
        return reference_id, snapped

    def _sketch_line_reference_candidate(
        self,
        position: QPointF,
    ) -> tuple[str, tuple[float, float]] | None:
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        nearest = None
        for geometry in self._sketch_entities:
            geometry_type = geometry.get("type")
            if geometry_type not in ("segment", "construction"):
                continue
            point_ids = geometry.get("point_ids", ())
            if not isinstance(point_ids, list) or len(point_ids) < 2:
                continue
            first = points.get(str(point_ids[0]))
            second = points.get(str(point_ids[1]))
            if first is None or second is None:
                continue
            first_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(first))
            )
            second_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(second))
            )
            dx = second_screen.x() - first_screen.x()
            dy = second_screen.y() - first_screen.y()
            squared_length = dx * dx + dy * dy
            if squared_length <= 1.0e-12:
                continue
            factor = (
                (position.x() - first_screen.x()) * dx
                + (position.y() - first_screen.y()) * dy
            ) / squared_length
            if geometry_type != "construction" and not 0.0 <= factor <= 1.0:
                continue
            projected_screen = QPointF(
                first_screen.x() + factor * dx,
                first_screen.y() + factor * dy,
            )
            distance = hypot(
                position.x() - projected_screen.x(),
                position.y() - projected_screen.y(),
            )
            if distance > 12.0:
                continue
            snapped = (
                first[0] + factor * (second[0] - first[0]),
                first[1] + factor * (second[1] - first[1]),
            )
            candidate = (
                distance,
                str(geometry.get("id", "")),
                snapped,
            )
            if nearest is None or candidate[0] < nearest[0]:
                nearest = candidate
        return (
            (nearest[1], nearest[2])
            if nearest is not None and nearest[1]
            else None
        )

    def _sketch_inferred_direction_constraint(
        self,
        candidate: tuple[float, float],
    ) -> str | None:
        if not self._sketch_pending_points:
            return None
        first = self._sketch_pending_points[-1]
        candidate_screen = self._screen_point(
            self._camera_point(self._sketch_world_point(candidate))
        )
        horizontal_screen = self._screen_point(
            self._camera_point(
                self._sketch_world_point((candidate[0], first[1]))
            )
        )
        vertical_screen = self._screen_point(
            self._camera_point(
                self._sketch_world_point((first[0], candidate[1]))
            )
        )
        horizontal_distance = hypot(
            candidate_screen.x() - horizontal_screen.x(),
            candidate_screen.y() - horizontal_screen.y(),
        )
        vertical_distance = hypot(
            candidate_screen.x() - vertical_screen.x(),
            candidate_screen.y() - vertical_screen.y(),
        )
        if (
            horizontal_distance <= 10.0
            and horizontal_distance <= vertical_distance
        ):
            return "horizontal"
        if vertical_distance <= 10.0:
            return "vertical"
        return None

    def _sketch_reference_direction_snap(
        self,
        reference_id: str,
        constraint: str,
        snapped: tuple[float, float],
    ) -> tuple[float, float] | None:
        if not self._sketch_pending_points:
            return None
        first = self._sketch_pending_points[-1]
        if reference_id == "sketch_origin":
            return (
                (0.0, 0.0)
                if (
                    (constraint == "horizontal" and abs(first[1]) <= 1e-9)
                    or (
                        constraint == "vertical"
                        and abs(first[0]) <= 1e-9
                    )
                )
                else None
            )
        if reference_id == "sketch_axis:x":
            geometry = {
                "type": "line",
                "point": (0.0, 0.0),
                "direction": (1.0, 0.0),
            }
        elif reference_id == "sketch_axis:y":
            geometry = {
                "type": "line",
                "point": (0.0, 0.0),
                "direction": (0.0, 1.0),
            }
        elif reference_id.startswith("sketch_geometry:"):
            geometry_id = reference_id.split(":", 1)[1]
            points = {
                str(entity.get("id", "")): (
                    float(entity.get("x", 0.0)),
                    float(entity.get("y", 0.0)),
                )
                for entity in self._sketch_entities
                if entity.get("type") == "point"
            }
            source = next(
                (
                    entity
                    for entity in self._sketch_entities
                    if str(entity.get("id", "")) == geometry_id
                    and entity.get("type") in ("segment", "construction")
                ),
                None,
            )
            point_ids = (
                list(map(str, source.get("point_ids", ())))
                if source is not None
                and isinstance(source.get("point_ids"), list)
                else []
            )
            line_first = points.get(point_ids[0]) if len(point_ids) == 2 else None
            line_second = points.get(point_ids[1]) if len(point_ids) == 2 else None
            geometry = (
                {
                    "type": "line",
                    "point": line_first,
                    "direction": (
                        line_second[0] - line_first[0],
                        line_second[1] - line_first[1],
                    ),
                    "bounded": source.get("type") == "segment",
                }
                if source is not None
                and line_first is not None
                and line_second is not None
                else None
            )
        else:
            reference = next(
                (
                    item
                    for item in self._sketch_external_references
                    if str(item.get("id", "")) == reference_id
                ),
                None,
            )
            geometry = (
                reference.get("geometry")
                if isinstance(reference, dict)
                else None
            )
        if not isinstance(geometry, dict):
            return None
        raw_lines = (
            (geometry,)
            if geometry.get("type") == "line"
            else geometry.get("lines", ())
            if geometry.get("type") == "lines"
            else ()
        )
        candidates: list[tuple[float, float]] = []
        for line in raw_lines:
            if not isinstance(line, dict):
                continue
            point = line.get("point", ())
            direction = line.get("direction", ())
            if (
                not isinstance(point, (list, tuple))
                or not isinstance(direction, (list, tuple))
                or len(point) < 2
                or len(direction) < 2
            ):
                continue
            px, py = float(point[0]), float(point[1])
            dx, dy = float(direction[0]), float(direction[1])
            if constraint == "horizontal":
                if abs(dy) > 1.0e-12:
                    factor = (first[1] - py) / dy
                    if not bool(line.get("bounded", False)) or 0.0 <= factor <= 1.0:
                        candidates.append((px + factor * dx, first[1]))
                elif abs(py - first[1]) <= 1.0e-9:
                    candidates.append((snapped[0], first[1]))
            elif constraint == "vertical":
                if abs(dx) > 1.0e-12:
                    factor = (first[0] - px) / dx
                    if not bool(line.get("bounded", False)) or 0.0 <= factor <= 1.0:
                        candidates.append((first[0], py + factor * dy))
                elif abs(px - first[0]) <= 1.0e-9:
                    candidates.append((first[0], snapped[1]))
        return (
            min(
                candidates,
                key=lambda candidate: (
                    (candidate[0] - snapped[0]) ** 2
                    + (candidate[1] - snapped[1]) ** 2
                ),
            )
            if candidates
            else None
        )

    def _sketch_point_candidate(
        self,
        position: QPointF,
    ) -> tuple[float, str, tuple[float, float]] | None:
        nearest_point: (
            tuple[float, str, tuple[float, float]] | None
        ) = None
        for entity in self._sketch_entities:
            if entity.get("type") != "point":
                continue
            point_id = str(entity.get("id", ""))
            if not point_id:
                continue
            point = (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            screen = self._screen_point(
                self._camera_point(self._sketch_world_point(point))
            )
            distance = hypot(
                position.x() - screen.x(),
                position.y() - screen.y(),
            )
            if (
                distance <= 12.0
                and (
                    nearest_point is None
                    or distance < nearest_point[0]
                )
            ):
                nearest_point = (distance, point_id, point)
        return nearest_point

    def _sketch_external_reference_candidate(
        self,
        position: QPointF,
    ) -> tuple[str, tuple[float, float]] | None:
        tolerance = 12.0
        nearest: tuple[float, str, tuple[float, float]] | None = None

        def consider_line(reference_id: str, raw_line) -> None:
            nonlocal nearest
            if not isinstance(raw_line, dict):
                return
            point = raw_line.get("point")
            direction = raw_line.get("direction")
            if (
                not isinstance(point, (list, tuple))
                or not isinstance(direction, (list, tuple))
                or len(point) < 2
                or len(direction) < 2
            ):
                return
            px, py = float(point[0]), float(point[1])
            dx, dy = float(direction[0]), float(direction[1])
            squared_length = dx * dx + dy * dy
            if squared_length <= 1.0e-18:
                return
            screen_point = self._screen_point(
                self._camera_point(self._sketch_world_point((px, py)))
            )
            screen_direction_end = self._screen_point(
                self._camera_point(
                    self._sketch_world_point((px + dx, py + dy))
                )
            )
            screen_dx = screen_direction_end.x() - screen_point.x()
            screen_dy = screen_direction_end.y() - screen_point.y()
            screen_squared_length = (
                screen_dx * screen_dx + screen_dy * screen_dy
            )
            if screen_squared_length <= 1.0e-18:
                return
            factor = (
                (position.x() - screen_point.x()) * screen_dx
                + (position.y() - screen_point.y()) * screen_dy
            ) / screen_squared_length
            snapped = (px + factor * dx, py + factor * dy)
            distance = hypot(
                position.x() - (screen_point.x() + factor * screen_dx),
                position.y() - (screen_point.y() + factor * screen_dy),
            )
            if (
                distance <= tolerance
                and (nearest is None or distance < nearest[0])
            ):
                nearest = (distance, reference_id, snapped)

        origin_screen = self._screen_point(
            self._camera_point(self._sketch_world_point((0.0, 0.0)))
        )
        origin_distance = hypot(
            position.x() - origin_screen.x(),
            position.y() - origin_screen.y(),
        )
        if origin_distance <= tolerance:
            return ("sketch_origin", (0.0, 0.0))
        consider_line(
            "sketch_axis:x",
            {
                "point": (0.0, 0.0),
                "direction": (1.0, 0.0),
            },
        )
        consider_line(
            "sketch_axis:y",
            {
                "point": (0.0, 0.0),
                "direction": (0.0, 1.0),
            },
        )
        for reference in self._sketch_external_references:
            reference_id = str(reference.get("id", ""))
            geometry = reference.get("geometry")
            if not reference_id or not isinstance(geometry, dict):
                continue
            geometry_type = geometry.get("type")
            if geometry_type == "line":
                consider_line(reference_id, geometry)
            elif geometry_type == "lines":
                raw_lines = geometry.get("lines", ())
                if isinstance(raw_lines, (list, tuple)):
                    for line_index, raw_line in enumerate(raw_lines):
                        source_line_index = (
                            int(raw_line.get("_reference_line_index", line_index))
                            if isinstance(raw_line, dict)
                            else line_index
                        )
                        consider_line(
                            f"{reference_id}::line:{source_line_index}",
                            raw_line,
                        )
            elif geometry_type == "point":
                point = geometry.get("point")
                if (
                    not isinstance(point, (list, tuple))
                    or len(point) < 2
                ):
                    continue
                snapped = (float(point[0]), float(point[1]))
                screen = self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(snapped)
                    )
                )
                distance = hypot(
                    position.x() - screen.x(),
                    position.y() - screen.y(),
                )
                if (
                    distance <= tolerance
                    and (nearest is None or distance < nearest[0])
                ):
                    nearest = (distance, reference_id, snapped)
        return (
            (nearest[1], nearest[2])
            if nearest is not None
            else None
        )

    def _cycle_sketch_entity(self, position: QPointF) -> None:
        candidates = self._sketch_selection_candidates(position)
        if not candidates:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
            self.update()
            return
        if candidates != self._sketch_cycle_ids:
            self._sketch_cycle_ids = candidates
            self._sketch_cycle_index = 0
        else:
            self._sketch_cycle_index = (
                self._sketch_cycle_index + 1
            ) % len(candidates)
        active_candidate = candidates[self._sketch_cycle_index]
        if active_candidate.startswith("reference:"):
            self._preview_sketch_entity_id = None
            reference_id = active_candidate.removeprefix("reference:")
            self._hovered_sketch_external_reference_id = reference_id
            self.sketchReferenceHovered.emit(reference_id)
        else:
            self._preview_sketch_entity_id = active_candidate
            if self._hovered_sketch_external_reference_id is not None:
                self._hovered_sketch_external_reference_id = None
                self.sketchReferenceHovered.emit("")
        self.update()

    def _sketch_selection_candidates(
        self,
        position: QPointF,
    ) -> tuple[str, ...]:
        candidates = list(self._sketch_entity_candidates(position))
        reference = self._sketch_external_reference_candidate(position)
        if reference is not None:
            candidates.append(f"reference:{reference[0]}")
        return tuple(candidates)

    def _paint_object_highlights(self) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        highlights = (
            (self._hovered_object_id, QColor.fromRgbF(1.0, 0.48, 0.0)),
            (self._selected_object_id, QColor.fromRgbF(0.0, 0.82, 1.0)),
        )
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for owner_id, color in highlights:
            if owner_id is None:
                continue
            painter.setPen(QPen(color, 3.0))
            for edge in mesh.edges:
                if (
                    edge.owner_id != owner_id
                    or edge.element_kind not in {"edge", "sketch"}
                ):
                    continue
                projected = [
                    self._screen_point(self._camera_point(point))
                    for point in edge.points
                ]
                for index in range(1, len(projected)):
                    painter.drawLine(
                        projected[index - 1],
                        projected[index],
                    )
        painter.end()

    def _paint_reference_highlights(self) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for edge in mesh.edges:
            key = (edge.owner_id, edge.edge_index)
            color = None
            if key == self._hovered_edge:
                color = QColor.fromRgbF(1.0, 0.48, 0.0)
            if (
                key == self._selected_edge
                or key in self._constraint_reference_edges
                or edge.owner_id in {
                    self._selected_reference_owner_id,
                }
                or edge.owner_id in self._constraint_reference_owner_ids
                or edge.owner_id in self._selected_container_content_ids
            ):
                color = QColor.fromRgbF(0.0, 0.82, 1.0)
            if color is None or edge.element_kind not in {
                "axis",
                "centerline",
                "edge",
                "sketch",
            }:
                continue
            painter.setPen(
                self._datum_centerline_pen(color, 3.0)
                if edge.element_kind == "centerline"
                else QPen(color, 3.0, Qt.PenStyle.SolidLine)
            )
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_edge_points(edge)
            ]
            for index in range(1, len(projected)):
                painter.drawLine(projected[index - 1], projected[index])
        painter.end()

    def _paint_centerlines(self) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for edge in mesh.edges:
            if edge.element_kind != "centerline":
                continue
            painter.setPen(
                self._datum_centerline_pen(
                    QColor.fromRgbF(*edge.base_color, 1.0),
                    1.5,
                )
            )
            projected = [
                self._screen_point(self._camera_point(point))
                for point in edge.points
            ]
            for index in range(1, len(projected)):
                painter.drawLine(projected[index - 1], projected[index])
        painter.end()

    @staticmethod
    def _datum_centerline_pen(color: QColor, width: float) -> QPen:
        highlight_width = 3.0
        highlight_pattern = (9.0, 4.0, 2.0, 4.0)
        pen = QPen(color, width, Qt.PenStyle.CustomDashLine)
        pen.setDashPattern(
            [
                value * highlight_width / width
                for value in highlight_pattern
            ]
        )
        return pen

    def selection_candidates_at(
        self,
        position: QPointF,
    ) -> tuple[tuple[str, str, int], ...]:
        mesh = self._mesh
        if mesh is None:
            return ()
        candidates: list[tuple[str, str, int]] = []
        entity_id = self._pick_object(position)
        if entity_id is not None:
            candidates.append(("object", entity_id, 0))
        threshold = 9.0 * float(self.devicePixelRatioF())
        for marker in mesh.points:
            screen = self._screen_point(self._camera_point(marker.position))
            if hypot(position.x() - screen.x(), position.y() - screen.y()) <= threshold:
                candidates.append(
                    ("point", marker.owner_id, marker.point_index)
                )
        for edge in mesh.edges:
            if edge.element_kind == "sketch":
                candidate_kind = "object"
            elif edge.element_kind in {"axis", "centerline"}:
                candidate_kind = "edge"
            else:
                continue
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_edge_points(edge)
            ]
            if any(
                self._point_segment_distance(
                    position,
                    projected[index - 1],
                    projected[index],
                )[0] <= threshold
                for index in range(1, len(projected))
            ):
                candidates.append(
                    (
                        candidate_kind,
                        edge.owner_id,
                        0 if candidate_kind == "object" else edge.edge_index,
                    )
                )
        for plane in mesh.planes:
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_plane_corners(plane)
            ]
            if any(
                self._point_segment_distance(
                    position,
                    projected[index],
                    projected[(index + 1) % len(projected)],
                )[0] <= threshold
                for index in range(len(projected))
            ):
                candidates.append(
                    ("plane", plane.owner_id, plane.plane_index)
                )
        return tuple(dict.fromkeys(candidates))

    def topology_candidates_at(
        self,
        position: QPointF,
    ) -> tuple[tuple[str, str, int], ...]:
        mesh = self._mesh
        if mesh is None:
            return ()
        threshold = 9.0 * float(self.devicePixelRatioF())
        candidates: list[tuple[str, str, int]] = []
        for marker in mesh.points:
            screen = self._screen_point(self._camera_point(marker.position))
            if hypot(position.x() - screen.x(), position.y() - screen.y()) <= threshold:
                candidates.append(("point", marker.owner_id, marker.point_index))
        for edge in mesh.edges:
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_edge_points(edge)
            ]
            if any(
                self._point_segment_distance(
                    position,
                    projected[index - 1],
                    projected[index],
                )[0] <= threshold
                for index in range(1, len(projected))
            ):
                candidates.append(("edge", edge.owner_id, edge.edge_index))
        for plane in mesh.planes:
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_plane_corners(plane)
            ]
            if any(
                self._point_segment_distance(
                    position,
                    projected[index],
                    projected[(index + 1) % len(projected)],
                )[0] <= threshold
                for index in range(len(projected))
            ):
                candidates.append(("plane", plane.owner_id, plane.plane_index))
        positions = mesh.triangle_positions
        sample_offsets = (
            (0.0, 0.0),
            (-4.0, 0.0),
            (4.0, 0.0),
            (0.0, -4.0),
            (0.0, 4.0),
        )
        for triangle_index, face_index in enumerate(mesh.triangle_face_indices):
            offset = triangle_index * 9
            camera_points = [
                self._camera_point(
                    tuple(
                        positions[offset + vertex * 3 + axis]
                        for axis in range(3)
                    )
                )
                for vertex in range(3)
            ]
            screen_points = tuple(
                self._screen_point(point) for point in camera_points
            )
            if any(
                self._triangle_weights(
                    QPointF(
                        position.x() + offset_x,
                        position.y() + offset_y,
                    ),
                    *screen_points,
                ) is not None
                for offset_x, offset_y in sample_offsets
            ):
                candidates.append(
                    (
                        "face",
                        mesh.triangle_owner_ids[triangle_index],
                        face_index,
                    )
                )
        return tuple(dict.fromkeys(candidates))

    def preview_topology_candidate(
        self,
        candidate: tuple[str, str, int],
    ) -> None:
        self._cycled_topology_candidate = candidate
        self._clear_topology_hover()
        kind, owner_id, element_index = candidate
        {
            "point": self._set_hovered_point,
            "edge": self._set_hovered_edge,
            "plane": self._set_hovered_plane,
            "face": self._set_hovered_face,
        }[kind]((owner_id, element_index))

    def _paint_planes(self) -> None:
        mesh = self._mesh
        if mesh is None or not mesh.planes:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for plane in mesh.planes:
            key = (plane.owner_id, plane.plane_index)
            color = plane.base_color
            if key == self._hovered_plane:
                color = (1.0, 0.48, 0.0)
            if (
                key == self._selected_plane
                or key in self._constraint_reference_planes
            ):
                color = (0.0, 0.82, 1.0)
            if (
                plane.owner_id == self._selected_reference_owner_id
                or plane.owner_id in self._constraint_reference_owner_ids
            ):
                color = (0.0, 0.82, 1.0)
            if plane.owner_id in self._selected_container_content_ids:
                color = (0.0, 0.82, 1.0)
            polygon = QPolygonF(
                [
                    self._screen_point(self._camera_point(point))
                    for point in self._display_plane_corners(plane)
                ]
            )
            outline = QColor.fromRgbF(*color, 1.0)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.setPen(
                QPen(
                    outline,
                    3.0
                    if (
                        key in (self._hovered_plane, self._selected_plane)
                        or key in self._constraint_reference_planes
                    )
                    else 1.5,
                )
            )
            painter.drawPolyline(polygon)
            painter.drawLine(polygon[-1], polygon[0])
            if plane.label:
                label_anchor = polygon[0]
                painter.drawText(
                    QPointF(
                        label_anchor.x() + 6.0,
                        label_anchor.y() - 5.0,
                    ),
                    plane.label,
                )
        painter.end()

    def _pick_plane(self, position: QPointF) -> TopologyKey | None:
        if self._selection_filter not in {"all", "plane", "normal"}:
            return None
        mesh = self._mesh
        if mesh is None:
            return None
        hits: list[tuple[float, float, str, int]] = []
        threshold = 8.0 * float(self.devicePixelRatioF())
        for plane in mesh.planes:
            camera_points = [
                self._camera_point(point)
                for point in self._display_plane_corners(plane)
            ]
            screen_points = [
                self._screen_point(point)
                for point in camera_points
            ]
            for index in range(4):
                next_index = (index + 1) % 4
                distance, fraction = self._point_segment_distance(
                    position,
                    screen_points[index],
                    screen_points[next_index],
                )
                if distance > threshold:
                    continue
                depth = (
                    camera_points[index][2] * (1.0 - fraction)
                    + camera_points[next_index][2] * fraction
                )
                hits.append(
                    (distance, -depth, plane.owner_id, plane.plane_index)
                )
        if not hits:
            return None
        selected = min(hits)
        return selected[2], selected[3]

    def _paint_points(self) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for marker in mesh.points:
            key = (marker.owner_id, marker.point_index)
            color = marker.base_color
            if key == self._hovered_point:
                color = (1.0, 0.48, 0.0)
            if key == self._selected_point:
                color = (0.0, 0.82, 1.0)
            if (
                key in self._constraint_reference_points
                or marker.owner_id == self._selected_reference_owner_id
                or marker.owner_id in self._constraint_reference_owner_ids
            ):
                color = (0.0, 0.82, 1.0)
            if marker.owner_id == self._selected_container_origin_id:
                color = (0.0, 0.82, 1.0)
            if marker.owner_id in self._selected_container_content_ids:
                color = (0.0, 0.82, 1.0)
            screen = self._screen_point(self._camera_point(marker.position))
            marker_color = QColor.fromRgbF(*color, 1.0)
            painter.setPen(QPen(marker_color, 1.0))
            painter.setBrush(QBrush(marker_color))
            radius = (
                6.0
                if (
                    key in (self._hovered_point, self._selected_point)
                    or key in self._constraint_reference_points
                    or marker.owner_id == self._selected_reference_owner_id
                    or marker.owner_id in self._constraint_reference_owner_ids
                    or marker.owner_id == self._selected_container_origin_id
                    or marker.owner_id in self._selected_container_content_ids
                )
                else 4.5
            )
            painter.drawEllipse(screen, radius, radius)
            if marker.label:
                painter.setPen(
                    QPen(QColor.fromRgbF(*color, 1.0), 1.0)
                )
                painter.drawText(
                    QPointF(
                        screen.x() + radius + 4.0,
                        screen.y() - radius - 2.0,
                    ),
                    marker.label,
                )
        painter.setPen(QPen(QColor.fromRgbF(0.0, 0.82, 1.0), 1.0))
        painter.setBrush(QBrush(QColor.fromRgbF(0.0, 0.82, 1.0)))
        for position in self._constraint_reference_positions:
            screen = self._screen_point(self._camera_point(position))
            painter.drawEllipse(screen, 6.0, 6.0)
        painter.end()

    def _pick_point(self, position: QPointF) -> TopologyKey | None:
        if self._selection_filter not in {"all", "point"}:
            return None
        mesh = self._mesh
        if mesh is None:
            return None
        hits: list[tuple[float, str, int]] = []
        threshold = 9.0 * float(self.devicePixelRatioF())
        for marker in mesh.points:
            screen = self._screen_point(self._camera_point(marker.position))
            distance = hypot(
                position.x() - screen.x(),
                position.y() - screen.y(),
            )
            if distance <= threshold:
                hits.append(
                    (distance, marker.owner_id, marker.point_index)
                )
        if not hits:
            return None
        selected = min(hits)
        return selected[1], selected[2]

    def _point_marker_is_visible(self, point: Point3) -> bool:
        mesh = self._mesh
        if mesh is None or not mesh.triangle_face_indices:
            return True
        camera_point = self._camera_point(point)
        screen = self._screen_point(camera_point)
        positions = mesh.triangle_positions
        front_depth: float | None = None
        for triangle_index in range(len(mesh.triangle_face_indices)):
            offset = triangle_index * 9
            triangle_points = [
                self._camera_point(
                    (
                        positions[offset + vertex * 3],
                        positions[offset + vertex * 3 + 1],
                        positions[offset + vertex * 3 + 2],
                    )
                )
                for vertex in range(3)
            ]
            weights = self._triangle_weights(
                screen,
                *(self._screen_point(item) for item in triangle_points),
            )
            if weights is None:
                continue
            depth = sum(
                weight * item[2]
                for weight, item in zip(weights, triangle_points)
            )
            front_depth = depth if front_depth is None else max(front_depth, depth)
        if front_depth is None:
            return True
        tolerance = self._scene_radius * 1e-5
        return camera_point[2] >= front_depth - tolerance

    def _pick_edge(self, position: QPointF) -> TopologyKey | None:
        mesh = self._mesh
        if mesh is None or mesh.is_empty:
            return None
        candidates: list[tuple[float, float, str, int]] = []
        threshold = 8.0 * float(self.devicePixelRatioF())
        for edge in mesh.edges:
            if self._selection_filter == "axis":
                if edge.element_kind not in {"axis", "centerline"}:
                    continue
            elif self._selection_filter != "all":
                continue
            camera_points = [
                self._camera_point(point)
                for point in self._display_edge_points(edge)
            ]
            screen_points = [
                self._screen_point(point)
                for point in camera_points
            ]
            for index in range(1, len(screen_points)):
                distance, fraction = self._point_segment_distance(
                    position,
                    screen_points[index - 1],
                    screen_points[index],
                )
                if distance <= threshold:
                    depth = (
                        camera_points[index - 1][2] * (1.0 - fraction)
                        + camera_points[index][2] * fraction
                    )
                    candidates.append(
                        (distance, -depth, edge.owner_id, edge.edge_index)
                    )
        if not candidates:
            return None
        candidates.sort()
        nearest_distance = candidates[0][0]
        depth_candidates = [
            candidate
            for candidate in candidates
            if candidate[0] <= nearest_distance + 1.5
        ]
        selected = min(depth_candidates, key=lambda candidate: candidate[1])
        return selected[2], selected[3]

    def _pick_face(self, position: QPointF) -> TopologyKey | None:
        if self._selection_filter not in {"all", "face", "normal"}:
            return None
        mesh = self._mesh
        if mesh is None or not mesh.triangle_face_indices:
            return None
        hits: list[tuple[float, str, int]] = []
        positions = mesh.triangle_positions
        for triangle_index, face_index in enumerate(
            mesh.triangle_face_indices
        ):
            offset = triangle_index * 9
            camera_points = [
                self._camera_point(
                    (
                        positions[offset + vertex * 3],
                        positions[offset + vertex * 3 + 1],
                        positions[offset + vertex * 3 + 2],
                    )
                )
                for vertex in range(3)
            ]
            weights = self._triangle_weights(
                position,
                *(self._screen_point(point) for point in camera_points),
            )
            if weights is None:
                continue
            depth = sum(
                weight * point[2]
                for weight, point in zip(weights, camera_points)
            )
            hits.append(
                (depth, mesh.triangle_owner_ids[triangle_index], face_index)
            )
        if not hits:
            return None
        selected = max(hits)
        return selected[1], selected[2]

    def _pick_object(self, position: QPointF) -> str | None:
        face = self._pick_face(position)
        if face is not None:
            return face[0]
        edge = self._pick_edge(position)
        return edge[0] if edge is not None else None

    def _pick_axis(self, position: QPointF) -> TopologyKey | None:
        edge = self._pick_edge(position)
        if edge is None or self._mesh is None:
            return None
        return (
            edge
            if any(
                item.owner_id == edge[0]
                and item.edge_index == edge[1]
                and item.element_kind in {"axis", "centerline"}
                for item in self._mesh.edges
            )
            else None
        )

    @staticmethod
    def _triangle_weights(
        point: QPointF,
        first: QPointF,
        second: QPointF,
        third: QPointF,
    ) -> tuple[float, float, float] | None:
        denominator = (
            (second.y() - third.y()) * (first.x() - third.x())
            + (third.x() - second.x()) * (first.y() - third.y())
        )
        if abs(denominator) <= 1e-12:
            return None
        first_weight = (
            (second.y() - third.y()) * (point.x() - third.x())
            + (third.x() - second.x()) * (point.y() - third.y())
        ) / denominator
        second_weight = (
            (third.y() - first.y()) * (point.x() - third.x())
            + (first.x() - third.x()) * (point.y() - third.y())
        ) / denominator
        third_weight = 1.0 - first_weight - second_weight
        epsilon = -1e-8
        if (
            first_weight < epsilon
            or second_weight < epsilon
            or third_weight < epsilon
        ):
            return None
        return first_weight, second_weight, third_weight

    @staticmethod
    def _point_segment_distance(
        point: QPointF,
        first: QPointF,
        second: QPointF,
    ) -> tuple[float, float]:
        dx = second.x() - first.x()
        dy = second.y() - first.y()
        length_squared = dx * dx + dy * dy
        if length_squared <= 1e-12:
            return hypot(point.x() - first.x(), point.y() - first.y()), 0.0
        fraction = (
            (point.x() - first.x()) * dx
            + (point.y() - first.y()) * dy
        ) / length_squared
        fraction = max(0.0, min(1.0, fraction))
        closest_x = first.x() + fraction * dx
        closest_y = first.y() + fraction * dy
        return hypot(point.x() - closest_x, point.y() - closest_y), fraction

    def _set_hovered_edge(self, edge: TopologyKey | None) -> None:
        if edge == self._hovered_edge:
            return
        self._hovered_edge = edge
        self.hoveredEdgeChanged.emit(*(edge or ("", 0)))
        self.update()

    def _set_selected_edge(self, edge: TopologyKey | None) -> None:
        if edge == self._selected_edge:
            return
        self._selected_edge = edge
        self.selectedEdgeChanged.emit(*(edge or ("", 0)))
        self.update()

    def _set_hovered_face(self, face: TopologyKey | None) -> None:
        if face == self._hovered_face:
            return
        self._hovered_face = face
        self.hoveredFaceChanged.emit(*(face or ("", 0)))
        self.update()

    def _set_selected_face(self, face: TopologyKey | None) -> None:
        if face == self._selected_face:
            return
        self._selected_face = face
        self.selectedFaceChanged.emit(*(face or ("", 0)))
        self.update()

    def _set_hovered_point(self, point: TopologyKey | None) -> None:
        if point == self._hovered_point:
            return
        self._hovered_point = point
        self.hoveredPointChanged.emit(*(point or ("", 0)))
        self.update()

    def _set_selected_point(self, point: TopologyKey | None) -> None:
        if point == self._selected_point:
            return
        self._selected_point = point
        self.selectedPointChanged.emit(*(point or ("", 0)))
        self.update()

    def _set_hovered_plane(self, plane: TopologyKey | None) -> None:
        if plane == self._hovered_plane:
            return
        self._hovered_plane = plane
        self.hoveredPlaneChanged.emit(*(plane or ("", 0)))
        self.update()

    def _set_selected_plane(self, plane: TopologyKey | None) -> None:
        if plane == self._selected_plane:
            return
        self._selected_plane = plane
        self.selectedPlaneChanged.emit(*(plane or ("", 0)))
        self.update()

    def _set_hovered_object(self, owner_id: str | None) -> None:
        if owner_id == self._hovered_object_id:
            return
        self._hovered_object_id = owner_id
        self.hoveredObjectChanged.emit(owner_id or "")
        self.update()

    def _set_selected_object(self, owner_id: str | None) -> None:
        if owner_id == self._selected_object_id:
            return
        self._selected_object_id = owner_id
        self.selectedObjectChanged.emit(owner_id or "")
        self.update()

    def _clear_topology_hover(self) -> None:
        self._set_hovered_edge(None)
        self._set_hovered_face(None)
        self._set_hovered_point(None)
        self._set_hovered_plane(None)

    def _clear_topology_selection(self) -> None:
        self._set_selected_edge(None)
        self._set_selected_face(None)
        self._set_selected_point(None)
        self._set_selected_plane(None)
        self._set_hovered_object(None)
        self._set_selected_object(None)

    def _camera_matrices(self) -> tuple[QMatrix4x4, QMatrix4x4]:
        aspect = max(1e-6, float(self.width()) / max(1.0, float(self.height())))
        half_height = self._scene_radius / max(self.camera.zoom, 1e-6)
        half_width = half_height * aspect
        units_per_pixel = (half_height * 2.0) / max(1.0, float(self.height()))
        model_view = QMatrix4x4()
        model_view.translate(
            self.camera.pan_x * units_per_pixel,
            -self.camera.pan_y * units_per_pixel,
            0.0,
        )
        model_view.rotate(self.camera.pitch_degrees, 1.0, 0.0, 0.0)
        model_view.rotate(self.camera.yaw_degrees, 0.0, 0.0, 1.0)
        model_view.translate(
            -self._scene_center[0],
            -self._scene_center[1],
            -self._scene_center[2],
        )
        projection = QMatrix4x4()
        projection.ortho(
            -half_width,
            half_width,
            -half_height,
            half_height,
            -self._scene_radius * 10.0,
            self._scene_radius * 10.0,
        )
        return model_view, projection * model_view

    def _paint_mesh(self, painter: QPainter) -> None:
        mesh = self._mesh
        if mesh is None or mesh.is_empty or self.width() <= 0 or self.height() <= 0:
            return
        # Antialiasing individual filled triangles creates hairline seams
        # between triangles belonging to the same CAD face.  Surface
        # antialiasing will be handled by multisampled OpenGL buffers later.
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        triangle_records = []
        positions = mesh.triangle_positions
        normals = mesh.triangle_normals
        for offset in range(0, len(positions), 9):
            world_points = [
                (
                    positions[offset + vertex * 3],
                    positions[offset + vertex * 3 + 1],
                    positions[offset + vertex * 3 + 2],
                )
                for vertex in range(3)
            ]
            camera_points = [
                self._camera_point(point)
                for point in world_points
            ]
            polygon = QPolygonF(
                [
                    self._screen_point(point)
                    for point in camera_points
                ]
            )
            normal = self._camera_vector(
                (
                    normals[offset],
                    normals[offset + 1],
                    normals[offset + 2],
                )
            )
            depth = sum(point[2] for point in camera_points) / 3.0
            triangle_records.append((depth, polygon, normal))

        surface_color = self._surface_color
        light = (0.25, -0.35, 0.902)
        painter.setPen(Qt.PenStyle.NoPen)
        for _depth, polygon, normal in sorted(
            triangle_records,
            key=lambda record: record[0],
        ):
            intensity = max(
                0.0,
                normal[0] * light[0]
                + normal[1] * light[1]
                + normal[2] * light[2],
            )
            brightness = 0.42 + 0.58 * intensity
            shaded = QColor(
                min(255, round(surface_color.red() * brightness)),
                min(255, round(surface_color.green() * brightness)),
                min(255, round(surface_color.blue() * brightness)),
            )
            painter.setBrush(QBrush(shaded))
            painter.drawPolygon(polygon)

        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setPen(
            QPen(
                QColor("#16191E"),
                max(1.0, float(self.devicePixelRatioF())),
            )
        )
        for edge in mesh.edges:
            projected = [
                self._screen_point(self._camera_point(point))
                for point in edge.points
            ]
            for index in range(1, len(projected)):
                painter.drawLine(projected[index - 1], projected[index])

    def _camera_point(self, point: Point3) -> Point3:
        relative = tuple(
            point[axis] - self._scene_center[axis]
            for axis in range(3)
        )
        return self._rotate(relative)

    def _camera_vector(self, vector: Point3) -> Point3:
        return self._rotate(vector)

    def _rotate(self, vector: Point3) -> Point3:
        yaw = radians(self.camera.yaw_degrees)
        pitch = radians(self.camera.pitch_degrees)
        yaw_x = cos(yaw) * vector[0] - sin(yaw) * vector[1]
        yaw_y = sin(yaw) * vector[0] + cos(yaw) * vector[1]
        yaw_z = vector[2]
        return (
            yaw_x,
            cos(pitch) * yaw_y - sin(pitch) * yaw_z,
            sin(pitch) * yaw_y + cos(pitch) * yaw_z,
        )

    def _screen_point(self, point: Point3) -> QPointF:
        scale = (
            float(self.height())
            * 0.5
            / self._scene_radius
            * self.camera.zoom
        )
        return QPointF(
            self.width() * 0.5 + self.camera.pan_x + point[0] * scale,
            self.height() * 0.5 + self.camera.pan_y - point[1] * scale,
        )
