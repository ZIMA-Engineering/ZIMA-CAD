#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>
#include <cmath>

namespace zima::app {
class ViewDescriptionFields final:public QWidget {
public:
    std::function<void()> changed;
    void set_computed_text(const QString& name,double scale) {
        for(int r=0;r<int(rows_.size());++r) {
            auto* text=qobject_cast<QLineEdit*>(table_->cellWidget(r,3));
            if(rows_[r].kind==drawing::ViewDescriptionKind::Name)text->setPlaceholderText(name);
            if(rows_[r].kind==drawing::ViewDescriptionKind::Scale)text->setPlaceholderText(scale>=1?QStringLiteral("%1:1").arg(scale,0,'g',6):QStringLiteral("1:%1").arg(1/scale,0,'g',6));
        }
    }
    explicit ViewDescriptionFields(std::vector<drawing::ViewDescriptionRow> rows,QWidget* parent)
        :QWidget(parent),rows_(std::move(rows)) {
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        table_=new QTableWidget(this);table_->setObjectName("drawingViewDescriptions");
        table_->setColumnCount(6);table_->setHorizontalHeaderLabels({tr("Pořadí"),tr("Zobrazit"),tr("Řádek"),tr("Text"),tr("Výška [mm]"),tr("Barva")});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        for(int c:{0,1,2,4,5})table_->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        table_->setMinimumHeight(150);table_->setMaximumHeight(150);layout->addWidget(table_);
        auto* actions=new QHBoxLayout;layout->addLayout(actions);
        up_=new QPushButton(ui::reference_arrow_icon(Qt::UpArrow),tr("Nahoru"),this);
        down_=new QPushButton(ui::reference_arrow_icon(Qt::DownArrow),tr("Dolů"),this);
        up_->setObjectName("drawingDescriptionUp");down_->setObjectName("drawingDescriptionDown");
        actions->addWidget(up_);actions->addWidget(down_);actions->addStretch();
        connect(up_,&QPushButton::clicked,this,[this]{move(-1);});connect(down_,&QPushButton::clicked,this,[this]{move(1);});
        populate();
    }
    std::vector<drawing::ViewDescriptionRow> values()const {
        auto rows=rows_;
        for(int r=0;r<int(rows.size());++r) {
            rows[r].visible=qobject_cast<QCheckBox*>(table_->cellWidget(r,1))->isChecked();
            rows[r].text=qobject_cast<QLineEdit*>(table_->cellWidget(r,3))->text().toStdString();
            const auto height=qobject_cast<QDoubleSpinBox*>(table_->cellWidget(r,4))->value();
            if(height!=std::round(rows_[r].height*1000)/1000)rows[r].height=height;
            rows[r].color=qobject_cast<QComboBox*>(table_->cellWidget(r,5))->currentData().toString().toStdString();
        }
        return rows;
    }
private:
    std::vector<drawing::ViewDescriptionRow> rows_;int selected_{-1};bool loading_{};
    QTableWidget* table_{};QPushButton *up_{},*down_{};
    void notify(){if(!loading_&&changed)changed();}
    void update_buttons(){up_->setEnabled(selected_>0);down_->setEnabled(selected_>=0&&selected_+1<int(rows_.size()));}
    void move(int delta){if(selected_<0||selected_+delta<0||selected_+delta>=int(rows_.size()))return;rows_=values();std::swap(rows_[selected_],rows_[selected_+delta]);selected_+=delta;populate();notify();}
    void populate() {
        loading_=true;table_->setRowCount(0);table_->setRowCount(int(rows_.size()));
        for(int r=0;r<int(rows_.size());++r) {
            const auto& row=rows_[r];auto* order=new QCheckBox(QString::number(r+1),table_);order->setChecked(r==selected_);table_->setCellWidget(r,0,order);
            connect(order,&QCheckBox::clicked,this,[this,r](bool checked){rows_=values();selected_=checked?r:-1;populate();});
            auto* visible=new QCheckBox(table_);visible->setChecked(row.visible);table_->setCellWidget(r,1,visible);connect(visible,&QCheckBox::toggled,this,[this]{notify();});
            auto* type=new QTableWidgetItem(row.kind==drawing::ViewDescriptionKind::Name?tr("Název"):row.kind==drawing::ViewDescriptionKind::Scale?tr("Měřítko"):tr("Text"));type->setFlags(Qt::ItemIsEnabled);
            type->setSizeHint({QFontMetrics(table_->font()).horizontalAdvance(type->text())+24,32});table_->setItem(r,2,type);
            auto* text=new QLineEdit(QString::fromStdString(row.text),table_);text->setMaxLength(4096);text->setReadOnly(row.kind!=drawing::ViewDescriptionKind::Text);
            text->setToolTip(tr("Parametry používají stejný zápis jako razítko, například &document.file_stem nebo &drawing.revision."));table_->setCellWidget(r,3,text);connect(text,&QLineEdit::textChanged,this,[this]{notify();});
            auto* height=new QDoubleSpinBox(table_);height->setRange(.5,100);height->setDecimals(3);height->setValue(row.height);table_->setCellWidget(r,4,height);connect(height,&QDoubleSpinBox::valueChanged,this,[this]{notify();});
            auto* color=new QComboBox(table_);
            for(const auto& choice:std::vector<std::pair<QString,QString>>{{tr("Bílá"),"#ffffff"},{tr("Zelená"),"#00ff00"},{tr("Žlutá"),"#ffff00"},{tr("Červená"),"#ff0000"}})color->addItem(choice.first,choice.second);
            auto index=color->findData(QString::fromStdString(row.color));if(index<0){color->addItem(QString::fromStdString(row.color),QString::fromStdString(row.color));index=color->count()-1;}color->setCurrentIndex(index);
            table_->setCellWidget(r,5,color);connect(color,&QComboBox::currentIndexChanged,this,[this]{notify();});
            table_->setRowHeight(r,32);
        }
        update_buttons();loading_=false;
    }
};
}
