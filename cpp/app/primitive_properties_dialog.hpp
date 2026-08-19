#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace zima::app {

class PrimitivePropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using LegacyCommitCallback =
        std::function<void(zima::document::HistoryContainer)>;
    using CommitCallback = std::function<void(
        zima::document::HistoryContainer, std::vector<std::string>)>;
    using AssemblyTarget = std::pair<std::string, std::string>;

    PrimitivePropertiesDialog(
        const zima::document::HistoryContainer& initial,
        bool edit_mode,
        bool allow_subtract,
        CommitCallback commit,
        QWidget* parent,
        std::vector<AssemblyTarget> assembly_targets = {},
        std::vector<std::string> selected_targets = {},
        bool assembly_cut_mode = false);
    PrimitivePropertiesDialog(
        const zima::document::HistoryContainer& initial,
        bool edit_mode,
        bool allow_subtract,
        LegacyCommitCallback commit,
        QWidget* parent);
    void set_extrusion_target(
        zima::kernel::FaceReference reference, zima::kernel::Vec3 origin,
        zima::kernel::Vec3 normal);
    void set_extrusion_surface_target(
        zima::kernel::FaceReference reference,
        std::vector<zima::kernel::Vec3> triangles);
    void set_extrusion_target_request(std::function<void()> callback);
    void set_preview_callback(
        std::function<void(const zima::document::HistoryContainer&)> callback);

protected:
    bool submit() override;

private:
    zima::document::HistoryContainer initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* operation_{};
    QDoubleSpinBox* length_{};
    QDoubleSpinBox* width_{};
    QDoubleSpinBox* height_{};
    QDoubleSpinBox* radius_{};
    QDoubleSpinBox* top_radius_{};
    QDoubleSpinBox* top_offset_{};
    QComboBox* extrusion_direction_{};
    QComboBox* extrusion_extent_{};
    QLabel* extrusion_target_{};
    std::function<void()> extrusion_target_request_;
    std::function<void(const zima::document::HistoryContainer&)> preview_;
    QComboBox* revolution_axis_{};
    QDoubleSpinBox* angle_{};
    QDoubleSpinBox* treatment_size_{};
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QLabel* error_{};
    QListWidget* assembly_targets_{};
    [[nodiscard]] zima::document::HistoryContainer values() const;
    void notify_preview();
};

}  // namespace zima::app
