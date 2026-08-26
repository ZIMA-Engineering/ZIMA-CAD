#pragma once

#include <functional>

#include <QColor>
#include <QString>
#include <QTableWidgetItem>

class QWidget;

namespace zima::ui {

// Centers a fixed-size cell widget within its QTableWidget cell. Direct port
// of Python's _centered_cell_widget (zima_cad/app.py): QTableWidget::
// setCellWidget stretches its argument to fill the cell exactly, so a small
// fixed-size button/label ends up pinned to the cell's top-left corner
// instead of the middle. Shared by every reference table (Point/Axis/Plane/
// Container, Orientation, Assembly component mates) so their "x"/arrow cells
// line up identically.
QWidget* centered_cell_widget(QWidget* inner);

// Builds the leading indicator column shared by every reference table: a
// green arrow prompt for an unfilled reference slot and a red "x" remove
// button once a reference has been assigned. Direct port of Python's
// _build_reference_row_indicator. Call set_reference_row_populated() to
// toggle between the two states as references are picked or cleared.
QWidget* build_reference_row_indicator(std::function<void()> remove_callback);

// Switches a build_reference_row_indicator() cell between its arrow/remove
// states. Direct port of Python's _set_reference_row_populated.
void set_reference_row_populated(QWidget* indicator, bool populated);

// Builds a checkable icon-style FLIP toggle button for an orientation-
// driving reference row (Axis/Plane FRONT/TOP picks). A checkable QToolButton
// rather than a QCheckBox, matching build_reference_row_indicator()'s icon-
// button visual language; the user only inspects the live 3D-view result,
// not the control's own checked appearance. Disabled (and left unchecked)
// for a plain position reference (Point, or a position-only Axis/Plane
// pick), which has no direction to invert. toggled_callback receives the
// new checked state.
QWidget* build_reference_row_flip_button(
    bool enabled, bool checked, std::function<void(bool)> toggled_callback);

// Reference-column table item: a flat QTableWidgetItem holding a reference
// descriptor (empty when unfilled), its display label and a "checked"/
// highlighted flag. Mirrors Python's _ReferenceCellItem: the reference is a
// data field on the row entry itself (a table item), not a push-button,
// keeping the whole row a set of data-entry cells rather than a mix of
// buttons and fields. Clicking an empty cell requests a new reference pick;
// clicking a populated cell toggles its viewer highlight.
class ReferenceCellItem : public QTableWidgetItem {
public:
    explicit ReferenceCellItem(const QString& text = QString());

    [[nodiscard]] bool has_reference() const { return has_reference_; }
    [[nodiscard]] QString reference() const { return reference_; }
    void set_reference(const QString& value);
    void clear_reference();

    [[nodiscard]] bool is_checked() const { return checked_; }
    void set_checked(bool value) { checked_ = value; }

    // Applies the "pick reference" placeholder styling (muted color, no
    // reference set) used while a row is empty.
    void set_placeholder_style(const QColor& muted);

private:
    bool has_reference_{false};
    QString reference_;
    bool checked_{false};
};

}  // namespace zima::ui
