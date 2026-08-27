#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <string>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTableWidget;

namespace zima::ui {
class ReferenceCellItem;
}  // namespace zima::ui

namespace zima::app {

// Embedded placement-reference table for an Assembly component, matching
// the Python reference design (AssemblyComponentPropertiesDialog, app.py:
// 6850-6999): up to 3 rows are entered directly in this dialog instead of
// via separate Plane/Axis/Point/Angle Mate commands and a "Vazby" tree
// branch. Each row picks a component-side reference (geometry on the
// occurrence being placed, in its own un-transformed source-body frame) and
// a target-side reference (geometry on the assembly origin or on another
// already-placed component), a mate type (reusing the existing
// zima::assembly::MateKind values), an offset/angle value, and a FLIP
// toggle (zima::ui::build_reference_row_flip_button) with the same
// semantics as zima::document::ConstructionReference::flip.
class ComponentPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::assembly::PartOccurrence)>;
    // Requests the viewer accept the next pick for placement-reference row
    // `index`'s `component_side` cell (true = component-side/moving
    // reference, false = target-side/fixed reference).
    using ReferenceRequestCallback = std::function<void(std::size_t index, bool component_side)>;

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

    void set_reference_request_callback(ReferenceRequestCallback callback);
    // Assigns the picked reference to row `index`'s component-side or
    // target-side cell and refreshes that row's display.
    void set_placement_reference(std::size_t index, bool component_side,
        zima::assembly::MateReference reference, const QString& label);
    [[nodiscard]] const std::vector<zima::assembly::ComponentPlacementReference>&
        placement_references() const { return placement_references_; }

protected:
    bool submit() override;

private:
    zima::assembly::PartOccurrence initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    QLabel* error_{};

    QTableWidget* placement_table_{};
    std::vector<zima::assembly::ComponentPlacementReference> placement_references_;
    std::array<zima::ui::ReferenceCellItem*, 3> component_items_{};
    std::array<zima::ui::ReferenceCellItem*, 3> target_items_{};
    std::array<QComboBox*, 3> mate_type_combos_{};
    std::array<QDoubleSpinBox*, 3> offset_fields_{};
    std::array<QWidget*, 3> flip_buttons_{};
    std::array<QToolButton*, 3> limit_buttons_{};
    ReferenceRequestCallback reference_request_;

    void refresh_placement_table();
    void remove_placement_reference(std::size_t index);
};

}  // namespace zima::app
