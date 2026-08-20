#pragma once

#include <QDialog>
#include <QPoint>
#include <QPointF>

class QDialogButtonBox;
class QLabel;
class QVBoxLayout;
class QWidget;

namespace zima::ui {

class PropertiesSubWindow : public QDialog {
public:
    explicit PropertiesSubWindow(const QString& title, QWidget* parent);
    ~PropertiesSubWindow() override;

    [[nodiscard]] QVBoxLayout* content_layout() const;
    [[nodiscard]] QDialogButtonBox* buttons() const;
    void set_internal_title(const QString& title);
    void set_centered_on_show(bool centered = true);

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
    QWidget* title_bar_{};
    QPointF title_drag_origin_;
    QPoint title_drag_window_origin_;
    bool title_drag_active_{};
    bool correcting_position_{};
    bool centered_on_show_{};
};

}  // namespace zima::ui
