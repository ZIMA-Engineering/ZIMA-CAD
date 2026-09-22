#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <optional>
#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;

namespace zima::app {

void rebuild_sketch_text_contours(zima::sketcher::SketchText& text, bool y_up = false);

class SketchTextPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using PreviewCallback = std::function<void(
        const std::optional<zima::sketcher::SketchText>&)>;
    using CommitCallback = std::function<void(zima::sketcher::SketchText)>;

    SketchTextPropertiesDialog(
        zima::sketcher::SketchText initial,
        std::optional<std::array<double, 2>> anchor,
        PreviewCallback preview, CommitCallback commit, QWidget* parent, bool y_up = false, bool drawing_text = false, std::optional<std::map<std::string,std::string>> action_settings = std::nullopt);
    std::map<std::string,std::string> field_action() const;

    void set_anchor(double x, double y);
    void set_preview_anchor(double x, double y);
    [[nodiscard]] bool needs_anchor() const { return !anchor_; }

protected:
    bool submit() override;

private:
    [[nodiscard]] zima::sketcher::SketchText build_text(bool preview = false) const;
    void update_preview();

    zima::sketcher::SketchText initial_;
    std::optional<std::array<double, 2>> anchor_;
    std::optional<std::array<double, 2>> preview_anchor_;
    PreviewCallback preview_;
    CommitCallback commit_;
    QPlainTextEdit* value_{};
    QDoubleSpinBox* height_{};
    QComboBox* horizontal_{};
    QComboBox* vertical_{};
    QComboBox* font_{};
    QComboBox* color_{};
    QComboBox* mode_{};
    QDoubleSpinBox* angle_{};
    QCheckBox* flipped_{};
    QLabel* error_{};
    QComboBox* field_action_{};
    QComboBox* date_format_{};
    QPlainTextEdit* choices_{};
    QCheckBox* allow_custom_{};
    bool y_up_{};
    bool drawing_text_{};
};

}  // namespace zima::app
