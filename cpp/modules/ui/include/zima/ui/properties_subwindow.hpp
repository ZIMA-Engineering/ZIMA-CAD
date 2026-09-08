#pragma once

#include <QDialog>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QRect>

class QDialogButtonBox;
class QLabel;
class QResizeEvent;
class QVBoxLayout;
class QWidget;
class QPushButton;

namespace zima::ui {

// Display precision is inherited from the owning document window, before values enter editors.
[[nodiscard]] int numeric_decimal_places(const QWidget* owner, int fallback = 3);

[[nodiscard]] QPushButton* create_origin_selection_button(QWidget* parent);

class PropertiesSubWindow : public QDialog {
public:
    explicit PropertiesSubWindow(const QString& title, QWidget* parent);
    ~PropertiesSubWindow() override;

    [[nodiscard]] QVBoxLayout* content_layout() const;
    [[nodiscard]] QDialogButtonBox* buttons() const;
    void set_internal_title(const QString& title);
    void set_centered_on_show(bool centered = true);
    void set_initial_size(const QSize& size);
    [[nodiscard]] QPushButton* ensure_origin_selection_button();

protected:
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    virtual bool submit() = 0;

private:
    void keep_inside_parent();
    QVBoxLayout* content_layout_{};
    QDialogButtonBox* buttons_{};
    QLabel* title_label_{};
    QLabel* submit_error_{};
    QWidget* title_bar_{};
    QPointF title_drag_origin_;
    QPoint title_drag_window_origin_;
    bool title_drag_active_{};
    bool correcting_position_{};
    bool correcting_size_{};
    bool centered_on_show_{};
    QSize initial_size_;
    Qt::Edges resize_edges_;
    QPointF resize_drag_origin_;
    QRect resize_drag_geometry_;
};

}  // namespace zima::ui
