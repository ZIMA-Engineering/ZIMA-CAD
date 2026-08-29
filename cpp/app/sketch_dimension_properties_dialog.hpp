#pragma once

#include <zima/sketcher/sketch.hpp>
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

class SketchDimensionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::sketcher::SketchDimension)>;

    SketchDimensionPropertiesDialog(
        zima::sketcher::SketchDimension initial, bool edit_mode,
        CommitCallback commit, QWidget* parent,
        QString custom_title = {});

protected:
    bool submit() override;

private:
    zima::sketcher::SketchDimension initial_;
    CommitCallback commit_;
    QDoubleSpinBox* value_{};
    QFormLayout* form_{};
    QCheckBox* driving_{};
    QLineEdit* prefix_{};
    QLineEdit* suffix_{};
    QComboBox* tolerance_mode_{};
    QLineEdit* symmetric_tolerance_{};
    QLineEdit* single_tolerance_{};
    QLineEdit* upper_tolerance_{};
    QLineEdit* lower_tolerance_{};
    QLabel* error_{};
    void refresh_tolerance_fields();
};

}  // namespace zima::app
