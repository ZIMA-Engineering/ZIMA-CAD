#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <memory>
#include <optional>
#include <functional>
#include <vector>

class QMouseEvent;
class QWheelEvent;

namespace zima::viewer {

class MeshView final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;
    void set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view = true);
    void fit_all();
    void set_selection_contract(std::vector<CandidateKind> allowed_kinds);
    void confirm_container(const std::string& owner_id);
    void confirm_occurrence(const std::string& instance_path);
    void clear_selection();
    void set_confirmation_callback(
        std::function<void(const ViewerCandidate&)> callback);
    void set_context_menu_callback(
        std::function<void(const ViewerCandidate&, const QPoint&)> callback);
    void set_world_click_callback(std::function<bool(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback);
    void set_world_pointer_callback(std::function<void(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback);
    void set_double_confirmation_callback(
        std::function<void(const ViewerCandidate&)> callback);
    void set_candidate_drag_callbacks(
        std::function<bool(const ViewerCandidate&)> begin,
        std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
        std::function<void()> end);
    void set_transient_edges(std::vector<zima::kernel::ViewerEdge> edges);
    [[nodiscard]] std::optional<ViewerCandidate> confirmed_candidate() const;

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
