#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <memory>
#include <array>
#include <optional>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QMouseEvent;
class QWheelEvent;
class QQuaternion;namespace zima::viewer {

enum class DisplayMode {
    Wire,
    HiddenEdges,
    NoHiddenEdges,
    ShadedWithEdges,
    Shaded,
};

enum class StandardView {
    Isometric,
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
};

enum class ReferenceVisibility {
    Origins,
    Points,
    Axes,
    Planes,
    Sketches,
};

struct ExtentManipulator {
    std::string start_key;
    std::string end_key;
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 direction;
    double length{};
};

class MeshView final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;
    void set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view = true);
    [[nodiscard]] std::array<float, 8> camera_state() const;
    void set_camera_state(const std::array<float, 8>& state);
    // Animates a full camera-state restore (orientation, pan, zoom),
    // matching Python's animate_camera_state used to restore custom saved
    // "Pohled kolmo" views.
    void animate_camera_state(const std::array<float, 8>& state);
    void fit_all();
    void set_display_mode(DisplayMode mode);
    [[nodiscard]] DisplayMode display_mode() const;
    void set_standard_view(StandardView view);
    void set_view_direction(const zima::kernel::Vec3& direction);
    void set_view_direction(const zima::kernel::Vec3& direction, float roll_degrees);
    void set_reference_visibility(ReferenceVisibility reference, bool visible);
    [[nodiscard]] bool reference_visible(ReferenceVisibility reference) const;
    void set_editing_origin_visible(bool visible);
    void set_selection_contract(std::vector<CandidateKind> allowed_kinds);
    void set_candidate_filter(
        std::function<bool(const ViewerCandidate&)> candidate_filter);
    void confirm_container(const std::string& owner_id);
    void confirm_occurrence(const std::string& instance_path);
    void confirm_result_body();
    void confirm_origin(const std::string& owner_id,
        const std::string& instance_path);
    void confirm_reference(const std::string& owner_id,
        const std::string& semantic_key, const std::string& instance_path,
        CandidateKind kind);
    void clear_selection();
    void set_confirmation_callback(
        std::function<void(const ViewerCandidate&)> callback);
    void set_empty_confirmation_callback(std::function<void()> callback);
    void set_context_menu_callback(
        std::function<void(const ViewerCandidate&, const QPoint&)> callback);
    void set_world_click_callback(std::function<bool(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback);
    void set_world_pointer_callback(std::function<void(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback);
    void set_command_gesture_callbacks(
        std::function<bool(
            const std::optional<ViewerCandidate>&,
            const zima::kernel::Vec3&, const zima::kernel::Vec3&)> begin,
        std::function<void(
            const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
        std::function<void()> end);
    void set_short_middle_click_callback(std::function<bool()> callback);
    void set_double_middle_click_callback(std::function<bool()> callback);
    void set_sketch_box_selection(
        bool enabled,
        std::function<void(std::vector<ViewerCandidate>, bool)> callback = {});
    // Executes the same command confirmation path as a short middle click at
    // the last View pointer position. Used by view-focused Enter.
    bool confirm_current_pointer();
    bool refresh_current_pointer_preview();
    [[nodiscard]] QPoint last_pointer_position() const;
    [[nodiscard]] std::optional<double> candidate_dimension_value(
        const ViewerCandidate& candidate) const;
    [[nodiscard]] std::optional<QPoint> candidate_dimension_label_position(
        const ViewerCandidate& candidate) const;
    void set_empty_right_click_callback(std::function<bool()> callback);
    void set_single_candidate_right_click_callback(
        std::function<bool(const ViewerCandidate&)> callback);
    void set_candidate_right_click_callback(
        std::function<bool(const ViewerCandidate&, std::size_t, std::size_t)> callback);
    void reset_candidate_cycle();
    void set_double_confirmation_callback(
        std::function<void(const ViewerCandidate&)> callback);
    void set_candidate_drag_callbacks(
        std::function<bool(const ViewerCandidate&, const zima::kernel::Vec3&,
            const zima::kernel::Vec3&)> begin,
        std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
        std::function<void()> end);
    void set_transient_point_transform(
        std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)> transform);
    void set_transient_edges(std::vector<zima::kernel::ViewerEdge> edges);
    // Active Sketch input points are deliberately separate from model points:
    // they are transient, non-pickable and always rendered in inference orange.
    void set_transient_points(std::vector<zima::kernel::Vec3> points);
    void set_transient_labels(std::vector<std::pair<zima::kernel::Vec3,
        std::string>> labels);
    // Sketch placement cursor: white in free space, orange when a persisted
    // candidate or inference will create a relation on confirmation.
    void set_sketch_cursor(std::optional<zima::kernel::Vec3> point,
        bool snapped = false, std::string relation_label = {});
    void set_extent_manipulator(std::optional<ExtentManipulator> manipulator);
    void set_extent_manipulator_callbacks(
        std::function<void(const std::string&)> begin,
        std::function<void(const std::string&, double)> update,
        std::function<void()> end);
    // Per-edge highlight priority state, mirroring the Python widget's
    // frozenset-based bookkeeping (zima_cad/viewer.py _edge_display_color /
    // _edge_is_highlighted). Highest priority first: selected > object
    // overlay > hovered > feature preview > color override > base color.
    void set_edge_treatment_selection_edges(std::set<EdgeKey> edges);
    void set_feature_hover_edges(std::set<EdgeKey> edges);
    void set_feature_selected_edges(std::set<EdgeKey> edges);
    void set_feature_preview_owners(std::set<std::string> owner_ids);
    void set_constraint_reference_highlights(
        std::set<std::string> owner_ids, std::set<EdgeKey> edges);
    void set_assembly_reference_edges(std::set<EdgeKey> edges);
    void set_selected_container_contents(std::set<std::string> owner_ids);
    void set_object_overlay_main_edges(std::set<EdgeKey> edges);
    void set_edge_color_override(std::optional<QColor> color);
    void set_body_surface_colors(QColor default_color,
        std::map<std::string, QColor> instance_colors = {});
    [[nodiscard]] std::optional<ViewerCandidate> confirmed_candidate() const;
    [[nodiscard]] std::optional<ViewerCandidate> hovered_candidate() const;
    [[nodiscard]] std::vector<ViewerCandidate> selection_candidates_at(
        const QPointF& position) const;
    [[nodiscard]] std::optional<zima::kernel::ViewerEdge> candidate_edge(
        const ViewerCandidate& candidate) const;
    [[nodiscard]] std::optional<zima::kernel::ViewerPoint> candidate_point(
        const ViewerCandidate& candidate) const;
    [[nodiscard]] std::optional<zima::kernel::ViewerAxis> candidate_axis(
        const ViewerCandidate& candidate) const;
    [[nodiscard]] std::vector<zima::kernel::EdgeReference> tangent_edge_route(
        const ViewerCandidate& seed,
        double angular_tolerance_degrees = 35.0) const;
    // Outward unit normal of a Face candidate's triangle, in the same
    // document-space coordinates as candidate_edge/candidate_point. Used by
    // the perpendicular-to-face ("Pohled kolmo") view orientation command.
    [[nodiscard]] std::optional<zima::kernel::Vec3> candidate_face_normal(
        const ViewerCandidate& candidate) const;
    [[nodiscard]] double world_tolerance_for_pixels(double pixels) const;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void upload_mesh();
    void update_candidates(const QPointF& position);
    void notify_confirmation();
    void animate_orientation_to(const QQuaternion& target);
    // Animates a full camera-state restore (orientation, pan, zoom),
    // matching Python's animate_camera_state used to restore custom saved
    // "Pohled kolmo" views (as opposed to animate_orientation_to, which only
    // handles the 7 built-in standard/normal views and eases pan to zero).
    [[nodiscard]] std::optional<std::pair<zima::kernel::Vec3, zima::kernel::Vec3>>
        ray_at(const QPointF& position) const;
};

}  // namespace zima::viewer
