#pragma once

#include <zima/document/part_document.hpp>

#include <array>
#include <functional>
#include <set>
#include <vector>

#include <QObject>
#include <QString>

class QWidget;
class QLabel;
class QTableWidget;
class QVBoxLayout;

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
    // the section appends its rows to, in the same top-to-bottom order as
    // the reference implementation: status label, "Umístění kontejneru"
    // heading, reference table, then (if `with_orientation`) the "Orientace
    // kontejneru" heading and orientation table, then the DOF label.
    ContainerPlacementSection(QWidget* parent_widget, QVBoxLayout* layout,
        bool with_orientation);

    void set_reference_request_callback(ReferenceRequestCallback callback);
    // Invoked whenever a reference/offset/orientation-role edit changes the
    // resolved placement and the owning dialog should recompute its preview.
    void set_changed_callback(ChangedCallback callback);
    void set_highlights_changed_callback(HighlightsChangedCallback callback);

    // Seeds the section from a container's persisted references, splitting
    // orientation-driving entries (Plane FRONT/TOP mirrors) from plain
    // position references, exactly like each dialog's constructor used to
    // do inline.
    void initialize_from_references(
        const std::vector<zima::document::ConstructionReference>& references,
        const std::function<QString(const std::string&)>& label_for_semantic);

    // Assigns reference `index` (0-2 = position row, 3-4 = FRONT/TOP
    // orientation row when `with_orientation` is enabled). `mirror_first_two
    // _into_orientation` matches Plane-kind containers' rule that position
    // rows 0/1 are simultaneously FRONT/TOP orientation references; primitive
    // Plane-oriented features pass false since their orientation rows are
    // independently selected. Returns false (and sets `error_text`) on a
    // duplicate-reference conflict.
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference, const QString& label,
        bool mirror_first_two_into_orientation, QString* error_text);

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

    [[nodiscard]] std::set<std::string> highlighted_reference_owner_ids() const;

    // Whether `index` (across both tables) refers to an already-populated
    // row and, if so, its owner id -- used by dialogs that must reject
    // re-picking a reference already owned by the container itself.
    [[nodiscard]] QLabel* reference_status_label() const { return reference_status_; }
    [[nodiscard]] QLabel* dof_label() const { return dof_label_; }

    void refresh_reference_table();
    void refresh_orientation_table();

private:
    QWidget* parent_widget_;
    bool with_orientation_;
    QLabel* reference_status_{};
    QLabel* dof_label_{};
    QTableWidget* reference_table_{};
    QTableWidget* orientation_table_{};
    std::array<ReferenceCellItem*, 3> reference_items_{};
    std::array<QWidget*, 3> reference_indicators_{};
    std::array<ReferenceCellItem*, 2> orientation_items_{};
    std::array<QWidget*, 2> orientation_indicators_{};
    std::vector<zima::document::ConstructionReference> references_;
    std::vector<QString> reference_labels_;
    std::vector<zima::document::ConstructionReference> orientation_references_;
    std::vector<QString> orientation_labels_;
    int remaining_translation_dof_{3};
    int remaining_rotation_dof_{3};
    std::set<std::size_t> highlighted_reference_rows_;
    std::set<std::size_t> highlighted_orientation_rows_;
    ReferenceRequestCallback reference_request_;
    ChangedCallback changed_;
    HighlightsChangedCallback highlights_changed_;

    void remove_reference(std::size_t index);
    void toggle_reference_highlight(std::size_t row);
    void toggle_orientation_highlight(std::size_t row);
    void update_reference_highlight_styles();
    void notify_changed();
};

}  // namespace zima::ui
