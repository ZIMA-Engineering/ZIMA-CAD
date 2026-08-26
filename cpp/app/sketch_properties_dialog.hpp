#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace zima::app {

class SketchPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::sketcher::Sketch)>;

    // Identifies one candidate Plane construction container the Sketch may
    // be placed on instead of one of the three fixed XY/XZ/YZ planes --
    // `owner_id` is the container's entity_id (matches
    // Sketch::plane_reference_owner_id / ConstructionReference::owner_id
    // convention), `label` its display name.
    struct PlaneOption {
        std::string owner_id;
        QString label;
    };

    SketchPropertiesDialog(
        zima::sketcher::Sketch initial, bool edit_mode,
        std::vector<PlaneOption> plane_options,
        CommitCallback commit, QWidget* parent);

protected:
    bool submit() override;

private:
    zima::sketcher::Sketch initial_;
    std::vector<PlaneOption> plane_options_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* plane_{};
    QDoubleSpinBox* offset_{};
    QComboBox* plane_reference_{};
    QLabel* error_{};

    void update_plane_fields_enabled();
};

}  // namespace zima::app
