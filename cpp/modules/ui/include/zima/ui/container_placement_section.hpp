#pragma once

#include <zima/document/part_document.hpp>

#include <array>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include <QObject>
#include <QString>

class QWidget;
class QLabel;
class QTableWidget;
class QVBoxLayout;
class QDoubleSpinBox;
class QPushButton;
class QToolButton;

namespace zima::ui {

class ReferenceCellItem;

// Shared "Umístění kontejneru" (container placement) implementation reused
// by every property dialog that lets the user pin a container's origin to
// up to three position references plus, for Plane-kind containers/features,
// a FRONT/TOP orientation frame -- matching the reference implementation's
// PointConstraintDialog base class, which AxisConstraintDialog /
// PlaneConstraintDialog / SolidConstraintDialog / ContainerPropertiesDialog
// all inherit from instead of re-implementing the same reference table,
// offset editing, DOF bookkeeping and viewer-highlight toggling themselves.
//
// A dialog embeds this section by constructing it with its own QWidget
// parent/content layout, then wires ReferenceRequestCallback (pick a new
// reference), PreviewChangedCallback (recompute + re-render the live
// preview) and, for Plane-kind containers, enables the orientation table.
class ContainerPlacementSection : public QObject {
public:
    using ReferenceRequestCallback = std::function<void(std::size_t)>;
    using ChangedCallback = std::function<void()>;
    using HighlightsChangedCallback = std::function<void()>;

    // `parent_widget` owns every widget created by this section (tables,
    // headings, labels); `layout` is the dialog's vertical content layout
    // the section immediately appends its adjacent "Umístění kontejneru"
    // and "Orientace kontejneru" blocks to. Dialogs no longer assemble
    // those two halves independently; one shared component owns their
    // ordering, behavior and visual style for every container kind.
    ContainerPlacementSection(QWidget* parent_widget, QVBoxLayout* layout,
        bool with_orientation, bool position_rows_can_define_rotation = false,
        int decimal_places = 3);

    // Inserts the (already constructed) DOF label into `layout` at its
    // current end. Call this once, at the position matching the reference
    // implementation's dialog for this container kind.
    void install_dof_label(QVBoxLayout* layout);
    // Shared numeric half of the placement contract. Every container uses
    // these fields; shape dialogs only add their own geometric parameters.
    void initialize_numeric_values(const zima::document::Placement& placement);
    [[nodiscard]] zima::document::Placement numeric_placement() const;
    void set_orientation_back(bool back);
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution);
    void set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained);
    void set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid = true);
    void set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state);
    [[nodiscard]] const std::array<QDoubleSpinBox*, 3>& translation_fields() const {
        return translation_;
    }
    [[nodiscard]] const std::array<QDoubleSpinBox*, 3>& rotation_fields() const {
        return rotation_;
    }
    [[nodiscard]] const std::array<QDoubleSpinBox*, 3>& rotation_offset_fields() const {
        return rotation_offset_;
    }
    // Reports that the initial local container frame was calculated from
    // the first placement reference. FRONT and TOP remain two stable,
    // independently editable local-plane slots; this state only supplies
    // their default display text and never locks the table.
    void set_orientation_locked(bool locked, const QString& source_label = {});
    // Display label of position row 0 ("1. <label>" with the index prefix
    // stripped), or empty if that row is not populated. Used to tell the
    // user which reference a locked orientation was derived from.
    [[nodiscard]] QString first_position_reference_label() const;

    void set_reference_request_callback(ReferenceRequestCallback callback);
    // Invoked whenever a reference/offset/orientation-role edit changes the
    // resolved placement and the owning dialog should recompute its preview.
    void set_changed_callback(ChangedCallback callback);
    void set_highlights_changed_callback(HighlightsChangedCallback callback);

    // Seeds the section from a container's persisted references, splitting
    // genuine orientation-only FRONT/TOP entries from plain position
    // references, exactly like each dialog's constructor used to do inline.
    void initialize_from_references(
        const std::vector<zima::document::ConstructionReference>& references,
        const std::function<QString(const std::string&)>& label_for_semantic);

    // Assigns reference `index` (0-2 = position row, 3-4 = FRONT/TOP
    // orientation row when `with_orientation` is enabled). Position and
    // orientation rows are now always independent: if the user wants a
    // reference to drive FRONT/TOP, they must pick it into the dedicated
    // orientation table explicitly. Returns false (and sets `error_text`)
    // on a duplicate-reference conflict.
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference, const QString& label,
        QString* error_text, bool derive_orientation = true);

    void set_remaining_translation_dof(int dof);
    void set_remaining_rotation_dof(int dof);
    [[nodiscard]] int remaining_translation_dof() const { return remaining_translation_dof_; }
    [[nodiscard]] int remaining_rotation_dof() const { return remaining_rotation_dof_; }

    [[nodiscard]] const std::vector<zima::document::ConstructionReference>&
        references() const { return references_; }
    [[nodiscard]] const std::vector<zima::document::ConstructionReference>&
        orientation_references() const { return orientation_references_; }

    // All references (position rows truncated to `required`, plus every
    // orientation reference appended), matching each dialog's
    // current_value()/submit() assembly of the persisted reference list.
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        combined_references(std::size_t required) const;

    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        references_without(std::size_t index) const;

    // Position references with empty ("removed") slots filtered out, in
    // their existing row order. Use this (not references()) whenever the
    // caller needs the actual list of currently-entered references -- e.g.
    // to persist a container's placement, or to determine how many rows are
    // truly filled -- since remove_reference() no longer shifts surviving
    // rows up to close the gap left by a deleted reference (see its comment
    // for why): references() itself may therefore contain empty placeholder
    // entries at any position, not just at the end.
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        populated_references() const;
    // Row index (0-2) of the first position row that is not currently
    // populated (empty, or past the end of references()), or 3 if all three
    // rows already hold a reference. Used by the "Počátek" bulk-fill (which
    // always wants to target real empty rows, never assume they start at
    // row 0) instead of trusting whichever row happens to be armed.
    [[nodiscard]] std::size_t first_empty_position_index() const;

    [[nodiscard]] std::set<std::string> highlighted_reference_owner_ids() const;

    // The full reference descriptors (owner_id + semantic_key +
    // instance_path) of every currently-toggled highlighted row. Unlike
    // highlighted_reference_owner_ids(), which only exposes the owner id,
    // this lets a caller build a precise per-entity match (e.g. one
    // specific Origin plane/axis/point, or one specific face of a body)
    // instead of one that matches every sibling entity sharing that owner
    // id (all of the Origin's planes/axes/points, or every face of the same
    // body, share one owner id and are only told apart by semantic_key).
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        highlighted_reference_entries() const;

    // Whether `index` (across both tables) refers to an already-populated
    // row and, if so, its owner id -- used by dialogs that must reject
    // re-picking a reference already owned by the container itself.
    [[nodiscard]] QLabel* reference_status_label() const { return reference_status_; }
    [[nodiscard]] QLabel* dof_label() const { return dof_label_; }
    // The owning dialog renames these to its own context-specific object
    // name (e.g. "constructionReferenceTable" vs. "primitiveReferenceTable")
    // right after construction, since this shared section is reused by
    // multiple dialogs whose contract tests look up the table by a
    // dialog-specific name.
    [[nodiscard]] QTableWidget* reference_table() const { return reference_table_; }
    [[nodiscard]] QTableWidget* orientation_table() const { return orientation_table_; }
    // Marks the exact reference row currently owned by the viewer picking
    // command. The selected-cell palette is the shared cyan command cue used
    // by both automatic sequential entry and explicit one-shot replacement.
    void set_active_reference_index(std::optional<std::size_t> index);
    void set_reference_inspected(std::size_t index, bool inspected);
    void clear_reference_highlights();

    void refresh_reference_table();
    void refresh_orientation_table();
    // populated_index follows the compact persisted/preview reference order,
    // while the table may contain a removed middle-row hole.
    bool set_reference_offset(std::size_t populated_index, double value);

private:
    QWidget* parent_widget_;
    bool with_orientation_;
    bool position_rows_can_define_rotation_;
    int decimal_places_{3};
    QLabel* reference_status_{};
    QLabel* dof_label_{};
    QTableWidget* reference_table_{};
    QTableWidget* orientation_table_{};
    QPushButton* orientation_flip_button_{};
    QPushButton* orientation_rotate_button_{};
    std::array<ReferenceCellItem*, 3> reference_items_{};
    std::array<QWidget*, 3> reference_indicators_{};
    std::array<QToolButton*, 3> reference_inspection_buttons_{};
    std::array<QDoubleSpinBox*, 3> reference_offset_fields_{};
    std::array<ReferenceCellItem*, 2> orientation_items_{};
    std::array<QWidget*, 2> orientation_indicators_{};
    std::array<QToolButton*, 2> orientation_inspection_buttons_{};
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    std::array<QDoubleSpinBox*, 3> rotation_offset_{};
    std::array<double, 3> absolute_rotation_values_{};
    std::array<double, 3> resolved_rotation_values_{};
    zima::document::OrientationConstraintState rotation_constraint_state_;
    bool orientation_back_{};
    int orientation_quarter_turns_{};
    std::vector<zima::document::ConstructionReference> references_;
    std::vector<QString> reference_labels_;
    std::vector<zima::document::ConstructionReference> orientation_references_;
    std::vector<QString> orientation_labels_;
    int remaining_translation_dof_{3};
    int remaining_rotation_dof_{3};
    bool orientation_reference_driven_{};
    std::set<std::size_t> highlighted_reference_rows_;
    std::set<std::size_t> highlighted_orientation_rows_;
    std::optional<std::size_t> active_reference_index_;
    bool orientation_inherited_{false};
    QString orientation_inherited_label_;
    ReferenceRequestCallback reference_request_;
    ChangedCallback changed_;
    HighlightsChangedCallback highlights_changed_;

    void remove_reference(std::size_t index);
    void apply_reference_visual_states();
    void toggle_reference_highlight(std::size_t row);
    void toggle_orientation_highlight(std::size_t row);
    void refresh_orientation_controls();
    void refresh_rotation_field_states();
    void notify_changed();
};

}  // namespace zima::ui
