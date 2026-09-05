#pragma once
#include <QTreeWidget>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <functional>
#include <optional>

namespace zima::app {
class HistoryTreeWidget final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(std::size_t)> history_cursor_moved;
    std::function<bool(QTreeWidgetItem*)> reorder_enabled;
    // before is the next sibling's stable ID, or empty for the end.
    std::function<bool(QTreeWidgetItem*,const QString&,bool)> reorder_requested;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        auto* item = itemAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && item == nullptr) {
            clearSelection();
            setCurrentItem(nullptr);
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton &&
            property("commandSelectionActive").toBool()) {
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton && item != nullptr &&
            item->isSelected()) {
            // Preserve Ctrl multi-selection until the custom context menu is
            // evaluated, matching the Python HistoryTreeWidget contract.
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && item != nullptr &&
            item->data(0, Qt::UserRole + 3).toString() == "part-insert-here") {
            dragging_cursor_ = true;
            drag_started_ = false;
            drag_origin_ = event->position().toPoint();
            setCurrentItem(item);
            event->accept();
            return;
        }
        const QPersistentModelIndex pressed=item ? indexFromItem(item) : QModelIndex{};
        QTreeWidget::mousePressEvent(event);
        item=pressed.isValid() ? itemFromIndex(pressed) : nullptr;
        if (event->button()==Qt::LeftButton && item && reorder_enabled && reorder_enabled(item)) {
            dragged_item_=indexFromItem(item);
            drag_origin_=event->position().toPoint();
            drag_started_=false;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragged_item_.isValid()) {
            if (!(event->buttons() & Qt::LeftButton)) { clear_reorder(); return; }
            if (!drag_started_ && (event->position().toPoint()-drag_origin_).manhattanLength() >= QApplication::startDragDistance())
                drag_started_=true;
            if (drag_started_) update_reorder(event->position().toPoint());
            event->accept(); return;
        }
        if (!dragging_cursor_) return QTreeWidget::mouseMoveEvent(event);
        if (!drag_started_ &&
            (event->position().toPoint() - drag_origin_).manhattanLength() >=
                QApplication::startDragDistance()) {
            drag_started_ = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (drag_started_) {
            insertion_y_ = event->position().toPoint().y();
            viewport()->update();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragged_item_.isValid() && event->button()==Qt::LeftButton) {
            if (drag_started_) update_reorder(event->position().toPoint());
            auto* source=itemFromIndex(dragged_item_);
            const auto before=before_id_;
            const bool was_drag=drag_started_;
            const bool commit=was_drag && drop_allowed_;
            clear_reorder();
            if (!was_drag) { QTreeWidget::mouseReleaseEvent(event);return; }
            if (commit && reorder_requested) reorder_requested(source,before,true);
            event->accept(); return;
        }
        if (!dragging_cursor_) return QTreeWidget::mouseReleaseEvent(event);
        dragging_cursor_ = false;
        viewport()->unsetCursor();
        if (drag_started_ && history_cursor_moved) {
            std::size_t cursor = 0;
            auto* root = topLevelItemCount() == 1 ? topLevelItem(0) : nullptr;
            if (root != nullptr) {
                for (int index = 0; index < root->childCount(); ++index) {
                    auto* child = root->child(index);
                    const auto role = child->data(0, Qt::UserRole + 3).toString();
                    if (role != "part-container" && role != "part-sketch" &&
                        role != "part-construction") continue;
                    if (visualItemRect(child).center().y() <
                        event->position().toPoint().y()) ++cursor;
                }
            }
            history_cursor_moved(cursor);
        }
        drag_started_ = false;
        insertion_y_.reset();
        viewport()->update();
        event->accept();
    }

    void paintEvent(QPaintEvent* event) override {
        QTreeWidget::paintEvent(event);
        if (!insertion_y_) return;
        QPainter painter(viewport());
        painter.setPen(QPen(QColor("#4DD811"), 2));
        painter.drawLine(4, *insertion_y_, viewport()->width() - 4, *insertion_y_);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key()==Qt::Key_Escape && dragged_item_.isValid()) {
            clear_reorder();event->accept();return;
        }
        QTreeWidget::keyPressEvent(event);
    }
private:
    QPersistentModelIndex dragged_item_;
    QString before_id_;
    bool drop_allowed_{};
    void clear_reorder() {
        dragged_item_=QPersistentModelIndex{};
        drag_started_=false;drop_allowed_=false;insertion_y_.reset();
        viewport()->unsetCursor();viewport()->update();
    }
    void update_reorder(const QPoint& position) {
        drop_allowed_=false;insertion_y_.reset();
        auto* source=itemFromIndex(dragged_item_);
        if (!source || !reorder_enabled || !reorder_enabled(source)) { clear_reorder();return; }
        if (position.y()<18) verticalScrollBar()->setValue(verticalScrollBar()->value()-verticalScrollBar()->singleStep());
        else if (position.y()>viewport()->height()-18) verticalScrollBar()->setValue(verticalScrollBar()->value()+verticalScrollBar()->singleStep());
        auto* parent=source->parent();
        auto* under=itemAt(position);
        while (under && under->parent()!=parent) under=under->parent();
        if (under && under->parent()!=parent) under=nullptr;
        const auto family=[](QTreeWidgetItem* row) {
            const auto role=row->data(0,Qt::UserRole+3).toString();
            if (role=="part-occurrence" || role=="assembly-occurrence") return QString("occurrence");
            if (role=="part-container" || role=="part-sketch" || role=="part-construction") return QString("part-history");
            return role;
        };
        if (parent && (!itemAt(position) || (under && family(under)==family(source)))) {
            QTreeWidgetItem* before=nullptr;
            QTreeWidgetItem* last=nullptr;
            for (int i=0;i<parent->childCount();++i) {
                auto* sibling=parent->child(i);
                if (family(sibling)!=family(source)) continue;
                last=sibling;
                if (position.y()<visualItemRect(sibling).center().y()) { before=sibling;break; }
            }
            before_id_=before ? before->data(0,Qt::UserRole).toString() : QString{};
            if (last && reorder_requested && reorder_requested(source,before_id_,false)) {
                drop_allowed_=true;
                if (before) insertion_y_=visualItemRect(before).top();
                else {
                    auto* bottom=last;
                    while (bottom->isExpanded() && bottom->childCount()) bottom=bottom->child(bottom->childCount()-1);
                    insertion_y_=visualItemRect(bottom).bottom()+1;
                }
            }
        }
        viewport()->setCursor(drop_allowed_ ? Qt::ClosedHandCursor : Qt::ForbiddenCursor);
        viewport()->update();
    }
    bool dragging_cursor_{};
    bool drag_started_{};
    QPoint drag_origin_;
    std::optional<int> insertion_y_;
};

}
