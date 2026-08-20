#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <QString>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace zima::app {

class ConstructionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::document::ConstructionObject)>;

    ConstructionPropertiesDialog(
        const zima::document::ConstructionObject& initial, bool edit_mode,
        CommitCallback commit, QWidget* parent);
    using ReferenceRequestCallback = std::function<void(std::size_t)>;
    using PreviewCallback = std::function<void(zima::document::ConstructionObject)>;
    void set_reference_request_callback(ReferenceRequestCallback callback);
    void set_preview_callback(PreviewCallback callback);
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label,
        zima::document::ConstructionDefinition definition);
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label);
    [[nodiscard]] zima::document::ConstructionDefinition current_definition() const;
    [[nodiscard]] zima::document::ConstructionKind construction_kind() const;
    [[nodiscard]] const std::string& construction_id() const;
    void set_remaining_translation_dof(int dof);
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution);

protected:
    bool submit() override;

private:
    zima::document::ConstructionObject initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> origin_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QComboBox* direction_combo_{};
    QDoubleSpinBox* display_size_{};
    QDoubleSpinBox* offset_{};
    QComboBox* definition_{};
    std::array<QPushButton*, 3> reference_buttons_{};
    QTableWidget* reference_table_{};
    QTableWidget* orientation_table_{};
    std::array<QComboBox*, 2> orientation_roles_{};
    std::vector<zima::document::ConstructionReference> orientation_references_;
    std::vector<QString> orientation_labels_;
    QLabel* reference_status_{};
    QLabel* dof_label_{};
    std::vector<zima::document::ConstructionReference> references_;
    std::vector<QString> reference_labels_;
    ReferenceRequestCallback reference_request_;
    PreviewCallback preview_;
    QLabel* error_{};
    int remaining_translation_dof_{3};
    void refresh_definition_fields();
    void refresh_reference_table();
    void refresh_orientation_table();
    void remove_reference(std::size_t index);
    [[nodiscard]] zima::document::ConstructionObject current_value() const;
    void notify_preview();
};

}  // namespace zima::app
