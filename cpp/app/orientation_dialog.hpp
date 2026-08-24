#pragma once

#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTableWidget;
class QWidget;

namespace zima::ui { class ReferenceCellItem; }

namespace zima::app {

// A single reference row entered into the "Pohled kolmo" table: the picked
// face/plane descriptor ("<owner_id>:face:<index>" or "<owner_id>:plane:<key>"),
// its display label, the FRONT/BACK/TOP/BOTTOM/LEFT/RIGHT role and whether the
// picked direction should be flipped. Mirrors the dicts produced by Python's
// OrientationDialog.orientation_rows().
struct OrientationReferenceRow {
    std::string reference;
    QString label;
    std::string role{"front"};
    bool flip{false};
};

// A saved camera view: either one of the 7 built-in standard views
// ("default"/"front"/"back"/"top"/"bottom"/"left"/"right") or a fully custom
// camera state captured from the current viewport. Mirrors the dicts stored
// in Python's document_settings["named_views"].
struct OrientationSavedView {
    QString name;
    std::string standard;  // non-empty for built-in views, empty for custom
    std::array<float, 8> camera_state{};  // valid only when standard is empty
    [[nodiscard]] bool is_custom() const { return standard.empty(); }
};

// C++ port of Python's OrientationDialog (zima_cad/app.py): a modeless
// "Pohled kolmo" reference/orientation editor shared by Part and Assembly
// documents, following the same PropertiesSubWindow presentation contract as
// every other property dialog in the application (see custom instructions:
// "Every newly created property ... dialog must use the same in-application
// Qt.WindowType.SubWindow presentation").
class OrientationDialog final : public zima::ui::PropertiesSubWindow {
public:
    using ReferenceRequestCallback = std::function<void(std::size_t)>;
    using RowsChangedCallback =
        std::function<void(const std::vector<OrientationReferenceRow>&)>;
    using ViewRequestedCallback =
        std::function<void(const OrientationSavedView&)>;
    using SaveViewCallback = std::function<void(const QString& name)>;
    using DeleteViewCallback = std::function<void(const QString& name)>;
    // Returns true if the two reference descriptors are independent (not
    // parallel/antiparallel), mirroring Python's
    // _orientation_references_are_independent(). Used to reject a candidate
    // reference that would duplicate the direction already used by another
    // row (e.g. picking TOP twice, or two opposite faces).
    using IndependenceCheckCallback =
        std::function<bool(const std::string& existing, const std::string& candidate)>;
    using ReferenceRejectedCallback = std::function<void()>;

    OrientationDialog(
        std::vector<OrientationSavedView> custom_views, QWidget* parent);

    void set_reference_request_callback(ReferenceRequestCallback callback);
    // Fired whenever a row's reference/role/flip changes (preview + highlight
    // sync), and again when the second row is completed (apply-and-stop).
    void set_rows_changed_callback(RowsChangedCallback callback);
    void set_view_requested_callback(ViewRequestedCallback callback);
    void set_save_view_callback(SaveViewCallback callback);
    void set_delete_view_callback(DeleteViewCallback callback);
    void set_independence_check_callback(IndependenceCheckCallback callback);
    // Fired when accept_reference() rejects a candidate because it is not
    // independent of an already-accepted reference.
    void set_reference_rejected_callback(ReferenceRejectedCallback callback) {
        reference_rejected_ = std::move(callback);
    }

    // Called by the workspace window once a face/plane candidate has been
    // picked in the 3D view for the currently active row.
    void accept_reference(const std::string& descriptor, const QString& label);
    void remove_row(std::size_t row);
    [[nodiscard]] std::vector<OrientationReferenceRow> orientation_rows() const;
    [[nodiscard]] std::size_t active_row() const { return active_row_; }
    void append_saved_view(OrientationSavedView view);
    void remove_saved_view(const QString& name);

protected:
    bool submit() override;

private:
    QTableWidget* reference_table_{};
    std::array<QComboBox*, 2> role_combos_{};
    std::array<QCheckBox*, 2> flip_checks_{};
    std::array<zima::ui::ReferenceCellItem*, 2> reference_items_{};
    std::array<QWidget*, 2> row_indicators_{};
    std::size_t active_row_{0};
    std::set<std::size_t> highlighted_rows_;
    QListWidget* view_list_{};
    std::vector<OrientationSavedView> custom_views_;
    QLineEdit* name_edit_{};
    ReferenceRequestCallback reference_request_;
    RowsChangedCallback rows_changed_;
    ViewRequestedCallback view_requested_;
    SaveViewCallback save_view_;
    DeleteViewCallback delete_view_;
    IndependenceCheckCallback independence_check_;
    ReferenceRejectedCallback reference_rejected_;
    bool updating_rows_{false};

    [[nodiscard]] bool references_independent(
        const std::string& candidate_descriptor) const;

    void refresh_reference_table();
    void refresh_view_list();
    void notify_rows_changed();
    void activate_row(std::size_t row);
    void handle_reference_cell_clicked(int row, int column);
    void update_highlights();
    void handle_view_item(QListWidgetItem* item);
    void handle_save_clicked();
    void handle_delete_clicked();
};

}  // namespace zima::app
