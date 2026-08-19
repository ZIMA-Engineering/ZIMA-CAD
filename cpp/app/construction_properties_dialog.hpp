#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class ConstructionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::document::ConstructionObject)>;

    ConstructionPropertiesDialog(
        const zima::document::ConstructionObject& initial, bool edit_mode,
        CommitCallback commit, QWidget* parent);

protected:
    bool submit() override;

private:
    zima::document::ConstructionObject initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> origin_{};
    std::array<QDoubleSpinBox*, 3> direction_{};
    QDoubleSpinBox* display_size_{};
    QLabel* error_{};
};

}  // namespace zima::app
