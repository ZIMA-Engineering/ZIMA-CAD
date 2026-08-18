#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QVBoxLayout;

namespace zima::ui {

class PropertiesSubWindow : public QDialog {
public:
    explicit PropertiesSubWindow(const QString& title, QWidget* parent);
    ~PropertiesSubWindow() override;

    [[nodiscard]] QVBoxLayout* content_layout() const;
    [[nodiscard]] QDialogButtonBox* buttons() const;
    void set_internal_title(const QString& title);

protected:
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    virtual bool submit() = 0;

private:
    void keep_inside_parent();
    QVBoxLayout* content_layout_{};
    QDialogButtonBox* buttons_{};
    QLabel* title_label_{};
    bool correcting_position_{};
};

}  // namespace zima::ui
