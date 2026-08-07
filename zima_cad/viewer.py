from __future__ import annotations

from array import array
from dataclasses import dataclass
from math import (
    acos, atan2, cos, degrees, hypot, pi, radians, sin, sqrt, tan,
)
import traceback
from typing import Any

import numpy as np

from PySide6.QtCore import (
    QEasingCurve,
    QLineF,
    QPoint,
    QPointF,
    QRectF,
    QTimer,
    Qt,
    QVariantAnimation,
    Signal,
)
from PySide6.QtGui import (
    QBrush,
    QColor,
    QFont,
    QFontMetrics,
    QMatrix4x4,
    QMouseEvent,
    QOpenGLExtraFunctions,
    QPainter,
    QPen,
    QPolygonF,
    QSurfaceFormat,
    QTransform,
    QVector3D,
    QWheelEvent,
)
from PySide6.QtOpenGL import (
    QOpenGLBuffer,
    QOpenGLShader,
    QOpenGLShaderProgram,
    QOpenGLVertexArrayObject,
)
from PySide6.QtOpenGLWidgets import QOpenGLWidget
from PySide6.QtWidgets import QApplication, QDialog

from OCC.Core.gp import gp_Pnt

from zima_cad.animation import ANIMATION_DURATION_MS
from zima_cad.sketch_geometry import (
    arc_cardinal_keypoints,
    center_arc_points,
    ellipse_points,
    elliptical_arc_cardinal_keypoints,
    elliptical_arc_points,
    evaluate_corner_radius,
    outward_minor_arc_endpoint,
    polyline_arc_start_context,
    regular_polygon_vertices,
)
from zima_cad.drawing_style import load_drawing_style
from zima_cad.spline_geometry import (
    orient_tangent,
    sample_interpolated_spline,
    sample_tangent_start_arc,
    spline_endpoint_support_tangent,
    stored_spline_tangent,
)
from zima_cad.opengl_platform import OPENGL_CONFIG, platform_shader
from zima_cad.viewer_mesh import (
    Point3,
    SilhouetteEdge,
    ViewerMesh,
    build_silhouette_edges,
    edge_visible_in_display,
    silhouette_segments_from_edges,
)


@dataclass(frozen=True)
class SketchConstraintMarker:
    label: str
    position: QPointF
    owner_id: str
    constraint_index: int
    selectable: bool = True

TopologyKey = tuple[str, int]


def _interpolated_spline_points(
    points: list[tuple[float, float]]
    | tuple[tuple[float, float], ...],
    start_tangent: tuple[float, float] | None = None,
    end_tangent: tuple[float, float] | None = None,
) -> tuple[tuple[float, float], ...]:
    """Sample the same interpolating spline used by the sketch model."""
    return sample_interpolated_spline(points, start_tangent, end_tangent)


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
    plane_normal: Point3 | None = None
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
GL_LINES = 0x0001
GL_LINE_STRIP = 0x0003
GL_MULTISAMPLE = 0x809D
GL_POLYGON_OFFSET_FILL = 0x8037
GL_TRIANGLES = 0x0004

BACKGROUND_VERTEX_SHADER = platform_shader("""
#version 300 es
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
""")

BACKGROUND_FRAGMENT_SHADER = platform_shader("""
#version 300 es
precision highp float;
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
""")

SURFACE_VERTEX_SHADER = platform_shader("""
#version 300 es
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 mvp;
uniform mat4 modelView;
out vec3 cameraNormal;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    cameraNormal = normalize(mat3(modelView) * normal);
}
""")

SURFACE_FRAGMENT_SHADER = platform_shader("""
#version 300 es
precision highp float;
in vec3 cameraNormal;
uniform vec3 surfaceColor;
out vec4 fragmentColor;
void main() {
    vec3 lightDirection = normalize(vec3(0.25, -0.35, 0.902));
    float diffuse = max(dot(normalize(cameraNormal), lightDirection), 0.0);
    float brightness = 0.42 + 0.58 * diffuse;
    fragmentColor = vec4(surfaceColor * brightness, 1.0);
}
""")

EDGE_VERTEX_SHADER = platform_shader("""
#version 300 es
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
}
""")

EDGE_FRAGMENT_SHADER = platform_shader("""
#version 300 es
precision highp float;
uniform vec3 edgeColor;
out vec4 fragmentColor;
void main() {
    fragmentColor = vec4(edgeColor, 1.0);
}
""")


# Standard camera convention shared by direct and animated view changes.
# The front view looks from +Y towards -Y, so +X appears left and +Z up.
STANDARD_VIEW_ORIENTATIONS: dict[str, tuple[float, float]] = {
    "default": (215.264, -45.0),
    "front": (180.0, -90.0),
    "back": (0.0, -90.0),
    "left": (-90.0, -90.0),
    "right": (90.0, -90.0),
    "top": (180.0, 0.0),
    "bottom": (180.0, 180.0),
}


def camera_angles_for_view_direction(normal: Point3) -> tuple[float, float]:
    """Map a world viewing direction onto the camera's negative depth axis."""
    nx, ny, nz = normal
    length = sqrt(nx * nx + ny * ny + nz * nz)
    if length <= 1e-12:
        return (0.0, 0.0)
    nx, ny, nz = nx / length, ny / length, nz / length
    horizontal = hypot(nx, ny)
    yaw = degrees(atan2(nx, ny)) if horizontal > 1e-12 else 0.0
    pitch = degrees(atan2(-horizontal, -nz))
    return yaw, pitch


def _surface_pass_for_display_mode(display_mode: str) -> str:
    if display_mode == "wire":
        return "none"
    if display_mode in {"hidden_edges", "no_hidden"}:
        return "depth"
    return "color"


@dataclass
class CameraState:
    """Viewer-owned camera state, independent from OCCT presentation classes."""

    # Default isometric view follows the drawing convention: +X is shown on
    # the left, +Y on the right and +Z points up.
    yaw_degrees: float = 215.264
    pitch_degrees: float = -45.0
    roll_degrees: float = 0.0
    pan_x: float = 0.0
    pan_y: float = 0.0
    zoom: float = 1.0


def _multiply_rotation_matrices(
    first: tuple[tuple[float, float, float], ...],
    second: tuple[tuple[float, float, float], ...],
) -> tuple[tuple[float, float, float], ...]:
    return tuple(
        tuple(
            sum(first[row][index] * second[index][column]
                for index in range(3))
            for column in range(3)
        )
        for row in range(3)
    )


def _camera_rotation_matrix(
    yaw_degrees: float,
    pitch_degrees: float,
    roll_degrees: float,
) -> tuple[tuple[float, float, float], ...]:
    yaw = radians(yaw_degrees)
    pitch = radians(pitch_degrees)
    roll = radians(roll_degrees)
    yaw_matrix = (
        (cos(yaw), -sin(yaw), 0.0),
        (sin(yaw), cos(yaw), 0.0),
        (0.0, 0.0, 1.0),
    )
    pitch_matrix = (
        (1.0, 0.0, 0.0),
        (0.0, cos(pitch), -sin(pitch)),
        (0.0, sin(pitch), cos(pitch)),
    )
    roll_matrix = (
        (cos(roll), -sin(roll), 0.0),
        (sin(roll), cos(roll), 0.0),
        (0.0, 0.0, 1.0),
    )
    return _multiply_rotation_matrices(
        roll_matrix,
        _multiply_rotation_matrices(pitch_matrix, yaw_matrix),
    )


def preserve_camera_for_scene_bounds(
    camera: CameraState,
    previous_center: Point3,
    previous_radius: float,
    new_center: Point3,
    new_radius: float,
    viewport_height: float,
) -> None:
    """Keep world-to-screen navigation stable across a bounds rebuild."""
    if previous_radius <= 1e-12 or new_radius <= 1e-12:
        return
    previous_scale = (
        max(0.0, float(viewport_height))
        * 0.5
        / previous_radius
        * camera.zoom
    )
    rotation = _camera_rotation_matrix(
        camera.yaw_degrees,
        camera.pitch_degrees,
        camera.roll_degrees,
    )
    center_delta = tuple(
        previous_center[axis] - new_center[axis]
        for axis in range(3)
    )
    rotated_delta = tuple(
        sum(rotation[row][column] * center_delta[column]
            for column in range(3))
        for row in range(3)
    )
    camera.zoom *= new_radius / previous_radius
    camera.pan_x -= rotated_delta[0] * previous_scale
    camera.pan_y += rotated_delta[1] * previous_scale


def _nearest_angle(angle: float, reference: float) -> float:
    return reference + ((angle - reference + 180.0) % 360.0) - 180.0


def _camera_angles_from_matrix(
    matrix: tuple[tuple[float, float, float], ...],
    previous: tuple[float, float, float],
) -> tuple[float, float, float]:
    previous_yaw, previous_pitch, previous_roll = previous
    positive_pitch = degrees(acos(max(-1.0, min(1.0, matrix[2][2]))))
    sine_pitch = sin(radians(positive_pitch))
    if abs(sine_pitch) <= 1.0e-8:
        combined = degrees(atan2(matrix[1][0], matrix[0][0]))
        roll = previous_roll
        if matrix[2][2] >= 0.0:
            yaw = combined - roll
            pitch = 0.0
        else:
            yaw = roll - combined
            pitch = 180.0
        return (
            _nearest_angle(yaw, previous_yaw),
            _nearest_angle(pitch, previous_pitch),
            _nearest_angle(roll, previous_roll),
        )

    positive = (
        degrees(atan2(matrix[2][0], matrix[2][1])),
        positive_pitch,
        degrees(atan2(matrix[0][2], -matrix[1][2])),
    )
    negative = (
        degrees(atan2(-matrix[2][0], -matrix[2][1])),
        -positive_pitch,
        degrees(atan2(-matrix[0][2], matrix[1][2])),
    )
    candidates = [
        tuple(
            _nearest_angle(value, reference)
            for value, reference in zip(candidate, previous)
        )
        for candidate in (positive, negative)
    ]
    return min(
        candidates,
        key=lambda candidate: sum(
            (candidate[index] - previous[index]) ** 2
            for index in range(3)
        ),
    )


def orbit_camera_state(
    camera: CameraState,
    horizontal_degrees: float,
    vertical_degrees: float,
) -> None:
    """Rotate around the current screen axes without Euler-axis reversal."""

    horizontal = radians(horizontal_degrees)
    vertical = radians(vertical_degrees)
    screen_y_rotation = (
        (cos(horizontal), 0.0, sin(horizontal)),
        (0.0, 1.0, 0.0),
        (-sin(horizontal), 0.0, cos(horizontal)),
    )
    screen_x_rotation = (
        (1.0, 0.0, 0.0),
        (0.0, cos(vertical), -sin(vertical)),
        (0.0, sin(vertical), cos(vertical)),
    )
    current = _camera_rotation_matrix(
        camera.yaw_degrees,
        camera.pitch_degrees,
        camera.roll_degrees,
    )
    rotated = _multiply_rotation_matrices(
        screen_x_rotation,
        _multiply_rotation_matrices(screen_y_rotation, current),
    )
    (
        camera.yaw_degrees,
        camera.pitch_degrees,
        camera.roll_degrees,
    ) = _camera_angles_from_matrix(
        rotated,
        (
            camera.yaw_degrees,
            camera.pitch_degrees,
            camera.roll_degrees,
        ),
    )


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
    sketchPlacementConfirmed = Signal(float, float, str, str)
    sketchRectangleAxisModeRequested = Signal()
    sketchRectangleAxisSelected = Signal(str)
    sketchReferenceHovered = Signal(str)
    sketchCancelCurrentRequested = Signal()
    sketchAlternateCurrentRequested = Signal()
    sketchConfirmCurrentRequested = Signal()
    sketchFinishCurrentRequested = Signal()
    sketchViewClicked = Signal()
    sketchEntitySelected = Signal(str)
    sketchTextEditRequested = Signal(str)
    sketchEntityAdditiveSelected = Signal(str)
    sketchEntitiesSelected = Signal(object, bool)
    sketchCornerRadiusSelected = Signal(str, str, str)
    sketchCornerRadiusDragged = Signal(str, float, float, bool)
    sketchDimensionDragged = Signal(str, float, float, bool)
    sketchDimensionSelected = Signal(str)
    sketchDimensionEditRequested = Signal(str)
    sketchDimensionHovered = Signal(str)
    sketchEntityHovered = Signal(str)
    sketchCursorMoved = Signal(float, float)
    sketchConstraintReferenceSelected = Signal(str)
    sketchConstraintSelected = Signal(str, int)
    sketchExternalReferenceSelected = Signal(str)
    sketchDeleteRequested = Signal()
    sketchArcDirectionSelected = Signal(bool)
    sketchTrimPreviewRequested = Signal(object)
    sketchTrimGestureRequested = Signal(object)
    rotation_degrees_per_pixel = 0.18

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        surface_format = QSurfaceFormat()
        if OPENGL_CONFIG.renderable_type == "gles":
            surface_format.setRenderableType(
                QSurfaceFormat.RenderableType.OpenGLES
            )
            surface_format.setVersion(*OPENGL_CONFIG.version)
            surface_format.setProfile(
                QSurfaceFormat.OpenGLContextProfile.NoProfile
            )
        else:
            surface_format.setRenderableType(
                QSurfaceFormat.RenderableType.OpenGL
            )
            surface_format.setVersion(*OPENGL_CONFIG.version)
            surface_format.setProfile(
                QSurfaceFormat.OpenGLContextProfile.CoreProfile
            )
        surface_format.setDepthBufferSize(24)
        # Avoid a multisampled native buffer while navigating large CAD
        # scenes. Edge smoothing is handled independently.
        surface_format.setSamples(0)
        # KWin already paces the composited widget.  A second GL swap wait can
        # throttle QOpenGLWidget to 30-45 FPS through XWayland even when the
        # actual render takes only a fraction of a millisecond.
        surface_format.setSwapInterval(0)
        self.setFormat(surface_format)
        self.setUpdateBehavior(QOpenGLWidget.UpdateBehavior.PartialUpdate)
        self.camera = CameraState()
        self._background_top = QColor("#3B4654")
        self._background_bottom = QColor("#171B21")
        self._surface_color = QColor("#B9C2CC")
        self._surface_colors_by_owner_id: dict[str, QColor] = {}
        self._edge_color_override: QColor | None = None
        self._last_mouse_position: QPoint | None = None
        self._last_click_position: QPoint | None = None
        self._middle_press_position: QPoint | None = None
        self._middle_dragged = False
        self._middle_chorded = False
        self._middle_double_clicked = False
        self._navigation_active = False
        self._navigation_repaint_pending = False
        self._mesh: ViewerMesh | None = None
        self._face_pick_cache_key: tuple[Any, ...] | None = None
        self._face_pick_cache: tuple[tuple[Any, ...], ...] = ()
        self._face_pick_arrays: dict[str, Any] = {}
        self._scene_center: Point3 = (0.0, 0.0, 0.0)
        self._scene_radius = 1.0
        self._gl: QOpenGLExtraFunctions | None = None
        self._background_program: QOpenGLShaderProgram | None = None
        self._surface_program: QOpenGLShaderProgram | None = None
        self._edge_program: QOpenGLShaderProgram | None = None
        self._surface_buffer: QOpenGLBuffer | None = None
        self._edge_buffer: QOpenGLBuffer | None = None
        self._silhouette_buffer: QOpenGLBuffer | None = None
        self._surface_vao: QOpenGLVertexArrayObject | None = None
        self._edge_vao: QOpenGLVertexArrayObject | None = None
        self._background_vao: QOpenGLVertexArrayObject | None = None
        self._surface_vertex_count = 0
        self._uploaded_surface_key: tuple[int, int, int, int] | None = None
        self._surface_data_cache_key: tuple[int, int, int, int] | None = None
        self._surface_data_cache = b""
        self._surface_face_ranges_cache: tuple[
            tuple[str, int, int, int], ...
        ] = ()
        self._surface_owner_ranges_cache: tuple[
            tuple[str, int, int], ...
        ] = ()
        # Keep the actual edge collection alive while its serialized GPU
        # data is cached.  Caching only id(edges) allowed CPython to recycle
        # the integer after a regenerated mesh was released, making a new
        # body's wireframe accidentally reuse the previous edge buffer.
        self._base_edge_cache_source: object | None = None
        self._base_edge_cache_data = b""
        self._base_edge_cache_ranges: tuple[tuple[int, int], ...] = ()
        self._base_edge_cache_vertex_count = 0
        self._base_edge_mesh: ViewerMesh | None = None
        self._face_ranges: tuple[tuple[str, int, int, int], ...] = ()
        self._owner_ranges: tuple[tuple[str, int, int], ...] = ()
        self._edge_ranges: tuple[tuple[int, int], ...] = ()
        self._silhouette_edges: tuple[SilhouetteEdge, ...] = ()
        self._silhouette_cache: list[
            tuple[ViewerMesh, tuple[SilhouetteEdge, ...]]
        ] = []
        self._buffers_dirty = False
        self._gpu_ready = False
        self._hovered_edge: TopologyKey | None = None
        self._selected_edge: TopologyKey | None = None
        self._edge_treatment_selection_edges: frozenset[TopologyKey] = frozenset()
        self._hovered_face: TopologyKey | None = None
        self._selected_face: TopologyKey | None = None
        self._feature_hover_edges: frozenset[TopologyKey] = frozenset()
        self._feature_selected_edges: frozenset[TopologyKey] = frozenset()
        self._feature_preview_owner_ids: frozenset[str] = frozenset()
        self._hovered_point: TopologyKey | None = None
        self._selected_point: TopologyKey | None = None
        self._hovered_plane: TopologyKey | None = None
        self._selected_plane: TopologyKey | None = None
        self._assembly_reference_faces: frozenset[TopologyKey] = frozenset()
        self._assembly_reference_edges: frozenset[TopologyKey] = frozenset()
        self._assembly_reference_planes: frozenset[TopologyKey] = frozenset()
        self._hovered_object_id: str | None = None
        self._selected_object_id: str | None = None
        self._interaction_mode = "object"
        self._selection_filter = "all"
        self._topology_owner_filter: frozenset[str] | None = None
        self._display_mode = "shaded_with_edges"
        self._selection_enabled = True
        self._large_mesh_topology_enabled = False
        self._outline_face_highlights = False
        self._object_overlay_mesh: ViewerMesh | None = None
        self._object_overlay_color = QColor.fromRgbF(1.0, 0.48, 0.0)
        self._object_overlay_persistent = False
        self._object_overlay_locks_interaction = False
        self._object_overlay_anchor: Point3 | None = None
        self._selected_reference_owner_id: str | None = None
        self._constraint_reference_owner_ids: frozenset[str] = frozenset()
        self._excluded_topology_owner_ids: frozenset[str] = frozenset()
        self._excluded_object_owner_ids: frozenset[str] = frozenset()
        self._constraint_reference_faces: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_edges: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_points: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_planes: frozenset[TopologyKey] = frozenset()
        self._constraint_reference_positions: tuple[Point3, ...] = ()
        self._selected_container_origin_id: str | None = None
        self._selected_container_content_ids: frozenset[str] = frozenset()
        self._cycled_topology_candidate: tuple[str, str, int] | None = None
        self._selection_preview_pending = False
        self._pending_model_hover_position: QPointF | None = None
        self._last_model_hover_position: QPointF | None = None
        self._model_hover_timer = QTimer(self)
        self._model_hover_timer.setSingleShot(True)
        self._model_hover_timer.setInterval(50)
        self._model_hover_timer.timeout.connect(
            self._apply_pending_model_hover
        )
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
        self._sketch_preview_is_keypoint = False
        self._sketch_placement_candidates: tuple[
            tuple[tuple[float, float], str | None, str | None], ...
        ] = ()
        self._sketch_placement_candidate_index = 0
        self._sketch_placement_candidate_cursor: QPointF | None = None
        self._selected_sketch_constraint: tuple[str, int] | None = None
        self._selected_sketch_reference_id: str | None = None
        self._hovered_sketch_constraint: tuple[str, int] | None = None
        self._sketch_constraint_hit_regions: list[tuple[QRectF, str, int]] = []
        self._sketch_selection_mode = False
        self._sketch_constraint_selection_mode = False
        self._sketch_reference_selection_mode = False
        self._sketch_reference_snapping = False
        self._sketch_arc_clockwise: bool | None = None
        self._sketch_arc_last_angle: float | None = None
        self._sketch_arc_accumulated_sweep = 0.0
        self._sketch_polyline_arc_reverse = False
        self._sketch_polygon_sides = 4
        self._sketch_rectangle_axis_mode = False
        self._sketch_rectangle_axis_id: str | None = None
        self._selected_sketch_entity_id: str | None = None
        self._selected_sketch_entity_ids: frozenset[str] = frozenset()
        self._selected_sketch_corner_radius: tuple[str, str, str] | None = None
        self._hovered_sketch_corner_radius: tuple[str, str, str] | None = None
        self._sketch_box_start: QPointF | None = None
        self._sketch_box_additive = False
        self._sketch_box_end: QPointF | None = None
        self._sketch_corner_drag_vertex_id: str | None = None
        self._sketch_corner_drag_moved = False
        self._sketch_dimension_drag_key: str | None = None
        self._sketch_dimension_drag_moved = False
        self._sketch_trim_path: list[tuple[float, float]] = []
        self._sketch_trim_preview_paths: tuple[
            tuple[tuple[float, float], ...], ...
        ] = ()
        self._sketch_trim_dragging = False
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

    def set_surface_colors(self, colors: dict[str, str]) -> None:
        self._surface_colors_by_owner_id = {
            owner_id: color
            for owner_id, value in colors.items()
            if (color := QColor(value)).isValid()
        }
        self.update()

    def set_edge_color_override(self, color: QColor | str | None) -> None:
        selected = QColor(color) if color is not None else QColor()
        self._edge_color_override = selected if selected.isValid() else None
        self.update()

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
        selected_constraint: tuple[str, int] | None = None,
        selected_reference_id: str | None = None,
        external_references: tuple[dict[str, Any], ...]
        | list[dict[str, Any]] = (),
        snap_to_external_references: bool = False,
        sketch_tool: str | None = None,
        polygon_sides: int = 4,
        rectangle_axis_mode: bool = False,
        rectangle_axis_id: str | None = None,
    ) -> None:
        self._sketch_frame = frame
        self._sketch_entities = tuple(entities)
        previous_pending = self._sketch_pending_points
        self._sketch_pending_points = tuple(pending_points)
        if (
            sketch_tool not in ("arc", "elliptical_arc")
            or (
                sketch_tool == "arc"
                and len(self._sketch_pending_points) != 2
            )
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
        self._selected_sketch_constraint = selected_constraint
        self._selected_sketch_reference_id = selected_reference_id
        self._sketch_external_references = tuple(external_references)
        self._sketch_reference_snapping = snap_to_external_references
        self._sketch_rectangle_axis_mode = bool(rectangle_axis_mode)
        self._sketch_rectangle_axis_id = rectangle_axis_id
        self._sketch_tool = sketch_tool
        self._sketch_polygon_sides = (
            polygon_sides if polygon_sides in (4, 6, 8) else 4
        )
        if sketch_tool != "trim":
            self._sketch_trim_path.clear()
            self._sketch_trim_preview_paths = ()
            self._sketch_trim_dragging = False
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
        hovered_reference_components = tuple(
            component
            for component in (
                self._hovered_sketch_external_reference_id or ""
            ).split("||")
            if component
        )
        intrinsic_prefixes = (
            "sketch_axis:",
            "sketch_geometry:",
            "sketch_circle:",
            "sketch_arc:",
            "sketch_ellipse:",
            "sketch_elliptical_arc:",
            "sketch_spline:",
            "sketch_radius:",
        )
        if hovered_reference_components and not all(
            component == "sketch_origin"
            or component.startswith(intrinsic_prefixes)
            or any(
                component == reference_id
                or component.startswith(reference_id + "::")
                for reference_id in reference_ids
            )
            for component in hovered_reference_components
        ):
            self._hovered_sketch_external_reference_id = None
        if not selection_mode or frame is None:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
        if selection_mode or frame is None or not pending_points:
            self._sketch_preview_position = None
            self._sketch_preview_constraint = None
            self._sketch_preview_is_keypoint = False
        self.update()

    def set_sketch_trim_preview(
        self,
        paths: object,
    ) -> None:
        normalized: list[tuple[tuple[float, float], ...]] = []
        if isinstance(paths, (list, tuple)):
            for path in paths:
                if not isinstance(path, (list, tuple)):
                    continue
                points = tuple(
                    (float(point[0]), float(point[1]))
                    for point in path
                    if isinstance(point, (list, tuple)) and len(point) >= 2
                )
                if len(points) >= 2:
                    normalized.append(points)
        self._sketch_trim_preview_paths = tuple(normalized)
        self.update()

    def set_sketch_reference_selection_mode(self, enabled: bool) -> None:
        self._sketch_reference_selection_mode = enabled
        if enabled:
            self._preview_sketch_entity_id = None
            self._sketch_cycle_ids = ()
            self._sketch_cycle_index = -1
        self.update()

    @staticmethod
    def _external_point_marker_visible(
        reference: dict[str, Any],
        hovered_reference_id: str | None,
        _reference_mode_active: bool,
    ) -> bool:
        del reference, hovered_reference_id
        return True

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

    def set_camera_state(self, camera: CameraState) -> None:
        """Restore an independent camera snapshot owned by a document tab."""
        self._stop_camera_animation()
        self.camera = CameraState(
            yaw_degrees=camera.yaw_degrees,
            pitch_degrees=camera.pitch_degrees,
            roll_degrees=camera.roll_degrees,
            pan_x=camera.pan_x,
            pan_y=camera.pan_y,
            zoom=camera.zoom,
        )
        self._buffers_dirty = True
        self.navigationChanged.emit(self.camera)
        self.update()

    def set_standard_view(self, view_name: str) -> None:
        if view_name not in STANDARD_VIEW_ORIENTATIONS:
            raise ValueError(f"Unknown standard view: {view_name}")
        yaw, pitch = STANDARD_VIEW_ORIENTATIONS[view_name]
        self.camera.yaw_degrees = yaw
        self.camera.pitch_degrees = pitch
        self.camera.roll_degrees = 0.0
        self.camera.pan_x = 0.0
        self.camera.pan_y = 0.0
        self.navigationChanged.emit(self.camera)
        self.update()

    def animate_standard_view(
        self,
        view_name: str,
        *,
        duration_ms: int = ANIMATION_DURATION_MS,
        fit: bool = False,
    ) -> None:
        if view_name not in STANDARD_VIEW_ORIENTATIONS:
            raise ValueError(f"Unknown standard view: {view_name}")
        target_yaw, target_pitch = STANDARD_VIEW_ORIENTATIONS[view_name]

        self._stop_camera_animation()
        start_yaw = self.camera.yaw_degrees
        yaw_delta = ((target_yaw - start_yaw + 180.0) % 360.0) - 180.0
        start_pitch = self.camera.pitch_degrees
        start_roll = self.camera.roll_degrees
        start_pan_x = self.camera.pan_x
        start_pan_y = self.camera.pan_y
        start_zoom = self.camera.zoom
        target_zoom = 1.0 if fit else start_zoom
        animation = QVariantAnimation(self)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setDuration(max(1, int(duration_ms)))
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)

        def apply_progress(raw_progress) -> None:
            progress = float(raw_progress)
            self.camera.yaw_degrees = start_yaw + yaw_delta * progress
            self.camera.pitch_degrees = (
                start_pitch + (target_pitch - start_pitch) * progress
            )
            self.camera.roll_degrees = start_roll * (1.0 - progress)
            self.camera.pan_x = start_pan_x * (1.0 - progress)
            self.camera.pan_y = start_pan_y * (1.0 - progress)
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

    def animate_camera_state(
        self,
        target: CameraState,
        *,
        duration_ms: int = ANIMATION_DURATION_MS,
    ) -> None:
        """Animate all persisted camera properties to a named view."""
        self._stop_camera_animation()
        start = CameraState(
            yaw_degrees=self.camera.yaw_degrees,
            pitch_degrees=self.camera.pitch_degrees,
            roll_degrees=self.camera.roll_degrees,
            pan_x=self.camera.pan_x,
            pan_y=self.camera.pan_y,
            zoom=self.camera.zoom,
        )
        yaw_delta = ((target.yaw_degrees - start.yaw_degrees + 180.0) % 360.0) - 180.0
        roll_delta = ((target.roll_degrees - start.roll_degrees + 180.0) % 360.0) - 180.0
        animation = QVariantAnimation(self)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setDuration(max(1, int(duration_ms)))
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)

        def apply_progress(raw_progress) -> None:
            progress = float(raw_progress)
            self.camera.yaw_degrees = start.yaw_degrees + yaw_delta * progress
            self.camera.pitch_degrees = start.pitch_degrees + (target.pitch_degrees - start.pitch_degrees) * progress
            self.camera.roll_degrees = start.roll_degrees + roll_delta * progress
            self.camera.pan_x = start.pan_x + (target.pan_x - start.pan_x) * progress
            self.camera.pan_y = start.pan_y + (target.pan_y - start.pan_y) * progress
            self.camera.zoom = start.zoom + (target.zoom - start.zoom) * progress
            self.navigationChanged.emit(self.camera)
            self.update()

        def finish_animation() -> None:
            if self._camera_animation is animation:
                self._camera_animation = None

        animation.valueChanged.connect(apply_progress)
        animation.finished.connect(finish_animation)
        self._camera_animation = animation
        animation.start()

    def set_view_normal(self, normal: Point3) -> None:
        nx, ny, nz = normal
        length = sqrt(nx * nx + ny * ny + nz * nz)
        if length <= 1e-12:
            return
        self.camera.yaw_degrees, self.camera.pitch_degrees = (
            camera_angles_for_view_direction(normal)
        )
        self.camera.roll_degrees = 0.0
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
        roll_degrees: float = 0.0,
        duration_ms: int = ANIMATION_DURATION_MS,
    ) -> None:
        nx, ny, nz = normal
        length = sqrt(nx * nx + ny * ny + nz * nz)
        if length <= 1e-12:
            return
        target_yaw, target_pitch = camera_angles_for_view_direction(normal)
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
        roll = radians(roll_degrees)
        rolled_x = cos(roll) * rotated_x - sin(roll) * rotated_y
        rolled_y = sin(roll) * rotated_x + cos(roll) * rotated_y
        scale = (
            float(self.height())
            * 0.5
            / max(self._scene_radius, 1e-9)
            * target_zoom
        )
        target_pan_x = -rolled_x * scale
        target_pan_y = rolled_y * scale

        self._stop_camera_animation()
        start_yaw = self.camera.yaw_degrees
        yaw_delta = (
            (target_yaw - start_yaw + 180.0) % 360.0
        ) - 180.0
        start_pitch = self.camera.pitch_degrees
        start_roll = self.camera.roll_degrees
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
            self.camera.roll_degrees = (
                start_roll + (roll_degrees - start_roll) * progress
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
        if display_mode not in {
            "wire", "hidden_edges", "no_hidden", "shaded_with_edges", "shaded",
        }:
            raise ValueError(f"Unknown Viewer display mode: {display_mode}")
        if display_mode == self._display_mode:
            return
        self._display_mode = display_mode
        self.displayModeChanged.emit(display_mode)
        self.update()

    @property
    def display_mode(self) -> str:
        return self._display_mode

    @property
    def scene_radius(self) -> float:
        return self._scene_radius

    @property
    def mesh(self) -> ViewerMesh | None:
        return self._mesh

    def world_to_screen(self, point: Point3) -> QPointF:
        return self._screen_point(self._camera_point(point))

    def edge_at(self, position: QPointF) -> TopologyKey | None:
        return self._pick_edge(position)

    def face_at(self, position: QPointF) -> TopologyKey | None:
        return self._pick_face(position)

    def face_at_mesh(
        self,
        mesh: ViewerMesh,
        position: QPointF,
    ) -> TopologyKey | None:
        """Pick a face from a non-displayed mesh using the current camera.

        Source/history meshes are kept out of the displayed scene so they do
        not become selectable result-body owners.  Reference picking still
        needs to resolve the face the user pointed at on one of those meshes.
        """
        if mesh is None or not mesh.triangle_face_indices:
            return None
        positions = mesh.triangle_positions
        hits: list[tuple[float, str, int]] = []
        for triangle_index, face_index in enumerate(mesh.triangle_face_indices):
            offset = triangle_index * 9
            camera_points = tuple(
                self._camera_point((
                    positions[offset + vertex * 3],
                    positions[offset + vertex * 3 + 1],
                    positions[offset + vertex * 3 + 2],
                ))
                for vertex in range(3)
            )
            screen_points = tuple(
                self._screen_point(point) for point in camera_points
            )
            if not (
                min(point.x() for point in screen_points)
                <= position.x()
                <= max(point.x() for point in screen_points)
                and min(point.y() for point in screen_points)
                <= position.y()
                <= max(point.y() for point in screen_points)
            ):
                continue
            weights = self._triangle_weights(position, *screen_points)
            if weights is None:
                continue
            depth = sum(
                weight * point[2]
                for weight, point in zip(weights, camera_points)
            )
            owner_id = (
                mesh.triangle_owner_ids[triangle_index]
                if triangle_index < len(mesh.triangle_owner_ids)
                else ""
            )
            hits.append((depth, owner_id, face_index))
        if not hits:
            return None
        selected = max(hits)
        return selected[1], selected[2]

    def edge_at_mesh(
        self,
        mesh: ViewerMesh,
        position: QPointF,
    ) -> TopologyKey | None:
        """Pick an edge from a non-displayed history mesh."""
        if mesh is None or mesh.is_empty:
            return None
        candidates: list[tuple[float, float, str, int]] = []
        threshold = 8.0 * float(self.devicePixelRatioF())
        for edge in mesh.edges:
            camera_points = [
                self._camera_point(point)
                for point in self._display_edge_points(edge)
            ]
            screen_points = [self._screen_point(point) for point in camera_points]
            for index in range(1, len(screen_points)):
                distance, fraction = self._point_segment_distance(
                    position,
                    screen_points[index - 1],
                    screen_points[index],
                )
                if distance > threshold:
                    continue
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

    def point_at_mesh(
        self,
        mesh: ViewerMesh,
        position: QPointF,
    ) -> TopologyKey | None:
        """Pick a topological vertex from a non-displayed history mesh."""
        if mesh is None or mesh.is_empty:
            return None
        hits: list[tuple[float, float, str, int]] = []
        threshold = 9.0 * float(self.devicePixelRatioF())
        for marker in mesh.points:
            if marker.element_kind != "vertex":
                continue
            camera_point = self._camera_point(marker.position)
            screen = self._screen_point(camera_point)
            distance = hypot(
                position.x() - screen.x(),
                position.y() - screen.y(),
            )
            if distance <= threshold:
                hits.append(
                    (
                        distance,
                        -camera_point[2],
                        marker.owner_id,
                        marker.point_index,
                    )
                )
        if not hits:
            return None
        selected = min(hits)
        return selected[2], selected[3]

    def point_at(self, position: QPointF) -> TopologyKey | None:
        return self._pick_point(position)

    def set_mesh(
        self,
        mesh: ViewerMesh | None,
        *,
        fit: bool = True,
        base_edge_mesh: ViewerMesh | None = None,
    ) -> None:
        previous_mesh = self._mesh
        previous_base_edge_mesh = self._base_edge_mesh
        previous_center = self._scene_center
        previous_radius = self._scene_radius
        previous_zoom = self.camera.zoom
        self._mesh = mesh
        self._base_edge_mesh = base_edge_mesh
        same_surface_buffers = (
            previous_mesh is not None
            and mesh is not None
            and previous_mesh.triangle_positions is mesh.triangle_positions
            and previous_mesh.triangle_normals is mesh.triangle_normals
            and previous_mesh.triangle_face_indices is mesh.triangle_face_indices
            and previous_mesh.triangle_owner_ids is mesh.triangle_owner_ids
        )
        cached_silhouettes = (
            next(
                (
                    silhouettes
                    for cached_mesh, silhouettes in self._silhouette_cache
                    if cached_mesh == mesh
                ),
                None,
            )
            if mesh is not None and mesh.triangle_count <= 100_000
            else None
        )
        if mesh is None:
            self._silhouette_edges = ()
        elif same_surface_buffers:
            # Opening a properties/reference dialog only adds lightweight
            # datum overlays. Rebuilding silhouettes for the unchanged STEP
            # surface made the command button itself appear frozen.
            pass
        elif mesh.triangle_count > 100_000:
            # Large imported STEP models already carry their exact CAD edge
            # polylines.  Building tessellation silhouettes synchronously for
            # hundreds of thousands of triangles delays the first frame by
            # minutes and leaves the viewport blank in the meantime.
            self._silhouette_edges = ()
        elif cached_silhouettes is not None:
            self._silhouette_edges = cached_silhouettes
        else:
            self._silhouette_edges = build_silhouette_edges(mesh)
            self._silhouette_cache.insert(
                0,
                (mesh, self._silhouette_edges),
            )
            del self._silhouette_cache[4:]
        self._set_hovered_edge(None)
        self._set_selected_edge(None)
        self._set_hovered_face(None)
        self._set_selected_face(None)
        self._set_hovered_point(None)
        self._set_selected_point(None)
        self._set_hovered_plane(None)
        self._set_selected_plane(None)
        same_base_edges = (
            previous_base_edge_mesh is not None
            and base_edge_mesh is not None
            and previous_base_edge_mesh.edges is base_edge_mesh.edges
        )

        def has_gpu_overlay_edges(
            candidate: ViewerMesh | None,
            base: ViewerMesh | None,
        ) -> bool:
            if candidate is None:
                return False
            base_count = len(base.edges) if base is not None else 0
            return any(
                not edge.screen_constant
                and edge.element_kind != "centerline"
                for edge in candidate.edges[base_count:]
            )

        # Origin axes and datum planes are painted as screen-constant Qt
        # overlays.  Toggling only those layers must not defer a complete edge
        # buffer upload until the next window activation/repaint.
        overlay_only_change = (
            same_surface_buffers
            and same_base_edges
            and not has_gpu_overlay_edges(
                previous_mesh, previous_base_edge_mesh
            )
            and not has_gpu_overlay_edges(mesh, base_edge_mesh)
        )
        if not overlay_only_change:
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
            if (
                previous_mesh is not None
                and not previous_mesh.is_empty
                and mesh is not None
                and not mesh.is_empty
                and previous_radius > 1e-12
            ):
                # Scene bounds are renderer bookkeeping, not navigation.
                # Compensate their changed center and radius so a live model
                # rebuild keeps every unchanged world point on the same
                # screen pixel at the same scale.
                self.camera.zoom = previous_zoom
                preserve_camera_for_scene_bounds(
                    self.camera,
                    previous_center,
                    previous_radius,
                    self._scene_center,
                    self._scene_radius,
                    float(self.height()),
                )
                self.navigationChanged.emit(self.camera)
            self.update()

    def clear_scene(self) -> None:
        self.set_mesh(None)

    def set_object_overlay(
        self,
        mesh: ViewerMesh | None,
        *,
        selected: bool = False,
        anchor: Point3 | None = None,
        locks_interaction: bool | None = None,
    ) -> None:
        self._object_overlay_mesh = mesh
        self._object_overlay_anchor = anchor
        self._object_overlay_persistent = selected
        self._object_overlay_locks_interaction = (
            selected if locks_interaction is None else locks_interaction
        ) and mesh is not None
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
        faces: set[TopologyKey],
        edges: set[TopologyKey],
        points: set[TopologyKey],
        planes: set[TopologyKey],
        positions: set[Point3],
    ) -> None:
        self._constraint_reference_owner_ids = frozenset(owner_ids)
        self._constraint_reference_faces = frozenset(faces)
        self._constraint_reference_edges = frozenset(edges)
        self._constraint_reference_points = frozenset(points)
        self._constraint_reference_planes = frozenset(planes)
        self._constraint_reference_positions = tuple(positions)
        self.update()

    def set_assembly_reference_highlights(
        self,
        *,
        faces: set[TopologyKey],
        planes: set[TopologyKey],
        edges: set[TopologyKey] | None = None,
    ) -> None:
        self._assembly_reference_faces = frozenset(faces)
        self._assembly_reference_edges = frozenset(edges or ())
        self._assembly_reference_planes = frozenset(planes)
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
                for base_key, tail_key in (
                    ("arrow_base", "arrow_tail"),
                    ("opposite_arrow_base", "opposite_arrow_tail"),
                ):
                    if base_key in geometry and tail_key in geometry:
                        segments.append((
                            geometry[base_key], geometry[tail_key]
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
                for base_key, tail_key in (
                    ("first_arrow_base", "first_tail"),
                    ("second_arrow_base", "second_tail"),
                ):
                    if base_key in geometry and tail_key in geometry:
                        segments.append((
                            geometry[base_key], geometry[tail_key]
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
        value_position = geometry.get("value_position")
        return value_position if isinstance(value_position, QPointF) else None

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
            "all", "face", "edge", "point", "axis", "plane", "normal",
            "surface",
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

    def set_large_mesh_topology_enabled(self, enabled: bool) -> None:
        """Allow real face picking when a large-scene reference tool needs it."""
        self._large_mesh_topology_enabled = bool(enabled)

    def set_topology_owner_filter(self, owner_ids: set[str] | None) -> None:
        self._topology_owner_filter = (
            None if owner_ids is None else frozenset(owner_ids)
        )
        self._clear_topology_hover()

    def set_excluded_topology_owners(self, owner_ids: set[str]) -> None:
        self._excluded_topology_owner_ids = frozenset(owner_ids)
        self._clear_topology_hover()

    def set_excluded_object_owners(self, owner_ids: set[str]) -> None:
        """Disable viewport object picks while retaining tree highlights."""
        excluded = frozenset(owner_ids)
        if excluded == self._excluded_object_owner_ids:
            return
        self._excluded_object_owner_ids = excluded
        if self._hovered_object_id in excluded:
            self._set_hovered_object(None)
        if self._selected_object_id in excluded:
            self._set_selected_object(None)
        self.update()

    def _topology_owner_is_selectable(self, owner_id: str) -> bool:
        return (
            owner_id not in self._excluded_topology_owner_ids
            and (
                self._topology_owner_filter is None
                or owner_id in self._topology_owner_filter
            )
        )

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
        context = self.context()
        if context is None:
            raise RuntimeError("OpenGL ES context is unavailable")
        self._gl = context.extraFunctions()
        self._gl.initializeOpenGLFunctions()
        def gl_text(name: int) -> str:
            value = self._gl.glGetString(name)
            if value is None:
                return "unknown"
            if isinstance(value, bytes):
                return value.decode("utf-8", errors="replace")
            return str(value)
        print(
            "ZIMA Viewer OpenGL: "
            f"vendor={gl_text(0x1F00)}; "
            f"renderer={gl_text(0x1F01)}; "
            f"version={gl_text(0x1F02)}",
            flush=True,
        )
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
        self._silhouette_buffer = QOpenGLBuffer(
            QOpenGLBuffer.Type.VertexBuffer
        )
        self._surface_vao = QOpenGLVertexArrayObject()
        self._edge_vao = QOpenGLVertexArrayObject()
        self._background_vao = QOpenGLVertexArrayObject()
        if (
            not self._surface_buffer.create()
            or not self._edge_buffer.create()
            or not self._silhouette_buffer.create()
        ):
            raise RuntimeError("Unable to create Viewer OpenGL buffers")
        if (
            not self._surface_vao.create()
            or not self._edge_vao.create()
            or not self._background_vao.create()
        ):
            raise RuntimeError("Unable to create Viewer OpenGL vertex arrays")
        self._gpu_ready = True
        # A newly created context owns empty buffers even when their source
        # mesh is unchanged. The retained CPU byte cache makes this upload
        # cheap without incorrectly treating the new buffer as populated.
        self._uploaded_surface_key = None
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
        if self.format().samples() > 1:
            gl.glEnable(GL_MULTISAMPLE)
        model_view, mvp = self._camera_matrices()
        surface_pass = _surface_pass_for_display_mode(self._display_mode)
        if surface_pass == "depth":
            # Hidden-line modes need the solid surfaces in the depth buffer.
            # Suppress their colour output and retain only the depth values.
            gl.glColorMask(False, False, False, False)
            try:
                self._draw_surfaces(model_view, mvp)
            finally:
                gl.glColorMask(True, True, True, True)
        elif surface_pass == "color":
            self._draw_surfaces(model_view, mvp)
        if (
            (mesh.edges or self._silhouette_edges)
            and self._display_mode != "shaded"
        ) or any((
            self._hovered_edge,
            self._selected_edge,
            self._hovered_object_id,
            self._selected_object_id,
            self._selected_reference_owner_id,
            self._constraint_reference_edges,
            self._assembly_reference_edges,
        )):
            self._draw_edges(
                mvp,
                draw_base_edges=self._display_mode != "shaded",
            )

    def paintEvent(self, event) -> None:
        super().paintEvent(event)
        # Repeating QPainter overlays for every mouse-move frame forces a GPU
        # synchronization. They are static during navigation and are painted
        # again immediately after it ends.
        if self._navigation_active:
            self._paint_screen_constant_edges()
            # Feature-boundary selection is persistent model state, not a
            # disposable hover decoration. Reproject it for every navigation
            # frame so a selected fillet stays blue while the camera rotates.
            self._paint_reference_highlights()
            # Dimension geometry is spatial context and must follow the
            # camera continuously in Part, Assembly and Sketch alike.  The
            # editable text widgets are positioned separately; omitting the
            # lines here left only a detached value visible while rotating.
            self._paint_dimensions()
            if self._sketch_frame is not None:
                # Sketch geometry is spatial editing context, not a static
                # decoration. Keep its entities, text, constraints and
                # dimensions projected continuously while the camera moves.
                self._paint_sketch_overlay()
                self._paint_sketch_trim_overlay()
            if self._object_overlay_persistent:
                self._paint_object_overlay()
            self._paint_edge_labels(screen_constant_only=True)
            return
        self._paint_screen_constant_edges()
        self._paint_centerlines()
        self._paint_object_highlights()
        self._paint_reference_highlights()
        self._paint_face_highlight_outlines()
        self._paint_planes()
        self._paint_points()
        self._paint_dimensions()
        self._paint_sketch_overlay()
        self._paint_sketch_trim_overlay()
        self._paint_sketch_selection_box()
        self._paint_object_overlay()
        self._paint_edge_labels()

    def _paint_face_highlight_outlines(self) -> None:
        mesh = self._mesh
        if (
            mesh is None
            or not self._outline_face_highlights
        ):
            return
        highlights = [
            (
                None
                if self._feature_hover_edges
                else self._hovered_face,
                QColor.fromRgbF(1.0, 0.48, 0.0),
            ),
            (
                None
                if self._feature_selected_edges
                else self._selected_face,
                QColor.fromRgbF(0.0, 0.82, 1.0),
            ),
        ]
        highlights.extend(
            (face, QColor.fromRgbF(0.0, 0.82, 1.0))
            for face in self._assembly_reference_faces
            if face != self._selected_face
        )
        highlights.extend(
            (face, QColor.fromRgbF(0.0, 0.82, 1.0))
            for face in self._constraint_reference_faces
            if face != self._selected_face
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
        if event.button() in (
            Qt.MouseButton.LeftButton,
            Qt.MouseButton.RightButton,
        ) and not (event.buttons() & Qt.MouseButton.MiddleButton):
            self._last_click_position = event.position().toPoint()
        if (
            self._sketch_frame is not None
            and event.button() == Qt.MouseButton.LeftButton
        ):
            self.sketchViewClicked.emit()
        if (
            event.button() == Qt.MouseButton.RightButton
            and self._sketch_frame is not None
            and self._sketch_tool == "trim"
        ):
            self._suppress_next_context_menu = True
            self.sketchCancelCurrentRequested.emit()
            event.accept()
            return
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
            if self._sketch_constraint_selection_mode:
                if (
                    self._sketch_tool == "dimension"
                    and any(
                        dimension.key == "sketch_dimension_preview"
                        for dimension in self._dimensions
                    )
                ):
                    self.sketchAlternateCurrentRequested.emit()
                elif self._sketch_selection_candidates(event.position()):
                    # Entity picking stays available while every dimension
                    # or constraint tool is active. Repeated RMB clicks cycle
                    # through coincident entities under the cursor.
                    self._cycle_sketch_entity(event.position())
                else:
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
                if (
                    self._sketch_tool == "rectangle"
                    and len(self._sketch_pending_points) == 1
                    and not self._sketch_rectangle_axis_mode
                    and not self._sketch_selection_candidates(event.position())
                ):
                    self.sketchRectangleAxisModeRequested.emit()
                    event.accept()
                    return
                entity_candidates = self._sketch_selection_candidates(
                    event.position()
                )
                candidates = self._smart_sketch_placement_candidates(
                    event.position()
                )
                if entity_candidates or len(candidates) > 1:
                    self._sketch_placement_candidates = candidates
                    self._sketch_placement_candidate_index = (
                        self._sketch_placement_candidate_index + 1
                    ) % len(candidates)
                    self._sketch_placement_candidate_cursor = QPointF(
                        event.position()
                    )
                    snapped, reference_id, constraint = candidates[
                        self._sketch_placement_candidate_index
                    ]
                    self._sketch_preview_position = snapped
                    self._sketch_preview_constraint = constraint
                    self._hovered_sketch_external_reference_id = reference_id
                    self._preview_sketch_entity_id = (
                        reference_id.removeprefix("sketch_geometry:")
                        if reference_id is not None
                        and reference_id.startswith("sketch_geometry:")
                        and "||" not in reference_id
                        else None
                    )
                    self.update()
                elif self._sketch_tool in (
                    "polyline", "polyline_arc", "hexagon",
                ):
                    self.sketchAlternateCurrentRequested.emit()
                    snapped, reference_id, constraint = (
                        self._sketch_placement_candidate(event.position())
                    )
                    self._sketch_preview_position = snapped
                    self._sketch_preview_constraint = constraint
                    self._hovered_sketch_external_reference_id = reference_id
                    self.update()
                else:
                    self.sketchCancelCurrentRequested.emit()
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_frame is not None
            and not self._sketch_reference_selection_mode
        ):
            if self._sketch_rectangle_axis_mode:
                if self._sketch_rectangle_axis_id is None:
                    axis_id = self._sketch_rectangle_axis_candidate(
                        event.position()
                    )
                    if axis_id is not None:
                        self.sketchRectangleAxisSelected.emit(axis_id)
                event.accept()
                return
            if self._sketch_tool == "trim":
                local = self._sketch_local_position(event.position())
                if local is not None:
                    self._sketch_trim_path = [local]
                    self._sketch_trim_dragging = True
                    self.sketchTrimPreviewRequested.emit(tuple(self._sketch_trim_path))
                    self.update()
                event.accept()
                return
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
                    if selected.startswith("constraint:"):
                        _prefix, owner_id, raw_index = selected.split(":", 2)
                        self.sketchConstraintSelected.emit(
                            owner_id,
                            int(raw_index),
                        )
                    elif selected.startswith("reference:"):
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
                    self._sketch_box_additive = bool(
                        event.modifiers()
                        & Qt.KeyboardModifier.ControlModifier
                    )
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
            if (
                self._sketch_frame is None
                and self._mesh is not None
                and self._mesh.triangle_count > 100_000
                and self._interaction_mode == "object"
                and not self._large_mesh_topology_enabled
            ):
                # Datum points, planes and axes are a tiny overlay and must
                # remain selectable even when detailed STEP topology is
                # disabled. Test those first without touching the triangle
                # array; only a click on the body falls back to its owner.
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
                if point is not None:
                    self._set_selected_point(point)
                elif plane is not None:
                    self._set_selected_plane(plane)
                elif axis is not None:
                    self._set_selected_edge(axis)
                elif self._interaction_mode == "object":
                    # A linear CPU ray test over every display triangle
                    # blocks the event loop for seconds. Select the single
                    # display owner without that scan.
                    owner_id = next(
                        (
                            candidate for candidate
                            in self._mesh.triangle_owner_ids
                            if candidate
                            and candidate
                            not in self._excluded_object_owner_ids
                        ),
                        "",
                    )
                    self._set_selected_object(owner_id or None)
                event.accept()
                return
            if self._interaction_mode == "object":
                if self._selection_preview_pending:
                    self._selection_preview_pending = False
                    if self._object_overlay_mesh is not None:
                        self._object_overlay_persistent = True
                        self._object_overlay_locks_interaction = True
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
            face = (
                None
                if point is not None or edge is not None or plane is not None
                else self._pick_face(event.position())
            )
            self._set_selected_point(point)
            self._set_selected_edge(edge)
            self._set_selected_plane(plane)
            self._set_selected_face(face)
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
            self._navigation_active = True
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
            was_dragged = self._middle_dragged
            if (
                self._middle_press_position is not None
                and (
                    position - self._middle_press_position
                ).manhattanLength()
                > (8 if self._sketch_frame is not None else 3)
            ):
                self._middle_dragged = True
            if event.buttons() & Qt.MouseButton.RightButton:
                self._middle_chorded = True
                self._suppress_next_context_menu = True
                self.camera.pan_x += float(delta.x())
                self.camera.pan_y += float(delta.y())
            else:
                if self._sketch_frame is not None and not self._middle_dragged:
                    # Mouse hardware commonly reports a few pixels of jitter
                    # during a click.  Keep that motion available for the
                    # final spline placement instead of rotating the camera.
                    event.accept()
                    return
                if (
                    self._sketch_frame is not None
                    and not was_dragged
                    and self._middle_press_position is not None
                ):
                    # Rotation starts only after crossing the drag threshold;
                    # apply the full displacement once so navigation does not
                    # jump or lose its initial movement.
                    delta = position - self._middle_press_position
                orbit_camera_state(
                    self.camera,
                    float(delta.x()) * self.rotation_degrees_per_pixel,
                    float(delta.y()) * self.rotation_degrees_per_pixel,
                )
            self.navigationChanged.emit(self.camera)
            self._request_navigation_repaint()
            event.accept()
            return
        if self._sketch_frame is not None and self._sketch_tool == "trim":
            local = self._sketch_local_position(event.position())
            if local is not None:
                if (
                    self._sketch_trim_dragging
                    and event.buttons() & Qt.MouseButton.LeftButton
                ):
                    if (
                        not self._sketch_trim_path
                        or hypot(
                            self._sketch_trim_path[-1][0] - local[0],
                            self._sketch_trim_path[-1][1] - local[1],
                        )
                        >= max(self.sketch_snap_tolerance(3.0), 1.0e-6)
                    ):
                        self._sketch_trim_path.append(local)
                preview_path = (
                    tuple(self._sketch_trim_path)
                    if self._sketch_trim_dragging
                    else (local,)
                )
                self.sketchTrimPreviewRequested.emit(preview_path)
                self.update()
            event.accept()
            return
        if (
            self._sketch_frame is not None
            and self._sketch_selection_mode
            and not self._sketch_reference_selection_mode
        ):
            hovered_constraint = (
                next(
                    (
                        (owner_id, constraint_index)
                        for bounds, owner_id, constraint_index
                        in reversed(self._sketch_constraint_hit_regions)
                        if bounds.adjusted(-4.0, -4.0, 4.0, 4.0).contains(
                            event.position()
                        )
                    ),
                    None,
                )
                if self._sketch_tool == "select"
                else None
            )
            if hovered_constraint != self._hovered_sketch_constraint:
                self._hovered_sketch_constraint = hovered_constraint
                self.update()
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
            constraint_candidate = (
                active_candidate.split(":", 2)
                if active_candidate.startswith("constraint:")
                else None
            )
            if constraint_candidate is not None:
                self._hovered_sketch_constraint = (
                    constraint_candidate[1],
                    int(constraint_candidate[2]),
                )
            elif active_candidate:
                self._hovered_sketch_constraint = None
            self._preview_sketch_entity_id = (
                active_candidate
                if active_candidate
                and reference_id is None
                and constraint_candidate is None
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
            if (
                self._sketch_rectangle_axis_mode
                and self._sketch_rectangle_axis_id is None
            ):
                self._preview_sketch_entity_id = (
                    self._sketch_rectangle_axis_candidate(event.position())
                )
                self._sketch_preview_position = None
                self._sketch_preview_constraint = None
                self.update()
                super().mouseMoveEvent(event)
                return
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
                cursor_length = hypot(cursor_x, cursor_y)
                start_radius = hypot(start_x, start_y)
                if cursor_length > 1.0e-9 and start_radius > 1.0e-9:
                    snapped = (
                        center[0] + start_radius * cursor_x / cursor_length,
                        center[1] + start_radius * cursor_y / cursor_length,
                    )
                    start_angle = atan2(start_y, start_x)
                    quadrant_candidates = tuple(
                        (
                            start_angle + step * pi * 0.5,
                            (
                                center[0] + start_radius * cos(
                                    start_angle + step * pi * 0.5
                                ),
                                center[1] + start_radius * sin(
                                    start_angle + step * pi * 0.5
                                ),
                            ),
                        )
                        for step in (-1, 1, 2)
                    )
                    nearest_quadrant = min(
                        (
                            hypot(
                                event.position().x()
                                - self._screen_point(self._camera_point(
                                    self._sketch_world_point(point)
                                )).x(),
                                event.position().y()
                                - self._screen_point(self._camera_point(
                                    self._sketch_world_point(point)
                                )).y(),
                            ),
                            angle,
                            point,
                        )
                        for angle, point in quadrant_candidates
                    )
                    if nearest_quadrant[0] <= 16.0:
                        _distance, quadrant_angle, snapped = nearest_quadrant
                        constraint = f"keypoint:{degrees(quadrant_angle) % 360.0}"
                    cursor_angle = atan2(
                        snapped[1] - center[1], snapped[0] - center[0]
                    )
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
            elif (
                self._sketch_tool in ("ellipse", "elliptical_arc")
                and len(self._sketch_pending_points) == 2
            ):
                # The third click does not use the raw cursor position: it
                # defines the end of the minor semi-axis, perpendicular to
                # the already chosen major axis. Show that real destination
                # point in the preview and mark it K only when the cursor is
                # close enough to snap to it.
                center, major = self._sketch_pending_points
                ax, ay = major[0] - center[0], major[1] - center[1]
                axis_length = hypot(ax, ay)
                if axis_length > 1.0e-12:
                    nx, ny = -ay / axis_length, ax / axis_length
                    signed = (
                        (snapped[0] - center[0]) * nx
                        + (snapped[1] - center[1]) * ny
                    )
                    snapped = (
                        center[0] + nx * signed,
                        center[1] + ny * signed,
                    )
                    keypoint_screen = self._screen_point(
                        self._camera_point(self._sketch_world_point(snapped))
                    )
                    keypoint_distance = hypot(
                        event.position().x() - keypoint_screen.x(),
                        event.position().y() - keypoint_screen.y(),
                    )
                    if keypoint_distance <= 16.0:
                        constraint = (
                            "keypoint:90.0" if signed >= 0.0
                            else "keypoint:270.0"
                        )
                        reference_id = None
                    elif (
                        constraint is not None
                        and constraint.startswith("keypoint:")
                    ):
                        # A keypoint selected before perpendicular projection
                        # is no longer the point shown or stored.
                        constraint = None
                        reference_id = None
            elif (
                self._sketch_tool == "elliptical_arc"
                and len(self._sketch_pending_points) in (3, 4)
            ):
                center, major, minor = self._sketch_pending_points[:3]
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                determinant = ax * by - ay * bx
                if abs(determinant) > 1.0e-12:
                    def ellipse_angle(point: tuple[float, float]) -> float:
                        dx, dy = point[0] - center[0], point[1] - center[1]
                        return atan2(
                            (ax * dy - ay * dx) / determinant,
                            (dx * by - dy * bx) / determinant,
                        )

                    cursor_angle = ellipse_angle(snapped)
                    snapped = (
                        center[0] + ax * cos(cursor_angle) + bx * sin(cursor_angle),
                        center[1] + ay * cos(cursor_angle) + by * sin(cursor_angle),
                    )
                    quadrant_candidates = tuple(
                        (
                            step * pi * 0.5,
                            (
                                center[0] + ax * cos(step * pi * 0.5)
                                + bx * sin(step * pi * 0.5),
                                center[1] + ay * cos(step * pi * 0.5)
                                + by * sin(step * pi * 0.5),
                            ),
                        )
                        for step in range(4)
                    )
                    nearest_quadrant = min(
                        (
                            hypot(
                                event.position().x()
                                - self._screen_point(self._camera_point(
                                    self._sketch_world_point(point)
                                )).x(),
                                event.position().y()
                                - self._screen_point(self._camera_point(
                                    self._sketch_world_point(point)
                                )).y(),
                            ),
                            angle,
                            point,
                        )
                        for angle, point in quadrant_candidates
                    )
                    if nearest_quadrant[0] <= 16.0:
                        _distance, cursor_angle, snapped = nearest_quadrant
                        constraint = f"keypoint:{degrees(cursor_angle) % 360.0}"
                    if len(self._sketch_pending_points) == 4:
                        start = self._sketch_pending_points[3]
                        if self._sketch_arc_last_angle is None:
                            self._sketch_arc_last_angle = ellipse_angle(start)
                        delta = (
                            cursor_angle - self._sketch_arc_last_angle + pi
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
            if constraint is not None and constraint.startswith(
                "equal_radius:"
            ):
                preview_id = constraint.split(":", 1)[1]
            elif constraint is not None and constraint.startswith(
                "equal_arc_radius:"
            ):
                preview_id = constraint.split(":", 1)[1]
            if preview_id != self._preview_sketch_entity_id:
                self._preview_sketch_entity_id = preview_id
                # Construction highlighting is visual only. Do not report
                # its ID through the point-hover signal used by Coincident.
                self.sketchEntityHovered.emit(point_id or "")
            if reference_id != self._hovered_sketch_external_reference_id:
                self._hovered_sketch_external_reference_id = reference_id
                self.sketchReferenceHovered.emit(reference_id or "")
            self._sketch_preview_is_keypoint = (
                self._placement_is_keypoint(snapped, constraint)
            )
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
        if (
            self._sketch_frame is None
            and (
                self._selected_object_id is not None
                or (
                    self._object_overlay_mesh is not None
                    and self._object_overlay_locks_interaction
                )
            )
        ):
            # A blue whole-object selection is an exclusive viewer state.
            # Do not offer a second, orange topology/object candidate until
            # the user clears the blue selection.
            self._clear_topology_hover()
            self._set_hovered_object(None)
            super().mouseMoveEvent(event)
            return
        self._pending_model_hover_position = QPointF(event.position())
        if not self._model_hover_timer.isActive():
            self._model_hover_timer.start()
        super().mouseMoveEvent(event)

    def _apply_pending_model_hover(self) -> None:
        position = self._pending_model_hover_position
        self._pending_model_hover_position = None
        if (
            position is None
            or (
                self._sketch_frame is not None
                and not self._sketch_reference_selection_mode
            )
            or not self._selection_enabled
            or self._navigation_active
        ):
            return
        self._last_model_hover_position = QPointF(position)
        if (
            self._selected_object_id is not None
            or (
                self._object_overlay_mesh is not None
                and self._object_overlay_locks_interaction
            )
        ):
            return
        if (
            self._mesh is not None
            and self._mesh.triangle_count > 100_000
            and self._interaction_mode == "object"
            and not self._large_mesh_topology_enabled
        ):
            # Hover picking used to project/test all 157k+ triangles for
            # every cursor event.  While that O(N) scan was running, middle
            # press and wheel events accumulated in Qt's queue and navigation
            # appeared to start only after a long wait.
            point = self._pick_point(position)
            plane = (
                None
                if point is not None
                else self._pick_plane(position)
            )
            axis = (
                None
                if point is not None or plane is not None
                else self._pick_axis(position)
            )
            self._set_hovered_object(None)
            # Update the resolved hover state directly. Clearing every kind
            # first and then restoring the same datum under the cursor emits
            # several redundant signals and can create a self-sustaining
            # repaint storm on a large imported STEP view.
            self._set_hovered_face(None)
            self._set_hovered_point(point)
            self._set_hovered_plane(plane)
            self._set_hovered_edge(axis)
            return
        if self._interaction_mode == "object":
            point = self._pick_point(position)
            plane = (
                None
                if point is not None
                else self._pick_plane(position)
            )
            axis = (
                None
                if point is not None or plane is not None
                else self._pick_axis(position)
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
                face = self._pick_face(position)
                self._set_hovered_object(
                    face[0] if face is not None
                    else self._pick_object(position)
                )
                # Emit the face last. The application can then replace the
                # whole-object hover with a feature-boundary highlight.
                self._set_hovered_face(face)
            return
        point = self._pick_point(position)
        edge = None if point is not None else self._pick_edge(position)
        plane = (
            None
            if point is not None or edge is not None
            else self._pick_plane(position)
        )
        self._set_hovered_point(point)
        self._set_hovered_edge(edge)
        self._set_hovered_plane(plane)
        self._set_hovered_face(
            None if point is not None or edge is not None or plane is not None
            else self._pick_face(position)
        )

    def _request_navigation_repaint(self) -> None:
        if self._navigation_repaint_pending:
            return
        self._navigation_repaint_pending = True

        def repaint_latest_camera() -> None:
            self._navigation_repaint_pending = False
            self.update()

        # High-polling mice can deliver hundreds of orbit events per second.
        # Render only the latest camera state at roughly display cadence so
        # navigation cannot starve clicks, toolbar toggles or window events.
        QTimer.singleShot(16, repaint_latest_camera)

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
            and self._sketch_frame is not None
            and self._sketch_tool == "trim"
            and self._sketch_trim_dragging
        ):
            local = self._sketch_local_position(event.position())
            if local is not None and (
                not self._sketch_trim_path
                or hypot(
                    self._sketch_trim_path[-1][0] - local[0],
                    self._sketch_trim_path[-1][1] - local[1],
                ) > 1.0e-9
            ):
                self._sketch_trim_path.append(local)
            path = tuple(self._sketch_trim_path)
            self._sketch_trim_path.clear()
            self._sketch_trim_dragging = False
            self.sketchTrimGestureRequested.emit(path)
            self.update()
            event.accept()
            return
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
            additive = self._sketch_box_additive
            self._sketch_box_additive = False
            self.sketchEntitiesSelected.emit(selected, additive)
            self.update()
            event.accept()
            return
        if event.button() == Qt.MouseButton.MiddleButton:
            self._navigation_active = False
            self.update()
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
            application = QApplication.instance()
            middle_confirmation_target = (
                getattr(application, "_middle_confirmation_target", None)
                if application is not None
                else None
            )
            properties_apply_active = (
                isinstance(middle_confirmation_target, QDialog)
                and middle_confirmation_target.isVisible()
            )
            dismiss_view_selection = (
                self._sketch_frame is None
                and not self._middle_dragged
                and not self._middle_chorded
                and not properties_apply_active
            )
            self._last_mouse_position = None
            self._middle_press_position = None
            self._middle_dragged = False
            self._middle_chorded = False
            if confirm_sketch:
                if (
                    self._sketch_tool == "spline"
                ):
                    # Spline completion must not depend on a cached preview or
                    # on the generic sketch selection-mode branch.  Resolve
                    # the release position, offer it as the final point, then
                    # always send the explicit finish request.  Signals are
                    # synchronous: a successful placement commits and clears
                    # the pending chain, making the finish request a no-op;
                    # if placement is rejected (for example as a duplicate),
                    # the finish request commits the existing valid chain.
                    (
                        self._sketch_preview_position,
                        self._hovered_sketch_external_reference_id,
                        self._sketch_preview_constraint,
                    ) = self._sketch_placement_candidate(event.position())
                    self.sketchPlacementConfirmed.emit(
                        *self._sketch_preview_position,
                        self._hovered_sketch_external_reference_id or "",
                        self._sketch_preview_constraint or "",
                    )
                    self.sketchConfirmCurrentRequested.emit()
                elif (
                    self._sketch_preview_position is not None
                    and not self._sketch_selection_mode
                ):
                    self.sketchPlacementConfirmed.emit(
                        *self._sketch_preview_position,
                        self._hovered_sketch_external_reference_id or "",
                        self._sketch_preview_constraint or "",
                    )
                else:
                    self.sketchConfirmCurrentRequested.emit()
                self._preview_sketch_entity_id = None
                self._sketch_cycle_ids = ()
                self._sketch_cycle_index = -1
                self.update()
            elif dismiss_view_selection:
                self.dimensionsDismissRequested.emit()
                self._clear_topology_selection()
                # Keep a plain middle click consistent with an empty-space
                # click and a middle double-click. A dragged middle gesture
                # is navigation only and deliberately preserves selection.
                self.selectedObjectChanged.emit("")
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

    def _paint_sketch_trim_overlay(self) -> None:
        if self._sketch_frame is None or self._sketch_tool != "trim":
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        if self._sketch_trim_preview_paths:
            painter.setPen(QPen(QColor("#FF7A00"), 4.0))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            for path in self._sketch_trim_preview_paths:
                painter.drawPolyline(QPolygonF([
                    self._screen_point(
                        self._camera_point(self._sketch_world_point(point))
                    )
                    for point in path
                ]))
        if self._sketch_trim_path:
            painter.setPen(QPen(QColor("#FFD740"), 2.0))
            screen_path = [
                self._screen_point(
                    self._camera_point(self._sketch_world_point(point))
                )
                for point in self._sketch_trim_path
            ]
            if len(screen_path) >= 2:
                painter.drawPolyline(QPolygonF(screen_path))
            elif screen_path:
                painter.setBrush(QBrush(QColor("#FFD740")))
                painter.drawEllipse(screen_path[0], 3.5, 3.5)
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
        crossing_selection = (
            self._sketch_box_end.x() < self._sketch_box_start.x()
        )
        rectangle_edges = (
            QLineF(rectangle.topLeft(), rectangle.topRight()),
            QLineF(rectangle.topRight(), rectangle.bottomRight()),
            QLineF(rectangle.bottomRight(), rectangle.bottomLeft()),
            QLineF(rectangle.bottomLeft(), rectangle.topLeft()),
        )
        for entity in self._sketch_entities:
            entity_id = str(entity.get("id", ""))
            entity_type = str(entity.get("type", ""))
            if (
                not entity_id
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
            if entity_type == "circle" and len(local_points) == 1:
                center = local_points[0]
                radius = float(entity.get("radius", 0.0))
                local_points = [
                    (
                        center[0] + radius * cos(2.0 * pi * index / 64.0),
                        center[1] + radius * sin(2.0 * pi * index / 64.0),
                    )
                    for index in range(65)
                ]
            elif (
                entity_type == "arc"
                and entity.get("arc_mode") == "center"
                and len(local_points) >= 3
            ):
                local_points = list(center_arc_points(
                    tuple(local_points[0]),
                    tuple(local_points[1]),
                    tuple(local_points[2]),
                    clockwise=bool(entity.get("clockwise", False)),
                ))
            elif entity_type == "ellipse" and len(local_points) >= 3:
                local_points = list(ellipse_points(
                    tuple(local_points[0]),
                    tuple(local_points[1]),
                    tuple(local_points[2]),
                ))
            elif entity_type == "elliptical_arc" and len(local_points) >= 5:
                local_points = list(elliptical_arc_points(
                    tuple(local_points[0]),
                    tuple(local_points[1]),
                    tuple(local_points[2]),
                    tuple(local_points[3]),
                    tuple(local_points[4]),
                    clockwise=bool(entity.get("clockwise", False)),
                ))
            elif entity_type == "spline" and len(local_points) >= 2:
                local_points = list(_interpolated_spline_points(
                    tuple(local_points),
                    stored_spline_tangent(entity, "start_tangent"),
                    stored_spline_tangent(entity, "end_tangent"),
                ))
            screen_points = [
                self._screen_point(
                    self._camera_point(self._sketch_world_point(point))
                )
                for point in local_points
            ]
            contained = bool(screen_points) and all(
                rectangle.contains(point) for point in screen_points
            )
            crossed = crossing_selection and (
                any(rectangle.contains(point) for point in screen_points)
                or any(
                    segment.intersects(edge)[0]
                    == QLineF.IntersectionType.BoundedIntersection
                    for first, second in zip(
                        screen_points, screen_points[1:]
                    )
                    for segment in (QLineF(first, second),)
                    for edge in rectangle_edges
                )
            )
            if contained or crossed:
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
                self._clear_topology_selection()
                # A tree selection need not be mirrored in the viewport, so
                # explicitly notify the application even when the viewer was
                # already internally empty.
                self.selectedObjectChanged.emit("")
            self._last_mouse_position = None
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._sketch_frame is not None
            and self._sketch_tool in ("select", "dimension")
        ):
            text_group = self._sketch_text_candidate(event.position())
            if text_group:
                self.sketchTextEditRequested.emit(text_group)
                event.accept()
                return
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
            and self._sketch_frame is None
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
        # CAD navigation convention requested by the application: rolling
        # the wheel forward zooms out and rolling it backward zooms in.
        wheel_steps = -event.angleDelta().y() / 120.0
        if wheel_steps:
            old_zoom = self.camera.zoom
            new_zoom = max(
                1e-4,
                min(1e4, old_zoom * 1.15 ** wheel_steps),
            )
            ratio = new_zoom / old_zoom
            cursor = event.position()
            self.camera.pan_x = (
                cursor.x() - self.width() * 0.5
                - (cursor.x() - self.width() * 0.5 - self.camera.pan_x)
                * ratio
            )
            self.camera.pan_y = (
                cursor.y() - self.height() * 0.5
                - (cursor.y() - self.height() * 0.5 - self.camera.pan_y)
                * ratio
            )
            self.camera.zoom = new_zoom
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
        for buffer in (
            self._surface_buffer,
            self._edge_buffer,
            self._silhouette_buffer,
        ):
            if buffer is not None and buffer.isCreated():
                buffer.destroy()
        self._background_vao = None
        self._surface_vao = None
        self._edge_vao = None
        self._surface_buffer = None
        self._edge_buffer = None
        self._silhouette_buffer = None
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
        edge_values = array("f")
        edge_ranges: list[tuple[int, int]] = []
        surface_key = (
            (
                id(mesh.triangle_positions),
                id(mesh.triangle_normals),
                id(mesh.triangle_face_indices),
                id(mesh.triangle_owner_ids),
            )
            if mesh is not None else None
        )
        surface_changed = surface_key != self._uploaded_surface_key
        if mesh is not None:
            if surface_changed:
                if surface_key != self._surface_data_cache_key:
                    surface_values = array("f")
                    for offset in range(0, len(mesh.triangle_positions), 3):
                        surface_values.extend(
                            mesh.triangle_positions[offset:offset + 3]
                        )
                        surface_values.extend(
                            mesh.triangle_normals[offset:offset + 3]
                        )
                    self._surface_data_cache_key = surface_key
                    self._surface_data_cache = surface_values.tobytes()
                    self._surface_face_ranges_cache = (
                        self._build_face_ranges(mesh)
                    )
                    self._surface_owner_ranges_cache = (
                        self._build_owner_ranges(mesh)
                    )
            base_edge_mesh = self._base_edge_mesh
            base_edge_count = (
                len(base_edge_mesh.edges)
                if base_edge_mesh is not None
                and len(base_edge_mesh.edges) <= len(mesh.edges)
                else 0
            )
            base_cache_source = (
                base_edge_mesh.edges if base_edge_count else None
            )
            if base_cache_source is not self._base_edge_cache_source:
                base_values = array("f")
                base_ranges: list[tuple[int, int]] = []
                base_vertex_start = 0
                for edge in (
                    base_edge_mesh.edges if base_edge_mesh is not None else ()
                ):
                    display_points = self._display_edge_points(edge)
                    for point in display_points:
                        base_values.extend(point)
                    base_ranges.append(
                        (base_vertex_start, len(display_points))
                    )
                    base_vertex_start += len(display_points)
                self._base_edge_cache_source = base_cache_source
                self._base_edge_cache_data = base_values.tobytes()
                self._base_edge_cache_ranges = tuple(base_ranges)
                self._base_edge_cache_vertex_count = base_vertex_start
            edge_ranges.extend(self._base_edge_cache_ranges)
            edge_vertex_start = self._base_edge_cache_vertex_count
            for edge in mesh.edges[base_edge_count:]:
                display_points = self._display_edge_points(edge)
                for point in display_points:
                    edge_values.extend(point)
                edge_ranges.append(
                    (edge_vertex_start, len(display_points))
                )
                edge_vertex_start += len(display_points)
        edge_data = self._base_edge_cache_data + edge_values.tobytes()
        if surface_changed:
            surface_buffer.bind()
            surface_buffer.allocate(
                self._surface_data_cache,
                len(self._surface_data_cache),
            )
            surface_buffer.release()
        edge_buffer.bind()
        edge_buffer.allocate(edge_data, len(edge_data))
        edge_buffer.release()
        if surface_changed:
            self._surface_vertex_count = len(self._surface_data_cache) // 24
            self._face_ranges = self._surface_face_ranges_cache
            self._owner_ranges = self._surface_owner_ranges_cache
            self._uploaded_surface_key = surface_key
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
        if self._owner_ranges:
            for owner_id, start, count in self._owner_ranges:
                owner_color = self._surface_colors_by_owner_id.get(
                    owner_id,
                    self._surface_color,
                )
                program.setUniformValue(
                    "surfaceColor",
                    QVector3D(
                        owner_color.redF(),
                        owner_color.greenF(),
                        owner_color.blueF(),
                    ),
                )
                gl.glDrawArrays(GL_TRIANGLES, start, count)
        else:
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

    @staticmethod
    def _build_owner_ranges(
        mesh: ViewerMesh | None,
    ) -> tuple[tuple[str, int, int], ...]:
        """Batch adjacent triangles with the same display colour owner."""
        if mesh is None or not mesh.triangle_owner_ids:
            return ()
        ranges: list[tuple[str, int, int]] = []
        first_triangle = 0
        current_owner = mesh.triangle_owner_ids[0]
        for triangle_index, owner_id in enumerate(
            mesh.triangle_owner_ids[1:], start=1
        ):
            if owner_id == current_owner:
                continue
            ranges.append((
                current_owner,
                first_triangle * 3,
                (triangle_index - first_triangle) * 3,
            ))
            current_owner = owner_id
            first_triangle = triangle_index
        ranges.append((
            current_owner,
            first_triangle * 3,
            (len(mesh.triangle_owner_ids) - first_triangle) * 3,
        ))
        return tuple(ranges)

    def _draw_highlighted_face(
        self,
        gl: QOpenGLExtraFunctions,
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
        if self._display_mode == "hidden_edges":
            # Paint the complete wire image in grey, then let the regular
            # depth-tested pass below overwrite only visible edges.  This is
            # more robust than an inverse-depth pass around polygon offsets.
            gl.glDisable(GL_DEPTH_TEST)
            program.setUniformValue(
                "edgeColor",
                QVector3D(0.5, 0.5, 0.5),
            )
            gl.glLineWidth(1.0)
            for edge, (first_vertex, vertex_count) in zip(
                mesh.edges if mesh is not None else (),
                self._edge_ranges,
            ):
                if (
                    edge.element_kind == "edge"
                    and edge_visible_in_display(edge, self._display_mode)
                ):
                    gl.glDrawArrays(
                        GL_LINE_STRIP,
                        first_vertex,
                        vertex_count,
                    )
            gl.glEnable(GL_DEPTH_TEST)
            gl.glDepthFunc(GL_LEQUAL)
            gl.glLineWidth(max(1.0, float(self.devicePixelRatioF())))
        for edge, (first_vertex, vertex_count) in zip(
            mesh.edges if mesh is not None else (),
            self._edge_ranges,
        ):
            if edge.element_kind == "centerline" or edge.screen_constant:
                continue
            if edge.element_kind == "sketch" and self._sketch_frame is not None:
                # The active sketch is painted by the editable 2D overlay.
                # Keeping its cached 3D scene edges visible duplicates every
                # entity and exposes hidden generated text contours.
                continue
            if not draw_base_edges and edge.element_kind == "edge":
                continue
            if not edge_visible_in_display(edge, self._display_mode):
                continue
            if edge.element_kind in {"axis", "sketch", "dimension"}:
                gl.glDisable(GL_DEPTH_TEST)
            preview_wire_color = (
                (0.0, 0.82, 1.0)
                if edge.owner_id in self._feature_preview_owner_ids
                and self._display_mode in {
                    "wire", "hidden_edges", "no_hidden",
                }
                else None
            )
            program.setUniformValue(
                "edgeColor",
                QVector3D(*(
                    preview_wire_color
                    if preview_wire_color is not None
                    else (
                        self._edge_color_override.redF(),
                        self._edge_color_override.greenF(),
                        self._edge_color_override.blueF(),
                    )
                    if self._edge_color_override is not None
                    and edge.element_kind == "edge"
                    else edge.base_color
                )),
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
        for edge in self._assembly_reference_edges:
            self._draw_highlighted_edge(
                gl,
                program,
                edge,
                QVector3D(0.0, 0.82, 1.0),
                3.0,
            )
        self._draw_gpu_silhouette_edges(gl, program)
        program.disableAttributeArray(0)
        buffer.release()
        program.release()
        vao.release()

    def _draw_gpu_silhouette_edges(
        self,
        gl: QOpenGLExtraFunctions,
        program: QOpenGLShaderProgram,
    ) -> None:
        buffer = self._silhouette_buffer
        if (
            buffer is None
            or self._display_mode not in {
                "wire", "hidden_edges", "no_hidden", "shaded_with_edges",
            }
            or not self._silhouette_edges
        ):
            return
        view_direction = self._inverse_rotate((0.0, 0.0, 1.0))
        segments = silhouette_segments_from_edges(
            self._silhouette_edges,
            view_direction,
        )
        if not segments:
            return
        values = array("f")
        for first, second in segments:
            values.extend(first)
            values.extend(second)
        data = values.tobytes()
        buffer.bind()
        buffer.allocate(data, len(data))
        program.setAttributeBuffer(0, 0x1406, 0, 3, 12)
        if self._display_mode == "hidden_edges":
            gl.glDisable(GL_DEPTH_TEST)
            program.setUniformValue(
                "edgeColor",
                QVector3D(0.5, 0.5, 0.5),
            )
            gl.glLineWidth(1.0)
            gl.glDrawArrays(GL_LINES, 0, len(values) // 3)
            gl.glEnable(GL_DEPTH_TEST)
            gl.glDepthFunc(GL_LEQUAL)
        program.setUniformValue(
            "edgeColor",
            QVector3D(*(
                (
                    self._edge_color_override.redF(),
                    self._edge_color_override.greenF(),
                    self._edge_color_override.blueF(),
                )
                if self._edge_color_override is not None
                else (0.086, 0.098, 0.118)
            )),
        )
        gl.glLineWidth(max(1.0, float(self.devicePixelRatioF())))
        gl.glDrawArrays(GL_LINES, 0, len(values) // 3)
        buffer.release()

    def _draw_highlighted_edge(
        self,
        gl: QOpenGLExtraFunctions,
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
        if edge.element_kind == "centerline" or edge.screen_constant:
            return
        if not edge_visible_in_display(edge, self._display_mode):
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
        gl: QOpenGLExtraFunctions,
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
                or not edge_visible_in_display(edge, self._display_mode)
            ):
                continue
            if edge.element_kind == "sketch":
                gl.glDisable(GL_DEPTH_TEST)
            gl.glDrawArrays(GL_LINE_STRIP, first_vertex, vertex_count)
            if edge.element_kind == "sketch":
                gl.glEnable(GL_DEPTH_TEST)

    def _draw_highlighted_reference(
        self,
        gl: QOpenGLExtraFunctions,
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
            if (
                edge.owner_id == owner_id
                and edge.element_kind == "axis"
                and not edge.screen_constant
            ):
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

    def _paint_screen_constant_edges(self) -> None:
        """Draw datum axes at a camera-independent on-screen length."""
        mesh = self._mesh
        if mesh is None:
            return
        edges = tuple(edge for edge in mesh.edges if edge.screen_constant)
        if not edges:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for edge in edges:
            color = edge.base_color
            key = (edge.owner_id, edge.edge_index)
            width = 1.5
            if key == self._hovered_edge:
                color = (1.0, 0.48, 0.0)
                width = 3.0
            if (
                key == self._selected_edge
                or edge.owner_id == self._selected_reference_owner_id
                or edge.owner_id in self._selected_container_content_ids
            ):
                color = (0.0, 0.82, 1.0)
                width = 3.0
            painter.setPen(QPen(QColor.fromRgbF(*color, 1.0), width))
            projected = [
                self._screen_point(self._camera_point(point))
                for point in self._display_edge_points(edge)
            ]
            for index in range(1, len(projected)):
                painter.drawLine(projected[index - 1], projected[index])
        painter.end()

    def _paint_edge_labels(self, *, screen_constant_only: bool = False) -> None:
        mesh = self._mesh
        if mesh is None:
            return
        labelled_edges = tuple(
            edge for edge in mesh.edges
            if edge.label and (not screen_constant_only or edge.screen_constant)
        )
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
                    "arrow_base": endpoint,
                    "arrow_tail": endpoint,
                    "opposite_arrow": QPolygonF(),
                    "opposite_arrow_base": endpoint,
                    "opposite_arrow_tail": endpoint,
                    "value_position": center,
                }
            ux, uy = dx / length, dy / length
            px, py = -uy, ux
            arrow_length = 10.0
            arrow_half_width = arrow_length * tan(radians(15.0))
            tail_length = 7.0
            outside = dimension.arrow_placement == "outside"
            arrow_sign = 1.0 if outside else -1.0
            base = QPointF(
                endpoint.x() + ux * arrow_length * arrow_sign,
                endpoint.y() + uy * arrow_length * arrow_sign,
            )
            tail = QPointF(
                base.x() + ux * tail_length * arrow_sign,
                base.y() + uy * tail_length * arrow_sign,
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
            opposite_tail = QPointF(
                opposite_base.x() - ux * tail_length,
                opposite_base.y() - uy * tail_length,
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
            # Match linear dimensions: continue a short tail behind the
            # arrow before the horizontal shoulder begins.
            radial_end = endpoint
            shoulder_start = tail
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
                "arrow_base": base,
                "arrow_tail": tail,
                "opposite_arrow": (
                    opposite_arrow if dimension.diameter else QPolygonF()
                ),
                "opposite_arrow_base": opposite_base,
                "opposite_arrow_tail": opposite_tail,
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
                if len(vector) < 3:
                    return None
                length = sqrt(dot(vector, vector))
                if length <= 1.0e-12:
                    return None
                return tuple(value / length for value in vector)

            vertex = self._screen_point(
                self._camera_point(dimension.vertex)
            )

            def invalid_angular_geometry() -> dict[str, Any]:
                return {
                    "angular": True,
                    "valid": False,
                    "vertex": vertex,
                    "first_dimension": vertex,
                    "second_dimension": vertex,
                    "arc": QPolygonF(),
                    "value_position": vertex,
                }

            first_vector = normalized(
                subtract(dimension.first_direction_point, dimension.vertex)
            )
            second_vector = normalized(
                subtract(dimension.second_direction_point, dimension.vertex)
            )
            if first_vector is None or second_vector is None:
                return invalid_angular_geometry()
            projection = dot(first_vector, second_vector)
            plane_normal = normalized(dimension.plane_normal or ())
            # A supplied modeling plane is the stable authority for sweep
            # orientation.  Deriving the second basis vector from the end ray
            # flips it after 180 degrees and degenerates exactly at 180/360.
            plane_second = (
                normalized((
                    plane_normal[1] * first_vector[2]
                    - plane_normal[2] * first_vector[1],
                    plane_normal[2] * first_vector[0]
                    - plane_normal[0] * first_vector[2],
                    plane_normal[0] * first_vector[1]
                    - plane_normal[1] * first_vector[0],
                ))
                if plane_normal is not None
                else normalized(
                    tuple(
                        second_vector[index]
                        - projection * first_vector[index]
                        for index in range(3)
                    )
                )
            )
            if plane_second is None:
                # Parallel directions are the valid geometry of a 0° (or
                # 180°) dimension.  Preserve the physical rotation plane
                # supplied by the constraint solver instead of deriving a
                # semantic direction from the current camera.
                plane_second = (
                    normalized((
                        plane_normal[1] * first_vector[2]
                        - plane_normal[2] * first_vector[1],
                        plane_normal[2] * first_vector[0]
                        - plane_normal[0] * first_vector[2],
                        plane_normal[0] * first_vector[1]
                        - plane_normal[1] * first_vector[0],
                    ))
                    if plane_normal is not None
                    else None
                )
                if plane_second is None:
                    trial = min(
                        (
                            (1.0, 0.0, 0.0),
                            (0.0, 1.0, 0.0),
                            (0.0, 0.0, 1.0),
                        ),
                        key=lambda candidate: abs(
                            dot(first_vector, candidate)
                        ),
                    )
                    trial_projection = dot(first_vector, trial)
                    plane_second = normalized(tuple(
                        trial[index]
                        - trial_projection * first_vector[index]
                        for index in range(3)
                    ))
                if plane_second is None:
                    return invalid_angular_geometry()
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
                    self._camera_point(
                        arc_world(sweep * i / steps)
                    )
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

            def angular_arrow(
                tip: QPointF,
                inward_x: float,
                inward_y: float,
            ) -> tuple[QPolygonF, QPointF, QPointF]:
                inward_length = hypot(inward_x, inward_y)
                if inward_length <= 1.0e-9:
                    return QPolygonF(), tip, tip
                inward_x /= inward_length
                inward_y /= inward_length
                arrow_length = 10.0
                tail_length = 7.0
                half_width = arrow_length * tan(radians(15.0))
                base_x = tip.x() - inward_x * arrow_length
                base_y = tip.y() - inward_y * arrow_length
                base = QPointF(base_x, base_y)
                tail = QPointF(
                    base_x - inward_x * tail_length,
                    base_y - inward_y * tail_length,
                )
                perpendicular_x = -inward_y
                perpendicular_y = inward_x
                return (
                    QPolygonF([
                        tip,
                        QPointF(
                            base_x + perpendicular_x * half_width,
                            base_y + perpendicular_y * half_width,
                        ),
                        QPointF(
                            base_x - perpendicular_x * half_width,
                            base_y - perpendicular_y * half_width,
                        ),
                    ]),
                    base,
                    tail,
                )

            fallback_tangent_x = (
                projected_second_unit.x() - vertex.x()
            )
            fallback_tangent_y = (
                projected_second_unit.y() - vertex.y()
            )
            first_inward_x = arc[1].x() - arc[0].x()
            first_inward_y = arc[1].y() - arc[0].y()
            second_inward_x = arc[-2].x() - arc[-1].x()
            second_inward_y = arc[-2].y() - arc[-1].y()
            if hypot(first_inward_x, first_inward_y) <= 1.0e-9:
                first_inward_x = fallback_tangent_x
                first_inward_y = fallback_tangent_y
            if hypot(second_inward_x, second_inward_y) <= 1.0e-9:
                second_inward_x = -fallback_tangent_x
                second_inward_y = -fallback_tangent_y
            first_arrow, first_arrow_base, first_tail = angular_arrow(
                arc[0], first_inward_x, first_inward_y
            )
            second_arrow, second_arrow_base, second_tail = angular_arrow(
                arc[-1], second_inward_x, second_inward_y
            )
            return {
                "angular": True,
                "vertex": vertex,
                "first_dimension": arc[0],
                "second_dimension": arc[-1],
                "arc": arc,
                "first_arrow": first_arrow,
                "second_arrow": second_arrow,
                "first_arrow_base": first_arrow_base,
                "second_arrow_base": second_arrow_base,
                "first_tail": first_tail,
                "second_tail": second_tail,
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
                painter.drawLine(
                    geometry["arrow_base"], geometry["arrow_tail"]
                )
                if not geometry["opposite_arrow"].isEmpty():
                    painter.drawPolygon(geometry["opposite_arrow"])
                    painter.drawLine(
                        geometry["opposite_arrow_base"],
                        geometry["opposite_arrow_tail"],
                    )
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
                first_dimension = geometry.get("first_dimension")
                second_dimension = geometry.get("second_dimension")
                if (
                    isinstance(first_dimension, QPointF)
                    and isinstance(second_dimension, QPointF)
                ):
                    painter.drawLine(
                        geometry["vertex"], first_dimension
                    )
                    painter.drawLine(
                        geometry["vertex"], second_dimension
                    )
                arc = geometry.get("arc")
                if isinstance(arc, QPolygonF) and not arc.isEmpty():
                    painter.drawPolyline(arc)
                for arrow_key in ("first_arrow", "second_arrow"):
                    arrow = geometry.get(arrow_key)
                    if isinstance(arrow, QPolygonF) and not arrow.isEmpty():
                        painter.drawPolygon(arrow)
                for base_key, tail_key in (
                    ("first_arrow_base", "first_tail"),
                    ("second_arrow_base", "second_tail"),
                ):
                    base = geometry.get(base_key)
                    tail = geometry.get(tail_key)
                    if isinstance(base, QPointF) and isinstance(tail, QPointF):
                        painter.drawLine(base, tail)
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

    def _pending_spline_start_tangent(
        self,
        preview: tuple[float, float] | None = None,
    ) -> tuple[float, float] | None:
        if (
            self._sketch_tool != "spline"
            or not self._sketch_pending_points
        ):
            return None
        positions = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        first_position = self._sketch_pending_points[0]
        first_id = next((
            point_id for point_id, point in positions.items()
            if hypot(
                point[0] - first_position[0],
                point[1] - first_position[1],
            ) <= 1.0e-9
        ), "")
        if not first_id:
            return None
        support = spline_endpoint_support_tangent(
            self._sketch_entities,
            first_id,
            positions,
        )
        if support is None:
            return None
        target = (
            self._sketch_pending_points[1]
            if len(self._sketch_pending_points) >= 2
            else preview
        )
        if target is None:
            return None
        start = self._sketch_pending_points[0]
        return orient_tangent(
            support[1], (target[0] - start[0], target[1] - start[1])
        )

    def _pending_spline_end_tangent(
        self,
        preview: tuple[float, float] | None,
    ) -> tuple[float, float] | None:
        if (
            self._sketch_tool != "spline"
            or not self._sketch_pending_points
            or preview is None
        ):
            return None
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        endpoint_id = next((
            point_id for point_id, point in points.items()
            if hypot(point[0] - preview[0], point[1] - preview[1]) <= 1.0e-9
        ), "")
        if endpoint_id:
            support = spline_endpoint_support_tangent(
                self._sketch_entities, endpoint_id, points
            )
            if support is not None:
                previous = self._sketch_pending_points[-1]
                return orient_tangent(
                    support[1],
                    (preview[0] - previous[0], preview[1] - previous[1]),
                )
        reference_id = self._hovered_sketch_external_reference_id or ""
        if not reference_id.startswith("sketch_circle:"):
            return None
        geometry_id = reference_id.split(":", 1)[1]
        circle = next((
            entity for entity in self._sketch_entities
            if entity.get("type") == "circle"
            and str(entity.get("id", "")) == geometry_id
        ), None)
        ids = tuple(map(str, circle.get("point_ids", ()))) if circle else ()
        center = points.get(ids[0]) if ids else None
        if center is None:
            return None
        radial = (preview[0] - center[0], preview[1] - center[1])
        direction = (
            preview[0] - self._sketch_pending_points[-1][0],
            preview[1] - self._sketch_pending_points[-1][1],
        )
        return orient_tangent((-radial[1], radial[0]), direction)

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
        hovered_reference_ids = set(
            component
            for component in (
                self._hovered_sketch_external_reference_id or ""
            ).split("||")
            if component
        )
        if (
            self._sketch_preview_constraint is not None
            and self._sketch_preview_constraint.startswith("tangent_first:")
        ):
            tangent_curve_id = self._sketch_preview_constraint.split(":", 1)[1]
            tangent_curve = next(
                (
                    entity for entity in self._sketch_entities
                    if str(entity.get("id", "")) == tangent_curve_id
                ),
                None,
            )
            if tangent_curve is not None:
                hovered_reference_ids.add(
                    f"sketch_{tangent_curve.get('type', '')}:{tangent_curve_id}"
                )
        if (
            self._sketch_preview_constraint is not None
            and self._sketch_preview_constraint.startswith("equal_length:")
        ):
            hovered_reference_ids.add(
                "sketch_geometry:"
                + self._sketch_preview_constraint.split(":", 2)[1]
            )
        if (
            self._sketch_preview_constraint is not None
            and self._sketch_preview_constraint.startswith(
                ("symmetric_point:", "rectangle_symmetric:", "rectangle_oriented:")
            )
        ):
            hovered_reference_ids.add(
                "sketch_geometry:"
                + self._sketch_preview_constraint.split(":", 2)[1]
            )

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
                highlighted_auxiliary(
                    QColor("#00D1FF")
                    if axis_reference_id
                    == self._selected_sketch_reference_id
                    else QColor("#FF7A00")
                )
                if axis_reference_id in hovered_reference_ids
                or axis_reference_id == self._selected_sketch_reference_id
                else dashed
            )
            infinite_line(origin, end)
        origin_hovered = (
            "sketch_origin" in hovered_reference_ids
        )
        origin_selected = self._selected_sketch_reference_id == "sketch_origin"
        origin_color = (
            QColor("#00D1FF")
            if origin_selected
            else QColor("#FF7A00") if origin_hovered else yellow
        )
        painter.setPen(QPen(
            origin_color,
            2.5 if origin_hovered or origin_selected else 2.0,
        ))
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
            reference_hovered = (
                str(reference.get("id", "")) in hovered_reference_ids
            )
            reference_selected = (
                str(reference.get("id", ""))
                == self._selected_sketch_reference_id
            )
            reference_color = (
                cyan
                if reference.get("selected") or reference_selected
                else (
                    QColor("#FF7A00")
                    if reference_hovered
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
                    or reference_selected
                    or reference_hovered
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
                    or reference_selected
                    or reference_hovered
                    else value
                    for value in (12.0, 10.0)
                ]
            )
            painter.setPen(reference_pen)
            painter.setBrush(QBrush(reference_color))
            geometry_type = geometry.get("type")
            if geometry_type == "axis_point":
                if not self._external_point_marker_visible(
                    reference,
                    self._hovered_sketch_external_reference_id,
                    self._sketch_reference_snapping
                    or self._sketch_reference_selection_mode,
                ):
                    continue
                point_color = (
                    cyan
                    if reference.get("selected") or reference_selected
                    else QColor("#FF7A00")
                    if reference_hovered
                    else QColor("#B34A3C")
                    if reference.get("broken")
                    else brown
                )
                painter.setPen(QPen(point_color, base_centerline_width))
                painter.setBrush(Qt.BrushStyle.NoBrush)
                point = geometry.get("point", ())
                if isinstance(point, (list, tuple)) and len(point) >= 2:
                    center = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (float(point[0]), float(point[1]))
                            )
                        )
                    )
                    marker_radius = 6.0 if reference.get("selected") else 5.0
                    painter.drawLine(
                        QPointF(
                            center.x() - marker_radius,
                            center.y() - marker_radius,
                        ),
                        QPointF(
                            center.x() + marker_radius,
                            center.y() + marker_radius,
                        ),
                    )
                    painter.drawLine(
                        QPointF(
                            center.x() - marker_radius,
                            center.y() + marker_radius,
                        ),
                        QPointF(
                            center.x() + marker_radius,
                            center.y() - marker_radius,
                        ),
                    )
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
                            (
                                f"{reference.get('id', '')}"
                                f"::line:{source_line_index}"
                            )
                            in hovered_reference_ids
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
                if not self._external_point_marker_visible(
                    reference,
                    self._hovered_sketch_external_reference_id,
                    self._sketch_reference_snapping
                    or self._sketch_reference_selection_mode,
                ):
                    continue
                point_color = (
                    QColor("#FF7A00")
                    if reference_hovered
                    else QColor("#B34A3C")
                    if reference.get("broken")
                    else brown
                )
                painter.setPen(QPen(point_color, base_centerline_width))
                painter.setBrush(Qt.BrushStyle.NoBrush)
                point = geometry.get("point", (0.0, 0.0))
                if isinstance(point, (list, tuple)) and len(point) >= 2:
                    screen = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point(
                                (float(point[0]), float(point[1]))
                            )
                        )
                    )
                    marker_radius = 6.0 if reference.get("selected") else 5.0
                    painter.drawLine(
                        QPointF(
                            screen.x() - marker_radius,
                            screen.y() - marker_radius,
                        ),
                        QPointF(
                            screen.x() + marker_radius,
                            screen.y() + marker_radius,
                        ),
                    )
                    painter.drawLine(
                        QPointF(
                            screen.x() - marker_radius,
                            screen.y() + marker_radius,
                        ),
                        QPointF(
                            screen.x() + marker_radius,
                            screen.y() - marker_radius,
                        ),
                    )
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
                int,
            ]
        ] = []
        for first_id, first_geometry in sketch_geometry_by_id.items():
            records = first_geometry.get("corner_radii", ())
            if not isinstance(records, list):
                continue
            first_ids = tuple(map(str, first_geometry.get("point_ids", ())))
            if len(first_ids) != 2:
                continue
            for record_index, record in enumerate(records):
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
                        record_index,
                    )
                )
        equal_circle_markers: list[tuple[QPointF, str]] = []
        for entity in self._sketch_entities:
            entity_type = str(entity.get("type", ""))
            selected = (
                str(entity.get("id", ""))
                == self._selected_sketch_entity_id
                or str(entity.get("id", ""))
                == self._sketch_rectangle_axis_id
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
                or any(
                    reference_id in hovered_reference_ids
                    for reference_id in (
                        f"sketch_geometry:{entity.get('id', '')}",
                        f"sketch_circle:{entity.get('id', '')}",
                        f"sketch_arc:{entity.get('id', '')}",
                        f"sketch_ellipse:{entity.get('id', '')}",
                        f"sketch_elliptical_arc:{entity.get('id', '')}",
                    )
                )
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
            elif entity_type == "ellipse" and len(raw_points) >= 3:
                raw_points = ellipse_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                )
            elif entity_type == "elliptical_arc" and len(raw_points) >= 5:
                raw_points = elliptical_arc_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                    tuple(raw_points[3]),
                    tuple(raw_points[4]),
                    clockwise=bool(entity.get("clockwise", False)),
                )
            elif entity_type == "spline" and len(raw_points) >= 2:
                raw_points = _interpolated_spline_points(
                    tuple(
                        (float(point[0]), float(point[1]))
                        for point in raw_points
                        if isinstance(point, (list, tuple))
                        and len(point) >= 2
                    ),
                    stored_spline_tangent(entity, "start_tangent"),
                    stored_spline_tangent(entity, "end_tangent"),
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
                auxiliary_circle = (
                    entity.get("role") == "construction"
                )
                circle_pen = (
                    highlighted_auxiliary(cyan)
                    if auxiliary_circle and selected
                    else highlighted_auxiliary(QColor("#FF7A00"))
                    if auxiliary_circle and previewed
                    else auxiliary_line
                    if auxiliary_circle
                    else QPen(
                        cyan
                        if selected
                        else QColor("#FF7A00")
                        if previewed
                        else yellow,
                        3.0 if selected or previewed else 2.0,
                    )
                )
                painter.setPen(circle_pen)
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.drawPolyline(projected_circle)
                if (
                    entity.get("equal_radius_group")
                    and bool(entity.get("equal_radius_reference", False))
                    and projected_circle
                ):
                    marker = projected_circle[len(projected_circle) // 8]
                    equal_circle_markers.append((
                        QPointF(marker.x() + 6.0, marker.y() - 6.0),
                        str(entity.get("id", "")),
                    ))
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
                if entity.get("text_role") == "anchor":
                    font, transform, text_origin, _bounds = (
                        self._sketch_text_layout(entity)
                    )
                    painter.save()
                    painter.setWorldTransform(transform)
                    painter.setFont(font)
                    value = str(entity.get("text_value", ""))
                    text_color = (
                        cyan
                        if selected
                        else QColor("#FF7A00")
                        if previewed
                        else QColor(str(
                            load_drawing_style()["pens"]["GREEN"]["color"]
                        ))
                        if entity.get("text_color") == "green"
                        else QColor("#FFFFFF")
                    )
                    painter.setPen(QPen(text_color, 1.0))
                    painter.drawText(text_origin, value)
                    painter.restore()
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
        equal_radius_marker_points: list[tuple[QPointF, str, int]] = []
        for (
            raw_arc,
            first_id,
            second_id,
            vertex_id,
            has_equal_radius,
            radius_record_index,
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
                equal_radius_marker_points.append((
                    arc[len(arc) // 2],
                    first_id,
                    radius_record_index,
                ))
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
        constraint_markers: list[SketchConstraintMarker] = []

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
            key: tuple[str, int],
            constrained_point_ids: tuple[str, str],
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
            first_distance = hypot(
                intersection.x() - constrained_first.x(),
                intersection.y() - constrained_first.y(),
            )
            second_distance = hypot(
                intersection.x() - constrained_second.x(),
                intersection.y() - constrained_second.y(),
            )
            # Automatic perpendicular placement can meet the reference at
            # either endpoint of the new line. Anchor the square at that
            # actual contact, rather than assuming it is always endpoint 2.
            if min(first_distance, second_distance) <= 12.0:
                at_first = first_distance <= second_distance
                anchor = constrained_first if at_first else constrained_second
                anchor_point_id = constrained_point_ids[0 if at_first else 1]
                constrained_direction = normalized_screen_direction(
                    anchor,
                    constrained_second if at_first else constrained_first,
                )
                reference_ends = sorted(
                    (reference_first, reference_second),
                    key=lambda endpoint: hypot(
                        endpoint.x() - anchor.x(),
                        endpoint.y() - anchor.y(),
                    ),
                    reverse=True,
                )
                reference_direction = normalized_screen_direction(
                    anchor,
                    reference_ends[0],
                )
            else:
                # Disconnected perpendicular lines may intersect far outside
                # their finite spans. Keep their marker near the constrained
                # geometry, matching the previous readable fallback.
                anchor = constrained_second
                anchor_point_id = constrained_point_ids[1]
                constrained_direction = normalized_screen_direction(
                    constrained_second,
                    constrained_first,
                )
                reference_direction = normalized_screen_direction(
                    reference_first,
                    reference_second,
                )
            if constrained_direction is None or reference_direction is None:
                return
            ux, uy = constrained_direction
            vx, vy = reference_direction
            del ux, uy, vx, vy
            position = point_marker_position(anchor_point_id, anchor)
            constraint_markers.append(SketchConstraintMarker(
                "⊥", position, key[0], key[1]
            ))

        constraint_color = QColor("#7CFF6B")
        constraint_font = painter.font()
        constraint_font.setBold(True)
        painter.setFont(constraint_font)
        painter.setPen(QPen(constraint_color, 2.0))
        self._sketch_constraint_hit_regions = []
        markers_by_geometry: dict[str, list[tuple[str, int]]] = {
            geometry_id: [] for geometry_id in geometry_by_id
        }
        tangent_contact_ids: dict[str, tuple[str, int]] = {}
        midpoint_point_ids: dict[str, tuple[str, int]] = {}
        coincident_point_ids: dict[str, tuple[str, int]] = {}
        keypoint_point_ids: set[str] = set()
        direction_point_ids: dict[str, list[tuple[str, int]]] = {}
        symmetric_point_pairs: list[tuple[str, str, int]] = []

        def add_marker(geometry_id: str, marker: str, index: int) -> None:
            markers = markers_by_geometry.get(geometry_id)
            record = (marker, index)
            if markers is not None and record not in markers:
                markers.append(record)

        for geometry_id, geometry in geometry_by_id.items():
            constraints = geometry.get("constraints", ())
            if not isinstance(constraints, list):
                continue
            for constraint_index, constraint in enumerate(constraints):
                if not isinstance(constraint, dict):
                    continue
                constraint_type = str(constraint.get("type", ""))
                if constraint_type == "horizontal":
                    add_marker(
                        geometry_id,
                        "//" if constraint.get("display_as") == "parallel" else "H",
                        constraint_index,
                    )
                elif constraint_type == "vertical":
                    add_marker(
                        geometry_id,
                        "//" if constraint.get("display_as") == "parallel" else "V",
                        constraint_index,
                    )
                elif constraint_type == "parallel":
                    add_marker(geometry_id, "//", constraint_index)
                elif constraint_type == "equal_length":
                    # The owner is the driven child. The reference remains
                    # discoverable through selection, but does not own a
                    # duplicate equality marker.
                    add_marker(geometry_id, "=", constraint_index)
                elif constraint_type == "tangent":
                    contact_id = str(
                        constraint.get("contact_point_id", "")
                    )
                    if contact_id:
                        tangent_contact_ids[contact_id] = (
                            geometry_id,
                            constraint_index,
                        )

        for entity in self._sketch_entities:
            if entity.get("type") != "point":
                continue
            if isinstance(entity.get("curve_attachment"), dict):
                coincident_point_ids.setdefault(
                    str(entity.get("id", "")),
                    (str(entity.get("id", "")), -1),
                )
                if bool(entity["curve_attachment"].get("locked", False)):
                    keypoint_point_ids.add(str(entity.get("id", "")))
            if bool(entity.get("keypoint_constraint", False)):
                keypoint_point_ids.add(str(entity.get("id", "")))
            constraints = entity.get("constraints", ())
            if isinstance(constraints, list):
                for constraint_index, constraint in enumerate(constraints):
                    if (
                        isinstance(constraint, dict)
                        and constraint.get("type") in ("horizontal", "vertical")
                        and constraint.get("point_id") is not None
                    ):
                        direction_point_ids.setdefault(
                            str(entity.get("id", "")), []
                        ).append((
                            "H" if constraint.get("type") == "horizontal" else "V",
                            constraint_index,
                        ))
                if any(
                    isinstance(constraint, dict)
                    and constraint.get("type") == "midpoint"
                    for constraint in constraints
                ):
                    midpoint_index = next(
                        index for index, constraint in enumerate(constraints)
                        if isinstance(constraint, dict)
                        and constraint.get("type") == "midpoint"
                    )
                    midpoint_point_ids[str(entity.get("id", ""))] = (
                        str(entity.get("id", "")), midpoint_index
                    )
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
                    coincident_index = next(
                        index for index, constraint in enumerate(constraints)
                        if isinstance(constraint, dict)
                        and constraint.get("type") in (
                            "coincident", "point_on_line", "point_on_reference"
                        )
                    )
                    coincident_point_ids[str(entity.get("id", ""))] = (
                        str(entity.get("id", "")), coincident_index
                    )
                for constraint_index, constraint in enumerate(constraints):
                    if (
                        isinstance(constraint, dict)
                        and constraint.get("type") == "symmetric"
                    ):
                        second_id = str(constraint.get("point_id", ""))
                        if second_id:
                            symmetric_point_pairs.append(
                                (
                                    str(entity.get("id", "")),
                                    second_id,
                                    constraint_index,
                                )
                            )

        endpoint_use: dict[str, int] = {}
        for geometry in self._sketch_entities:
            geometry_type = str(geometry.get("type", ""))
            ids = tuple(map(str, geometry.get("point_ids", ())))
            endpoint_ids = (
                ids
                if geometry_type in ("segment", "construction")
                else ids[1:3]
                if geometry_type == "arc"
                else ()
            )
            for point_id in endpoint_ids:
                endpoint_use[point_id] = endpoint_use.get(point_id, 0) + 1
        for point_id, use_count in endpoint_use.items():
            if use_count >= 2:
                coincident_point_ids.setdefault(
                    point_id,
                    (point_id, -2),
                )

        metrics = painter.fontMetrics()
        marker_spacing = 16.0
        point_marker_slots: dict[tuple[int, int], int] = {}

        def point_marker_position(point_id: str, point: QPointF) -> QPointF:
            del point_id
            # Different model points can occupy the same screen position
            # (notably centers of concentric circles). Share their marker
            # slots so constraint labels do not paint over one another.
            position_key = (round(point.x()), round(point.y()))
            slot = point_marker_slots.get(position_key, 0)
            point_marker_slots[position_key] = slot + 1
            return QPointF(
                point.x() + 7.0 + slot * marker_spacing,
                point.y() - 7.0,
            )
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
            for marker_index, (label, constraint_index) in enumerate(markers):
                along = (
                    marker_index - (len(markers) - 1) * 0.5
                ) * marker_spacing
                position = QPointF(
                    center_x
                    + dx * along
                    - metrics.horizontalAdvance(label) * 0.5
                    - dy * 11.0,
                    center_y
                    + dy * along
                    + metrics.ascent() * 0.5
                    + dx * 11.0,
                )
                constraint_markers.append(SketchConstraintMarker(
                    label, position, geometry_id, constraint_index
                ))

        for contact_id, key in tangent_contact_ids.items():
            local_contact = point_positions.get(contact_id)
            if local_contact is None:
                continue
            contact = self._screen_point(
                self._camera_point(self._sketch_world_point(local_contact))
            )
            position = point_marker_position(contact_id, contact)
            constraint_markers.append(SketchConstraintMarker(
                "T", position, key[0], key[1]
            ))

        for point_id, key in midpoint_point_ids.items():
            local_midpoint = point_positions.get(point_id)
            if local_midpoint is None:
                continue
            midpoint = self._screen_point(
                self._camera_point(self._sketch_world_point(local_midpoint))
            )
            position = point_marker_position(point_id, midpoint)
            constraint_markers.append(SketchConstraintMarker(
                "M", position, key[0], key[1]
            ))

        for point_id, records in direction_point_ids.items():
            local_point = point_positions.get(point_id)
            if local_point is None:
                continue
            point = self._screen_point(
                self._camera_point(self._sketch_world_point(local_point))
            )
            for label, constraint_index in records:
                constraint_markers.append(SketchConstraintMarker(
                    label,
                    point_marker_position(point_id, point),
                    point_id,
                    constraint_index,
                ))

        for point_id, key in coincident_point_ids.items():
            local_point = point_positions.get(point_id)
            if local_point is None:
                continue
            point = self._screen_point(
                self._camera_point(self._sketch_world_point(local_point))
            )
            position = point_marker_position(point_id, point)
            constraint_markers.append(SketchConstraintMarker(
                "C", position, key[0], key[1]
            ))

        for point_id in keypoint_point_ids:
            local_point = point_positions.get(point_id)
            if local_point is None:
                continue
            point = self._screen_point(
                self._camera_point(self._sketch_world_point(local_point))
            )
            constraint_markers.append(SketchConstraintMarker(
                "K",
                point_marker_position(point_id, point),
                "",
                -1,
                False,
            ))

        for spline in self._sketch_entities:
            if spline.get("type") != "spline" or not bool(
                spline.get("closed_smooth", False)
            ):
                continue
            ids = tuple(map(str, spline.get("point_ids", ())))
            local_point = point_positions.get(ids[0]) if ids else None
            if local_point is None:
                continue
            point = self._screen_point(
                self._camera_point(self._sketch_world_point(local_point))
            )
            for label in ("C", "T"):
                constraint_markers.append(SketchConstraintMarker(
                    label,
                    point_marker_position(ids[0], point),
                    "",
                    -1,
                    False,
                ))

        for first_id, second_id, constraint_index in symmetric_point_pairs:
            for point_id in (first_id, second_id):
                local_point = point_positions.get(point_id)
                if local_point is None:
                    continue
                point = self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(local_point)
                    )
                )
                constraint_markers.append(SketchConstraintMarker(
                    "S",
                    point_marker_position(point_id, point),
                    first_id,
                    constraint_index,
                ))

        for position, circle_id in equal_circle_markers:
            key = (circle_id, -3)
            constraint_markers.append(SketchConstraintMarker(
                "=", position, key[0], key[1]
            ))

        for arc_point, owner_id, record_index in equal_radius_marker_points:
            constraint_markers.append(SketchConstraintMarker(
                "=",
                QPointF(arc_point.x() + 7.0, arc_point.y() - 7.0),
                owner_id,
                -1000 - record_index,
            ))

        painter.setPen(QPen(constraint_color, 2.0))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for geometry_id, geometry in geometry_by_id.items():
            constraints = geometry.get("constraints", ())
            if not isinstance(constraints, list):
                continue
            constrained_point_ids = tuple(
                map(str, geometry.get("point_ids", ()))
            )
            if len(constrained_point_ids) != 2:
                continue
            constrained_line = geometry_screen_line(geometry)
            if constrained_line is None:
                continue
            for constraint_index, constraint in enumerate(constraints):
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
                        (geometry_id, constraint_index),
                        constrained_point_ids,
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
                                (geometry_id, constraint_index),
                                constrained_point_ids,
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
                        (geometry_id, constraint_index),
                        constrained_point_ids,
                    )

        # Every persistent constraint symbol uses this single style and
        # interaction path. Geometry-specific code above only computes its
        # anchor position and identity.
        for marker in constraint_markers:
            key = (marker.owner_id, marker.constraint_index)
            painter.setPen(QPen(
                cyan if key == self._selected_sketch_constraint
                else QColor("#FF7A00")
                if key == self._hovered_sketch_constraint
                else constraint_color,
                2.0,
            ))
            painter.drawText(marker.position, marker.label)
            if marker.selectable and marker.owner_id:
                self._sketch_constraint_hit_regions.append((
                    QRectF(
                        marker.position.x(),
                        marker.position.y() - metrics.ascent(),
                        float(metrics.horizontalAdvance(marker.label)),
                        float(metrics.height()),
                    ),
                    marker.owner_id,
                    marker.constraint_index,
                ))

        if (
            not self._sketch_pending_points
            and self._sketch_preview_position is not None
            and self._sketch_tool not in ("select", "dimension")
        ):
            point_ids = {
                str(entity.get("id", ""))
                for entity in self._sketch_entities
                if entity.get("type") == "point"
            }
            preview = self._screen_point(
                self._camera_point(
                    self._sketch_world_point(self._sketch_preview_position)
                )
            )
            label = (
                "C  C"
                if self._sketch_preview_constraint == "intersection"
                else "M"
                if self._sketch_preview_constraint is not None
                and self._sketch_preview_constraint.startswith("midpoint:")
                else "K"
                if self._sketch_preview_is_keypoint
                else "X"
                if self._sketch_preview_constraint == "axis:x"
                else "Y"
                if self._sketch_preview_constraint == "axis:y"
                else "C"
                if (
                    self._hovered_sketch_external_reference_id is not None
                    or self._preview_sketch_entity_id in point_ids
                )
                else ""
            )
            if label:
                orange = QColor("#FF7A00")
                painter.setPen(QPen(orange, 2.0))
                painter.setBrush(QBrush(orange))
                painter.drawEllipse(preview, 5.0, 5.0)
                painter.drawText(QPointF(preview.x() + 8.0, preview.y() - 8.0), label)

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
            if (
                self._sketch_tool in ("polyline", "polyline_arc")
                and len(self._sketch_pending_points) == 1
            ):
                chain_start = self._sketch_pending_points[0]
                point_positions = {
                    str(entity.get("id", "")): (
                        float(entity.get("x", 0.0)),
                        float(entity.get("y", 0.0)),
                    )
                    for entity in self._sketch_entities
                    if entity.get("type") == "point"
                }
                previous_geometry = next((
                    entity for entity in reversed(self._sketch_entities)
                    if entity.get("type") in ("segment", "arc")
                    and entity.get("point_ids")
                    and str(entity.get("point_ids")[-1]) in point_positions
                    and hypot(
                        point_positions[str(entity.get("point_ids")[-1])][0]
                        - chain_start[0],
                        point_positions[str(entity.get("point_ids")[-1])][1]
                        - chain_start[1],
                    ) <= 1.0e-9
                ), None)
                if previous_geometry is not None:
                    contact = pending[0]
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.drawText(
                        QPointF(contact.x() + 7.0, contact.y() - 7.0),
                        "C",
                    )
                    if (
                        self._sketch_tool == "polyline_arc"
                        or previous_geometry.get("type") == "arc"
                    ):
                        painter.drawText(
                            QPointF(contact.x() + 21.0, contact.y() - 7.0),
                            "T",
                        )
            if (
                len(pending) >= 2
                and self._sketch_tool not in ("ellipse", "elliptical_arc")
            ):
                if self._sketch_tool == "spline":
                    start_tangent = self._pending_spline_start_tangent()
                    sampled = (
                        sample_tangent_start_arc(
                            self._sketch_pending_points[0],
                            self._sketch_pending_points[1],
                            start_tangent,
                        )
                        if len(self._sketch_pending_points) == 2
                        and start_tangent is not None
                        else _interpolated_spline_points(
                            self._sketch_pending_points,
                            start_tangent,
                        )
                    )
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(Qt.BrushStyle.NoBrush)
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
                if self._sketch_tool == "polyline_arc":
                    start = self._sketch_pending_points[-1]
                    direction = self._polyline_arc_start_direction(start)
                    length = hypot(*(direction or (0.0, 0.0)))
                    if direction is not None and length > 1.0e-12:
                        tx, ty = direction[0] / length, direction[1] / length
                        nx, ny = -ty, tx
                        dx = self._sketch_preview_position[0] - start[0]
                        dy = self._sketch_preview_position[1] - start[1]
                        denominator = 2.0 * (dx * nx + dy * ny)
                        if abs(denominator) > 1.0e-9:
                            signed_radius = (dx * dx + dy * dy) / denominator
                            center = (
                                start[0] + nx * signed_radius,
                                start[1] + ny * signed_radius,
                            )
                            center_screen = self._screen_point(
                                self._camera_point(self._sketch_world_point(center))
                            )
                            painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                            painter.setBrush(QBrush(QColor("#FF7A00")))
                            painter.drawEllipse(center_screen, 4.5, 4.5)
                            painter.drawText(
                                QPointF(center_screen.x() + 7.0,
                                        center_screen.y() - 7.0),
                                "C",
                            )
                            sampled = center_arc_points(
                                center,
                                start,
                                self._sketch_preview_position,
                                clockwise=signed_radius < 0.0,
                            )
                            painter.setBrush(Qt.BrushStyle.NoBrush)
                            painter.drawPolyline(QPolygonF([
                                self._screen_point(self._camera_point(
                                    self._sketch_world_point(point)
                                ))
                                for point in sampled
                            ]))
                elif self._sketch_tool == "circle":
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
                    center = self._sketch_pending_points[0]
                    start = self._sketch_pending_points[1]
                    sampled = center_arc_points(
                        center,
                        start,
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
                elif (
                    self._sketch_tool in ("ellipse", "elliptical_arc")
                    and len(pending) == 2
                ):
                    center, major = self._sketch_pending_points
                    ax = major[0] - center[0]
                    ay = major[1] - center[1]
                    axis_length = hypot(ax, ay)
                    if axis_length > 1.0e-12:
                        nx, ny = -ay / axis_length, ax / axis_length
                        signed = (
                            (self._sketch_preview_position[0] - center[0]) * nx
                            + (self._sketch_preview_position[1] - center[1]) * ny
                        )
                        minor = (
                            center[0] + nx * signed,
                            center[1] + ny * signed,
                        )
                        sampled = ellipse_points(center, major, minor)
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        painter.drawPolyline(QPolygonF([
                            self._screen_point(self._camera_point(
                                self._sketch_world_point(point)
                            ))
                            for point in sampled
                        ]))
                elif (
                    self._sketch_tool == "elliptical_arc"
                    and len(pending) >= 3
                ):
                    center, major, minor = self._sketch_pending_points[:3]
                    if len(pending) == 3:
                        # Keep the complete ellipse only while selecting the
                        # first arc endpoint; it is useful as a placement
                        # guide at this stage.
                        sampled = ellipse_points(center, major, minor)
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        painter.drawPolyline(QPolygonF([
                            self._screen_point(self._camera_point(
                                self._sketch_world_point(point)
                            ))
                            for point in sampled
                        ]))
                    else:
                        sampled = elliptical_arc_points(
                            center,
                            major,
                            minor,
                            self._sketch_pending_points[3],
                            self._sketch_preview_position,
                            clockwise=bool(self._sketch_arc_clockwise),
                        )
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        painter.drawPolyline(QPolygonF([
                            self._screen_point(self._camera_point(
                                self._sketch_world_point(point)
                            ))
                            for point in sampled
                        ]))
                    # The fourth/fifth click selects a point on the ellipse,
                    # not a straight helper segment. Keep that candidate as
                    # the standard yellow sketch point.
                    painter.setPen(QPen(QColor("#FFD740"), 2.0))
                    painter.setBrush(QBrush(QColor("#FFD740")))
                    painter.drawEllipse(preview, 4.0, 4.0)
                elif self._sketch_tool == "spline":
                    preview_points = self._sketch_pending_points + (
                        self._sketch_preview_position,
                    )
                    start_tangent = self._pending_spline_start_tangent(
                        self._sketch_preview_position
                    )
                    sampled = (
                        sample_tangent_start_arc(
                            preview_points[0],
                            preview_points[1],
                            start_tangent,
                        )
                        if len(preview_points) == 2
                        and start_tangent is not None
                        else _interpolated_spline_points(
                            preview_points,
                            start_tangent,
                            self._pending_spline_end_tangent(
                                self._sketch_preview_position
                            ),
                        )
                    )
                    spline = QPolygonF([
                        self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(point)
                            )
                        )
                        for point in sampled
                    ])
                    # Do not inherit a transparent/helper pen from another
                    # preview branch. The active spline must stay visible
                    # after every added interpolation point.
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawPolyline(spline)
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawEllipse(preview, 4.5, 4.5)
                elif self._sketch_tool == "rectangle":
                    first_local = self._sketch_pending_points[0]
                    opposite_local = self._sketch_preview_position
                    local_corners = None
                    compound = self._sketch_preview_constraint or ""
                    if compound.startswith("rectangle_oriented:"):
                        axis_id = compound.split(":", 1)[1]
                        point_map = {
                            str(entity.get("id", "")): (
                                float(entity.get("x", 0.0)),
                                float(entity.get("y", 0.0)),
                            )
                            for entity in self._sketch_entities
                            if entity.get("type") == "point"
                        }
                        axis = next((
                            entity for entity in self._sketch_entities
                            if entity.get("type") == "construction"
                            and str(entity.get("id", "")) == axis_id
                        ), None)
                        ids = (
                            tuple(map(str, axis.get("point_ids", ())))
                            if axis is not None else ()
                        )
                        if len(ids) == 2 and all(pid in point_map for pid in ids):
                            axis_a, axis_b = point_map[ids[0]], point_map[ids[1]]
                            dx, dy = axis_b[0] - axis_a[0], axis_b[1] - axis_a[1]
                            axis_length = hypot(dx, dy)
                            if axis_length > 1.0e-12:
                                ux, uy = dx / axis_length, dy / axis_length
                                projection = (
                                    (first_local[0] - axis_a[0]) * ux
                                    + (first_local[1] - axis_a[1]) * uy
                                )
                                foot = (
                                    axis_a[0] + projection * ux,
                                    axis_a[1] + projection * uy,
                                )
                                mirrored = (
                                    2.0 * foot[0] - first_local[0],
                                    2.0 * foot[1] - first_local[1],
                                )
                                length = (
                                    (opposite_local[0] - first_local[0]) * ux
                                    + (opposite_local[1] - first_local[1]) * uy
                                )
                                far_first = (
                                    first_local[0] + length * ux,
                                    first_local[1] + length * uy,
                                )
                                far_mirrored = (
                                    mirrored[0] + length * ux,
                                    mirrored[1] + length * uy,
                                )
                                local_corners = (
                                    first_local, far_first, far_mirrored,
                                    mirrored, first_local,
                                )
                    if local_corners is None:
                        local_corners = (
                            first_local,
                            (opposite_local[0], first_local[1]),
                            opposite_local,
                            (first_local[0], opposite_local[1]),
                            first_local,
                        )
                    rectangle = QPolygonF([
                        self._screen_point(
                            self._camera_point(self._sketch_world_point(point))
                        )
                        for point in local_corners
                    ])
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawPolyline(rectangle)
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawEllipse(rectangle[1], 4.5, 4.5)
                    painter.drawEllipse(rectangle[3], 4.5, 4.5)
                    if compound.startswith("rectangle_corner:"):
                        try:
                            corner_index = int(compound.split(":", 2)[1])
                            marker = rectangle[corner_index]
                            painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                            painter.setBrush(QBrush(QColor("#FF7A00")))
                            painter.drawEllipse(marker, 5.0, 5.0)
                            painter.drawText(
                                QPointF(marker.x() + 8.0, marker.y() - 8.0),
                                "C",
                            )
                        except (IndexError, TypeError, ValueError):
                            pass
                    elif compound.startswith("rectangle_oriented:"):
                        painter.setPen(QPen(QColor("#FF7A00"), 1.5))
                        painter.drawText(
                            QPointF(rectangle[3].x() + 8.0, rectangle[3].y() - 8.0),
                            "S  ∥",
                        )
                elif self._sketch_tool == "hexagon":
                    center = self._sketch_pending_points[0]
                    radius = hypot(
                        self._sketch_preview_position[0] - center[0],
                        self._sketch_preview_position[1] - center[1],
                    )
                    local_vertices = regular_polygon_vertices(
                        center,
                        self._sketch_preview_position,
                        self._sketch_polygon_sides,
                    )
                    if local_vertices:
                        polygon = QPolygonF([
                            self._screen_point(
                                self._camera_point(self._sketch_world_point(point))
                            )
                            for point in (*local_vertices, local_vertices[0])
                        ])
                        circle = QPolygonF([
                            self._screen_point(
                                self._camera_point(self._sketch_world_point((
                                    center[0] + radius * cos(angle),
                                    center[1] + radius * sin(angle),
                                )))
                            )
                            for angle in (
                                2.0 * pi * i / 96.0 for i in range(97)
                            )
                        ])
                        painter.setPen(highlighted_auxiliary(QColor("#FF7A00")))
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        painter.drawPolyline(circle)
                        painter.setPen(QPen(QColor("#FF7A00"), 1.5))
                        painter.drawPolyline(polygon)
                elif self._sketch_tool == "text":
                    # Sketch text owns one anchor point.  Rotation is picked
                    # explicitly from its properties panel; the ordinary
                    # cursor preview must never suggest a second line point.
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawEllipse(pending[-1], 4.0, 4.0)
                elif self._sketch_tool == "construction":
                    painter.setPen(highlighted_centerline(QColor("#FF7A00")))
                    infinite_line(pending[-1], preview)
                else:
                    painter.drawLine(pending[-1], preview)
                preview_labels: list[str] = []
                if (
                    self._sketch_tool == "spline"
                    and self._sketch_pending_points
                    and (
                        self._pending_spline_end_tangent(
                            self._sketch_preview_position
                        ) is not None
                        or (
                            len(self._sketch_pending_points) >= 3
                            and self._sketch_preview_position is not None
                            and hypot(
                                self._sketch_preview_position[0]
                                - self._sketch_pending_points[0][0],
                                self._sketch_preview_position[1]
                                - self._sketch_pending_points[0][1],
                            ) <= 1.0e-9
                        )
                    )
                ):
                    preview_labels.extend(("C", "T"))
                elif self._sketch_preview_constraint == "horizontal":
                    preview_labels.append("H")
                elif self._sketch_preview_constraint == "vertical":
                    preview_labels.append("V")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        ("symmetric_point:", "rectangle_symmetric:", "rectangle_oriented:")
                    )
                ):
                    preview_labels.append("S")
                    if self._sketch_preview_constraint.endswith(
                        ":horizontal"
                    ):
                        preview_labels.append("H")
                    elif self._sketch_preview_constraint.endswith(
                        ":vertical"
                    ):
                        preview_labels.append("V")
                    else:
                        preview_labels.append("⊥")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith("parallel:")
                ):
                    preview_labels.append("//")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        "perpendicular:"
                    )
                ):
                    pass
                elif self._sketch_preview_constraint == "axis:x":
                    preview_labels.append("X")
                elif self._sketch_preview_constraint == "axis:y":
                    preview_labels.append("Y")
                elif (
                    self._sketch_preview_is_keypoint
                ):
                    preview_labels.append("K")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith("midpoint:")
                ):
                    preview_labels.append("M")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        "tangent_both:"
                    )
                ):
                    preview_labels.extend(("T", "C", "T"))
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        ("circle_tangent:", "circle_curve_tangent:")
                    )
                ):
                    preview_labels.append("C")
                elif (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        (
                            "equal_length:", "equal_radius:",
                            "equal_corner_radius:",
                            "equal_arc_radius:",
                        )
                    )
                ):
                    preview_labels.append("=")
                    if self._sketch_preview_constraint.endswith(
                        ":horizontal"
                    ):
                        preview_labels.append("H")
                    elif self._sketch_preview_constraint.endswith(
                        ":vertical"
                    ):
                        preview_labels.append("V")
                elif self._sketch_preview_constraint == "intersection":
                    preview_labels.extend(("C", "C"))
                point_ids = {
                    str(entity.get("id", ""))
                    for entity in self._sketch_entities
                    if entity.get("type") == "point"
                }
                if (
                    (
                        self._hovered_sketch_external_reference_id is not None
                        and self._sketch_preview_constraint != "intersection"
                        and not (
                            self._sketch_preview_constraint is not None
                            and self._sketch_preview_constraint.startswith(
                                (
                                    "midpoint:",
                                    "circle_tangent:",
                                    "circle_curve_tangent:",
                                )
                            )
                        )
                    )
                    or self._preview_sketch_entity_id in point_ids
                ):
                    preview_labels.append("C")
                point_labels = [
                    label
                    for label in preview_labels
                    if label in ("C", "M", "K", "X", "Y")
                ]
                preview_labels = [
                    label
                    for label in preview_labels
                    if label not in ("C", "M", "K", "X", "Y")
                ]
                if (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        "equal_length:"
                    )
                    and "=" in preview_labels
                ):
                    # Equality belongs to the line, while a combined H/V
                    # belongs to its active second point.
                    preview_labels.remove("=")
                    equal_metrics = painter.fontMetrics()
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.drawText(
                        QPointF(
                            (pending[-1].x() + preview.x()) * 0.5
                            - equal_metrics.horizontalAdvance("=") * 0.5,
                            (pending[-1].y() + preview.y()) * 0.5 - 9.0,
                        ),
                        "=",
                    )
                point_label_text = "  ".join(point_labels)
                point_label_width = (
                    painter.fontMetrics().horizontalAdvance(point_label_text)
                    if point_label_text
                    else 0
                )
                if point_labels:
                    tangent_at_preview = (
                        self._sketch_preview_constraint is not None
                        and self._sketch_preview_constraint.startswith(
                            (
                                "tangent:",
                                "circle_tangent:",
                                "circle_curve_tangent:",
                            )
                        )
                    )
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawEllipse(preview, 5.0, 5.0)
                    painter.drawText(
                        QPointF(
                            preview.x() + (23.0 if tangent_at_preview else 7.0),
                            preview.y() - 9.0,
                        ),
                        point_label_text,
                    )
                if (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        "perpendicular:"
                    )
                ):
                    # This inference means that the new line leaves its
                    # supporting geometry normally. Its contact is therefore
                    # always the already confirmed first point; snapping the
                    # cursor end elsewhere must not move the relation marker.
                    perpendicular_contact = pending[-1]
                    contact_point_id = next(
                        (
                            point_id
                            for point_id, local_point
                            in point_positions.items()
                            if hypot(
                                local_point[0]
                                - self._sketch_pending_points[-1][0],
                                local_point[1]
                                - self._sketch_pending_points[-1][1],
                            )
                            <= 1.0e-9
                        ),
                        None,
                    )
                    perpendicular_slot = (
                        point_marker_slots.get(contact_point_id, 0)
                        if contact_point_id is not None
                        else len(point_labels)
                    )
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.drawText(
                        QPointF(
                            perpendicular_contact.x()
                            + 7.0
                            + perpendicular_slot * marker_spacing,
                            perpendicular_contact.y() - 9.0,
                        ),
                        "⊥",
                    )
                if preview_labels:
                    label_position = (
                        QPointF(
                            preview.x()
                            + 9.0
                            + (point_label_width + 8.0 if point_labels else 0.0),
                            preview.y() - 9.0,
                        )
                        if self._sketch_preview_constraint
                        in ("horizontal", "vertical")
                        or (
                            self._sketch_preview_constraint is not None
                            and self._sketch_preview_constraint.startswith(
                                "equal_length:"
                            )
                            and self._sketch_preview_constraint.endswith(
                                (":horizontal", ":vertical")
                            )
                        )
                        or (
                            self._sketch_preview_constraint is not None
                            and self._sketch_preview_constraint.startswith(
                                "symmetric_point:"
                            )
                        )
                        or self._sketch_tool in (
                            "circle", "arc", "ellipse", "elliptical_arc",
                            "hexagon",
                        )
                        else QPointF(
                            (pending[-1].x() + preview.x()) * 0.5 + 6.0,
                            (pending[-1].y() + preview.y()) * 0.5 - 9.0,
                        )
                    )
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawText(
                        label_position,
                        "  ".join(preview_labels),
                    )
                if (
                    self._sketch_preview_constraint is not None
                    and self._sketch_preview_constraint.startswith(
                        (
                            "tangent:",
                            "tangent_first:",
                            "circle_tangent:",
                            "circle_curve_tangent:",
                        )
                    )
                ):
                    # Match the marker used by a committed tangent constraint.
                    # For tangent_first the contact is the first endpoint;
                    # for a tangent drawn towards a curve it is the preview end.
                    tangent_contact = (
                        pending[-1]
                        if self._sketch_preview_constraint.startswith(
                            "tangent_first:"
                        )
                        else preview
                    )
                    painter.setPen(QPen(QColor("#FF7A00"), 2.0))
                    painter.setBrush(QBrush(QColor("#FF7A00")))
                    painter.drawEllipse(tangent_contact, 5.0, 5.0)
                    tangent_label = (
                        "C  T"
                        if self._sketch_preview_constraint.startswith(
                            "tangent_first:"
                        )
                        else "T"
                    )
                    painter.drawText(
                        QPointF(
                            tangent_contact.x() + 7.0,
                            tangent_contact.y() - 9.0,
                        ),
                        tangent_label,
                    )
        # Repaint a hovered finite external point last.  A coincident sketch
        # point, constraint marker or main axis may otherwise cover the
        # reference marker even though hit testing selected it correctly.
        hovered_reference_id = self._hovered_sketch_external_reference_id
        if hovered_reference_id is not None:
            hovered_reference = next(
                (
                    reference
                    for reference in self._sketch_external_references
                    if str(reference.get("id", ""))
                    == hovered_reference_id
                ),
                None,
            )
            hovered_geometry = (
                hovered_reference.get("geometry")
                if isinstance(hovered_reference, dict)
                else None
            )
            if (
                isinstance(hovered_geometry, dict)
                and hovered_geometry.get("type") in ("point", "axis_point")
            ):
                hovered_point = hovered_geometry.get("point", ())
                if (
                    isinstance(hovered_point, (list, tuple))
                    and len(hovered_point) >= 2
                ):
                    screen = self._screen_point(
                        self._camera_point(
                            self._sketch_world_point((
                                float(hovered_point[0]),
                                float(hovered_point[1]),
                            ))
                        )
                    )
                    orange = QColor("#FF7A00")
                    painter.setPen(QPen(orange, 2.0))
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    marker_radius = 6.0
                    painter.drawLine(
                        QPointF(
                            screen.x() - marker_radius,
                            screen.y() - marker_radius,
                        ),
                        QPointF(
                            screen.x() + marker_radius,
                            screen.y() + marker_radius,
                        ),
                    )
                    painter.drawLine(
                        QPointF(
                            screen.x() - marker_radius,
                            screen.y() + marker_radius,
                        ),
                        QPointF(
                            screen.x() + marker_radius,
                            screen.y() - marker_radius,
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
            elif entity_type == "ellipse" and len(raw_points) >= 3:
                raw_points = ellipse_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                )
            elif entity_type == "elliptical_arc" and len(raw_points) >= 5:
                raw_points = elliptical_arc_points(
                    tuple(raw_points[0]),
                    tuple(raw_points[1]),
                    tuple(raw_points[2]),
                    tuple(raw_points[3]),
                    tuple(raw_points[4]),
                    clockwise=bool(entity.get("clockwise", False)),
                )
            elif entity_type == "spline" and len(raw_points) >= 2:
                raw_points = _interpolated_spline_points(
                    tuple(
                        (float(point[0]), float(point[1]))
                        for point in raw_points
                        if isinstance(point, (list, tuple))
                        and len(point) >= 2
                    ),
                    stored_spline_tangent(entity, "start_tangent"),
                    stored_spline_tangent(entity, "end_tangent"),
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
        text_group = self._sketch_text_candidate(position)
        if text_group:
            for order, entity in enumerate(self._sketch_entities):
                if (
                    entity.get("type") == "point"
                    and entity.get("text_role") == "anchor"
                    and entity.get("text_group") == text_group
                ):
                    candidates.append((-1, 0.0, order, str(entity.get("id", ""))))
                    break
        for distance, vertex_id in self._corner_radius_handle_candidates(
            position
        ):
            candidates.append((0, distance, -1, vertex_id))
        candidates.sort()
        return tuple(candidate[3] for candidate in candidates)

    def _sketch_text_layout(
        self,
        entity: dict[str, Any],
    ) -> tuple[QFont, QTransform, QPointF, QRectF]:
        anchor = (float(entity.get("x", 0.0)), float(entity.get("y", 0.0)))
        screen = self._screen_point(
            self._camera_point(self._sketch_world_point(anchor))
        )
        font = QFont("osifont")
        font.setPixelSize(1000)
        metrics = QFontMetrics(font)
        value = str(entity.get("text_value", ""))
        ink = QRectF(metrics.tightBoundingRect(value))
        ink_height = max(1.0, ink.height())
        world_scale = max(float(entity.get("text_height", 10.0)), 0.01) / ink_height
        angle = radians(float(entity.get("text_angle", 0.0)))
        x_sign = -1.0 if bool(entity.get("text_flip", False)) else 1.0
        x_local = (
            x_sign * cos(angle) * world_scale,
            x_sign * sin(angle) * world_scale,
        )
        # Font coordinates grow downwards. Map that direction to negative
        # sketch Y after applying the requested in-plane rotation.
        y_local = (sin(angle) * world_scale, -cos(angle) * world_scale)
        x_screen = self._screen_point(self._camera_point(
            self._sketch_world_point((anchor[0] + x_local[0], anchor[1] + x_local[1]))
        ))
        y_screen = self._screen_point(self._camera_point(
            self._sketch_world_point((anchor[0] + y_local[0], anchor[1] + y_local[1]))
        ))
        transform = QTransform(
            x_screen.x() - screen.x(),
            x_screen.y() - screen.y(),
            y_screen.x() - screen.x(),
            y_screen.y() - screen.y(),
            screen.x(),
            screen.y(),
        )
        x_offset = {
            "left": -ink.left(),
            "center": -(ink.left() + ink.right()) * 0.5,
            "right": -ink.right(),
        }.get(str(entity.get("text_horizontal", "left")), -ink.left())
        y_offset = {
            "bottom": -ink.bottom(),
            "middle": -(ink.top() + ink.bottom()) * 0.5,
            "top": -ink.top(),
        }.get(str(entity.get("text_vertical", "bottom")), -ink.bottom())
        origin = QPointF(x_offset, y_offset)
        return font, transform, origin, ink.translated(origin)

    def _sketch_text_candidate(self, position: QPointF) -> str | None:
        for entity in reversed(self._sketch_entities):
            if entity.get("type") != "point" or entity.get("text_role") != "anchor":
                continue
            _font, transform, _origin, bounds = self._sketch_text_layout(entity)
            screen_scale = max(
                hypot(transform.m11(), transform.m12()),
                hypot(transform.m21(), transform.m22()),
                1.0e-6,
            )
            padding = 5.0 / screen_scale
            bounds = bounds.adjusted(-padding, -padding, padding, padding)
            polygon = transform.map(QPolygonF([
                bounds.topLeft(), bounds.topRight(),
                bounds.bottomRight(), bounds.bottomLeft(),
            ]))
            if polygon.containsPoint(position, Qt.FillRule.WindingFill):
                return str(entity.get("text_group", ""))
        return None

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

    def _smart_sketch_placement_candidates(
        self,
        position: QPointF,
    ) -> tuple[tuple[tuple[float, float], str | None, str | None], ...]:
        base = self._base_sketch_placement_candidate(position)
        if (
            base[1] == "sketch_axis:x||sketch_axis:y"
            and base[2] == "intersection"
        ):
            return (base,)
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        if (
            self._sketch_tool == "rectangle"
            and self._sketch_rectangle_axis_id is not None
        ):
            return ((
                base[0],
                None,
                f"rectangle_oriented:{self._sketch_rectangle_axis_id}",
            ),)
        ranked: list[
            tuple[float, int, tuple[tuple[float, float], str | None, str | None]]
        ] = []

        def offer(
            point: tuple[float, float],
            reference_id: str,
            constraint: str | None,
            priority: int,
        ) -> None:
            screen = self._screen_point(
                self._camera_point(self._sketch_world_point(point))
            )
            distance = hypot(position.x() - screen.x(), position.y() - screen.y())
            if distance <= 16.0:
                ranked.append((distance, priority, (point, reference_id, constraint)))

        # The two generated rectangle corners are not under the cursor. Let
        # either of them snap to a circle keypoint and adjust the matching
        # coordinate of the opposite corner.
        if self._sketch_tool == "rectangle" and len(self._sketch_pending_points) == 1:
            first = self._sketch_pending_points[0]
            local = self._sketch_local_position(position)
            if local is not None:
                for curve in self._sketch_entities:
                    if curve.get("type") != "circle":
                        continue
                    curve_id = str(curve.get("id", ""))
                    ids = tuple(map(str, curve.get("point_ids", ())))
                    if not curve_id or not ids or ids[0] not in points:
                        continue
                    center = points[ids[0]]
                    radius = float(curve.get("radius", 0.0))
                    if radius <= 1.0e-12:
                        continue
                    for angle, keypoint in (
                        (0, (center[0] + radius, center[1])),
                        (90, (center[0], center[1] + radius)),
                        (180, (center[0] - radius, center[1])),
                        (270, (center[0], center[1] - radius)),
                    ):
                        for corner_index, active, opposite in (
                            (1, (local[0], first[1]), (keypoint[0], local[1])),
                            (3, (first[0], local[1]), (local[0], keypoint[1])),
                        ):
                            active_screen = self._screen_point(
                                self._camera_point(self._sketch_world_point(active))
                            )
                            keypoint_screen = self._screen_point(
                                self._camera_point(self._sketch_world_point(keypoint))
                            )
                            distance = hypot(
                                active_screen.x() - keypoint_screen.x(),
                                active_screen.y() - keypoint_screen.y(),
                            )
                            if distance <= 16.0:
                                ranked.append((
                                    distance,
                                    0,
                                    (
                                        opposite,
                                        None,
                                        f"rectangle_corner:{corner_index}:{curve_id}:{angle}",
                                    ),
                                ))

        for (
            guide_distance,
            tangent_point,
            curve_reference_id,
            tangent_constraint,
        ) in self._sketch_tangent_placement_candidates(position):
            ranked.append((
                guide_distance,
                -1 if tangent_constraint.startswith("tangent_both:") else 0,
                (tangent_point, curve_reference_id, tangent_constraint),
            ))
        for guide_distance, guide_point, geometry_id in (
            self._sketch_perpendicular_placement_candidates(position)
        ):
            ranked.append((
                guide_distance,
                1,
                (guide_point, None, f"perpendicular:{geometry_id}"),
            ))

        if (
            self._sketch_tool in ("segment", "construction", "polyline")
            and self._sketch_pending_points
        ):
            start = self._sketch_pending_points[-1]
            local = self._sketch_local_position(position)
            if local is not None:
                cursor_dx = local[0] - start[0]
                cursor_dy = local[1] - start[1]
                cursor_length = hypot(cursor_dx, cursor_dy)
                if cursor_length > 1.0e-12:
                    ux = cursor_dx / cursor_length
                    uy = cursor_dy / cursor_length
                    direction_constraint = (
                        self._sketch_inferred_direction_constraint(local)
                    )
                    if direction_constraint == "horizontal":
                        ux = 1.0 if cursor_dx >= 0.0 else -1.0
                        uy = 0.0
                    elif direction_constraint == "vertical":
                        ux = 0.0
                        uy = 1.0 if cursor_dy >= 0.0 else -1.0
                    for geometry in self._sketch_entities:
                        if geometry.get("type") not in (
                            "segment", "construction",
                        ):
                            continue
                        geometry_id = str(geometry.get("id", ""))
                        ids = tuple(map(str, geometry.get("point_ids", ())))
                        if (
                            not geometry_id
                            or len(ids) != 2
                            or any(point_id not in points for point_id in ids)
                        ):
                            continue
                        reference_length = hypot(
                            points[ids[1]][0] - points[ids[0]][0],
                            points[ids[1]][1] - points[ids[0]][1],
                        )
                        if reference_length <= 1.0e-12:
                            continue
                        offer(
                            (
                                start[0] + ux * reference_length,
                                start[1] + uy * reference_length,
                            ),
                            "",
                            f"equal_length:{geometry_id}"
                            + (
                                f":{direction_constraint}"
                                if direction_constraint
                                in ("horizontal", "vertical")
                                else ""
                            ),
                            0 if direction_constraint else 2,
                        )

        if (
            self._sketch_tool in ("segment", "construction", "polyline")
            and self._sketch_pending_points
        ):
            start = self._sketch_pending_points[-1]
            for axis in self._sketch_entities:
                if axis.get("type") != "construction":
                    continue
                axis_id = str(axis.get("id", ""))
                ids = tuple(map(str, axis.get("point_ids", ())))
                if (
                    not axis_id
                    or len(ids) != 2
                    or any(point_id not in points for point_id in ids)
                ):
                    continue
                first, second = points[ids[0]], points[ids[1]]
                dx = second[0] - first[0]
                dy = second[1] - first[1]
                length_squared = dx * dx + dy * dy
                if length_squared <= 1.0e-24:
                    continue
                factor = (
                    (start[0] - first[0]) * dx
                    + (start[1] - first[1]) * dy
                ) / length_squared
                foot = (first[0] + factor * dx, first[1] + factor * dy)
                mirrored = (
                    2.0 * foot[0] - start[0],
                    2.0 * foot[1] - start[1],
                )
                if hypot(mirrored[0] - start[0], mirrored[1] - start[1]) <= 1.0e-12:
                    continue
                relation = (
                    "horizontal"
                    if abs(mirrored[1] - start[1]) <= 1.0e-9
                    else "vertical"
                    if abs(mirrored[0] - start[0]) <= 1.0e-9
                    else "perpendicular"
                )
                offer(
                    mirrored,
                    "",
                    f"symmetric_point:{axis_id}:{relation}",
                    1,
                )

        if self._sketch_tool == "rectangle" and len(self._sketch_pending_points) == 1:
            start = self._sketch_pending_points[0]
            local = self._sketch_local_position(position)
            if local is not None:
                size = (
                    abs(local[0] - start[0])
                    + abs(local[1] - start[1])
                ) * 0.5
                start_screen = self._screen_point(
                    self._camera_point(self._sketch_world_point(start))
                )
                if size > 1.0e-12:
                    horizontal_sign = (
                        1.0 if local[0] >= start[0] else -1.0
                    )
                    for axis in self._sketch_entities:
                        if axis.get("type") != "construction":
                            continue
                        axis_id = str(axis.get("id", ""))
                        ids = tuple(map(str, axis.get("point_ids", ())))
                        if (
                            not axis_id
                            or len(ids) != 2
                            or any(point_id not in points for point_id in ids)
                        ):
                            continue
                        first, second = points[ids[0]], points[ids[1]]
                        dx = second[0] - first[0]
                        dy = second[1] - first[1]
                        axis_length = hypot(dx, dy)
                        if axis_length <= 1.0e-12:
                            continue
                        ux, uy = dx / axis_length, dy / axis_length
                        factor = (
                            (start[0] - first[0]) * ux
                            + (start[1] - first[1]) * uy
                        )
                        foot = (
                            first[0] + factor * ux,
                            first[1] + factor * uy,
                        )
                        foot_screen = self._screen_point(
                            self._camera_point(self._sketch_world_point(foot))
                        )
                        if hypot(
                            foot_screen.x() - start_screen.x(),
                            foot_screen.y() - start_screen.y(),
                        ) > 12.0:
                            continue
                        vx = horizontal_sign * size
                        dot = vx * ux
                        reflected_x = 2.0 * dot * ux - vx
                        reflected_y = 2.0 * dot * uy
                        if abs(reflected_x) > size * 1.0e-6:
                            continue
                        opposite = (
                            start[0] + vx,
                            start[1] + reflected_y,
                        )
                        if abs(reflected_y) <= 1.0e-12:
                            continue
                        offer(
                            opposite,
                            "",
                            f"rectangle_symmetric:{axis_id}",
                            1,
                        )

        if self._sketch_tool == "circle" and self._sketch_pending_points:
            center = self._sketch_pending_points[0]
            local = self._sketch_local_position(position)
            if local is not None:
                dx, dy = local[0] - center[0], local[1] - center[1]
                cursor_radius = hypot(dx, dy)
                if cursor_radius > 1.0e-12:
                    ux, uy = dx / cursor_radius, dy / cursor_radius
                    equal_radius_groups = {
                        str(curve.get("equal_radius_group", "")): str(
                            curve.get("id", "")
                        )
                        for curve in self._sketch_entities
                        if curve.get("type") == "circle"
                        and str(curve.get("equal_radius_group", ""))
                        and not bool(
                            curve.get("equal_radius_reference", False)
                        )
                    }
                    for curve in self._sketch_entities:
                        if curve.get("type") != "circle":
                            continue
                        curve_id = str(curve.get("id", ""))
                        group = str(curve.get("equal_radius_group", ""))
                        if (
                            bool(curve.get("equal_radius_reference", False))
                            or group
                            and equal_radius_groups.get(group, curve_id)
                            != curve_id
                        ):
                            continue
                        radius = float(curve.get("radius", 0.0))
                        if not curve_id or radius <= 1.0e-12:
                            continue
                        equal_point = (
                            center[0] + ux * radius,
                            center[1] + uy * radius,
                        )
                        equal_screen = self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(equal_point)
                            )
                        )
                        distance = hypot(
                            position.x() - equal_screen.x(),
                            position.y() - equal_screen.y(),
                        )
                        if distance <= 16.0:
                            ranked.append((
                                distance,
                                0,
                                (
                                    equal_point,
                                    None,
                                    f"equal_radius:{curve_id}",
                                ),
                            ))
                    for owner in self._sketch_entities:
                        records = owner.get("corner_radii", ())
                        if not isinstance(records, list):
                            continue
                        for record in records:
                            if (
                                not isinstance(record, dict)
                                or bool(record.get("suppressed", False))
                                or bool(record.get("equal_radius_reference", False))
                            ):
                                continue
                            owner_id = str(owner.get("id", ""))
                            radius_id = str(
                                record.get("id")
                                or f"radius:{owner_id}:"
                                f"{record.get('other_geometry_id', '')}:"
                                f"{record.get('vertex_id', '')}"
                            )
                            radius = float(record.get("radius", 0.0))
                            if not radius_id or radius <= 1.0e-12:
                                continue
                            equal_point = (
                                center[0] + ux * radius,
                                center[1] + uy * radius,
                            )
                            equal_screen = self._screen_point(
                                self._camera_point(
                                    self._sketch_world_point(equal_point)
                                )
                            )
                            distance = hypot(
                                position.x() - equal_screen.x(),
                                position.y() - equal_screen.y(),
                            )
                            if distance <= 16.0:
                                ranked.append((
                                    distance,
                                    0,
                                    (
                                        equal_point,
                                        None,
                                        f"equal_corner_radius:{radius_id}",
                                    ),
                                ))
                    for curve in self._sketch_entities:
                        if curve.get("type") != "arc":
                            continue
                        curve_id = str(curve.get("id", ""))
                        ids = tuple(map(str, curve.get("point_ids", ())))
                        if (
                            not curve_id
                            or len(ids) < 2
                            or ids[0] not in points
                            or ids[1] not in points
                            or bool(curve.get("equal_radius_reference", False))
                        ):
                            continue
                        radius = hypot(
                            points[ids[1]][0] - points[ids[0]][0],
                            points[ids[1]][1] - points[ids[0]][1],
                        )
                        if radius <= 1.0e-12:
                            continue
                        equal_point = (
                            center[0] + ux * radius,
                            center[1] + uy * radius,
                        )
                        equal_screen = self._screen_point(
                            self._camera_point(
                                self._sketch_world_point(equal_point)
                            )
                        )
                        distance = hypot(
                            position.x() - equal_screen.x(),
                            position.y() - equal_screen.y(),
                        )
                        if distance <= 16.0:
                            ranked.append((
                                distance,
                                0,
                                (
                                    equal_point,
                                    None,
                                    f"equal_arc_radius:{curve_id}",
                                ),
                            ))

        line_references: list[
            tuple[str, tuple[float, float], tuple[float, float], bool]
        ] = [
            ("sketch_axis:x", (0.0, 0.0), (1.0, 0.0), False),
            ("sketch_axis:y", (0.0, 0.0), (0.0, 1.0), False),
        ]
        for geometry in self._sketch_entities:
            if geometry.get("type") not in ("segment", "construction"):
                continue
            geometry_id = str(geometry.get("id", ""))
            ids = tuple(map(str, geometry.get("point_ids", ())))
            if not geometry_id or len(ids) != 2 or any(pid not in points for pid in ids):
                continue
            first, second = points[ids[0]], points[ids[1]]
            line_references.append((
                f"sketch_geometry:{geometry_id}",
                first,
                (second[0] - first[0], second[1] - first[1]),
                geometry.get("type") == "segment",
            ))
            if (
                self._sketch_tool in ("segment", "construction", "polyline")
                and self._sketch_pending_points
            ):
                start = self._sketch_pending_points[-1]
                dx = second[0] - first[0]
                dy = second[1] - first[1]
                length_squared = dx * dx + dy * dy
                if length_squared > 1.0e-24:
                    factor = (
                        (start[0] - first[0]) * dx
                        + (start[1] - first[1]) * dy
                    ) / length_squared
                    foot = (
                        first[0] + factor * dx,
                        first[1] + factor * dy,
                    )
                    if (
                        (geometry.get("type") != "segment" or 0.0 <= factor <= 1.0)
                        and hypot(foot[0] - start[0], foot[1] - start[1])
                        > 1.0e-9
                    ):
                        offer(
                            foot,
                            f"sketch_geometry:{geometry_id}",
                            f"perpendicular:{geometry_id}",
                            0,
                        )
            if self._sketch_tool == "circle" and self._sketch_pending_points:
                center = self._sketch_pending_points[0]
                dx = second[0] - first[0]
                dy = second[1] - first[1]
                length_squared = dx * dx + dy * dy
                if length_squared > 1.0e-24:
                    factor = (
                        (center[0] - first[0]) * dx
                        + (center[1] - first[1]) * dy
                    ) / length_squared
                    if geometry.get("type") != "segment" or 0.0 <= factor <= 1.0:
                        offer(
                            (
                                first[0] + factor * dx,
                                first[1] + factor * dy,
                            ),
                            f"sketch_geometry:{geometry_id}",
                            f"circle_tangent:{geometry_id}",
                            0,
                        )
            offer(
                (
                    (first[0] + second[0]) * 0.5,
                    (first[1] + second[1]) * 0.5,
                ),
                f"sketch_geometry:{geometry_id}",
                f"midpoint:{geometry_id}",
                2,
            )
        for reference in self._sketch_external_references:
            reference_id = str(reference.get("id", ""))
            geometry = reference.get("geometry")
            if not reference_id or not isinstance(geometry, dict):
                continue
            raw_lines = (
                (geometry,)
                if geometry.get("type") == "line"
                else geometry.get("lines", ())
                if geometry.get("type") == "lines"
                else ()
            )
            for index, line in enumerate(raw_lines):
                if not isinstance(line, dict):
                    continue
                raw_point = line.get("point", ())
                raw_direction = line.get("direction", ())
                if (
                    not isinstance(raw_point, (list, tuple))
                    or not isinstance(raw_direction, (list, tuple))
                    or len(raw_point) < 2
                    or len(raw_direction) < 2
                ):
                    continue
                suffix = f"::line:{index}" if len(raw_lines) > 1 else ""
                line_references.append((
                    reference_id + suffix,
                    (float(raw_point[0]), float(raw_point[1])),
                    (float(raw_direction[0]), float(raw_direction[1])),
                    bool(line.get("bounded", False)),
                ))

        # A direct point-on-line snap used to come only from the single
        # nearest reference selected by _base_sketch_placement_candidate().
        # Consequently coincident axes, construction lines and segments
        # collapsed into one choice. Add every line under the cursor as its
        # own placement candidate so RMB can cycle their reference IDs.
        local_cursor = self._sketch_local_position(position)
        if local_cursor is not None:
            for (
                reference_id,
                line_origin,
                line_direction,
                bounded,
            ) in line_references:
                length_squared = (
                    line_direction[0] * line_direction[0]
                    + line_direction[1] * line_direction[1]
                )
                if length_squared <= 1.0e-18:
                    continue
                factor = (
                    (local_cursor[0] - line_origin[0]) * line_direction[0]
                    + (local_cursor[1] - line_origin[1]) * line_direction[1]
                ) / length_squared
                if bounded and not 0.0 <= factor <= 1.0:
                    continue
                projected = (
                    line_origin[0] + factor * line_direction[0],
                    line_origin[1] + factor * line_direction[1],
                )
                direction_constraint = (
                    self._sketch_inferred_direction_constraint(projected)
                    if self._sketch_pending_points
                    else None
                )
                offer(
                    projected,
                    reference_id,
                    direction_constraint,
                    -2,
                )
        for first_index, first_line in enumerate(line_references):
            first_id, first_origin, first_direction, first_bounded = first_line
            for second_id, second_origin, second_direction, second_bounded in line_references[first_index + 1:]:
                denominator = (
                    first_direction[0] * second_direction[1]
                    - first_direction[1] * second_direction[0]
                )
                if abs(denominator) <= 1.0e-12:
                    continue
                offset = (
                    second_origin[0] - first_origin[0],
                    second_origin[1] - first_origin[1],
                )
                first_factor = (
                    offset[0] * second_direction[1]
                    - offset[1] * second_direction[0]
                ) / denominator
                second_factor = (
                    offset[0] * first_direction[1]
                    - offset[1] * first_direction[0]
                ) / denominator
                if (
                    (first_bounded and not 0.0 <= first_factor <= 1.0)
                    or (second_bounded and not 0.0 <= second_factor <= 1.0)
                ):
                    continue
                intersection = (
                    first_origin[0] + first_factor * first_direction[0],
                    first_origin[1] + first_factor * first_direction[1],
                )
                offer(
                    intersection,
                    first_id + "||" + second_id,
                    "intersection",
                    1,
                )

        for arc in self._sketch_entities:
            if arc.get("type") != "arc":
                continue
            arc_id = str(arc.get("id", ""))
            ids = tuple(map(str, arc.get("point_ids", ())))
            if not arc_id or len(ids) != 3 or any(pid not in points for pid in ids):
                continue
            for angle, keypoint in arc_cardinal_keypoints(
                points[ids[0]], points[ids[1]], points[ids[2]],
                clockwise=bool(arc.get("clockwise", False)),
            ):
                offer(
                    keypoint,
                    f"sketch_arc:{arc_id}",
                    f"keypoint:{angle}",
                    3,
                )

        for arc in self._sketch_entities:
            if arc.get("type") != "elliptical_arc":
                continue
            arc_id = str(arc.get("id", ""))
            ids = tuple(map(str, arc.get("point_ids", ())))
            if not arc_id or len(ids) != 5 or any(pid not in points for pid in ids):
                continue
            for angle, keypoint in elliptical_arc_cardinal_keypoints(
                points[ids[0]], points[ids[1]], points[ids[2]],
                points[ids[3]], points[ids[4]],
                clockwise=bool(arc.get("clockwise", False)),
            ):
                offer(
                    keypoint,
                    f"sketch_elliptical_arc:{arc_id}",
                    f"keypoint:{angle}",
                    3,
                )

        for curve in self._sketch_entities:
            curve_type = str(curve.get("type", ""))
            if curve_type not in ("circle", "ellipse"):
                continue
            curve_id = str(curve.get("id", ""))
            ids = tuple(map(str, curve.get("point_ids", ())))
            if not curve_id or not ids or ids[0] not in points:
                continue
            center = points[ids[0]]
            if curve_type == "circle":
                radius = float(curve.get("radius", 0.0))
                if radius <= 1.0e-12:
                    continue
                reference_id = f"sketch_circle:{curve_id}"
                axes = ((radius, 0.0), (0.0, radius))
                if self._sketch_tool == "circle" and self._sketch_pending_points:
                    new_center = self._sketch_pending_points[0]
                    center_dx = new_center[0] - center[0]
                    center_dy = new_center[1] - center[1]
                    center_distance = hypot(center_dx, center_dy)
                    if center_distance > 1.0e-12:
                        ux = center_dx / center_distance
                        uy = center_dy / center_distance
                        for sign in (1.0, -1.0):
                            offer(
                                (
                                    center[0] + sign * radius * ux,
                                    center[1] + sign * radius * uy,
                                ),
                                reference_id,
                                f"circle_curve_tangent:{curve_id}",
                                0,
                            )
            else:
                if len(ids) < 3 or any(pid not in points for pid in ids[:3]):
                    continue
                major, minor = points[ids[1]], points[ids[2]]
                reference_id = f"sketch_ellipse:{curve_id}"
                axes = (
                    (major[0] - center[0], major[1] - center[1]),
                    (minor[0] - center[0], minor[1] - center[1]),
                )
            for angle_index, vector in enumerate((
                axes[0], axes[1], (-axes[0][0], -axes[0][1]),
                (-axes[1][0], -axes[1][1]),
            )):
                offer(
                    (center[0] + vector[0], center[1] + vector[1]),
                    reference_id,
                    f"keypoint:{angle_index * 90}",
                    3,
                )
            # Intersect the curve with every linear reference, not only the
            # sketch X/Y axes. A construction geometry is treated as an
            # infinite line; an ordinary segment remains bounded by its ends.
            for line_id, line_origin, direction, bounded in line_references:
                dx, dy = direction
                if dx * dx + dy * dy <= 1.0e-24:
                    continue
                ox = line_origin[0] - center[0]
                oy = line_origin[1] - center[1]
                if curve_type == "circle":
                    quadratic_a = dx * dx + dy * dy
                    quadratic_b = 2.0 * (ox * dx + oy * dy)
                    quadratic_c = ox * ox + oy * oy - radius * radius
                else:
                    ax, ay = axes[0]
                    bx, by = axes[1]
                    determinant = ax * by - ay * bx
                    if abs(determinant) <= 1.0e-12:
                        continue
                    local_u = (ox * by - oy * bx) / determinant
                    local_v = (ax * oy - ay * ox) / determinant
                    direction_u = (dx * by - dy * bx) / determinant
                    direction_v = (ax * dy - ay * dx) / determinant
                    quadratic_a = (
                        direction_u * direction_u
                        + direction_v * direction_v
                    )
                    quadratic_b = 2.0 * (
                        local_u * direction_u
                        + local_v * direction_v
                    )
                    quadratic_c = local_u * local_u + local_v * local_v - 1.0
                if quadratic_a <= 1.0e-24:
                    continue
                discriminant = (
                    quadratic_b * quadratic_b
                    - 4.0 * quadratic_a * quadratic_c
                )
                if discriminant < -1.0e-12:
                    continue
                root = sqrt(max(0.0, discriminant))
                factors = (
                    (-quadratic_b - root) / (2.0 * quadratic_a),
                    (-quadratic_b + root) / (2.0 * quadratic_a),
                )
                for factor in dict.fromkeys(factors):
                    if bounded and not -1.0e-12 <= factor <= 1.0 + 1.0e-12:
                        continue
                    offer(
                        (
                            line_origin[0] + factor * dx,
                            line_origin[1] + factor * dy,
                        ),
                        line_id + "||" + reference_id,
                        "intersection",
                        1,
                    )
        if (
            self._sketch_tool == "polyline"
            and base[2] is not None
            and base[2].startswith("tangent:")
        ):
            # After an arc the outgoing tangent is mandatory for this chain
            # segment.  Generic point/line candidates collected above must
            # not replace the tangent-line intersection selected by base.
            # RMB cycling would otherwise make the preview disappear and
            # allow the committed endpoint attachment to break tangency.
            tangent_both = [
                item[2]
                for item in ranked
                if item[2][2] is not None
                and item[2][2].startswith("tangent_both:")
            ]
            return tuple(dict.fromkeys((*tangent_both, base)))
        ranked.sort(key=lambda item: (item[1], item[0]))
        candidates = [item[2] for item in ranked]
        if (
            self._sketch_point_candidate(position) is not None
            or (
                base[1] is not None
                and base[2] is not None
                and base[2].startswith("perpendicular:")
            )
        ):
            candidates.insert(0, base)
        elif base[2] in ("horizontal", "vertical"):
            direction_relation_index = next(
                (
                    index
                    for index, candidate in enumerate(candidates)
                    if candidate[2] is not None
                    and (
                        candidate[2].startswith(
                            ("perpendicular:", "parallel:")
                        )
                        or (
                            candidate[2].startswith("equal_length:")
                            and not candidate[2].endswith(
                                (":horizontal", ":vertical")
                            )
                        )
                    )
                ),
                len(candidates),
            )
            candidates.insert(direction_relation_index, base)
        else:
            candidates.append(base)
        return tuple(dict.fromkeys(candidates))

    def _sketch_placement_candidate(
        self,
        position: QPointF,
    ) -> tuple[tuple[float, float], str | None, str | None]:
        candidates = self._smart_sketch_placement_candidates(position)
        same_cursor = (
            self._sketch_placement_candidate_cursor is not None
            and hypot(
                position.x() - self._sketch_placement_candidate_cursor.x(),
                position.y() - self._sketch_placement_candidate_cursor.y(),
            ) <= 3.0
        )
        if not same_cursor or candidates != self._sketch_placement_candidates:
            self._sketch_placement_candidate_index = 0
        self._sketch_placement_candidates = candidates
        self._sketch_placement_candidate_cursor = QPointF(position)
        if not candidates:
            self._sketch_preview_is_keypoint = False
            return (self._sketch_local_position(position) or (0.0, 0.0)), None, None
        self._sketch_placement_candidate_index %= len(candidates)
        candidate, reference_id, constraint = candidates[
            self._sketch_placement_candidate_index
        ]
        self._sketch_preview_is_keypoint = (
            self._placement_is_keypoint(candidate, constraint)
        )
        if constraint in ("horizontal", "vertical") and (
            self._redundant_sketch_direction_preview(
                candidate,
                reference_id,
                constraint,
            )
        ):
            # Two already coincident/axis-fixed endpoints need no additional
            # line-direction relation. The preview must show C, not H/V.
            constraint = None
        if self._sketch_tool == "polyline_arc" and self._sketch_pending_points:
            start = self._sketch_pending_points[-1]
            start_entity = next(
                (entity for entity in self._sketch_entities
                 if entity.get("type") == "point"
                 and hypot(float(entity.get("x", 0.0)) - start[0],
                           float(entity.get("y", 0.0)) - start[1]) <= 1.0e-9),
                None,
            )
            context = polyline_arc_start_context(
                list(self._sketch_entities),
                str(start_entity.get("id", "")) if start_entity else "",
            )
            local_cursor = self._sketch_local_position(position)
            support_start = context is not None and context[2] is not None
            if not support_start:
                # The side chosen for an axis/construction arc must not leak
                # through the next segment and reverse its following tangent
                # arc back into that segment.
                self._sketch_polyline_arc_reverse = False
            if support_start and local_cursor is not None:
                base_direction = context[0]
                side = (
                    (local_cursor[0] - start[0]) * base_direction[0]
                    + (local_cursor[1] - start[1]) * base_direction[1]
                )
                if abs(side) > 1.0e-9:
                    self._sketch_polyline_arc_reverse = side < 0.0
            if context is not None and context[1] is not None:
                outward = outward_minor_arc_endpoint(
                    start,
                    context[0],
                    candidate,
                )
                if hypot(
                    outward[0] - candidate[0], outward[1] - candidate[1]
                ) > 1.0e-9:
                    candidate = outward
                    # The reflected point is deliberately no longer the
                    # behind-the-line snap target.
                    reference_id = None
                    constraint = None
            snapped = self._polyline_arc_center_snap(candidate)
            if snapped is not None:
                candidate, center_constraint = snapped
                constraint = center_constraint + (
                    ":reverse" if self._sketch_polyline_arc_reverse else ":forward"
                )
            elif support_start:
                constraint = (
                    "polyline_arc_direction:reverse"
                    if self._sketch_polyline_arc_reverse
                    else "polyline_arc_direction:forward"
                )
        pending_keypoint = self._pending_arc_quadrant_snap(position, candidate)
        if pending_keypoint is not None:
            candidate, constraint = pending_keypoint
            reference_id = None
        if (
            self._sketch_tool in ("ellipse", "elliptical_arc")
            and len(self._sketch_pending_points) == 1
            and constraint is None
        ):
            # K is an automatic, lowest-priority relation. Any stronger
            # inference selected for the first semi-axis remains untouched.
            constraint = "keypoint:0.0"
        minor_keypoint = self._pending_ellipse_minor_axis_snap(
            position, candidate
        )
        if minor_keypoint is not None:
            candidate, is_near = minor_keypoint
            if is_near and constraint is None:
                center, major = self._sketch_pending_points
                cross = (
                    (major[0] - center[0]) * (candidate[1] - center[1])
                    - (major[1] - center[1]) * (candidate[0] - center[0])
                )
                constraint = "keypoint:90.0" if cross >= 0.0 else "keypoint:270.0"
                reference_id = None
        self._sketch_preview_is_keypoint = self._placement_is_keypoint(
            candidate, constraint
        )
        return candidate, reference_id, constraint

    def _placement_is_keypoint(
        self,
        position: tuple[float, float],
        constraint: str | None,
    ) -> bool:
        if constraint is not None and constraint.startswith("keypoint:"):
            return True
        if (
            self._sketch_tool in ("ellipse", "elliptical_arc")
            and len(self._sketch_pending_points) == 1
        ):
            # The second input is the end of the first semi-axis and is
            # therefore intrinsically one of the new ellipse keypoints.
            return True
        # A keypoint is magnetic feedback for a nearby existing curve.  The
        # minor-axis step receives K separately only when the cursor is near
        # its perpendicular projected endpoint.
        return self._is_sketch_keypoint_position(position)

    def _redundant_sketch_direction_preview(
        self,
        candidate: tuple[float, float],
        reference_id: str | None,
        constraint_type: str,
    ) -> bool:
        if not self._sketch_pending_points:
            return False
        coordinate = "y" if constraint_type == "horizontal" else "x"
        fixed_references = (
            {"sketch_origin", "sketch_axis:x"}
            if coordinate == "y"
            else {"sketch_origin", "sketch_axis:y"}
        )

        def matching_point(position: tuple[float, float]):
            return next((
                entity
                for entity in self._sketch_entities
                if entity.get("type") == "point"
                and hypot(
                    float(entity.get("x", 0.0)) - position[0],
                    float(entity.get("y", 0.0)) - position[1],
                ) <= 1.0e-9
            ), None)

        def fixed(point, direct_reference: str | None = None) -> bool:
            if direct_reference in fixed_references:
                return True
            if point is None:
                return False
            constraints = point.get("constraints", ())
            return isinstance(constraints, list) and any(
                isinstance(item, dict)
                and item.get("type") == "point_on_reference"
                and str(item.get("reference_id", "")) in fixed_references
                for item in constraints
            )

        first_point = matching_point(self._sketch_pending_points[-1])
        second_point = matching_point(candidate)

        def linear_support_ids(
            point,
            direct_reference: str | None = None,
        ) -> set[str]:
            support_ids: set[str] = set()
            if direct_reference and direct_reference.startswith(
                "sketch_geometry:"
            ):
                support_ids.add(direct_reference.split(":", 1)[1])
            if point is None:
                return support_ids
            point_id = str(point.get("id", ""))
            support_ids.update(
                str(entity.get("id", ""))
                for entity in self._sketch_entities
                if entity.get("type") in ("segment", "construction")
                and point_id in map(str, entity.get("point_ids", ()))
            )
            constraints = point.get("constraints", ())
            if isinstance(constraints, list):
                attached_lines = {
                    tuple(map(str, item.get("point_ids", ())))
                    for item in constraints
                    if isinstance(item, dict)
                    and item.get("type") == "point_on_line"
                }
                support_ids.update(
                    str(entity.get("id", ""))
                    for entity in self._sketch_entities
                    if entity.get("type") in ("segment", "construction")
                    and tuple(map(str, entity.get("point_ids", ())))
                    in attached_lines
                )
            return support_ids

        same_linear_support = bool(
            linear_support_ids(first_point)
            & linear_support_ids(second_point, reference_id)
        )
        return same_linear_support or (
            fixed(first_point)
            and fixed(second_point, reference_id)
        )

    def _pending_arc_quadrant_snap(
        self,
        cursor: QPointF,
        current: tuple[float, float],
    ) -> tuple[tuple[float, float], str] | None:
        candidates: list[tuple[float, tuple[float, float]]] = []
        if self._sketch_tool == "arc" and len(self._sketch_pending_points) == 2:
            center, start = self._sketch_pending_points
            radius = hypot(start[0] - center[0], start[1] - center[1])
            start_angle = atan2(start[1] - center[1], start[0] - center[0])
            if radius > 1.0e-12:
                candidates = [
                    (
                        degrees(angle) % 360.0,
                        (center[0] + radius * cos(angle),
                         center[1] + radius * sin(angle)),
                    )
                    for angle in (
                        start_angle + step * pi * 0.5
                        for step in (-1, 1, 2)
                    )
                ]
        elif (
            self._sketch_tool == "elliptical_arc"
            and len(self._sketch_pending_points) in (3, 4)
        ):
            center, major, minor = self._sketch_pending_points[:3]
            ax, ay = major[0] - center[0], major[1] - center[1]
            bx, by = minor[0] - center[0], minor[1] - center[1]
            candidates = [
                (
                    float(step * 90),
                    (center[0] + ax * cos(step * pi * 0.5)
                     + bx * sin(step * pi * 0.5),
                     center[1] + ay * cos(step * pi * 0.5)
                     + by * sin(step * pi * 0.5)),
                )
                for step in range(4)
            ]
        elif self._sketch_tool == "polyline_arc" and self._sketch_pending_points:
            start = self._sketch_pending_points[-1]
            direction = self._polyline_arc_start_direction(start)
            length = hypot(*(direction or (0.0, 0.0)))
            if direction is not None and length > 1.0e-12:
                tx, ty = direction[0] / length, direction[1] / length
                nx, ny = -ty, tx
                dx, dy = current[0] - start[0], current[1] - start[1]
                denominator = 2.0 * (dx * nx + dy * ny)
                if abs(denominator) > 1.0e-9:
                    signed_radius = (dx * dx + dy * dy) / denominator
                    center = (
                        start[0] + nx * signed_radius,
                        start[1] + ny * signed_radius,
                    )
                    radius = abs(signed_radius)
                    start_angle = atan2(start[1] - center[1], start[0] - center[0])
                    sweep_sign = -1.0 if signed_radius < 0.0 else 1.0
                    candidates = [
                        (
                            float(step * 90),
                            (
                                center[0] + radius * cos(
                                    start_angle + sweep_sign * step * pi * 0.5
                                ),
                                center[1] + radius * sin(
                                    start_angle + sweep_sign * step * pi * 0.5
                                ),
                            ),
                        )
                        for step in (1, 2)
                    ]
        ranked = []
        for angle, point in candidates:
            screen = self._screen_point(
                self._camera_point(self._sketch_world_point(point))
            )
            ranked.append((
                hypot(cursor.x() - screen.x(), cursor.y() - screen.y()),
                angle,
                point,
            ))
        if not ranked:
            return None
        distance, angle, point = min(ranked, key=lambda item: item[0])
        if distance > 16.0:
            return None
        return point, f"keypoint:{angle}"

    def _pending_ellipse_minor_axis_snap(
        self,
        cursor: QPointF,
        current: tuple[float, float],
    ) -> tuple[tuple[float, float], bool] | None:
        if (
            self._sketch_tool not in ("ellipse", "elliptical_arc")
            or len(self._sketch_pending_points) != 2
        ):
            return None
        center, major = self._sketch_pending_points
        ax, ay = major[0] - center[0], major[1] - center[1]
        length = hypot(ax, ay)
        if length <= 1.0e-12:
            return None
        nx, ny = -ay / length, ax / length
        signed = (
            (current[0] - center[0]) * nx
            + (current[1] - center[1]) * ny
        )
        projected = (center[0] + nx * signed, center[1] + ny * signed)
        screen = self._screen_point(
            self._camera_point(self._sketch_world_point(projected))
        )
        return projected, hypot(
            cursor.x() - screen.x(), cursor.y() - screen.y()
        ) <= 16.0

    def _is_sketch_keypoint_position(
        self,
        position: tuple[float, float],
    ) -> bool:
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        candidates: list[tuple[float, float]] = []
        for curve in self._sketch_entities:
            curve_type = str(curve.get("type", ""))
            ids = tuple(map(str, curve.get("point_ids", ())))
            if not ids or ids[0] not in points:
                continue
            center = points[ids[0]]
            if curve_type == "circle":
                radius = float(curve.get("radius", 0.0))
                candidates.extend((
                    (center[0] + radius, center[1]),
                    (center[0], center[1] + radius),
                    (center[0] - radius, center[1]),
                    (center[0], center[1] - radius),
                ))
            elif curve_type == "arc" and len(ids) == 3 and all(pid in points for pid in ids):
                candidates.extend(
                    point for _angle, point in arc_cardinal_keypoints(
                        center, points[ids[1]], points[ids[2]],
                        clockwise=bool(curve.get("clockwise", False)),
                    )
                )
            elif curve_type == "ellipse" and len(ids) >= 3 and all(pid in points for pid in ids[:3]):
                major, minor = points[ids[1]], points[ids[2]]
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                candidates.extend((
                    (center[0] + ax, center[1] + ay),
                    (center[0] + bx, center[1] + by),
                    (center[0] - ax, center[1] - ay),
                    (center[0] - bx, center[1] - by),
                ))
            elif curve_type == "elliptical_arc" and len(ids) == 5 and all(pid in points for pid in ids):
                candidates.extend(
                    point for _angle, point in elliptical_arc_cardinal_keypoints(
                        center, points[ids[1]], points[ids[2]],
                        points[ids[3]], points[ids[4]],
                        clockwise=bool(curve.get("clockwise", False)),
                    )
                )
        return any(hypot(position[0] - point[0], position[1] - point[1]) <= 1.0e-7
                   for point in candidates)

    def _polyline_arc_center_snap(
        self,
        endpoint: tuple[float, float],
    ) -> tuple[tuple[float, float], str] | None:
        """Snap the derived arc centre to a real point or sketch origin."""
        start = self._sketch_pending_points[-1]
        direction = self._polyline_arc_start_direction(start)
        length = hypot(*(direction or (0.0, 0.0)))
        if direction is None or length <= 1.0e-12:
            return None
        tx, ty = direction[0] / length, direction[1] / length
        nx, ny = -ty, tx
        dx, dy = endpoint[0] - start[0], endpoint[1] - start[1]
        denominator = 2.0 * (dx * nx + dy * ny)
        if abs(denominator) <= 1.0e-9:
            return None
        radius = (dx * dx + dy * dy) / denominator
        raw_center = (start[0] + nx * radius, start[1] + ny * radius)
        targets = [
            (
                (float(entity.get("x", 0.0)), float(entity.get("y", 0.0))),
                f"polyline_arc_center_point:{entity.get('id', '')}",
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        ]
        targets.append(((0.0, 0.0), "polyline_arc_center_origin"))
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        center_lines = [
            ((0.0, 0.0), (1.0, 0.0), "sketch_axis:x"),
            ((0.0, 0.0), (0.0, 1.0), "sketch_axis:y"),
        ]
        for geometry in self._sketch_entities:
            if geometry.get("type") != "construction":
                continue
            ids = tuple(map(str, geometry.get("point_ids", ())))
            if len(ids) != 2 or any(pid not in points for pid in ids):
                continue
            first, second = points[ids[0]], points[ids[1]]
            center_lines.append((
                first,
                (second[0] - first[0], second[1] - first[1]),
                f"sketch_geometry:{geometry.get('id', '')}",
            ))
        # The centre is restricted to the normal through the arc start.  Its
        # intersection with an axis/construction line is therefore a genuine
        # one-click coincident candidate.
        for origin, line_direction, reference_id in center_lines:
            determinant = nx * line_direction[1] - ny * line_direction[0]
            if abs(determinant) <= 1.0e-12:
                continue
            offset = (origin[0] - start[0], origin[1] - start[1])
            factor = (
                offset[0] * line_direction[1]
                - offset[1] * line_direction[0]
            ) / determinant
            target = (start[0] + factor * nx, start[1] + factor * ny)
            targets.append((
                target,
                f"polyline_arc_center_reference:{reference_id}",
            ))
        raw_screen = self._screen_point(
            self._camera_point(self._sketch_world_point(raw_center))
        )
        ranked = []
        for target, marker in targets:
            target_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(target))
            )
            distance = hypot(
                raw_screen.x() - target_screen.x(),
                raw_screen.y() - target_screen.y(),
            )
            # A valid centre must lie on the normal through the arc start.
            normal_error = abs(
                (target[0] - start[0]) * tx
                + (target[1] - start[1]) * ty
            )
            if distance <= 12.0 and normal_error <= 1.0e-7:
                ranked.append((distance, target, marker))
        if not ranked:
            return None
        _distance, center, marker = min(ranked, key=lambda item: item[0])
        fixed_radius = hypot(center[0] - start[0], center[1] - start[1])
        ex, ey = endpoint[0] - center[0], endpoint[1] - center[1]
        endpoint_length = hypot(ex, ey)
        if fixed_radius <= 1.0e-12 or endpoint_length <= 1.0e-12:
            return None
        return (
            (
                center[0] + fixed_radius * ex / endpoint_length,
                center[1] + fixed_radius * ey / endpoint_length,
            ),
            marker,
        )

    def _base_sketch_placement_candidate(
        self,
        position: QPointF,
    ) -> tuple[tuple[float, float], str | None, str | None]:
        local = self._sketch_local_position(position) or (0.0, 0.0)
        outgoing_arc_constraint: str | None = None
        outgoing_arc_direction: tuple[float, float] | None = None
        if self._sketch_tool == "polyline" and self._sketch_pending_points:
            start = self._sketch_pending_points[-1]
            start_entity = next((
                entity for entity in self._sketch_entities
                if entity.get("type") == "point"
                and hypot(
                    float(entity.get("x", 0.0)) - start[0],
                    float(entity.get("y", 0.0)) - start[1],
                ) <= 1.0e-9
            ), None)
            context = polyline_arc_start_context(
                list(self._sketch_entities),
                str(start_entity.get("id", "")) if start_entity else "",
            )
            support_id = context[1] if context is not None else None
            support = next((
                entity for entity in self._sketch_entities
                if str(entity.get("id", "")) == support_id
            ), None)
            if support is not None and support.get("type") == "arc":
                outgoing_arc_constraint = f"tangent:{support_id}"
                outgoing_arc_direction = context[0]
        perpendicular_candidates = (
            self._sketch_perpendicular_placement_candidates(position)
        )
        perpendicular_constraint = (
            f"perpendicular:{perpendicular_candidates[0][2]}"
            if perpendicular_candidates
            else None
        )
        nearest_point = self._sketch_point_candidate(position)
        if nearest_point is not None:
            snapped = nearest_point[2]
            constraint = outgoing_arc_constraint
            if constraint is not None and outgoing_arc_direction is not None:
                first = self._sketch_pending_points[-1]
                # A point can carry both coincidence and outgoing tangency
                # only when it already lies on the tangent guide.
                dx, dy = outgoing_arc_direction
                scale = hypot(dx, dy)
                line_error = abs(
                    (snapped[0] - first[0]) * dy
                    - (snapped[1] - first[1]) * dx
                ) / max(scale, 1.0e-12)
                if line_error > 1.0e-7:
                    nearest_point = None
            if nearest_point is None:
                pass
            else:
                constraint = constraint or None
                if perpendicular_candidates:
                    guided = perpendicular_candidates[0][1]
                    snapped_screen = self._screen_point(
                        self._camera_point(self._sketch_world_point(snapped))
                    )
                    guided_screen = self._screen_point(
                        self._camera_point(self._sketch_world_point(guided))
                    )
                    if hypot(
                        snapped_screen.x() - guided_screen.x(),
                        snapped_screen.y() - guided_screen.y(),
                    ) <= 14.0:
                        constraint = perpendicular_constraint
                if constraint is None and self._sketch_pending_points:
                    constraint = self._sketch_inferred_direction_constraint(snapped)
                return snapped, None, constraint
        origin_screen = self._screen_point(
            self._camera_point(self._sketch_world_point((0.0, 0.0)))
        )
        if hypot(
            position.x() - origin_screen.x(),
            position.y() - origin_screen.y(),
        ) <= 12.0:
            return (
                (0.0, 0.0),
                "sketch_axis:x||sketch_axis:y",
                "intersection",
            )
        if self._sketch_reference_snapping:
            sketch_curve = self._sketch_curve_reference_candidate(position)
            if sketch_curve is not None:
                snapped = sketch_curve[1]
                preserve_outgoing_tangent = outgoing_arc_constraint is not None
                constraint = outgoing_arc_constraint or perpendicular_constraint or (
                    self._sketch_inferred_direction_constraint(snapped)
                    if self._sketch_pending_points
                    else None
                )
                if constraint is not None:
                    combined = self._sketch_reference_direction_snap(
                        sketch_curve[0],
                        constraint,
                        snapped,
                    )
                    if combined is None:
                        constraint = None
                    else:
                        snapped = combined
                if not preserve_outgoing_tangent or combined is not None:
                    return snapped, sketch_curve[0], constraint
            sketch_line = self._sketch_line_reference_candidate(position)
            if sketch_line is not None:
                geometry_id, snapped = sketch_line
                reference_id = f"sketch_geometry:{geometry_id}"
                preserve_outgoing_tangent = outgoing_arc_constraint is not None
                constraint = (
                    outgoing_arc_constraint or perpendicular_constraint
                    or self._sketch_inferred_direction_constraint(snapped)
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
                if not preserve_outgoing_tangent or combined is not None:
                    return snapped, reference_id, constraint
            reference = self._sketch_external_reference_candidate(
                position
            )
            if reference is not None:
                reference_id, snapped = reference
                preserve_outgoing_tangent = outgoing_arc_constraint is not None
                constraint = (
                    outgoing_arc_constraint or perpendicular_constraint
                    or self._sketch_inferred_direction_constraint(snapped)
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
                if not preserve_outgoing_tangent or combined is not None:
                    return (snapped, reference_id, constraint)

        if self._sketch_tool == "polyline" and self._sketch_pending_points:
            first = self._sketch_pending_points[-1]
            point_positions = {
                str(entity.get("id", "")): (
                    float(entity.get("x", 0.0)),
                    float(entity.get("y", 0.0)),
                )
                for entity in self._sketch_entities
                if entity.get("type") == "point"
            }
            previous_arc = next((
                entity for entity in reversed(self._sketch_entities)
                if entity.get("type") == "arc"
                and entity.get("point_ids")
                and str(entity.get("point_ids")[-1]) in point_positions
                and hypot(
                    point_positions[str(entity.get("point_ids")[-1])][0] - first[0],
                    point_positions[str(entity.get("point_ids")[-1])][1] - first[1],
                ) <= 1.0e-9
            ), None)
            ids = tuple(map(str, previous_arc.get("point_ids", ()))) if previous_arc else ()
            if len(ids) == 3 and ids[0] in point_positions:
                center = point_positions[ids[0]]
                radial = (first[0] - center[0], first[1] - center[1])
                tangent = (
                    (radial[1], -radial[0])
                    if previous_arc.get("clockwise")
                    else (-radial[1], radial[0])
                )
                length_squared = tangent[0] ** 2 + tangent[1] ** 2
                if length_squared > 1.0e-12:
                    factor = (
                        (local[0] - first[0]) * tangent[0]
                        + (local[1] - first[1]) * tangent[1]
                    ) / length_squared
                    return (
                        first[0] + factor * tangent[0],
                        first[1] + factor * tangent[1],
                    ), None, f"tangent:{previous_arc.get('id', '')}"
        if self._sketch_pending_points and self._sketch_tool != "text":
            constraint = self._sketch_inferred_direction_constraint(
                local
            )
            if constraint == "horizontal":
                first = self._sketch_pending_points[-1]
                return (local[0], first[1]), None, constraint
            if constraint == "vertical":
                first = self._sketch_pending_points[-1]
                return (first[0], local[1]), None, constraint
            if constraint is not None and constraint.startswith("parallel:"):
                geometry_id = constraint.split(":", 1)[1]
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
                        entity for entity in self._sketch_entities
                        if str(entity.get("id", "")) == geometry_id
                        and entity.get("type") in ("segment", "construction")
                    ),
                    None,
                )
                ids = tuple(map(str, source.get("point_ids", ()))) if source else ()
                if len(ids) == 2 and all(point_id in points for point_id in ids):
                    direction = (
                        points[ids[1]][0] - points[ids[0]][0],
                        points[ids[1]][1] - points[ids[0]][1],
                    )
                    length_squared = direction[0] ** 2 + direction[1] ** 2
                    if length_squared > 1.0e-12:
                        first = self._sketch_pending_points[-1]
                        factor = (
                            (local[0] - first[0]) * direction[0]
                            + (local[1] - first[1]) * direction[1]
                        ) / length_squared
                        return (
                            first[0] + factor * direction[0],
                            first[1] + factor * direction[1],
                        ), None, constraint
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
        for ellipse in self._sketch_entities:
            ellipse_type = str(ellipse.get("type", ""))
            if ellipse_type not in ("ellipse", "elliptical_arc"):
                continue
            ids = tuple(map(str, ellipse.get("point_ids", ())))
            required = 3 if ellipse_type == "ellipse" else 5
            if len(ids) < required or any(pid not in points for pid in ids):
                continue
            sampled = (
                ellipse_points(points[ids[0]], points[ids[1]], points[ids[2]])
                if ellipse_type == "ellipse"
                else elliptical_arc_points(
                    points[ids[0]],
                    points[ids[1]],
                    points[ids[2]],
                    points[ids[3]],
                    points[ids[4]],
                    clockwise=bool(ellipse.get("clockwise", False)),
                )
            )
            for curve_point in sampled:
                screen = self._screen_point(
                    self._camera_point(self._sketch_world_point(curve_point))
                )
                distance = hypot(
                    position.x() - screen.x(), position.y() - screen.y()
                )
                if distance <= 12.0:
                    candidates.append((
                        distance,
                        f"sketch_{ellipse_type}:{ellipse.get('id', '')}",
                        curve_point,
                    ))
        for spline in self._sketch_entities:
            if spline.get("type") != "spline":
                continue
            ids = tuple(map(str, spline.get("point_ids", ())))
            if len(ids) < 2 or any(point_id not in points for point_id in ids):
                continue
            sampled = _interpolated_spline_points(
                tuple(points[point_id] for point_id in ids),
                stored_spline_tangent(spline, "start_tangent"),
                stored_spline_tangent(spline, "end_tangent"),
            )
            for curve_point in sampled:
                screen = self._screen_point(
                    self._camera_point(self._sketch_world_point(curve_point))
                )
                distance = hypot(
                    position.x() - screen.x(), position.y() - screen.y()
                )
                if distance <= 12.0:
                    candidates.append((
                        distance,
                        f"sketch_spline:{spline.get('id', '')}",
                        curve_point,
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
        if self._sketch_tool not in ("segment", "construction", "polyline", "polyline_arc"):
            return None
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        nearest_parallel: tuple[float, str] | None = None
        for geometry in self._sketch_entities:
            if geometry.get("type") not in ("segment", "construction"):
                continue
            geometry_id = str(geometry.get("id", ""))
            ids = tuple(map(str, geometry.get("point_ids", ())))
            if not geometry_id or len(ids) != 2 or any(pid not in points for pid in ids):
                continue
            source_first = points[ids[0]]
            source_second = points[ids[1]]
            guide_end = (
                first[0] + source_second[0] - source_first[0],
                first[1] + source_second[1] - source_first[1],
            )
            guide_first_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(first))
            )
            guide_end_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(guide_end))
            )
            distance, _factor = self._point_segment_distance(
                candidate_screen,
                guide_first_screen,
                guide_end_screen,
            )
            # Measure against the infinite guide rather than the source
            # segment length.
            dx = guide_end_screen.x() - guide_first_screen.x()
            dy = guide_end_screen.y() - guide_first_screen.y()
            length = hypot(dx, dy)
            if length <= 1.0e-9:
                continue
            distance = abs(
                (candidate_screen.x() - guide_first_screen.x()) * dy
                - (candidate_screen.y() - guide_first_screen.y()) * dx
            ) / length
            if distance <= 14.0 and (
                nearest_parallel is None or distance < nearest_parallel[0]
            ):
                nearest_parallel = (distance, geometry_id)
        if nearest_parallel is not None:
            return f"parallel:{nearest_parallel[1]}"
        return None

    def _polyline_arc_start_direction(
        self,
        start: tuple[float, float],
    ) -> tuple[float, float] | None:
        """Direction in which a Polyline arc must leave its start point."""
        start_entity = next(
            (entity for entity in self._sketch_entities
             if entity.get("type") == "point"
             and hypot(float(entity.get("x", 0.0)) - start[0],
                       float(entity.get("y", 0.0)) - start[1]) <= 1.0e-9),
            None,
        )
        context = polyline_arc_start_context(
            list(self._sketch_entities),
            str(start_entity.get("id", "")) if start_entity else "",
        )
        if context is not None:
            direction = context[0]
            if context[2] is not None and self._sketch_polyline_arc_reverse:
                return (-direction[0], -direction[1])
            return direction
        return None

    def _sketch_tangent_placement_candidates(
        self,
        position: QPointF,
    ) -> tuple[
        tuple[float, tuple[float, float], str | None, str], ...
    ]:
        """Return tangent endpoints whose guide passes near the cursor."""

        if (
            self._sketch_tool not in ("segment", "construction", "polyline", "polyline_arc")
            or len(self._sketch_pending_points) != 1
        ):
            return ()
        first = self._sketch_pending_points[0]
        first_screen = self._screen_point(
            self._camera_point(self._sketch_world_point(first))
        )
        cursor_dx = position.x() - first_screen.x()
        cursor_dy = position.y() - first_screen.y()
        if hypot(cursor_dx, cursor_dy) < 20.0:
            return ()
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        candidates: list[
            tuple[float, tuple[float, float], str | None, str]
        ] = []

        # If the first endpoint already lies on a curve, guide the second
        # endpoint along the tangent at that exact first contact.
        first_entity = next(
            (
                entity for entity in self._sketch_entities
                if entity.get("type") == "point"
                and hypot(
                    float(entity.get("x", 0.0)) - first[0],
                    float(entity.get("y", 0.0)) - first[1],
                ) <= 1.0e-9
            ),
            None,
        )
        first_attachment = (
            first_entity.get("curve_attachment")
            if first_entity is not None else None
        )
        first_tangent_curve_id = ""
        first_tangent_direction: tuple[float, float] | None = None
        # A circle's explicit rim point and an arc's native end points do not
        # need a separate curve_attachment. They are nevertheless valid
        # tangent origins and must produce the same live T candidate as an
        # ordinary point attached to the curve.
        if first_entity is not None and not isinstance(first_attachment, dict):
            first_id = str(first_entity.get("id", ""))
            circular_curve = next(
                (
                    entity for entity in self._sketch_entities
                    if (
                        entity.get("type") == "circle"
                        and isinstance(entity.get("rim_coincident"), dict)
                        and str(entity["rim_coincident"].get("point_id", ""))
                        == first_id
                    )
                    or (
                        entity.get("type") == "arc"
                        and first_id in tuple(
                            map(str, entity.get("point_ids", ())[1:3])
                        )
                    )
                ),
                None,
            )
            circular_ids = (
                tuple(map(str, circular_curve.get("point_ids", ())))
                if circular_curve is not None else ()
            )
            if circular_ids and circular_ids[0] in points:
                center = points[circular_ids[0]]
                first_attachment = {
                    "type": str(circular_curve.get("type", "circle")),
                    "geometry_id": str(circular_curve.get("id", "")),
                    "angle": atan2(first[1] - center[1], first[0] - center[0]),
                }

        # Elliptical-arc end points are native points of the arc and do not
        # need a separate curve_attachment for the arc itself.  They still
        # must be valid tangent origins, so derive the attachment parameter
        # from the endpoint geometry when drawing starts there.
        if first_entity is not None and not isinstance(first_attachment, dict):
            first_id = str(first_entity.get("id", ""))
            endpoint_curve = next(
                (
                    entity for entity in self._sketch_entities
                    if entity.get("type") == "elliptical_arc"
                    and first_id in tuple(
                        map(str, entity.get("point_ids", ())[3:5])
                    )
                ),
                None,
            )
            endpoint_ids = (
                tuple(map(str, endpoint_curve.get("point_ids", ())))
                if endpoint_curve is not None else ()
            )
            if (
                len(endpoint_ids) >= 5
                and all(point_id in points for point_id in endpoint_ids[:3])
            ):
                center, major, minor = (
                    points[point_id] for point_id in endpoint_ids[:3]
                )
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                determinant = ax * by - ay * bx
                if abs(determinant) > 1.0e-12:
                    px, py = first
                    cosine = (
                        (px - center[0]) * by
                        - (py - center[1]) * bx
                    ) / determinant
                    sine = (
                        ax * (py - center[1])
                        - ay * (px - center[0])
                    ) / determinant
                    first_attachment = {
                        "type": "elliptical_arc",
                        "geometry_id": str(endpoint_curve.get("id", "")),
                        "angle": atan2(sine, cosine),
                    }
        # The two semi-axis endpoints are native K points of an ellipse.
        # They have no curve_attachment because they define the curve itself,
        # but a line leaving either point must still receive the same C + T
        # tangent guide as a separately attached point on the ellipse.
        if first_entity is not None and not isinstance(first_attachment, dict):
            first_id = str(first_entity.get("id", ""))
            axis_curve = next((
                entity for entity in self._sketch_entities
                if entity.get("type") in ("ellipse", "elliptical_arc")
                and first_id in tuple(
                    map(str, entity.get("point_ids", ())[1:3])
                )
            ), None)
            axis_ids = (
                tuple(map(str, axis_curve.get("point_ids", ())))
                if axis_curve is not None else ()
            )
            if len(axis_ids) >= 3:
                first_attachment = {
                    "type": str(axis_curve.get("type", "ellipse")),
                    "geometry_id": str(axis_curve.get("id", "")),
                    "angle": 0.0 if first_id == axis_ids[1] else pi * 0.5,
                    "locked": True,
                }
        if isinstance(first_attachment, dict):
            curve_id = str(first_attachment.get("geometry_id", ""))
            curve = next(
                (
                    entity for entity in self._sketch_entities
                    if str(entity.get("id", "")) == curve_id
                    and entity.get("type") in (
                        "circle", "arc", "ellipse", "elliptical_arc", "spline",
                    )
                ),
                None,
            )
            ids = (
                tuple(map(str, curve.get("point_ids", ())))
                if curve is not None else ()
            )
            tangent_direction = None
            if (
                curve is not None
                and curve.get("type") in ("circle", "arc")
                and ids
                and ids[0] in points
            ):
                center = points[ids[0]]
                tangent_direction = (
                    -(first[1] - center[1]),
                    first[0] - center[0],
                )
            elif (
                curve is not None
                and curve.get("type") in ("ellipse", "elliptical_arc")
                and len(ids) >= 3
                and all(pid in points for pid in ids[:3])
            ):
                center, major, minor = (points[pid] for pid in ids[:3])
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                angle = float(first_attachment.get("angle", 0.0))
                tangent_direction = (
                    -ax * sin(angle) + bx * cos(angle),
                    -ay * sin(angle) + by * cos(angle),
                )
            elif curve is not None and curve.get("type") == "spline":
                if len(ids) >= 2 and all(point_id in points for point_id in ids):
                    sampled = _interpolated_spline_points(
                        tuple(points[point_id] for point_id in ids),
                        stored_spline_tangent(curve, "start_tangent"),
                        stored_spline_tangent(curve, "end_tangent"),
                    )
                    fraction = max(0.0, min(1.0, float(first_attachment.get("fraction", 0.0))))
                    index = min(len(sampled) - 1, max(0, round(fraction * (len(sampled) - 1))))
                    lower, upper = max(0, index - 1), min(len(sampled) - 1, index + 1)
                    if upper > lower:
                        tangent_direction = (
                            sampled[upper][0] - sampled[lower][0],
                            sampled[upper][1] - sampled[lower][1],
                        )
            if tangent_direction is not None:
                first_tangent_curve_id = curve_id
                first_tangent_direction = tangent_direction
                guide_end = (
                    first[0] + tangent_direction[0],
                    first[1] + tangent_direction[1],
                )
                guide_end_screen = self._screen_point(
                    self._camera_point(self._sketch_world_point(guide_end))
                )
                guide_dx = guide_end_screen.x() - first_screen.x()
                guide_dy = guide_end_screen.y() - first_screen.y()
                guide_length_squared = guide_dx * guide_dx + guide_dy * guide_dy
                if guide_length_squared > 1.0e-12:
                    factor = (
                        cursor_dx * guide_dx + cursor_dy * guide_dy
                    ) / guide_length_squared
                    guide_distance = abs(
                        cursor_dx * guide_dy - cursor_dy * guide_dx
                    ) / sqrt(guide_length_squared)
                    if abs(factor) > 0.05 and guide_distance <= 14.0:
                        candidates.append((
                            guide_distance,
                            (
                                first[0] + factor * tangent_direction[0],
                                first[1] + factor * tangent_direction[1],
                            ),
                            None,
                            f"tangent_first:{curve_id}",
                        ))
        for curve in self._sketch_entities:
            curve_type = str(curve.get("type", ""))
            if curve_type not in (
                "circle", "arc", "ellipse", "elliptical_arc", "spline",
            ):
                continue
            curve_id = str(curve.get("id", ""))
            ids = tuple(map(str, curve.get("point_ids", ())))
            if not curve_id or not ids or ids[0] not in points:
                continue
            if curve_type == "spline":
                if len(ids) < 2 or any(point_id not in points for point_id in ids):
                    continue
                sampled = _interpolated_spline_points(
                    tuple(points[point_id] for point_id in ids),
                    stored_spline_tangent(curve, "start_tangent"),
                    stored_spline_tangent(curve, "end_tangent"),
                )
                screens = tuple(
                    self._screen_point(self._camera_point(self._sketch_world_point(point)))
                    for point in sampled
                )
                index = min(range(len(screens)), key=lambda candidate: hypot(
                    position.x() - screens[candidate].x(), position.y() - screens[candidate].y()
                ))
                contact_distance = hypot(position.x() - screens[index].x(), position.y() - screens[index].y())
                if contact_distance > 16.0:
                    continue
                lower, upper = max(0, index - 1), min(len(sampled) - 1, index + 1)
                tangent = (
                    sampled[upper][0] - sampled[lower][0],
                    sampled[upper][1] - sampled[lower][1],
                )
                line = (sampled[index][0] - first[0], sampled[index][1] - first[1])
                scale = hypot(*tangent) * hypot(*line)
                if scale <= 1.0e-12 or abs(line[0] * tangent[1] - line[1] * tangent[0]) / scale > 0.035:
                    continue
                candidates.append((
                    contact_distance,
                    sampled[index],
                    f"sketch_spline:{curve_id}",
                    f"tangent:{curve_id}",
                ))
                continue
            center = points[ids[0]]
            tangent_points: tuple[tuple[float, float], ...] = ()
            if curve_type in ("circle", "arc"):
                radius = float(curve.get("radius", 0.0))
                if (
                    curve_type == "arc" and len(ids) >= 2
                    and ids[1] in points and radius <= 1.0e-12
                ):
                    radius = hypot(
                        points[ids[1]][0] - center[0],
                        points[ids[1]][1] - center[1],
                    )
                px, py = first[0] - center[0], first[1] - center[1]
                distance_squared = px * px + py * py
                if radius <= 1.0e-12 or distance_squared <= radius * radius + 1.0e-12:
                    continue
                along = radius * radius / distance_squared
                across = (
                    radius
                    * sqrt(max(0.0, distance_squared - radius * radius))
                    / distance_squared
                )
                tangent_points = (
                    (
                        center[0] + along * px - across * py,
                        center[1] + along * py + across * px,
                    ),
                    (
                        center[0] + along * px + across * py,
                        center[1] + along * py - across * px,
                    ),
                )
            else:
                required = 3 if curve_type == "ellipse" else 5
                if len(ids) < required or any(pid not in points for pid in ids[:required]):
                    continue
                major, minor = points[ids[1]], points[ids[2]]
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                determinant = ax * by - ay * bx
                if abs(determinant) <= 1.0e-12:
                    continue
                px, py = first[0] - center[0], first[1] - center[1]
                local_u = (px * by - py * bx) / determinant
                local_v = (ax * py - ay * px) / determinant
                local_distance = hypot(local_u, local_v)
                if local_distance <= 1.0 + 1.0e-12:
                    continue
                phase = atan2(local_v, local_u)
                offset = acos(max(-1.0, min(1.0, 1.0 / local_distance)))
                tangent_points = tuple(
                    (
                        center[0] + ax * cos(angle) + bx * sin(angle),
                        center[1] + ay * cos(angle) + by * sin(angle),
                    )
                    for angle in (phase - offset, phase + offset)
                )
            reference_id = f"sketch_{curve_type}:{curve_id}"
            for tangent_point in tangent_points:
                if curve_type == "arc":
                    domain = center_arc_points(
                        points[ids[0]], points[ids[1]], points[ids[2]],
                        segments=256,
                        clockwise=bool(curve.get("clockwise", False)),
                    )
                    if min(
                        (hypot(point[0] - tangent_point[0], point[1] - tangent_point[1])
                         for point in domain),
                        default=float("inf"),
                    ) > max(1.0e-5, radius * 1.0e-4):
                        continue
                elif curve_type == "elliptical_arc":
                    def ellipse_parameter(point):
                        px, py = point[0] - center[0], point[1] - center[1]
                        return atan2(
                            (ax * py - ay * px) / determinant,
                            (px * by - py * bx) / determinant,
                        )

                    start_angle = ellipse_parameter(points[ids[3]])
                    end_angle = ellipse_parameter(points[ids[4]])
                    tangent_angle = ellipse_parameter(tangent_point)
                    if bool(curve.get("clockwise", False)):
                        sweep = (start_angle - end_angle) % (2.0 * pi)
                        position_on_arc = (
                            start_angle - tangent_angle
                        ) % (2.0 * pi)
                    else:
                        sweep = (end_angle - start_angle) % (2.0 * pi)
                        position_on_arc = (
                            tangent_angle - start_angle
                        ) % (2.0 * pi)
                    if position_on_arc > sweep + 1.0e-7:
                        continue
                tangent_screen = self._screen_point(
                    self._camera_point(
                        self._sketch_world_point(tangent_point)
                    )
                )
                # A target-curve tangent is a local snap candidate.  The
                # infinite guide from the first endpoint through the tangent
                # point may pass close to the cursor anywhere in the view;
                # do not activate it until the cursor is also over the actual
                # contact on the curve.
                if hypot(
                    position.x() - tangent_screen.x(),
                    position.y() - tangent_screen.y(),
                ) > 16.0:
                    continue
                guide_dx = tangent_screen.x() - first_screen.x()
                guide_dy = tangent_screen.y() - first_screen.y()
                guide_length = hypot(guide_dx, guide_dy)
                if guide_length <= 1.0e-9:
                    continue
                projection = (
                    cursor_dx * guide_dx + cursor_dy * guide_dy
                ) / (guide_length * guide_length)
                if projection <= 0.05:
                    continue
                guide_distance = abs(
                    cursor_dx * guide_dy - cursor_dy * guide_dx
                ) / guide_length
                if guide_distance <= 14.0:
                    tangent_constraint = f"tangent:{curve_id}"
                    if (
                        first_tangent_curve_id
                        and first_tangent_curve_id != curve_id
                        and first_tangent_direction is not None
                    ):
                        line_direction = (
                            tangent_point[0] - first[0],
                            tangent_point[1] - first[1],
                        )
                        direction_scale = max(
                            hypot(*line_direction)
                            * hypot(*first_tangent_direction),
                            1.0e-12,
                        )
                        parallel_error = abs(
                            line_direction[0] * first_tangent_direction[1]
                            - line_direction[1] * first_tangent_direction[0]
                        ) / direction_scale
                        if parallel_error <= 1.0e-6:
                            direction_constraint = (
                                "horizontal"
                                if abs(line_direction[1])
                                <= max(1.0e-9, abs(line_direction[0]) * 1.0e-7)
                                else "vertical"
                                if abs(line_direction[0])
                                <= max(1.0e-9, abs(line_direction[1]) * 1.0e-7)
                                else ""
                            )
                            tangent_constraint = (
                                f"tangent_both:{first_tangent_curve_id}:"
                                f"{curve_id}:{direction_constraint}"
                            )
                    candidates.append((
                        guide_distance,
                        tangent_point,
                        reference_id,
                        tangent_constraint,
                    ))
        return tuple(sorted(candidates, key=lambda item: item[0]))

    def _sketch_perpendicular_placement_candidates(
        self,
        position: QPointF,
    ) -> tuple[tuple[float, tuple[float, float], str], ...]:
        """Guide a new line normally away from geometry at its first point."""
        if (
            self._sketch_tool not in ("segment", "construction", "polyline")
            or len(self._sketch_pending_points) != 1
        ):
            return ()
        start = self._sketch_pending_points[0]
        local = self._sketch_local_position(position)
        if local is None:
            return ()
        points = {
            str(entity.get("id", "")): (
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            )
            for entity in self._sketch_entities
            if entity.get("type") == "point"
        }
        start_screen = self._screen_point(
            self._camera_point(self._sketch_world_point(start))
        )
        candidates: list[tuple[float, tuple[float, float], str]] = []
        for geometry in self._sketch_entities:
            if geometry.get("type") not in ("segment", "construction"):
                continue
            geometry_id = str(geometry.get("id", ""))
            ids = tuple(map(str, geometry.get("point_ids", ())))
            if (
                not geometry_id
                or len(ids) != 2
                or any(point_id not in points for point_id in ids)
            ):
                continue
            first, second = points[ids[0]], points[ids[1]]
            dx = second[0] - first[0]
            dy = second[1] - first[1]
            length_squared = dx * dx + dy * dy
            if length_squared <= 1.0e-24:
                continue
            start_factor = (
                (start[0] - first[0]) * dx
                + (start[1] - first[1]) * dy
            ) / length_squared
            line_distance = abs(
                (start[0] - first[0]) * dy
                - (start[1] - first[1]) * dx
            ) / sqrt(length_squared)
            attached_to_geometry = any(
                hypot(endpoint[0] - start[0], endpoint[1] - start[1])
                <= 1.0e-9
                for endpoint in (first, second)
            ) or any(
                entity.get("type") == "point"
                and hypot(
                    float(entity.get("x", 0.0)) - start[0],
                    float(entity.get("y", 0.0)) - start[1],
                )
                <= 1.0e-9
                and any(
                    isinstance(constraint, dict)
                    and constraint.get("type") in ("midpoint", "point_on_line")
                    and tuple(map(str, constraint.get("point_ids", ()))) == ids
                    for constraint in entity.get("constraints", ())
                )
                for entity in self._sketch_entities
            )
            if (
                not attached_to_geometry
                or line_distance > 1.0e-7
                or (
                    geometry.get("type") == "segment"
                    and not -1.0e-9 <= start_factor <= 1.0 + 1.0e-9
                )
            ):
                continue
            normal = (-dy, dx)
            factor = (
                (local[0] - start[0]) * normal[0]
                + (local[1] - start[1]) * normal[1]
            ) / length_squared
            if abs(factor) <= 1.0e-5:
                continue
            guided = (
                start[0] + factor * normal[0],
                start[1] + factor * normal[1],
            )
            guided_screen = self._screen_point(
                self._camera_point(self._sketch_world_point(guided))
            )
            distance = hypot(
                position.x() - guided_screen.x(),
                position.y() - guided_screen.y(),
            )
            if distance <= 14.0 and hypot(
                guided_screen.x() - start_screen.x(),
                guided_screen.y() - start_screen.y(),
            ) >= 20.0:
                candidates.append((distance, guided, geometry_id))
        return tuple(sorted(candidates, key=lambda item: item[0]))

    def _sketch_reference_direction_snap(
        self,
        reference_id: str,
        constraint: str,
        snapped: tuple[float, float],
    ) -> tuple[float, float] | None:
        if not self._sketch_pending_points:
            return None
        first = self._sketch_pending_points[-1]
        curve_prefixes = {
            "sketch_circle:": "circle",
            "sketch_arc:": "arc",
            "sketch_ellipse:": "ellipse",
            "sketch_elliptical_arc:": "elliptical_arc",
        }
        curve_type = next(
            (
                geometry_type
                for prefix, geometry_type in curve_prefixes.items()
                if reference_id.startswith(prefix)
            ),
            None,
        )
        if curve_type is not None:
            geometry_id = reference_id.split(":", 1)[1]
            points = {
                str(entity.get("id", "")): (
                    float(entity.get("x", 0.0)),
                    float(entity.get("y", 0.0)),
                )
                for entity in self._sketch_entities
                if entity.get("type") == "point"
            }
            curve = next(
                (
                    entity for entity in self._sketch_entities
                    if str(entity.get("id", "")) == geometry_id
                    and entity.get("type") == curve_type
                ),
                None,
            )
            ids = (
                tuple(map(str, curve.get("point_ids", ())))
                if curve is not None
                else ()
            )
            intersections: list[tuple[float, float]] = []
            if constraint.startswith(("parallel:", "perpendicular:", "tangent:")):
                source_id = constraint.split(":", 1)[1]
                source = next(
                    (
                        entity for entity in self._sketch_entities
                        if str(entity.get("id", "")) == source_id
                        and entity.get("type") in (
                            "segment", "construction", "arc",
                        )
                    ),
                    None,
                )
                source_ids = (
                    tuple(map(str, source.get("point_ids", ())))
                    if source is not None else ()
                )
                sampled: list[tuple[float, float]] = []
                if (
                    source is not None
                    and source.get("type") == "arc"
                    and len(source_ids) == 3
                    and all(pid in points for pid in source_ids)
                ):
                    center = points[source_ids[0]]
                    radial = (
                        first[0] - center[0], first[1] - center[1],
                    )
                    direction = (
                        (radial[1], -radial[0])
                        if source.get("clockwise")
                        else (-radial[1], radial[0])
                    )
                    sampled = []
                    if curve_type == "circle" and ids and ids[0] in points:
                        center = points[ids[0]]
                        radius = float(curve.get("radius", 0.0))
                        sampled = [
                            (center[0] + radius * cos(2.0 * pi * index / 256),
                             center[1] + radius * sin(2.0 * pi * index / 256))
                            for index in range(257)
                        ]
                    elif curve_type == "arc" and len(ids) == 3 and all(pid in points for pid in ids):
                        sampled = center_arc_points(
                            points[ids[0]], points[ids[1]], points[ids[2]],
                            segments=256,
                            clockwise=bool(curve.get("clockwise", False)),
                        )
                    elif curve_type == "ellipse" and len(ids) >= 3 and all(pid in points for pid in ids[:3]):
                        sampled = ellipse_points(
                            *(points[pid] for pid in ids[:3]), segments=256
                        )
                    elif curve_type == "elliptical_arc" and len(ids) >= 5 and all(pid in points for pid in ids[:5]):
                        sampled = elliptical_arc_points(
                            *(points[pid] for pid in ids[:5]),
                            clockwise=bool(curve.get("clockwise", False)),
                            segments=256,
                        )
                elif len(source_ids) == 2 and all(pid in points for pid in source_ids):
                    direction = (
                        points[source_ids[1]][0] - points[source_ids[0]][0],
                        points[source_ids[1]][1] - points[source_ids[0]][1],
                    )
                    if constraint.startswith("perpendicular:"):
                        direction = (-direction[1], direction[0])
                    if curve_type == "circle" and ids and ids[0] in points:
                        center = points[ids[0]]
                        radius = float(curve.get("radius", 0.0))
                        sampled = [
                            (
                                center[0] + radius * cos(2.0 * pi * index / 256),
                                center[1] + radius * sin(2.0 * pi * index / 256),
                            )
                            for index in range(257)
                        ]
                    elif curve_type == "arc" and len(ids) == 3 and all(pid in points for pid in ids):
                        sampled = center_arc_points(
                            points[ids[0]], points[ids[1]], points[ids[2]],
                            segments=256,
                            clockwise=bool(curve.get("clockwise", False)),
                        )
                    elif curve_type == "ellipse" and len(ids) >= 3 and all(pid in points for pid in ids[:3]):
                        sampled = ellipse_points(
                            *(points[pid] for pid in ids[:3]), segments=256
                        )
                    elif curve_type == "elliptical_arc" and len(ids) >= 5 and all(pid in points for pid in ids[:5]):
                        sampled = elliptical_arc_points(
                            *(points[pid] for pid in ids[:5]),
                            clockwise=bool(curve.get("clockwise", False)),
                            segments=256,
                        )
                    gx, gy = first
                    dx, dy = direction
                    for start, end in zip(sampled, sampled[1:]):
                        sx, sy = start
                        ex, ey = end[0] - sx, end[1] - sy
                        denominator = dx * ey - dy * ex
                        if abs(denominator) <= 1.0e-12:
                            continue
                        offset_x, offset_y = sx - gx, sy - gy
                        curve_factor = (offset_x * dy - offset_y * dx) / denominator
                        if 0.0 <= curve_factor <= 1.0:
                            intersections.append((
                                sx + curve_factor * ex,
                                sy + curve_factor * ey,
                            ))
                return (
                    min(
                        intersections,
                        key=lambda point: hypot(
                            point[0] - snapped[0], point[1] - snapped[1]
                        ),
                    )
                    if intersections else None
                )
            if curve_type == "circle" and ids and ids[0] in points:
                center = points[ids[0]]
                radius = float(curve.get("radius", 0.0))
                fixed_delta = (
                    first[1] - center[1]
                    if constraint == "horizontal"
                    else first[0] - center[0]
                )
                remainder = radius * radius - fixed_delta * fixed_delta
                if remainder >= -1.0e-12:
                    offset = sqrt(max(0.0, remainder))
                    intersections = (
                        [
                            (center[0] - offset, first[1]),
                            (center[0] + offset, first[1]),
                        ]
                        if constraint == "horizontal"
                        else [
                            (first[0], center[1] - offset),
                            (first[0], center[1] + offset),
                        ]
                    )
            elif (
                curve_type == "ellipse"
                and len(ids) >= 3
                and all(point_id in points for point_id in ids[:3])
            ):
                center, major, minor = (points[point_id] for point_id in ids[:3])
                ax, ay = major[0] - center[0], major[1] - center[1]
                bx, by = minor[0] - center[0], minor[1] - center[1]
                alpha, beta, target = (
                    (ay, by, first[1] - center[1])
                    if constraint == "horizontal"
                    else (ax, bx, first[0] - center[0])
                )
                amplitude = hypot(alpha, beta)
                if amplitude > 1.0e-12 and abs(target) <= amplitude + 1.0e-12:
                    phase = atan2(beta, alpha)
                    delta = acos(max(-1.0, min(1.0, target / amplitude)))
                    for angle in (phase - delta, phase + delta):
                        point = (
                            center[0] + ax * cos(angle) + bx * sin(angle),
                            center[1] + ay * cos(angle) + by * sin(angle),
                        )
                        intersections.append(
                            (point[0], first[1])
                            if constraint == "horizontal"
                            else (first[0], point[1])
                        )
            else:
                sampled: tuple[tuple[float, float], ...] | list[tuple[float, float]] = ()
                if curve_type == "arc" and len(ids) == 3 and all(pid in points for pid in ids):
                    sampled = center_arc_points(
                        points[ids[0]], points[ids[1]], points[ids[2]],
                        segments=256,
                        clockwise=bool(curve.get("clockwise", False)),
                    )
                elif curve_type == "elliptical_arc" and len(ids) >= 5 and all(pid in points for pid in ids[:5]):
                    sampled = elliptical_arc_points(
                        *(points[pid] for pid in ids[:5]),
                        clockwise=bool(curve.get("clockwise", False)),
                        segments=256,
                    )
                fixed_axis = 1 if constraint == "horizontal" else 0
                target = first[fixed_axis]
                for start, end in zip(sampled, sampled[1:]):
                    delta = end[fixed_axis] - start[fixed_axis]
                    if abs(delta) <= 1.0e-12:
                        continue
                    factor = (target - start[fixed_axis]) / delta
                    if 0.0 <= factor <= 1.0:
                        point = (
                            start[0] + factor * (end[0] - start[0]),
                            start[1] + factor * (end[1] - start[1]),
                        )
                        intersections.append(point)
            return (
                min(
                    intersections,
                    key=lambda point: hypot(
                        point[0] - snapped[0],
                        point[1] - snapped[1],
                    ),
                )
                if intersections
                else None
            )
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
        guide_direction = None
        if constraint.startswith(("parallel:", "perpendicular:", "tangent:")):
            source_id = constraint.split(":", 1)[1]
            point_positions = {
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
                    if str(entity.get("id", "")) == source_id
                    and entity.get("type") in (
                        "segment", "construction", "arc",
                    )
                ),
                None,
            )
            source_ids = (
                tuple(map(str, source.get("point_ids", ())))
                if source is not None
                else ()
            )
            if (
                source is not None
                and source.get("type") == "arc"
                and len(source_ids) == 3
                and all(point_id in point_positions for point_id in source_ids)
            ):
                center = point_positions[source_ids[0]]
                radial = (first[0] - center[0], first[1] - center[1])
                guide_direction = (
                    (radial[1], -radial[0])
                    if source.get("clockwise")
                    else (-radial[1], radial[0])
                )
            elif len(source_ids) == 2 and all(
                point_id in point_positions for point_id in source_ids
            ):
                guide_direction = (
                    point_positions[source_ids[1]][0]
                    - point_positions[source_ids[0]][0],
                    point_positions[source_ids[1]][1]
                    - point_positions[source_ids[0]][1],
                )
                if constraint.startswith("perpendicular:"):
                    guide_direction = (-guide_direction[1], guide_direction[0])
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
            elif guide_direction is not None:
                gx, gy = guide_direction
                denominator = gx * dy - gy * dx
                if abs(denominator) <= 1.0e-12:
                    continue
                offset_x = px - first[0]
                offset_y = py - first[1]
                guide_factor = (offset_x * dy - offset_y * dx) / denominator
                line_factor = (offset_x * gy - offset_y * gx) / denominator
                if not bool(line.get("bounded", False)) or 0.0 <= line_factor <= 1.0:
                    candidates.append((
                        first[0] + guide_factor * gx,
                        first[1] + guide_factor * gy,
                    ))
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

    def _sketch_rectangle_axis_candidate(
        self,
        position: QPointF,
    ) -> str | None:
        construction_ids = {
            str(entity.get("id", ""))
            for entity in self._sketch_entities
            if entity.get("type") == "construction"
        }
        return next((
            candidate
            for candidate in self._sketch_selection_candidates(position)
            if candidate in construction_ids
        ), None)

    def _sketch_external_reference_candidate(
        self,
        position: QPointF,
    ) -> tuple[str, tuple[float, float]] | None:
        tolerance = 12.0
        nearest: tuple[float, str, tuple[float, float]] | None = None
        nearest_external_point: (
            tuple[float, str, tuple[float, float]] | None
        ) = None

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
            if bool(raw_line.get("bounded", False)):
                factor = max(0.0, min(1.0, factor))
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
        # External geometry is considered before the sketch axes.  The
        # nearest-candidate comparison intentionally keeps the first item on
        # an equal distance, so this order makes an external line selectable
        # even when it lies exactly on a main axis.
        for reference in self._sketch_external_references:
            reference_id = str(reference.get("id", ""))
            geometry = reference.get("geometry")
            if not reference_id or not isinstance(geometry, dict):
                continue
            geometry_type = geometry.get("type")
            if geometry_type == "line":
                consider_line(reference_id, geometry)
            elif geometry_type == "polyline":
                points = geometry.get("points", ())
                if isinstance(points, (list, tuple)):
                    for first, second in zip(points, points[1:]):
                        if not (
                            isinstance(first, (list, tuple))
                            and isinstance(second, (list, tuple))
                            and len(first) >= 2
                            and len(second) >= 2
                        ):
                            continue
                        consider_line(reference_id, {
                            "point": first,
                            "direction": (
                                float(second[0]) - float(first[0]),
                                float(second[1]) - float(first[1]),
                            ),
                            "bounded": True,
                        })
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
            elif geometry_type in ("point", "axis_point"):
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
                    and (
                        nearest_external_point is None
                        or distance < nearest_external_point[0]
                    )
                ):
                    nearest_external_point = (
                        distance,
                        reference_id,
                        snapped,
                    )
        # A finite external point is a more specific target than an infinite
        # axis (or the sketch origin).  Once the cursor is inside its marker,
        # keep it selectable even when both project to exactly the same place.
        if nearest_external_point is not None:
            return (
                nearest_external_point[1],
                nearest_external_point[2],
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
        if active_candidate.startswith("constraint:"):
            _prefix, owner_id, raw_index = active_candidate.split(":", 2)
            self._preview_sketch_entity_id = None
            self._hovered_sketch_constraint = (owner_id, int(raw_index))
            if self._hovered_sketch_external_reference_id is not None:
                self._hovered_sketch_external_reference_id = None
                self.sketchReferenceHovered.emit("")
        elif active_candidate.startswith("reference:"):
            self._preview_sketch_entity_id = None
            self._hovered_sketch_constraint = None
            reference_id = active_candidate.removeprefix("reference:")
            self._hovered_sketch_external_reference_id = reference_id
            self.sketchReferenceHovered.emit(reference_id)
        else:
            self._preview_sketch_entity_id = active_candidate
            self._hovered_sketch_constraint = None
            if self._hovered_sketch_external_reference_id is not None:
                self._hovered_sketch_external_reference_id = None
                self.sketchReferenceHovered.emit("")
        self.update()

    def _sketch_selection_candidates(
        self,
        position: QPointF,
    ) -> tuple[str, ...]:
        candidates = list(self._sketch_entity_candidates(position))
        if (
            self._sketch_tool == "dimension"
            and len(self._selected_sketch_entity_ids) == 1
        ):
            selected_id = next(iter(self._selected_sketch_entity_ids))
            entity_types = {
                str(entity.get("id", "")): str(entity.get("type", ""))
                for entity in self._sketch_entities
            }
            if entity_types.get(selected_id) in (
                "segment",
                "construction",
            ):
                # At a shared vertex the point normally wins hit-testing.
                # For the second arm of an angular dimension, prefer the
                # other line.  Keep the selected line behind the point so a
                # point-to-line dimension remains available where no second
                # line is present.
                candidates.sort(
                    key=lambda candidate: (
                        0
                        if candidate != selected_id
                        and entity_types.get(candidate)
                        in ("segment", "construction")
                        else 2
                        if candidate == selected_id
                        else 1
                    )
                )
        candidates.extend(
            f"constraint:{owner_id}:{constraint_index}"
            for bounds, owner_id, constraint_index
            in self._sketch_constraint_hit_regions
            if bounds.adjusted(-5.0, -5.0, 5.0, 5.0).contains(position)
        )
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
                    # Object selection deliberately reveals tangent blend
                    # boundaries which the normal shaded-with-edges mode
                    # hides.  On a filleted cylinder these are the circular
                    # curves at both ends of the radius and provide the only
                    # clear visual description of the selected Body.
                    or edge.topology_role in {"seam", "periodic_tangent"}
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
            # A fully blended body may have no sharp topology edge on its
            # visible outline.  In that case an object highlight made only
            # from CAD edges disappears completely.  Include the owner-scoped
            # tessellation silhouette so Body remains highlightable even when
            # every boundary has been filleted smooth.
            view_direction = self._inverse_rotate((0.0, 0.0, 1.0))
            silhouettes = tuple(
                edge
                for edge in self._silhouette_edges
                if edge.owner_id == owner_id
            )
            for first, second in silhouette_segments_from_edges(
                silhouettes,
                view_direction,
            ):
                painter.drawLine(
                    self._screen_point(self._camera_point(first)),
                    self._screen_point(self._camera_point(second)),
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
                or key in self._edge_treatment_selection_edges
                or key in self._feature_selected_edges
                or key in self._constraint_reference_edges
                or key in self._assembly_reference_edges
                or edge.owner_id in {
                    self._selected_reference_owner_id,
                }
                or edge.owner_id in self._constraint_reference_owner_ids
                or edge.owner_id in self._selected_container_content_ids
            ):
                color = QColor.fromRgbF(0.0, 0.82, 1.0)
            if (
                key in self._feature_hover_edges
                and key not in self._feature_selected_edges
            ):
                color = QColor.fromRgbF(1.0, 0.48, 0.0)
            if color is None or edge.element_kind not in {
                "axis",
                "centerline",
                "edge",
                "sketch",
            }:
                continue
            if (
                not edge_visible_in_display(edge, self._display_mode)
                and key not in self._edge_treatment_selection_edges
                and key not in self._feature_hover_edges
                and key not in self._feature_selected_edges
            ):
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
        unique_candidates = tuple(dict.fromkeys(candidates))
        return tuple(
            candidate for candidate in unique_candidates
            if self._topology_owner_is_selectable(candidate[1])
        )

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
            if not self._point_marker_is_selectable(marker.element_kind):
                continue
            screen = self._screen_point(self._camera_point(marker.position))
            if hypot(position.x() - screen.x(), position.y() - screen.y()) <= threshold:
                candidates.append(("point", marker.owner_id, marker.point_index))
        for edge in mesh.edges:
            if (
                not edge_visible_in_display(edge, self._display_mode)
                and not (
                    self._selection_filter == "edge"
                    and edge.element_kind == "edge"
                    and edge.topology_role not in {"seam", "periodic_tangent"}
                )
            ):
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
        sample_offsets = (
            (0.0, 0.0),
            (-4.0, 0.0),
            (4.0, 0.0),
            (0.0, -4.0),
            (0.0, 4.0),
        )
        for offset_x, offset_y in sample_offsets:
            candidates.extend(
                ("face", owner_id, face_index)
                for _depth, owner_id, face_index in self._face_hits(
                    QPointF(
                        position.x() + offset_x,
                        position.y() + offset_y,
                    ),
                    bounds_tolerance=4.0,
                )
            )
        unique_candidates = tuple(dict.fromkeys(candidates))
        return tuple(
            candidate for candidate in unique_candidates
            if self._topology_owner_is_selectable(candidate[1])
        )

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
        if self._selection_filter not in {"all", "plane", "normal", "surface"}:
            return None
        mesh = self._mesh
        if mesh is None:
            return None
        hits: list[tuple[float, float, str, int]] = []
        threshold = 8.0 * float(self.devicePixelRatioF())
        for plane in mesh.planes:
            if not self._topology_owner_is_selectable(plane.owner_id):
                continue
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
            if (
                marker.element_kind == "vertex"
                and key not in (self._hovered_point, self._selected_point)
            ):
                continue
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
            if not self._topology_owner_is_selectable(marker.owner_id):
                continue
            if not self._point_marker_is_selectable(marker.element_kind):
                continue
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

    def _point_marker_is_selectable(self, element_kind: str) -> bool:
        return (
            element_kind != "vertex"
            or self._sketch_reference_selection_mode
            or self._interaction_mode == "topology"
        )

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

    def _pick_edge(
        self,
        position: QPointF,
        *,
        allowed_element_kinds: frozenset[str] | None = None,
    ) -> TopologyKey | None:
        if (
            allowed_element_kinds is None
            and self._selection_filter not in {"all", "edge"}
        ):
            return None
        mesh = self._mesh
        if mesh is None or mesh.is_empty:
            return None
        candidates: list[tuple[float, float, str, int]] = []
        threshold = 8.0 * float(self.devicePixelRatioF())
        for edge in mesh.edges:
            if (
                allowed_element_kinds is not None
                and edge.element_kind not in allowed_element_kinds
            ):
                continue
            if not self._topology_owner_is_selectable(edge.owner_id):
                continue
            if (
                not edge_visible_in_display(edge, self._display_mode)
                and not (
                    self._selection_filter == "edge"
                    and edge.element_kind == "edge"
                    and edge.topology_role not in {"seam", "periodic_tangent"}
                )
            ):
                continue
            if self._selection_filter == "axis":
                if edge.element_kind not in {"axis", "centerline"}:
                    continue
            elif self._selection_filter not in {"all", "edge"}:
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
        if self._selection_filter not in {"all", "face", "normal", "surface"}:
            return None
        mesh = self._mesh
        if mesh is None or not mesh.triangle_face_indices:
            return None
        cache_key = (
            # Definition/property dialogs add datum overlays by wrapping the
            # unchanged body buffers in a new ViewerMesh.  Keying by the
            # wrapper identity discarded the already warm face picker and
            # made the first placement reference reproject every STEP
            # triangle; the second and third reference then appeared fast.
            id(mesh.triangle_positions),
            id(mesh.triangle_face_indices),
            id(mesh.triangle_owner_ids),
            self.width(),
            self.height(),
            self.camera.yaw_degrees,
            self.camera.pitch_degrees,
            self.camera.roll_degrees,
            self.camera.pan_x,
            self.camera.pan_y,
            self.camera.zoom,
            self._scene_center,
            self._scene_radius,
            self._topology_owner_filter,
            self._excluded_topology_owner_ids,
        )
        self._ensure_face_pick_arrays(cache_key)
        hits = self._face_hits(position)
        if not hits:
            return None
        selected = max(hits)
        return selected[1], selected[2]

    def _ensure_face_pick_arrays(self, cache_key: tuple[Any, ...]) -> None:
        if cache_key == self._face_pick_cache_key:
            return
        mesh = self._mesh
        if mesh is None or not mesh.triangle_face_indices:
            self._face_pick_arrays = {}
            self._face_pick_cache_key = cache_key
            return
        world = np.asarray(mesh.triangle_positions, dtype=np.float64).reshape(
            (-1, 3, 3)
        )
        rotation = np.asarray(
            _camera_rotation_matrix(
                self.camera.yaw_degrees,
                self.camera.pitch_degrees,
                self.camera.roll_degrees,
            ),
            dtype=np.float64,
        )
        camera = (world - np.asarray(self._scene_center)) @ rotation.T
        scale = (
            float(self.height()) * 0.5
            / max(self._scene_radius, 1.0e-12)
            * self.camera.zoom
        )
        screen = np.empty((len(camera), 3, 2), dtype=np.float64)
        screen[:, :, 0] = (
            self.width() * 0.5 + self.camera.pan_x + camera[:, :, 0] * scale
        )
        screen[:, :, 1] = (
            self.height() * 0.5 + self.camera.pan_y - camera[:, :, 1] * scale
        )
        owners = np.asarray(mesh.triangle_owner_ids, dtype=object)
        selectable = np.fromiter(
            (self._topology_owner_is_selectable(str(owner)) for owner in owners),
            dtype=bool,
            count=len(owners),
        )
        self._face_pick_arrays = {
            "screen": screen[selectable],
            "depth": camera[:, :, 2][selectable],
            "owners": owners[selectable],
            "faces": np.asarray(mesh.triangle_face_indices)[selectable],
        }
        self._face_pick_cache = ()
        self._face_pick_cache_key = cache_key

    def _face_hits(
        self,
        position: QPointF,
        *,
        bounds_tolerance: float = 0.0,
    ) -> list[tuple[float, str, int]]:
        if not self._face_pick_arrays:
            self._pick_face(position)
            if not self._face_pick_arrays:
                return []
        screen = self._face_pick_arrays["screen"]
        x = float(position.x())
        y = float(position.y())
        tolerance = float(bounds_tolerance)
        candidate_mask = (
            (screen[:, :, 0].min(axis=1) - tolerance <= x)
            & (screen[:, :, 0].max(axis=1) + tolerance >= x)
            & (screen[:, :, 1].min(axis=1) - tolerance <= y)
            & (screen[:, :, 1].max(axis=1) + tolerance >= y)
        )
        candidate_indices = np.flatnonzero(candidate_mask)
        if not len(candidate_indices):
            return []
        triangles = screen[candidate_indices]
        first = triangles[:, 0]
        second = triangles[:, 1]
        third = triangles[:, 2]
        denominator = (
            (second[:, 1] - third[:, 1])
            * (first[:, 0] - third[:, 0])
            + (third[:, 0] - second[:, 0])
            * (first[:, 1] - third[:, 1])
        )
        valid = np.abs(denominator) > 1.0e-12
        safe_denominator = np.where(valid, denominator, 1.0)
        first_weight = (
            (second[:, 1] - third[:, 1]) * (x - third[:, 0])
            + (third[:, 0] - second[:, 0]) * (y - third[:, 1])
        ) / safe_denominator
        second_weight = (
            (third[:, 1] - first[:, 1]) * (x - third[:, 0])
            + (first[:, 0] - third[:, 0]) * (y - third[:, 1])
        ) / safe_denominator
        third_weight = 1.0 - first_weight - second_weight
        valid &= (
            (first_weight >= -1.0e-8)
            & (second_weight >= -1.0e-8)
            & (third_weight >= -1.0e-8)
        )
        candidate_indices = candidate_indices[valid]
        if not len(candidate_indices):
            return []
        weights = np.column_stack((
            first_weight[valid],
            second_weight[valid],
            third_weight[valid],
        ))
        depths = self._face_pick_arrays["depth"]
        owners = self._face_pick_arrays["owners"]
        faces = self._face_pick_arrays["faces"]
        hit_depths = np.sum(depths[candidate_indices] * weights, axis=1)
        return [
            (float(depth), str(owner), int(face))
            for depth, owner, face in zip(
                hit_depths,
                owners[candidate_indices],
                faces[candidate_indices],
            )
        ]

    def _pick_object(self, position: QPointF) -> str | None:
        excluded = getattr(
            self,
            "_excluded_object_owner_ids",
            frozenset(),
        )
        face = self._pick_face(position)
        if (
            face is not None
            and face[0] not in excluded
        ):
            return face[0]
        edge = self._pick_edge(position)
        return (
            edge[0]
            if edge is not None
            and edge[0] not in excluded
            else None
        )

    def _pick_axis(self, position: QPointF) -> TopologyKey | None:
        # Do not call the unrestricted edge picker and filter its result
        # afterwards.  On a large imported STEP that needlessly projects
        # every model edge for each mouse-enter/move event.
        return self._pick_edge(
            position,
            allowed_element_kinds=frozenset({"axis", "centerline"}),
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

    def set_edge_treatment_selection_edges(
        self,
        edges: set[TopologyKey] | frozenset[TopologyKey],
    ) -> None:
        selected = frozenset(edges)
        if selected == self._edge_treatment_selection_edges:
            return
        self._edge_treatment_selection_edges = selected
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

    def set_feature_hover_edges(
        self,
        edges: set[TopologyKey] | frozenset[TopologyKey],
    ) -> None:
        selected = frozenset(edges)
        if selected == self._feature_hover_edges:
            return
        self._feature_hover_edges = selected
        self.update()

    def set_feature_selected_edges(
        self,
        edges: set[TopologyKey] | frozenset[TopologyKey],
    ) -> None:
        selected = frozenset(edges)
        if selected == self._feature_selected_edges:
            return
        self._feature_selected_edges = selected
        self.update()

    def set_feature_preview_owners(
        self,
        owner_ids: set[str] | frozenset[str],
    ) -> None:
        selected = frozenset(owner_ids)
        if selected == self._feature_preview_owner_ids:
            return
        self._feature_preview_owner_ids = selected
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
        model_view.rotate(self.camera.roll_degrees, 0.0, 0.0, 1.0)
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
        for triangle_index, offset in enumerate(range(0, len(positions), 9)):
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
            triangle_records.append((
                depth,
                polygon,
                normal,
                mesh.triangle_owner_ids[triangle_index],
            ))

        surface_color = self._surface_color
        light = (0.25, -0.35, 0.902)
        painter.setPen(Qt.PenStyle.NoPen)
        for _depth, polygon, normal, owner_id in sorted(
            triangle_records,
            key=lambda record: record[0],
        ):
            surface_color = self._surface_colors_by_owner_id.get(
                owner_id,
                self._surface_color,
            )
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
        for edge in mesh.edges:
            if not edge_visible_in_display(edge, self._display_mode):
                continue
            painter.setPen(QPen(
                QColor("#00D1FF")
                if edge.owner_id in self._feature_preview_owner_ids
                and self._display_mode in {
                    "wire", "hidden_edges", "no_hidden",
                }
                else QColor("#16191E"),
                max(1.0, float(self.devicePixelRatioF())),
            ))
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
        roll = radians(self.camera.roll_degrees)
        yaw_x = cos(yaw) * vector[0] - sin(yaw) * vector[1]
        yaw_y = sin(yaw) * vector[0] + cos(yaw) * vector[1]
        yaw_z = vector[2]
        pitch_x = yaw_x
        pitch_y = cos(pitch) * yaw_y - sin(pitch) * yaw_z
        pitch_z = sin(pitch) * yaw_y + cos(pitch) * yaw_z
        return (
            cos(roll) * pitch_x - sin(roll) * pitch_y,
            sin(roll) * pitch_x + cos(roll) * pitch_y,
            pitch_z,
        )

    def _inverse_rotate(self, vector: Point3) -> Point3:
        yaw = radians(self.camera.yaw_degrees)
        pitch = radians(self.camera.pitch_degrees)
        roll = radians(self.camera.roll_degrees)
        roll_x = cos(roll) * vector[0] + sin(roll) * vector[1]
        roll_y = -sin(roll) * vector[0] + cos(roll) * vector[1]
        roll_z = vector[2]
        pitch_x = roll_x
        pitch_y = cos(pitch) * roll_y + sin(pitch) * roll_z
        pitch_z = -sin(pitch) * roll_y + cos(pitch) * roll_z
        return (
            cos(yaw) * pitch_x + sin(yaw) * pitch_y,
            -sin(yaw) * pitch_x + cos(yaw) * pitch_y,
            pitch_z,
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
