#pragma once
#include <QWidget>
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/interchange/step.hpp>
#include <memory>

class QTreeWidget;
class QLabel;
class DXFInterface;
class DXFSceneView;
namespace zima::viewer { class MeshView; }
namespace zima::app {
struct StepPreviewNode {
    interchange::StepPart source;
    kernel::ViewerMesh mesh;
};
struct StepPreviewData { std::vector<StepPreviewNode> nodes; };
// Explicit, read-only import. No native documents, filesystem writes or tabs.
StepPreviewData load_step_preview(const QString& path);
class ModelPreviewTab final : public QWidget {
public:
    explicit ModelPreviewTab(const QString& path,QWidget* parent=nullptr);
    ~ModelPreviewTab() override;
    void load_dxf();
    void set_step(StepPreviewData data);
    QString path()const{return path_;}
private:
    QString path_;
    QTreeWidget* tree_{};
    QLabel* details_{};
    viewer::MeshView* model_{};
    DXFSceneView* drawing_{};
    std::unique_ptr<DXFInterface> dxf_;
    StepPreviewData step_;
    void update_model();
};
}
