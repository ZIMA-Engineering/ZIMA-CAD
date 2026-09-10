#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/kernel/dimension_layout.hpp>
class QTabWidget;
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace zima::app {
class DimensionTextFields;
class DimensionPlacementFields;

class SketchDimensionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::sketcher::SketchDimension)>;

    SketchDimensionPropertiesDialog(
        zima::sketcher::SketchDimension initial, bool edit_mode,
        CommitCallback commit, QWidget* parent,
        QString custom_title = {});

    void set_presentation(kernel::ViewerDimension,kernel::DimensionLayout,std::function<void(kernel::DimensionLayout)>);
    void set_dimension_identifier(const QString& identifier);

protected:
    bool submit() override;

private:
    zima::sketcher::SketchDimension initial_;
    CommitCallback commit_;
    QDoubleSpinBox* value_{};
    QFormLayout* form_{};
    QCheckBox* driving_{};
    QCheckBox* locked_{};
    QTabWidget* tabs_{};
    DimensionTextFields* text_fields_{};
    DimensionPlacementFields* placement_fields_{};
    std::function<void(kernel::DimensionLayout)> pending_layout_;
    QLabel* error_{};
};

}  // namespace zima::app
