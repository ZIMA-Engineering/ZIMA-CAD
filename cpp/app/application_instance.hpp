#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QString>

#include <memory>

namespace zima::app {

// A lock reserves only a display number. It never prevents another process
// from starting, forwards documents, or locks a project/document.
class ApplicationInstance final {
public:
    explicit ApplicationInstance(const QString& directory = QDir(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                .filePath(QStringLiteral("ZIMA-CAD/instances"))) {
        if (!QDir().mkpath(directory)) return;
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
            if (lock->error() != QLockFile::LockFailedError) return;
        }
    }

    [[nodiscard]] QString label() const {
        return number_ > 0
            ? QCoreApplication::translate("ApplicationInstance", "Instance %1").arg(number_)
            : QCoreApplication::translate("ApplicationInstance", "Instance PID %1")
                .arg(QCoreApplication::applicationPid());
    }
    [[nodiscard]] int number() const { return number_; }

private:
    int number_{};
    std::unique_ptr<QLockFile> lock_;
};

} // namespace zima::app