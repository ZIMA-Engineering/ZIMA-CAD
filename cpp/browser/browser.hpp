#pragma once

#include "bookmarks.hpp"
#include "preferences.hpp"
#include <QPointer>
#include <QWidget>
#include <functional>
#include <atomic>
#include <memory>

class QAction;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;
class QLockFile;

namespace zima::browser {
class Credentials;

class Runtime final : public QObject {
    Q_OBJECT
public:
    Runtime(QString directory, std::function<QString()> workingDirectory, QWidget* owner);
    ~Runtime() override;
    Bookmarks bookmarks;
    Preferences preferences;
    QWebEngineProfile* profile();
    Credentials* credentials();
    void configure(Preferences value);
    void show_passwords();
    void clear_cookies();
    void clear_cache();
    std::function<QWebEnginePage*()> new_page;
signals:
    void status(const QString& message);
    void preferences_changed();
private:
    QString directory_;
    std::function<QString()> working_directory_;
    QPointer<QWidget> owner_;
    QWebEngineProfile* profile_{};
    Credentials* credentials_{};
    std::unique_ptr<QLockFile> profile_lock_;
    std::atomic<bool> block_third_party_{true};
};

class Tab final : public QWidget {
    Q_OBJECT
public:
    Tab(Runtime& runtime, QWidget* parent = nullptr);
    QWebEnginePage* page() const;
    QString title() const;
    QUrl url() const;
    void navigate(const QUrl& url);
signals:
    void title_changed();
    void settings_requested();
private:
    void refresh_tree();
    void edit_bookmark(QTreeWidgetItem* item, bool folder = false, bool currentPage = false);
    void update_pin();
    void report(const QString& error);
    QString selected_folder() const;
    Runtime& runtime_;
    QWebEngineView* view_{};
    QTreeWidget* tree_{};
    QLineEdit* address_{};
    QAction* pin_{};
};

} // namespace zima::browser
