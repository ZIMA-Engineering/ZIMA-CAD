#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class ComponentPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::assembly::PartOccurrence)>;

    ComponentPropertiesDialog(
        const zima::assembly::PartOccurrence& initial,
        CommitCallback commit,
        QWidget* parent);

protected:
    bool submit() override;

private:
    zima::assembly::PartOccurrence initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QLabel* error_{};
};

}  // namespace zima::app
