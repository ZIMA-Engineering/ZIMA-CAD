#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class MatePropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::assembly::AssemblyMate)>;

    MatePropertiesDialog(
        zima::assembly::AssemblyMate initial,
        CommitCallback commit,
        QWidget* parent);

protected:
    bool submit() override;

private:
    zima::assembly::AssemblyMate initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QDoubleSpinBox* offset_{};
    QLabel* error_{};
};

}  // namespace zima::app
