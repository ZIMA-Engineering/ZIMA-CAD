#include "bookmarks.hpp"
#include "preferences.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace zima::browser {
namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool read(const QString& path, QJsonArray& items, QString* error) {
    QFile file(path);
    if (!file.exists()) {
        items = QJsonArray{QJsonObject{{"id", "zima-official"}, {"parent", ""},
            {"name", "ZIMA-CAD"}, {"url", Bookmarks::official_url()}, {"folder", false}}};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) return fail(error, QObject::tr("Cannot read bookmark file: %1").arg(file.fileName()));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt() != 1
        || !document.object().value("items").isArray())
        return fail(error, QObject::tr("Invalid bookmark file."));
    items = document.object().value("items").toArray();
    QSet<QString> ids;
    for (const auto& value : items) {
        const auto item = value.toObject();
        const auto id = item.value("id").toString();
        if (id.isEmpty() || id.contains('/') || id.contains('\\') || id.contains("..") || ids.contains(id) || !item.value("folder").isBool()
            || item.value("name").toString().trimmed().isEmpty()
            || (!item.value("folder").toBool() && !Bookmarks::allowed_url(QUrl(item.value("url").toString()))))
            return fail(error, QObject::tr("Invalid bookmark file."));
        ids.insert(id);
    }
    for (const auto& value : items) {
        QSet<QString> ancestors;
        auto parent = value.toObject().value("parent").toString();
        const auto own = value.toObject().value("id").toString();
        while (!parent.isEmpty()) {
            if (parent == own || ancestors.contains(parent)) return fail(error, QObject::tr("Invalid bookmark folder."));
            ancestors.insert(parent);
            bool found = false;
            for (const auto& candidate : items) {
                const auto object = candidate.toObject();
                if (object.value("id").toString() != parent) continue;
                if (!object.value("folder").toBool()) return fail(error, QObject::tr("Invalid bookmark folder."));
                parent = object.value("parent").toString(); found = true; break;
            }
            if (!found) return fail(error, QObject::tr("Invalid bookmark folder."));
        }
    }
    return true;
}
}
QString Bookmarks::official_url() {
    return zima::browser::official_url();
}
bool Bookmarks::allowed_url(const QUrl& url) {
    return zima::browser::allowed_url(url);
}
Bookmarks::Bookmarks(QString directory, QObject* parent) : QObject(parent), directory_(std::move(directory)) {}
bool Bookmarks::reload(QString* error) {
    if (!QFile::exists(QDir(directory_).filePath("bookmarks.json")))
        return mutate([](QJsonArray&, QString*) { return true; }, error);
    QJsonArray next;
    if (!read(QDir(directory_).filePath("bookmarks.json"), next, error)) return false;
    items_ = next;
    emit changed();
    return true;
}
bool Bookmarks::mutate(const std::function<bool(QJsonArray&, QString*)>& action, QString* error) {
    if (!QDir().mkpath(directory_)) return fail(error, QObject::tr("Cannot create browser data directory."));
    QLockFile lock(QDir(directory_).filePath("bookmarks.lock"));
    if (!lock.tryLock(0)) return fail(error, QObject::tr("Bookmarks are being changed by another application instance."));
    QJsonArray next;
    const auto path = QDir(directory_).filePath("bookmarks.json");
    if (!read(path, next, error) || !action(next, error)) return false;
    QSaveFile file(path);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"items", next}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QObject::tr("Cannot save bookmark file: %1").arg(file.fileName()));
    items_ = next;
    emit changed();
    return true;
}
QString Bookmarks::add(QString name, QUrl url, QString parentId, bool folder, QString* error) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto ok = mutate([&](QJsonArray& items, QString* detail) {
        if (name.trimmed().isEmpty() || (!folder && !allowed_url(url)))
            return fail(detail, QObject::tr("Enter a name and a valid HTTP or HTTPS address."));
        if (!parentId.isEmpty()) {
            bool found = false;
            for (const auto& v : items) if (v.toObject().value("id") == parentId && v.toObject().value("folder").toBool()) found = true;
            if (!found) return fail(detail, QObject::tr("Invalid bookmark folder."));
        }
        items.append(QJsonObject{{"id", id}, {"name", name.trimmed()}, {"url", folder ? QString{} : url.toString()},
            {"parent", parentId}, {"folder", folder}});
        return true;
    }, error);
    return ok ? id : QString{};
}
bool Bookmarks::update(QString id, QString name, QUrl url, QString parentId, QString* error) {
    return mutate([&](QJsonArray& items, QString* detail) {
        int index = -1;
        for (int i = 0; i < items.size(); ++i) if (items[i].toObject().value("id") == id) index = i;
        if (index < 0) return fail(detail, QObject::tr("Bookmark no longer exists."));
        auto item = items[index].toObject();
        if (name.trimmed().isEmpty() || (!item.value("folder").toBool() && !allowed_url(url)))
            return fail(detail, QObject::tr("Enter a name and a valid HTTP or HTTPS address."));
        auto ancestor = parentId;
        QSet<QString> visited;
        while (!ancestor.isEmpty()) {
            if (ancestor == id || visited.contains(ancestor)) return fail(detail, QObject::tr("Invalid bookmark folder."));
            visited.insert(ancestor); bool found = false;
            for (const auto& v : items) {
                const auto candidate = v.toObject();
                if (candidate.value("id") != ancestor) continue;
                if (!candidate.value("folder").toBool()) return fail(detail, QObject::tr("Invalid bookmark folder."));
                ancestor = candidate.value("parent").toString(); found = true; break;
            }
            if (!found) return fail(detail, QObject::tr("Invalid bookmark folder."));
        }
        item["name"] = name.trimmed(); item["parent"] = parentId;
        if (!item.value("folder").toBool()) item["url"] = url.toString();
        items[index] = item; return true;
    }, error);
}
bool Bookmarks::remove(QString id, QString* error) {
    return mutate([&](QJsonArray& items, QString* detail) {
        QSet<QString> deleted{id}; bool found = false;
        for (const auto& v : items) if (v.toObject().value("id") == id) found = true;
        if (!found) return fail(detail, QObject::tr("Bookmark no longer exists."));
        bool more;
        do { more = false; for (const auto& v : items) {
            const auto item = v.toObject(); const auto child = item.value("id").toString();
            if (deleted.contains(item.value("parent").toString()) && !deleted.contains(child)) { deleted.insert(child); more = true; }
        }} while (more);
        for (int i = items.size() - 1; i >= 0; --i) if (deleted.contains(items[i].toObject().value("id").toString())) items.removeAt(i);
        return true;
    }, error);
}
bool Bookmarks::save_icon(QString id, const QImage& image, QString* error) {
    // IDs from the store are never used as unrestricted filesystem paths.
    bool found = false;
    for (const auto& v : items_) if (v.toObject().value("id") == id) found = true;
    if (!found || id.contains('/') || id.contains('\\') || id.contains("..") || image.isNull()) return false;
    const auto dir = QDir(directory_).filePath("icons");
    if (!QDir().mkpath(dir)) return fail(error, QObject::tr("Cannot create browser data directory."));
    QSaveFile file(QDir(dir).filePath(id + ".png"));
    if (!file.open(QIODevice::WriteOnly) || !image.scaled(32,32,Qt::KeepAspectRatio,Qt::SmoothTransformation).save(&file,"PNG") || !file.commit())
        return fail(error, QObject::tr("Cannot save bookmark icon: %1").arg(file.fileName()));
    emit changed(); return true;
}
} // namespace zima::browser
