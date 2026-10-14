#pragma once

#include "application_settings.hpp"

#include <QWidget>
#include <functional>

class QAction;
class QFileSystemModel;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QTabWidget;
class QToolButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTreeView;
class QTreeWidget;
class QWebEngineView;

namespace zima::app {

struct FileManagerNativeParameters {
    QStringList order;
    QMap<QString,QString> values;
    QMap<QString,QMap<QString,QString>> labels;
    QMap<QString,QMap<QString,QString>> localized_values;
    QString error;
    bool unsaved{};
};

class FileManagerTab final : public QWidget {
public:
    explicit FileManagerTab(const ApplicationSettings& settings,
        const QString& working_directory, const QString& initial_path = {},
        QWidget* parent = nullptr);
    ~FileManagerTab() override;

    std::function<void(const QString&)> open_file_requested;
    std::function<void(const QString&)> preview_file_requested;
    std::function<void(const QString&)> new_tab_requested;
    std::function<void()> settings_requested;
    std::function<void(const QString&)> working_directory_requested;
    std::function<void(const QString&)> title_changed;
    std::function<FileManagerNativeParameters(const QString&)> native_parameters_requested;
    std::function<QString(const QString&)> native_parameters_revision_requested;
    std::function<QString(const QString&,const QString&,const QString&)> native_parameter_edit_requested;

    [[nodiscard]] QString current_directory() const { return current_directory_; }
    void set_working_directory(const QString& path);
    void set_working_directory_action(QAction* action);
    void reload_settings(const ApplicationSettings& settings);
    void navigate(const QString& path, bool track_history = true);

protected:
    void changeEvent(QEvent* event) override;

private:
    struct SourcePage {
        FileManagerSource source;
        QFileSystemModel* model{};
        QSortFilterProxyModel* proxy{};
        QTreeView* tree{};
    };

    ApplicationSettings settings_;
    QString working_directory_;
    QString current_directory_;
    QVector<SourcePage> sources_;
    QStringList history_;
    int history_index_{-1};
    bool changing_source_{};
    bool refreshing_files_{};
    QMap<QString,QPair<QString,FileManagerNativeParameters>> native_parameter_cache_;

    QListWidget* source_list_{};
    QStackedWidget* source_stack_{};
    QLineEdit* path_{};
    QToolButton* choose_working_directory_{};
    QToolButton* back_{};
    QToolButton* forward_{};
    QToolButton* up_{};
    QTabWidget* views_{};
    QWidget* automatic_overview_{};
    QLabel* project_logo_{};
    QLabel* project_title_{};
    QLabel* project_details_{};
    QLabel* project_image_{};
    QLabel* overview_empty_{};
    QStackedWidget* overview_stack_{};
    QListWidget* overview_{};
    QWebEngineView* overview_web_{};
    QTreeWidget* files_{};
    QPushButton* copy_{};
    QPushButton* clean_{};
    QPushButton* move_{};
    QPushButton* remove_{};
    QToolButton* filter_versions_{};

    void build_ui();
    void rebuild_sources();
    void refresh_source_labels();
    void refresh_directory();
    void refresh_overview_header();
    void refresh_overview();
    void sync_overview_theme();
    void refresh_files();
    void apply_file_filters();
    void update_actions();
    void open_entry(const QString& path);
    void edit_directory(const QString& directory);
    void edit_project(const QString& directory, bool create);
    void show_source_menu(const QPoint& point);
    void show_tree_menu(QTreeView* tree, const QPoint& point);
    void copy_selected(bool move);
    void delete_selected();
    void clean_directory(const QString& directory);
    void save_column_layout();
    void restore_column_layout();
    void save_parameter(const QModelIndex& index);
    [[nodiscard]] int source_index_for(const QString& path) const;
    [[nodiscard]] bool current_source_read_only() const;
    [[nodiscard]] QString current_source_label() const;
    [[nodiscard]] QStringList selected_paths() const;
};

} // namespace zima::app
