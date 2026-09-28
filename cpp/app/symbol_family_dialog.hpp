#pragma once
#include "symbol_labels.hpp"
#include "resource_icon.hpp"
#include <zima/symbols/definition.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

namespace zima::app {
class SymbolFamilyDialog final:public ui::PropertiesSubWindow {
public:
    SymbolFamilyDialog(symbols::Definition value,std::function<void(symbols::Definition)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Family Table"),parent),value_(std::move(value)),commit_(std::move(commit)) {
        setObjectName("symbolFamilyDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({760,460});
        setProperty("expandBottomTable",true);
        // Text visibility uses the same named fields consumed by insertion.
        for(const auto& sketch:value_.sketches)for(const auto& text:sketch.texts) {
            if(std::ranges::any_of(value_.fields,[&](const auto& item){return item.second.sketch_id==sketch.id&&item.second.text_id==text.id;}))continue;
            auto key=text.value.empty()?text.id:text.value;
            const auto base=key;int suffix=2;while(value_.fields.contains(key))key=base+" "+std::to_string(suffix++);
            value_.fields[key]={sketch.id,text.id,{},true};
        }
        auto* actions=new QHBoxLayout;
        auto* add=new QPushButton(tr("Přidat"),this);add->setObjectName("symbolVariantAdd");
        auto* remove=new QPushButton(tr("Odstranit"),this);remove->setObjectName("symbolVariantRemove");
        add->setIcon(resource_icon("new"));remove->setIcon(resource_icon("delete"));
        actions->addWidget(add);actions->addWidget(remove);actions->addStretch();
        content_layout()->addLayout(actions);
        table_=new QTableWidget(this);table_->setObjectName("symbolFamilyTable");
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->verticalHeader()->setDefaultSectionSize(28);
        auto* bottom=new QHBoxLayout;bottom->addWidget(new QLabel(tr("Výchozí varianta"),this));
        defaults_=new QComboBox(this);defaults_->setObjectName("symbolDefaultVariant");bottom->addWidget(defaults_,1);
        content_layout()->addLayout(bottom);content_layout()->addWidget(table_,1);rebuild();
        connect(add,&QPushButton::clicked,this,[this]{
            if(!read_action())return;auto key=tr("Varianta").toStdString();const auto base=key;int n=2;
            while(value_.variants.contains(key))key=base+" "+std::to_string(n++);
            value_.variants[key]=value_.variants.at(value_.default_variant);rebuild();
            for(int row=0;row<table_->rowCount();++row)if(table_->item(row,0)->text().toStdString()==key){table_->setCurrentCell(row,0);table_->editItem(table_->item(row,0));}
        });
        connect(remove,&QPushButton::clicked,this,[this]{
            if(table_->currentRow()<0||value_.variants.size()<2)return;
            const auto* item=table_->item(table_->currentRow(),0);
            const auto old=item->data(Qt::UserRole).toString().toStdString();
            const auto key=item->text()==symbol_variant_name(value_,old)?old:item->text().trimmed().toStdString();
            if(!value_.variant_source.empty()&&(key=="first_angle"||key=="third_angle"))return;
            if(!read_action())return;value_.variants.erase(key);
            if(!value_.variants.contains(value_.default_variant))value_.default_variant=value_.variants.begin()->first;
            rebuild();
        });
    }
protected:
    bool submit()override{read();value_.validate();commit_(value_);return true;}
private:
    symbols::Definition value_;std::function<void(symbols::Definition)> commit_;
    QTableWidget* table_{};QComboBox* defaults_{};
    bool read_action(){try{read();return true;}catch(const std::exception& e){QMessageBox::warning(this,tr("Symbol"),QString::fromUtf8(e.what()));return false;}}
    void rebuild(){
        table_->clear();QStringList headers{tr("Varianta")};
        for(const auto& sketch:value_.sketches)headers<<symbol_sketch_name(value_,sketch);
        for(const auto& [key,field]:value_.fields)headers<<tr("Text")+": "+symbol_field_name(value_,key);
        table_->setColumnCount(headers.size());table_->setHorizontalHeaderLabels(headers);table_->setRowCount(value_.variants.size());
        defaults_->clear();int r=0;
        for(const auto& [key,row]:value_.variants) {
            auto* name=new QTableWidgetItem(symbol_variant_name(value_,key));name->setData(Qt::UserRole,QString::fromStdString(key));
            if(!value_.variant_source.empty()&&(key=="first_angle"||key=="third_angle"))name->setFlags(name->flags()&~Qt::ItemIsEditable);
            table_->setItem(r,0,name);int c=1;
            const auto cell=[&](bool on){auto* item=new QTableWidgetItem;item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);item->setCheckState(on?Qt::Checked:Qt::Unchecked);table_->setItem(r,c++,item);};
            for(const auto& sketch:value_.sketches)cell(std::ranges::find(row.sketches,sketch.id)!=row.sketches.end());
            for(const auto& [field,info]:value_.fields)cell(std::ranges::find(row.hidden_texts,field)==row.hidden_texts.end());
            defaults_->addItem(symbol_variant_name(value_,key),QString::fromStdString(key));++r;
        }
        defaults_->setCurrentIndex(defaults_->findData(QString::fromStdString(value_.default_variant)));
    }
    void read(){
        auto rows=value_.variants;rows.clear();std::string selected;
        const auto old_default=defaults_->currentData().toString().toStdString();
        for(int r=0;r<table_->rowCount();++r) {
            const auto old=table_->item(r,0)->data(Qt::UserRole).toString().toStdString();
            const auto label=table_->item(r,0)->text().trimmed();
            const auto key=label==symbol_variant_name(value_,old)?old:label.toStdString();
            if(key.empty()||rows.contains(key))throw std::invalid_argument(tr("Názvy variant musí být vyplněné a jedinečné.").toStdString());
            auto row=value_.variants.at(old);int c=1;std::vector<std::string> visible,hidden;
            for(const auto& sketch:value_.sketches)if(table_->item(r,c++)->checkState()==Qt::Checked)visible.push_back(sketch.id);
            for(const auto& [field,info]:value_.fields)if(table_->item(r,c++)->checkState()!=Qt::Checked)hidden.push_back(field);
            const auto retain_order=[](auto& stored,const auto& selected){
                std::erase_if(stored,[&](const auto& id){return std::ranges::find(selected,id)==selected.end();});
                for(const auto& id:selected)if(std::ranges::find(stored,id)==stored.end())stored.push_back(id);
            };
            retain_order(row.sketches,visible);retain_order(row.hidden_texts,hidden);
            rows[key]=std::move(row);if(old==old_default)selected=key;
        }
        auto checked=value_;checked.variants=std::move(rows);checked.default_variant=selected;
        checked.validate();value_=std::move(checked);
    }
};
}
