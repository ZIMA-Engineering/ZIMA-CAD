#pragma once

#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <functional>
#include <map>

namespace zima::app {

// Row actions occupy the vertical header, leaving data-column identities intact.
// Reuse the same indicator widgets as container placement without changing its contract.
class EntryRowHeader final : public QHeaderView {
public:
    explicit EntryRowHeader(QTableWidget* table) : QHeaderView(Qt::Vertical,table) {
        setFixedWidth(34);setMinimumSectionSize(32);setDefaultSectionSize(32);
        connect(this,&QHeaderView::geometriesChanged,this,[this]{position_actions();});
        connect(this,&QHeaderView::sectionResized,this,[this]{position_actions();});
    }
    void set_action(int row,bool populated,std::function<void()> remove,
                    std::function<void()> enter={}) {
        if(auto old=actions_[row]) old->deleteLater();
        auto* indicator=zima::ui::build_reference_row_indicator(std::move(remove));
        indicator->setParent(viewport());
        indicator->setObjectName(QString("tableRowAction%1").arg(row));
        zima::ui::set_reference_row_populated(indicator,populated);
        indicator->setToolTip(populated?tr("Odstranit položku"):tr("Zadat novou položku"));
        for(const auto* property:{"_arrowWidget","_removeWidget"})
            if(auto* widget=qobject_cast<QWidget*>(indicator->property(property).value<QObject*>()))
                widget->setToolTip(indicator->toolTip());
        if(enter) {
            auto* arrow=indicator->property("_arrowWidget").value<QObject*>();
            auto* filter=new ArrowClick(std::move(enter),indicator);
            arrow->installEventFilter(filter);
        }
        actions_[row]=indicator;position_actions();
    }
    void clear_actions() {
        for(auto& [row,widget]:actions_) if(widget) {widget->setObjectName({});widget->hide();widget->deleteLater();}
        actions_.clear();
    }
    void set_open_action(int row,const QIcon& icon,std::function<void()> open) {
        auto* button=new QToolButton(viewport());button->setAutoRaise(true);
        button->setIcon(icon);button->setFixedSize(28,28);
        button->setObjectName(QString("tableRowOpen%1").arg(row));
        button->setToolTip(tr("Otevřít instanci"));
        connect(button,&QToolButton::clicked,this,[open=std::move(open)]{open();});
        actions_[row]=button;position_actions();
    }
protected:
    void paintSection(QPainter*,const QRect&,int) const override {}
    void scrollContentsBy(int dx,int dy) override {
        QHeaderView::scrollContentsBy(dx,dy);position_actions();
    }
    void resizeEvent(QResizeEvent* event) override {
        QHeaderView::resizeEvent(event);position_actions();
    }
private:
    class ArrowClick final : public QObject {
    public:
        ArrowClick(std::function<void()> callback,QObject* parent)
            : QObject(parent),callback_(std::move(callback)) {}
        bool eventFilter(QObject*,QEvent* event) override {
            if(event->type()==QEvent::MouseButtonRelease &&
                static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
                callback_();return true;
            }
            return false;
        }
    private:
        std::function<void()> callback_;
    };
    void position_actions() {
        for(auto& [row,widget]:actions_) if(widget) {
            const int y=sectionViewportPosition(row);
            widget->move(2,y+(sectionSize(row)-widget->height())/2);
            widget->setVisible(row<count()&&!isSectionHidden(row)&&y+sectionSize(row)>0&&y<height());
        }
    }
    std::map<int,QPointer<QWidget>> actions_;
};

inline EntryRowHeader* entry_row_header(QTableWidget* table) {
    auto* header=dynamic_cast<EntryRowHeader*>(table->verticalHeader());
    if(!header) {header=new EntryRowHeader(table);table->setVerticalHeader(header);}
    header->show();return header;
}

inline bool table_row_has_text(QTableWidget* table,int row) {
    for(int column=0;column<table->columnCount();++column) {
        if(auto* item=table->item(row,column);item&&!item->text().trimmed().isEmpty()) return true;
        if(auto* combo=qobject_cast<QComboBox*>(table->cellWidget(row,column));combo&&!combo->currentText().trimmed().isEmpty()) return true;
    }
    return false;
}

inline void edit_table_cell(QTableWidget* table,int row,int column) {
    table->setCurrentCell(row,column);
    if(auto* item=table->item(row,column))table->scrollToItem(item);
    if(auto* widget=table->cellWidget(row,column)) {
        widget->setFocus();
        if(auto* combo=qobject_cast<QComboBox*>(widget);combo&&combo->lineEdit()) combo->lineEdit()->selectAll();
    } else if(auto* item=table->item(row,column);item&&(item->flags()&Qt::ItemIsEditable)) {
        table->editItem(item);
    }
}

// Keep one offered blank row. Deletion addresses a persistent row index so that
// an earlier deletion cannot redirect a later action to a different entry.
class TableEntryRows final : public QObject {
public:
    TableEntryRows(QTableWidget* table,std::function<void()> append,int protected_rows=0,
                  std::function<void(int)> open_row={},QIcon open_icon={})
        : QObject(table),table_(table),append_(std::move(append)),protected_rows_(protected_rows),
          open_row_(std::move(open_row)),open_icon_(std::move(open_icon)) {
        table_->verticalHeader()->show();
        table_->verticalHeader()->setMinimumSectionSize(32);table_->verticalHeader()->setDefaultSectionSize(32);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);table_->setColumnWidth(0,34);
        if(open_row_)entry_row_header(table_);
        connect(table_,&QTableWidget::itemChanged,this,[this]{schedule();});
        connect(table_->model(),&QAbstractItemModel::rowsInserted,this,[this]{schedule();});
        connect(table_->model(),&QAbstractItemModel::rowsRemoved,this,[this]{schedule();});
        connect(table_->model(),&QAbstractItemModel::rowsAboutToBeRemoved,this,[this](const QModelIndex&,int first,int last){
            for(int row=first;row<=last;++row)if(auto* widget=table_->cellWidget(row,0))widget->hide();
        });
        connect(table_->model(),&QAbstractItemModel::modelReset,this,[this]{schedule();});
        refresh();
    }
    void refresh() {
        if(refreshing_) return;
        refreshing_=true;
        if(table_->rowCount()<=protected_rows_ || table_row_has_text(table_,table_->rowCount()-1)) append_();
        auto* header=open_row_?entry_row_header(table_):nullptr;if(header)header->clear_actions();
        for(int row=protected_rows_;row<table_->rowCount();++row) {
            const QPersistentModelIndex index(table_->model()->index(row,1));
            const bool populated=row<table_->rowCount()-1||table_row_has_text(table_,row);
            auto* indicator=zima::ui::build_reference_row_indicator([this,index] {
                if(index.isValid()) table_->removeRow(index.row());
                refresh();
            });
            indicator->setObjectName(QString("tableRowAction%1").arg(row));
            zima::ui::set_reference_row_populated(indicator,populated);
            auto* arrow=indicator->property("_arrowWidget").value<QObject*>();
            arrow->setProperty("entryRow",QVariant::fromValue(index));arrow->installEventFilter(this);
            if(auto* old=table_->cellWidget(row,0))old->hide();
            table_->setCellWidget(row,0,zima::ui::centered_cell_widget(indicator));
            if(header&&populated)header->set_open_action(row,open_icon_,[this,index]{if(index.isValid())open_row_(index.row());});
        }
        for(auto* combo:table_->findChildren<QComboBox*>()) if(!combo->property("entryRowsBound").toBool()) {
            combo->setProperty("entryRowsBound",true);
            connect(combo,&QComboBox::currentTextChanged,this,[this]{schedule();});
        }
        refreshing_=false;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
            const auto index=object->property("entryRow").value<QPersistentModelIndex>();
            if(index.isValid()){edit_table_cell(table_,index.row(),1);return true;}
        }
        return QObject::eventFilter(object,event);
    }
private:
    void schedule() {
        if(pending_||refreshing_) return;
        pending_=true;
        QTimer::singleShot(0,this,[this]{pending_=false;refresh();});
    }
    QTableWidget* table_;
    std::function<void()> append_;
    int protected_rows_{};
    std::function<void(int)> open_row_;
    QIcon open_icon_;
    bool pending_{},refreshing_{};
};

class EnterDownDelegate final : public QStyledItemDelegate {
public:
    explicit EnterDownDelegate(QTableWidget* table) : QStyledItemDelegate(table),table_(table) {}
    bool eventFilter(QObject* editor,QEvent* event) override {
        if(event->type()==QEvent::KeyPress) {
            const auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter) {
                const int row=table_->currentRow(),column=table_->currentColumn();
                auto* widget=qobject_cast<QWidget*>(editor);
                emit commitData(widget);
                emit closeEditor(widget,QAbstractItemDelegate::NoHint);
                QTimer::singleShot(0,table_,[table=table_,row,column] {
                    if(row+1<table->rowCount()) edit_table_cell(table,row+1,column);
                });
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(editor,event);
    }
private:
    QTableWidget* table_;
};

} // namespace zima::app
