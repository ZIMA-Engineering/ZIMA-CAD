#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <vector>

class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QSpinBox;

namespace zima::app {

class SketchBSplinePropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(
        unsigned, bool, const std::vector<std::array<double, 2>>&)>;

    SketchBSplinePropertiesDialog(
        unsigned degree, bool closed,
        std::vector<std::array<double, 2>> control_points,
        CommitCallback commit, QWidget* parent);

    void set_exact_geometry(bool source_linked);

protected:
    bool submit() override;

private:
    bool exact_geometry_{};
    CommitCallback commit_;
    std::vector<std::array<double, 2>> original_points_;
    std::vector<std::array<double, 2>> displayed_points_;
    QSpinBox* degree_{};
    QCheckBox* closed_{};
    std::vector<QDoubleSpinBox*> x_;
    std::vector<QDoubleSpinBox*> y_;
    QLabel* error_{};
};

}  // namespace zima::app
