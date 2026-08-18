#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class PrimitivePropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::document::HistoryContainer)>;

    PrimitivePropertiesDialog(
        const zima::document::HistoryContainer& initial,
        bool edit_mode,
        bool allow_subtract,
        CommitCallback commit,
        QWidget* parent);

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
    QComboBox* extrusion_direction_{};
    QComboBox* revolution_axis_{};
    QDoubleSpinBox* angle_{};
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QLabel* error_{};
};

}  // namespace zima::app
