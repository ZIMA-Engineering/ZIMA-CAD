#pragma once
#include <zima/commands/dispatcher.hpp>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

namespace CadAi {
QJsonArray toolDefinitions();
QString instructions();
QString hostInstructions();
QJsonObject publicContext(const QJsonObject& snapshot);

// The shared CAD command host owns geometry, validation, history and persistence.
// This adapter only binds an AI request to its live document and reviews changes.
class CommandSession {
public:
    using Execute = std::function<zima::commands::Result(const QString&)>;
    using Snapshot = std::function<QJsonObject()>;
    struct Reply {
        QJsonObject data;
        bool success = false, approval = false;
        QString review;
    };
    CommandSession(Execute execute, Snapshot snapshot);
    QJsonObject begin();
    void cancel();
    Reply call(const QString& name, const QJsonObject& arguments);
    Reply decide(bool allow);
    bool pending() const { return !pending_.isEmpty(); }
private:
    bool current() const;
    Reply execute(const QJsonObject& request);
    Execute execute_;
    Snapshot snapshot_;
    QJsonObject captured_, pending_;
    QJsonArray catalog_;
    bool active_ = false;
};
}
