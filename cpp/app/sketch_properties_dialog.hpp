#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class SketchPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::sketcher::Sketch)>;

    SketchPropertiesDialog(
        zima::sketcher::Sketch initial, bool edit_mode,
        CommitCallback commit, QWidget* parent);

protected:
    bool submit() override;

private:
    zima::sketcher::Sketch initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* plane_{};
    QDoubleSpinBox* offset_{};
    QLabel* error_{};
};

}  // namespace zima::app
