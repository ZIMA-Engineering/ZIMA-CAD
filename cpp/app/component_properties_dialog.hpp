#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <string>

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

    // Identity of the occurrence this dialog edits, used by the viewer to
    // gate free-component drag to the occurrence currently open for editing
    // (matching Python's `assembly_component_dialog`/`dialog.component`
    // guard in `_on_insertion_origin_dragged`).
    [[nodiscard]] const std::string& occurrence_id() const {
        return initial_.occurrence_id;
    }

    // Live-updates the translation spinboxes while a free-component drag is
    // in progress, without touching rotation fields or emitting a commit.
    void set_live_translation(double x, double y, double z);

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
