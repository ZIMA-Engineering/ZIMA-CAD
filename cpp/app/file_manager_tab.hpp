#pragma once

#include "application_settings.hpp"

#include <QWidget>
#include <functional>

class QFileSystemModel;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QTabWidget;
class QToolBox;
class QToolButton;
class QTreeView;
class QTreeWidget;

namespace zima::app {

struct FileManagerNativeParameters {
    QStringList order;
    QMap<QString,QString> values;
    QMap<QString,QMap<QString,QString>> labels;
    QString error;
};

class FileManagerTab final : public QWidget {
public:
    explicit FileManagerTab(const ApplicationSettings& settings,
        const QString& working_directory, const QString& initial_path = {},
        QWidget* parent = nullptr);

    std::function<void(const QString&)> open_file_requested;
    std::function<void(const QString&)> new_tab_requested;
    std::function<void()> settings_requested;
    std::function<void(const QString&)> title_changed;
    std::function<FileManagerNativeParameters(const QString&)> native_parameters_requested;
    std::function<QString(const QString&,const QString&,const QString&)> native_parameter_edit_requested;

    [[nodiscard]] QString current_directory() const { return current_directory_; }
    void set_working_directory(const QString& path);
    void reload_settings(const ApplicationSettings& settings);
    void navigate(const QString& path, bool track_history = true);

private:
    struct SourcePage {
        FileManagerSource source;
        QFileSystemModel* model{};
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

    QToolBox* source_box_{};
    QLineEdit* path_{};
    QLineEdit* name_filter_{};
    QToolButton* back_{};
    QToolButton* forward_{};
    QToolButton* up_{};
    QTabWidget* views_{};
    QWidget* project_page_{};
    QLabel* project_logo_{};
    QLabel* project_title_{};
    QLabel* project_details_{};
    QLabel* project_image_{};
    QPushButton* edit_project_{};
    QListWidget* overview_{};
    QTreeWidget* files_{};
    QPushButton* copy_{};
    QPushButton* move_{};
    QPushButton* remove_{};
    QToolButton* filter_asmz_{};
    QToolButton* filter_prtz_{};
    QToolButton* filter_drwz_{};
    QToolButton* filter_versions_{};

    void build_ui();
    void rebuild_sources();
    void refresh_directory();
    void refresh_project();
    void refresh_overview();
    void refresh_files();
    void update_actions();
    void open_entry(const QString& path);
    void edit_project(const QString& directory, bool create);
    void show_tree_menu(QTreeView* tree, const QPoint& point);
    void copy_selected(bool move);
    void delete_selected();
    void save_parameter(const QModelIndex& index);
    [[nodiscard]] int source_index_for(const QString& path) const;
    [[nodiscard]] bool current_source_read_only() const;
    [[nodiscard]] QString current_source_label() const;
    [[nodiscard]] QStringList selected_paths() const;
};

} // namespace zima::app
