#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <memory>
#include <array>
#include <optional>
#include <functional>
#include <vector>

class QMouseEvent;
class QWheelEvent;

namespace zima::viewer {

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

class MeshView final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;
    void set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view = true);
    [[nodiscard]] std::array<float, 7> camera_state() const;
    void set_camera_state(const std::array<float, 7>& state);
    void fit_all();
    void set_display_mode(DisplayMode mode);
    [[nodiscard]] DisplayMode display_mode() const;
    void set_standard_view(StandardView view);
    void set_view_direction(const zima::kernel::Vec3& direction);
    void set_reference_visibility(ReferenceVisibility reference, bool visible);
    [[nodiscard]] bool reference_visible(ReferenceVisibility reference) const;
    void set_editing_origin_visible(bool visible);
    void set_selection_contract(std::vector<CandidateKind> allowed_kinds);
    void set_candidate_filter(
        std::function<bool(const ViewerCandidate&)> candidate_filter);
    void confirm_container(const std::string& owner_id);
    void confirm_occurrence(const std::string& instance_path);
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
    void set_double_confirmation_callback(
        std::function<void(const ViewerCandidate&)> callback);
    void set_candidate_drag_callbacks(
        std::function<bool(const ViewerCandidate&)> begin,
        std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
        std::function<void()> end);
    void set_transient_point_transform(
        std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)> transform);
    void set_transient_edges(std::vector<zima::kernel::ViewerEdge> edges);
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
    [[nodiscard]] std::optional<std::pair<zima::kernel::Vec3, zima::kernel::Vec3>>
        ray_at(const QPointF& position) const;
};

}  // namespace zima::viewer
