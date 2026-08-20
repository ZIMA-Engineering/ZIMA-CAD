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
class QPushButton;
class QWidget;

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
    void set_profile_pick_request(std::function<void(bool)> callback);
    void set_edit_sketch_callback(std::function<void(std::string)> callback);
    void set_preview_callback(
        std::function<void(const zima::document::HistoryContainer&)> callback);

protected:
    bool submit() override;

private:
    zima::document::HistoryContainer initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* operation_{};
    QPushButton* add_operation_button_{};
    QPushButton* subtract_operation_button_{};
    QDoubleSpinBox* length_{};
    QDoubleSpinBox* width_{};
    QDoubleSpinBox* height_{};
    QDoubleSpinBox* radius_{};
    QDoubleSpinBox* top_radius_{};
    QDoubleSpinBox* top_offset_{};
    QComboBox* extrusion_direction_{};
    QComboBox* extrusion_extent_{};
    QLabel* extrusion_target_{};
    QLineEdit* profile_source_{};
    QPushButton* profile_pick_button_{};
    QPushButton* profile_reset_button_{};
    QPushButton* own_sketch_button_{};
    QLineEdit* profile_status_{};
    QComboBox* result_type_{};
    QPushButton* result_type_switch_button_{};
    QDoubleSpinBox* thin_thickness_{};
    QComboBox* thin_mode_{};
    QPushButton* thin_mode_switch_button_{};
    QWidget* thin_thickness_row_{};
    QWidget* thin_mode_row_{};
    QComboBox* extent_mode_{};
    QPushButton* extent_switch_button_{};
    QPushButton* direction_flip_button_{};
    QDoubleSpinBox* forward_length_{};
    QDoubleSpinBox* reverse_length_{};
    QComboBox* forward_end_condition_{};
    QComboBox* reverse_end_condition_{};
    QLineEdit* forward_end_target_{};
    QLineEdit* reverse_end_target_{};
    QPushButton* forward_end_targets_button_{};
    QPushButton* reverse_end_targets_button_{};
    QWidget* reverse_end_row_{};
    std::array<std::array<double, 2>, 3> revolution_extent_values_{{
        {{360.0, 360.0}}, {{45.0, 45.0}}, {{45.0, 45.0}}}};
    int revolution_previous_extent_index_{};
    std::string active_end_target_side_{"forward"};
    std::function<void()> extrusion_target_request_;
    std::function<void(bool)> profile_pick_request_;
    std::function<void(std::string)> edit_sketch_;
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
