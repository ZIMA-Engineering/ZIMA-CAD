#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class UpdatesPage : public QWidget {
public:
    struct State {
        bool automatic = true, busy = false, installed = false;
        QString previous, phase, error, prepared, lastCheck;
        QJsonObject offer;
        qint64 received = 0, total = 0;
    };
    // Presentation dependencies; production always uses the authenticated service.
    // Tests can exercise real buttons without a network or installer process.
    struct Backend {
        std::function<State()> state;
        std::function<void()> check, download, cancel;
        std::function<bool(bool, QString*)> save;
        std::function<QString()> restartBlocker;
    };
    explicit UpdatesPage(std::function<void(bool)> restart, QWidget* parent = nullptr);
    UpdatesPage(std::function<void(bool)> restart, Backend backend, QWidget* parent);
    ~UpdatesPage() override;
    bool save(QString* error);
    void refresh();
    void cancelPendingInstallation();
private:
    void install();
    void cancel();
    Backend backend_;
    std::function<void(bool)> restart_;
    QString requested_, localError_;
    quint64 requestSerial_ = 0;
    bool restartQueued_ = false;
    QCheckBox* automatic_;
    QLabel *versions_, *status_, *detail_;
    QPlainTextEdit* notes_;
    QProgressBar* progress_;
    QPushButton *check_, *install_, *rollback_, *cancel_;
};
