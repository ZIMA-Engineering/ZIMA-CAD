#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace zima::app {

class SketchDimensionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::sketcher::SketchDimension)>;

    SketchDimensionPropertiesDialog(
        zima::sketcher::SketchDimension initial, bool edit_mode,
        CommitCallback commit, QWidget* parent);

protected:
    bool submit() override;

private:
    zima::sketcher::SketchDimension initial_;
    CommitCallback commit_;
    QDoubleSpinBox* value_{};
    QCheckBox* lower_enabled_{};
    QCheckBox* driving_{};
    QDoubleSpinBox* lower_{};
    QCheckBox* upper_enabled_{};
    QDoubleSpinBox* upper_{};
    QLabel* error_{};
};

}  // namespace zima::app
