#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <array>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class BoxPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::document::HistoryContainer)>;

    BoxPropertiesDialog(const zima::document::HistoryContainer& initial,
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
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QLabel* error_{};
};

}  // namespace zima::app
