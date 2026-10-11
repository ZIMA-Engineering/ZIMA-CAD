#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QImage;

namespace zima::browser {

// One user store, independent of document paths and native model transactions.
// Writers reload under a lock so separate CAD processes cannot overwrite each other.
class Bookmarks final : public QObject {
    Q_OBJECT
public:
    explicit Bookmarks(QString directory, QObject* parent = nullptr);
    bool reload(QString* error = nullptr);
    [[nodiscard]] QJsonArray items() const { return items_; }
    [[nodiscard]] QString directory() const { return directory_; }
    QString add(QString name, QUrl url, QString parentId, bool folder, QString* error = nullptr);
    bool update(QString id, QString name, QUrl url, QString parentId, QString* error = nullptr);
    bool remove(QString id, QString* error = nullptr);
    bool save_icon(QString id, const QImage& image, QString* error = nullptr);
    static bool allowed_url(const QUrl& url);
    static QString official_url();
signals:
    void changed();
private:
    bool mutate(const std::function<bool(QJsonArray&, QString*)>& action, QString* error);
    QString directory_;
    QJsonArray items_;
};

} // namespace zima::browser
