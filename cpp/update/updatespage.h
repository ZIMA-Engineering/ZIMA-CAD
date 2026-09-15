#pragma once
#include <QWidget>
#include <functional>
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class UpdatesPage : public QWidget {
public:
    explicit UpdatesPage(std::function<void(bool)> restart, QWidget* parent = nullptr);
    bool save(QString* error);
private:
    void refresh();
    QCheckBox* automatic_;
    QLabel *versions_, *status_, *detail_;
    QPlainTextEdit* notes_;
    QProgressBar* progress_;
    QPushButton *check_, *install_, *rollback_, *cancel_;
};
