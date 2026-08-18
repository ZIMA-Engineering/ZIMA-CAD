#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <memory>
#include <optional>
#include <vector>

class QMouseEvent;
class QWheelEvent;

namespace zima::viewer {

class MeshView final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MeshView(QWidget* parent = nullptr);
    ~MeshView() override;
    void set_mesh(zima::kernel::ViewerMesh mesh);
    void fit_all();
    void set_selection_contract(std::vector<CandidateKind> allowed_kinds);
    [[nodiscard]] std::optional<ViewerCandidate> confirmed_candidate() const;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void upload_mesh();
    void update_candidates(const QPointF& position);
};

}  // namespace zima::viewer
