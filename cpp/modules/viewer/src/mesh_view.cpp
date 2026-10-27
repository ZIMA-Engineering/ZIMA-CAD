#include <zima/kernel/transition_edge_display.hpp>
#include <zima/kernel/axis_display.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/state_display.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include "../../../common/datum_display.hpp"
#include "../../../common/interaction_colors.hpp"
#include <zima/kernel/tangent_edge_route.hpp>
#include "../../../common/technical_font.hpp"
#include <zima/viewer/dimension_presentation.hpp>
#include <zima/kernel/annotation_layout.hpp>
#include <zima/viewer/dimension_text_layer.hpp>
#include <QApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <zima/viewer/view_theme.hpp>
#include <zima/viewer/annotation_arrow.hpp>
#include <zima/viewer/infinite_line.hpp>
#include <zima/viewer/sketch_hv.hpp>
#include <zima/viewer/sketch_pick_priority.hpp>
#include <QTimer>
#include <QOpenGLPaintDevice>
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/surface_results.hpp>
#include <zima/viewer/embedded_image.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/viewer/feature_ownership.hpp>
#include <zima/viewer/shading.hpp>
#include <zima/viewer/silhouette.hpp>

#include <QMatrix4x4>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFontMetricsF>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QRectF>
#include <QQuaternion>
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QThreadPool>
#include <cstdio>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zima::viewer {

namespace {

struct RetiredViewerGeometry {
    zima::kernel::ViewerMesh display;
    std::optional<zima::kernel::ViewerMesh> surface_source;
    zima::kernel::ViewerMesh persisted_references;
    zima::kernel::ViewerMesh sheet_container_pick;
    zima::kernel::ViewerMesh sketch_interaction;
    std::optional<zima::viewer::PickingIndex> display_index;
    std::optional<zima::viewer::PickingIndex> reference_index;
    std::optional<zima::viewer::PickingIndex> sheet_index;
};

bool has_viewer_storage(const zima::kernel::ViewerMesh& mesh) {
    return !mesh.vertices.empty() || !mesh.triangles.empty() ||
        !mesh.triangle_references.empty() || !mesh.edges.empty() ||
        !mesh.points.empty() || !mesh.axes.empty() ||
        !mesh.dimensions.empty() || !mesh.constraint_markers.empty() ||
        !mesh.images.empty() || !mesh.annotation_frames.empty() ||
        !mesh.original_references.vertices.empty() ||
        !mesh.original_references.triangles.empty() ||
        !mesh.original_references.edges.empty() ||
        !mesh.original_references.points.empty() ||
        !mesh.original_references.axes.empty();
}

class RelationMarkLayer final : public QWidget {
public:
    std::vector<DimensionTextLabel> labels;
    explicit RelationMarkLayer(QWidget* parent):QWidget(parent) {
        setObjectName("relationMarkLayer");setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::TextAntialiasing);
        for(const auto& label:labels) {
            painter.save();painter.translate(label.baseline);painter.rotate(label.angle);
            painter.setFont(label.font);painter.setPen(label.color);
            painter.drawText(QPointF(-QFontMetricsF(label.font).horizontalAdvance("fx "),0),"fx");painter.restore();
        }
    }
};

bool is_sketch_wire_edge(const zima::kernel::ViewerEdge& edge) {
    const auto& key = edge.reference.semantic_key;
    return key.starts_with("segment:") || key.starts_with("trim_piece:") ||
        key.starts_with("circle:") || key.starts_with("arc:") ||
        key.starts_with("corner_radius:") || key.starts_with("ellipse:") ||
        key.starts_with("elliptical_arc:") || key.starts_with("bspline:") ||
        key.starts_with("text:") || key.starts_with("external_edge:") ||
        key.starts_with("external_axis:") || key.starts_with("external_face:") ||
        key.starts_with("repeat_region:") || key.starts_with("symbol:");
}

template<class Project> void paint_normal_text(QPainter& painter,
    const std::vector<zima::kernel::ViewerEdge>& edges,
    const std::vector<zima::kernel::ViewerEdge>& preview,Project project,bool visible,bool symbols_visible) {
    const auto foreground=view_theme(qApp->palette()).foreground;
    std::map<zima::viewer::EdgeKey,std::pair<QPainterPath,QColor>> paths;
    const auto append=[&](const auto& source,bool transient) {
        for(const auto& edge:source) if(edge.filled_text && !edge.points.empty()) {
            if(!transient && !(edge.reference.semantic_key.starts_with("symbol:")?symbols_visible:visible))continue;
            auto& [path,color]=paths[zima::viewer::edge_key(edge.reference)];
            path.setFillRule(Qt::OddEvenFill);path.moveTo(project(edge.points.front()));
            for(std::size_t i=1;i<edge.points.size();++i)path.lineTo(project(edge.points[i]));
            path.closeSubpath();
            const auto& key=edge.reference.semantic_key;
            color=transient?interaction::selected:!edge.color.empty()?QColor(QString::fromStdString(edge.color)):
                key.ends_with(":red")?QColor(255,0,0):key.ends_with(":yellow")?QColor(245,205,80):key.ends_with(":white")?QColor(255,255,255):QColor(77,216,17);
        }
    };
    append(edges,false);append(preview,true);
    for(const auto& [key,item]:paths)painter.fillPath(item.first,item.second==QColor(Qt::white)?foreground:item.second);
}


constexpr float kPerspectiveFieldOfViewDegrees = 45.0F;

QQuaternion default_camera_orientation() {
    return QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, -45.0F) *
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 215.264F);
}

bool is_screen_constant_plane(const std::string& semantic_key) {
    return semantic_key == "border" || semantic_key == "plane:end" ||
        semantic_key.starts_with("origin:plane:");
}

void draw_circular_marker(QPainter& painter, const QPointF& center,
    const QColor& color, double radius = 4.5) {
    QPolygonF polygon;
    constexpr int segments = 32;
    polygon.reserve(segments);
    for (int segment = 0; segment < segments; ++segment) {
        const double angle = 2.0 * std::numbers::pi * segment / segments;
        polygon.push_back(center + QPointF(
            std::cos(angle) * radius, std::sin(angle) * radius));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(polygon);
}

}  // namespace

struct MeshView::Impl {
    RelationMarkLayer* relation_marks{};
    std::map<std::pair<std::string,std::string>,std::string> dimension_relations;
    zima::kernel::ViewerMesh mesh;
    std::optional<zima::kernel::ViewerMesh> surface_source_mesh;
    bool show_surfaces{true};
    // Picking traverses this compact view on every pointer move. Build it once
    // when the scene changes instead of copying all persisted geometry for
    // every hover sample.
    zima::kernel::ViewerMesh persisted_reference_mesh;
    // Pick-only authored sheet regions. Later coplanar Boolean merging keeps
    // the oldest face identity, so these source skins preserve which Flat or
    // Bend produced a visible area without changing topology or rendering it.
    zima::kernel::ViewerMesh sheet_container_pick_mesh;
    std::optional<PickingIndex> display_picking_index;
    std::optional<PickingIndex> reference_picking_index;
    std::optional<PickingIndex> sheet_container_picking_index;
    struct FaceBoundarySegment {
        kernel::Vec3 first,second;
        std::size_t uses{};
        std::array<QVector3D,2> normals{};
    };
    std::size_t picker_overlay_revision{};
    std::map<EdgeKey,std::vector<std::size_t>> display_face_triangles,original_face_triangles;
    std::map<std::tuple<CandidateGeometry,CandidateKind,EdgeKey>,std::vector<FaceBoundarySegment>> face_boundaries;
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>>
        original_container_edge_indices;
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>>
        edge_treatment_boundary_edge_indices;
    // Active Sketcher picking never needs to traverse the displayed result
    // body's faces/edges. Keep a tiny owner-filtered packet with maps back to
    // the base mesh so candidate identities remain unchanged.
    zima::kernel::ViewerMesh sketch_interaction_mesh;
    std::vector<std::size_t> sketch_edge_indices;
    std::vector<std::size_t> sketch_point_indices;
    std::vector<std::size_t> sketch_axis_indices;
    std::vector<std::size_t> sketch_dimension_indices;
    std::vector<std::size_t> sketch_constraint_indices;
    QOpenGLShaderProgram program;
    QOpenGLBuffer vertices{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer transparent_triangles{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lines{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer selected_source_lines{QOpenGLBuffer::VertexBuffer};
    using SourceWireKey=std::tuple<std::string,std::string,std::size_t>;
    std::optional<SourceWireKey> selected_source_key;
    std::size_t inspection_wire_revision{};
    GLsizei selected_source_vertex_count{};
    bool selected_source_native_sketch{};
    std::vector<float> selected_source_data;
    QOpenGLBuffer silhouette{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vertex_array;
    std::vector<std::pair<GLint, GLsizei>> line_ranges;
    // Mesh edges kept in lock-step with line_ranges (same filtering as
    // upload_mesh's line_data build) purely so paintGL can resolve each
    // edge's highlight-priority colour before its individual draw call.
    std::vector<zima::kernel::ViewerEdge> line_edges;
    // Index in mesh.edges for every line_edges entry. A stable topology
    // reference is not a unique draw identity after an edge is split.
    std::vector<std::size_t> line_mesh_edge_indices;
    GLsizei line_vertex_count{};
    // Candidate internal triangulation edges eligible to become silhouettes
    // (shared by exactly two triangles of the same owning face, not already
    // a real topological edge). Rebuilt only when the mesh changes; the
    // visible segments are re-selected only when that mesh or the exact
    // camera direction changes. The completed line buffer survives repaint,
    // highlighting, pan and zoom.
    struct SilhouetteCandidate {
        QVector3D first;
        QVector3D second;
        QVector3D normal_a;
        QVector3D normal_b;
        bool smooth_face{};
        std::size_t source_triangle{};
        bool operator==(const SilhouetteCandidate&)const=default;
    };
    std::vector<SilhouetteCandidate> silhouette_candidates;
    std::vector<SmoothSilhouetteTriangle> smooth_silhouette_triangles;
    struct SilhouetteSource {
        std::size_t first_triangle{},second_triangle{},first_side{};
        std::size_t first_address{},second_address{},face_key{};
    };
    struct SilhouetteConnectivity {
        std::vector<std::size_t> vertex_addresses;
        std::vector<std::uint32_t> triangles;
        std::vector<std::pair<std::size_t,std::string>> face_runs;
        std::map<std::string,std::vector<std::pair<std::size_t,std::size_t>>,std::less<>> topology;
        std::vector<std::string> face_keys;
        std::vector<SilhouetteSource> sources;
    };
    std::optional<SilhouetteConnectivity> silhouette_connectivity;
    std::set<std::pair<std::string,std::string>> curve3d_point_owners;
    std::map<std::pair<std::string,std::string>,std::pair<ReferenceVisibility,EdgeKey>> marker_parents;
    std::optional<std::array<float, 3>> silhouette_view_direction;
    GLsizei silhouette_vertex_count{};
    std::vector<std::size_t> silhouette_source_triangles;
    std::optional<EdgeKey> silhouette_color_key;
    std::vector<std::pair<GLint,GLsizei>> silhouette_highlight_ranges;
    std::vector<CandidateKind> allowed_kinds{CandidateKind::Container};
    SelectionFilter selection_filter{SelectionFilter::All};
    std::function<bool(const ViewerCandidate&)> candidate_filter;
    bool offer_result_faces{};
    bool offer_original_faces{};
    bool offer_original_containers{};
    bool advance_selection_on_hover{true};
    std::map<std::pair<std::string,std::string>,bool> sketch_hv_mapping;
    std::function<void()> sketch_hv_changed;
    bool sketch_hv_notification_pending{};
    std::function<int(const ViewerCandidate&)> candidate_priority;
    std::vector<ViewerCandidate> candidates;
    // Position at which `candidates` was last (re)computed. RMB cycling
    // reuses the list as-is instead of forcing a redundant recompute when the
    // pointer is still within picking tolerance of this position, so the
    // first RMB press after a stationary hover advances the cycle instead of
    // merely re-stabilizing an already-correct list.
    QPointF candidates_position;
    bool has_candidates_position{};
    std::size_t active_candidate{};
    std::optional<ViewerCandidate> confirmed_candidate;
    std::optional<ViewerCandidate> component_candidate;
    std::set<std::size_t> component_edge_indices;
    std::vector<zima::kernel::ViewerEdge> component_wire;
    std::string selected_container_origin_id;
    std::function<void(const ViewerCandidate&)> confirmation_callback;
    std::function<void()> empty_confirmation_callback;
    std::function<void(const ViewerCandidate&)> double_confirmation_callback;
    std::function<bool(const ViewerCandidate&, const zima::kernel::Vec3&,
        const zima::kernel::Vec3&)> drag_begin_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        drag_update_callback;
    std::function<void()> drag_end_callback;
    bool drag_active{};
    std::optional<ViewerCandidate> pending_dimension_drag;
    QPointF dimension_drag_press_position;
    std::function<void(const ViewerCandidate&, const QPoint&)> context_menu_callback;
    std::function<bool(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        world_click_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        world_pointer_callback;
    std::function<bool(
        const std::optional<ViewerCandidate>&,
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        command_gesture_begin_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        command_gesture_update_callback;
    std::function<void()> command_gesture_end_callback;
    bool command_gesture_active{};
    std::function<bool()> short_middle_click_callback;
    std::function<bool()> double_middle_click_callback;
    bool sketch_box_selection_enabled{};
    std::optional<QPointF> sketch_box_start;
    std::optional<QPointF> sketch_box_end;
    std::function<void(std::vector<ViewerCandidate>, bool)>
        sketch_box_selection_callback;
    std::vector<ViewerCandidate> sketch_box_selected_candidates;
    std::function<bool()> dimension_placement_cycle_callback;
    std::function<bool()> empty_right_click_callback;
    std::function<bool(const ViewerCandidate&)>
        single_candidate_right_click_callback;
    std::function<bool(const ViewerCandidate&, std::size_t, std::size_t)>
        candidate_right_click_callback;
    bool middle_dragged{};
    bool middle_double_clicked{};
    QPoint middle_press_position;
    std::vector<zima::kernel::ViewerEdge> transient_edges;
    zima::kernel::ViewerMesh transient_context_mesh;
    std::vector<zima::kernel::ViewerDimension> transient_dimensions;
    std::vector<zima::kernel::ViewerPoint> command_snap_points;
    std::function<std::vector<kernel::ViewerPoint>(const kernel::Vec3&,const kernel::Vec3&)> command_snap_point_provider;
    bool command_snap_points_visible{};
    std::vector<zima::kernel::Vec3> transient_points;
    std::vector<kernel::ViewerPoint> transient_model_points;
    std::vector<std::pair<zima::kernel::Vec3, std::string>> transient_labels;
    std::optional<zima::kernel::Vec3> sketch_cursor;
    bool sketch_cursor_snapped{};
    std::string sketch_cursor_label;
    std::vector<ExtentManipulator> extent_manipulators;
    std::vector<OperationDirectionIndicator> operation_direction_indicators;
    std::optional<ExtentManipulator> dragged_extent_manipulator;
    std::string dragged_extent_key;
    std::string hovered_extent_key;
    std::function<void(const std::string&)> extent_begin_callback;
    std::function<void(const std::string&, double)> extent_update_callback;
    std::function<void()> extent_end_callback;
    std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)>
        transient_point_transform;
    // Per-edge highlight priority state. Mirrors the frozenset bookkeeping in
    // zima_cad/viewer.py's MeshView (_edge_display_color/_edge_is_highlighted):
    // selected > object overlay > hovered > feature preview > color override
    // > base color. Populated via the set_*_edges/owners setters below.
    std::set<EdgeKey> edge_treatment_selection_edges;
    std::set<EdgeKey> feature_hover_edges;
    std::set<EdgeKey> feature_selected_edges;
    std::vector<zima::kernel::ViewerEdge> container_inspection_wire;
    std::function<std::vector<kernel::ViewerEdge>(const std::string&,const std::string&)> container_preview_provider;
    std::function<bool(const std::string&,const std::string&)> container_preview_authoritative;
    std::optional<ViewerCandidate> inspected_container;
    std::optional<EdgeKey> container_color_key;
    std::vector<bool> container_color_edges;
    std::vector<std::pair<GLint,GLsizei>> container_selected_ranges,container_other_ranges;
    bool wire_has_individual_colors{};
    std::set<std::size_t> feature_hover_edge_indices;
    std::set<std::size_t> feature_selected_edge_indices;
    std::set<std::string> feature_preview_owner_ids;

    std::string active_sketch_owner_id;
    bool geometry_editing_presentation{};
    std::string editing_curve_owner;
    std::string editing_curve_path;
    std::set<std::string> constraint_reference_owner_ids;
    std::set<EdgeKey> constraint_reference_edges;
    std::set<EdgeKey> sketch_relation_highlights;
    std::set<EdgeKey> assembly_reference_edges;
    std::set<std::string> selected_container_content_ids;
    std::set<EdgeKey> object_overlay_main_edge_keys;
    std::optional<QColor> edge_color_override;
    QColor body_surface_color{"#B9C2CC"};
    std::map<std::string, QColor> body_surface_instance_colors;
    zima::kernel::SurfaceStyle surface_style;
    std::map<std::string,zima::kernel::SurfaceStyle> instance_styles, owner_styles, face_styles;
    // Face identities are stable across camera changes; source/reference changes invalidate them.
    bool reference_faces_dirty{true};
    std::vector<ViewerCandidate> reference_faces;
    std::vector<ViewerCandidate> confirmed_reference_faces;
    void prepare_reference_faces() {
        if(!reference_faces_dirty)return;
        reference_faces=inspected_face_candidates(mesh,constraint_reference_edges,confirmed_reference_faces);
        reference_faces_dirty=false;
    }
    struct SurfaceBatch {
        std::size_t first, count;
        zima::kernel::SurfaceStyle style;
        QColor color;
        bool technological_thread;
    };
    bool surface_batches_dirty{true};
    std::vector<SurfaceBatch> surface_batches;
    void prepare_surface_batches() {
        if (!surface_batches_dirty) return;
        surface_batches.clear();
        const std::size_t triangle_count = mesh.triangles.size() / 3;
        const auto triangle_style = [&](std::size_t triangle) {
            auto style=surface_style;
            if(triangle>=mesh.triangle_references.size())return style;
            const auto& r=mesh.triangle_references[triangle];
            if(const auto it=instance_styles.find(r.instance_path);it!=instance_styles.end())style=it->second;
            const auto owner=r.instance_path+"\x1f"+r.owner_id;
            if(const auto it=owner_styles.find(owner);it!=owner_styles.end())style=it->second;
            if(const auto it=face_styles.find(owner+"\x1f"+r.semantic_key);it!=face_styles.end())style=it->second;
            if(r.semantic_key.starts_with("thread:surface:"))style={"#8F969D",.5,.65};
            if(r.surface_result)style={zima::kernel::surface_result_color,0.0,.55};
            return style;
        };
        for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
            const auto style = triangle_style(triangle);
            const bool thread = triangle < mesh.triangle_references.size() &&
                mesh.triangle_references[triangle].semantic_key.starts_with("thread:surface:");
            if (!surface_batches.empty() && surface_batches.back().style == style &&
                surface_batches.back().technological_thread == thread) {
                ++surface_batches.back().count;
            } else {
                surface_batches.push_back({triangle, 1, style,
                    QColor(QString::fromStdString(style.color)), thread});
            }
        }
        surface_batches_dirty = false;
    }
    std::vector<ViewerCandidate> inspected_faces;
    std::map<std::string, QColor> body_surface_face_colors;
    QPoint last_pointer;
    QVector3D center;
    QPointF pan_pixels;
    QPointF resize_shift_pixels;
    int viewport_width{};
    bool preserve_screen_position_on_resize{};
    float radius{1.0F};
    float view_scale{1.4F};
    float reference_view_scale{1.4F};
    // Once a baseline reference_view_scale has been established (first fit
    // of a brand-new document, or restored from a saved camera state), later
    // fit_all() calls must NOT overwrite it. reference_view_scale is the
    // fixed "screen-constant" anchor that keeps the Origin's (and every
    // container's own origin) apparent size in the View identical no matter
    // how far the camera has to zoom out to fit newly added geometry -- see
    // fit_all()'s comment for the full reasoning.
    bool reference_view_scale_initialized{false};
    QQuaternion orientation = default_camera_orientation();
    // Mirrors Python's animate_standard_view/animate_view_normal: a running
    // QVariantAnimation slerps orientation and lerps pan/zoom back to the
    // resting state, matching zima_cad/viewer.py's camera transition feel
    // instead of a hard jump cut.
    QVariantAnimation* camera_animation{};
    ProjectionMode projection_mode{ProjectionMode::Orthographic};
    bool fly_navigation_enabled{};
    QVector3D fly_position;
    DisplayMode display_mode{DisplayMode::ShadedWithEdges};
    int dimension_decimal_places{3};
    DimensionDisplayUnits dimension_units;
    bool show_origins{true};
    bool show_points{true};
    bool show_axes{true};
    // Axis endpoints are command helpers, not permanent model markers.  They
    // remain available through the exact persisted references when a command
    // explicitly requests vertices/axes, but ordinary View never shows them.
    bool show_axis_points{false};
    bool show_planes{true};
    bool show_sketches{true};
    bool show_dimensions{true};
    bool show_symbols{true};
    std::map<kernel::ObjectEnvelopeKey,bool> annotation_sides;
    std::function<std::optional<SymbolHandles>()> symbol_handles;
    std::function<void(const std::string&,int,kernel::Vec3,double)> symbol_handle_move;
    struct SymbolDrag {std::string id;kernel::AnnotationStroke annotation;kernel::Vec3 right,up,normal,anchor;bool left;int index;};
    std::optional<SymbolDrag> symbol_drag;
    bool symbol_contact_drag{},symbol_contact_moved{};QPointF symbol_contact_drag_start;
    std::function<std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope>(const kernel::ViewerMesh&)> object_frame_provider;
    kernel::ModelEnvelope dimension_bounds;
    std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope> object_bounds;
    bool show_dimension_frame{};
    bool dimension_layout_editable{true};
    std::function<bool(const kernel::EdgeReference&)> dimension_layout_edit_filter;
    struct LayoutDrag {ViewerCandidate candidate;kernel::ViewerDimension source,shown;kernel::ModelEnvelope bounds;kernel::DimensionLayout initial,current;QPointF start;int handle{};bool moved{};};
    std::optional<LayoutDrag> layout_drag;

    std::vector<kernel::ViewerDimension> source_dimensions,source_transient_dimensions;
    std::function<std::optional<kernel::DimensionLayout>(const kernel::EdgeReference&)> dimension_layout_resolver;
    std::function<void(const kernel::EdgeReference&,kernel::DimensionLayout)> dimension_layout_commit;

    std::function<bool(const zima::kernel::ViewerDimension&)> dimension_visibility_filter;
    bool editing_origin_visible{};
    std::string document_origin_id;
    QColor origin_point_color(const zima::kernel::ViewerPoint& point) const {
        return point.reference.owner_id==document_origin_id && point.reference.instance_path.empty()
            ? QColor(0,0,0) : interaction::axis;
    }
    std::function<bool(const EdgeKey&)> origin_visibility_filter;
    bool origin_visible(const EdgeKey& key) const {
        // Component placement must respect the user's Origins switch even
        // while reference entry or the purple drag handle requests datums.
        if (component_origin_handle && !show_origins &&
            (key.semantic_key == "origin" || key.semantic_key.starts_with("origin:"))) return false;
        // The explicitly selected component's drag handle is independently
        // offered; this does not reveal its axes or planes.
        if(component_origin_handle && key==*component_origin_handle)return true;
        return (key.semantic_key != "origin" && !key.semantic_key.starts_with("origin:")) ||
            !origin_visibility_filter || origin_visibility_filter(key);
    }
    bool active_sketch_geometry(const auto& reference) const {
        return !active_sketch_owner_id.empty()&&reference.owner_id==active_sketch_owner_id&&
            reference.instance_path==editing_curve_path;
    }
    bool passive_sketch_axis(const kernel::ViewerEdge& edge) const {
        return is_sketch_wire_edge(edge)&&edge.construction&&edge.dash_dot&&!active_sketch_geometry(edge.reference);
    }
    bool sketch_axis_entry_requested() const {
        return std::ranges::any_of(allowed_kinds,[](auto kind){return kind==CandidateKind::Axis||kind==CandidateKind::SketchAxis;});
    }
    bool sketch_axis_visible(const kernel::ViewerEdge& edge) const {
        const auto key=edge_key(edge.reference);
        return show_axes||sketch_axis_entry_requested()||constraint_reference_edges.contains(key)||
            assembly_reference_edges.contains(key)||constraint_reference_owner_ids.contains(edge.reference.owner_id)||
            feature_preview_owner_ids.contains(edge.reference.owner_id);
    }
    bool sketch_wire_visible(const kernel::ViewerEdge& edge) const {
        return passive_sketch_axis(edge)?sketch_axis_visible(edge):
            edge.reference.semantic_key.starts_with("symbol:")?show_symbols:show_sketches;
    }
    bool passive_sketch_axis_point(const kernel::ViewerPoint& point) const {
        // The Sketch publishes real point incidence: an axis-only point has
        // construction=true; a point shared with a profile retains its profile
        // role. This uses no position matching or invented reference identity.
        return point.construction&&point.reference.semantic_key.starts_with("point:")&&
            !point.reference.semantic_key.starts_with("point:from:")&&!active_sketch_geometry(point.reference);
    }
    bool helper_point_visible(const kernel::ViewerPoint& point,bool picking=false) const {
        if(active_sketch_geometry(point.reference)&&point.construction&&
            point.reference.semantic_key.starts_with("point:")&&
            !point.reference.semantic_key.starts_with("point:from:")) {
            // The visibility preference also covers authored construction
            // curves in the active Sketch. Keep real editable points available
            // for snapping/dragging, with normal feedback only when picked.
            return show_axis_points||picking;
        }
        if(passive_sketch_axis_point(point)) {
            const bool explicit_entry=sketch_axis_entry_requested()||std::ranges::any_of(allowed_kinds,[](auto kind){
                return kind==CandidateKind::Vertex||kind==CandidateKind::SketchPoint;});
            return (show_axes&&show_axis_points)||explicit_entry||constraint_reference_owner_ids.contains(point.reference.owner_id)||
                constraint_reference_edges.contains({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})||
                assembly_reference_edges.contains({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})||
                feature_preview_owner_ids.contains(point.reference.owner_id);
        }
        auto axis_key=kernel::axis_endpoint_parent_key(point.reference.semantic_key);
        const auto parent=marker_parents.find({point.reference.owner_id,point.reference.instance_path});
        // Unified Feature Axis endpoints keep their real Start/End ancestry.
        // They are not the older construction-axis presentation keys.
        constexpr std::string_view side_point="point:from:";
        if(axis_key.empty()&&parent!=marker_parents.end()&&parent->second.first==ReferenceVisibility::Axes&&
            parent->second.second.semantic_key=="axis"&&point.display_owner_id==point.reference.owner_id&&
            point.reference.semantic_key.starts_with(side_point)&&
            kernel::feature_side_parent(std::string_view(point.reference.semantic_key).substr(side_point.size())))
            axis_key="axis";
        const bool defining_marker=point.reference.semantic_key=="point"||point.reference.semantic_key=="preview:plane-offset-point";
        if(axis_key.empty()&&(!defining_marker||parent==marker_parents.end()))return true;
        const auto category=axis_key.empty()?parent->second.first:ReferenceVisibility::Axes;
        const auto reference=axis_key.empty()?parent->second.second:EdgeKey{point.reference.owner_id,axis_key,point.reference.instance_path};
        const bool explicit_entry=std::ranges::any_of(allowed_kinds,[category](auto kind){
            return kind==CandidateKind::Vertex||kind==CandidateKind::SketchPoint||
                (category==ReferenceVisibility::Axes&&(kind==CandidateKind::Axis||kind==CandidateKind::SketchAxis))||
                (category==ReferenceVisibility::Planes&&kind==CandidateKind::Plane);
        });
        return (category==ReferenceVisibility::Axes?(show_axes&&show_axis_points):show_planes)||explicit_entry||editing_origin_visible||
            constraint_reference_owner_ids.contains(point.reference.owner_id)||
            constraint_reference_owner_ids.contains(reference.owner_id)||
            constraint_reference_edges.contains(reference)||
            constraint_reference_edges.contains(EdgeKey{point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})||
            feature_preview_owner_ids.contains(point.reference.owner_id)||
            feature_preview_owner_ids.contains(reference.owner_id);
    }
    std::optional<EdgeKey> component_origin_handle;
    bool gpu_dirty{true};
    std::size_t base_mesh_revision{};

    void rebuild_sketch_interaction_mesh() {
        sketch_interaction_mesh = {};
        sketch_edge_indices.clear();
        sketch_point_indices.clear();
        sketch_axis_indices.clear();
        sketch_dimension_indices.clear();
        sketch_constraint_indices.clear();
        if (active_sketch_owner_id.empty()) return;
        const auto owned = [&](const auto& value) {
            return value.reference.owner_id == active_sketch_owner_id;
        };
        for (std::size_t index = 0; index < mesh.edges.size(); ++index) {
            if (!owned(mesh.edges[index])) continue;
            sketch_edge_indices.push_back(index);
            sketch_interaction_mesh.edges.push_back(mesh.edges[index]);
        }
        for (std::size_t index = 0; index < mesh.points.size(); ++index) {
            if (!owned(mesh.points[index])) continue;
            sketch_point_indices.push_back(index);
            sketch_interaction_mesh.points.push_back(mesh.points[index]);
        }
        for (std::size_t index = 0; index < mesh.axes.size(); ++index) {
            if (!owned(mesh.axes[index])) continue;
            sketch_axis_indices.push_back(index);
            sketch_interaction_mesh.axes.push_back(mesh.axes[index]);
        }
        for (std::size_t index = 0; index < mesh.dimensions.size(); ++index) {
            if (!owned(mesh.dimensions[index])) continue;
            sketch_dimension_indices.push_back(index);
            sketch_interaction_mesh.dimensions.push_back(mesh.dimensions[index]);
        }
        for (std::size_t index = 0;
             index < mesh.constraint_markers.size(); ++index) {
            if (!owned(mesh.constraint_markers[index])) continue;
            sketch_constraint_indices.push_back(index);
            sketch_interaction_mesh.constraint_markers.push_back(
                mesh.constraint_markers[index]);
        }
    }

    void rebuild_sheet_container_pick_mesh() {
        auto& target=sheet_container_pick_mesh;target={};
        const auto& source=mesh.original_references;
        std::size_t count{};
        for(std::size_t triangle=0;triangle<source.triangle_references.size();++triangle) {
            const auto& reference=source.triangle_references[triangle];
            if(!reference.sheet_owner.empty()&&
                (reference.sheet_role==kernel::SheetFaceRole::SideA||
                 reference.sheet_role==kernel::SheetFaceRole::SideB)&&
                triangle*3+2<source.triangles.size())++count;
        }
        target.vertices.reserve(count*3);target.triangles.reserve(count*3);
        target.triangle_references.reserve(count);
        for(std::size_t triangle=0;triangle<source.triangle_references.size();++triangle) {
            const auto& reference=source.triangle_references[triangle];
            if(reference.sheet_owner.empty()||
                (reference.sheet_role!=kernel::SheetFaceRole::SideA&&
                 reference.sheet_role!=kernel::SheetFaceRole::SideB)||
                triangle*3+2>=source.triangles.size())continue;
            const auto base=static_cast<std::uint32_t>(target.vertices.size());
            bool valid=true;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto index=source.triangles[triangle*3+corner];
                if(index>=source.vertices.size()){valid=false;break;}
                target.vertices.push_back(source.vertices[index]);
            }
            if(!valid){target.vertices.resize(base);continue;}
            target.triangles.insert(target.triangles.end(),{base,base+1,base+2});
            target.triangle_references.push_back(reference);
        }
        if(target.triangle_references.empty())sheet_container_picking_index.reset();
        else sheet_container_picking_index.emplace(target);
    }

    [[nodiscard]] bool has_authored_container_wire(const ViewerCandidate& candidate) const {
        const auto key=std::make_pair(candidate.owner_id,candidate.instance_path);
        return original_container_edge_indices.contains(key)||edge_treatment_boundary_edge_indices.contains(key)||
            (container_preview_authoritative&&container_preview_authoritative(candidate.owner_id,candidate.instance_path));
    }

    [[nodiscard]] std::optional<ViewerCandidate> authored_container_candidate(
            const std::string& owner,const std::string& path = {}) const {
        const bool authored=container_preview_authoritative&&container_preview_authoritative(owner,path);
        if(auto candidate=container_candidate(mesh,owner,path)) {
            // A feature can retain only its helper axis after later coplanar
            // merging. Tree selection still selects the authored feature,
            // rather than reinterpreting it as that surviving helper.
            if(authored)candidate->semantic_key.clear();
            return candidate;
        }
        // Tree selection can inspect an Assembly-owned tool even when its
        // displayed faces belong to target occurrence paths or were consumed.
        // This is an object selection, not a new selectable topology record.
        if(authored)
            return ViewerCandidate{CandidateKind::Container,0.,0,owner,{},path,CandidateGeometry::Display};
        return {};
    }

    [[nodiscard]] std::vector<kernel::ViewerEdge> container_preview_wire(
            const std::string& owner,const std::string& path) const {
        std::vector<kernel::ViewerEdge> wire;
        if(owner.empty())return wire;
        if(container_preview_provider) {
            wire=container_preview_provider(owner,path);
            if(!wire.empty()||(container_preview_authoritative&&container_preview_authoritative(owner,path)))return wire;
        }
        const auto key=std::make_pair(owner,path);
        const auto treatment=edge_treatment_boundary_edge_indices.find(key);
        const auto original=original_container_edge_indices.find(key);
        const bool transformed_display=std::any_of(mesh.edges.begin(),mesh.edges.end(),[&](const auto& edge) {
            return !edge.construction&&!edge.overlay&&edge.display_owner_id==owner&&
                edge.reference.instance_path==path&&edge.reference.owner_id!=owner;
        });
        if(treatment!=edge_treatment_boundary_edge_indices.end()) {
            for(const auto index:treatment->second)if(index<mesh.edges.size())wire.push_back(mesh.edges[index]);
        } else if(!transformed_display&&original!=original_container_edge_indices.end()) {
            for(const auto index:original->second)if(index<mesh.original_references.edges.size())
                wire.push_back(mesh.original_references.edges[index]);
        } else {
            const ViewerCandidate candidate{CandidateKind::Container,0,0,owner,{},path,CandidateGeometry::Display};
            for(const auto& edge:mesh.edges)if(candidate_recolors_wire_edge(candidate,edge))wire.push_back(edge);
        }
        return wire;
    }

    void rebuild_persisted_reference_mesh() {
        auto& target = persisted_reference_mesh;
        target = {};
        original_container_edge_indices.clear();
        edge_treatment_boundary_edge_indices.clear();
        // Screen-constant datum-plane borders are rebuilt below from the
        // displayed rectangle. Do not retain their unscaled model-space
        // originals as a second pick target: that produced exactly the two
        // simultaneous hover offers visible in 01.png/02.png (the small
        // displayed rectangle plus a much larger invisible rectangle).
        std::copy_if(mesh.original_references.edges.begin(),
            mesh.original_references.edges.end(),
            std::back_inserter(target.edges), [](const auto& edge) {
                return !is_screen_constant_plane(edge.reference.semantic_key);
            });
        for (std::size_t index = 0;
             index < mesh.original_references.edges.size(); ++index) {
            const auto& edge = mesh.original_references.edges[index];
            ViewerCandidate candidate{CandidateKind::Container, 0.0, 0,
                edge.reference.owner_id, {}, edge.reference.instance_path,
                CandidateGeometry::OriginalReference};
            if (!candidate_uses_original_container_wire_edge(candidate, edge))
                continue;
            original_container_edge_indices[
                {edge.reference.owner_id, edge.reference.instance_path}]
                    .push_back(index);
        }
        for (std::size_t index = 0; index < mesh.edges.size(); ++index) {
            const auto& edge = mesh.edges[index];
            for (const auto& owner_id : edge_treatment_boundary_owners(edge)) {
                edge_treatment_boundary_edge_indices[
                    {owner_id, edge.reference.instance_path}].push_back(index);
            }
        }
        target.points = mesh.original_references.points;
        target.axes = mesh.original_references.axes;
        const bool has_local_display_faces = std::any_of(
            mesh.triangle_references.begin(), mesh.triangle_references.end(),
            [](const auto& reference) {
                return reference.valid() && reference.instance_path.empty();
            });
        const double scale = view_scale /
            std::max(reference_view_scale, 1.0e-6F);
        for (auto& axis : target.axes) {
            if (axis.reference.semantic_key.starts_with("origin:axis:")) {
                axis.display_length *= scale;
            }
        }
        // Origin and work planes are orientation aids and therefore keep the
        // same fixed apparent LCD size. The picker consumes this exact scaled
        // rectangle too, so hover and confirmation cannot drift from it.
        for (const auto& edge : mesh.edges) {
            if (edge.reference.semantic_key != "border" && edge.reference.semantic_key != "plane:end" &&
                !edge.reference.semantic_key.starts_with("origin:plane:")) continue;
            if (edge.points.size() < 4) continue;
            auto scaled = edge;
            const std::size_t count = edge.points.size() - 1;
            zima::kernel::Vec3 center;
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            const double edge_scale = is_screen_constant_plane(
                edge.reference.semantic_key) ? scale : 1.0;
            for (auto& point : scaled.points) {
                point = {center.x + (point.x - center.x) * edge_scale,
                         center.y + (point.y - center.y) * edge_scale,
                         center.z + (point.z - center.z) * edge_scale};
            }
            target.edges.push_back(std::move(scaled));
        }
        // Keep persisted solid faces unchanged. Origin planes are screen-size
        // overlays; rebuild their pick triangles from the exact scaled border
        // which paintGL displays, so hover and click cannot be spatially offset.
        const auto include_triangle=[&](std::size_t triangle) {
            const auto& reference=mesh.original_references.triangle_references[triangle];
            return !(has_local_display_faces && reference.instance_path.empty() && !offer_original_containers && !offer_original_faces) &&
                !reference.semantic_key.starts_with("origin:plane:") &&
                reference.semantic_key!="plane" && reference.semantic_key!="plane:end" &&
                triangle*3+2<mesh.original_references.triangles.size();
        };
        std::size_t reference_triangle_count=0;
        for(std::size_t triangle=0;triangle<mesh.original_references.triangle_references.size();++triangle)
            reference_triangle_count+=include_triangle(triangle);
        // Reserve the exact accepted face count before copying identities.
        // Repeated vector growth otherwise copies large string-bearing face
        // records several times; selection and vertex publication stay ordered.
        target.vertices.reserve(reference_triangle_count*3);
        target.triangles.reserve(reference_triangle_count*3);
        target.triangle_references.reserve(reference_triangle_count);
        for (std::size_t triangle = 0;
             triangle < mesh.original_references.triangle_references.size();
             ++triangle) {
            const auto& reference =
                mesh.original_references.triangle_references[triangle];
            if (!include_triangle(triangle)) continue;
            const auto base = static_cast<std::uint32_t>(target.vertices.size());
            for (std::size_t corner = 0; corner < 3; ++corner) {
                target.vertices.push_back(mesh.original_references.vertices[
                    mesh.original_references.triangles[triangle * 3 + corner]]);
                target.triangles.push_back(base + static_cast<std::uint32_t>(corner));
            }
            target.triangle_references.push_back(reference);
        }
        for (const auto& edge : mesh.edges) {
            if (!is_screen_constant_plane(edge.reference.semantic_key) ||
                edge.points.size() < 4) continue;
            zima::kernel::Vec3 center;
            const std::size_t count = edge.points.size() > 4
                ? edge.points.size() - 1 : edge.points.size();
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            const auto base = static_cast<std::uint32_t>(target.vertices.size());
            for (std::size_t index = 0; index < 4; ++index) {
                const auto& point = edge.points[index];
                target.vertices.push_back({
                    center.x + (point.x - center.x) * scale,
                    center.y + (point.y - center.y) * scale,
                    center.z + (point.z - center.z) * scale});
            }
            target.triangles.insert(target.triangles.end(),
                {base, base + 1, base + 2, base, base + 2, base + 3});
            const zima::kernel::FaceReference reference{
                edge.reference.owner_id,
                edge.reference.semantic_key == "border"
                    ? std::string("plane") : edge.reference.semantic_key,
                edge.reference.instance_path};
            target.triangle_references.push_back(reference);
            target.triangle_references.push_back(reference);
        }
        reference_picking_index.emplace(target);
    }

    [[nodiscard]] float orbit_distance() const {
        const float half_angle = kPerspectiveFieldOfViewDegrees *
            static_cast<float>(std::numbers::pi) / 360.0F;
        return std::max(view_scale, 0.001F) / std::tan(half_angle);
    }

    void place_fly_camera_at_orbit_position() {
        const float units_per_pixel = 2.0F * view_scale /
            std::max(1, viewport_height);
        const QVector3D camera_translation(
            -static_cast<float>(pan_pixels.x()) * units_per_pixel,
            static_cast<float>(pan_pixels.y()) * units_per_pixel,
            orbit_distance());
        fly_position = center +
            orientation.inverted().rotatedVector(camera_translation);
    }

    [[nodiscard]] QMatrix4x4 view() const {
        QMatrix4x4 result;
        if (fly_navigation_enabled) {
            result.rotate(orientation);
            result.translate(-fly_position);
            return result;
        }
        const float units_per_pixel = 2.0F * view_scale /
            std::max(1, viewport_height);
        result.translate(static_cast<float>(pan_pixels.x()) * units_per_pixel,
                         static_cast<float>(-pan_pixels.y()) * units_per_pixel,
                         projection_mode == ProjectionMode::Perspective
                            ? -orbit_distance() : 0.0F);
        result.rotate(orientation);
        result.translate(-center);
        return result;
    }

    int viewport_height{1};

    [[nodiscard]] QMatrix4x4 projection(int width, int height) const {
        const float aspect = height > 0 ? static_cast<float>(width) / height : 1.0F;
        QMatrix4x4 result;
        const auto anchored=[&](const QMatrix4x4& projection){
            QMatrix4x4 shift;shift.translate(static_cast<float>(2*resize_shift_pixels.x()/std::max(1,width)),static_cast<float>(-2*resize_shift_pixels.y()/std::max(1,height)));
            return shift*projection;
        };
        const float vertical = std::max(view_scale, 0.001F);
        // Fit All owns the navigation radius, not the clipping volume. A
        // regenerated or newly opened model can grow while retaining its camera.
        // Use the current persisted viewer envelope, with no kernel work.
        float closest_depth = std::numeric_limits<float>::max();
        float furthest_depth = std::numeric_limits<float>::lowest();
        if (dimension_bounds.valid) {
            const auto camera = view();
            for (const auto& point : dimension_bounds.corners()) {
                const float depth = -camera.map(QVector3D(
                    static_cast<float>(point.x), static_cast<float>(point.y),
                    static_cast<float>(point.z))).z();
                closest_depth = std::min(closest_depth, depth);
                furthest_depth = std::max(furthest_depth, depth);
            }
        }
        if (projection_mode == ProjectionMode::Perspective) {
            const float camera_distance = fly_navigation_enabled
                ? std::max((center - fly_position).length(), radius)
                : orbit_distance();
            // Empty scenes use the navigation sphere. For model geometry,
            // tighten the range around its current camera-space envelope to
            // preserve depth precision on adjacent/coplanar CAD triangles.
            const float scene_radius = std::max(radius, 0.001F);
            const float minimum_near = std::max(
                scene_radius * 1.0e-3F, 1.0e-4F);
            float near_plane = std::max(minimum_near,
                camera_distance - scene_radius * 1.1F);
            float far_plane = std::max(
                camera_distance + scene_radius * 1.1F,
                near_plane + scene_radius * 0.01F);
            if (dimension_bounds.valid && furthest_depth > 0.0F) {
                const float margin = std::max({
                    (furthest_depth - closest_depth) * 0.1F,
                    std::abs(furthest_depth) * 1.0e-4F, 0.001F});
                near_plane = std::max(minimum_near, closest_depth - margin);
                far_plane = std::max(near_plane + margin, furthest_depth + margin);
            }
            result.perspective(kPerspectiveFieldOfViewDegrees,
                std::max(aspect, 0.001F), near_plane, far_plane);
            return anchored(result);
        }
        float depth_extent = std::max({radius * 20.0F, 100.0F,
            fly_navigation_enabled
                ? (center - fly_position).length() + radius * 2.0F
                : 0.0F});
        if (dimension_bounds.valid) {
            depth_extent = std::max(depth_extent,
                std::max(std::abs(closest_depth), std::abs(furthest_depth)) * 1.1F + 0.001F);
        }
        result.ortho(-vertical * aspect, vertical * aspect, -vertical, vertical,
                     -depth_extent, depth_extent);
        return anchored(result);
    }
};

MeshView::MeshView(QWidget* parent)
    : QOpenGLWidget(parent), impl_(std::make_unique<Impl>()) {
    impl_->relation_marks=new RelationMarkLayer(this);
    setFont(zima::technical_font());
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

MeshView::~MeshView() {
    if (context() != nullptr) {
        makeCurrent();
        impl_->vertex_array.destroy();
        impl_->vertices.destroy();
        impl_->transparent_triangles.destroy();
        impl_->lines.destroy();
        impl_->selected_source_lines.destroy();
        impl_->silhouette.destroy();
        doneCurrent();
    }
}

void MeshView::update_annotation_presentation() const {
    const auto camera=impl_->view().inverted();
    const auto axis=[&](QVector3D v){const auto p=camera.mapVector(v).normalized();return kernel::Vec3{p.x(),p.y(),p.z()};};
    const auto right=axis({1,0,0}),up=axis({0,1,0});
    for(auto* edges:{&impl_->mesh.edges,&impl_->transient_edges})for(auto& edge:*edges)if(edge.annotation) {
        const auto& a=*edge.annotation;const kernel::ObjectEnvelopeKey key{edge.reference.owner_id,edge.reference.instance_path};
        const double side=kernel::dimension_dot(kernel::dimension_sub(a.contact,a.grip),right);
        auto [it,inserted]=impl_->annotation_sides.try_emplace(key,side<=0);
        // Screen-space dead band avoids flicker when orbiting through the shelf center.
        if(std::abs(side)>world_tolerance_for_pixels(2.))it->second=side<=0;
        edge.points=kernel::annotation_stroke(a,right,up,it->second,true,true);
    }
}

void MeshView::set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view) {
    QElapsedTimer timer;timer.start();
    const auto phase=[&](const char* name) {
        if(qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Viewer mesh %s: %.3f ms\n",name,timer.nsecsElapsed()/1e6);
        timer.restart();
    };
    const bool retiring_scene=!has_viewer_storage(mesh)&&has_viewer_storage(impl_->mesh);
    zima::kernel::associate_solid_state_display(mesh);
    if(zima::kernel::has_surface_results(mesh))
        impl_->surface_source_mesh=mesh;
    else if(!retiring_scene) impl_->surface_source_mesh.reset();
    if(!impl_->show_surfaces)zima::kernel::hide_surface_results(mesh);
    impl_->container_inspection_wire.clear();
    impl_->inspected_container.reset();
    impl_->selected_source_key.reset();
    impl_->container_color_key.reset();
    const auto previous_confirmation = impl_->confirmed_candidate;
    impl_->dimension_bounds=kernel::model_envelope(mesh);
    impl_->object_bounds=kernel::object_envelopes(mesh,impl_->object_frame_provider?impl_->object_frame_provider(mesh):std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope>{});
    phase("envelopes");
    impl_->layout_drag.reset();
    impl_->source_dimensions=mesh.dimensions;
    if(impl_->dimension_layout_resolver)for(auto& d:mesh.dimensions)
        if(!d.rotation_handle)if(auto layout=impl_->dimension_layout_resolver(d.reference))d=kernel::layout_dimension(d,impl_->object_bounds.contains({d.reference.owner_id,d.reference.instance_path})&&impl_->object_bounds.at({d.reference.owner_id,d.reference.instance_path}).valid?impl_->object_bounds.at({d.reference.owner_id,d.reference.instance_path}):impl_->dimension_bounds,*layout);
    // Show the already persisted end-profile datum in Parts and Assembly
    // occurrences. Its border and picking share the same screen-sized plane;
    // displaying the reference never asks the kernel to calculate geometry.
    std::map<EdgeKey, std::vector<kernel::Vec3>> end_planes;
    const auto& refs = mesh.original_references;
    for (std::size_t i=0;i<refs.triangle_references.size();++i) {
        const auto& ref=refs.triangle_references[i];
        if(ref.semantic_key!="plane:end" || i*3+2>=refs.triangles.size())continue;
        auto& corners=end_planes[{ref.owner_id,ref.semantic_key,ref.instance_path}];
        for(std::size_t j=0;j<3;++j) {
            const auto index=refs.triangles[i*3+j];
            if(index>=refs.vertices.size())continue;
            const auto p=refs.vertices[index];
            // Calculated and construction packets may repeat the same datum,
            // and triangles need not share vertex-array indices.
            if(std::ranges::none_of(corners,[&](const auto& q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z)<1e-8;}))corners.push_back(p);
        }
    }
    // Remove existing borders in one scene traversal. Keys retain the owner and
    // occurrence, so one instance's datum never suppresses another instance.
    if(!end_planes.empty())for(const auto& edge:mesh.edges)
        if(edge.reference.semantic_key=="plane:end")end_planes.erase(edge_key(edge.reference));
    for(const auto& [key,corners]:end_planes) {
        if(corners.size()!=4)continue;
        kernel::ViewerEdge edge;edge.reference={key.owner_id,key.semantic_key,key.instance_path};
        edge.construction=true;edge.overlay=true;edge.display_owner_id=key.owner_id;
        edge.points=corners;
        kernel::Vec3 center{};
        for(const auto& p:corners){center.x+=p.x/4;center.y+=p.y/4;center.z+=p.z/4;}
        const auto& a=corners[0];const auto& b=corners[1];
        const double length=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);
        if(length<1e-8)continue;
        const double factor=presentation::container_origin_plane_size/length;
        for(auto& p:edge.points)p={center.x+(p.x-center.x)*factor,center.y+(p.y-center.y)*factor,center.z+(p.z-center.z)*factor};
        edge.points.push_back(edge.points.front());mesh.edges.push_back(std::move(edge));
    }
    std::erase_if(mesh.edges, [](const auto& edge) {
        return edge.parameter_seam || zima::kernel::smooth_transition_junction(edge);
    });
    // Work planes, points and reference packets can change while the shaded
    // solid and depth-tested wire stay identical. Compare the actual upload
    // inputs before replacing the scene; keep their normals/adjacency/buffers.
    const auto same_upload = [&] {
        const auto& previous=impl_->mesh;
        const auto same_points=[](const auto& a,const auto& b) {
            return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& p,const auto& q) {
                return std::bit_cast<std::uint64_t>(p.x)==std::bit_cast<std::uint64_t>(q.x)&&
                    std::bit_cast<std::uint64_t>(p.y)==std::bit_cast<std::uint64_t>(q.y)&&
                    std::bit_cast<std::uint64_t>(p.z)==std::bit_cast<std::uint64_t>(q.z);
            });
        };
        if(!same_points(previous.vertices,mesh.vertices) || previous.triangles!=mesh.triangles ||
                previous.triangle_references!=mesh.triangle_references)return false;
        auto old=previous.edges.begin();auto next=mesh.edges.cbegin();
        for(;;) {
            while(old!=previous.edges.end()&&old->overlay)++old;
            while(next!=mesh.edges.end()&&next->overlay)++next;
            if(old==previous.edges.end()||next==mesh.edges.end())
                return old==previous.edges.end()&&next==mesh.edges.end();
            if(!same_points(old->points,next->points) || old->reference!=next->reference)return false;
            ++old;++next;
        }
    }();
    std::shared_ptr<RetiredViewerGeometry> retired;
    if(retiring_scene) {
        retired=std::make_shared<RetiredViewerGeometry>();
        retired->display=std::move(impl_->mesh);
        retired->surface_source=std::move(impl_->surface_source_mesh);
        retired->persisted_references=std::move(impl_->persisted_reference_mesh);
        retired->sheet_container_pick=std::move(impl_->sheet_container_pick_mesh);
        retired->sketch_interaction=std::move(impl_->sketch_interaction_mesh);
        retired->display_index=std::move(impl_->display_picking_index);
        retired->reference_index=std::move(impl_->reference_picking_index);
        retired->sheet_index=std::move(impl_->sheet_container_picking_index);
    }
    impl_->mesh = std::move(mesh);
    if(retired)QThreadPool::globalInstance()->start(
        [retired=std::move(retired)] {});
    // Datum display size must be initialized even when a document refresh
    // deliberately preserves the camera (and therefore does not call fit_all).
    // A body or a point alone cannot establish this presentation baseline.
    if (!impl_->reference_view_scale_initialized) {
        double extent = 0;
        for (const auto& edge : impl_->mesh.edges) {
            if (!is_screen_constant_plane(edge.reference.semantic_key) || edge.points.size()<2) continue;
            const auto count=edge.points.size()-1;
            kernel::Vec3 center{};
            for(std::size_t i=0;i<count;++i) {
                center.x+=edge.points[i].x/count;center.y+=edge.points[i].y/count;center.z+=edge.points[i].z/count;
            }
            for(std::size_t i=0;i<count;++i) {
                const auto& p=edge.points[i];
                extent=std::max(extent,std::hypot(p.x-center.x,p.y-center.y,p.z-center.z));
            }
        }
        for(const auto& axis:impl_->mesh.axes)
            if(axis.reference.semantic_key.starts_with("origin:axis:"))extent=std::max(extent,axis.display_length);
        if(extent>0) {
            impl_->reference_view_scale=static_cast<float>(std::max(extent,.5)*2);
            impl_->reference_view_scale_initialized=true;
        }
    }
    impl_->surface_batches_dirty = true;
    impl_->reference_faces_dirty = true;
    impl_->candidates.clear();
    impl_->confirmed_candidate.reset();
    impl_->feature_hover_edges.clear();
    impl_->feature_selected_edges.clear();
    impl_->feature_hover_edge_indices.clear();
    impl_->feature_selected_edge_indices.clear();
    impl_->selected_container_origin_id.clear();
    impl_->gpu_dirty = impl_->gpu_dirty || !same_upload;
    if(!impl_->gpu_dirty) {
        // These draw identities and styles belong to the new scene even when
        // its buffer coordinates match. Overlay insertion can shift edge slots.
        impl_->line_edges.clear();impl_->line_mesh_edge_indices.clear();
        for(std::size_t i=0;i<impl_->mesh.edges.size();++i) {
            const auto& edge=impl_->mesh.edges[i];
            if(edge.overlay||is_screen_constant_plane(edge.reference.semantic_key)||edge.points.size()<2)continue;
            impl_->line_edges.push_back(edge);impl_->line_mesh_edge_indices.push_back(i);
        }
    }
    ++impl_->base_mesh_revision;
    phase("presentation");
    impl_->display_picking_index.emplace(impl_->mesh);
    impl_->rebuild_sheet_container_pick_mesh();
    phase("picking index");
    impl_->face_boundaries.clear();
    const auto index_faces=[](const auto& faces,auto& target) {
        target.clear();
        const kernel::FaceReference* previous{};std::vector<std::size_t>* group{};
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& f=faces[i];
            if(!previous||previous->owner_id!=f.owner_id||previous->semantic_key!=f.semantic_key||
                    previous->instance_path!=f.instance_path)
                group=&target[{f.owner_id,f.semantic_key,f.instance_path}];
            group->push_back(i);previous=&f;
        }
    };
    index_faces(impl_->mesh.triangle_references,impl_->display_face_triangles);
    index_faces(impl_->mesh.original_references.triangle_references,impl_->original_face_triangles);
    impl_->curve3d_point_owners.clear();
    for(const auto& edge:impl_->mesh.edges)if(is_curve3d_edge(edge.reference.semantic_key))
        impl_->curve3d_point_owners.emplace(edge.display_owner_id,edge.reference.instance_path);
    impl_->marker_parents.clear();
    const auto marker_parent=[&](const auto& reference,ReferenceVisibility category){
        const EdgeKey key{reference.owner_id,reference.semantic_key,reference.instance_path};
        impl_->marker_parents[{reference.owner_id,reference.instance_path}]={category,key};
        if(reference.owner_id.ends_with(":entity")){
            const auto origin=reference.owner_id.substr(0,reference.owner_id.size()-7)+":origin";
            impl_->marker_parents[{origin,reference.instance_path}]={category,key};
        }
    };
    for(const auto& axis:impl_->mesh.axes)if(!axis.reference.semantic_key.starts_with("origin:"))marker_parent(axis.reference,ReferenceVisibility::Axes);
    // Assembly presentation can omit a hidden datum's line while retaining
    // its real points. Its original reference packet still defines ownership.
    for(const auto& axis:impl_->mesh.original_references.axes)if(!axis.reference.semantic_key.starts_with("origin:"))marker_parent(axis.reference,ReferenceVisibility::Axes);
    for(const auto& edge:impl_->mesh.edges)if(edge.reference.semantic_key=="plane")marker_parent(edge.reference,ReferenceVisibility::Planes);
    for(const auto& face:impl_->mesh.original_references.triangle_references)if(face.semantic_key=="plane")marker_parent(face,ReferenceVisibility::Planes);
    impl_->rebuild_sketch_interaction_mesh();
    if (fit_view) fit_all();
    impl_->rebuild_persisted_reference_mesh();
    phase("reference mesh");
    if (previous_confirmation &&
        (previous_confirmation->kind == CandidateKind::Dimension ||
         previous_confirmation->kind == CandidateKind::SketchConstraint ||
         previous_confirmation->kind == CandidateKind::SketchPoint ||
         previous_confirmation->kind == CandidateKind::SketchSegment ||
         previous_confirmation->kind == CandidateKind::SketchCurve ||
         previous_confirmation->kind == CandidateKind::SketchText ||
         previous_confirmation->kind ==
             CandidateKind::SketchExternalReference)) {
        confirm_reference(previous_confirmation->owner_id,
            previous_confirmation->semantic_key,
            previous_confirmation->instance_path,
            previous_confirmation->kind);
    }
    update();
}

void MeshView::set_dimension_layout_editable(bool editable,
    std::function<bool(const kernel::EdgeReference&)> filter) {
    impl_->dimension_layout_editable=editable;
    impl_->dimension_layout_edit_filter=std::move(filter);
}
bool MeshView::dimension_layout_editable(const ViewerCandidate& candidate) const {
    return impl_->dimension_layout_editable && candidate.kind==CandidateKind::Dimension &&
        (!impl_->dimension_layout_edit_filter || impl_->dimension_layout_edit_filter(
            {candidate.owner_id,candidate.semantic_key,candidate.instance_path}));
}
std::optional<QPointF> MeshView::dimension_handle_position(const ViewerCandidate& candidate,int index)const {
    if(candidate.kind!=CandidateKind::Dimension||index<0||index>2)return {};
    const auto i=candidate.geometry_index;
    const auto* d=i<impl_->mesh.dimensions.size()?&impl_->mesh.dimensions[i]:i-impl_->mesh.dimensions.size()<impl_->transient_dimensions.size()?&impl_->transient_dimensions[i-impl_->mesh.dimensions.size()]:nullptr;
    if(!d||!impl_->show_dimensions||(impl_->dimension_visibility_filter&&!impl_->dimension_visibility_filter(*d)))return {};
    if(index==2&&d->kind==kernel::ViewerDimensionKind::Radius)return {};
    const auto mvp=impl_->projection(width(),height())*impl_->view();
    const auto project=[&](kernel::Vec3 p){auto q=mvp*QVector4D(p.x,p.y,p.z,1);if(std::abs(q.w())>1e-9)q/=q.w();return QPointF((q.x()+1)*width()/2.,(1-q.y())*height()/2.);};
    const auto text=dimension_label_text(*d);
    const auto layout=dimension_text_presentation(*d,project,font(),text,.5*logicalDpiX()/25.4,1.5,10,3,false,1.5*logicalDpiX()/25.4,impl_->active_sketch_owner_id.empty());
    return layout.valid?std::optional(layout.handles[index]):std::nullopt;
}
void MeshView::set_symbol_handle_callbacks(std::function<std::optional<SymbolHandles>()> provider,
    std::function<void(const std::string&,int,kernel::Vec3,double)> move) {
    impl_->symbol_handles=std::move(provider);impl_->symbol_handle_move=std::move(move);update();
}
std::optional<kernel::AnnotationStroke> MeshView::symbol_handle_annotation()const {
    if(!impl_->show_symbols||!impl_->symbol_handles)return {};
    const auto handles=impl_->symbol_handles();if(!handles)return {};
    for(const auto* edges:{&impl_->transient_edges,&impl_->mesh.edges})for(const auto& edge:*edges)
        if(edge.annotation&&edge.annotation->role==1&&edge.reference.owner_id==handles->id) {
            const auto d=kernel::dimension_sub(edge.annotation->grip,handles->grip);
            if(kernel::dimension_dot(d,d)<1e-12)return edge.annotation;
        }
    return {};
}
std::optional<QPointF> MeshView::symbol_handle_position(int index)const {
    if(index<0||index>2)return {};
    const auto a=symbol_handle_annotation();if(!a)return {};
    update_annotation_presentation();
    const auto camera=impl_->view().inverted();const auto r=camera.mapVector(QVector3D(1,0,0)).normalized(),u=camera.mapVector(QVector3D(0,1,0)).normalized();
    const kernel::Vec3 right{r.x(),r.y(),r.z()},up{u.x(),u.y(),u.z()};
    const bool left=kernel::dimension_dot(kernel::dimension_sub(a->contact,a->grip),right)<=0;
    const auto p=kernel::annotation_handles(*a,right,up,left,true,true)[index];
    auto q=(impl_->projection(width(),height())*impl_->view())*QVector4D(p.x,p.y,p.z,1);
    if(std::abs(q.w())<1e-9)return {};q/=q.w();
    return QPointF((q.x()+1)*width()/2.,(1-q.y())*height()/2.);
}
void MeshView::set_object_frame_provider(std::function<std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope>(const kernel::ViewerMesh&)> provider){impl_->object_frame_provider=std::move(provider);}
void MeshView::set_dimension_layout_resolver(std::function<std::optional<kernel::DimensionLayout>(const kernel::EdgeReference&)> resolver){impl_->dimension_layout_resolver=std::move(resolver);}
void MeshView::set_dimension_layout_commit(std::function<void(const kernel::EdgeReference&,kernel::DimensionLayout)> commit){impl_->dimension_layout_commit=std::move(commit);}
std::optional<kernel::ViewerDimension> MeshView::dimension_source(const ViewerCandidate& c)const {
    if(c.kind!=CandidateKind::Dimension)return {};
    const auto i=c.geometry_index;
    if(i<impl_->source_dimensions.size())return impl_->source_dimensions[i];
    if(i-impl_->source_dimensions.size()<impl_->source_transient_dimensions.size())return impl_->source_transient_dimensions[i-impl_->source_dimensions.size()];
    return {};
}
kernel::ModelEnvelope MeshView::dimension_envelope()const{return impl_->dimension_bounds;}
void MeshView::set_dimension_frame_visible(bool visible){impl_->show_dimension_frame=visible;update();}

void MeshView::set_dimension_display_units(DimensionDisplayUnits units) {
    if(!std::isfinite(units.millimetres_per_unit)||units.millimetres_per_unit<=0||
        !std::isfinite(units.degrees_per_unit)||units.degrees_per_unit<=0)
        throw std::invalid_argument("Invalid dimension display units");
    if(impl_->dimension_units==units)return;
    impl_->dimension_units=std::move(units);update();
}
QString MeshView::dimension_label_text(const kernel::ViewerDimension& dimension)const {
    const auto label=dimension_unit_presentation(dimension,impl_->dimension_decimal_places,impl_->dimension_units);
    auto primary=QString::fromStdString(label.primary);
    if(label.style)primary=dimension_render_text(*label.style,std::move(primary));
    return dimension_secondary_text(std::move(primary),QString::fromStdString(label.secondary));
}

void MeshView::set_dimension_decimal_places(int decimal_places) {
    impl_->dimension_decimal_places = std::clamp(decimal_places, 0, 12);
    update();
}

int MeshView::dimension_decimal_places() const {
    return impl_->dimension_decimal_places;
}

std::array<float, 8> MeshView::camera_state() const {
    return {impl_->orientation.scalar(), impl_->orientation.x(),
            impl_->orientation.y(), impl_->orientation.z(), impl_->view_scale,
            static_cast<float>(impl_->pan_pixels.x()),
            static_cast<float>(impl_->pan_pixels.y()),
            impl_->reference_view_scale};
}

void MeshView::set_camera_state(const std::array<float, 8>& state) {
    if (!std::all_of(state.begin(), state.end(), [](float value) {
            return std::isfinite(value);
        }) || state[4] <= 0.0F) return;
    impl_->orientation = QQuaternion(state[0], state[1], state[2], state[3]).normalized();
    impl_->view_scale = state[4];
    impl_->pan_pixels = QPointF(state[5], state[6]);
    // reference_view_scale must be restored alongside view_scale so the
    // screen-constant origin axis/plane ratio (view_scale /
    // reference_view_scale) stays 1.0 across saves/undo/tab switches, not
    // just right after fit_all(). A missing/zero stored value falls back to
    // view_scale itself (equivalent to "just fit").
    impl_->reference_view_scale = state[7] > 0.0F ? state[7] : impl_->view_scale;
    impl_->reference_view_scale_initialized = true;
    if (impl_->fly_navigation_enabled) {
        impl_->place_fly_camera_at_orbit_position();
    }
    impl_->candidates.clear();
    impl_->rebuild_persisted_reference_mesh();
    update();
}

std::vector<CandidateKind> MeshView::selection_contract() const {
    return impl_->allowed_kinds;
}

std::function<bool(const ViewerCandidate&)> MeshView::candidate_filter() const {
    return impl_->candidate_filter;
}

void MeshView::set_selection_filter(SelectionFilter filter) {
    if (impl_->selection_filter == filter) return;
    impl_->selection_filter = filter;
    clear_selection();
    if (impl_->empty_confirmation_callback) impl_->empty_confirmation_callback();
}

SelectionFilter MeshView::selection_filter() const { return impl_->selection_filter; }

void MeshView::set_selection_contract(std::vector<CandidateKind> allowed_kinds) {
    impl_->allowed_kinds = std::move(allowed_kinds);
    impl_->candidate_filter = {};
    impl_->candidate_priority = {};
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    update();
}

void MeshView::set_geometry_editing_presentation(bool editing,
        const std::string& curve_owner, const std::string& instance_path) {
    if(impl_->geometry_editing_presentation==editing &&
       impl_->editing_curve_owner==curve_owner && impl_->editing_curve_path==instance_path)return;
    impl_->geometry_editing_presentation=editing;
    impl_->editing_curve_owner=editing?curve_owner:std::string{};
    impl_->editing_curve_path=editing?instance_path:std::string{};
    update();
}
bool MeshView::sketch_hv_swapped(const std::string& owner,const std::string& path) const {
    if(owner.empty())return false;
    const auto key=std::pair{owner,path};auto [entry,inserted]=impl_->sketch_hv_mapping.try_emplace(key,false);
    const kernel::ViewerAxis *x=nullptr,*y=nullptr;
    for(const auto& axis:impl_->mesh.axes)if(axis.reference.owner_id==owner&&axis.reference.instance_path==path) {
        if(axis.reference.semantic_key=="sketch_axis:x")x=&axis;
        else if(axis.reference.semantic_key=="sketch_axis:y")y=&axis;
    }
    if(!x||!y)return entry->second;
    const auto rotated=[&](const auto& v){return impl_->orientation.rotatedVector(QVector3D(v.x,v.y,v.z));};
    const bool next=sketch_hv_swap_for_view(rotated(x->direction),rotated(y->direction),entry->second);
    if(next!=entry->second) {
        entry->second=next;
        if(impl_->sketch_hv_changed&&!impl_->sketch_hv_notification_pending) {
            impl_->sketch_hv_notification_pending=true;
            QTimer::singleShot(0,const_cast<MeshView*>(this),[this]{
                impl_->sketch_hv_notification_pending=false;
                if(impl_->sketch_hv_changed)impl_->sketch_hv_changed();
            });
        }
    }
    return entry->second;
}
std::string MeshView::sketch_hv_label(std::string label,const std::string& owner,const std::string& path) const {
    if((label=="H"||label=="V")&&sketch_hv_swapped(owner,path))return label=="H"?"V":"H";
    return label;
}
void MeshView::set_sketch_hv_changed_callback(std::function<void()> callback) {
    impl_->sketch_hv_changed=std::move(callback);
}

void MeshView::set_active_sketch_owner(std::string owner_id) {
    if (impl_->active_sketch_owner_id == owner_id) return;
    impl_->active_sketch_owner_id = std::move(owner_id);
    impl_->rebuild_sketch_interaction_mesh();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    update();
}

bool MeshView::advances_selection_on_hover() const {
    return impl_->advance_selection_on_hover;
}

void MeshView::set_candidate_filter(
    std::function<bool(const ViewerCandidate&)> candidate_filter, bool advance_on_hover) {
    impl_->advance_selection_on_hover = advance_on_hover;
    impl_->candidate_filter = std::move(candidate_filter);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    update();
}

void MeshView::set_candidate_priority(
    std::function<int(const ViewerCandidate&)> candidate_priority) {
    impl_->candidate_priority = std::move(candidate_priority);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    update();
}

void MeshView::confirm_result_body() {
    if (impl_->mesh.triangles.empty()) {
        clear_selection();
        return;
    }
    // An empty-path Occurrence is never produced by ordinary hover.  It is a
    // deliberate Tree-only presentation token meaning "all displayed result
    // faces of this Part", while persisted leaf Containers remain the only
    // candidates offered under the pointer.
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate = ViewerCandidate{
        CandidateKind::Occurrence, 0.0, 0, {}, {}, {},
        CandidateGeometry::Display};
    update();
}

std::optional<ViewerCandidate> MeshView::confirmed_candidate() const {
    return impl_->confirmed_candidate;
}

std::optional<ViewerCandidate> MeshView::hovered_candidate() const {
    if (impl_->confirmed_candidate || impl_->candidates.empty() ||
        impl_->active_candidate >= impl_->candidates.size()) return std::nullopt;
    return impl_->candidates[impl_->active_candidate];
}

std::optional<ViewerCandidate> MeshView::offered_candidate() const {
    if (impl_->candidates.empty() ||
        impl_->active_candidate >= impl_->candidates.size()) return std::nullopt;
    return impl_->candidates[impl_->active_candidate];
}

std::vector<ViewerCandidate> MeshView::selection_candidates_at(
    const QPointF& position) const {
    update_annotation_presentation();
    if (width() <= 0 || height() <= 0 || (impl_->allowed_kinds.empty() && !impl_->component_origin_handle)) {
        return {};
    }
    const auto ray = ray_at(position);
    if (!ray) return {};
    const auto& [ray_origin, ray_direction] = *ray;
    if(impl_->command_snap_point_provider)
        impl_->command_snap_points=impl_->command_snap_point_provider(ray_origin,ray_direction);
    // Python parity: point/topology picking uses a 9 px radius scaled by the
    // Wayland device pixel ratio. Keep this expressed through the camera's
    // world-per-pixel conversion so the visible marker and its hit area stay
    // consistent at every zoom and DPI.
    const double world_tolerance =
        world_tolerance_for_pixels(9.0 * devicePixelRatioF());
    const auto sketch_owned_kind = [](CandidateKind kind) {
        return kind == CandidateKind::SketchPoint ||
            kind == CandidateKind::SketchSegment ||
            kind == CandidateKind::SketchCurve ||
            kind == CandidateKind::SketchAxis ||
            kind == CandidateKind::SketchConstraint ||
            kind == CandidateKind::SketchText ||
            kind == CandidateKind::SketchExternalReference ||
            kind == CandidateKind::SketchTrimPiece ||
            kind == CandidateKind::Dimension;
    };
    const bool sketch_only = !impl_->active_sketch_owner_id.empty() &&
        std::ranges::all_of(impl_->allowed_kinds, sketch_owned_kind);
    auto candidates = sketch_only
        ? ordered_viewer_candidates(
            impl_->sketch_interaction_mesh, zima::kernel::ViewerMesh{},
            ray_origin, ray_direction, world_tolerance)
        : ordered_viewer_candidates(
            impl_->mesh, impl_->persisted_reference_mesh,
            ray_origin, ray_direction, world_tolerance, impl_->offer_result_faces,impl_->offer_original_containers,impl_->offer_original_faces,
            impl_->display_picking_index?&*impl_->display_picking_index:nullptr,
            impl_->reference_picking_index?&*impl_->reference_picking_index:nullptr,
            [this](const auto& edge){return !impl_->passive_sketch_axis(edge)||impl_->sketch_axis_visible(edge);});
    if(!sketch_only&&impl_->sheet_container_picking_index&&
            std::ranges::find(impl_->allowed_kinds,CandidateKind::Container)!=impl_->allowed_kinds.end()) {
        const auto visible=ordered_ray_candidates(impl_->mesh,ray_origin,ray_direction,true,
            impl_->display_picking_index?&*impl_->display_picking_index:nullptr);
        const auto sheets=ordered_ray_candidates(impl_->sheet_container_pick_mesh,ray_origin,ray_direction,false,
            &*impl_->sheet_container_picking_index);
        for(const auto& hit:sheets) {
            const double tolerance=1e-8*std::max(1.,std::abs(hit.distance));
            if(std::ranges::none_of(visible,[&](const auto& face) {
                    return std::abs(face.distance-hit.distance)<=tolerance;
                }))continue;
            const auto duplicate=std::ranges::find_if(candidates,[&](const auto& candidate) {
                return candidate.kind==CandidateKind::Container&&
                    candidate.owner_id==hit.reference.sheet_owner&&
                    candidate.instance_path==hit.reference.instance_path;
            });
            if(duplicate!=candidates.end()) {
                if(!duplicate->marker_derived)continue;
                candidates.erase(duplicate);
            }
            candidates.insert(candidates.begin(),ViewerCandidate{CandidateKind::Container,
                hit.distance,hit.triangle,hit.reference.sheet_owner,{},
                hit.reference.instance_path,CandidateGeometry::Display});
        }
    }
    if (sketch_only) {
        for (auto& candidate : candidates) {
            const std::vector<std::size_t>* indices = nullptr;
            switch (candidate.kind) {
                case CandidateKind::SketchPoint:
                    indices = &impl_->sketch_point_indices;
                    break;
                case CandidateKind::SketchAxis:
                    indices = &impl_->sketch_axis_indices;
                    break;
                case CandidateKind::Dimension:
                    indices = &impl_->sketch_dimension_indices;
                    break;
                case CandidateKind::SketchConstraint:
                    indices = &impl_->sketch_constraint_indices;
                    break;
                case CandidateKind::SketchExternalReference:
                    indices = candidate.semantic_key.starts_with(
                            "external_point:") ||
                        candidate.semantic_key.starts_with(
                            "sketch_midpoint:") ||
                        candidate.semantic_key.starts_with(
                            "sketch_intersection:") ||
                        candidate.semantic_key.starts_with(
                            "sketch_curve_keypoint:")
                        ? &impl_->sketch_point_indices
                        : &impl_->sketch_edge_indices;
                    break;
                default:
                    indices = &impl_->sketch_edge_indices;
                    break;
            }
            if (candidate.geometry_index < indices->size()) {
                candidate.geometry_index =
                    (*indices)[candidate.geometry_index];
            }
        }
    }

    if (!impl_->command_snap_points.empty()) {
        zima::kernel::ViewerMesh command_mesh;
        command_mesh.points=impl_->command_snap_points;
        auto command_candidates=ordered_viewer_candidates(command_mesh,
            zima::kernel::ViewerMesh{},ray_origin,ray_direction,world_tolerance);
        for(auto& candidate:command_candidates)
            candidate.geometry_index+=impl_->mesh.points.size();
        candidates.insert(candidates.begin(),command_candidates.begin(),command_candidates.end());
    }

    // Dimensions are painted as a screen-space annotation. Their text,
    // witness lines and leaders therefore cannot be picked reliably by the
    // world-space ray test alone (and the text was not represented there at
    // all). Add those same visible parts to this one common candidate list,
    // which is subsequently consumed unchanged by hover, LMB, RMB and double
    // click.
    const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
    const auto project = [&](const zima::kernel::Vec3& point) {
        QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
        if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
        return QPointF((clip.x() + 1.0F) * width() / 2.0F,
            (1.0F - clip.y()) * height() / 2.0F);
    };
    const QFontMetricsF metrics(font());
    constexpr double hit_radius = 9.0;
    const std::size_t persisted_dimension_count = impl_->mesh.dimensions.size();
    const std::size_t displayed_dimension_count =
        persisted_dimension_count + impl_->transient_dimensions.size();
    for (std::size_t index = 0; index < displayed_dimension_count; ++index) {
        const auto& dimension = index < persisted_dimension_count
            ? impl_->mesh.dimensions[index]
            : impl_->transient_dimensions[index - persisted_dimension_count];
        if(impl_->dimension_visibility_filter&&!impl_->dimension_visibility_filter(dimension))continue;
        const QPointF witness_first = project(dimension.witness_first);
        const QPointF witness_second = project(dimension.witness_second);
        const QPointF line_first = project(dimension.line_first);
        const QPointF line_second = project(dimension.line_second);
        const auto text=dimension_label_text(dimension);
        const auto layout=dimension_text_presentation(dimension,project,font(),text,.5*logicalDpiX()/25.4,1.5,10,3,false,1.5*logicalDpiX()/25.4,impl_->active_sketch_owner_id.empty());
        if(!layout.valid) {
            std::erase_if(candidates,[index](const auto& c){return c.kind==CandidateKind::Dimension && c.geometry_index==index;});
            continue;
        }
        QTransform text_transform;text_transform.translate(layout.text_baseline.x(),layout.text_baseline.y());text_transform.rotate(layout.text_angle);
        auto text_bounds=text_transform.mapRect(dimension_text_box(font(),text,0,kernel::dimension_is_basic(dimension)));
        text_bounds.adjust(-hit_radius,-hit_radius,hit_radius,hit_radius);
        // A dimension is an editable annotation, not selectable model
        // geometry. Offer it only over its visible text; witness/leader/arc
        // strokes remain available to the geometry underneath and therefore
        // cannot steal hover from edges, faces or reference planes.
        const bool hit = dimension.rotation_handle
            ? QLineF(position, layout.handles[0]).length() <= hit_radius || text_bounds.contains(position)
            : text_bounds.contains(position);
        if (!hit) continue;
        std::erase_if(candidates, [index](const auto& candidate) {
            return candidate.kind == CandidateKind::Dimension &&
                candidate.geometry_index == index;
        });
        // A dimension witness commonly terminates exactly on a Sketch point.
        // Keep the point first so a normal press can drag geometry; the same
        // Dimension remains immediately behind it for RMB cycling and can be
        // selected directly on its line, leader or text away from the point.
        const auto insertion = std::find_if(candidates.begin(), candidates.end(),
            [](const auto& candidate) {
                return candidate.kind != CandidateKind::SketchPoint;
            });
        candidates.insert(insertion, ViewerCandidate{
            CandidateKind::Dimension, 0.0, index,
            dimension.reference.owner_id, dimension.reference.semantic_key,
            dimension.reference.instance_path});
    }
    // Constraint labels sharing one geometric anchor are painted in separate
    // slots to the right (for example C T). Pick those actual visible slots,
    // not only the common invisible anchor, so the second and later relation
    // can be hovered and selected directly.
    auto constraint_font = font();
    constraint_font.setBold(true);
    const QFontMetricsF constraint_metrics(constraint_font);
    std::map<std::pair<int, int>, int> constraint_slots;
    for (std::size_t index = 0;
         index < impl_->mesh.constraint_markers.size(); ++index) {
        const auto& marker = impl_->mesh.constraint_markers[index];
        const QPointF anchor = project(marker.position);
        const auto key = std::pair{
            static_cast<int>(std::lround(anchor.x())),
            static_cast<int>(std::lround(anchor.y()))};
        const int slot = constraint_slots[key]++;
        if (marker.reference.semantic_key.starts_with("offset:")) continue;
        const QPointF baseline = anchor + QPointF(7.0 + slot * 16.0, -7.0);
        QRectF bounds = constraint_metrics.boundingRect(
            QString::fromStdString(sketch_hv_label(marker.label,marker.reference.owner_id,marker.reference.instance_path)));
        bounds.moveTopLeft(
            baseline + QPointF(0.0, -constraint_metrics.ascent()));
        // Match the same 9 px ordinary picking radius, with a small allowance
        // for the anchor having first been located anywhere inside its own
        // point marker. This keeps every visibly separated relation slot
        // directly selectable instead of making only the first one reliable.
        bounds.adjust(-12.0, -12.0, 12.0, 12.0);
        if (!bounds.contains(position)) continue;
        std::erase_if(candidates, [index](const auto& candidate) {
            return candidate.kind == CandidateKind::SketchConstraint &&
                candidate.geometry_index == index;
        });
        candidates.insert(candidates.begin(), ViewerCandidate{
            CandidateKind::SketchConstraint, 0.0, index,
            marker.reference.owner_id, marker.reference.semantic_key,
            marker.reference.instance_path});
    }
    if (!impl_->active_sketch_owner_id.empty()) {
        std::erase_if(candidates, [&](const auto& candidate) {
            // The active Sketch's editable display packet is current. Its
            // original reference packet still describes the committed profile
            // and must not offer a second, stale curve or point during edits.
            // Keep other occurrences and explicit external sources available.
            if (candidate.geometry == CandidateGeometry::OriginalReference &&
                candidate.owner_id == impl_->active_sketch_owner_id &&
                candidate.instance_path == impl_->editing_curve_path) return true;
            // An explicit reference-entry command may consume another Sketch's
            // persisted geometry. Its command filter owns source eligibility;
            // ordinary Sketch editing still offers only its own editable items.
            const bool external_source = !sketch_only && impl_->candidate_filter &&
                candidate.geometry == CandidateGeometry::OriginalReference &&
                (candidate.kind == CandidateKind::SketchPoint ||
                 candidate.kind == CandidateKind::SketchSegment ||
                 candidate.kind == CandidateKind::SketchCurve);
            return sketch_owned_kind(candidate.kind) &&
                candidate.owner_id != impl_->active_sketch_owner_id &&
                !external_source;
        });
    }
    std::erase_if(candidates, [&](const auto& candidate) {
        return !impl_->origin_visible({candidate.owner_id,candidate.semantic_key,candidate.instance_path});
    });
    std::erase_if(candidates,[&](const auto& candidate){
        if(candidate.kind==CandidateKind::Container&&candidate.marker_derived) {
            const auto& points=candidate.geometry==CandidateGeometry::OriginalReference?impl_->persisted_reference_mesh.points:impl_->mesh.points;
            if(candidate.geometry_index<points.size())return !impl_->helper_point_visible(points[candidate.geometry_index]);
        }
        if(candidate.kind!=CandidateKind::Vertex&&candidate.kind!=CandidateKind::SketchPoint&&
            !(candidate.kind==CandidateKind::Container&&(candidate.semantic_key=="point"||
                !kernel::axis_endpoint_parent_key(candidate.semantic_key).empty()||candidate.semantic_key.starts_with("point:from:"))))return false;
        const auto& points=candidate.geometry==CandidateGeometry::OriginalReference?impl_->persisted_reference_mesh.points:impl_->mesh.points;
        return candidate.geometry_index<points.size()&&!impl_->helper_point_visible(points[candidate.geometry_index],true);
    });
    const bool origin_geometry_requested = impl_->component_origin_handle || impl_->editing_origin_visible ||
        std::ranges::any_of(impl_->allowed_kinds, [](CandidateKind kind) {
            return kind == CandidateKind::Vertex ||
                kind == CandidateKind::Axis || kind == CandidateKind::Plane;
        });
    if (!impl_->show_origins && !origin_geometry_requested) {
        // Hidden means both visually hidden and absent from the ordinary
        // common picker. An active point/axis/plane-taking command remains
        // an explicit exception and makes the same origin entities visible
        // and selectable for that command.
        std::erase_if(candidates, [](const auto& candidate) {
            return candidate.semantic_key == "origin" ||
                candidate.semantic_key.starts_with("origin:");
        });
    }
    if(impl_->dimension_visibility_filter)std::erase_if(candidates,[&](const auto& candidate){
        if(candidate.kind!=CandidateKind::Dimension)return false;
        const auto index=candidate.geometry_index;
        const auto* dimension=index<impl_->mesh.dimensions.size()?&impl_->mesh.dimensions[index]:
            index-impl_->mesh.dimensions.size()<impl_->transient_dimensions.size()?&impl_->transient_dimensions[index-impl_->mesh.dimensions.size()]:nullptr;
        return dimension&&!impl_->dimension_visibility_filter(*dimension);
    });
    if (!impl_->show_symbols) std::erase_if(candidates, [](const auto& candidate) { return candidate.kind == CandidateKind::Symbol; });
    if (!impl_->show_dimensions) std::erase_if(candidates, [](const auto& candidate) { return candidate.kind == CandidateKind::Dimension; });
    auto filtered = filter_candidates(candidates,
        impl_->allowed_kinds, impl_->candidate_filter);
    if (impl_->drag_begin_callback) {
        // In normal Sketch editing a point under the pointer owns the direct
        // drag before annotations or curve wires at the same screen pixel.
        // Put that policy into the one shared candidate list so hover, LMB
        // and RMB all see the same order; RMB can still cycle to a Dimension
        // or another overlapping point without LMB replacing its choice.
        std::stable_partition(filtered.begin(), filtered.end(),
            [](const auto& candidate) {
                return candidate.kind == CandidateKind::SketchPoint;
            });
    }
    if (impl_->candidate_priority) {
        std::stable_sort(filtered.begin(), filtered.end(),
            [&](const auto& left, const auto& right) {
                return impl_->candidate_priority(left) <
                    impl_->candidate_priority(right);
            });
    }
    if(!impl_->candidate_priority&&!impl_->active_sketch_owner_id.empty()) {
        const auto priority=[&](const auto& candidate) {
            return candidate.owner_id==impl_->active_sketch_owner_id&&candidate.instance_path==impl_->editing_curve_path
                ? sketch_edit_pick_priority(candidate):5;
        };
        std::stable_sort(filtered.begin(),filtered.end(),[&](const auto& left,const auto& right){return priority(left)<priority(right);});
    }
    if (impl_->component_origin_handle) {
        const auto& key = *impl_->component_origin_handle;
        const auto is_handle = [&](const auto& candidate) {
            return candidate.kind == CandidateKind::Vertex && candidate.owner_id == key.owner_id &&
                candidate.semantic_key == key.semantic_key && candidate.instance_path == key.instance_path;
        };
        // Reuse the original point hit and tolerance. A manipulator is an
        // explicit command candidate even while a reference field owns input.
        if (const auto point = std::find_if(candidates.begin(), candidates.end(), is_handle);
            point != candidates.end()) {
            std::erase_if(filtered,is_handle);
            filtered.insert(filtered.begin(),*point);
        }
    }
    // BOM regions stay above text/dimension hit boxes and Sketch drag priorities.
    // Apply this once to the final shared list used by hover, LMB and RMB.
    std::stable_partition(filtered.begin(),filtered.end(),[](const auto& candidate){return candidate.kind==CandidateKind::TemplateRegion;});
    // Final user gate also covers command-injected origin handles. Commands
    // may replace their own contract/filter, never this independent policy.
    std::erase_if(filtered, [&](const auto& candidate) {
        return !matches_selection_filter(candidate, impl_->selection_filter);
    });
    return filtered;
}

std::optional<zima::kernel::ViewerEdge> MeshView::candidate_edge(
    const ViewerCandidate& candidate) const {
    const auto& edges = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.edges : impl_->mesh.edges;
    if (candidate.geometry_index >= edges.size()) return std::nullopt;
    const auto& edge = edges[candidate.geometry_index];
    if (edge.reference.owner_id != candidate.owner_id ||
        edge.reference.semantic_key != candidate.semantic_key ||
        edge.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return edge;
}

std::optional<zima::kernel::ViewerEdge> MeshView::display_edge(
    const zima::kernel::EdgeReference& reference) const {
    if (!reference.valid()) return std::nullopt;
    const auto found = std::ranges::find_if(impl_->mesh.edges,
        [&](const auto& edge) { return edge.reference == reference; });
    return found == impl_->mesh.edges.end()
        ? std::nullopt
        : std::optional<zima::kernel::ViewerEdge>{*found};
}

std::optional<zima::kernel::ViewerPoint> MeshView::candidate_point(
    const ViewerCandidate& candidate) const {
    const auto& points = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.points : impl_->mesh.points;
    const auto index=candidate.geometry_index;
    const auto* source=index<points.size() ? &points[index]
        : candidate.geometry==CandidateGeometry::Display &&
          index-points.size()<impl_->command_snap_points.size()
            ? &impl_->command_snap_points[index-points.size()] : nullptr;
    if(source==nullptr) return std::nullopt;
    const auto& point=*source;
    if (point.reference.owner_id != candidate.owner_id ||
        point.reference.semantic_key != candidate.semantic_key ||
        point.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return point;
}

std::optional<zima::kernel::ViewerAxis> MeshView::candidate_axis(
    const ViewerCandidate& candidate) const {
    const auto& axes = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.axes : impl_->mesh.axes;
    if (candidate.geometry_index >= axes.size()) return std::nullopt;
    const auto& axis = axes[candidate.geometry_index];
    if (axis.reference.owner_id != candidate.owner_id ||
        axis.reference.semantic_key != candidate.semantic_key ||
        axis.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return axis;
}

std::vector<zima::kernel::EdgeReference> MeshView::tangent_edge_route(
    const ViewerCandidate& seed, double angular_tolerance_degrees) const {
    if (seed.kind != CandidateKind::Edge) return {};
    const auto& edges = seed.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.edges : impl_->mesh.edges;
    return zima::kernel::tangent_edge_route(edges,
        {seed.owner_id,seed.semantic_key,seed.instance_path},
        angular_tolerance_degrees,world_tolerance_for_pixels(0.05),seed.geometry_index);
}

std::set<std::size_t> MeshView::edge_treatment_boundary_edge_indices(
    const std::string& owner_id,
    const std::string& instance_path) const {
    std::set<std::size_t> result;
    const auto found = impl_->edge_treatment_boundary_edge_indices.find(
        {owner_id, instance_path});
    if (found != impl_->edge_treatment_boundary_edge_indices.end()) {
        result.insert(found->second.begin(), found->second.end());
    }
    return result;
}

std::optional<zima::kernel::Vec3> MeshView::candidate_face_normal(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Face && candidate.kind != CandidateKind::Plane) return std::nullopt;
    const auto resolve = [&](const auto& mesh) -> std::optional<zima::kernel::Vec3> {
        std::size_t triangle = candidate.geometry_index;
        if(candidate.kind==CandidateKind::Plane) {
            const auto found=std::ranges::find_if(mesh.triangle_references,[&](const auto& reference){return reference.owner_id==candidate.owner_id&&reference.semantic_key==candidate.semantic_key&&reference.instance_path==candidate.instance_path;});
            if(found==mesh.triangle_references.end())return std::nullopt;
            triangle=static_cast<std::size_t>(found-mesh.triangle_references.begin());
        }
        if (triangle * 3 + 2 >= mesh.triangles.size() ||
            triangle >= mesh.triangle_references.size()) return std::nullopt;
        const auto& reference = mesh.triangle_references[triangle];
        if (reference.owner_id != candidate.owner_id ||
            reference.semantic_key != candidate.semantic_key ||
            reference.instance_path != candidate.instance_path) {
            return std::nullopt;
        }
        const auto first = mesh.triangles[triangle * 3];
        const auto second = mesh.triangles[triangle * 3 + 1];
        const auto third = mesh.triangles[triangle * 3 + 2];
        if (first >= mesh.vertices.size() || second >= mesh.vertices.size() ||
            third >= mesh.vertices.size()) return std::nullopt;
        const auto& a = mesh.vertices[first];
        const auto& b = mesh.vertices[second];
        const auto& c = mesh.vertices[third];
        const zima::kernel::Vec3 edge_one{b.x - a.x, b.y - a.y, b.z - a.z};
        const zima::kernel::Vec3 edge_two{c.x - a.x, c.y - a.y, c.z - a.z};
        zima::kernel::Vec3 normal{
            edge_one.y * edge_two.z - edge_one.z * edge_two.y,
            edge_one.z * edge_two.x - edge_one.x * edge_two.z,
            edge_one.x * edge_two.y - edge_one.y * edge_two.x};
        const double length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
            normal.z * normal.z);
        if (!std::isfinite(length) || length <= 1.0e-12) return std::nullopt;
        normal.x /= length; normal.y /= length; normal.z /= length;
        return normal;
    };
    if(candidate.kind==CandidateKind::Plane) {
        if(auto normal=resolve(impl_->mesh.original_references))return normal;
        return resolve(impl_->mesh);
    }
    return candidate.geometry == CandidateGeometry::OriginalReference
        ? resolve(impl_->mesh.original_references) : resolve(impl_->mesh);
}

std::vector<zima::kernel::Vec3> MeshView::candidate_face_triangles(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Face) return {};
    const auto resolve = [&](const auto& mesh) {
        std::vector<zima::kernel::Vec3> result;
        for (std::size_t triangle = 0;
             triangle < mesh.triangle_references.size(); ++triangle) {
            if (mesh.triangle_references[triangle].owner_id != candidate.owner_id ||
                mesh.triangle_references[triangle].semantic_key !=
                    candidate.semantic_key ||
                mesh.triangle_references[triangle].instance_path !=
                    candidate.instance_path ||
                triangle * 3 + 2 >= mesh.triangles.size()) {
                continue;
            }
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto vertex = mesh.triangles[triangle * 3 + corner];
                if (vertex >= mesh.vertices.size()) {
                    return std::vector<zima::kernel::Vec3>{};
                }
                result.push_back(mesh.vertices[vertex]);
            }
        }
        return result;
    };
    return candidate.geometry == CandidateGeometry::OriginalReference
        ? resolve(impl_->mesh.original_references) : resolve(impl_->mesh);
}

const zima::kernel::ViewerMesh& MeshView::mesh() const { return impl_->mesh; }

void MeshView::confirm_container_component(const std::string& owner_id,
        const std::string& component, std::set<std::size_t> edge_indices,
        const std::string& instance_path) {
    clear_selection();
    impl_->component_candidate=ViewerCandidate{CandidateKind::Container,0.0,0,
        owner_id,"component:"+component,instance_path,CandidateGeometry::Display};
    impl_->component_edge_indices=std::move(edge_indices);
    impl_->component_wire.clear();
    impl_->confirmed_candidate=impl_->component_candidate;
    update();
}

std::set<std::size_t> MeshView::confirmed_component_edge_indices() const {
    return impl_->confirmed_candidate && impl_->confirmed_candidate==impl_->component_candidate
        ? impl_->component_edge_indices : std::set<std::size_t>{};
}

void MeshView::confirm_container_component_wire(const std::string& owner_id,
        const std::string& component,std::vector<zima::kernel::ViewerEdge> wire,
        const std::string& instance_path) {
    confirm_container_component(owner_id,component,{},instance_path);
    std::erase_if(wire, zima::kernel::smooth_transition_junction);
    impl_->component_wire=std::move(wire);
    update();
}

std::vector<zima::kernel::ViewerEdge> MeshView::confirmed_component_wire() const {
    return impl_->confirmed_candidate && impl_->confirmed_candidate==impl_->component_candidate
        ? impl_->component_wire : std::vector<zima::kernel::ViewerEdge>{};
}

void MeshView::set_container_preview_provider(std::function<std::vector<kernel::ViewerEdge>(
        const std::string&,const std::string&)> provider,
        std::function<bool(const std::string&,const std::string&)> authoritative) {
    impl_->container_preview_provider=std::move(provider);
    impl_->container_preview_authoritative=std::move(authoritative);
    impl_->selected_source_key.reset();
    update();
}

void MeshView::set_container_inspection(const std::string& owner_id,
        const std::string& instance_path) {
    ++impl_->inspection_wire_revision;
    impl_->inspected_container=owner_id.empty()?std::nullopt:std::optional<ViewerCandidate>{
        ViewerCandidate{CandidateKind::Container,0,0,owner_id,{},instance_path,CandidateGeometry::Display}};
    impl_->container_inspection_wire=impl_->container_preview_wire(owner_id,instance_path);
    update();
}

const std::vector<zima::kernel::ViewerEdge>& MeshView::container_inspection_wire() const {
    return impl_->container_inspection_wire;
}

void MeshView::set_container_inspection_wire(std::vector<zima::kernel::ViewerEdge> wire) {
    impl_->container_inspection_wire=std::move(wire);
    ++impl_->inspection_wire_revision;
    if(impl_->container_inspection_wire.empty())impl_->inspected_container.reset();
    update();
}

void MeshView::confirm_container(const std::string& owner_id) {
    impl_->container_inspection_wire.clear();
    impl_->inspected_container.reset();
    auto candidate = impl_->authored_container_candidate(owner_id);
    if (!candidate) {
        clear_selection();
        return;
    }
    impl_->confirmed_candidate = std::move(candidate);
    // "point"/"axis"/"plane" are the three container kinds whose own
    // defining-point marker lives at `owner_id + ":origin"` -- see
    // construction_viewer_mesh's Point/Axis/Plane marker pushes.
    impl_->selected_container_origin_id =
        (impl_->confirmed_candidate->semantic_key == "point" ||
         impl_->confirmed_candidate->semantic_key == "axis" ||
         impl_->confirmed_candidate->semantic_key == "plane")
        ? owner_id + ":origin" : std::string{};
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_occurrence(const std::string& instance_path) {
    impl_->container_inspection_wire.clear();
    impl_->inspected_container.reset();
    auto candidate = occurrence_candidate(impl_->mesh, instance_path);
    if (!candidate) {
        clear_selection();
        return;
    }
    impl_->confirmed_candidate = std::move(candidate);
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_origin(const std::string& owner_id,
    const std::string& instance_path) {
    impl_->confirmed_candidate = ViewerCandidate{CandidateKind::Container, 0.0, 0,
        owner_id, "origin", instance_path, CandidateGeometry::Display};
    impl_->selected_container_origin_id = owner_id;
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_reference(const std::string& owner_id,
    const std::string& semantic_key, const std::string& instance_path,
    CandidateKind kind) {
    ViewerCandidate candidate{kind, 0.0, 0, owner_id, semantic_key,
        instance_path, CandidateGeometry::Display};
    if(kind==CandidateKind::Container) {
        const auto found=impl_->authored_container_candidate(owner_id,instance_path);
        if(!found)return clear_selection();
        candidate=*found;
    } else if (kind == CandidateKind::Vertex || kind == CandidateKind::SketchPoint ||
        (kind == CandidateKind::SketchExternalReference &&
         semantic_key.starts_with("external_point:"))) {
        auto found = std::find_if(impl_->mesh.points.begin(),
            impl_->mesh.points.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.points.end() && semantic_key == "origin:point") {
            found = std::find_if(impl_->mesh.points.begin(),
                impl_->mesh.points.end(), [&](const auto& value) {
                    return value.reference.owner_id == owner_id &&
                        value.reference.semantic_key == "point" &&
                        value.reference.instance_path == instance_path;
                });
            if (found != impl_->mesh.points.end()) {
                candidate.semantic_key = "point";
            }
        }
        if (found == impl_->mesh.points.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.points.begin(), found));
    } else if (kind == CandidateKind::SketchSegment ||
               kind == CandidateKind::SketchCurve ||
               kind == CandidateKind::SketchText ||
               kind == CandidateKind::TemplateRegion ||
               kind == CandidateKind::TemplateImage ||
               kind == CandidateKind::Symbol ||
               kind == CandidateKind::SketchExternalReference) {
        const auto found = std::find_if(impl_->mesh.edges.begin(),
            impl_->mesh.edges.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    (value.reference.semantic_key == semantic_key ||
                     value.reference.semantic_key.starts_with(semantic_key + ":")) &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.edges.end()) return clear_selection();
        candidate.semantic_key = found->reference.semantic_key;
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.edges.begin(), found));
    } else if (kind == CandidateKind::Axis) {
        const auto found = std::find_if(impl_->mesh.axes.begin(),
            impl_->mesh.axes.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.axes.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.axes.begin(), found));
    } else if (kind == CandidateKind::Dimension) {
        const auto found = std::find_if(impl_->mesh.dimensions.begin(),
            impl_->mesh.dimensions.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found != impl_->mesh.dimensions.end()) {
            candidate.geometry_index = static_cast<std::size_t>(
                std::distance(impl_->mesh.dimensions.begin(), found));
        } else {
            const auto transient = std::find_if(
                impl_->transient_dimensions.begin(),
                impl_->transient_dimensions.end(), [&](const auto& value) {
                    return value.reference.owner_id == owner_id &&
                        value.reference.semantic_key == semantic_key &&
                        value.reference.instance_path == instance_path;
                });
            if (transient == impl_->transient_dimensions.end()) {
                return clear_selection();
            }
            candidate.geometry_index = impl_->mesh.dimensions.size() +
                static_cast<std::size_t>(std::distance(
                    impl_->transient_dimensions.begin(), transient));
        }
    } else if (kind == CandidateKind::SketchConstraint) {
        const auto found = std::find_if(impl_->mesh.constraint_markers.begin(),
            impl_->mesh.constraint_markers.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.constraint_markers.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.constraint_markers.begin(), found));
    } else if (kind == CandidateKind::Face) {
        const auto& display_references = impl_->mesh.triangle_references;
        const auto display = std::find_if(
            display_references.begin(), display_references.end(),
            [&](const auto& value) {
                return value.owner_id == owner_id &&
                    value.semantic_key == semantic_key &&
                    value.instance_path == instance_path;
            });
        if (display != display_references.end()) {
            candidate.geometry_index = static_cast<std::size_t>(
                std::distance(display_references.begin(), display));
        } else {
            const auto& original =
                impl_->mesh.original_references.triangle_references;
            const auto found = std::find_if(original.begin(), original.end(),
                [&](const auto& value) {
                    return value.owner_id == owner_id &&
                        value.semantic_key == semantic_key &&
                        value.instance_path == instance_path;
                });
            if (found == original.end()) return clear_selection();
            candidate.geometry = CandidateGeometry::OriginalReference;
            candidate.geometry_index = static_cast<std::size_t>(
                std::distance(original.begin(), found));
        }
    } else if (kind == CandidateKind::Plane) {
        const auto& references = impl_->mesh.original_references.triangle_references;
        const auto found = std::find_if(references.begin(), references.end(),
            [&](const auto& value) {
                return value.owner_id == owner_id &&
                    value.semantic_key == semantic_key &&
                    value.instance_path == instance_path;
            });
        if (found == references.end()) return clear_selection();
        candidate.geometry = CandidateGeometry::OriginalReference;
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(references.begin(), found));
    } else {
        return clear_selection();
    }
    impl_->confirmed_candidate = std::move(candidate);
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    update();
}

void MeshView::clear_selection() {
    impl_->container_inspection_wire.clear();
    impl_->inspected_container.reset();
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->sketch_box_selected_candidates.clear();
    update();
}

void MeshView::set_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->confirmation_callback = std::move(callback);
}

void MeshView::set_empty_confirmation_callback(std::function<void()> callback) {
    impl_->empty_confirmation_callback = std::move(callback);
}

void MeshView::set_context_menu_callback(
    std::function<void(const ViewerCandidate&, const QPoint&)> callback) {
    impl_->context_menu_callback = std::move(callback);
}

void MeshView::set_world_click_callback(std::function<bool(
    const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback) {
    impl_->world_click_callback = std::move(callback);
}

void MeshView::set_world_pointer_callback(std::function<void(
    const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback) {
    impl_->world_pointer_callback = std::move(callback);
}

void MeshView::set_command_gesture_callbacks(
    std::function<bool(
        const std::optional<ViewerCandidate>&,
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> begin,
    std::function<void(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
    std::function<void()> end) {
    impl_->command_gesture_begin_callback = std::move(begin);
    impl_->command_gesture_update_callback = std::move(update);
    impl_->command_gesture_end_callback = std::move(end);
}

void MeshView::set_short_middle_click_callback(std::function<bool()> callback) {
    impl_->short_middle_click_callback = std::move(callback);
}

void MeshView::set_double_middle_click_callback(std::function<bool()> callback) {
    impl_->double_middle_click_callback = std::move(callback);
}

void MeshView::set_sketch_box_selection(bool enabled,
    std::function<void(std::vector<ViewerCandidate>, bool)> callback) {
    impl_->sketch_box_selection_enabled = enabled;
    impl_->sketch_box_selection_callback = std::move(callback);
    if (!enabled) {
        impl_->sketch_box_start.reset();
        impl_->sketch_box_end.reset();
    }
    update();
}

bool MeshView::confirm_current_pointer() {
    bool handled = impl_->short_middle_click_callback &&
        impl_->short_middle_click_callback();
    if (!handled && impl_->world_click_callback) {
        const auto ray = ray_at(impl_->last_pointer);
        handled = ray && impl_->world_click_callback(ray->first, ray->second);
    }
    if (handled) {
        impl_->candidates.clear();
        update();
    }
    return handled;
}

bool MeshView::refresh_current_pointer_preview() {
    if (!impl_->world_pointer_callback) return false;
    const auto ray = ray_at(impl_->last_pointer);
    if (!ray) return false;
    impl_->world_pointer_callback(ray->first, ray->second);
    return true;
}

QPoint MeshView::last_pointer_position() const { return impl_->last_pointer; }

std::optional<double> MeshView::candidate_dimension_value(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Dimension) return std::nullopt;
    const auto* dimension = candidate.geometry_index < impl_->mesh.dimensions.size()
        ? &impl_->mesh.dimensions[candidate.geometry_index]
        : candidate.geometry_index - impl_->mesh.dimensions.size() <
                impl_->transient_dimensions.size()
            ? &impl_->transient_dimensions[
                  candidate.geometry_index - impl_->mesh.dimensions.size()]
            : nullptr;
    if (dimension == nullptr) return std::nullopt;
    if (dimension->reference.owner_id != candidate.owner_id ||
        dimension->reference.semantic_key != candidate.semantic_key) {
        return std::nullopt;
    }
    return dimension->value;
}

std::optional<QPoint> MeshView::candidate_dimension_label_position(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Dimension ||
        width() <= 0 || height() <= 0) return std::nullopt;
    const auto* selected_dimension =
        candidate.geometry_index < impl_->mesh.dimensions.size()
        ? &impl_->mesh.dimensions[candidate.geometry_index]
        : candidate.geometry_index - impl_->mesh.dimensions.size() <
                impl_->transient_dimensions.size()
            ? &impl_->transient_dimensions[
                  candidate.geometry_index - impl_->mesh.dimensions.size()]
            : nullptr;
    if (selected_dimension == nullptr) return std::nullopt;
    const auto& dimension = *selected_dimension;
    if (dimension.reference.owner_id != candidate.owner_id ||
        dimension.reference.semantic_key != candidate.semantic_key) return std::nullopt;
    const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
    const auto project = [&](const zima::kernel::Vec3& point) {
        QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
        if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
        return QPointF((clip.x() + 1.0F) * width() / 2.0F,
            (1.0F - clip.y()) * height() / 2.0F);
    };
    const auto text=dimension_label_text(dimension);
    const QFontMetricsF metrics(font());
    const auto layout=dimension_text_presentation(dimension,project,font(),text,.5*logicalDpiX()/25.4,1.5,10,3,false,1.5*logicalDpiX()/25.4,impl_->active_sketch_owner_id.empty());
    if(!layout.valid)return std::nullopt;
    QTransform transform;transform.translate(layout.text_baseline.x(),layout.text_baseline.y());transform.rotate(layout.text_angle);
    return transform.map(dimension_text_box(font(),text,0,kernel::dimension_is_basic(dimension)).center()).toPoint();
}
void MeshView::set_dimension_placement_cycle_callback(std::function<bool()> callback) { impl_->dimension_placement_cycle_callback=std::move(callback); }

void MeshView::set_empty_right_click_callback(std::function<bool()> callback) {
    impl_->empty_right_click_callback = std::move(callback);
}

void MeshView::set_single_candidate_right_click_callback(
    std::function<bool(const ViewerCandidate&)> callback) {
    impl_->single_candidate_right_click_callback = std::move(callback);
}

void MeshView::set_candidate_right_click_callback(
    std::function<bool(const ViewerCandidate&, std::size_t, std::size_t)> callback) {
    impl_->candidate_right_click_callback = std::move(callback);
}

void MeshView::reset_candidate_cycle() {
    impl_->active_candidate = 0;
    update();
}

void MeshView::set_double_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->double_confirmation_callback = std::move(callback);
}

void MeshView::set_candidate_drag_callbacks(
    std::function<bool(const ViewerCandidate&, const zima::kernel::Vec3&,
        const zima::kernel::Vec3&)> begin,
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
    std::function<void()> end) {
    impl_->drag_begin_callback = std::move(begin);
    impl_->drag_update_callback = std::move(update);
    impl_->drag_end_callback = std::move(end);
}

std::vector<ViewerCandidate> MeshView::sketch_selection() const {
    auto result = impl_->sketch_box_selected_candidates;
    if (impl_->confirmed_candidate &&
        (impl_->confirmed_candidate->kind == CandidateKind::SketchSegment ||
         impl_->confirmed_candidate->kind == CandidateKind::SketchCurve ||
         impl_->confirmed_candidate->kind == CandidateKind::SketchPoint ||
         impl_->confirmed_candidate->kind == CandidateKind::SketchText ||
         impl_->confirmed_candidate->kind ==
             CandidateKind::SketchExternalReference) &&
        std::find(result.begin(), result.end(), *impl_->confirmed_candidate) ==
            result.end()) {
        result.push_back(*impl_->confirmed_candidate);
    }
    return result;
}

void MeshView::set_transient_edges(std::vector<zima::kernel::ViewerEdge> edges) {
    if (impl_->transient_point_transform) {
        for (auto& edge : edges) {
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,impl_->transient_point_transform);
            for (auto& point : edge.points) {
                point = impl_->transient_point_transform(point);
            }
        }
    }
    impl_->transient_edges = std::move(edges);
    // Every edge submission starts a fresh preview frame. Callers that need
    // active input markers append them immediately with set_transient_points.
    impl_->transient_points.clear();
    impl_->transient_model_points.clear();
    impl_->transient_labels.clear();
    update();
}

const std::vector<zima::kernel::ViewerEdge>& MeshView::transient_edges() const {
    return impl_->transient_edges;
}

void MeshView::set_transient_context_mesh(zima::kernel::ViewerMesh mesh) {
    // Dimensions have their own editable transient channel. Keeping them out
    // of this visual-only carrier preserves the existing candidate indices.
    mesh.dimensions.clear();
    if (impl_->transient_point_transform) {
        const auto transform=impl_->transient_point_transform;
        for(auto& point:mesh.vertices)point=transform(point);
        for(auto& edge:mesh.edges) {
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
            for(auto& point:edge.points)point=transform(point);
        }
        for(auto& point:mesh.points)point.position=transform(point.position);
        for(auto& axis:mesh.axes) {
            const auto origin=transform(axis.point);
            const auto endpoint=transform({
                axis.point.x+axis.direction.x,
                axis.point.y+axis.direction.y,
                axis.point.z+axis.direction.z});
            axis.point=origin;
            axis.direction={endpoint.x-origin.x,endpoint.y-origin.y,
                endpoint.z-origin.z};
        }
        for(auto& marker:mesh.constraint_markers)
            marker.position=transform(marker.position);
    }
    impl_->transient_context_mesh=std::move(mesh);
    setProperty("transientContextEdgeCount",
        static_cast<qulonglong>(impl_->transient_context_mesh.edges.size()));
    setProperty("transientContextPointCount",
        static_cast<qulonglong>(impl_->transient_context_mesh.points.size()));
    setProperty("transientContextAxisCount",
        static_cast<qulonglong>(impl_->transient_context_mesh.axes.size()));
    update();
}

const zima::kernel::ViewerMesh& MeshView::transient_context_mesh() const {
    return impl_->transient_context_mesh;
}

const std::vector<zima::kernel::Vec3>& MeshView::transient_points() const {
    return impl_->transient_points;
}

void MeshView::set_transient_dimensions(
    std::vector<zima::kernel::ViewerDimension> dimensions) {
    ++impl_->picker_overlay_revision;
    if (impl_->transient_point_transform) {
        const auto origin = impl_->transient_point_transform({});
        for (auto& dimension : dimensions) {
            dimension.witness_first =
                impl_->transient_point_transform(dimension.witness_first);
            dimension.witness_second =
                impl_->transient_point_transform(dimension.witness_second);
            dimension.line_first =
                impl_->transient_point_transform(dimension.line_first);
            dimension.line_second =
                impl_->transient_point_transform(dimension.line_second);
            if (dimension.label_position) {
                dimension.label_position =
                    impl_->transient_point_transform(*dimension.label_position);
            }
            if(dimension.measurement_direction) {
                const auto endpoint=impl_->transient_point_transform(*dimension.measurement_direction);
                dimension.measurement_direction=kernel::Vec3{endpoint.x-origin.x,endpoint.y-origin.y,endpoint.z-origin.z};
            }
            const auto normal_endpoint =
                impl_->transient_point_transform(dimension.plane_normal);
            dimension.plane_normal = {
                normal_endpoint.x - origin.x,
                normal_endpoint.y - origin.y,
                normal_endpoint.z - origin.z};
        }
    }
    const std::size_t persisted_count = impl_->mesh.dimensions.size();
    if (impl_->confirmed_candidate &&
        impl_->confirmed_candidate->kind == CandidateKind::Dimension &&
        impl_->confirmed_candidate->geometry_index >= persisted_count) {
        impl_->confirmed_candidate.reset();
    }
    std::erase_if(impl_->candidates, [persisted_count](const auto& candidate) {
        return candidate.kind == CandidateKind::Dimension &&
            candidate.geometry_index >= persisted_count;
    });
    impl_->source_transient_dimensions=dimensions;
    if(impl_->dimension_layout_resolver)for(auto& d:dimensions)
        if(auto layout=impl_->dimension_layout_resolver(d.reference))d=kernel::layout_dimension(d,impl_->object_bounds.contains({d.reference.owner_id,d.reference.instance_path})&&impl_->object_bounds.at({d.reference.owner_id,d.reference.instance_path}).valid?impl_->object_bounds.at({d.reference.owner_id,d.reference.instance_path}):impl_->dimension_bounds,*layout);
    impl_->transient_dimensions = std::move(dimensions);
    update();
}

std::size_t MeshView::base_mesh_revision() const {
    return impl_->base_mesh_revision;
}

void MeshView::set_command_snap_points(std::vector<zima::kernel::ViewerPoint> points,bool visible) {
    ++impl_->picker_overlay_revision;
    impl_->command_snap_point_provider={};
    impl_->command_snap_points_visible=visible;
    impl_->command_snap_points=std::move(points);
    impl_->candidates.clear();
    impl_->active_candidate=0;
    update();
}

void MeshView::set_command_snap_point_provider(std::function<std::vector<kernel::ViewerPoint>(
        const kernel::Vec3&,const kernel::Vec3&)> provider) {
    set_command_snap_points({});
    impl_->command_snap_point_provider=std::move(provider);
}

void MeshView::set_transient_points(std::vector<zima::kernel::Vec3> points) {
    impl_->transient_model_points.clear();
    if (impl_->transient_point_transform) {
        for (auto& point : points) point = impl_->transient_point_transform(point);
    }
    impl_->transient_points = std::move(points);
    update();
}

void MeshView::set_transient_model_points(std::vector<kernel::ViewerPoint> points) {
    if(impl_->transient_point_transform)for(auto& point:points)point.position=impl_->transient_point_transform(point.position);
    impl_->transient_points.clear();impl_->transient_model_points=std::move(points);update();
}
const std::vector<kernel::ViewerPoint>& MeshView::transient_model_points() const {return impl_->transient_model_points;}

const std::vector<std::pair<kernel::Vec3,std::string>>& MeshView::transient_labels() const {
    return impl_->transient_labels;
}
void MeshView::set_transient_labels(
    std::vector<std::pair<zima::kernel::Vec3, std::string>> labels) {
    if (impl_->transient_point_transform) {
        for (auto& [point, label] : labels) {
            static_cast<void>(label);
            point = impl_->transient_point_transform(point);
        }
    }
    impl_->transient_labels = std::move(labels);
    update();
}

void MeshView::set_sketch_cursor(std::optional<zima::kernel::Vec3> point,
    bool snapped, std::string relation_label) {
    if (point && impl_->transient_point_transform) {
        *point = impl_->transient_point_transform(*point);
    }
    impl_->sketch_cursor = std::move(point);
    impl_->sketch_cursor_snapped = snapped;
    impl_->sketch_cursor_label = std::move(relation_label);
    update();
}

void MeshView::set_extent_manipulator(
    std::optional<ExtentManipulator> manipulator) {
    set_extent_manipulators(manipulator
        ? std::vector<ExtentManipulator>{std::move(*manipulator)}
        : std::vector<ExtentManipulator>{});
}

void MeshView::set_extent_manipulators(
    std::vector<ExtentManipulator> manipulators) {
    for (auto& value : manipulators) {
      if (impl_->transient_point_transform) {
        const auto transformed_origin =
            impl_->transient_point_transform(value.origin);
        const auto transformed_end = impl_->transient_point_transform({
            value.origin.x + value.direction.x,
            value.origin.y + value.direction.y,
            value.origin.z + value.direction.z});
        value.origin = transformed_origin;
        value.direction = {transformed_end.x - transformed_origin.x,
            transformed_end.y - transformed_origin.y,
            transformed_end.z - transformed_origin.z};
      }
    }
    impl_->extent_manipulators = std::move(manipulators);
    if (impl_->extent_manipulators.empty()) {
        impl_->hovered_extent_key.clear();
        if (impl_->dragged_extent_key.empty())
            impl_->dragged_extent_manipulator.reset();
    }
    update();
}

const std::vector<ExtentManipulator>& MeshView::extent_manipulators() const {
    return impl_->extent_manipulators;
}

void MeshView::set_operation_direction_indicator(
        std::optional<OperationDirectionIndicator> indicator) {
    set_operation_direction_indicators(indicator
        ? std::vector<OperationDirectionIndicator>{std::move(*indicator)}
        : std::vector<OperationDirectionIndicator>{});
}

void MeshView::set_operation_direction_indicators(
        std::vector<OperationDirectionIndicator> indicators) {
    for (auto& value : indicators) {
      if (impl_->transient_point_transform) {
        const auto transformed_origin =
            impl_->transient_point_transform(value.origin);
        const auto transformed_direction_end =
            impl_->transient_point_transform({
                value.origin.x + value.direction.x,
                value.origin.y + value.direction.y,
                value.origin.z + value.direction.z});
        const auto transformed_radial_end =
            impl_->transient_point_transform({
                value.origin.x + value.radial.x,
                value.origin.y + value.radial.y,
                value.origin.z + value.radial.z});
        value.origin = transformed_origin;
        value.direction = {
            transformed_direction_end.x - transformed_origin.x,
            transformed_direction_end.y - transformed_origin.y,
            transformed_direction_end.z - transformed_origin.z};
        value.radial = {
            transformed_radial_end.x - transformed_origin.x,
            transformed_radial_end.y - transformed_origin.y,
            transformed_radial_end.z - transformed_origin.z};
      }
    }
    impl_->operation_direction_indicators = std::move(indicators);
    update();
}

void MeshView::set_extent_manipulator_callbacks(
    std::function<void(const std::string&)> begin,
    std::function<void(const std::string&, double)> update_callback,
    std::function<void()> end) {
    impl_->extent_begin_callback = std::move(begin);
    impl_->extent_update_callback = std::move(update_callback);
    impl_->extent_end_callback = std::move(end);
}

void MeshView::set_edge_treatment_selection_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->edge_treatment_selection_edges) return;
    impl_->edge_treatment_selection_edges = std::move(edges);
    update();
}

void MeshView::set_feature_hover_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->feature_hover_edges &&
        impl_->feature_hover_edge_indices.empty()) return;
    impl_->feature_hover_edges = std::move(edges);
    impl_->feature_hover_edge_indices.clear();
    update();
}

void MeshView::set_feature_selected_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->feature_selected_edges &&
        impl_->feature_selected_edge_indices.empty()) return;
    impl_->feature_selected_edges = std::move(edges);
    impl_->feature_selected_edge_indices.clear();
    update();
}

void MeshView::set_feature_hover_edge_indices(
    std::set<std::size_t> edges) {
    if (edges == impl_->feature_hover_edge_indices &&
        impl_->feature_hover_edges.empty()) return;
    impl_->feature_hover_edge_indices = std::move(edges);
    impl_->feature_hover_edges.clear();
    update();
}

void MeshView::set_feature_selected_edge_indices(
    std::set<std::size_t> edges) {
    if (edges == impl_->feature_selected_edge_indices &&
        impl_->feature_selected_edges.empty()) return;
    impl_->feature_selected_edge_indices = std::move(edges);
    impl_->feature_selected_edges.clear();
    update();
}

void MeshView::set_feature_preview_owners(std::set<std::string> owner_ids) {
    if (owner_ids == impl_->feature_preview_owner_ids) return;
    impl_->feature_preview_owner_ids = std::move(owner_ids);
    update();
}

void MeshView::set_sketch_relation_highlights(std::set<EdgeKey> references) {
    impl_->sketch_relation_highlights=std::move(references);update();
}

void MeshView::set_constraint_reference_highlights(
    std::set<std::string> owner_ids, std::set<EdgeKey> edges) {
    impl_->constraint_reference_owner_ids = std::move(owner_ids);
    if(edges.empty())impl_->confirmed_reference_faces.clear();
    if (impl_->constraint_reference_edges != edges) {
        impl_->constraint_reference_edges = std::move(edges);
        impl_->reference_faces_dirty = true;
    }
    update();
}

void MeshView::remember_reference_face(const ViewerCandidate& candidate) {
    if(candidate.kind!=CandidateKind::Face||candidate.geometry!=CandidateGeometry::Display)return;
    // Most recent confirmation wins when several fragments share a source.
    impl_->confirmed_reference_faces.insert(impl_->confirmed_reference_faces.begin(),candidate);
    impl_->reference_faces_dirty=true;update();
}

void MeshView::set_assembly_reference_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->assembly_reference_edges) return;
    impl_->assembly_reference_edges = std::move(edges);
    update();
}

void MeshView::set_selected_container_contents(std::set<std::string> owner_ids) {
    if (owner_ids == impl_->selected_container_content_ids) return;
    impl_->selected_container_content_ids = std::move(owner_ids);
    update();
}

void MeshView::set_object_overlay_main_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->object_overlay_main_edge_keys) return;
    impl_->object_overlay_main_edge_keys = std::move(edges);
    update();
}

void MeshView::set_edge_color_override(std::optional<QColor> color) {
    impl_->edge_color_override = std::move(color);
    update();
}

void MeshView::set_body_surface_styles(zima::kernel::SurfaceStyle base,
        std::map<std::string,zima::kernel::SurfaceStyle> instances,
        std::map<std::string,zima::kernel::SurfaceStyle> owners,
        std::map<std::string,zima::kernel::SurfaceStyle> faces) {
    impl_->surface_batches_dirty = true;
    impl_->surface_style=std::move(base);impl_->instance_styles=std::move(instances);
    impl_->owner_styles=std::move(owners);impl_->face_styles=std::move(faces);update();
}
void MeshView::set_result_face_selection(bool enabled) { impl_->offer_result_faces=enabled; clear_selection(); }
void MeshView::set_original_face_selection(bool enabled) {
    if (impl_->offer_original_faces == enabled) return;
    impl_->offer_original_faces = enabled;
    impl_->rebuild_persisted_reference_mesh();
    clear_selection();
}
void MeshView::set_original_container_selection(bool enabled) {
    if(impl_->offer_original_containers==enabled)return;
    impl_->offer_original_containers=enabled;impl_->rebuild_persisted_reference_mesh();clear_selection();
}

void MeshView::set_inspected_faces(std::vector<ViewerCandidate> faces){impl_->inspected_faces=std::move(faces);update();}

void MeshView::set_body_surface_colors(
    QColor default_color, std::map<std::string, QColor> instance_colors,
    std::map<std::string, QColor> face_colors) {
    impl_->surface_batches_dirty = true;
    impl_->body_surface_color = default_color.isValid()
        ? default_color : QColor("#B9C2CC");
    impl_->body_surface_instance_colors = instance_colors;
    impl_->surface_style.color=default_color.name(QColor::HexArgb).toStdString();
    impl_->instance_styles.clear();impl_->owner_styles.clear();impl_->face_styles.clear();
    for(const auto& [key,color]:instance_colors){auto style=impl_->surface_style;style.color=color.name(QColor::HexArgb).toStdString();impl_->instance_styles[key]=style;}
    for(const auto& [key,color]:face_colors){auto style=impl_->surface_style;style.color=color.name(QColor::HexArgb).toStdString();impl_->face_styles[key]=style;}
    impl_->body_surface_face_colors = std::move(face_colors);
    update();
}

void MeshView::set_transient_point_transform(
    std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)> transform) {
    impl_->transient_point_transform = std::move(transform);
    // Changing the document/occurrence coordinate mapping is not a request
    // to destroy the command preview. refresh_scene() re-establishes this
    // mapping while Primitive Properties is open; clearing here made the
    // already calculated cyan Box/Cylinder/... wire disappear immediately,
    // and rollback editing triggered the same loss a second time. Preview
    // lifetime is owned explicitly by set_transient_edges({}) when the
    // dialog closes or its operation changes.
    update();
}

double MeshView::world_tolerance_for_pixels(double pixels) const {
    if (!std::isfinite(pixels) || pixels < 0.0 || height() <= 0) return 0.0;
    double visible_half_height = impl_->view_scale;
    if (impl_->fly_navigation_enabled &&
        impl_->projection_mode == ProjectionMode::Perspective) {
        const double distance = (impl_->center - impl_->fly_position).length();
        visible_half_height = std::max(0.001, distance * std::tan(
            kPerspectiveFieldOfViewDegrees * std::numbers::pi / 360.0));
    }
    return 2.0 * visible_half_height * pixels /
        static_cast<double>(height());
}

void MeshView::notify_confirmation() {
    if (impl_->confirmed_candidate && impl_->confirmation_callback) {
        // Confirmation handlers may immediately change the selection contract
        // or rebuild the scene. Both operations clear confirmed_candidate, so
        // never expose a reference into that optional to the callback.
        const ViewerCandidate candidate = *impl_->confirmed_candidate;
        impl_->confirmation_callback(candidate);
    }
}

void MeshView::fit_points(const std::vector<zima::kernel::Vec3>& points) {
    if(points.empty())return;
    auto minimum=points.front(),maximum=minimum;
    for(const auto& p:points) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
    }
    impl_->resize_shift_pixels={};
    impl_->center=QVector3D((minimum.x+maximum.x)/2,(minimum.y+maximum.y)/2,(minimum.z+maximum.z)/2);
    const double radius=std::hypot(maximum.x-minimum.x,maximum.y-minimum.y,maximum.z-minimum.z)/2;
    const double dpi=physicalDpiY()>=50&&physicalDpiY()<=400?physicalDpiY():96.;
    const double aspect=static_cast<double>(std::max(width(),1))/std::max(height(),1);
    impl_->radius=static_cast<float>(radius>1e-9?radius:std::max(height(),1)*25.4/(2*dpi));
    impl_->view_scale=radius>1e-9?static_cast<float>(radius*1.2/std::min(aspect,1.)):impl_->radius;
    impl_->pan_pixels={};
    if(impl_->fly_navigation_enabled)impl_->place_fly_camera_at_orbit_position();
    impl_->candidates.clear();
    impl_->rebuild_persisted_reference_mesh();
    update();
}

void MeshView::fit_all() {
    impl_->resize_shift_pixels={};
    // World coordinates are millimetres. Qt reports widget DPI in the same
    // logical-pixel units as height(), so startup is approximately 1:1 on
    // this monitor, independently of window size and desktop scaling.
    const double dpi = physicalDpiY() >= 50 && physicalDpiY() <= 400
        ? physicalDpiY() : 96.0;
    const float empty_half_extent = static_cast<float>(
        std::max(height(), 1) * 25.4 / (2.0 * dpi));
    std::vector<zima::kernel::Vec3> bounds = impl_->mesh.vertices;
    std::vector<zima::kernel::Vec3> reference_centers;
    double reference_extent = 0.5;
    bool has_reference_extent = false;
    for (const auto& edge : impl_->mesh.edges) {
        if (is_screen_constant_plane(edge.reference.semantic_key)) {
            const std::size_t count = edge.points.size() > 1
                ? edge.points.size() - 1 : edge.points.size();
            if (count == 0) continue;
            has_reference_extent = true;
            zima::kernel::Vec3 center;
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            reference_centers.push_back(center);
            for (std::size_t index = 0; index < count; ++index) {
                reference_extent = std::max(reference_extent,
                    std::hypot(std::hypot(edge.points[index].x - center.x,
                                         edge.points[index].y - center.y),
                               edge.points[index].z - center.z));
            }
            continue;
        }
        bounds.insert(bounds.end(), edge.points.begin(), edge.points.end());
    }
    for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
        const bool preview_helper =
            point.reference.semantic_key == "preview:plane-offset-point" ||
            impl_->feature_preview_owner_ids.contains(point.reference.owner_id);
        if (point.reference.semantic_key == "origin:point" ||
            point.reference.semantic_key == "external_point:sketch_origin" || preview_helper) {
            reference_centers.push_back(point.position);
        } else {
            bounds.push_back(point.position);
        }
    }
    for (const auto& axis : impl_->mesh.axes) {
                if (!impl_->origin_visible({axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path})) continue;
        const bool origin = axis.reference.semantic_key.starts_with("origin:axis:");
        if (axis.reference.semantic_key.starts_with("sketch_axis:")) {
            reference_centers.push_back(axis.point);
            continue;
        }
        if (origin) {
            has_reference_extent = true;
            reference_centers.push_back(axis.point);
            reference_extent = std::max(reference_extent, axis.display_length);
            continue;
        }
        const double first = origin ? 0.0 : -axis.display_length * 0.5;
        const double second = origin ? axis.display_length : axis.display_length * 0.5;
        bounds.push_back({axis.point.x + axis.direction.x * first,
                          axis.point.y + axis.direction.y * first,
                          axis.point.z + axis.direction.z * first});
        bounds.push_back({axis.point.x + axis.direction.x * second,
                          axis.point.y + axis.direction.y * second,
                          axis.point.z + axis.direction.z * second});
    }
    for (const auto& dimension : impl_->mesh.dimensions) {
        bounds.push_back(dimension.witness_first);
        bounds.push_back(dimension.witness_second);
        bounds.push_back(dimension.line_first);
        bounds.push_back(dimension.line_second);
    }
    // Pending primitive bodies are analytical transient wires until OK, so
    // they are intentionally absent from mesh.vertices/mesh.edges. They are
    // nevertheless the primary object the user needs framed while entering
    // Box/Cylinder/etc. parameters.
    for (const auto& edge : impl_->transient_edges) {
        bounds.insert(bounds.end(), edge.points.begin(), edge.points.end());
    }
    if (bounds.empty() && reference_centers.empty()) {
        impl_->center = {};
        impl_->radius = empty_half_extent;
        impl_->view_scale = empty_half_extent;
        // This is the degenerate "nothing to frame yet" case (no origin
        // geometry has even been generated into the mesh yet) -- it must
        // NOT lock in reference_view_scale_initialized, otherwise a
        // premature call here (before the real origin-only fit below ever
        // runs) would permanently freeze the screen-constant baseline at an
        // arbitrary 1.4F, unrelated to the origin's actual rendered extent.
        if (!impl_->reference_view_scale_initialized) {
            impl_->reference_view_scale = 1.4F;
        }
        impl_->pan_pixels = {};
        if (impl_->fly_navigation_enabled)
            impl_->place_fly_camera_at_orbit_position();
        return;
    }
    if (bounds.empty()) {
        // No real body/user geometry yet (e.g. a brand-new empty Part): the
        // scene contains only origin axes/planes/points. Frame the view
        // around their actual rendered extent (reference_extent, tracking
        // axis display_length / plane half-diagonal) rather than around the
        // tiny cluster of near-zero center points in reference_centers,
        // which would otherwise make the fixed-length origin axes appear
        // enormous relative to an overly tight fit radius.
        zima::kernel::Vec3 center{};
        if (!reference_centers.empty()) {
            for (const auto& point : reference_centers) {
                center.x += point.x;
                center.y += point.y;
                center.z += point.z;
            }
            const double count = static_cast<double>(reference_centers.size());
            center = {center.x / count, center.y / count, center.z / count};
        }
        impl_->center = QVector3D(static_cast<float>(center.x),
                                   static_cast<float>(center.y),
                                   static_cast<float>(center.z));
        // Add headroom around the origin's own extent so the axes/planes
        // do not fill the entire viewport edge-to-edge (matches the visual
        // margin a body with real bounds gets from its own surrounding
        // whitespace once panned/zoomed by the user).
        const float reference_frame_scale = static_cast<float>(
            std::max(reference_extent, 0.5) * 2.0);
        impl_->radius = std::max(
            reference_frame_scale, empty_half_extent);
        impl_->view_scale = impl_->radius;
        // Only establish the screen-constant reference baseline the FIRST
        // time it is ever needed. Once set, it must survive every later
        // fit_all() -- including calls made after real geometry (an Axis,
        // Plane, body...) has been added -- so the Origin (and every
        // container's own origin) keeps an identical apparent size in the
        // View no matter how far the camera has to zoom out to frame newly
        // added, more distant geometry. Resetting it on every fit_all() call
        // (the previous behaviour) made the Origin visibly shrink each time
        // "Zobrazit vše" had to zoom out further than before.
        // A point alone does not define datum display size. Keep initialization
        // pending until real plane/axis display geometry reaches the viewer.
        if (!impl_->reference_view_scale_initialized && has_reference_extent) {
            // Decouple real-world startup scale from screen-constant datum
            // size: use the monitor metric scale for the world, but keep document
            // and container Origins at their established LCD size.
            impl_->reference_view_scale = reference_frame_scale;
            impl_->reference_view_scale_initialized = true;
        }
        impl_->pan_pixels = {};
        if (impl_->fly_navigation_enabled)
            impl_->place_fly_camera_at_orbit_position();
        return;
    }
    zima::kernel::Vec3 minimum{
        std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    zima::kernel::Vec3 maximum{
        std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
    for (const auto& point : bounds) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    impl_->center = QVector3D(
        static_cast<float>((minimum.x + maximum.x) / 2.0),
        static_cast<float>((minimum.y + maximum.y) / 2.0),
        static_cast<float>((minimum.z + maximum.z) / 2.0));
    const QVector3D diagonal(
        static_cast<float>(maximum.x - minimum.x),
        static_cast<float>(maximum.y - minimum.y),
        static_cast<float>(maximum.z - minimum.z));
    impl_->radius = std::max(diagonal.length() / 2.0F, 0.5F);
    // A bounding sphere guarantees that every corner fits the orthographic
    // projection, but using its radius verbatim leaves the outermost corner
    // directly on the viewport edge.  Keep a modest frame around newly
    // created geometry so the complete primitive is visible immediately.
    // In a portrait/narrow viewport the horizontal half-extent is
    // view_scale * aspect, therefore compensate for aspect ratios below 1.
    constexpr float fit_margin = 1.20F;
    const float aspect = height() > 0
        ? static_cast<float>(std::max(width(), 1)) /
            static_cast<float>(height())
        : 1.0F;
    const float narrow_view_compensation = 1.0F / std::min(aspect, 1.0F);
    impl_->view_scale = bounds.size() == 1
        ? std::max(impl_->reference_view_scale,
              empty_half_extent)
        : impl_->radius * fit_margin * narrow_view_compensation;
    // Only establish the screen-constant reference baseline once (see the
    // identical guard/comment in the origin-only branch above). A document
    // that already has an established reference_view_scale (from an earlier
    // fit, or restored from a saved camera state) must keep it unchanged
    // here, otherwise every "Zobrazit vše" after adding new, farther-away
    // geometry would re-baseline the ratio to 1.0 and make the Origin (and
    // every container's own origin) visibly shrink.
    if (!impl_->reference_view_scale_initialized && has_reference_extent) {
        // A document may be opened for the first time with an already large
        // persisted body. The body controls only the camera fit; it must not
        // establish the LCD size of datum axes/planes. Use the datum's own
        // model extent exactly like the origin-only branch above, so opening
        // a 10 mm and a 10 m Part presents an identical Origin on screen.
        impl_->reference_view_scale = static_cast<float>(
            std::max(reference_extent, 0.5) * 2.0);
        impl_->reference_view_scale_initialized = true;
    }
    impl_->pan_pixels = {};
    if (impl_->fly_navigation_enabled)
        impl_->place_fly_camera_at_orbit_position();
    update();
}

void MeshView::set_display_mode(DisplayMode mode) {
    impl_->display_mode = mode;
    update();
}

DisplayMode MeshView::display_mode() const { return impl_->display_mode; }

void MeshView::set_projection_mode(ProjectionMode mode) {
    if (impl_->projection_mode == mode) return;
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    impl_->projection_mode = mode;
    impl_->candidates.clear();
    update();
}

ProjectionMode MeshView::projection_mode() const {
    return impl_->projection_mode;
}

void MeshView::set_fly_navigation_enabled(bool enabled) {
    if (impl_->fly_navigation_enabled == enabled) return;
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    if (enabled) {
        impl_->place_fly_camera_at_orbit_position();
        impl_->fly_navigation_enabled = true;
        setFocus(Qt::OtherFocusReason);
        impl_->candidates.clear();
        update();
        return;
    }
    // Fly navigation is independent of projection. Leaving it restores the
    // ordinary isometric CAD camera but keeps the selected Ortho/Perspective
    // projection unchanged.
    impl_->fly_navigation_enabled = false;
    impl_->orientation = default_camera_orientation();
    impl_->pan_pixels = {};
    fit_all();
}

bool MeshView::fly_navigation_enabled() const {
    return impl_->fly_navigation_enabled;
}

namespace {
// Shared camera transition helper for set_standard_view/set_view_direction,
// matching Python's animate_standard_view/animate_view_normal: slerp the
// orientation and ease pan back to zero over ANIMATION_DURATION_MS with an
// InOutCubic curve instead of a hard jump cut.
constexpr int kAnimationDurationMs = 850;
}  // namespace

void MeshView::animate_orientation_to(const QQuaternion& target, bool preserve_pan) {
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    const QQuaternion start_orientation = impl_->orientation;
    const QPointF start_pan = impl_->pan_pixels;
    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(kAnimationDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, start_orientation, target, start_pan, preserve_pan](const QVariant& raw) {
            const float progress = static_cast<float>(raw.toDouble());
            impl_->orientation =
                QQuaternion::slerp(start_orientation, target, progress);
            impl_->pan_pixels = preserve_pan ? start_pan : start_pan * (1.0 - progress);
            update();
        });
    connect(animation, &QVariantAnimation::finished, this, [this, animation] {
        if (impl_->camera_animation == animation) impl_->camera_animation = nullptr;
    });
    impl_->camera_animation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MeshView::animate_camera_state(const std::array<float, 8>& state) {
    if (!std::all_of(state.begin(), state.end(), [](float value) {
            return std::isfinite(value);
        }) || state[4] <= 0.0F) return;
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    const QQuaternion start_orientation = impl_->orientation;
    const QPointF start_pan = impl_->pan_pixels;
    const float start_scale = impl_->view_scale;
    const float start_reference_scale = impl_->reference_view_scale;
    const QQuaternion target_orientation =
        QQuaternion(state[0], state[1], state[2], state[3]).normalized();
    const QPointF target_pan(state[5], state[6]);
    const float target_scale = state[4];
    // reference_view_scale must be restored alongside view_scale, otherwise
    // the screen-constant origin axis/plane ratio drifts to a stale value
    // after restoring a saved "Pohledy" view.
    const float target_reference_scale = state[7] > 0.0F ? state[7] : target_scale;
    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(kAnimationDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, start_orientation, target_orientation, start_pan, target_pan,
            start_scale, target_scale, start_reference_scale,
            target_reference_scale](const QVariant& raw) {
            const float progress = static_cast<float>(raw.toDouble());
            impl_->orientation = QQuaternion::slerp(
                start_orientation, target_orientation, progress);
            impl_->pan_pixels = start_pan +
                (target_pan - start_pan) * static_cast<double>(progress);
            impl_->view_scale = start_scale +
                (target_scale - start_scale) * progress;
            impl_->reference_view_scale = start_reference_scale +
                (target_reference_scale - start_reference_scale) * progress;
            impl_->candidates.clear();
            update();
        });
    connect(animation, &QVariantAnimation::finished, this, [this, animation] {
        if (impl_->camera_animation == animation) impl_->camera_animation = nullptr;
        impl_->rebuild_persisted_reference_mesh();
    });
    impl_->camera_animation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}


void MeshView::set_standard_view(StandardView view) {
    float yaw{};
    float pitch{};
    switch (view) {
        case StandardView::Isometric:
            yaw = 215.264F; pitch = -45.0F;
            break;
        case StandardView::Front:
            yaw = 180.0F; pitch = -90.0F;
            break;
        case StandardView::Back:
            yaw = 0.0F; pitch = -90.0F;
            break;
        case StandardView::Left:
            yaw = -90.0F; pitch = -90.0F;
            break;
        case StandardView::Right:
            yaw = 90.0F; pitch = -90.0F;
            break;
        case StandardView::Top:
            yaw = 0.0F; pitch = 0.0F;
            break;
        case StandardView::Bottom:
            yaw = 180.0F; pitch = 180.0F;
            break;
    }
    const QQuaternion target =
        QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, pitch) *
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, yaw);
    impl_->candidates.clear();
    animate_orientation_to(target);
}

void MeshView::set_view_direction(const zima::kernel::Vec3& direction) {
    set_view_direction(direction, 0.0F);
}

void MeshView::set_view_direction(
    const zima::kernel::Vec3& direction, float roll_degrees, bool preserve_pan) {
    const double length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("View direction must be finite and non-zero");
    }
    const double x = direction.x / length;
    const double y = direction.y / length;
    const double z = direction.z / length;
    const double horizontal = std::hypot(x, y);
    const float yaw = horizontal > 1.0e-12
        ? static_cast<float>(std::atan2(x, y) * 180.0 / std::numbers::pi) : 0.0F;
    const float pitch = static_cast<float>(
        std::atan2(-horizontal, -z) * 180.0 / std::numbers::pi);
    const QQuaternion target =
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, roll_degrees) *
        QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, pitch) *
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, yaw);
    impl_->candidates.clear();
    animate_orientation_to(target,preserve_pan);
}

void MeshView::set_dimension_visibility_filter(std::function<bool(const zima::kernel::ViewerDimension&)> filter) {
    impl_->dimension_visibility_filter=std::move(filter);impl_->candidates.clear();update();
}
void MeshView::set_dimension_relations(std::map<std::pair<std::string,std::string>,std::string> values) {
    if(impl_->dimension_relations==values)return;
    impl_->dimension_relations=std::move(values);impl_->relation_marks->labels.clear();impl_->relation_marks->update();update();
}
bool MeshView::event(QEvent* event) {
    if(event->type()==QEvent::ToolTip) {
        const auto candidate=hovered_candidate();
        if(candidate&&candidate->kind==CandidateKind::Dimension) {
            const auto relation=impl_->dimension_relations.find({candidate->owner_id,candidate->semantic_key});
            if(relation!=impl_->dimension_relations.end()) {
                const auto* help=static_cast<QHelpEvent*>(event);
                QToolTip::showText(help->globalPos(),"<pre>"+QString::fromStdString(relation->second).toHtmlEscaped()+"</pre>",this);
                return true;
            }
        }
        QToolTip::hideText();
    }
    return QOpenGLWidget::event(event);
}
void MeshView::set_reference_visibility(
    ReferenceVisibility reference, bool visible) {
    switch (reference) {
        case ReferenceVisibility::Surfaces:
            if(impl_->show_surfaces!=visible){impl_->show_surfaces=visible;if(impl_->surface_source_mesh)set_mesh(*impl_->surface_source_mesh,false);}
            break;
        case ReferenceVisibility::Origins:
            impl_->show_origins = visible;
            impl_->candidates.clear();
            if (!visible && impl_->component_origin_handle && impl_->confirmed_candidate &&
                !impl_->origin_visible({impl_->confirmed_candidate->owner_id,
                    impl_->confirmed_candidate->semantic_key,impl_->confirmed_candidate->instance_path})) clear_selection();
            break;
        case ReferenceVisibility::Points: impl_->show_points = visible; break;
        case ReferenceVisibility::AxisPoints: impl_->show_axis_points = visible; impl_->candidates.clear(); break;
        case ReferenceVisibility::Axes: impl_->show_axes = visible; impl_->candidates.clear(); break;
        case ReferenceVisibility::Planes: impl_->show_planes = visible; impl_->candidates.clear(); break;
        case ReferenceVisibility::Symbols:
            impl_->show_symbols=visible;impl_->candidates.clear();
            if(!visible&&impl_->confirmed_candidate&&impl_->confirmed_candidate->kind==CandidateKind::Symbol)clear_selection();
            break;
        case ReferenceVisibility::Sketches: impl_->show_sketches = visible; break;
        case ReferenceVisibility::Dimensions:
            impl_->show_dimensions = visible;
            impl_->candidates.clear();
            if (!visible && impl_->confirmed_candidate && impl_->confirmed_candidate->kind == CandidateKind::Dimension) clear_selection();
            break;
    }
    update();
}

bool MeshView::reference_visible(ReferenceVisibility reference) const {
    switch (reference) {
        case ReferenceVisibility::Surfaces: return impl_->show_surfaces;
        case ReferenceVisibility::Origins: return impl_->show_origins;
        case ReferenceVisibility::Points: return impl_->show_points;
        case ReferenceVisibility::AxisPoints: return impl_->show_axis_points;
        case ReferenceVisibility::Axes: return impl_->show_axes;
        case ReferenceVisibility::Planes: return impl_->show_planes;
        case ReferenceVisibility::Symbols: return impl_->show_symbols;
        case ReferenceVisibility::Sketches: return impl_->show_sketches;
        case ReferenceVisibility::Dimensions: return impl_->show_dimensions;
    }
    return false;
}

void MeshView::set_component_origin_handle(std::optional<EdgeKey> reference) {
    impl_->component_origin_handle = std::move(reference);
    update();
}

void MeshView::set_origin_visibility_filter(std::function<bool(const EdgeKey&)> filter) {
    impl_->origin_visibility_filter = std::move(filter);
    impl_->candidates.clear();
    update();
}

void MeshView::set_document_origin(const std::string& owner_id) {
    if(impl_->document_origin_id==owner_id)return;
    impl_->document_origin_id=owner_id;update();
}

void MeshView::set_editing_origin_visible(bool visible) {
    if (impl_->editing_origin_visible == visible) return;
    impl_->editing_origin_visible = visible;
    update();
}

void MeshView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    // Viewer meshes are persisted/reference geometry, not an OCCT
    // presentation.  Their winding is not a visibility contract and can be
    // reversed by mirrors or occurrence transforms.  Python consequently
    // renders both sides as well.  Back-face culling here made whole faces
    // disappear from the depth pass, which in turn broke both hidden-line
    // modes and made the visible body disagree with the two-sided picker.
    glDisable(GL_CULL_FACE);
    glClearColor(0.14F, 0.16F, 0.18F, 1.0F);
    impl_->program.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 330 core\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=1) in vec3 normal;\n"
        "uniform mat4 mvp;\n"
        "uniform mat3 normalMatrix;\n"
        "out vec3 viewNormal;\n"
        "void main(){ viewNormal = normalize(normalMatrix * normal);"
        " gl_Position = mvp * vec4(position, 1.0); }\n");
    impl_->program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"GLSL(
#version 330 core
uniform vec4 color;
uniform int unlit;
uniform float roughness;
uniform float metallic;
in vec3 viewNormal;
out vec4 fragmentColor;
// Procedural studio radiance: broad fill plus two softbox reflections.
// This is view shading only; no OCCT or scene-geometry reconstruction.
vec3 studio(vec3 r,float rough){
    float width=mix(150.0,3.0,rough*rough);
    float a=pow(max(dot(r,normalize(vec3(-.65,.6,.5))),0.0),width);
    float b=pow(max(dot(r,normalize(vec3(.8,.25,.4))),0.0),width*.55);
    float horizon=smoothstep(-.6,.8,r.y);
    return mix(vec3(.055,.065,.085),vec3(.4,.44,.5),horizon)+vec3(1.8)*a+vec3(.8,.9,1.1)*b;
}
void main(){
    if(unlit!=0){fragmentColor=color;return;}
    vec3 n=normalize(viewNormal);if(n.z<0.0)n=-n;
    vec3 v=vec3(0,0,1);vec3 l=normalize(vec3(.25,.35,.902));
    vec3 h=normalize(l+v);float nv=max(dot(n,v),.001),nl=max(dot(n,l),0.0);
    float rough=clamp(roughness,.04,1.0);float metal=clamp(metallic,0.0,1.0);
    vec3 base=pow(color.rgb,vec3(2.2));vec3 f0=mix(vec3(.04),base,metal);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-nv,5.0);
    float a=rough*rough,a2=a*a;float nh=max(dot(n,h),0.0);
    float d=a2/(3.14159*pow(nh*nh*(a2-1.0)+1.0,2.0));
    float k=(rough+1.0)*(rough+1.0)/8.0;
    float visibility=nv/(nv*(1.0-k)+k)*nl/(nl*(1.0-k)+k);
    vec3 reflected=studio(reflect(-v,n),rough)*fresnel;
    vec3 direct=d*visibility*fresnel/(4.0*nv*max(nl,.001))*nl*.65;
    vec3 diffuse=base*(1.0-metal)*(.32+.68*nl);
    vec3 linear=diffuse+reflected+direct;
    fragmentColor=vec4(pow(clamp(linear,0.0,1.0),vec3(1.0/2.2)),color.a);
}
)GLSL");
    impl_->program.link();
    impl_->vertex_array.create();
    impl_->vertex_array.bind();
    impl_->vertices.create();
    impl_->transparent_triangles.create();
    impl_->lines.create();
    impl_->selected_source_lines.create();
    impl_->selected_source_key.reset();impl_->selected_source_data.clear();
    impl_->silhouette.create();
    impl_->silhouette_view_direction.reset();
    impl_->silhouette_vertex_count = 0;
    upload_mesh();
    impl_->vertex_array.release();
}

void MeshView::set_preserve_screen_position_on_resize(bool enabled){
    impl_->preserve_screen_position_on_resize=enabled;
    if(!enabled)impl_->resize_shift_pixels={};
}

void MeshView::resizeGL(int, int) {
    const int current_width=width(),current_height=height();
    if(impl_->preserve_screen_position_on_resize&&impl_->viewport_width>0&&current_height==impl_->viewport_height){
        impl_->resize_shift_pixels.rx()+=(impl_->viewport_width-current_width)*.5;
    }
    impl_->viewport_width=current_width;
    impl_->viewport_height = std::max(1, current_height);
}

void MeshView::upload_mesh() {
    if (!isValid() || !impl_->gpu_dirty) return;
    QElapsedTimer upload_timer;upload_timer.start();
    const auto upload_phase=[&](const char* name) {
        if(qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Viewer upload %s: %.3f ms\n",name,upload_timer.nsecsElapsed()/1e6);
        upload_timer.restart();
    };
    setProperty("gpuMeshUploadCount",property("gpuMeshUploadCount").toULongLong()+1);
    auto previous_smooth=std::move(impl_->smooth_silhouette_triangles);
    auto previous_candidates=std::move(impl_->silhouette_candidates);
    const auto previous_direction=impl_->silhouette_view_direction;
    const auto previous_vertex_count=impl_->silhouette_vertex_count;
    const auto vertex_data = shaded_triangle_vertices(impl_->mesh);
    impl_->smooth_silhouette_triangles=zima::viewer::smooth_silhouette_triangles(impl_->mesh,vertex_data);
    impl_->silhouette_color_key.reset();impl_->silhouette_highlight_ranges.clear();
    impl_->vertices.bind();
    impl_->vertices.allocate(vertex_data.data(),
        static_cast<int>(vertex_data.size() * sizeof(float)));

    upload_phase("triangles");
    std::vector<float> line_data;
    impl_->line_ranges.clear();
    impl_->container_color_key.reset();impl_->wire_has_individual_colors=false;
    impl_->line_edges.clear();
    impl_->line_mesh_edge_indices.clear();
    for (std::size_t mesh_edge_index = 0;
         mesh_edge_index < impl_->mesh.edges.size(); ++mesh_edge_index) {
        const auto& edge = impl_->mesh.edges[mesh_edge_index];
        // Datum/work-plane rectangles are rendered later by QPainter after
        // applying the screen-constant reference scale. Uploading their raw
        // model-space border to the GL line pass as well draws a second,
        // differently-sized rectangle that separates from the real one
        // during zoom (the small flickering plane visible in 01.png).
        if (edge.overlay || is_screen_constant_plane(edge.reference.semantic_key) ||
            edge.points.size() < 2) continue;
        const auto first = static_cast<GLint>(line_data.size() / 6);
        const auto count = static_cast<GLsizei>(
            (edge.points.size() - 1) * 2);
        impl_->line_ranges.emplace_back(first, count);
        impl_->line_edges.push_back(edge);
        const auto& key=edge.reference.semantic_key;
        impl_->wire_has_individual_colors|=!edge.color.empty()||edge.construction||edge.dash_dot||edge.annotation.has_value()||key.starts_with("hole:cosmetic-thread:")||
            key.starts_with("thread:wire:")||key.starts_with("thread:boundary:");
        impl_->line_mesh_edge_indices.push_back(mesh_edge_index);
        // GL_LINES permits the complete ordinary wire to be rendered in one
        // draw call.  GL_LINE_STRIP required one call per persisted edge,
        // which meant about 29,000 driver calls per frame for the large STEP
        // reference model.
        for (std::size_t index = 1; index < edge.points.size(); ++index) {
            for (const auto& point :
                    {edge.points[index - 1], edge.points[index]}) {
                line_data.insert(line_data.end(), {
                    static_cast<float>(point.x), static_cast<float>(point.y),
                    static_cast<float>(point.z), 0.0F, 0.0F, 1.0F});
            }
        }
    }
    impl_->line_vertex_count = static_cast<GLsizei>(line_data.size() / 6);
    impl_->lines.bind();
    impl_->lines.allocate(line_data.data(),
        static_cast<int>(line_data.size() * sizeof(float)));

    upload_phase("edges");
    // Candidate silhouette edges: internal triangulation edges shared by
    // exactly two triangles of the same owning face (owner_id + semantic_key)
    // that are not already a persisted topological edge. Exact connectivity
    // may be retained across uploads; positions and normals always come from
    // the actual mesh. The visible subset is re-selected every frame from
    // the current camera direction in paintGL.
    {
        using RoundedPoint=std::array<double,3>;
        struct PointHash {
            std::size_t operator()(const RoundedPoint& point) const {
                std::size_t hash=0;
                for(const auto value:point)
                    hash^=std::hash<double>{}(value)+0x9e3779b9+(hash<<6)+(hash>>2);
                return hash;
            }
        };
        using SharedKey=std::pair<std::string_view,std::pair<std::size_t,std::size_t>>;
        struct SharedHash {
            std::size_t operator()(const SharedKey& key) const {
                auto hash=std::hash<std::string_view>{}(key.first);
                for(const auto value:{key.second.first,key.second.second})
                    hash^=std::hash<std::size_t>{}(value)+0x9e3779b9+(hash<<6)+(hash>>2);
                return hash;
            }
        };
        std::unordered_set<SharedKey,SharedHash> topology_segments;
        std::size_t segment_count=0;
        for(const auto& edge:impl_->mesh.edges)if(!edge.overlay&&edge.points.size()>1)
            segment_count+=edge.points.size()-1;
        topology_segments.reserve(segment_count);
        const auto rounded = [](const zima::kernel::Vec3& point) {
            constexpr double scale = 1.0e7;
            return std::array<double, 3>{
                std::round(point.x * scale) / scale,
                std::round(point.y * scale) / scale,
                std::round(point.z * scale) / scale};
        };
        // Identical rounded coordinates get one operation-local address.
        // Retain the exact established rounding and coordinate ordering; the
        // addresses only avoid copying/hashing six doubles per incident edge.
        std::vector<RoundedPoint> rounded_points;
        std::unordered_map<RoundedPoint,std::size_t,PointHash> point_addresses;
        rounded_points.reserve(impl_->mesh.vertices.size());
        point_addresses.reserve(impl_->mesh.vertices.size());
        const auto point_address=[&](const zima::kernel::Vec3& point) {
            const auto coordinates=rounded(point);
            const auto [found,inserted]=point_addresses.emplace(coordinates,rounded_points.size());
            if(inserted)rounded_points.push_back(coordinates);
            return found->second;
        };
        std::vector<std::size_t> vertex_addresses;
        vertex_addresses.reserve(impl_->mesh.vertices.size());
        for(const auto& vertex:impl_->mesh.vertices)vertex_addresses.push_back(point_address(vertex));
        const auto ordered_segment=[&](std::size_t first,std::size_t second) {
            if(rounded_points[second]<rounded_points[first])std::swap(first,second);
            return std::pair{first,second};
        };
        for (const auto& edge : impl_->mesh.edges) {
            if (edge.overlay) continue;
            for (std::size_t index = 0; index + 1 < edge.points.size(); ++index) {
                topology_segments.insert(
                    {edge.reference.owner_id, ordered_segment(point_address(edge.points[index]),
                        point_address(edge.points[index+1]))});
            }
        }
        // Verify the actual combinatorial inputs before reusing adjacency.
        // Rounded coordinate classes are recomputed for every upload: a real
        // class merge/split, topology edit, triangle edit or face-key change
        // takes the full path. Coordinate values themselves are never reused.
        decltype(Impl::SilhouetteConnectivity::topology) topology_signature;
        for(const auto& entry:topology_segments) {
            auto addresses=entry.second;
            if(addresses.second<addresses.first)std::swap(addresses.first,addresses.second);
            auto owner=topology_signature.find(entry.first);
            if(owner==topology_signature.end())
                owner=topology_signature.emplace(std::string(entry.first),std::vector<std::pair<std::size_t,std::size_t>>{}).first;
            owner->second.push_back(addresses);
        }
        for(auto& [owner,segments]:topology_signature)std::ranges::sort(segments);
        const auto triangle_count=impl_->mesh.triangles.size()/3;
        std::vector<std::pair<std::size_t,std::string>> face_runs;
        std::string_view run_owner,run_semantic;
        bool have_run=false;
        for(std::size_t triangle=0;triangle<std::min(triangle_count,impl_->mesh.triangle_references.size());++triangle) {
            const auto& ref=impl_->mesh.triangle_references[triangle];
            const std::string_view owner=ref.valid()?std::string_view(ref.owner_id):std::string_view{};
            if(!have_run||run_owner!=owner||run_semantic!=ref.semantic_key) {
                face_runs.emplace_back(1,std::string(owner)+"|"+ref.semantic_key);
                run_owner=owner;run_semantic=ref.semantic_key;have_run=true;
            } else ++face_runs.back().first;
        }
        const bool reuse_connectivity=impl_->silhouette_connectivity && !property("forceAdjacencyBuild").toBool() &&
            impl_->silhouette_connectivity->vertex_addresses==vertex_addresses &&
            impl_->silhouette_connectivity->triangles==impl_->mesh.triangles &&
            impl_->silhouette_connectivity->face_runs==face_runs &&
            impl_->silhouette_connectivity->topology==topology_signature;
        if(qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_COMMAND")&&impl_->silhouette_connectivity) {
            const auto& previous=*impl_->silhouette_connectivity;
            std::fprintf(stderr,"Viewer adjacency reuse=%d forced=%d vertices=%d triangles=%d faces=%d topology=%d\n",
                int(reuse_connectivity),int(property("forceAdjacencyBuild").toBool()),
                int(previous.vertex_addresses==vertex_addresses),int(previous.triangles==impl_->mesh.triangles),
                int(previous.face_runs==face_runs),int(previous.topology==topology_signature));
        }
        impl_->silhouette_candidates.clear();
        impl_->silhouette_view_direction.reset();
        impl_->silhouette_vertex_count=0;
        if(reuse_connectivity) {
            const auto& connectivity=*impl_->silhouette_connectivity;
            std::vector<const Impl::SilhouetteSource*> ordered;
            ordered.reserve(connectivity.sources.size());
            for(const auto& source:connectivity.sources)ordered.push_back(&source);
            const auto less=[&](const auto* first,const auto* second) {
                const auto a=ordered_segment(first->first_address,first->second_address);
                const auto b=ordered_segment(second->first_address,second->second_address);
                return std::tie(connectivity.face_keys[first->face_key],rounded_points[a.first],rounded_points[a.second])<
                    std::tie(connectivity.face_keys[second->face_key],rounded_points[b.first],rounded_points[b.second]);
            };
            if(!std::ranges::is_sorted(ordered,less))std::ranges::sort(ordered,less);
            const auto triangle_points=[&](std::size_t triangle) {
                std::array<QVector3D,3> points;
                for(std::size_t corner=0;corner<3;++corner) {
                    const auto& point=impl_->mesh.vertices[impl_->mesh.triangles[triangle*3+corner]];
                    points[corner]=QVector3D(static_cast<float>(point.x),static_cast<float>(point.y),static_cast<float>(point.z));
                }
                return points;
            };
            const auto triangle_normal=[](const auto& points) {
                auto normal=QVector3D::crossProduct(points[1]-points[0],points[2]-points[0]);
                if(normal.lengthSquared()>1.0e-12F)normal.normalize();
                return normal;
            };
            for(const auto* source:ordered) {
                const auto first=triangle_points(source->first_triangle);
                const auto second=triangle_points(source->second_triangle);
                impl_->silhouette_candidates.push_back({first[source->first_side],first[(source->first_side+1)%3],
                    triangle_normal(first),triangle_normal(second),
                    impl_->mesh.triangle_references[source->first_triangle].valid(),source->first_triangle});
            }
            setProperty("gpuAdjacencyReuseCount",property("gpuAdjacencyReuseCount").toULongLong()+1);
        } else {
        struct SharedRecord { QVector3D first, second, normal; };
        struct SharedRecords {
            SharedRecord first;
            QVector3D second_normal;
            unsigned count{};
            std::size_t first_triangle{},second_triangle{},first_side{};
        };
        // Intern each exact face key once for this upload. Set nodes keep
        // their strings stable until the adjacency map has been destroyed.
        std::set<std::string> face_keys;
        std::unordered_map<SharedKey,SharedRecords,SharedHash> shared;
        shared.reserve(triangle_count);
        std::string_view previous_owner,previous_semantic,face_key;
        bool have_previous_face=false;
        for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
            if (triangle >= impl_->mesh.triangle_references.size()) continue;
            const auto& face_reference = impl_->mesh.triangle_references[triangle];
            const std::array<std::uint32_t, 3> indices{
                impl_->mesh.triangles[triangle * 3],
                impl_->mesh.triangles[triangle * 3 + 1],
                impl_->mesh.triangles[triangle * 3 + 2]};
            if (indices[0] >= impl_->mesh.vertices.size() ||
                indices[1] >= impl_->mesh.vertices.size() ||
                indices[2] >= impl_->mesh.vertices.size()) continue;
            const std::array<zima::kernel::Vec3, 3> points{
                impl_->mesh.vertices[indices[0]],
                impl_->mesh.vertices[indices[1]],
                impl_->mesh.vertices[indices[2]]};
            const QVector3D pa(static_cast<float>(points[0].x),
                static_cast<float>(points[0].y), static_cast<float>(points[0].z));
            const QVector3D pb(static_cast<float>(points[1].x),
                static_cast<float>(points[1].y), static_cast<float>(points[1].z));
            const QVector3D pc(static_cast<float>(points[2].x),
                static_cast<float>(points[2].y), static_cast<float>(points[2].z));
            QVector3D normal = QVector3D::crossProduct(pb - pa, pc - pa);
            if (normal.lengthSquared() > 1.0e-12F) normal.normalize();
            const std::pair<QVector3D, QVector3D> triangle_edges[3]{
                {pa, pb}, {pb, pc}, {pc, pa}};
            const std::string_view owner=face_reference.valid()
                ? std::string_view(face_reference.owner_id) : std::string_view{};
            if(!have_previous_face||previous_owner!=owner||previous_semantic!=face_reference.semantic_key) {
                face_key=*face_keys.insert(std::string(owner)+"|"+face_reference.semantic_key).first;
                previous_owner=owner;previous_semantic=face_reference.semantic_key;have_previous_face=true;
            }
            for (std::size_t side = 0; side < 3; ++side) {
                const auto segment=ordered_segment(vertex_addresses[indices[side]],
                    vertex_addresses[indices[(side+1)%3]]);
                auto& records = shared[{face_key, segment}];
                // Only an exact pair can become a silhouette candidate.
                // Retain its original order; three means permanently ineligible.
                if (records.count == 0) {
                    records.first = {triangle_edges[side].first,
                        triangle_edges[side].second, normal};
                    records.first_triangle=triangle;records.first_side=side;
                } else if (records.count == 1) {
                    records.second_normal = normal;records.second_triangle=triangle;
                }
                if (records.count < 3) ++records.count;
            }
        }
        // Preserve the original ordered-map publication order exactly. Hash
        // lookup avoids a tree search and allocation per incident triangle;
        // sorting is needed only once for the eligible candidate records.
        std::vector<const decltype(shared)::value_type*> ordered;
        ordered.reserve(shared.size());
        for(const auto& entry:shared) {
            const auto& [key,records]=entry;
            if(records.count!=2)continue;
            const auto separator=key.first.find('|');
            const std::string_view owner=separator==std::string_view::npos?key.first:key.first.substr(0,separator);
            if(!topology_segments.contains({owner,key.second}))ordered.push_back(&entry);
        }
        std::ranges::sort(ordered,[&](const auto* first,const auto* second){
            const auto& a=first->first;const auto& b=second->first;
            return std::tie(a.first,rounded_points[a.second.first],rounded_points[a.second.second])<
                std::tie(b.first,rounded_points[b.second.first],rounded_points[b.second.second]);
        });
        Impl::SilhouetteConnectivity connectivity;
        connectivity.vertex_addresses=std::move(vertex_addresses);
        connectivity.triangles=impl_->mesh.triangles;
        connectivity.face_runs=std::move(face_runs);
        connectivity.topology=std::move(topology_signature);
        connectivity.face_keys.assign(face_keys.begin(),face_keys.end());
        std::unordered_map<std::string_view,std::size_t> face_addresses;
        face_addresses.reserve(connectivity.face_keys.size());
        for(std::size_t index=0;index<connectivity.face_keys.size();++index)
            face_addresses.emplace(connectivity.face_keys[index],index);
        connectivity.sources.reserve(ordered.size());
        for(const auto* entry:ordered) {
            const auto& records=entry->second;
            impl_->silhouette_candidates.push_back({records.first.first,
                records.first.second,records.first.normal,records.second_normal,
                impl_->mesh.triangle_references[records.first_triangle].valid(),records.first_triangle});
            connectivity.sources.push_back({records.first_triangle,records.second_triangle,records.first_side,
                entry->first.second.first,entry->first.second.second,
                face_addresses.at(entry->first.first)});
        }
        impl_->silhouette_connectivity=std::move(connectivity);
        setProperty("gpuAdjacencyBuildCount",property("gpuAdjacencyBuildCount").toULongLong()+1);
        }
    }
    // Navigation may refresh screen-sized reference helpers without changing
    // any surface. Retain the completed outline only when all of its actual
    // point/normal/faceted-fallback inputs are exactly unchanged.
    if(previous_smooth==impl_->smooth_silhouette_triangles&&
       previous_candidates==impl_->silhouette_candidates) {
        impl_->silhouette_view_direction=previous_direction;
        impl_->silhouette_vertex_count=previous_vertex_count;
    }
    upload_phase("adjacency");
    if(qEnvironmentVariableIsSet("ZIMA_CPP_VERIFY_GPU_UPLOAD")||qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_COMMAND")) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(vertex_data.data()),vertex_data.size()*sizeof(float)));
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(line_data.data()),line_data.size()*sizeof(float)));
        for(const auto& edge:impl_->silhouette_candidates) {
            for(const auto& value:{edge.first,edge.second,edge.normal_a,edge.normal_b}) {
                const std::array<float,3> coordinates{value.x(),value.y(),value.z()};
                hash.addData(QByteArrayView(reinterpret_cast<const char*>(coordinates.data()),sizeof(coordinates)));
            }
            const std::uint8_t smooth=edge.smooth_face;
            hash.addData(QByteArrayView(reinterpret_cast<const char*>(&smooth),sizeof(smooth)));
        }
        setProperty("gpuMeshUploadDigest",hash.result().toHex());
    }
    impl_->gpu_dirty = false;
}

void MeshView::paintGL() {
    if(!impl_->active_sketch_owner_id.empty())
        static_cast<void>(sketch_hv_swapped(impl_->active_sketch_owner_id,impl_->editing_curve_path));
    const bool profile_paint=qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_VIEW_PAINT");
    QElapsedTimer paint_timer;paint_timer.start();
    const auto paint_phase=[&](const char* name){if(profile_paint)std::fprintf(stderr,"View paint %s: %.3f ms\n",name,paint_timer.nsecsElapsed()/1e6);paint_timer.restart();};
    impl_->relation_marks->labels.clear();
    impl_->relation_marks->setGeometry(rect());impl_->relation_marks->raise();impl_->relation_marks->update();
    update_annotation_presentation();
    // Coincident local planes are common while entering Curve/Sweep Points.
    // Paint inspected frames last so a later ordinary frame cannot erase
    // their cyan outline while leaving only the label visibly highlighted.
    std::vector<const zima::kernel::ViewerEdge*> plane_edges;
    for (const auto& edge : impl_->mesh.edges) {
        if (!impl_->origin_visible({edge.reference.owner_id,edge.reference.semantic_key,edge.reference.instance_path})) continue;
        if (edge.reference.semantic_key == "border" || edge.reference.semantic_key == "plane:end" ||
            edge.reference.semantic_key.starts_with("origin:plane:"))
            plane_edges.push_back(&edge);
    }
    for(const auto& edge:impl_->transient_context_mesh.edges)
        if(edge.reference.semantic_key=="border" ||
           edge.reference.semantic_key=="plane:end" ||
           edge.reference.semantic_key.starts_with("origin:plane:"))
            plane_edges.push_back(&edge);
    auto highlighted_plane = impl_->confirmed_candidate;
    if (!highlighted_plane && !impl_->candidates.empty() &&
        impl_->active_candidate < impl_->candidates.size())
        highlighted_plane = impl_->candidates[impl_->active_candidate];
    std::stable_partition(plane_edges.begin(), plane_edges.end(),
        [&](const auto* edge) {
            const auto& ref = edge->reference;
            const bool selected = highlighted_plane &&
                highlighted_plane->instance_path == ref.instance_path &&
                ((!edge->display_owner_id.empty() &&
                  highlighted_plane->kind == CandidateKind::Container &&
                  highlighted_plane->semantic_key.empty() &&
                  highlighted_plane->owner_id == edge->display_owner_id) ||
                 (highlighted_plane->owner_id == ref.owner_id &&
                  ((highlighted_plane->kind == CandidateKind::Container &&
                    highlighted_plane->semantic_key == "origin") ||
                   (highlighted_plane->kind == CandidateKind::Plane &&
                    highlighted_plane->semantic_key == ref.semantic_key))));
            return !selected &&
                !impl_->constraint_reference_edges.contains(edge_key(ref));
        });
    glClear(GL_DEPTH_BUFFER_BIT);
    // QPainter overlays and the individual display-mode passes modify GL
    // state.  Re-establish the Python viewer's common baseline every frame.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_SCISSOR_TEST);
    const auto theme = view_theme(palette());
    constexpr int background_bands = 64;
    const qreal pixel_ratio = devicePixelRatioF();
    const int framebuffer_width = std::max(
        1, static_cast<int>(std::lround(width() * pixel_ratio)));
    const int framebuffer_height = std::max(
        1, static_cast<int>(std::lround(height() * pixel_ratio)));
    for (int band = 0; band < background_bands; ++band) {
        const float factor = (static_cast<float>(band) + 0.5F) /
            static_cast<float>(background_bands);
        const auto channel = [factor](int bottom, int top) {
            return (bottom + (top - bottom) * factor) / 255.0F;
        };
        const int first_y = framebuffer_height * band / background_bands;
        const int last_y = framebuffer_height * (band + 1) / background_bands;
        glScissor(0, first_y, framebuffer_width, std::max(1, last_y - first_y));
        glClearColor(channel(theme.bottom.red(), theme.top.red()), channel(theme.bottom.green(), theme.top.green()), channel(theme.bottom.blue(), theme.top.blue()), 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    impl_->vertex_array.bind();
    if(qEnvironmentVariableIsSet("ZIMA_CPP_VERIFY_GPU_UPLOAD")&&property("forceMeshUpload").toBool()) {
        setProperty("forceMeshUpload",false);impl_->gpu_dirty=true;
    }
    if (impl_->gpu_dirty) upload_mesh();
    if (!impl_->program.isLinked()) {
        impl_->vertex_array.release();
        // Reference geometry is viewer-owned 2D overlay data and must remain
        // usable even when the solid-body OpenGL shader is unavailable.  In
        // particular, a newly created empty Part has no body at all, but its
        // persisted Origin must still be visible.
        const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                           (1.0F - clip.y()) * height() / 2.0F);
        };
        const double reference_scale = impl_->view_scale /
            std::max(impl_->reference_view_scale, 1.0e-6F);
        QOpenGLPaintDevice overlay_device(QSize(framebuffer_width,framebuffer_height));
        overlay_device.setDevicePixelRatio(pixel_ratio);
        QPainter painter(&overlay_device);
        std::vector<DimensionTextLabel> dimension_texts;
        painter.setRenderHint(QPainter::Antialiasing);
        if(impl_->show_dimension_frame) {
            painter.setPen(QPen(QColor("#808080"),1,Qt::DashLine));
            const auto key=impl_->confirmed_candidate?kernel::ObjectEnvelopeKey{impl_->confirmed_candidate->owner_id,impl_->confirmed_candidate->instance_path}:kernel::ObjectEnvelopeKey{};
            const auto corners=(impl_->object_bounds.contains(key)&&impl_->object_bounds.at(key).valid?impl_->object_bounds.at(key):impl_->dimension_bounds).corners();
            for(unsigned i=0;i<8;++i)for(unsigned bit:{1U,2U,4U})if(!(i&bit))painter.drawLine(project(corners[i]),project(corners[i|bit]));
        }
        paint_normal_text(painter,impl_->mesh.edges,impl_->transient_edges,project,impl_->show_sketches,impl_->show_symbols);
        for(const auto& image:impl_->mesh.images) {
            QPolygonF target;for(const auto& p:image.corners)target<<project(p);
            paint_embedded_image(painter,image.data_base64,image.format,target);
            const auto matches=[&](const auto& c){return c && c->kind==CandidateKind::TemplateImage && c->owner_id==image.reference.owner_id && c->semantic_key==image.reference.semantic_key;};
            if(matches(impl_->confirmed_candidate)||matches(hovered_candidate())) {
                painter.setPen(QPen(matches(impl_->confirmed_candidate)?interaction::selected:interaction::hover,2));painter.setBrush(Qt::NoBrush);painter.drawPolygon(target);
            }
        }
        const auto draw_reference_segment = [&](const QPointF& first,
                const QPointF& second, const QColor& color, double width) {
            const QLineF line(first, second);
            if (line.length() <= 1.0e-6) return;
            const QPointF normal{-line.dy() / line.length() * width * 0.5,
                                 line.dx() / line.length() * width * 0.5};
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{first + normal, second + normal,
                                          second - normal, first - normal});
            painter.restore();
        };
if (impl_->show_origins) {
            painter.setPen(QPen(interaction::axis, 1.5));
            for (const auto* plane_edge : plane_edges) {
                const auto& edge = *plane_edge;
                const bool origin = edge.reference.semantic_key.starts_with(
                    "origin:plane:");
                if (!origin) continue;
                // Origin planes are painted as a screen-space QPainter overlay
                // (not the depth-tested GL edge pass), so they must consult the
                // same constraint-reference highlight state as
                // edge_is_highlighted()/edge_display_color() above; otherwise
                // clicking a reference row in a placement dialog never shows a
                // visible highlight for an Origin-plane reference.
                const bool referenced =
                    impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id) ||
                    impl_->constraint_reference_edges.contains(edge_key(edge.reference));
                const QColor plane_color = referenced
                    ? interaction::selected : interaction::axis;
                zima::kernel::Vec3 center;
                const std::size_t corner_count = edge.points.size() > 1
                    ? edge.points.size() - 1 : edge.points.size();
                for (std::size_t index = 0; index < corner_count; ++index) {
                    const auto& point = edge.points[index];
                    center.x += point.x; center.y += point.y; center.z += point.z;
                }
                if (corner_count != 0) {
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                }
                const double plane_scale = is_screen_constant_plane(
                    edge.reference.semantic_key) ? reference_scale : 1.0;
                const auto plane_point = [&](const auto& point) {
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * plane_scale,
                        center.y + (point.y - center.y) * plane_scale,
                        center.z + (point.z - center.z) * plane_scale};
                };
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    draw_reference_segment(project(plane_point(edge.points[index - 1])),
                        project(plane_point(edge.points[index])), plane_color, 1.5);
                }
                if (!edge.points.empty()) {
                    painter.setPen(QPen(plane_color, 1.5));
                    painter.drawText(project(plane_point(edge.points.front())) + QPointF(6.0, -5.0),
                        QString::fromStdString(edge.reference.semantic_key.substr(
                            std::string("origin:plane:").size())).toUpper());
                }
            }
        }
        if (impl_->show_origins) {
            for (const auto& axis : impl_->mesh.axes) {
                if (!impl_->origin_visible({axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path})) continue;
                if (!axis.reference.semantic_key.starts_with("origin:axis:")) continue;
                const QColor color = axis.reference.semantic_key == "origin:axis:x"
                    ? QColor(232, 76, 61)
                    : axis.reference.semantic_key == "origin:axis:y"
                        ? QColor(46, 204, 112) : QColor(51, 153, 219);
                painter.setPen(QPen(color, 1.5));
                const QPointF start = project(axis.point);
                const QPointF end = project({
                    axis.point.x + axis.direction.x * axis.display_length * reference_scale,
                    axis.point.y + axis.direction.y * axis.display_length * reference_scale,
                    axis.point.z + axis.direction.z * axis.display_length * reference_scale});
                draw_reference_segment(start, end, color, 1.5);
                const QLineF line(start, end);
                if (line.length() > 1.0) {
                    const QPointF unit = (line.p2() - line.p1()) / line.length();
                    const QPointF normal{-unit.y(), unit.x()};
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(color);
                    painter.drawPolygon(QPolygonF{end, end - unit * 20.0 + normal * 3.526,
                                                  end - unit * 20.0 - normal * 3.526});
                }
                painter.setPen(QPen(color, 1.5));
                painter.drawText(end + QPointF(5.0, -4.0),
                    axis.label.empty() ? QString::fromStdString(
                        axis.reference.semantic_key.substr(
                            std::string("origin:axis:").size())).toUpper()
                    : QString::fromStdString(axis.label).toUpper());
            }
        }
        if (impl_->show_origins) {
                painter.setPen(QPen(QColor(0, 0, 0), 1.0));
                painter.setBrush(QColor(0, 0, 0));
            for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                if (point.reference.semantic_key != "origin:point") continue;
                const QPointF center = project(point.position);
                const auto color=impl_->origin_point_color(point);
                draw_circular_marker(painter, center, color);
                painter.setPen(QPen(color,1.0));
                if (!point.label.empty()) {
                    painter.drawText(center + QPointF(8.0, -6.0),
                                     QString::fromStdString(point.label));
                }
            }
        }
        if (!impl_->mesh.constraint_markers.empty()) {
            auto marker_font = painter.font();
            marker_font.setBold(true);
            painter.setFont(marker_font);
            painter.setPen(QPen(QColor("#7CFF6B"), 2.0));
            std::map<std::pair<int, int>, int> occupied_slots;
            for (std::size_t marker_index = 0;
                 marker_index < impl_->mesh.constraint_markers.size(); ++marker_index) {
                const auto& marker = impl_->mesh.constraint_markers[marker_index];
                const QPointF anchor = project(marker.position);
                const auto key = std::pair{
                    static_cast<int>(std::lround(anchor.x())),
                    static_cast<int>(std::lround(anchor.y()))};
                const int slot = occupied_slots[key]++;
                painter.drawText(anchor + QPointF(7.0 + slot * 16.0, -7.0),
                    QString::fromStdString(sketch_hv_label(marker.label,marker.reference.owner_id,marker.reference.instance_path)));
            }
        }
        return;
    }
    impl_->program.bind();
    const QMatrix4x4 view = impl_->view();
    impl_->program.setUniformValue("mvp",
        impl_->projection(width(), height()) * view);
    impl_->program.setUniformValue("normalMatrix", view.normalMatrix());
    const auto bind_attributes = [&](QOpenGLBuffer& buffer) {
        buffer.bind();
        impl_->program.enableAttributeArray(0);
        impl_->program.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        impl_->program.enableAttributeArray(1);
        impl_->program.setAttributeBuffer(
            1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
    };

    const auto draw_triangles = [&](bool depth_only=false) {
        impl_->program.setUniformValue("unlit", 0);
        bind_attributes(impl_->vertices);
        // Keep body edges in front of their supporting surface.  Without the
        // same polygon offset used by the Python viewer, coplanar fragments
        // z-fight and the three edge display modes look nearly identical or
        // lose arbitrary edge sections depending on the driver.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0F, 1.0F);
        const auto color_vector = [](const QColor& color) {
            return QVector4D(static_cast<float>(color.redF()),
                static_cast<float>(color.greenF()),
                static_cast<float>(color.blueF()),
                static_cast<float>(color.alphaF()));
        };
        impl_->prepare_surface_batches();
        const auto set_material = [&](const auto& batch) {
            const auto& style = batch.style;
            const auto& color = batch.color;
            const bool technological_thread = batch.technological_thread;
            // The solid is pushed slightly back for edge readability. A
            // coincident technological sheet needs the unshifted depth so
            // its grey face remains visible on an outside cylinder, while
            // genuinely nearer solid geometry still occludes it.
            glPolygonOffset(technological_thread ? 0.0F : 1.0F,
                technological_thread ? 0.0F : 1.0F);
            impl_->program.setUniformValue("roughness",static_cast<float>(style.roughness));
            impl_->program.setUniformValue("metallic",static_cast<float>(style.metallic));
            impl_->program.setUniformValue("color", color_vector(color));
        };
        // Surface alpha controls fill blending only. The separate edge-depth
        // pass must include every surface, even a fully transparent one.
        // During fill rendering only opaque geometry establishes depth.
        for (const auto& batch : impl_->surface_batches) {
            if(!depth_only&&batch.color.alpha()<255)continue;
            set_material(batch);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.first * 3),
                static_cast<GLsizei>(batch.count * 3));
        }
        if(!depth_only) {
            struct TransparentTriangle { std::uint32_t triangle; const Impl::SurfaceBatch* batch; float depth; };
            std::vector<TransparentTriangle> sorted;
            for(const auto& batch:impl_->surface_batches)if(batch.color.alpha()>0&&batch.color.alpha()<255)
                for(std::size_t triangle=batch.first;triangle<batch.first+batch.count;++triangle) {
                    QVector3D center;
                    for(std::size_t corner=0;corner<3;++corner) {
                        const auto& p=impl_->mesh.vertices[impl_->mesh.triangles[triangle*3+corner]];
                        center+=QVector3D(p.x,p.y,p.z);
                    }
                    sorted.push_back({static_cast<std::uint32_t>(triangle),&batch,(view*(center/3)).z()});
                }
            std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
            if(!sorted.empty()) {
                std::vector<std::uint32_t> indices;indices.reserve(sorted.size()*3);
                for(const auto& triangle:sorted)for(unsigned corner=0;corner<3;++corner)
                    indices.push_back(triangle.triangle*3+corner);
                impl_->transparent_triangles.bind();
                impl_->transparent_triangles.allocate(indices.data(),static_cast<int>(indices.size()*sizeof(std::uint32_t)));
                glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
                for(std::size_t first=0;first<sorted.size();) {
                    std::size_t end=first+1;
                    while(end<sorted.size()&&sorted[end].batch==sorted[first].batch)++end;
                    set_material(*sorted[first].batch);
                    glDrawElements(GL_TRIANGLES,static_cast<GLsizei>((end-first)*3),GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(first*3*sizeof(std::uint32_t)));
                    first=end;
                }
                glDepthMask(GL_TRUE);glDisable(GL_BLEND);impl_->transparent_triangles.release();
            }
        }
        glPolygonOffset(1.0F, 1.0F);
        glDisable(GL_POLYGON_OFFSET_FILL);
    };
    // Resolves one final edge colour by priority, mirroring Python's
    // _edge_display_color() in zima_cad/viewer.py: selected > object overlay
    // > hovered > feature preview (line-drawing modes only) > colour
    // override > base colour (white). `hovered`/`selected` here are driven
    // by the existing single-candidate hover/confirm mechanism rather than
    // Python's separate _hovered_edge/_selected_edge fields, since C++ has
    // one shared candidate-cycling model instead of a dedicated edge cursor.
    const auto exact_edge_treatment_wire = [&](const auto& highlighted)
            -> const std::vector<std::size_t>* {
        if (!highlighted || highlighted->kind != CandidateKind::Container ||
            !highlighted->semantic_key.empty()) return nullptr;
        const auto found = impl_->edge_treatment_boundary_edge_indices.find(
            {highlighted->owner_id, highlighted->instance_path});
        return found == impl_->edge_treatment_boundary_edge_indices.end()
            ? nullptr : &found->second;
    };
    const auto ordinary_preview_candidate=[&]()->std::optional<ViewerCandidate> {
        if(impl_->geometry_editing_presentation)return {};
        auto candidate=impl_->confirmed_candidate;
        if((!candidate||candidate->kind==CandidateKind::Dimension)&&impl_->inspected_container)
            candidate=impl_->inspected_container;
        if(!candidate&&!impl_->candidates.empty())candidate=impl_->candidates[impl_->active_candidate];
        if(candidate&&candidate->kind==CandidateKind::Container&&
            (candidate->semantic_key.empty()||candidate->semantic_key=="solid"||
                ((candidate->semantic_key=="sketch"||candidate->semantic_key=="axis")&&impl_->container_preview_authoritative&&
                    impl_->container_preview_authoritative(candidate->owner_id,candidate->instance_path)))&&
            impl_->has_authored_container_wire(*candidate))return candidate;
        return {};
    }();
    const bool authored_preview=!impl_->geometry_editing_presentation&&
        (ordinary_preview_candidate||impl_->inspected_container||!impl_->container_inspection_wire.empty());
    std::set<std::string> ordinary_sketch_point_owners;
    if(ordinary_preview_candidate)for(const auto& edge:impl_->mesh.edges) {
        if(is_sketch_wire_edge(edge)&&candidate_recolors_wire_edge(*ordinary_preview_candidate,edge))
            ordinary_sketch_point_owners.insert(edge.reference.owner_id);
    }
    // Source-less imported wire and explicit editing still use their existing
    // result-edge batches. Authored feature hover/selection uses one preview.
    const auto feature_edge_state=[&] {
        return !impl_->feature_selected_edges.empty()||!impl_->feature_hover_edges.empty()||
            !impl_->feature_selected_edge_indices.empty()||!impl_->feature_hover_edge_indices.empty();
    };
    const auto line_candidate=[&] {
        auto candidate=impl_->confirmed_candidate;
        if((!candidate||candidate->kind==CandidateKind::Dimension)&&impl_->inspected_container)
            candidate=impl_->inspected_container;
        if(!candidate&&!impl_->candidates.empty())candidate=impl_->candidates[impl_->active_candidate];
        if(authored_preview)candidate.reset();
        return candidate;
    }();
    const std::optional<EdgeKey> color_key=line_candidate&&line_candidate->kind==CandidateKind::Container&&
        (line_candidate->semantic_key.empty()||line_candidate->semantic_key=="solid")
        ?std::optional<EdgeKey>{{line_candidate->owner_id,{},line_candidate->instance_path}}:std::nullopt;
    if(color_key&&color_key!=impl_->container_color_key) {
        impl_->container_color_edges.clear();impl_->container_color_edges.reserve(impl_->mesh.edges.size());
        for(const auto& edge:impl_->mesh.edges)
            impl_->container_color_edges.push_back(candidate_recolors_wire_edge(*line_candidate,edge));
        impl_->container_selected_ranges.clear();impl_->container_other_ranges.clear();
        for(std::size_t i=0;i<impl_->line_ranges.size();++i) {
            auto& ranges=impl_->container_color_edges.at(impl_->line_mesh_edge_indices.at(i))
                ?impl_->container_selected_ranges:impl_->container_other_ranges;
            const auto [first,count]=impl_->line_ranges[i];
            if(!ranges.empty()&&ranges.back().first+ranges.back().second==first)ranges.back().second+=count;
            else ranges.emplace_back(first,count);
        }
        impl_->container_color_key=color_key;
        setProperty("containerColorBuildCount",property("containerColorBuildCount").toULongLong()+1);
    }
    const auto candidate_colors_edge=[&](const ViewerCandidate& candidate,const kernel::ViewerEdge& edge,std::size_t index) {
        if(color_key&&candidate.kind==CandidateKind::Container&&(candidate.semantic_key.empty()||candidate.semantic_key=="solid")&&
           candidate.owner_id==color_key->owner_id&&candidate.instance_path==color_key->instance_path&&
           index<impl_->container_color_edges.size())return bool(impl_->container_color_edges[index]);
        return candidate_recolors_wire_edge(candidate,edge);
    };
    const auto component_edges=confirmed_component_edge_indices();
    const auto edge_is_highlighted = [&](const zima::kernel::ViewerEdge& edge,
            std::size_t mesh_edge_index,
            const std::optional<ViewerCandidate>& highlighted,
            bool separate_candidate_wire) {
        const auto key = edge_key(edge.reference);
        const bool preview = impl_->feature_preview_owner_ids.contains(edge.reference.owner_id) &&
            (impl_->display_mode == DisplayMode::Wire ||
             impl_->display_mode == DisplayMode::HiddenEdges ||
             impl_->display_mode == DisplayMode::NoHiddenEdges);
        const bool candidate_match = highlighted && !separate_candidate_wire &&
            candidate_colors_edge(*highlighted,edge,mesh_edge_index);
        return candidate_match ||
            impl_->edge_treatment_selection_edges.contains(key) ||
            impl_->feature_selected_edges.contains(key)||impl_->feature_hover_edges.contains(key)||
            impl_->feature_selected_edge_indices.contains(mesh_edge_index)||impl_->feature_hover_edge_indices.contains(mesh_edge_index) ||
            component_edges.contains(mesh_edge_index) ||
            impl_->constraint_reference_edges.contains(key) ||
            impl_->assembly_reference_edges.contains(key) ||
            impl_->object_overlay_main_edge_keys.contains(key) ||
            impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id) ||
            impl_->selected_container_content_ids.contains(edge.reference.owner_id) ||
            preview;
    };
    const auto edge_display_color = [&](const zima::kernel::ViewerEdge& edge,
            std::size_t mesh_edge_index,
            const std::optional<ViewerCandidate>& highlighted,
            bool candidate_is_confirmed, bool separate_candidate_wire) {
        const auto key = edge_key(edge.reference);
        const bool candidate_match = highlighted && !separate_candidate_wire &&
            candidate_colors_edge(*highlighted,edge,mesh_edge_index);
        const bool reference_selected =
            impl_->edge_treatment_selection_edges.contains(key) ||
            impl_->feature_selected_edges.contains(key)||impl_->feature_selected_edge_indices.contains(mesh_edge_index) ||
            component_edges.contains(mesh_edge_index) ||
            impl_->constraint_reference_edges.contains(key) ||
            impl_->assembly_reference_edges.contains(key) ||
            impl_->selected_container_content_ids.contains(edge.reference.owner_id) ||
            impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id);
        if (reference_selected) return interaction::rgba(interaction::selected);
        if (candidate_match && candidate_is_confirmed)
            return interaction::rgba(interaction::selected);
        if (impl_->object_overlay_main_edge_keys.contains(key)) {
            return interaction::rgba(interaction::hover);
        }
        const bool hovered =
            (candidate_match && !candidate_is_confirmed) ||
            impl_->feature_hover_edges.contains(key)||impl_->feature_hover_edge_indices.contains(mesh_edge_index);
        if (hovered) return interaction::rgba(interaction::hover);
        const bool preview = impl_->feature_preview_owner_ids.contains(edge.reference.owner_id) &&
            (impl_->display_mode == DisplayMode::Wire ||
             impl_->display_mode == DisplayMode::HiddenEdges ||
             impl_->display_mode == DisplayMode::NoHiddenEdges);
        if (preview) return interaction::rgba(interaction::selected);
        if(edge.surface_result) return interaction::rgba(interaction::surface);
        if (impl_->edge_color_override) {
            const auto& color = *impl_->edge_color_override;
            return QVector4D(static_cast<float>(color.redF()),
                static_cast<float>(color.greenF()),
                static_cast<float>(color.blueF()), 1.0F);
        }
        const auto& semantic = edge.reference.semantic_key;
        if (semantic.starts_with("hole:cosmetic-thread:") ||
            semantic.starts_with("thread:wire:") ||
            semantic.starts_with("thread:boundary:")) {
            return QVector4D(0.62F, 0.62F, 0.62F, 1.0F);
        }
        return interaction::rgba(theme.foreground);
    };
    const auto draw_lines = [&](bool force_black_if_not_highlighted = false,
                                bool highlighted_only = false) {
        impl_->program.setUniformValue("unlit", 1);
        bind_attributes(impl_->lines);
        const auto& highlighted=line_candidate;
        const bool confirmed=impl_->confirmed_candidate.has_value()||impl_->inspected_container.has_value();
        // Current result edges retain their normal GL pass and width. The
        // confirmed source wire has its own cached, single GPU batch below.
        const bool separate_candidate_wire = impl_->geometry_editing_presentation&&exact_edge_treatment_wire(highlighted) != nullptr;
        const bool simple_container=color_key&&!separate_candidate_wire&&!impl_->wire_has_individual_colors&&
            impl_->edge_treatment_selection_edges.empty()&&impl_->feature_selected_edges.empty()&&
            impl_->feature_hover_edges.empty()&&impl_->feature_selected_edge_indices.empty()&&
            component_edges.empty()&&impl_->feature_hover_edge_indices.empty()&&
            impl_->constraint_reference_edges.empty()&&impl_->assembly_reference_edges.empty()&&
            impl_->object_overlay_main_edge_keys.empty()&&impl_->constraint_reference_owner_ids.empty()&&
            impl_->selected_container_content_ids.empty()&&impl_->feature_preview_owner_ids.empty();
        if(simple_container) {
            const auto draw_cached=[&](const auto& ranges) {
                for(const auto& [first,count]:ranges)glDrawArrays(GL_LINES,first,count);
            };
            if(force_black_if_not_highlighted) {
                impl_->program.setUniformValue("color",QVector4D(0,0,0,1));
                draw_cached(impl_->container_other_ranges);
            } else {
                if(!highlighted_only) {
                    const auto base=impl_->edge_color_override
                        ?QVector4D(impl_->edge_color_override->redF(),impl_->edge_color_override->greenF(),impl_->edge_color_override->blueF(),1.F)
                        :interaction::rgba(theme.foreground);
                    impl_->program.setUniformValue("color",base);
                    glDrawArrays(GL_LINES,0,impl_->line_vertex_count);
                }
                impl_->program.setUniformValue("color",interaction::rgba(confirmed?interaction::selected:interaction::hover));
                draw_cached(impl_->container_selected_ranges);
            }
            return;
        }
        const bool has_highlight_state = (highlighted && !separate_candidate_wire) ||
            !impl_->edge_treatment_selection_edges.empty() ||
            feature_edge_state() || !component_edges.empty() ||
            !impl_->constraint_reference_edges.empty() || !impl_->assembly_reference_edges.empty() ||
            !impl_->object_overlay_main_edge_keys.empty() || !impl_->constraint_reference_owner_ids.empty() ||
            !impl_->selected_container_content_ids.empty() || !impl_->feature_preview_owner_ids.empty();
        // Ordinary navigation has no per-edge highlight to resolve. Keep the
        // same whole-buffer draw without constructing reference keys for it.
        if(!force_black_if_not_highlighted && !has_highlight_state) {
            if(!highlighted_only) {
                const QVector4D color=impl_->edge_color_override
                    ? QVector4D(impl_->edge_color_override->redF(),impl_->edge_color_override->greenF(),
                        impl_->edge_color_override->blueF(),1.0F)
                    : interaction::rgba(theme.foreground);
                impl_->program.setUniformValue("color",color);
                glDrawArrays(GL_LINES,0,impl_->line_vertex_count);
            }
            return;
        }
        const auto draw_ranges = [&](const std::vector<GLint>& first,
                                     const std::vector<GLsizei>& count) {
            for (std::size_t index = 0; index < first.size(); ++index) {
                glDrawArrays(GL_LINES, first[index], count[index]);
            }
        };
        if (force_black_if_not_highlighted) {
            std::vector<GLint> ordinary_first;
            std::vector<GLsizei> ordinary_count;
            std::vector<GLint> thread_first;
            std::vector<GLsizei> thread_count;
            ordinary_first.reserve(impl_->line_ranges.size());
            ordinary_count.reserve(impl_->line_ranges.size());
            for (std::size_t index = 0;
                 index < impl_->line_ranges.size(); ++index) {
                if (edge_is_highlighted(
                        impl_->line_edges[index],
                        impl_->line_mesh_edge_indices[index], highlighted, separate_candidate_wire)) continue;
                const auto& semantic =
                    impl_->line_edges[index].reference.semantic_key;
                const bool cosmetic_thread = semantic.starts_with(
                        "hole:cosmetic-thread:") ||
                    semantic.starts_with("thread:wire:");
                auto& first = cosmetic_thread ? thread_first : ordinary_first;
                auto& count = cosmetic_thread ? thread_count : ordinary_count;
                first.push_back(impl_->line_ranges[index].first);
                count.push_back(impl_->line_ranges[index].second);
            }
            if (thread_first.empty() && ordinary_first.size() ==
                    impl_->line_ranges.size()) {
                impl_->program.setUniformValue(
                    "color", QVector4D(0.0F, 0.0F, 0.0F, 1.0F));
                glDrawArrays(GL_LINES, 0, impl_->line_vertex_count);
                return;
            }
            impl_->program.setUniformValue(
                "color", QVector4D(0.0F, 0.0F, 0.0F, 1.0F));
            draw_ranges(ordinary_first, ordinary_count);
            // A cosmetic thread commonly lies entirely behind/on its
            // supporting cylinder. Pure black made its two rings and two
            // boundary lines disappear against the dark View background.
            impl_->program.setUniformValue(
                "color", QVector4D(0.55F, 0.55F, 0.55F, 1.0F));
            draw_ranges(thread_first, thread_count);
            return;
        }

        const QVector4D base_color = impl_->edge_color_override
            ? QVector4D(static_cast<float>(impl_->edge_color_override->redF()),
                  static_cast<float>(impl_->edge_color_override->greenF()),
                  static_cast<float>(impl_->edge_color_override->blueF()), 1.0F)
            : interaction::rgba(theme.foreground);
        struct ColorBatch {
            QVector4D color;
            std::vector<GLint> first;
            std::vector<GLsizei> count;
        };
        std::vector<ColorBatch> batches;
        for (std::size_t index = 0; index < impl_->line_ranges.size(); ++index) {
            const auto& edge = impl_->line_edges[index];
            const auto mesh_edge_index = impl_->line_mesh_edge_indices[index];
            if (!edge_is_highlighted(
                    edge, mesh_edge_index, highlighted, separate_candidate_wire)) continue;
            const auto color = edge_display_color(
                edge, mesh_edge_index, highlighted, confirmed, separate_candidate_wire);
            auto batch = std::find_if(batches.begin(), batches.end(),
                [&](const auto& value) { return value.color == color; });
            if (batch == batches.end()) {
                batches.push_back({color, {}, {}});
                batch = std::prev(batches.end());
            }
            batch->first.push_back(impl_->line_ranges[index].first);
            batch->count.push_back(impl_->line_ranges[index].second);
        }
        if (highlighted_only) {
            // Plain Shaded deliberately hides the ordinary body wire, but it
            // must not hide the feedback for the one candidate produced by
            // the common picker. Draw only hover/selected persisted edges; do
            // not reintroduce a complete second wire for large models.
            for (const auto& batch : batches) {
                impl_->program.setUniformValue("color", batch.color);
                draw_ranges(batch.first, batch.count);
            }
            return;
        }
        if (batches.size() == 1 &&
            batches.front().first.size() == impl_->line_ranges.size()) {
            // The complete ordinary/hovered/selected wire has one colour.
            // This is the common large-STEP case and remains one basic
            // glDrawArrays call without requiring glMultiDrawArrays or a
            // newer OpenGL profile on Linux/Wayland.
            impl_->program.setUniformValue("color", batches.front().color);
            glDrawArrays(GL_LINES, 0, impl_->line_vertex_count);
            return;
        }
        impl_->program.setUniformValue("color", base_color);
        glDrawArrays(GL_LINES, 0, impl_->line_vertex_count);
        for (const auto& batch : batches) {
            impl_->program.setUniformValue("color", batch.color);
            draw_ranges(batch.first, batch.count);
        }
    };
    const auto depth_prepass = [&] {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        draw_triangles(true);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    };
    const auto draw_visible_lines = [&] {
        glDepthFunc(GL_LEQUAL);
        draw_lines();
        glDepthFunc(GL_LESS);
    };
    // Per-edge colour now comes from edge_display_color() by priority
    // (selected/overlay/hover/preview/override/base white), matching
    // zima_cad/viewer.py's _edge_display_color(). Display-mode switch below
    // only toggles depth handling and the hidden_edges black underlay pass.
    switch (impl_->display_mode) {
        case DisplayMode::Wire:
            glDisable(GL_DEPTH_TEST);
            draw_lines();
            glEnable(GL_DEPTH_TEST);
            break;
        case DisplayMode::HiddenEdges:
            depth_prepass();
            glDisable(GL_DEPTH_TEST);
            draw_lines(/*force_black_if_not_highlighted=*/true);
            glEnable(GL_DEPTH_TEST);
            draw_visible_lines();
            break;
        case DisplayMode::NoHiddenEdges:
            depth_prepass();
            draw_visible_lines();
            break;
        case DisplayMode::ShadedWithEdges:
            // Default body fill matches Python's ViewerMesh surface color
            // #B9C2CC (zima_cad/viewer.py:565), not a hardcoded green.
            draw_triangles();
            depth_prepass();
            draw_visible_lines();
            break;
        case DisplayMode::Shaded:
            draw_triangles();
            glDepthFunc(GL_LEQUAL);
            draw_lines(/*force_black_if_not_highlighted=*/false,
                       /*highlighted_only=*/true);
            glDepthFunc(GL_LESS);
            break;
    }
    // Whole-object and individual-face selection are wire-only.
    setProperty("selectedFeatureFaceTriangleCount",QVariant::fromValue<qulonglong>(0));
    if(color_key&&!impl_->geometry_editing_presentation&&impl_->display_mode!=DisplayMode::Wire) {
        // The same cached result-edge ranges show the offered/confirmed wire
        // through occluders. Reuse its VBO at ordinary width, without a model
        // calculation, source-wire upload or screen-space reprojection.
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glLineWidth(1.F);
        draw_lines(/*force_black_if_not_highlighted=*/false,/*highlighted_only=*/true);
        glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);
    }

    // Cosmetic thread lines and unreferenced coloured display helpers (Section
    // hatching) remain visible in Shaded, obeying the body's depth buffer. LEQUAL keeps the portion lying on an
    // outside cylindrical face legible while ordinary LESS/occlusion hides
    // the part running inside material. Hidden Edges and Wire still reveal
    // the complete symbol through their normal display-mode passes.
    {
        const auto& thread_highlight=line_candidate;
        const bool thread_confirmed=impl_->confirmed_candidate.has_value();
        glDepthFunc(GL_LEQUAL);
        bind_attributes(impl_->lines);
        for (std::size_t index = 0; index < impl_->line_ranges.size(); ++index) {
            const auto& edge = impl_->line_edges[index];
            const auto& semantic = edge.reference.semantic_key;
            const bool display_helper = !edge.reference.valid() &&
                !edge.color.empty();
            if (!display_helper && !semantic.starts_with("hole:cosmetic-thread:") &&
                !semantic.starts_with("thread:wire:")) continue;
            QVector4D color(0.62F, 0.62F, 0.62F, 1.0F);
            if (display_helper) {
                const QColor source(QString::fromStdString(edge.color));
                color = QVector4D(source.redF(), source.greenF(), source.blueF(), 1.0F);
            }
            if (!display_helper && thread_highlight &&
                candidate_recolors_wire_edge(*thread_highlight, edge)) {
                color = thread_confirmed
                    ? interaction::rgba(interaction::selected)
                    : interaction::rgba(interaction::hover);
            }
            impl_->program.setUniformValue("color", color);
            glDrawArrays(GL_LINES, impl_->line_ranges[index].first,
                impl_->line_ranges[index].second);
        }
        glDepthFunc(GL_LESS);
    }

    // Boundary/silhouette edges on curved surfaces (cylinders, spheres, ...)
    // that have no real sharp topology edge there. Gated the same way as
    // Python's _draw_gpu_silhouette_edges() in zima_cad/viewer.py: only in
    // the line-drawing display modes, never in plain Shaded.
    const auto silhouette_hover_key=!impl_->geometry_editing_presentation&&!impl_->inspected_container
        ?color_key:std::nullopt;
    if ((impl_->display_mode != DisplayMode::Shaded||silhouette_hover_key) &&
        (!impl_->silhouette_candidates.empty()||!impl_->smooth_silhouette_triangles.empty())) {
        const QVector3D view_direction =
            impl_->orientation.inverted().rotatedVector(QVector3D(0.0F, 0.0F, 1.0F));
        const std::array<float, 3> direction_key{
            view_direction.x(), view_direction.y(), view_direction.z()};
        // Exact comparison: do not round away a small rotation near a silhouette
        // threshold. Geometry uploads and GL-buffer creation invalidate reuse.
        if (impl_->silhouette_view_direction != direction_key) {
            std::vector<float> silhouette_data;
            silhouette_data.reserve(impl_->silhouette_candidates.size() * 12);
            impl_->silhouette_source_triangles.clear();impl_->silhouette_color_key.reset();
            const auto append_segment=[&](const QVector3D& a,const QVector3D& b,std::size_t triangle) {
                silhouette_data.insert(silhouette_data.end(),{
                    a.x(),a.y(),a.z(),0.0F,0.0F,1.0F,
                    b.x(),b.y(),b.z(),0.0F,0.0F,1.0F});
                impl_->silhouette_source_triangles.push_back(triangle);
            };
            for(const auto& triangle:impl_->smooth_silhouette_triangles)
                if(const auto segment=smooth_silhouette_segment(triangle,view_direction))
                    append_segment((*segment)[0],(*segment)[1],triangle.source_triangle);
            for (const auto& candidate : impl_->silhouette_candidates) {
                // Unreferenced faceted meshes retain their established outline.
                // Native faces follow the actual smooth shading field instead.
                if(candidate.smooth_face)continue;
                const float side_a = QVector3D::dotProduct(candidate.normal_a, view_direction);
                const float side_b = QVector3D::dotProduct(candidate.normal_b, view_direction);
                constexpr float epsilon = 1.0e-4F;
                if ((side_a >= -epsilon) == (side_b >= -epsilon)) continue;
                append_segment(candidate.first,candidate.second,candidate.source_triangle);
            }
            impl_->silhouette_vertex_count = static_cast<GLsizei>(silhouette_data.size() / 6);
            if(qEnvironmentVariableIsSet("ZIMA_CPP_VERIFY_GPU_UPLOAD")) {
                setProperty("gpuSilhouetteBuildCount",property("gpuSilhouetteBuildCount").toULongLong()+1);
                setProperty("gpuSilhouetteVertexCount",impl_->silhouette_vertex_count);
            }
            if (impl_->silhouette_vertex_count != 0) {
                impl_->silhouette.bind();
                impl_->silhouette.allocate(silhouette_data.data(),
                    static_cast<int>(silhouette_data.size() * sizeof(float)));
                setProperty("gpuSilhouetteUploadCount",property("gpuSilhouetteUploadCount").toULongLong()+1);
            }
            // Cache the empty result too; the next rotation may make it visible.
            impl_->silhouette_view_direction = direction_key;
        }
        if(impl_->silhouette_color_key!=silhouette_hover_key) {
            impl_->silhouette_highlight_ranges.clear();
            if(silhouette_hover_key)for(std::size_t i=0;i<impl_->silhouette_source_triangles.size();++i) {
                const auto triangle=impl_->silhouette_source_triangles[i];
                if(triangle>=impl_->mesh.triangle_references.size())continue;
                const auto& face=impl_->mesh.triangle_references[triangle];
                if(face.instance_path!=silhouette_hover_key->instance_path||visible_face_owner(face)!=silhouette_hover_key->owner_id)continue;
                auto& ranges=impl_->silhouette_highlight_ranges;const auto first=static_cast<GLint>(i*2);
                if(!ranges.empty()&&ranges.back().first+ranges.back().second==first)ranges.back().second+=2;
                else ranges.emplace_back(first,2);
            }
            impl_->silhouette_color_key=silhouette_hover_key;
            setProperty("silhouetteColorBuildCount",property("silhouetteColorBuildCount").toULongLong()+1);
        }
        if (impl_->silhouette_vertex_count != 0) {
            impl_->silhouette.bind();
            bind_attributes(impl_->silhouette);
            if (impl_->display_mode == DisplayMode::HiddenEdges) {
                glDisable(GL_DEPTH_TEST);
                impl_->program.setUniformValue(
                    "color", QVector4D(0.0F, 0.0F, 0.0F, 1.0F));
                glDrawArrays(GL_LINES, 0,
                    impl_->silhouette_vertex_count);
                glEnable(GL_DEPTH_TEST);
            }
            impl_->program.setUniformValue(
                "color", interaction::rgba(theme.foreground));
            glDepthFunc(GL_LEQUAL);
            if(impl_->display_mode!=DisplayMode::Shaded)
                glDrawArrays(GL_LINES, 0,impl_->silhouette_vertex_count);
            if(silhouette_hover_key) {
                glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glLineWidth(1.F);
                impl_->program.setUniformValue("color",interaction::rgba(impl_->confirmed_candidate?interaction::selected:interaction::hover));
                for(const auto& [first,count]:impl_->silhouette_highlight_ranges)glDrawArrays(GL_LINES,first,count);
                glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);
            }
            glDepthFunc(GL_LESS);
        }
    }

    // Hover, click and inspection share the same authored preview wire.
    // Keep one packet in a GPU buffer; pan,
    // zoom, rotation, repaint and reselecting it never project/rebuild its
    // segments. set_mesh invalidates this cache with its actual source data.
    std::optional<Impl::SourceWireKey> source_wire_key;
    // Empty is also an authoritative inspection result. Its revision must
    // invalidate a preceding nonempty batch, without reading the source twice.
    const bool inspecting_source=impl_->inspected_container.has_value()||!impl_->container_inspection_wire.empty();
    if(inspecting_source)source_wire_key=Impl::SourceWireKey{"@inspection",{},impl_->inspection_wire_revision};
    else if(ordinary_preview_candidate)
        source_wire_key=Impl::SourceWireKey{ordinary_preview_candidate->owner_id,ordinary_preview_candidate->instance_path,0};
    if(source_wire_key&&source_wire_key!=impl_->selected_source_key) {
        std::vector<float> data;
        bool native_strokes=false,model_strokes=false;
        const auto append=[&](const kernel::ViewerEdge& edge) {
            if(edge.construction||edge.parameter_seam)return;
            if(is_sketch_wire_edge(edge))native_strokes=true;else model_strokes=true;
            for(std::size_t i=1;i<edge.points.size();++i)for(const auto& p:{edge.points[i-1],edge.points[i]})
                data.insert(data.end(),{float(p.x),float(p.y),float(p.z),0.F,0.F,1.F});
        };
        if(inspecting_source)for(const auto& edge:impl_->container_inspection_wire)append(edge);
        else {
            const auto& candidate=impl_->inspected_container?impl_->inspected_container:ordinary_preview_candidate;
            for(const auto& edge:impl_->container_preview_wire(candidate->owner_id,candidate->instance_path))append(edge);
        }
        impl_->selected_source_vertex_count=static_cast<GLsizei>(data.size()/6);
        impl_->selected_source_native_sketch=native_strokes&&!model_strokes;
        if(!data.empty()&&data!=impl_->selected_source_data) {
            impl_->selected_source_lines.bind();
            impl_->selected_source_lines.allocate(data.data(),static_cast<int>(data.size()*sizeof(float)));
            setProperty("gpuSourceWireUploadCount",property("gpuSourceWireUploadCount").toULongLong()+1);
        }
        impl_->selected_source_data=std::move(data);
        impl_->selected_source_key=std::move(source_wire_key);
    }
    const bool source_wire_visible=source_wire_key.has_value()&&impl_->selected_source_vertex_count>0;
    setProperty("gpuSourceWireVertexCount",source_wire_visible?impl_->selected_source_vertex_count:0);
    // Native Sketch and 3D Curve wires use the same antialiased QPainter
    // stroke below. Do not add the ordinary one-pixel GPU stroke beneath it.
    // Only the source preview itself decides whether its native Sketch
    // strokes are painted below. A modeling feature may also publish a
    // construction Sketch axis; that helper must never suppress its tool wire.
    if(source_wire_visible&&!impl_->selected_source_native_sketch) {
        impl_->program.setUniformValue("unlit",1);
        impl_->program.setUniformValue("color",interaction::rgba(interaction::selected));
        bind_attributes(impl_->selected_source_lines);
        glDisable(GL_DEPTH_TEST);glLineWidth(1.F);
        glDrawArrays(GL_LINES,0,impl_->selected_source_vertex_count);
        glEnable(GL_DEPTH_TEST);
    }
    std::optional<ViewerCandidate> highlighted = impl_->confirmed_candidate;
    if (!highlighted && !impl_->candidates.empty()) {
        highlighted = impl_->candidates[impl_->active_candidate];
    }
    if(authored_preview&&highlighted&&highlighted->kind==CandidateKind::Container)highlighted.reset();
    impl_->prepare_reference_faces();
    auto inspected_faces=impl_->inspected_faces;
    for(const auto& reference:impl_->reference_faces)
        if(std::ranges::none_of(inspected_faces,[&](const auto& face) {
            return face.owner_id==reference.owner_id && face.semantic_key==reference.semantic_key && face.instance_path==reference.instance_path;
        }))inspected_faces.push_back(reference);
    if (!highlighted && !inspected_faces.empty()) {
        highlighted=inspected_faces.front();
    }
    impl_->program.disableAttributeArray(0);
    impl_->program.disableAttributeArray(1);
    impl_->program.release();
    paint_phase("main GL scene");
    impl_->vertex_array.release();

    const bool axes_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Axis) != impl_->allowed_kinds.end() ||
        std::find(impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::SketchAxis) != impl_->allowed_kinds.end();
    const bool axes_visible = !impl_->transient_context_mesh.axes.empty() ||
        impl_->show_axes || impl_->show_origins ||
        impl_->editing_origin_visible || axes_selectable ||
        std::any_of(impl_->mesh.edges.begin(), impl_->mesh.edges.end(), [&](const auto& edge) {
            std::string source_key;
            return kernel::construction_source_key(edge.reference.semantic_key,source_key).starts_with("centerline:from:") &&
                highlighted && candidate_recolors_wire_edge(*highlighted, edge);
        }) ||
        std::any_of(impl_->mesh.axes.begin(),impl_->mesh.axes.end(),[&](const auto& axis){
            return impl_->constraint_reference_edges.contains(EdgeKey{axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path});
        });
    const auto has_visible_sketch_wire = [&](const auto& edges) {
        return std::any_of(edges.begin(), edges.end(), [&](const auto& edge) {
            return is_sketch_wire_edge(edge) && impl_->sketch_wire_visible(edge);
        });
    };
    const bool sketch_geometry_visible =
        has_visible_sketch_wire(impl_->mesh.edges) ||
        has_visible_sketch_wire(impl_->transient_context_mesh.edges);
    const bool curve3d_geometry_visible = std::any_of(
        impl_->mesh.edges.begin(), impl_->mesh.edges.end(),
        [](const auto& edge) {
            return is_curve3d_edge(edge.reference.semantic_key);
        });
    const bool external_points_visible = impl_->show_sketches && std::any_of(
        impl_->mesh.points.begin(), impl_->mesh.points.end(), [](const auto& point) {
            return point.reference.semantic_key.starts_with("external_point:");
        });
    const bool points_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Vertex) != impl_->allowed_kinds.end() ||
        std::find(impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::SketchPoint) != impl_->allowed_kinds.end();
    const bool point_containers_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Container) != impl_->allowed_kinds.end() &&
        std::any_of(impl_->mesh.points.begin(), impl_->mesh.points.end(),
            [](const auto& point) {
                return point.reference.semantic_key == "point";
            });
    // A Point container's own marker is `always_visible` (it IS the visible
    // entity, see geometry_kernel.hpp) and must keep rendering even while a
    // different construction dialog (Axis/Plane...) narrows the active
    // selection filter to kinds that no longer include Vertex/Container --
    // otherwise an already-created Point disappears from the view the
    // moment another construction command starts picking references.
    const bool always_visible_point_present = std::any_of(
        impl_->mesh.points.begin(), impl_->mesh.points.end(),
        [](const auto& point) { return point.always_visible ||
            (!point.label.empty() && point.display_owner_id==point.reference.owner_id); });
    const bool points_visible = !impl_->transient_context_mesh.points.empty() ||
        ((impl_->show_points || points_selectable || point_containers_selectable ||
          always_visible_point_present) &&
         !impl_->mesh.points.empty()) ||
        external_points_visible || impl_->show_origins ||
        impl_->editing_origin_visible;
    const bool planes_selectable = std::find(
        impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
        CandidateKind::Plane) != impl_->allowed_kinds.end();
    const bool planes_visible = (!impl_->transient_context_mesh.edges.empty() ||
        impl_->show_planes || impl_->show_origins ||
        planes_selectable || impl_->editing_origin_visible) && std::any_of(
        plane_edges.begin(),plane_edges.end(), [](const auto* edge) {
            return edge->reference.semantic_key == "border" || edge->reference.semantic_key == "plane:end" ||
                edge->reference.semantic_key.starts_with("origin:plane:");
        });
    const bool dimensions_visible = !impl_->mesh.dimensions.empty() ||
        !impl_->transient_dimensions.empty();
    const bool overlay_edges_visible=std::ranges::any_of(impl_->mesh.edges,[](const auto& edge){return edge.overlay&&edge.reference.semantic_key=="section:sketch";});
    if (impl_->command_snap_points_visible || overlay_edges_visible || axes_visible || points_visible || planes_visible ||
        sketch_geometry_visible || curve3d_geometry_visible ||
        dimensions_visible ||
        !impl_->transient_edges.empty() || !impl_->transient_points.empty() || !impl_->transient_model_points.empty() ||
        !impl_->transient_labels.empty() ||
        impl_->sketch_cursor.has_value() ||
        impl_->component_origin_handle || !impl_->extent_manipulators.empty() ||
        !impl_->operation_direction_indicators.empty() ||
        // Every candidate kind has an overlay below.  Restricting entry to
        // point/edge/axis candidates accidentally made Face, Container and
        // Occurrence hover completely invisible whenever datum overlays were
        // hidden.  The picker had found the Box face, but the user received
        // no green feedback and it looked unselectable.
        highlighted.has_value() || impl_->show_dimension_frame || impl_->sketch_box_start.has_value()) {
        const QMatrix4x4 mvp = impl_->projection(width(), height()) * view;
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip = mvp * QVector4D(
                point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                           (1.0F - clip.y()) * height() / 2.0F);
        };
        const double reference_scale = impl_->view_scale /
            std::max(impl_->reference_view_scale, 1.0e-6F);
        QOpenGLPaintDevice overlay_device(QSize(framebuffer_width,framebuffer_height));
        overlay_device.setDevicePixelRatio(pixel_ratio);
        QPainter painter(&overlay_device);
        std::vector<DimensionTextLabel> dimension_texts;
        painter.setRenderHint(QPainter::Antialiasing);
        if(impl_->show_dimension_frame) {
            painter.setPen(QPen(QColor("#808080"),1,Qt::DashLine));
            const auto key=impl_->confirmed_candidate?kernel::ObjectEnvelopeKey{impl_->confirmed_candidate->owner_id,impl_->confirmed_candidate->instance_path}:kernel::ObjectEnvelopeKey{};
            const auto corners=(impl_->object_bounds.contains(key)&&impl_->object_bounds.at(key).valid?impl_->object_bounds.at(key):impl_->dimension_bounds).corners();
            for(unsigned i=0;i<8;++i)for(unsigned bit:{1U,2U,4U})if(!(i&bit))painter.drawLine(project(corners[i]),project(corners[i|bit]));
        }
        paint_normal_text(painter,impl_->mesh.edges,impl_->transient_edges,project,impl_->show_sketches,impl_->show_symbols);
        for(const auto& image:impl_->mesh.images) {
            QPolygonF target;for(const auto& p:image.corners)target<<project(p);
            paint_embedded_image(painter,image.data_base64,image.format,target);
            const auto matches=[&](const auto& c){return c && c->kind==CandidateKind::TemplateImage && c->owner_id==image.reference.owner_id && c->semantic_key==image.reference.semantic_key;};
            if(matches(impl_->confirmed_candidate)||matches(hovered_candidate())) {
                painter.setPen(QPen(matches(impl_->confirmed_candidate)?interaction::selected:interaction::hover,2));painter.setBrush(Qt::NoBrush);painter.drawPolygon(target);
            }
        }
        const auto reference_segment_polygon = [](const QPointF& first,
                const QPointF& second, double width) {
            const QLineF line(first, second);
            if (line.length() <= 1.0e-6) return QPolygonF{};
            const QPointF normal{-line.dy() / line.length() * width * 0.5,
                                 line.dx() / line.length() * width * 0.5};
            return QPolygonF{first + normal, second + normal,
                             second - normal, first - normal};
        };
        const auto draw_reference_segment = [&](const QPointF& first,
                const QPointF& second, const QColor& color, double width) {
            const auto polygon=reference_segment_polygon(first,second,width);
            if(polygon.isEmpty())return;
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(polygon);
            painter.restore();
        };
        const auto screen_infinite_line = [&](const QPointF& first,
                const QPointF& second) {
            return zima::viewer::screen_infinite_line(first, second,
                QSizeF(width(), height()));
        };
        for (const auto& indicator : impl_->operation_direction_indicators) {
            const QColor purple("#D05CFF");
            painter.setPen(QPen(purple, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(purple);
            const QPointF center = project(indicator.origin);
            std::vector<QPointF> path;
            if (!indicator.angular) {
                const QPointF projected_direction = project({
                    indicator.origin.x + indicator.direction.x,
                    indicator.origin.y + indicator.direction.y,
                    indicator.origin.z + indicator.direction.z}) - center;
                const double magnitude = std::hypot(
                    projected_direction.x(), projected_direction.y());
                if (magnitude > 1.0e-6) {
                    path = {center, center + projected_direction / magnitude * 62.0};
                }
            } else {
                const auto normalize = [](zima::kernel::Vec3 value) {
                    const double length = std::sqrt(value.x * value.x +
                        value.y * value.y + value.z * value.z);
                    return length > 1.0e-12
                        ? zima::kernel::Vec3{value.x / length, value.y / length,
                            value.z / length}
                        : zima::kernel::Vec3{};
                };
                const auto axis = normalize(indicator.direction);
                const auto radial_unit = normalize(indicator.radial);
                const double radial_length = std::sqrt(
                    indicator.radial.x * indicator.radial.x +
                    indicator.radial.y * indicator.radial.y +
                    indicator.radial.z * indicator.radial.z);
                const auto tangent_unit = normalize({
                    axis.y * radial_unit.z - axis.z * radial_unit.y,
                    axis.z * radial_unit.x - axis.x * radial_unit.z,
                    axis.x * radial_unit.y - axis.y * radial_unit.x});
                const double shown_angle = std::clamp(
                    std::abs(indicator.angle_degrees), 0.001, 360.0);
                constexpr int samples = 40;
                for (int sample = 0; sample <= samples; ++sample) {
                    const double radians = shown_angle *
                        static_cast<double>(sample) / samples *
                        std::numbers::pi / 180.0 *
                        (indicator.angle_degrees < 0.0 ? -1.0 : 1.0);
                    const zima::kernel::Vec3 point{
                        indicator.origin.x + radial_length *
                            (radial_unit.x * std::cos(radians) +
                             tangent_unit.x * std::sin(radians)),
                        indicator.origin.y + radial_length *
                            (radial_unit.y * std::cos(radians) +
                             tangent_unit.y * std::sin(radians)),
                        indicator.origin.z + radial_length *
                            (radial_unit.z * std::cos(radians) +
                             tangent_unit.z * std::sin(radians))};
                    path.push_back(project(point));
                }
            }
            for (std::size_t index = 1; index < path.size(); ++index)
                painter.drawLine(path[index - 1], path[index]);
            if (path.size() >= 2) {
                QPointF along = path.back() - path[path.size() - 2];
                const double magnitude = std::hypot(along.x(), along.y());
                if (magnitude > 1.0e-6) {
                    along /= magnitude;
                    const QPointF normal{-along.y(), along.x()};
                    const QPointF tip = path.back();
                    const QPointF base = tip - along * 11.0;
                    painter.drawPolygon(QPolygonF{
                        tip, base + normal * 4.0, base - normal * 4.0});
                }
            }
            painter.setBrush(Qt::NoBrush);
        }
        for (const auto& handle : impl_->extent_manipulators) {
            const double direction_length = std::sqrt(
                handle.direction.x * handle.direction.x +
                handle.direction.y * handle.direction.y +
                handle.direction.z * handle.direction.z);
            if (direction_length > 1.0e-12) {
                const zima::kernel::Vec3 direction{
                    handle.direction.x / direction_length,
                    handle.direction.y / direction_length,
                    handle.direction.z / direction_length};
                const zima::kernel::Vec3 endpoint{
                    handle.origin.x + direction.x * handle.length,
                    handle.origin.y + direction.y * handle.length,
                    handle.origin.z + direction.z * handle.length};
                const QPointF start = project(handle.origin);
                const QPointF finish = project(endpoint);
                const QColor purple("#D05CFF");
                const bool start_hot = impl_->hovered_extent_key ==
                        handle.start_key || impl_->dragged_extent_key ==
                        handle.start_key;
                const bool end_hot = impl_->hovered_extent_key ==
                        handle.end_key || impl_->dragged_extent_key ==
                        handle.end_key;
                if (!handle.point_only) {
                    painter.setPen(QPen(purple, 2.0));
                    painter.drawLine(start, finish);
                    QPointF along = finish - start;
                    const double screen_length = std::hypot(along.x(), along.y());
                    if (screen_length > 1.0e-6) {
                        along /= screen_length;
                        const QPointF normal{-along.y(), along.x()};
                        const QPointF tip = finish - along * 9.0;
                        const QPointF base = tip - along * 10.0;
                        painter.setBrush(purple);
                        painter.drawPolygon(QPolygonF{tip,
                            base + normal * 1.763269807,
                            base - normal * 1.763269807});
                    }
                }
                // Purple operation manipulators keep their semantic colour
                // even under the pointer. They are painted by QPainter above
                // the depth-tested model, so they remain visible through the
                // preview body just like the Plane offset handle.
                static_cast<void>(start_hot);
                static_cast<void>(end_hot);
                const QColor start_color = purple;
                painter.setPen(QPen(start_color, 1.0));
                painter.setBrush(start_color);
                draw_circular_marker(painter,start,start_color,6.0);
                if (!handle.point_only) {
                    painter.setPen(QPen(purple, 1.0));
                    painter.setBrush(purple);
                    draw_circular_marker(painter,finish,purple,6.0);
                }
                painter.setBrush(Qt::NoBrush);
            }
        }
        // Reference axes are background context for authored Sketch wires and previews.
        const auto draw_axes = [&] {
            if (!impl_->active_sketch_owner_id.empty()) painter.save();
            // Coincident document and Body axes must not cover the offered axis.
            for (const bool highlight_pass : {false, true}) {
                for(const auto* axes:{&impl_->mesh.axes,
                                      &impl_->transient_context_mesh.axes})
                for (const auto& axis : *axes) {
                const bool transient_context=axes==&impl_->transient_context_mesh.axes;
                if (!transient_context&&!impl_->origin_visible({axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path})) continue;
                    const bool origin = axis.reference.semantic_key.starts_with(
                        "origin:axis:");
                    const bool inspected = impl_->constraint_reference_edges.contains(EdgeKey{axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path});
                    if (!transient_context&&!inspected && ((origin && !impl_->show_origins && !axes_selectable &&
                            !impl_->editing_origin_visible) ||
                        (!origin && !impl_->show_axes && !axes_selectable))) continue;
                    const bool feature_axis = axis.reference.semantic_key == "axis" &&
                        (std::ranges::any_of(impl_->mesh.points,[&](const auto& point) {
                            return point.display_owner_id == axis.reference.owner_id &&
                                point.reference.owner_id == axis.reference.owner_id &&
                                point.reference.semantic_key == "point";
                        }) || std::ranges::any_of(impl_->transient_context_mesh.points,[&](const auto& point) {
                            return point.display_owner_id == axis.reference.owner_id &&
                                point.reference.owner_id == axis.reference.owner_id &&
                                point.reference.semantic_key == "point";
                        }));
                    const bool whole_feature_axis = highlighted && !origin &&
                        highlighted->kind == CandidateKind::Container &&
                        highlighted->semantic_key.empty() &&
                        highlighted->owner_id == axis.reference.owner_id;
                    const bool exact_highlight = highlighted &&
                        (whole_feature_axis ||
                         (feature_axis && highlighted->kind == CandidateKind::Container &&
                          highlighted->owner_id == axis.reference.owner_id &&
                          (highlighted->semantic_key.empty() || highlighted->semantic_key == "axis")) ||
                         ((highlighted->kind == CandidateKind::Axis ||
                           highlighted->kind == CandidateKind::SketchAxis) &&
                          highlighted->owner_id == axis.reference.owner_id &&
                          highlighted->semantic_key == axis.reference.semantic_key) ||
                         (highlighted->kind == CandidateKind::Container &&
                          highlighted->semantic_key == "axis" &&
                          highlighted->owner_id + ":entity" ==
                              axis.reference.owner_id) ||
                         // A ray landing exactly on the Axis container's own
                         // defining-point marker (see construction_viewer_mesh)
                         // resolves to a Vertex pick of that point -- or, under
                         // the default selection contract (Container-only, see
                         // MeshView::Impl::allowed_kinds), to the paired
                         // Container candidate with semantic_key=="point" that
                         // ordered_viewer_candidates derives from that same
                         // Vertex pick -- rather than to the Axis line itself.
                         // Both resolve with higher priority than Axis/Container
                         // "axis" in ordered_viewer_candidates. Without these
                         // two branches, confirming/hovering that point would
                         // highlight only the dot, leaving the line its plain
                         // presentation color even though the whole container
                         // is what got selected.
                         (highlighted->kind == CandidateKind::Vertex &&
                          highlighted->semantic_key == "point" &&
                          axis.reference.owner_id.ends_with(":entity") &&
                          highlighted->owner_id == axis.reference.owner_id.substr(
                              0, axis.reference.owner_id.size() -
                                  std::string_view(":entity").size()) + ":origin") ||
                         (highlighted->kind == CandidateKind::Container &&
                          highlighted->semantic_key == "point" &&
                          axis.reference.owner_id.ends_with(":entity") &&
                          highlighted->owner_id == axis.reference.owner_id.substr(
                              0, axis.reference.owner_id.size() -
                                  std::string_view(":entity").size())) ||
                         // Selecting/hovering the whole Origin also highlights
                         // its own X/Y/Z axis lines and labels, matching
                         // Python's unified selected/hovered-object check.
                         (origin && highlighted->kind == CandidateKind::Container &&
                          highlighted->semantic_key == "origin" &&
                          highlighted->owner_id == axis.reference.owner_id)) &&
                        highlighted->instance_path == axis.reference.instance_path;
                    if (exact_highlight != highlight_pass) continue;
                    // Match ONLY the precise per-entity key -- see the identical
                    // comment on the plane block above.
                    const bool referenced = !exact_highlight &&
                        impl_->constraint_reference_edges.contains(EdgeKey{
                            axis.reference.owner_id, axis.reference.semantic_key,
                            axis.reference.instance_path});
                    const bool creation_preview = !origin &&
                        impl_->feature_preview_owner_ids.contains(
                            axis.reference.owner_id);
                    const QColor color = exact_highlight
                        ? (impl_->confirmed_candidate ? interaction::selected
                                                      : interaction::hover)
                        : (referenced || creation_preview) ? interaction::selected
                        : axis.reference.semantic_key == "origin:axis:x"
                            ? QColor(232, 76, 61)
                        : axis.reference.semantic_key == "origin:axis:y"
                            ? QColor(46, 204, 112)
                        : axis.reference.semantic_key == "origin:axis:z"
                            ? QColor(51, 153, 219)
                            : QColor(150, 150, 150);
                    const bool sketch_axis = axis.reference.semantic_key ==
                            "sketch_axis:x" ||
                        axis.reference.semantic_key == "sketch_axis:y";
                    const QColor presentation_color = !origin &&
                            !exact_highlight && !referenced && !creation_preview
                        ? interaction::axis : color;
                    const double axis_width=((exact_highlight && impl_->confirmed_candidate) || referenced) ? interaction::selected_wire_width : origin ? 2.0 : 1.5;
                    painter.setPen(QPen(presentation_color, axis_width,
                        Qt::SolidLine));
                    const double display_length=kernel::axis_display_length(axis);
                    const double first = origin ? 0.0 : -display_length * 0.5;
                    const double second = origin
                        ? axis.display_length * reference_scale
                        : display_length * 0.5;
                    const QPointF start = project({axis.point.x + axis.direction.x * first,
                                                    axis.point.y + axis.direction.y * first,
                                                    axis.point.z + axis.direction.z * first});
                    const QPointF end = project({axis.point.x + axis.direction.x * second,
                                                  axis.point.y + axis.direction.y * second,
                                                  axis.point.z + axis.direction.z * second});
                    if (origin) {
                        draw_reference_segment(start, end, presentation_color, 2.0);
                    } else {
                        // A plain painter.drawLine() call silently produces no
                        // visible output in this overlay pass (leftover GL state
                        // from the solid-body shader/VAO breaks QPainter's native
                        // line primitive), whereas the filled-polygon helper
                        // already used for the Origin's own axes renders
                        // correctly.  Draw the construction axis as a dash-dot
                        // pattern of small filled quads through the same helper
                        // so it is actually visible, matching the persisted
                        // origin axes rendering technique.
                        const QLineF full_line = sketch_axis
                            ? screen_infinite_line(start, end) : QLineF(start, end);
                        const double total_length = full_line.length();
                        if (total_length > 1.0e-6) {
                            const QPointF pattern_start = full_line.p1();
                            const QPointF unit = (full_line.p2() - full_line.p1()) /
                                total_length;
                            // Keep the Python Sketcher-like dash-dot rhythm legible
                            // at normal DPI. The former 10/5/2/5 pixel pattern was
                            // visually compressed and almost looked continuous.
                            constexpr double dash = 20.0;
                            constexpr double gap = 10.0;
                            constexpr double dot = 3.0;
                            constexpr double pattern = dash + gap + dot + gap;
                            double offset = 0.0;
                            while (offset < total_length) {
                                const double dash_end = std::min(offset + dash, total_length);
                                draw_reference_segment(pattern_start + unit * offset,
                                    pattern_start + unit * dash_end,
                                    presentation_color, axis_width);
                                const double dot_start = std::min(offset + dash + gap, total_length);
                                const double dot_end = std::min(dot_start + dot, total_length);
                                if (dot_end > dot_start) {
                                    draw_reference_segment(pattern_start + unit * dot_start,
                                        pattern_start + unit * dot_end,
                                        presentation_color, axis_width);
                                }
                                offset += pattern;
                            }
                        }
                        // Mark the axis's own origin point with a small filled
                        // dot in the same hover/select/reference-aware color as
                        // the axis line itself, matching the always-visible dot
                        // used for a standalone Point container -- this is the
                        // only way to see where a construction Axis actually
                        // starts (its dimension/length is measured from here),
                        // and it must stay visible together with the length
                        // dimension annotation, not just on hover/selection.
                        // Mark the axis's own origin point with a small filled
                        // dot, but only while this axis is hovered or selected
                        // (exact_highlight) -- matching a solid body's own
                        // origin-indicator convention -- not permanently, so it
                        // does not clutter idle/default rendering.
                        if (exact_highlight && !feature_axis && !whole_feature_axis) {
                            draw_circular_marker(
                                painter, project(axis.point), presentation_color);
                        }
                    }
                    std::string axis_source_storage;
                    if (kernel::construction_source_key(axis.reference.semantic_key,axis_source_storage).starts_with("centerline:from:centroid:")) {
                        // Profile axes extend 1 mm beyond each actual endpoint.
                        painter.setPen(QPen(presentation_color));
                        for(const double contact:{first + kernel::axis_display_overhang,
                                second - kernel::axis_display_overhang}) {
                            const auto anchor = project({axis.point.x + axis.direction.x * contact,
                                axis.point.y + axis.direction.y * contact,
                                axis.point.z + axis.direction.z * contact});
                            painter.drawText(anchor + QPointF(8.0, -6.0), QStringLiteral("T"));
                        }
                    }
                    if (origin) {
                        const QLineF line(start, end);
                        if (line.length() > 1.0) {
                            const QPointF unit = (line.p2() - line.p1()) / line.length();
                            const QPointF normal{-unit.y(), unit.x()};
                            painter.setPen(Qt::NoPen);
                            painter.setBrush(color);
                            painter.drawPolygon(QPolygonF{
                                end, end - unit * 20.0 + normal * 3.526,
                                end - unit * 20.0 - normal * 3.526});
                        }
                        auto axis_font = painter.font();
                        axis_font.setBold(true);
                        axis_font.setPointSizeF(std::max(9.0, axis_font.pointSizeF()));
                        painter.setFont(axis_font);
                        painter.setPen(QPen(color, 1.5));
                        painter.drawText(end + QPointF(5.0, -4.0),
                            axis.label.empty()
                                ? QString::fromStdString(axis.reference.semantic_key.substr(
                                      std::string("origin:axis:").size())).toUpper()
                                : QString::fromStdString(axis.label).toUpper());
                    }
                }
            }
            if (!impl_->active_sketch_owner_id.empty()) painter.restore();
        };
        if (!impl_->active_sketch_owner_id.empty() && axes_visible) draw_axes();
        // Read-only reference markers belong below authored Sketch geometry.
        if(points_visible&&impl_->show_sketches)for(const auto& point:impl_->mesh.points) {
            if(!point.reference.semantic_key.starts_with("external_point:")||
               point.reference.semantic_key=="external_point:sketch_origin"||
               !impl_->helper_point_visible(point)||
               !impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path}))continue;
            const QColor color = point.reference.semantic_key.ends_with(
                ":broken") ? QColor(179, 74, 60) : QColor(145, 105, 72);
            painter.setPen(QPen(color, 1.8));
            const QPointF center = project(point.position);
            painter.drawLine(center + QPointF(-4.0, -4.0),
                             center + QPointF(4.0, 4.0));
            painter.drawLine(center + QPointF(-4.0, 4.0),
                             center + QPointF(4.0, -4.0));
        }
        if (sketch_geometry_visible) {
            qulonglong painted_transient_sketch_edges{};
            // All references first; native construction/profile strokes retain
            // their existing relative order above read-only reference strokes.
            for (const bool reference_pass : {true, false})
            for (const bool construction_pass : {false, true})
            for (const auto* sketch_edges : {&impl_->mesh.edges,
                                             &impl_->transient_context_mesh.edges})
            for (const auto& edge : *sketch_edges) {
                if (!is_sketch_wire_edge(edge)) continue;
                if (edge.construction != construction_pass) continue;
                if(!impl_->sketch_wire_visible(edge))continue;
                if(edge.filled_text)continue; // Filled annotation; selection still draws its exact wire.
                const bool text = edge.reference.semantic_key.starts_with("text:");
                const bool external = edge.reference.semantic_key.starts_with(
                        "external_edge:") ||
                    edge.reference.semantic_key.starts_with("external_axis:");
                const bool external_face = edge.reference.semantic_key.starts_with(
                    "external_face:");
                if ((external || external_face) != reference_pass) continue;
                if (sketch_edges == &impl_->transient_context_mesh.edges)
                    ++painted_transient_sketch_edges;
                const bool ordinary_sketch_highlight=ordinary_preview_candidate&&!external&&!external_face&&
                    candidate_recolors_wire_edge(*ordinary_preview_candidate,edge);
                const QColor text_color = edge.reference.semantic_key.ends_with(":red") ? QColor(255,0,0)
                    : edge.reference.semantic_key.ends_with(":yellow")
                    ? QColor(245, 205, 80)
                    : edge.reference.semantic_key.ends_with(":white")
                        ? theme.foreground : QColor(77, 216, 17);
                const QColor external_color = edge.reference.semantic_key.ends_with(
                    ":broken") ? QColor(179, 74, 60) : QColor(145, 105, 72);
                QPen edge_pen = text ? QPen(text_color, 1.8)
                    : (external || external_face)
                        ? QPen(external_color, 1.5, Qt::DashLine)
                    : edge.construction
                        ? QPen(edge.dash_dot ? interaction::axis : interaction::auxiliary, 1.5, Qt::DashLine)
                        : QPen(theme.foreground,
                            impl_->geometry_editing_presentation||!impl_->active_sketch_owner_id.empty()?1.8:1.0);
                if (!edge.color.empty()) {const QColor color(QString::fromStdString(edge.color));edge_pen.setColor(color==QColor(Qt::white)?theme.foreground:color);}
                if (edge.construction && !external && !external_face &&
                    edge.reference.owner_id==impl_->active_sketch_owner_id &&
                    edge.reference.instance_path==impl_->editing_curve_path)
                    edge_pen.setColor(interaction::sketch_construction);
                const bool candidate_match = highlighted &&
                    candidate_recolors_wire_edge(*highlighted, edge);
                const auto key = edge_key(edge.reference);
                const bool selected =
                    (candidate_match && impl_->confirmed_candidate) ||
                    impl_->edge_treatment_selection_edges.contains(key) ||
                    impl_->feature_selected_edges.contains(key) ||
                    impl_->constraint_reference_edges.contains(key) ||
                    impl_->assembly_reference_edges.contains(key) ||
                    impl_->selected_container_content_ids.contains(
                        edge.reference.owner_id) ||
                    impl_->constraint_reference_owner_ids.contains(
                        edge.reference.owner_id);
                const bool hovered =
                    (candidate_match && !impl_->confirmed_candidate) ||
                    impl_->feature_hover_edges.contains(key) ||
                    impl_->object_overlay_main_edge_keys.contains(key);
                const bool preview =
                    impl_->feature_preview_owner_ids.contains(
                        edge.reference.owner_id);
                if (ordinary_sketch_highlight) {
                    edge_pen=QPen(interaction::selected,interaction::curve_highlight_wire_width,
                        Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
                } else if (selected) {
                    edge_pen.setColor(interaction::selected);
                    if (!edge.construction && !external && !external_face) edge_pen.setWidthF(interaction::selected_wire_width);
                } else if (hovered) {
                    edge_pen.setColor(interaction::hover);
                    if (!edge.construction && !external && !external_face) edge_pen.setWidthF(1.8);
                } else if (preview && !(edge.construction && !external && !external_face &&
                    edge.color == interaction::sketch_construction.name().toStdString())) {
                    // Editing-profile annotations retain their explicit green;
                    // the feature's ordinary preview wires remain cyan.
                    edge_pen.setColor(interaction::selected);
                }
                if (edge.construction && !edge.dash_dot) {
                    // Qt's default dash pattern is dense enough that construction
                    // geometry reads as a solid line at ordinary sketch zoom levels.
                    edge_pen.setStyle(Qt::CustomDashLine);
                    edge_pen.setDashPattern({10.0, 7.0});
                }
                if (edge.dash_dot) {
                    edge_pen.setStyle(Qt::CustomDashLine);
                    edge_pen.setDashPattern({9.0, 4.0, 2.0, 4.0});
                }
                painter.setPen(edge_pen);
                if (edge.infinite && edge.points.size() >= 2) {
                    painter.drawLine(screen_infinite_line(
                        project(edge.points.front()), project(edge.points.back())));
                    continue;
                }
                if (!edge.points.empty()) {
                    // Stroke the complete sampled curve as one path. Calling
                    // drawLine for every chord restarts a custom dash pattern
                    // on every chord, which makes construction circles/arcs/
                    // ellipses look almost solid while a one-chord segment
                    // has the intended spacing.
                    const auto overhang=edge.construction?kernel::axis_curve_overhang(edge):std::nullopt;
                    QPainterPath path(project(overhang?(*overhang)[0]:edge.points.front()));
                    if(overhang)path.lineTo(project(edge.points.front()));
                    for (std::size_t index = 1;
                         index < edge.points.size(); ++index) {
                        path.lineTo(project(edge.points[index]));
                    }
                    if(overhang)path.lineTo(project((*overhang)[1]));
                    painter.drawPath(path);
                }
            }
            if (property("paintedTransientSketchEdgeCount").toULongLong() !=
                painted_transient_sketch_edges)
                setProperty("paintedTransientSketchEdgeCount",
                    painted_transient_sketch_edges);
        } else {
            if (property("paintedTransientSketchEdgeCount").toULongLong() != 0)
                setProperty("paintedTransientSketchEdgeCount", qulonglong{});
        }
        if (curve3d_geometry_visible || axes_visible) {
            // 3D Curves share Sketch's screen-space stroke contract. Their
            // persisted segment paths remain the single picking geometry;
            // QPainter only presents those same samples at a stable 1.8 px
            // width on every OpenGL driver.
            for (const auto& edge : impl_->mesh.edges) {
                std::string source_key;
                const auto datum_key=kernel::construction_source_key(edge.reference.semantic_key,source_key);
                const bool centerline=datum_key.starts_with("centerline:from:");
                const bool editing_curve = !impl_->editing_curve_owner.empty() &&
                    edge.display_owner_id == impl_->editing_curve_owner &&
                    edge.reference.instance_path == impl_->editing_curve_path;
                const bool candidate_match = highlighted &&
                    candidate_recolors_wire_edge(*highlighted, edge);
                if(centerline ? !(impl_->show_axes || axes_selectable || candidate_match) :
                    (!curve3d_geometry_visible || !is_curve3d_edge(edge.reference.semantic_key)))continue;
                const auto key = edge_key(edge.reference);
                const bool referenced =
                    impl_->constraint_reference_edges.contains(key) ||
                    impl_->assembly_reference_edges.contains(key) ||
                    impl_->selected_container_content_ids.contains(
                        edge.reference.owner_id);
                const bool preview =
                    impl_->feature_preview_owner_ids.contains(
                        edge.reference.owner_id);
                const QColor color = candidate_match
                    ? impl_->confirmed_candidate
                        ? interaction::selected
                        : interaction::hover
                    : (referenced || preview)
                        ? interaction::selected
                        : centerline ? interaction::axis : theme.foreground;
                QPen curve_pen(color, centerline ? 1.5 : candidate_match||referenced||preview||editing_curve?interaction::curve_highlight_wire_width:1.0,
                    Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
                if (centerline) {
                    // Match the ordinary axes: 20 px dash, 10 px gaps, 3 px dot.
                    // QPen dash lengths are expressed in multiples of pen width.
                    curve_pen.setDashPattern({20.0 / 1.5, 10.0 / 1.5,
                        3.0 / 1.5, 10.0 / 1.5});
                }
                painter.setPen(curve_pen);
                if (edge.points.empty()) continue;
                const auto overhang=centerline?kernel::axis_curve_overhang(edge):std::nullopt;
                QPainterPath path(project(overhang?(*overhang)[0]:edge.points.front()));
                if(overhang)path.lineTo(project(edge.points.front()));
                for (std::size_t index = 1;
                     index < edge.points.size(); ++index) {
                    path.lineTo(project(edge.points[index]));
                }
                if(overhang)path.lineTo(project((*overhang)[1]));
                painter.drawPath(path);
                if (datum_key.starts_with("centerline:from:centroid:")) {
                    painter.setPen(QPen(color));
                    painter.drawText(project(edge.points.front()) + QPointF(8.0, -6.0), QStringLiteral("T"));
                    if(edge.points.back()!=edge.points.front())
                        painter.drawText(project(edge.points.back()) + QPointF(8.0, -6.0), QStringLiteral("T"));
                }
            }
        }
        if (!impl_->mesh.constraint_markers.empty()) {
            auto marker_font = painter.font();
            marker_font.setBold(true);
            painter.setFont(marker_font);
            painter.setPen(QPen(QColor("#7CFF6B"), 2.0));
            std::map<std::pair<int, int>, int> occupied_slots;
            for (std::size_t marker_index = 0;
                 marker_index < impl_->mesh.constraint_markers.size(); ++marker_index) {
                const auto& marker = impl_->mesh.constraint_markers[marker_index];
                const bool selected = highlighted &&
                    highlighted->kind == CandidateKind::SketchConstraint &&
                    highlighted->geometry_index == marker_index;
                painter.setPen(QPen(selected
                    ? (impl_->confirmed_candidate ? interaction::selected
                                                  : interaction::hover)
                    : QColor("#7CFF6B"), 2.0));
                const QPointF anchor = project(marker.position);
                const auto key = std::pair{
                    static_cast<int>(std::lround(anchor.x())),
                    static_cast<int>(std::lround(anchor.y()))};
                const int slot = occupied_slots[key]++;
                painter.drawText(anchor + QPointF(7.0 + slot * 16.0, -7.0),
                    QString::fromStdString(sketch_hv_label(marker.label,marker.reference.owner_id,marker.reference.instance_path)));
            }
        }
        if (planes_visible) {
            painter.setPen(QPen(interaction::axis, 1.5));
            for (const auto* plane_edge : plane_edges) {
                const auto& edge = *plane_edge;
                const bool transient_context=std::ranges::any_of(
                    impl_->transient_context_mesh.edges,
                    [&](const auto& candidate){return &candidate==plane_edge;});
                const bool origin = edge.reference.semantic_key.starts_with(
                    "origin:plane:");
                if (edge.reference.semantic_key != "border" && edge.reference.semantic_key != "plane:end" && !origin) continue;
                const bool creation_preview = !origin &&
                    impl_->feature_preview_owner_ids.contains(
                        edge.reference.owner_id);
                if (!transient_context&&((origin && !impl_->show_origins && !planes_selectable &&
                        !impl_->editing_origin_visible) ||
                    (!origin && !impl_->show_planes && !planes_selectable &&
                        !creation_preview))) continue;
                const bool exact_highlight = highlighted &&
                    ((highlighted->kind == CandidateKind::Container &&
                      !edge.display_owner_id.empty() &&
                      highlighted->owner_id == edge.display_owner_id &&
                      highlighted->semantic_key.empty()) ||
                     (highlighted->kind == CandidateKind::Plane &&
                      highlighted->owner_id == edge.reference.owner_id &&
                      (highlighted->semantic_key == edge.reference.semantic_key ||
                       (highlighted->semantic_key == "plane" &&
                        edge.reference.semantic_key == "border"))) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "plane" &&
                      highlighted->owner_id + ":entity" ==
                          edge.reference.owner_id) ||
                     // A ray landing exactly on the Plane container's own
                     // defining-point marker (see construction_viewer_mesh)
                     // resolves to a Vertex pick of that point -- or, under
                     // the default selection contract (Container-only, see
                     // MeshView::Impl::allowed_kinds), to the paired
                     // Container candidate with semantic_key=="point" that
                     // ordered_viewer_candidates derives from that same
                     // Vertex pick -- rather than to the border edges
                     // themselves. Both resolve with higher priority than
                     // Face/Container "plane" in ordered_viewer_candidates.
                     // Without these two branches, confirming/hovering that
                     // point would highlight only the dot, leaving the
                     // border its plain presentation color even though the
                     // whole container is what got selected.
                     (highlighted->kind == CandidateKind::Vertex &&
                      highlighted->semantic_key == "point" &&
                      edge.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == edge.reference.owner_id.substr(
                          0, edge.reference.owner_id.size() -
                              std::string_view(":entity").size()) + ":origin") ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "point" &&
                      edge.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == edge.reference.owner_id.substr(
                          0, edge.reference.owner_id.size() -
                              std::string_view(":entity").size())) ||
                     // Selecting/hovering the whole Origin (e.g. clicking it
                     // in the tree) highlights every one of its own
                     // FRONT/TOP/... plane labels too, matching Python's
                     // unified _selected_object_id highlight check.
                     (origin && highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "origin" &&
                      highlighted->owner_id == edge.reference.owner_id)) &&
                    highlighted->instance_path == edge.reference.instance_path;
                // Clicking a populated reference row in a placement dialog
                // (Umístění kontejneru) highlights the referenced plane
                // here too. Match ONLY the precise per-entity key (owner_id
                // + semantic_key + instance_path) -- e.g. exactly
                // "origin:plane:xz", never every sibling plane of the same
                // Origin/body owner_id, which is why owner_id alone is
                // never used for this check.
                const bool referenced = !exact_highlight &&
                    impl_->constraint_reference_edges.contains(edge_key(edge.reference));
                const QColor plane_color = exact_highlight
                    ? (impl_->confirmed_candidate ? interaction::selected
                                                  : interaction::hover)
                    : (referenced || creation_preview) ? interaction::selected
                    : interaction::axis;
                zima::kernel::Vec3 center;
                const std::size_t corner_count = edge.points.size() > 1
                    ? edge.points.size() - 1 : edge.points.size();
                for (std::size_t index = 0; index < corner_count; ++index) {
                    const auto& point = edge.points[index];
                    center.x += point.x; center.y += point.y; center.z += point.z;
                }
                if (corner_count != 0) {
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                }
                const double plane_scale = is_screen_constant_plane(
                    edge.reference.semantic_key) ? reference_scale : 1.0;
                const auto plane_point = [&](const auto& point) {
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * plane_scale,
                        center.y + (point.y - center.y) * plane_scale,
                        center.z + (point.z - center.z) * plane_scale};
                };
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    draw_reference_segment(project(plane_point(edge.points[index - 1])),
                        project(plane_point(edge.points[index])),
                        plane_color,
                        ((exact_highlight && impl_->confirmed_candidate) || referenced) ? interaction::selected_wire_width : origin ? 1.5 : 1.4);
                }
                if (origin && !edge.points.empty()) {
                    painter.setPen(QPen(plane_color, 1.5));
                    const auto key = QString::fromStdString(
                        edge.reference.semantic_key.substr(
                            std::string("origin:plane:").size())).toUpper();
                    painter.drawText(project(plane_point(edge.points.front())) +
                        QPointF(6.0, -5.0), key);
                }
            }
        }
        for(const auto& edge:impl_->mesh.edges)if(edge.overlay&&edge.reference.semantic_key=="section:sketch") {
            const bool selected=highlighted&&highlighted->kind==CandidateKind::Container&&highlighted->owner_id==edge.reference.owner_id&&highlighted->instance_path==edge.reference.instance_path;
            painter.setPen(QPen(selected?(impl_->confirmed_candidate?interaction::selected:interaction::hover):
                edge.color.empty()?interaction::axis:QColor(QString::fromStdString(edge.color)),selected?1.8:edge.color=="#AD6E2E"?1.0:2.0));
            for(std::size_t i=1;i<edge.points.size();++i)painter.drawLine(project(edge.points[i-1]),project(edge.points[i]));
        }
        if (!impl_->transient_edges.empty()) {
            // Pending wire uses the shared selection colour. Only symbolic through-all
            // terminal outlines are dashed; geometry and picking are unchanged.
            painter.setPen(QPen(interaction::selected, 2.0, Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            for (const auto& edge : impl_->transient_edges) {
                const bool inference_reference =
                    edge.reference.semantic_key == "inference:reference";
                painter.setPen(QPen(inference_reference
                        ? interaction::hover : !edge.color.empty()?QColor(QString::fromStdString(edge.color)):interaction::selected,
                    2.0, edge.preview_terminal_dashed?Qt::DashLine:Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                if (edge.preview_terminal_dashed && !edge.points.empty()) {
                    // Continue the dash pattern across the sampled terminal.
                    // Per-chord drawLine restarts it and fills every gap on
                    // finely sampled circles/arcs, making them appear solid.
                    QPainterPath path(project(edge.points.front()));
                    for (std::size_t index = 1; index < edge.points.size(); ++index)
                        path.lineTo(project(edge.points[index]));
                    painter.drawPath(path);
                } else {
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
                    }
                }
            }
        }
        for (const auto& point : impl_->transient_points) {
            draw_circular_marker(painter, project(point), interaction::hover);
        }
        for(const auto& point:impl_->transient_model_points) {
            const auto& ref=point.reference;
            const bool inspected=impl_->constraint_reference_edges.contains(EdgeKey{ref.owner_id,ref.semantic_key,ref.instance_path});
            const auto color=inspected?interaction::selected:
                !ref.semantic_key.starts_with("point:")?interaction::axis:theme.foreground;
            draw_circular_marker(painter,project(point.position),color);
        }
        if (impl_->sketch_cursor) {
            const QColor cursor_color = impl_->sketch_cursor_snapped
                ? interaction::hover : theme.foreground;
            const QPointF cursor = project(*impl_->sketch_cursor);
            draw_circular_marker(painter, cursor, cursor_color);
            if (!impl_->sketch_cursor_label.empty()) {
                painter.setPen(QPen(cursor_color, 1.5));
                painter.drawText(cursor + QPointF(8.0, -8.0),
                    QString::fromStdString(sketch_hv_label(impl_->sketch_cursor_label,impl_->active_sketch_owner_id,impl_->editing_curve_path)));
            }
        }
        if (!impl_->transient_labels.empty()) {
            painter.setPen(QPen(interaction::hover, 1.5));
            for (const auto& [point, label] : impl_->transient_labels) {
                painter.drawText(project(point) + QPointF(8.0, -8.0),
                    QString::fromStdString(sketch_hv_label(label,impl_->active_sketch_owner_id,impl_->editing_curve_path)));
            }
        }
        if (dimensions_visible && impl_->show_dimensions) {
            painter.setFont(font());
            const std::size_t persisted_dimension_count =
                impl_->mesh.dimensions.size();
            const std::size_t displayed_dimension_count =
                persisted_dimension_count + impl_->transient_dimensions.size();
            for (std::size_t index = 0;
                 index < displayed_dimension_count; ++index) {
                const auto& dimension = index < persisted_dimension_count
                    ? impl_->mesh.dimensions[index]
                    : impl_->transient_dimensions[
                        index - persisted_dimension_count];
                if(impl_->dimension_visibility_filter&&!impl_->dimension_visibility_filter(dimension))continue;
                const bool selected = highlighted &&
                    highlighted->kind == CandidateKind::Dimension &&
                    highlighted->geometry_index == index;
                const bool locked=dimension_lock_query_?dimension_lock_query_(dimension.reference).value_or(dimension.locked):dimension.locked;
                const QColor idle_color = !dimension.driving
                    ? interaction::axis  // measured; same as Sketch axes
                    : locked
                        ? QColor(0, 0, 0)  // locked dimension value
                        : QColor(245, 205, 80);  // editable driver
                const QColor color = selected
                    ? (impl_->confirmed_candidate ? interaction::selected
                                                  : interaction::hover)
                    : idle_color;
                // Selection is represented by colour only. Drawing the same
                // dimension thicker made Tree selection look like several
                // dimension graphics stacked on top of one another.
                painter.setPen(QPen(color, 1.5));
                painter.setBrush(color);
                const auto text=dimension_label_text(dimension);
                const auto layout=dimension_text_presentation(dimension,project,font(),text,.5*logicalDpiX()/25.4,1.5,10,3,false,1.5*logicalDpiX()/25.4,impl_->active_sketch_owner_id.empty());
                if(!layout.valid)continue;
                if (dimension.rotation_handle) {
                    const QColor purple("#D05CFF");
                    painter.setPen(QPen(purple, 2.0));
                    for (const auto& curve : layout.curves) painter.drawPolyline(curve);
                    draw_circular_marker(painter, layout.handles[0], purple, 6.0);
                    dimension_texts.push_back({text,layout.text_baseline,0,painter.font(),purple});
                    painter.setBrush(Qt::NoBrush);
                    continue;
                }
                for(const auto& curve:layout.curves)painter.drawPolyline(curve);
                for(const auto& [tip,direction]:layout.arrows)painter.drawPolygon(annotation_arrow(tip,direction,10));
                dimension_texts.push_back({text,layout.text_baseline,layout.text_angle,painter.font(),color,kernel::dimension_is_basic(dimension)});
                if(impl_->dimension_relations.contains({dimension.reference.owner_id,dimension.reference.semantic_key}))
                    impl_->relation_marks->labels.push_back(dimension_texts.back());
                painter.setBrush(Qt::NoBrush);
            }
        }
        if (impl_->active_sketch_owner_id.empty() && axes_visible) draw_axes();
        // Non-origin construction Point markers are drawn AFTER the plane
        // and axis overlays (rather than before them, as previously) so an
        // overlapping origin/construction axis line can never paint over
        // and hide a Point container's own marker -- the marker must
        // always stay on top, matching how the Origin's own point (drawn
        // even later, right below) is never hidden either.
        std::vector<QPointF> ordinary_sketch_markers;
        if (points_visible) {
            for(const auto* points:{&impl_->mesh.points,
                                    &impl_->transient_context_mesh.points})
            for (const auto& point : *points) {
                const bool transient_context=points==&impl_->transient_context_mesh.points;
                if(!transient_context&&!impl_->helper_point_visible(point))continue;
                if (!transient_context&&!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                // In an inactive Sketch this point is deliberately visible
                // only together with the whole-container hover/selection
                // overlay below.  Construction/profile points retain their
                // normal visibility rules.
                if (point.reference.semantic_key ==
                        "sketch:origin-marker" ||
                    (point.reference.semantic_key ==
                        "container:origin-marker" && !point.always_visible && point.label.empty())) continue;
                const bool external = point.reference.semantic_key.starts_with(
                        "external_point:") &&
                    point.reference.semantic_key !=
                        "external_point:sketch_origin";
                const bool origin = point.reference.semantic_key == "origin:point";
                const bool point_container_candidate =
                    point_containers_selectable &&
                    point.reference.semantic_key == "point";
                if (origin) continue;
                if ((origin && !impl_->show_origins && !points_selectable) ||
                    (!origin && !external && !point.always_visible &&
                     !impl_->show_points && (point.label.empty() || point.display_owner_id!=point.reference.owner_id) &&
                     !points_selectable && !point_container_candidate) ||
                    (external && !impl_->show_sketches)) continue;
                if (external) {
                    continue; // Reference markers were painted below native Sketch wires.
                } else {
                    const bool replaced_by_active_point_manipulator =
                        std::any_of(impl_->extent_manipulators.begin(),
                            impl_->extent_manipulators.end(), [&](const auto& handle) {
                                return handle.point_only &&
                                    std::abs(point.position.x - handle.origin.x) <= 1.0e-9 &&
                                    std::abs(point.position.y - handle.origin.y) <= 1.0e-9 &&
                                    std::abs(point.position.z - handle.origin.z) <= 1.0e-9;
                            });
                    const bool plane_offset_preview =
                        point.reference.semantic_key ==
                            "preview:plane-offset-point";
                    const bool creation_preview = transient_context ||
                        impl_->feature_preview_owner_ids.contains(
                            point.reference.owner_id);
                    const auto parent_axis=kernel::axis_endpoint_parent_key(point.reference.semantic_key);
                    const auto axis_candidate=[&](const auto& candidate) {
                        return candidate&&!parent_axis.empty()&&
                            (candidate->kind==CandidateKind::Axis||candidate->kind==CandidateKind::Edge)&&
                            candidate->owner_id==point.reference.owner_id&&candidate->semantic_key==parent_axis&&
                            candidate->instance_path==point.reference.instance_path;
                    };
                    const bool ordinary_sketch_point=ordinary_preview_candidate&&
                        ordinary_sketch_point_owners.contains(point.reference.owner_id)&&
                        ordinary_preview_candidate->instance_path==point.reference.instance_path&&
                        point.reference.semantic_key.starts_with("point:");
                    const bool selected = (ordinary_sketch_point&&impl_->confirmed_candidate) || point.reference.owner_id ==
                            impl_->selected_container_origin_id ||
                        axis_candidate(impl_->confirmed_candidate) ||
                        (!point.display_owner_id.empty() && impl_->confirmed_candidate &&
                         impl_->confirmed_candidate->kind == CandidateKind::Container &&
                         impl_->confirmed_candidate->owner_id == point.display_owner_id &&
                         impl_->confirmed_candidate->instance_path == point.reference.instance_path) ||
                        (impl_->confirmed_candidate &&
                         (impl_->confirmed_candidate->kind == CandidateKind::Vertex ||
                          impl_->confirmed_candidate->kind ==
                              CandidateKind::SketchPoint ||
                          impl_->confirmed_candidate->kind ==
                              CandidateKind::SketchExternalReference) &&
                         impl_->confirmed_candidate->owner_id ==
                            point.reference.owner_id &&
                         impl_->confirmed_candidate->semantic_key ==
                            point.reference.semantic_key &&
                         impl_->confirmed_candidate->instance_path ==
                            point.reference.instance_path);
                    const bool hovered = (ordinary_sketch_point&&!impl_->confirmed_candidate) || (!impl_->confirmed_candidate && highlighted &&
                        (axis_candidate(highlighted) || (!point.display_owner_id.empty() &&
                          highlighted->kind == CandidateKind::Container &&
                          highlighted->owner_id == point.display_owner_id) ||
                         (highlighted->kind == CandidateKind::Container &&
                          (highlighted->semantic_key == "point" ||
                           highlighted->semantic_key == "axis" ||
                           highlighted->semantic_key == "plane") &&
                          point.reference.owner_id ==
                            highlighted->owner_id + ":origin") ||
                         ((highlighted->kind == CandidateKind::Vertex ||
                           highlighted->kind == CandidateKind::SketchPoint ||
                           highlighted->kind ==
                               CandidateKind::SketchExternalReference) &&
                          highlighted->semantic_key == point.reference.semantic_key &&
                          highlighted->owner_id == point.reference.owner_id)) &&
                        point.reference.instance_path == highlighted->instance_path);
                    // Match ONLY the precise per-entity key -- see the
                    // identical comment on the plane block above.
                    const bool referenced = !selected && !hovered &&
                        (impl_->constraint_reference_edges.contains(EdgeKey{
                            point.reference.owner_id, point.reference.semantic_key,
                            point.reference.instance_path}) || (!parent_axis.empty()&&
                         impl_->constraint_reference_edges.contains(EdgeKey{
                             point.reference.owner_id,parent_axis,point.reference.instance_path})));
                    // An Axis/Plane container's own marker
                    // (always_visible=false) stays fully invisible in the
                    // ordinary state -- the Axis line / Plane border is
                    // enough on its own; the dot only appears once one of
                    // the states above gives it a meaningful color.
                    const bool hidden_marker = !point.always_visible && !selected &&
                        !hovered && !referenced && !creation_preview;
                    const bool feature_label = (point.reference.semantic_key == "point" ||
                        point.reference.semantic_key == "container:origin-marker") &&
                        point.display_owner_id == point.reference.owner_id;
                    if (hidden_marker && !feature_label) continue;
                    // A live work-plane preview is one visual object: its
                    // rectangular border and its offset-origin point use the
                    // same cyan preview colour in Plane, Sketch, Extrusion
                    // and Revolution properties. The old purple exception
                    // made the point look unrelated to its plane.
                    // Sketcher points use the same interaction language as
                    // the rest of the Viewer: white while idle and azure
                    // while offered or confirmed (including a drag).
                    // Purple remains reserved for the separate feature and
                    // plane manipulators drawn elsewhere.
                    // State-owned datums keep the presentation of their
                    // authored point type through every persisted parent.
                    std::string parent_marker_key;
                    const auto state_marker_key = kernel::solid_state_source_key(
                        point.reference.semantic_key, parent_marker_key);
                    std::string construction_marker_key;
                    const auto marker_key = kernel::construction_source_key(
                        state_marker_key, construction_marker_key);
                    const QColor marker_color = plane_offset_preview
                        ? interaction::selected
                        : selected
                        ? interaction::selected
                        : hovered ? interaction::hover
                        : (referenced || creation_preview)
                            ? interaction::selected
                        : marker_key == "point" && !point.display_owner_id.empty() &&
                            impl_->curve3d_point_owners.contains({point.display_owner_id,point.reference.instance_path})
                            ? theme.foreground
                        : marker_key ==
                                "external_point:sketch_origin"
                            ? QColor(0, 0, 0)
                        : marker_key.starts_with("point:")
                            ? marker_key.starts_with("point:from:")||impl_->passive_sketch_axis_point(point)
                                ? interaction::axis : theme.foreground
                        : marker_key.starts_with(
                                "corner_radius_handle:")
                            ? QColor(255, 255, 255)
                        : marker_key == "point" || marker_key == "container:origin-marker" ||
                            marker_key == "axis:start" || marker_key == "axis:end" ||
                            (marker_key.starts_with("sweep:path-point:") || marker_key.starts_with("profile:path-point:") || marker_key.starts_with("axis:point:") || marker_key.starts_with("helical:axis-point:"))
                            ? interaction::axis
                            : QColor(0, 0, 0);
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.setBrush(marker_color);
                    const QPointF center = project(point.position);
                    // While Plane Properties owns this exact position, its
                    // purple drag handle replaces (rather than overlays) the
                    // normal cyan plane marker. The persisted point remains
                    // in viewer data and returns immediately when the dialog
                    // closes, preserving ordinary View-only editing.
                    if (!hidden_marker && !replaced_by_active_point_manipulator) {
                        if(ordinary_sketch_point)ordinary_sketch_markers.push_back(center);
                        else draw_circular_marker(painter, center, marker_color);
                    }
                    if (!point.label.empty()) {
                        painter.setPen(QPen(replaced_by_active_point_manipulator
                                ? QColor("#D05CFF")
                                : marker_color,
                            1.0));
                        painter.drawText(center + QPointF(8.0, -6.0),
                                         QString::fromStdString(point.label));
                    }
                }
            }
        }
        if (impl_->show_origins || points_selectable ||
            impl_->editing_origin_visible) {
            for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                if (point.reference.semantic_key != "origin:point") continue;
                const QPointF center = project(point.position);
                // Match ONLY the precise per-entity key -- see the identical
                // comment on the plane block above.
                const bool referenced = impl_->constraint_reference_edges.contains(EdgeKey{
                    point.reference.owner_id, point.reference.semantic_key,
                    point.reference.instance_path});
                const QColor marker_color = referenced
                    ? interaction::selected : impl_->origin_point_color(point);
                painter.setPen(QPen(marker_color, 1.0));
                painter.setBrush(marker_color);
                draw_circular_marker(painter, center, marker_color);
                if (!point.label.empty()) {
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.drawText(center + QPointF(8.5, -6.5),
                                     QString::fromStdString(point.label));
                }
            }
        }
        // Face reference inspection uses the shared face overlays below.
        // Edge, point and datum-plane inspection keep their existing paths.
        // Relation participants come directly from the persisted Sketch marker.
        auto relation_keys=impl_->sketch_relation_highlights;
        QColor relation_color=interaction::hover;
        if (highlighted && highlighted->kind==CandidateKind::SketchConstraint &&
            highlighted->geometry_index<impl_->mesh.constraint_markers.size()) {
            relation_keys.clear();
            const auto& marker=impl_->mesh.constraint_markers[highlighted->geometry_index];
            for (const auto& semantic : marker.participant_semantic_keys)
                relation_keys.insert(EdgeKey{marker.reference.owner_id,semantic,marker.reference.instance_path});
            if (impl_->confirmed_candidate) relation_color=interaction::selected;
        }
        if (highlighted && highlighted->kind==CandidateKind::Dimension) {
            const auto index=highlighted->geometry_index;
            const auto* dimension=index<impl_->mesh.dimensions.size()
                ? &impl_->mesh.dimensions[index]
                : index-impl_->mesh.dimensions.size()<impl_->transient_dimensions.size()
                    ? &impl_->transient_dimensions[index-impl_->mesh.dimensions.size()]
                    : nullptr;
            if (dimension) {
                relation_keys.clear();
                for (const auto& semantic : dimension->participant_semantic_keys)
                    relation_keys.insert({dimension->reference.owner_id,semantic,
                        dimension->reference.instance_path});
                if (impl_->confirmed_candidate) relation_color=interaction::selected;
            }
        }
        painter.save();
        painter.setPen(QPen(relation_color,2.4));painter.setBrush(relation_color);
        if(!relation_keys.empty()) {
        for (const auto& edge : impl_->mesh.edges) if (relation_keys.contains(edge_key(edge.reference)))
            for (std::size_t i=1;i<edge.points.size();++i)
                draw_reference_segment(project(edge.points[i-1]),project(edge.points[i]),relation_color,2.4);
        for (const auto& axis : impl_->mesh.axes) if (relation_keys.contains(EdgeKey{axis.reference.owner_id,axis.reference.semantic_key,axis.reference.instance_path})) {
            const double half=axis.display_length*0.5;
            draw_reference_segment(project({axis.point.x-axis.direction.x*half,axis.point.y-axis.direction.y*half,axis.point.z-axis.direction.z*half}),
                project({axis.point.x+axis.direction.x*half,axis.point.y+axis.direction.y*half,axis.point.z+axis.direction.z*half}),relation_color,2.4);
        }
        for (const auto& point : impl_->mesh.points) if (relation_keys.contains(EdgeKey{
                point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path}))
            painter.drawEllipse(project(point.position),4.5,4.5);
        }
        painter.restore();
        // Confirmed wires and dimension inspection use one screen-space
        // presentation, including portions inside the solid. Picking still
        // consumes the unchanged common candidate stream.
        painter.save();painter.setPen(QPen(interaction::selected,1.5,
            Qt::SolidLine,Qt::FlatCap));painter.setBrush(Qt::NoBrush);
        std::size_t selected_wire_segments=0;
        std::vector<QLineF> selected_wire_lines;
        const auto draw_selected_wire = [&](const zima::kernel::ViewerEdge& edge) {
            for (std::size_t i = 1; i < edge.points.size(); ++i) {
                const QLineF line(project(edge.points[i-1]),project(edge.points[i]));
                if(line.length()>1e-6){selected_wire_lines.push_back(line);++selected_wire_segments;}
            }
        };
        paint_phase("base annotation layer");
        const auto* treatment_wire = exact_edge_treatment_wire(impl_->confirmed_candidate);
        // Display-mesh selection is already recolored by draw_lines(). Do not
        // project and repaint it as a second, thicker screen-space wire.
        // Edge-treatment boundaries have a separate semantic wire, so retain
        // that presentation just as for hover and original-reference inspection.
        if (treatment_wire&&!source_wire_visible&&impl_->geometry_editing_presentation) {
            for (const auto index : *treatment_wire)
                if (index < impl_->mesh.edges.size())
                    draw_selected_wire(impl_->mesh.edges[index]);
        }
        if(!selected_wire_lines.empty())painter.drawLines(selected_wire_lines.data(),
            static_cast<int>(selected_wire_lines.size()));
        painter.restore();
        paint_phase("confirmed display wire");
        if(profile_paint)std::fprintf(stderr,"View selected wire segments=%zu paint_state_pairs=1\n",selected_wire_segments);
        for (const auto& edge : confirmed_component_wire())
            for (std::size_t i=1;i<edge.points.size();++i)
                draw_reference_segment(project(edge.points[i-1]),project(edge.points[i]),
                    interaction::selected,1.5);
        if (highlighted) {
            const QColor color = impl_->confirmed_candidate
                ? interaction::selected : interaction::hover;
            const double wire_width = sketch_geometry_visible && impl_->confirmed_candidate
                ? interaction::selected_wire_width : 1.5;
            std::set<std::string> selected_sketch_owners;
            if(highlighted->kind==CandidateKind::Container)for(const auto& edge:impl_->mesh.edges) {
                const auto& key=edge.reference.semantic_key;
                if(edge.reference.instance_path==highlighted->instance_path&&
                    (edge.reference.owner_id==highlighted->owner_id||edge.display_owner_id==highlighted->owner_id)&&
                    (key.starts_with("segment:")||key.starts_with("circle:")||key.starts_with("arc:")||key.starts_with("ellipse:")||key.starts_with("elliptical_arc:")||key.starts_with("bspline:")||key.starts_with("text:")))
                    selected_sketch_owners.insert(edge.reference.owner_id);
            }
            const bool sketch_container = !selected_sketch_owners.empty() || (
                highlighted->kind == CandidateKind::Container &&
                highlighted->semantic_key == "sketch");
            if(sketch_container)selected_sketch_owners.insert(highlighted->owner_id);
            // Datum planes are selected through their two hidden picking
            // triangles, but hover/confirmation must present the semantic
            // plane as its rectangular border. Never expose the triangulated
            // picking surface (including its diagonal) as the highlighted
            // geometry. This applies equally to Origin planes and standalone
            // Plane construction containers.
            const bool datum_plane = highlighted->kind == CandidateKind::Plane &&
                (highlighted->semantic_key.starts_with("origin:plane:") ||
                 highlighted->semantic_key == "plane");
            if (datum_plane) {
                const auto edge = std::find_if(impl_->mesh.edges.begin(),
                    impl_->mesh.edges.end(), [&](const auto& candidate) {
                        return candidate.reference.owner_id == highlighted->owner_id &&
                            (candidate.reference.semantic_key == highlighted->semantic_key ||
                             (highlighted->semantic_key == "plane" &&
                              candidate.reference.semantic_key == "border")) &&
                            candidate.reference.instance_path == highlighted->instance_path;
                    });
                if (edge != impl_->mesh.edges.end() && edge->points.size() > 1) {
                    const std::size_t corner_count = edge->points.size() - 1;
                    zima::kernel::Vec3 center;
                    for (std::size_t index = 0; index < corner_count; ++index) {
                        center.x += edge->points[index].x;
                        center.y += edge->points[index].y;
                        center.z += edge->points[index].z;
                    }
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                    const double plane_scale = is_screen_constant_plane(
                        edge->reference.semantic_key) ? reference_scale : 1.0;
                    const auto display_point = [&](const auto& point) {
                        return zima::kernel::Vec3{
                            center.x + (point.x - center.x) * plane_scale,
                            center.y + (point.y - center.y) * plane_scale,
                            center.z + (point.z - center.z) * plane_scale};
                    };
                    for (std::size_t index = 1; index < edge->points.size(); ++index) {
                        draw_reference_segment(project(display_point(edge->points[index - 1])),
                            project(display_point(edge->points[index])), color, wire_width);
                    }
                }
            }
            if (sketch_container) {
                // The whole Sketch is the exact ordinary-mode candidate.
                // Highlight its persisted ZIMA curves together, while
                // active Sketcher still highlights individual entities via
                // SketchSegment/SketchCurve candidates.
                painter.setPen(QPen(color, wire_width, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(color);
                if(!authored_preview)for (const auto& edge : impl_->mesh.edges) {
                    if (!selected_sketch_owners.contains(edge.reference.owner_id) ||
                        edge.reference.instance_path !=
                            highlighted->instance_path || edge.construction) continue;
                    const auto& key = edge.reference.semantic_key;
                    if (!key.starts_with("segment:") &&
                        !key.starts_with("circle:") &&
                        !key.starts_with("arc:") &&
                        !key.starts_with("corner_radius:") &&
                        !key.starts_with("ellipse:") &&
                        !key.starts_with("elliptical_arc:") &&
                        !key.starts_with("bspline:") &&
                        !key.starts_with("text:")) continue;
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
                for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                    if (selected_sketch_owners.contains(point.reference.owner_id) &&
                        point.reference.instance_path ==
                            highlighted->instance_path &&
                        (point.reference.semantic_key.starts_with("point:") ||
                         point.reference.semantic_key ==
                            "sketch:origin-marker")) {
                        draw_circular_marker(
                            painter, project(point.position), color);
                    }
                }
            }
            if (highlighted->kind == CandidateKind::Container) {
                for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                    if (point.reference.semantic_key !=
                            "container:origin-marker" ||
                        point.reference.owner_id != highlighted->owner_id ||
                        point.reference.instance_path !=
                            highlighted->instance_path) continue;
                    painter.setPen(QPen(color, 1.0));
                    painter.setBrush(color);
                    draw_circular_marker(painter, project(point.position), color);
                }
            }
            auto face_overlays=inspected_faces;
            if(candidate_uses_face_boundary_overlay(*highlighted) &&
                std::none_of(face_overlays.begin(),face_overlays.end(),[&](const auto& face){
                    return face.owner_id==highlighted->owner_id && face.semantic_key==highlighted->semantic_key && face.instance_path==highlighted->instance_path;}))
                face_overlays.push_back(*highlighted);
            for(const auto& face_overlay:face_overlays) {
                const auto highlighted=std::optional<ViewerCandidate>(face_overlay);
                const bool inspected=std::any_of(inspected_faces.begin(),inspected_faces.end(),[&](const auto& face){
                    return face.owner_id==face_overlay.owner_id && face.semantic_key==face_overlay.semantic_key && face.instance_path==face_overlay.instance_path;});
                painter.setPen(QPen(inspected?interaction::selected:color,1.0));
                painter.setBrush(Qt::NoBrush);
                const bool original = highlighted->geometry ==
                    CandidateGeometry::OriginalReference;
                const auto& vertices = original
                    ? impl_->mesh.original_references.vertices : impl_->mesh.vertices;
                const auto& triangles = original
                    ? impl_->mesh.original_references.triangles : impl_->mesh.triangles;
                const auto& triangle_references = original
                    ? impl_->mesh.original_references.triangle_references
                    : impl_->mesh.triangle_references;
                using RoundedPoint = std::array<long long, 3>;
                using BoundaryKey = std::tuple<std::string, RoundedPoint, RoundedPoint>;
                const auto cache_key=std::tuple{highlighted->geometry,highlighted->kind,
                    EdgeKey{highlighted->owner_id,highlighted->semantic_key,highlighted->instance_path}};
                auto cached=impl_->face_boundaries.find(cache_key);
                if(cached==impl_->face_boundaries.end()) {
                // Adjacency is model data; only silhouette visibility changes
                // with the camera. Bound retained hover packets to 32 faces.
                if(impl_->face_boundaries.size()>=32)impl_->face_boundaries.clear();
                std::map<BoundaryKey, Impl::FaceBoundarySegment> boundary;
                const auto rounded = [](const zima::kernel::Vec3& point) {
                    constexpr double scale = 1.0e7;
                    return RoundedPoint{
                        std::llround(point.x * scale),
                        std::llround(point.y * scale),
                        std::llround(point.z * scale)};
                };
                const auto& face_triangles=original?impl_->original_face_triangles:impl_->display_face_triangles;
                const auto selected=face_triangles.find(std::get<2>(cache_key));
                const std::vector<std::size_t>* triangle_slots=highlighted->kind==CandidateKind::Face
                    ? (selected==face_triangles.end()?nullptr:&selected->second):nullptr;
                const auto count=highlighted->kind==CandidateKind::Face?(triangle_slots?triangle_slots->size():0):triangle_references.size();
                for (std::size_t slot=0;slot<count;++slot) {
                    const auto triangle=triangle_slots?(*triangle_slots)[slot]:slot;
                    const auto& reference = triangle_references[triangle];
                    // Datum and work planes have their own screen-constant
                    // rectangular highlight above. Feeding their persisted
                    // model-space picking triangles into this generic
                    // Container/Face boundary pass draws a second tiny
                    // unscaled rectangle (the "ghost Origin" in 01.png).
                    // Picking triangles remain intact; only this duplicate
                    // presentation path is suppressed.
                    if (reference.semantic_key == "plane" ||
                        reference.semantic_key.starts_with("origin:plane:")) {
                        continue;
                    }
                    const bool matches = highlighted->kind == CandidateKind::Occurrence
                        ? reference.instance_path.starts_with(highlighted->instance_path)
                        : highlighted->kind == CandidateKind::Container
                            ? reference.owner_id == highlighted->owner_id ||
                                ((highlighted->semantic_key == "plane" ||
                                  highlighted->semantic_key == "axis") &&
                                 reference.owner_id ==
                                    highlighted->owner_id + ":entity")
                            : reference.owner_id == highlighted->owner_id &&
                              reference.semantic_key == highlighted->semantic_key;
                    if (!matches || (highlighted->kind != CandidateKind::Occurrence &&
                        reference.instance_path != highlighted->instance_path) ||
                        triangle * 3 + 2 >= triangles.size()) continue;
                    const auto first = triangles[triangle * 3];
                    const auto second = triangles[triangle * 3 + 1];
                    const auto third = triangles[triangle * 3 + 2];
                    if (first >= vertices.size() || second >= vertices.size() ||
                        third >= vertices.size()) continue;
                    const std::array indices{first, second, third};
                    const auto& pa = vertices[first];
                    const auto& pb = vertices[second];
                    const auto& pc = vertices[third];
                    QVector3D triangle_normal = QVector3D::crossProduct(
                        QVector3D(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z),
                        QVector3D(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z));
                    if (triangle_normal.lengthSquared() > 1.0e-12F) {
                        triangle_normal.normalize();
                    }
                    for (std::size_t side = 0; side < 3; ++side) {
                        const auto& a = vertices[indices[side]];
                        const auto& b = vertices[indices[(side + 1) % 3]];
                        auto key_a = rounded(a);
                        auto key_b = rounded(b);
                        if (key_b < key_a) std::swap(key_a, key_b);
                        // Count tessellation edges inside one semantic face,
                        // not across the whole Container. A real Box edge is
                        // shared by two different faces and must remain in
                        // the container/body outline; only a triangle
                        // diagonal repeated inside the same face disappears.
                        auto& segment = boundary[{
                            reference.semantic_key, key_a, key_b}];
                        if (segment.uses == 0) {
                            segment.first = a;
                            segment.second = b;
                        }
                        if (segment.uses < segment.normals.size()) {
                            segment.normals[segment.uses] = triangle_normal;
                        }
                        ++segment.uses;
                    }
                }
                std::vector<Impl::FaceBoundarySegment> segments;segments.reserve(boundary.size());
                for(const auto& [key,segment]:boundary)segments.push_back(segment);
                cached=impl_->face_boundaries.emplace(cache_key,std::move(segments)).first;
                }
                // Highlight the semantic face boundary and its current-view
                // silhouette, never the complete OCCT triangulation. A
                // closed curved face such as a Sphere has no boundary at
                // all, so without the silhouette the picker did find the
                // Container but produced no visible green feedback.
                const QVector3D view_direction = impl_->orientation.inverted()
                    .rotatedVector(QVector3D(0.0F, 0.0F, 1.0F));
                for (const auto& segment : cached->second) {
                    QVector3D second_normal = segment.normals[1];
                    if (QVector3D::dotProduct(
                            segment.normals[0], second_normal) < 0.0F) {
                        second_normal = -second_normal;
                    }
                    const bool silhouette = segment.uses == 2 &&
                        (QVector3D::dotProduct(segment.normals[0], view_direction) >= 0.0F) !=
                        (QVector3D::dotProduct(second_normal, view_direction) >= 0.0F);
                    if (segment.uses == 1 || silhouette) {
                        painter.drawLine(project(segment.first),
                                         project(segment.second));
                    }
                }
            }
            const auto& selectable_edges =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.edges : impl_->mesh.edges;
            const auto& selectable_points =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.points : impl_->mesh.points;
            const auto& selectable_axes =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.axes : impl_->mesh.axes;
            if ((highlighted->kind == CandidateKind::Edge ||
                 highlighted->kind == CandidateKind::SketchSegment ||
                 highlighted->kind == CandidateKind::SketchCurve ||
                 highlighted->kind == CandidateKind::SketchText ||
                 highlighted->kind == CandidateKind::Symbol ||
                 (highlighted->kind == CandidateKind::SketchExternalReference &&
                  (highlighted->semantic_key.starts_with("external_edge:") ||
                   highlighted->semantic_key.starts_with("external_axis:") ||
                   highlighted->semantic_key.starts_with("external_face:"))) ||
                 highlighted->kind == CandidateKind::SketchTrimPiece) &&
                highlighted->geometry_index < selectable_edges.size() &&
                (!impl_->passive_sketch_axis(selectable_edges[highlighted->geometry_index])||
                    impl_->sketch_axis_visible(selectable_edges[highlighted->geometry_index])) &&
                // Sketch wires were already recolored in their styled pass.
                // A second solid stroke fills dash gaps and truncates centerlines.
                !(sketch_geometry_visible &&
                  is_sketch_wire_edge(selectable_edges[highlighted->geometry_index]) &&
                  (selectable_edges[highlighted->geometry_index].construction ||
                   selectable_edges[highlighted->geometry_index].dash_dot) &&
                  !selectable_edges[highlighted->geometry_index].filled_text)) {
                painter.setPen(QPen(color, wire_width, Qt::SolidLine, Qt::RoundCap));
                if (highlighted->kind == CandidateKind::SketchText || highlighted->kind == CandidateKind::Symbol ||
                    (highlighted->kind == CandidateKind::SketchExternalReference &&
                     highlighted->semantic_key.starts_with("external_face:"))) {
                    for (const auto& edge : selectable_edges) {
                        if (edge.reference.owner_id != highlighted->owner_id ||
                            edge.reference.semantic_key != highlighted->semantic_key ||
                            edge.reference.instance_path != highlighted->instance_path) continue;
                        for (std::size_t index = 1; index < edge.points.size(); ++index) {
                            painter.drawLine(project(edge.points[index - 1]),
                                             project(edge.points[index]));
                        }
                    }
                } else {
                    const auto& edge = selectable_edges[highlighted->geometry_index];
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
                if(highlighted->kind==CandidateKind::SketchText) {
                    painter.setPen(QPen(color,2.0));painter.setBrush(color);
                    for(const auto& point:selectable_points)
                        if(point.reference.owner_id==highlighted->owner_id&&point.reference.instance_path==highlighted->instance_path&&
                           point.sketch_text_key==highlighted->semantic_key)
                            painter.drawEllipse(project(point.position),5.0,5.0);
                }
            } else if ((highlighted->kind == CandidateKind::Vertex ||
                        highlighted->kind == CandidateKind::SketchPoint ||
                        (highlighted->kind == CandidateKind::SketchExternalReference &&
                         highlighted->semantic_key.starts_with("external_point:"))) &&
                       highlighted->geometry_index < selectable_points.size() &&
                       impl_->helper_point_visible(selectable_points[highlighted->geometry_index],true)) {
                painter.setPen(QPen(color, 2.0));
                painter.setBrush(color);
                painter.drawEllipse(project(
                    selectable_points[highlighted->geometry_index].position), 5.0, 5.0);
            }
            // A constraint marker is an independently selectable record.
            // Selecting it colours only its glyph (drawn above); participant
            // geometry must not look selected or join the deletion target.
            // A Dimension is an independent selectable annotation. Its own
            // witness/main/leader lines and text are recoloured in the
            // dimension pass above; recolouring participant geometry here
            // made one LMB confirmation appear to select both the dimension
            // and its measured segment/points, and made RMB ownership
            // ambiguous.
            // Whole-Origin hover/selection is already handled by the main
            // plane/axis overlay loops above. Redrawing the same geometry a
            // second time here produced two differently antialiased copies
            // that appeared to pulse between sizes while zooming/picking.
        }
        // Point markers are the final reference layer.  Python recolours the
        // marker itself on hover/selection; drawing a later highlight on top
        // made the black Origin centre disappear at the axis intersection.
        if (impl_->show_origins || points_selectable ||
            impl_->editing_origin_visible) {
            for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                if (point.reference.semantic_key != "origin:point") continue;
                QColor marker_color=impl_->origin_point_color(point);
                bool exact_highlight = highlighted &&
                    highlighted->owner_id == point.reference.owner_id &&
                    highlighted->instance_path == point.reference.instance_path &&
                    ((highlighted->kind == CandidateKind::Vertex &&
                      highlighted->semantic_key == point.reference.semantic_key) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "origin"));
                if (!exact_highlight && highlighted &&
                    highlighted->kind == CandidateKind::Container &&
                    highlighted->semantic_key == "point") {
                    const auto& highlighted_points = highlighted->geometry ==
                            CandidateGeometry::OriginalReference
                        ? impl_->mesh.original_references.points
                        : impl_->mesh.points;
                    if (highlighted->geometry_index < highlighted_points.size()) {
                        exact_highlight = QLineF(project(point.position), project(
                            highlighted_points[highlighted->geometry_index].position))
                                .length() <= 1.0;
                    }
                }
                if (exact_highlight) {
                    marker_color = impl_->confirmed_candidate
                        ? interaction::selected : interaction::hover;
                }
                const QPointF center = project(point.position);
                draw_circular_marker(painter, center, marker_color);
                if (!point.label.empty()) {
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.drawText(center + QPointF(8.5, -6.5),
                        QString::fromStdString(point.label));
                }
            }
        }
        // Whole-Sketch point feedback must also remain visible where a real
        // native point coincides with a Body or document Origin marker.
        for(const auto& center:ordinary_sketch_markers)
            draw_circular_marker(painter,center,interaction::selected);
        if(impl_->command_snap_points_visible)for(const auto& point:impl_->command_snap_points) {
            const auto hovered=hovered_candidate();
            const bool hover=hovered && hovered->owner_id==point.reference.owner_id &&
                hovered->semantic_key==point.reference.semantic_key && hovered->instance_path==point.reference.instance_path;
            draw_circular_marker(painter,project(point.position),hover?interaction::hover:
                point.construction?interaction::selected:palette().color(QPalette::Text),6.0);
        }
        if (impl_->component_origin_handle) {
            const auto& key = *impl_->component_origin_handle;
            for (const auto& point : impl_->mesh.points) {
                if (!impl_->origin_visible({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path})) continue;
                if (point.reference.owner_id != key.owner_id ||
                    point.reference.semantic_key != key.semantic_key ||
                    point.reference.instance_path != key.instance_path) continue;
                draw_circular_marker(painter,project(point.position),QColor("#D05CFF"),6.0);
                break;
            }
        }
        painter.setPen(QPen(interaction::selected, interaction::selected_wire_width,
                            Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(interaction::selected);
        for (const auto& selected : impl_->sketch_box_selected_candidates) {
            if ((selected.kind == CandidateKind::SketchSegment ||
                 selected.kind == CandidateKind::SketchCurve ||
                 selected.kind == CandidateKind::SketchText ||
                 (selected.kind == CandidateKind::SketchExternalReference &&
                  !selected.semantic_key.starts_with("external_point:"))) &&
                selected.geometry_index < impl_->mesh.edges.size()) {
                if(selected.kind==CandidateKind::SketchText) {
                    for(const auto& point:impl_->mesh.points)
                        if(point.reference.owner_id==selected.owner_id&&point.reference.instance_path==selected.instance_path&&point.sketch_text_key==selected.semantic_key)
                            painter.drawEllipse(project(point.position),5.0,5.0);
                }
                const auto& edge = impl_->mesh.edges[selected.geometry_index];
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    draw_reference_segment(project(edge.points[index - 1]),
                        project(edge.points[index]), interaction::selected, 2.0);
                }
            } else if ((selected.kind == CandidateKind::SketchPoint ||
                        (selected.kind == CandidateKind::SketchExternalReference &&
                         selected.semantic_key.starts_with("external_point:"))) &&
                       selected.geometry_index < impl_->mesh.points.size()) {
                painter.drawEllipse(project(
                    impl_->mesh.points[selected.geometry_index].position), 5.0, 5.0);
            }
        }
        if (impl_->sketch_box_start && impl_->sketch_box_end) {
            const QRectF box = QRectF(*impl_->sketch_box_start,
                                      *impl_->sketch_box_end).normalized();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(67, 185, 255, 35));
            painter.drawPolygon(QPolygonF{box.topLeft(), box.topRight(),
                                          box.bottomRight(), box.bottomLeft()});
            const QColor border("#43B9FF");
            draw_reference_segment(box.topLeft(), box.topRight(), border, 1.0);
            draw_reference_segment(box.topRight(), box.bottomRight(), border, 1.0);
            draw_reference_segment(box.bottomRight(), box.bottomLeft(), border, 1.0);
            draw_reference_segment(box.bottomLeft(), box.topLeft(), border, 1.0);
        }
        // Editor-only repeat regions are the topmost wire, including over text
        // and constraint labels. They never become printable template geometry.
        for(const auto& edge:impl_->mesh.edges)if(edge.reference.semantic_key.starts_with("repeat_region:")) {
            QColor color("#D580FF");
            if(impl_->confirmed_candidate&&candidate_recolors_wire_edge(*impl_->confirmed_candidate,edge))color=interaction::selected;
            else if(hovered_candidate()&&candidate_recolors_wire_edge(*hovered_candidate(),edge))color=interaction::hover;
            painter.setPen(QPen(color,2.0));painter.setBrush(Qt::NoBrush);
            for(std::size_t i=1;i<edge.points.size();++i)painter.drawLine(project(edge.points[i-1]),project(edge.points[i]));
        }
        paint_dimension_text_layer(painter,dimension_texts,.5*logicalDpiX()/25.4,
            [&](QPainter& text_painter,const QPainterPath& mask) {
                text_painter.setClipPath(mask,Qt::IntersectClip);
                // Match the exact OpenGL background bands, including high-DPI rounding.
                for(int band=0;band<background_bands;++band) {
                    const qreal factor=(band+.5)/background_bands;
                    const auto channel=[&](int bottom,int top){return (bottom+(top-bottom)*factor)/255.;};
                    const QColor color=QColor::fromRgbF(channel(theme.bottom.red(),theme.top.red()),channel(theme.bottom.green(),theme.top.green()),channel(theme.bottom.blue(),theme.top.blue()));
                    const int first=framebuffer_height*band/background_bands,last=framebuffer_height*(band+1)/background_bands;
                    text_painter.fillRect(QRectF(0,(framebuffer_height-last)/pixel_ratio,width(),(last-first)/pixel_ratio),color);
                }
                text_painter.setClipping(false);
            });
        for(int i=0;i<3;++i)if(auto point=symbol_handle_position(i))draw_circular_marker(painter,*point,QColor("#D05CFF"));
        // Editing handles remain above coincident geometry and point markers.
        if(impl_->dimension_layout_commit&&impl_->confirmed_candidate&&dimension_layout_editable(*impl_->confirmed_candidate)&&impl_->show_dimensions) {
            for(int i=0;i<3;++i)if(auto point=dimension_handle_position(*impl_->confirmed_candidate,i))draw_circular_marker(painter,*point,QColor("#D05CFF"));
        }
    }
}

void MeshView::update_candidates(const QPointF& position) {
    if (width() <= 0 || height() <= 0) return;
    auto next = selection_candidates_at(position);
    const bool same_order = next.size() == impl_->candidates.size() &&
        std::equal(next.begin(), next.end(), impl_->candidates.begin(),
            [](const ViewerCandidate& left, const ViewerCandidate& right) {
                return left.kind == right.kind && left.owner_id == right.owner_id &&
                    left.semantic_key == right.semantic_key &&
                    left.instance_path == right.instance_path;
            });
    impl_->candidates = std::move(next);
    impl_->candidates_position = position;
    impl_->has_candidates_position = true;
    if (!same_order || impl_->active_candidate >= impl_->candidates.size()) {
        impl_->active_candidate = 0;
    }
    update();
}

std::optional<std::pair<zima::kernel::Vec3, zima::kernel::Vec3>>
MeshView::ray_at(const QPointF& position) const {
    if (width() <= 0 || height() <= 0) return std::nullopt;
    const float x = static_cast<float>(2.0 * position.x() / width() - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * position.y() / height());
    bool invertible = false;
    const QMatrix4x4 inverse =
        (impl_->projection(width(), height()) * impl_->view()).inverted(&invertible);
    if (!invertible) return std::nullopt;
    QVector4D near_point = inverse * QVector4D(x, y, -1.0F, 1.0F);
    QVector4D far_point = inverse * QVector4D(x, y, 1.0F, 1.0F);
    near_point /= near_point.w();
    far_point /= far_point.w();
    const QVector3D direction = (far_point.toVector3D() - near_point.toVector3D()).normalized();
    return std::pair{
        zima::kernel::Vec3{near_point.x(), near_point.y(), near_point.z()},
        zima::kernel::Vec3{direction.x(), direction.y(), direction.z()}};
}

void MeshView::mousePressEvent(QMouseEvent* event) {
    if(event->button()==Qt::LeftButton&&impl_->symbol_handle_move&&impl_->symbol_handles) {
        int closest=-1;double distance=6.;for(int i=0;i<3;++i)if(auto p=symbol_handle_position(i)){const double d=QLineF(*p,event->position()).length();if(d<=distance){distance=d;closest=i;}}
        for(int i=closest;i>=0;) {
            const auto handles=impl_->symbol_handles();if(!handles)break;
            const auto a=symbol_handle_annotation();if(!a)break;
            if(i==0){impl_->symbol_handle_move(handles->id,0,handles->contact,a->shelf_length);impl_->symbol_contact_drag=true;impl_->symbol_contact_moved=false;impl_->symbol_contact_drag_start=event->position();}
            else {
                const auto camera=impl_->view().inverted();const auto r=camera.mapVector(QVector3D(1,0,0)).normalized(),u=camera.mapVector(QVector3D(0,1,0)).normalized();
                const kernel::Vec3 right{r.x(),r.y(),r.z()},up{u.x(),u.y(),u.z()};const bool left=kernel::dimension_dot(kernel::dimension_sub(a->contact,a->grip),right)<=0;
                const auto point=kernel::annotation_handles(*a,right,up,left,true,true)[i];
                impl_->symbol_drag=Impl::SymbolDrag{handles->id,*a,right,up,kernel::dimension_cross(right,up),point,left,i};
            }
            event->accept();return;
        }
    }
    if(event->button()==Qt::RightButton&&impl_->layout_drag&&(event->buttons()&Qt::LeftButton)) {
        auto& drag=*impl_->layout_drag;kernel::cycle_dimension_presentation(drag.initial,drag.source.kind);
        drag.current.arrows_reversed=drag.initial.arrows_reversed;
        drag.current.radius_center_line_hidden=drag.initial.radius_center_line_hidden;drag.moved=true;
        auto shown=kernel::layout_dimension(drag.source,drag.bounds,drag.current);const auto i=drag.candidate.geometry_index;
        if(i<impl_->mesh.dimensions.size())impl_->mesh.dimensions[i]=shown;else impl_->transient_dimensions[i-impl_->mesh.dimensions.size()]=shown;
        update();event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&impl_->dimension_layout_commit&&impl_->confirmed_candidate&&dimension_layout_editable(*impl_->confirmed_candidate)) {
        const auto candidate=*impl_->confirmed_candidate;
        if(auto source=dimension_source(candidate);source && !source->rotation_handle)for(int handle=0;handle<3;++handle)if(auto point=dimension_handle_position(candidate,handle);point&&QLineF(*point,event->position()).length()<=5.5) {
            const auto i=candidate.geometry_index;const auto shown=i<impl_->mesh.dimensions.size()?impl_->mesh.dimensions[i]:impl_->transient_dimensions[i-impl_->mesh.dimensions.size()];
            const auto key=kernel::ObjectEnvelopeKey{source->reference.owner_id,source->reference.instance_path};
            const auto bounds=impl_->object_bounds.contains(key)&&impl_->object_bounds.at(key).valid?impl_->object_bounds.at(key):impl_->dimension_bounds;
            const auto initial=impl_->dimension_layout_resolver?impl_->dimension_layout_resolver(source->reference).value_or(kernel::DimensionLayout{}):kernel::DimensionLayout{};
            if(impl_->camera_animation){impl_->camera_animation->stop();impl_->camera_animation=nullptr;}
            impl_->layout_drag=Impl::LayoutDrag{candidate,*source,shown,bounds,initial,initial,event->position(),handle,false};
            event->accept();return;
        }
    }
    if(event->button()==Qt::RightButton && impl_->confirmed_candidate &&
       impl_->confirmed_candidate->kind==CandidateKind::Dimension) {
        for(int handle=0;handle<3;++handle)
            if(auto point=dimension_handle_position(*impl_->confirmed_candidate,handle);
               point && QLineF(*point,event->position()).length()<=5.5) {
                event->accept();return;
            }
    }
    impl_->last_pointer = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
      for (const auto& handle : impl_->extent_manipulators) {
        const double norm = std::sqrt(handle.direction.x * handle.direction.x +
            handle.direction.y * handle.direction.y +
            handle.direction.z * handle.direction.z);
        if (norm > 1.0e-12) {
            const zima::kernel::Vec3 direction{handle.direction.x / norm,
                handle.direction.y / norm, handle.direction.z / norm};
            const auto project = [&](const zima::kernel::Vec3& point) {
                QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                    QVector4D(point.x, point.y, point.z, 1.0F);
                if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                    (1.0F - clip.y()) * height() / 2.0F);
            };
            const zima::kernel::Vec3 endpoint{handle.origin.x + direction.x * handle.length,
                handle.origin.y + direction.y * handle.length,
                handle.origin.z + direction.z * handle.length};
            const QPointF pointer = event->position();
            const double start_distance = QLineF(pointer, project(handle.origin)).length();
            const double end_distance = handle.point_only
                ? std::numeric_limits<double>::infinity()
                : QLineF(pointer, project(endpoint)).length();
            constexpr double tolerance = 12.0;
            if (std::min(start_distance, end_distance) <=
                    tolerance * devicePixelRatioF()) {
                impl_->dragged_extent_manipulator = handle;
                impl_->dragged_extent_key = start_distance <= end_distance
                    ? handle.start_key : handle.end_key;
                impl_->hovered_extent_key = impl_->dragged_extent_key;
                if (impl_->extent_begin_callback)
                    impl_->extent_begin_callback(impl_->dragged_extent_key);
                event->accept();
                update();
                return;
            }
        }
      }
    }
    if (event->button() == Qt::RightButton &&
        event->buttons().testFlag(Qt::MiddleButton)) {
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        impl_->middle_dragged = false;
        impl_->middle_press_position = event->position().toPoint();
    }
    const bool additive_sketch_click = event->button() == Qt::LeftButton &&
        event->modifiers().testFlag(Qt::ControlModifier);
    const auto multi_selectable = [](CandidateKind kind) {
        return kind == CandidateKind::SketchSegment ||
            kind == CandidateKind::SketchCurve ||
            kind == CandidateKind::SketchPoint ||
            kind == CandidateKind::SketchText ||
            kind == CandidateKind::SketchExternalReference;
    };
    // A confirmed candidate normally suppresses hover. When Ctrl starts an
    // additive Sketch selection, transfer that candidate to the persistent
    // multi-selection overlay first so the common picker can offer the next
    // segment without requiring an empty click between them.
    if (additive_sketch_click && impl_->confirmed_candidate &&
        multi_selectable(impl_->confirmed_candidate->kind)) {
        if (std::find(impl_->sketch_box_selected_candidates.begin(),
                impl_->sketch_box_selected_candidates.end(),
                *impl_->confirmed_candidate) ==
            impl_->sketch_box_selected_candidates.end()) {
            impl_->sketch_box_selected_candidates.push_back(
                *impl_->confirmed_candidate);
        }
        impl_->confirmed_candidate.reset();
        impl_->candidates.clear();
    }
    // Hover, RMB cycling and LMB confirmation always consume the candidate
    // list produced for the click position. A preceding mouse move is not a
    // precondition for selecting or clearing a selection.
    if (event->button() == Qt::LeftButton) {
        const bool replacing_confirmed_selection =
            impl_->confirmed_candidate.has_value();
        update_candidates(event->position());
        if (replacing_confirmed_selection && !impl_->candidates.empty()) {
            // RMB cycling chooses the candidate for the immediately following
            // confirmation only. Once an object is confirmed, a new LMB
            // gesture starts again from the viewer's first offered candidate.
            // Otherwise a Dimension selected behind an axis-owned point kept
            // stealing every later attempt to drag that point.
            impl_->active_candidate = 0;
        }
        // Candidate construction already orders an editable Sketch point
        // ahead of overlapping annotations. Do not search for another point
        // here: RMB may have deliberately cycled to a different point or
        // annotation in this exact list, and LMB must confirm that candidate.
        if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
            QStringList offered;
            for (const auto& item : impl_->candidates) {
                offered.push_back(QString::fromStdString(item.semantic_key));
            }
            QStringList selected;
            for (const auto& item : impl_->sketch_box_selected_candidates) {
                selected.push_back(QString::fromStdString(item.semantic_key));
            }
            qInfo().noquote() << "SKETCH_TRACE|PRESS|ctrl="
                << additive_sketch_click << "|offered=" << offered.join(',')
                << "|viewer_selected=" << selected.join(',');
        }
        // Match Python Sketcher: with exactly two selected segments, the
        // coincident point under the pointer owns the corner-radius drag even
        // when the common picker ordered a Segment before that Point.
        const auto selected_segment_count = std::count_if(
            impl_->sketch_box_selected_candidates.begin(),
            impl_->sketch_box_selected_candidates.end(), [](const auto& candidate) {
                return candidate.kind == CandidateKind::SketchSegment;
            });
        if (selected_segment_count == 2) {
            const auto point = std::find_if(impl_->candidates.begin(),
                impl_->candidates.end(), [](const auto& candidate) {
                    return candidate.kind == CandidateKind::SketchPoint;
                });
            if (point != impl_->candidates.end()) {
                impl_->active_candidate = static_cast<std::size_t>(
                    std::distance(impl_->candidates.begin(), point));
            }
        }
    }
    // RMB cycles the exact same ordered list used by hover and LMB.  Refresh
    // it at the click position so cycling also works immediately after a
    // command changes its selection contract, without requiring a preceding
    // mouse-move event. If the pointer is still within picking tolerance of
    // where the list was last computed (a stationary hover just before the
    // click), reuse that list unchanged instead of recomputing it: a
    // redundant recompute can otherwise re-stabilize the same order and
    // silently reset the active index, making the first RMB press appear to
    // do nothing before cycling starts working from the second press.
    if (event->button() == Qt::RightButton && !impl_->confirmed_candidate) {
        const QPointF click_position = event->position();
        const QPointF delta = impl_->has_candidates_position
            ? click_position - impl_->candidates_position : QPointF{};
        const bool cursor_stationary = impl_->has_candidates_position &&
            !impl_->candidates.empty() &&
            std::hypot(delta.x(), delta.y()) <= 9.0 * devicePixelRatioF();
        if (!cursor_stationary) {
            update_candidates(click_position);
        }
    }
    // Reference entry owns RMB over an offered candidate. Only empty View
    // space cycles the pending dimension presentation; otherwise its preview
    // can consume every click and make overlapping points/lines unreachable.
    if(event->button()==Qt::RightButton && !impl_->confirmed_candidate &&
       impl_->candidates.empty() && impl_->dimension_placement_cycle_callback &&
       impl_->dimension_placement_cycle_callback()) {event->accept();return;}
    if (event->button() == Qt::LeftButton &&
        impl_->command_gesture_begin_callback) {
        const auto ray = ray_at(event->position());
        const std::optional<ViewerCandidate> candidate = impl_->candidates.empty()
            ? std::nullopt
            : std::optional{impl_->candidates[impl_->active_candidate]};
        if (ray && impl_->command_gesture_begin_callback(
                candidate, ray->first, ray->second)) {
            impl_->command_gesture_active = true;
            impl_->confirmed_candidate.reset();
            impl_->candidates.clear();
            update();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton &&
        impl_->sketch_box_selection_enabled) {
        // A confirmed Sketch entity deliberately suppresses ordinary hover,
        // so the cached candidate list may still describe the previous
        // click. Box selection must decide from the exact press position.
        update_candidates(event->position());
    }
    if (event->button() == Qt::LeftButton &&
        impl_->sketch_box_selection_enabled && impl_->candidates.empty()) {
        impl_->sketch_box_start = event->position();
        impl_->sketch_box_end = event->position();
        impl_->confirmed_candidate.reset();
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && impl_->world_click_callback) {
        const auto ray = ray_at(event->position());
        if (ray && impl_->world_click_callback(ray->first, ray->second)) {
            impl_->candidates.clear();
            update();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !impl_->candidates.empty()) {
        // Keep the exact candidate consumed by this press locally. The
        // confirmation callback may rebuild the scene and consequently clear
        // the viewer's transient confirmed/candidate state before a drag is
        // started; that must not make dragging depend on callback side effects.
        const ViewerCandidate pressed_candidate =
            impl_->candidates[impl_->active_candidate];
        if (pressed_candidate.kind != CandidateKind::Dimension)
            {impl_->container_inspection_wire.clear();impl_->inspected_container.reset();}
        impl_->confirmed_candidate = pressed_candidate;
        impl_->selected_container_origin_id =
            impl_->confirmed_candidate->kind == CandidateKind::Container &&
                (impl_->confirmed_candidate->semantic_key == "point" ||
                 impl_->confirmed_candidate->semantic_key == "axis" ||
                 impl_->confirmed_candidate->semantic_key == "plane")
            ? impl_->confirmed_candidate->owner_id + ":origin"
            : std::string{};
        // Start the gesture from the same pressed candidate before notifying
        // ordinary selection listeners. Those listeners are allowed to
        // rebuild the tree or scene, but that must not invalidate a gesture
        // that was already unambiguously requested by this press.
        const auto drag_ray = ray_at(event->position());
        // A dimension click also opens its value editor on double click.
        // Defer the modeling gesture until the pointer crosses the drag
        // threshold, so click jitter cannot change a value or rebuild the scene.
        const bool defer_dimension_drag = pressed_candidate.kind == CandidateKind::Dimension;
        impl_->pending_dimension_drag = defer_dimension_drag
            ? std::optional{pressed_candidate} : std::nullopt;
        impl_->dimension_drag_press_position = event->position();
        const bool drag_started = !defer_dimension_drag && impl_->drag_begin_callback && drag_ray
            ? impl_->drag_begin_callback(
                  pressed_candidate, drag_ray->first, drag_ray->second)
            : false;
        if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
            qInfo().noquote() << "SKETCH_TRACE|VIEW_DRAG_BEGIN|candidate="
                << QString::fromStdString(pressed_candidate.semantic_key)
                << "|accepted=" << drag_started;
        }
        notify_confirmation();
        if (additive_sketch_click && multi_selectable(pressed_candidate.kind)) {
            const auto selected = std::find(
                impl_->sketch_box_selected_candidates.begin(),
                impl_->sketch_box_selected_candidates.end(), pressed_candidate);
            if (selected == impl_->sketch_box_selected_candidates.end()) {
                impl_->sketch_box_selected_candidates.push_back(pressed_candidate);
            } else {
                impl_->sketch_box_selected_candidates.erase(selected);
            }
            impl_->confirmed_candidate.reset();
            impl_->candidates.clear();
        } else if (!additive_sketch_click && multi_selectable(pressed_candidate.kind)) {
            impl_->sketch_box_selected_candidates.clear();
        }
        impl_->drag_active = drag_started;
        if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
            QStringList selected;
            for (const auto& item : impl_->sketch_box_selected_candidates) {
                selected.push_back(QString::fromStdString(item.semantic_key));
            }
            qInfo().noquote() << "SKETCH_TRACE|AFTER_CONFIRM|viewer_selected="
                << selected.join(',');
        }
        update();
    } else if (event->button() == Qt::LeftButton) {
        clear_selection();
        if (impl_->empty_confirmation_callback) {
            impl_->empty_confirmation_callback();
        }
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        if (impl_->confirmed_candidate) {
            if(impl_->confirmed_candidate->kind==CandidateKind::Dimension) {
                const auto candidates=selection_candidates_at(event->position());
                const auto& selected=*impl_->confirmed_candidate;
                if(std::none_of(candidates.begin(),candidates.end(),[&](const auto& c){
                    return c.kind==selected.kind && c.owner_id==selected.owner_id &&
                           c.semantic_key==selected.semantic_key && c.instance_path==selected.instance_path;
                })){event->accept();return;}
            }
            if (impl_->context_menu_callback) {
                impl_->context_menu_callback(
                    *impl_->confirmed_candidate, event->globalPosition().toPoint());
            }
        } else if (!impl_->candidates.empty() &&
                   impl_->candidate_right_click_callback &&
                   impl_->candidate_right_click_callback(
                       impl_->candidates[impl_->active_candidate],
                       impl_->active_candidate, impl_->candidates.size())) {
            event->accept();
        } else if (impl_->candidates.size() > 1) {
            impl_->active_candidate =
                next_candidate_index(impl_->active_candidate, impl_->candidates.size());
            update();
            static_cast<void>(refresh_current_pointer_preview());
        } else if (impl_->candidates.size() == 1 &&
                   impl_->single_candidate_right_click_callback &&
                   impl_->single_candidate_right_click_callback(
                       impl_->candidates.front())) {
            event->accept();
        } else if (impl_->candidates.empty() &&
                   impl_->empty_right_click_callback &&
                   impl_->empty_right_click_callback()) {
            event->accept();
        }
    }
}

void MeshView::mouseDoubleClickEvent(QMouseEvent* event) {
    impl_->pending_dimension_drag.reset();
    impl_->layout_drag.reset();
    impl_->last_pointer = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) {
        impl_->middle_double_clicked = true;
        if (impl_->double_middle_click_callback &&
            impl_->double_middle_click_callback()) {
            event->accept();
        }
        return;
    }
    if (event->button() != Qt::LeftButton || !impl_->double_confirmation_callback) return;
    // The press preceding QEvent::MouseButtonDblClick has already confirmed
    // the exact candidate selected by the user. Re-picking below may rebuild
    // or reorder an overlapping candidate list and reset active_candidate to
    // zero; opening that new first item edited a different container. Keep
    // the confirmed identity and use the refreshed list only as a fallback
    // when no press candidate exists.
    const auto pressed_candidate = impl_->confirmed_candidate;
    update_candidates(event->position());
    if (pressed_candidate) {
        impl_->double_confirmation_callback(*pressed_candidate);
        event->accept();
    } else if (!impl_->candidates.empty()) {
        impl_->double_confirmation_callback(
            impl_->candidates[impl_->active_candidate]);
        event->accept();
    }
}

void MeshView::mouseMoveEvent(QMouseEvent* event) {
    if(impl_->symbol_contact_drag&&(event->buttons()&Qt::LeftButton)){
        if((event->position()-impl_->symbol_contact_drag_start).manhattanLength()>=QApplication::startDragDistance())impl_->symbol_contact_moved=true;
        if(impl_->symbol_contact_moved)update_candidates(event->position());update();event->accept();return;
    }
    if(impl_->symbol_drag&&(event->buttons()&Qt::LeftButton)) {
        const auto handle=*impl_->symbol_drag;const auto ray=ray_at(event->position());
        if(ray&&impl_->symbol_handle_move) {
            const auto n=handle.normal;const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
            const auto denominator=dot(n,ray->second);
            if(std::abs(denominator)>1e-10) {
                const kernel::Vec3 offset{handle.anchor.x-ray->first.x,handle.anchor.y-ray->first.y,handle.anchor.z-ray->first.z};
                const auto t=dot(offset,n)/denominator;
                const auto moved=kernel::drag_annotation(handle.annotation,handle.right,handle.up,handle.left,true,handle.index,{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,ray->first.z+t*ray->second.z},true);
                impl_->symbol_handle_move(handle.id,handle.index,moved.grip,moved.shelf_length);
            }
        }
        event->accept();return;
    }
    if(impl_->layout_drag&&(event->buttons()&Qt::LeftButton)) {
        auto& drag=*impl_->layout_drag;const auto delta=event->position()-drag.start;
        if(!drag.moved&&delta.manhattanLength()<QApplication::startDragDistance()){event->accept();return;}
        const auto& d=drag.shown;
        const bool angular=d.kind==kernel::ViewerDimensionKind::Angular;
        const auto u=kernel::dimension_measurement_direction(d);
        const auto v=kernel::dimension_unit(kernel::dimension_cross(d.plane_normal,u));
        const auto mvp=impl_->projection(width(),height())*impl_->view();
        const auto project=[&](kernel::Vec3 p){auto q=mvp*QVector4D(p.x,p.y,p.z,1);if(std::abs(q.w())>1e-9)q/=q.w();return QPointF((q.x()+1)*width()/2.,(1-q.y())*height()/2.);};
        const auto o=project(d.witness_first),a=project(kernel::dimension_add(d.witness_first,u))-o,b=project(kernel::dimension_add(d.witness_first,v))-o;
        const auto movement=dimension_plane_drag(delta,a,b);
        if(!movement){event->accept();return;}
        const double along=movement->x(),outward=movement->y();
        std::optional<kernel::Vec3> text_grip;
        const bool polar=d.kind==kernel::ViewerDimensionKind::Radius||d.kind==kernel::ViewerDimensionKind::Diameter||
            (angular&&!impl_->active_sketch_owner_id.empty());
        if(drag.handle==0) {
            const auto label=d.label_position.value_or(kernel::dimension_scale(kernel::dimension_add(d.line_first,d.line_second),.5));
            const auto i=drag.candidate.geometry_index;
            // Use the original rendered grip, not the source label (automatic
            // outside placement can put these far apart in an oblique view).
            const auto text=dimension_label_text(d);
            const auto initial_grip=dimension_text_presentation(d,project,font(),text,.5*logicalDpiX()/25.4,1.5,10,3,false,1.5*logicalDpiX()/25.4,impl_->active_sketch_owner_id.empty()).handles[0];
            const auto correction=initial_grip-project(label);
            if(const auto offset=dimension_plane_drag(correction,a,b)) {
                text_grip=kernel::dimension_add(label,kernel::dimension_add(kernel::dimension_scale(u,offset->x()),kernel::dimension_scale(v,offset->y())));
            }
        }
        drag.current=kernel::dragged_dimension_layout(drag.shown,drag.bounds,drag.initial,drag.handle,along,outward,
            angular&&!impl_->active_sketch_owner_id.empty(),polar?text_grip:std::nullopt);
        if(!polar&&text_grip){
            const auto label=d.label_position.value_or(kernel::dimension_scale(kernel::dimension_add(d.line_first,d.line_second),.5));
            const auto correction=kernel::dimension_sub(*text_grip,label);
            drag.current.text_along+=kernel::dimension_dot(correction,u);drag.current.text_outward+=kernel::dimension_dot(correction,v);
        }
        auto display=kernel::layout_dimension(drag.source,drag.bounds,drag.current);const auto i=drag.candidate.geometry_index;
        if(i<impl_->mesh.dimensions.size())impl_->mesh.dimensions[i]=std::move(display);else impl_->transient_dimensions[i-impl_->mesh.dimensions.size()]=std::move(display);
        drag.moved=true;update();event->accept();return;
    }
    const QPoint current = event->position().toPoint();
    const QPoint movement = current - impl_->last_pointer;
    impl_->last_pointer = current;
    if ((event->buttons() & Qt::LeftButton) && impl_->sketch_box_start) {
        impl_->sketch_box_end = event->position();
        event->accept();
        update();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) &&
        impl_->dragged_extent_manipulator &&
        !impl_->dragged_extent_key.empty()) {
        const auto& handle = *impl_->dragged_extent_manipulator;
        const double norm = std::sqrt(handle.direction.x * handle.direction.x +
            handle.direction.y * handle.direction.y +
            handle.direction.z * handle.direction.z);
        if (norm > 1.0e-12 && impl_->extent_update_callback) {
            const zima::kernel::Vec3 direction{handle.direction.x / norm,
                handle.direction.y / norm, handle.direction.z / norm};
            const auto project = [&](const zima::kernel::Vec3& point) {
                QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                    QVector4D(point.x, point.y, point.z, 1.0F);
                if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                    (1.0F - clip.y()) * height() / 2.0F);
            };
            const QPointF screen_origin = project(handle.origin);
            const QPointF screen_unit = project({handle.origin.x + direction.x,
                handle.origin.y + direction.y, handle.origin.z + direction.z});
            const QPointF axis = screen_unit - screen_origin;
            const double pixels_squared = axis.x() * axis.x() + axis.y() * axis.y();
            if (pixels_squared > 1.0e-9) {
                const QPointF offset = event->position() - screen_origin;
                const double coordinate =
                    (offset.x() * axis.x() + offset.y() * axis.y()) /
                    pixels_squared;
                impl_->extent_update_callback(
                    impl_->dragged_extent_key, coordinate);
            }
        }
        event->accept();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && impl_->command_gesture_active) {
        if (impl_->command_gesture_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->command_gesture_update_callback(ray->first, ray->second);
        }
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && impl_->pending_dimension_drag) {
        if ((event->position() - impl_->dimension_drag_press_position).manhattanLength()
            < QApplication::startDragDistance()) {
            event->accept();
            return;
        }
        const auto candidate = *impl_->pending_dimension_drag;
        impl_->pending_dimension_drag.reset();
        const auto ray = ray_at(impl_->dimension_drag_press_position);
        impl_->drag_active = impl_->drag_begin_callback && ray &&
            impl_->drag_begin_callback(candidate, ray->first, ray->second);
    }
    if ((event->buttons() & Qt::LeftButton) && impl_->drag_active) {
        if (impl_->drag_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->drag_update_callback(ray->first, ray->second);
        }
        return;
    }
    if (event->buttons() & Qt::MiddleButton) {
        if ((current - impl_->middle_press_position).manhattanLength() > 2) {
            impl_->middle_dragged = true;
        }
        if (event->buttons().testFlag(Qt::RightButton)) {
            if (impl_->fly_navigation_enabled) {
                const auto inverse = impl_->orientation.inverted();
                const QVector3D right = inverse.rotatedVector(
                    QVector3D(1.0F, 0.0F, 0.0F));
                const QVector3D up = inverse.rotatedVector(
                    QVector3D(0.0F, 1.0F, 0.0F));
                const float distance_per_pixel = 2.0F * impl_->view_scale /
                    std::max(1, height());
                impl_->fly_position +=
                    -right * movement.x() * distance_per_pixel +
                    up * movement.y() * distance_per_pixel;
            } else {
                impl_->pan_pixels += QPointF(movement.x(), movement.y());
            }
        } else {
            constexpr float degrees_per_pixel = 0.22F;
            const auto horizontal = QQuaternion::fromAxisAndAngle(
                0.0F, 1.0F, 0.0F, movement.x() * degrees_per_pixel);
            const auto vertical = QQuaternion::fromAxisAndAngle(
                1.0F, 0.0F, 0.0F, movement.y() * degrees_per_pixel);
            impl_->orientation = (vertical * horizontal * impl_->orientation).normalized();
        }
        impl_->candidates.clear();
        update();
        return;
    }
    const bool command_wants_next_candidate =
        static_cast<bool>(impl_->candidate_filter) && impl_->advance_selection_on_hover;
    if (event->buttons() == Qt::NoButton &&
        (!impl_->confirmed_candidate || command_wants_next_candidate)) {
        // A command-local selection filter means that the previous click was
        // an input step, not a terminal ordinary selection.  Never let a
        // confirmed candidate left by that press suppress hover for the next
        // required reference (for example the second line/axis of an angular
        // dimension). Ordinary selection has no filter and retains the usual
        // confirmed-selection behavior.
        if (command_wants_next_candidate && impl_->confirmed_candidate) {
            impl_->confirmed_candidate.reset();
            impl_->selected_container_origin_id.clear();
            impl_->candidates.clear();
            impl_->active_candidate = 0;
        }
        std::string hovered;
        for (const auto& handle : impl_->extent_manipulators) {
            const double norm = std::sqrt(handle.direction.x * handle.direction.x +
                handle.direction.y * handle.direction.y +
                handle.direction.z * handle.direction.z);
            if (norm > 1.0e-12) {
                const zima::kernel::Vec3 direction{handle.direction.x / norm,
                    handle.direction.y / norm, handle.direction.z / norm};
                const auto project = [&](const zima::kernel::Vec3& point) {
                    QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                        QVector4D(point.x, point.y, point.z, 1.0F);
                    if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                    return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                        (1.0F - clip.y()) * height() / 2.0F);
                };
                const zima::kernel::Vec3 endpoint{
                    handle.origin.x + direction.x * handle.length,
                    handle.origin.y + direction.y * handle.length,
                    handle.origin.z + direction.z * handle.length};
                const double start_distance =
                    QLineF(event->position(), project(handle.origin)).length();
                const double end_distance = handle.point_only
                    ? std::numeric_limits<double>::infinity()
                    : QLineF(event->position(), project(endpoint)).length();
                if (std::min(start_distance, end_distance) <=
                        12.0 * devicePixelRatioF()) {
                    hovered = start_distance <= end_distance
                        ? handle.start_key : handle.end_key;
                }
            }
        }
        if (hovered != impl_->hovered_extent_key) {
            impl_->hovered_extent_key = std::move(hovered);
            update();
        }
        update_candidates(event->position());
        const auto picked_revision=impl_->base_mesh_revision;
        const auto picked_overlay_revision=impl_->picker_overlay_revision;
        const bool has_pointer_callback=static_cast<bool>(impl_->world_pointer_callback);
        if (has_pointer_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->world_pointer_callback(ray->first, ray->second);
        }
        // A live command preview may rebuild the mesh from inside the pointer
        // callback. set_mesh() necessarily invalidates the candidate indices,
        // but without this second pass it also erased the just-computed hover
        // before the frame was painted. Re-evaluate against the final mesh at
        // the same pointer position so the next line/axis remains visibly
        // green while an interactive dimension follows the cursor.
        if (command_wants_next_candidate && has_pointer_callback &&
            (!impl_->active_sketch_owner_id.empty() || picked_revision!=impl_->base_mesh_revision ||
             picked_overlay_revision!=impl_->picker_overlay_revision || impl_->candidates.empty())) {
            update_candidates(event->position());
        }
    }
}

void MeshView::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()==Qt::LeftButton&&impl_->symbol_contact_drag){
        impl_->symbol_contact_drag=false;
        if(impl_->symbol_contact_moved&&!impl_->candidates.empty()){impl_->confirmed_candidate=impl_->candidates[impl_->active_candidate];notify_confirmation();}
        event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&impl_->symbol_drag){impl_->symbol_drag.reset();event->accept();return;}
    if (event->button() == Qt::LeftButton) impl_->pending_dimension_drag.reset();
    if(impl_->layout_drag&&event->button()==Qt::RightButton){event->accept();return;}
    if(event->button()==Qt::LeftButton&&impl_->layout_drag){auto drag=*impl_->layout_drag;impl_->layout_drag.reset();if(drag.moved&&impl_->dimension_layout_commit)impl_->dimension_layout_commit(drag.source.reference,drag.current);event->accept();return;}
    if (event->button() == Qt::LeftButton && impl_->sketch_box_start) {
        const QPointF start = *impl_->sketch_box_start;
        const QPointF end = event->position();
        const QRectF rectangle(start, end);
        const QRectF normalized = rectangle.normalized();
        const bool crossing = end.x() < start.x();
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip =
                (impl_->projection(width(), height()) * impl_->view()) *
                QVector4D(point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                (1.0F - clip.y()) * height() / 2.0F);
        };
        const std::array borders{
            QLineF(normalized.topLeft(), normalized.topRight()),
            QLineF(normalized.topRight(), normalized.bottomRight()),
            QLineF(normalized.bottomRight(), normalized.bottomLeft()),
            QLineF(normalized.bottomLeft(), normalized.topLeft())};
        std::vector<ViewerCandidate> selected;
        if (normalized.width() >= 3.0 || normalized.height() >= 3.0) {
            for (std::size_t edge_index = 0;
                 edge_index < impl_->mesh.edges.size(); ++edge_index) {
                const auto& edge = impl_->mesh.edges[edge_index];
                if (!impl_->active_sketch_owner_id.empty() &&
                    edge.reference.owner_id !=
                        impl_->active_sketch_owner_id) continue;
                if (edge.points.empty()) continue;
                CandidateKind kind;
                if (edge.reference.semantic_key.starts_with("segment:"))
                    kind = CandidateKind::SketchSegment;
                else if (edge.reference.semantic_key.starts_with("circle:") ||
                         edge.reference.semantic_key.starts_with("arc:") ||
                         edge.reference.semantic_key.starts_with("corner_radius:") ||
                         edge.reference.semantic_key.starts_with("ellipse:") ||
                         edge.reference.semantic_key.starts_with("elliptical_arc:") ||
                         edge.reference.semantic_key.starts_with("bspline:"))
                    kind = CandidateKind::SketchCurve;
                else if (edge.reference.semantic_key.starts_with("text:"))
                    kind = CandidateKind::SketchText;
                else if (edge.reference.semantic_key.starts_with("external_edge:") ||
                         edge.reference.semantic_key.starts_with("external_axis:") ||
                         edge.reference.semantic_key.starts_with("external_face:"))
                    kind = CandidateKind::SketchExternalReference;
                else
                    continue;
                std::vector<QPointF> screen;
                screen.reserve(edge.points.size());
                std::transform(edge.points.begin(), edge.points.end(),
                    std::back_inserter(screen), project);
                const bool contained = std::all_of(screen.begin(), screen.end(),
                    [&](const auto& point) { return normalized.contains(point); });
                bool crossed = crossing && std::any_of(screen.begin(), screen.end(),
                    [&](const auto& point) { return normalized.contains(point); });
                if (crossing && !crossed) {
                    for (std::size_t index = 1;
                         index < screen.size() && !crossed; ++index) {
                        const QLineF segment(screen[index - 1], screen[index]);
                        crossed = std::any_of(borders.begin(), borders.end(),
                            [&](const auto& border) {
                                return segment.intersects(border, nullptr) ==
                                    QLineF::BoundedIntersection;
                            });
                    }
                }
                if (contained || crossed) {
                    selected.push_back({kind, 0.0, edge_index,
                        edge.reference.owner_id,
                        edge.reference.semantic_key,
                        edge.reference.instance_path, CandidateGeometry::Display});
                }
            }
            for (std::size_t point_index = 0;
                 point_index < impl_->mesh.points.size(); ++point_index) {
                const auto& point = impl_->mesh.points[point_index];
                if (!impl_->active_sketch_owner_id.empty() &&
                    point.reference.owner_id !=
                        impl_->active_sketch_owner_id) continue;
                const bool external_point =
                    point.reference.semantic_key.starts_with("external_point:");
                if (!point.reference.semantic_key.starts_with("point:") &&
                    !external_point) continue;
                if (normalized.contains(project(point.position))) {
                    selected.push_back({external_point
                            ? CandidateKind::SketchExternalReference
                            : CandidateKind::SketchPoint,
                        0.0, point_index,
                        point.reference.owner_id, point.reference.semantic_key,
                        point.reference.instance_path, CandidateGeometry::Display});
                }
            }
        }
        impl_->sketch_box_start.reset();
        impl_->sketch_box_end.reset();
        std::erase_if(selected, [&](const auto& candidate) {
            return !matches_selection_filter(candidate, impl_->selection_filter);
        });
        if (impl_->sketch_box_selection_callback) {
            const bool additive =
                event->modifiers().testFlag(Qt::ControlModifier) ||
                event->modifiers().testFlag(Qt::ShiftModifier);
            if (!additive) {
                impl_->sketch_box_selected_candidates = selected;
            } else {
                for (const auto& candidate : selected) {
                    const auto existing = std::find(
                        impl_->sketch_box_selected_candidates.begin(),
                        impl_->sketch_box_selected_candidates.end(), candidate);
                    if (existing == impl_->sketch_box_selected_candidates.end()) {
                        impl_->sketch_box_selected_candidates.push_back(candidate);
                    } else {
                        impl_->sketch_box_selected_candidates.erase(existing);
                    }
                }
            }
            impl_->sketch_box_selection_callback(
                std::move(selected), additive);
        }
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::LeftButton &&
        !impl_->dragged_extent_key.empty()) {
        impl_->dragged_extent_key.clear();
        impl_->dragged_extent_manipulator.reset();
        if (impl_->extent_end_callback) impl_->extent_end_callback();
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::MiddleButton && impl_->middle_double_clicked) {
        impl_->middle_double_clicked = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && !impl_->middle_dragged) {
        impl_->last_pointer = event->position().toPoint();
        const bool handled = confirm_current_pointer();
        if (handled) {
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && impl_->command_gesture_active) {
        if (impl_->command_gesture_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->command_gesture_update_callback(ray->first, ray->second);
        }
        impl_->command_gesture_active = false;
        if (impl_->command_gesture_end_callback) {
            impl_->command_gesture_end_callback();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && impl_->drag_active) {
        impl_->drag_active = false;
        if (impl_->drag_end_callback) impl_->drag_end_callback();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void MeshView::wheelEvent(QWheelEvent* event) {
    const float steps = event->angleDelta().y() / 120.0F;
    if (impl_->fly_navigation_enabled &&
        impl_->projection_mode == ProjectionMode::Perspective) {
        const QVector3D forward = impl_->orientation.inverted().rotatedVector(
            QVector3D(0.0F, 0.0F, -1.0F));
        const float distance = std::max(impl_->radius * 0.12F,
            impl_->view_scale * 0.08F);
        // Positive wheel steps widen the ordinary camera's view scale.
        // Move backward in fly mode to preserve that same zoom direction.
        impl_->fly_position -= forward * steps * distance;
        impl_->candidates.clear();
        update();
        event->accept();
        return;
    }
    const float old_scale = impl_->view_scale;
    impl_->view_scale *= std::pow(1.15F, steps);
    impl_->view_scale = std::clamp(
        impl_->view_scale, impl_->radius * 0.001F, impl_->radius * 1000.0F);
    if (old_scale > 0.0F) {
        const float zoom_ratio = old_scale / impl_->view_scale;
        const QPointF cursor = event->position();
        const QPointF center(width() * 0.5, height() * 0.5);
        impl_->pan_pixels = cursor - center -
            (cursor - center - impl_->pan_pixels) * zoom_ratio;
    }
    impl_->candidates.clear();
    impl_->rebuild_persisted_reference_mesh();
    update();
    event->accept();
}

void MeshView::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Escape){impl_->symbol_drag.reset();impl_->symbol_contact_drag=false;}
    if(event->key()==Qt::Key_Escape&&impl_->layout_drag){auto drag=*impl_->layout_drag;impl_->layout_drag.reset();const auto i=drag.candidate.geometry_index;if(i<impl_->mesh.dimensions.size())impl_->mesh.dimensions[i]=drag.shown;else if(i-impl_->mesh.dimensions.size()<impl_->transient_dimensions.size())impl_->transient_dimensions[i-impl_->mesh.dimensions.size()]=drag.shown;update();event->accept();return;}
    if (!impl_->fly_navigation_enabled) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    const auto inverse = impl_->orientation.inverted();
    const QVector3D forward = inverse.rotatedVector(
        QVector3D(0.0F, 0.0F, -1.0F));
    const QVector3D right = inverse.rotatedVector(
        QVector3D(1.0F, 0.0F, 0.0F));
    const QVector3D up = inverse.rotatedVector(
        QVector3D(0.0F, 1.0F, 0.0F));
    QVector3D direction;
    switch (event->key()) {
        case Qt::Key_W:
        case Qt::Key_Up: direction = forward; break;
        case Qt::Key_S:
        case Qt::Key_Down: direction = -forward; break;
        case Qt::Key_A:
        case Qt::Key_Left: direction = -right; break;
        case Qt::Key_D:
        case Qt::Key_Right: direction = right; break;
        case Qt::Key_Q: direction = -up; break;
        case Qt::Key_E: direction = up; break;
        default:
            QOpenGLWidget::keyPressEvent(event);
            return;
    }
    const float multiplier = event->modifiers().testFlag(Qt::ShiftModifier)
        ? 4.0F : 1.0F;
    impl_->fly_position += direction *
        std::max(impl_->radius * 0.04F, 0.05F) * multiplier;
    impl_->candidates.clear();
    update();
    event->accept();
}

}  // namespace zima::viewer
