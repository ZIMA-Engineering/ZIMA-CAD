#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

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
    void set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label);
    [[nodiscard]] zima::document::ConstructionDefinition current_definition() const;

protected:
    bool submit() override;

private:
    zima::document::ConstructionObject initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> origin_{};
    std::array<QDoubleSpinBox*, 3> direction_{};
    QDoubleSpinBox* display_size_{};
    QDoubleSpinBox* offset_{};
    QComboBox* definition_{};
    std::array<QPushButton*, 3> reference_buttons_{};
    std::vector<zima::document::ConstructionReference> references_;
    ReferenceRequestCallback reference_request_;
    PreviewCallback preview_;
    QLabel* error_{};
    void refresh_definition_fields();
    [[nodiscard]] zima::document::ConstructionObject current_value() const;
    void notify_preview();
};

}  // namespace zima::app
