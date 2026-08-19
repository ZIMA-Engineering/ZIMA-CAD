#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;

namespace zima::app {

class SketchTextPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using PreviewCallback = std::function<void(
        const std::optional<zima::sketcher::SketchText>&)>;
    using CommitCallback = std::function<void(zima::sketcher::SketchText)>;

    SketchTextPropertiesDialog(
        zima::sketcher::SketchText initial,
        std::optional<std::array<double, 2>> anchor,
        PreviewCallback preview, CommitCallback commit, QWidget* parent);

    void set_anchor(double x, double y);

protected:
    bool submit() override;

private:
    [[nodiscard]] zima::sketcher::SketchText build_text() const;
    void update_preview();

    zima::sketcher::SketchText initial_;
    std::optional<std::array<double, 2>> anchor_;
    PreviewCallback preview_;
    CommitCallback commit_;
    QPlainTextEdit* value_{};
    QDoubleSpinBox* height_{};
    QComboBox* horizontal_{};
    QComboBox* vertical_{};
    QComboBox* font_{};
    QComboBox* color_{};
    QDoubleSpinBox* angle_{};
    QCheckBox* flipped_{};
    QLabel* error_{};
};

}  // namespace zima::app
