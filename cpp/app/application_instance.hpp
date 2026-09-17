#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QString>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStringList>

#include <memory>
#include <map>
#include <mutex>
#include <stdexcept>

namespace zima::app {

// Process-lifetime reservations are disposable, never native document sidecars.
class ApplicationInstance final {
public:
    explicit ApplicationInstance(const QString& directory = QDir::temp().filePath(
                QStringLiteral("ZIMA-CAD-instance-locks"))) : directory_(directory) {
        if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create application reservation directory");
        for (int number = 1; number <= 10000; ++number) {
            auto lock = std::make_unique<QLockFile>(QDir(directory).filePath(
                QStringLiteral("%1.lock").arg(number)));
            // Long-running CAD sessions must never expire. QLockFile still
            // recovers a reservation left by a process that has exited.
            lock->setStaleLockTime(0);
            if (lock->tryLock()) {
                number_ = number;
                lock_ = std::move(lock);
                return;
            }
            if (lock->error() != QLockFile::LockFailedError)
                throw std::runtime_error("Cannot reserve an application instance number");
        }
        throw std::runtime_error("No application instance number is available");
    }

    [[nodiscard]] QString label() const {
        return QCoreApplication::translate("ApplicationInstance", "Instance %1").arg(number_);
    }
    [[nodiscard]] int number() const { return number_; }

    [[nodiscard]] static QString path_key(const QString& path) {
        QFileInfo info(path);
        auto result=info.canonicalFilePath();
        if(result.isEmpty()) {
            const auto parent=QFileInfo(info.absolutePath()).canonicalFilePath();
            result=parent.isEmpty()?info.absoluteFilePath():QDir(parent).filePath(info.fileName());
        }
        result=QDir::cleanPath(result);
#ifdef Q_OS_WIN
        result=result.toCaseFolded();
#endif
        return result;
    }
    [[nodiscard]] std::shared_ptr<QLockFile> reserve_directory(const QString& path) {
        return reserve(QStringLiteral("directory:"),path);
    }
    void set_directory(const QString& path) {
        auto next=reserve_directory(path);
        directory_lock_=std::move(next);
    }
    void reserve_file(const QString& path) {
        if(path.isEmpty())return;
        auto next=reserve(QStringLiteral("file:"),path);
        std::lock_guard guard(mutex_);
        files_[path_key(path)]=std::move(next);
    }
    void retain_files(const QStringList& paths) {
        std::lock_guard guard(mutex_);
        QStringList keys;for(const auto& path:paths)if(!path.isEmpty())keys.push_back(path_key(path));
        std::erase_if(files_,[&](const auto& row){return !keys.contains(row.first);});
    }

private:
    std::shared_ptr<QLockFile> reserve(const QString& kind,const QString& path) {
        // Windows within one process share ownership; other processes must
        // acquire the same OS-visible reservation independently.
        static std::mutex registry_mutex;
        static std::map<QString,std::weak_ptr<QLockFile>> reservations;
        const auto key=kind+path_key(path);
        const auto registry_key=path_key(directory_)+"/"+key;
        std::lock_guard guard(registry_mutex);
        if(auto existing=reservations[registry_key].lock())return existing;
        const auto hash=QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex();
        auto lock=std::make_shared<QLockFile>(QDir(directory_).filePath(QString::fromLatin1(hash)+".lock"));
        lock->setStaleLockTime(0);
        if(!lock->tryLock()) {
            if(lock->error()!=QLockFile::LockFailedError)
                throw std::runtime_error("Cannot reserve access to: "+path.toStdString());
            const auto message=kind==QStringLiteral("directory:")
                ? QStringLiteral("Pracovní adresář již používá jiná instance ZIMA-CAD: %1")
                : QStringLiteral("Soubor již používá jiná instance ZIMA-CAD: %1");
            throw std::runtime_error(message.arg(path).toStdString());
        }
        reservations[registry_key]=lock;return lock;
    }
    QString directory_;
    std::mutex mutex_;
    std::map<QString,std::shared_ptr<QLockFile>> files_;
    std::shared_ptr<QLockFile> directory_lock_;
    int number_{};
    std::unique_ptr<QLockFile> lock_;
};

} // namespace zima::app
