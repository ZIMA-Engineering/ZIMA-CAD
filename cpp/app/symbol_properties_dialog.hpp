#pragma once
#include <zima/symbols/definition.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <set>

namespace zima::app {
class SymbolDialog final:public ui::PropertiesSubWindow {
public:
    using Instance=sketcher::SymbolInstance;
    SymbolDialog(Instance value,std::function<void(const Instance&)> preview,std::function<void(Instance)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti symbolu"),parent),value_(std::move(value)),definition_(symbols::Definition::from_serialized(value_.definition)),preview_(std::move(preview)),commit_(std::move(commit)) {
        setObjectName("symbolPropertiesDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({420,360});
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        form->addRow(tr("Symbol"),new QLabel(QString::fromStdString(definition_.name),this));
        variant_=new QComboBox(this);variant_->setObjectName("symbolVariant");
        for(const auto& [key,row]:definition_.variants)variant_->addItem(variant_label(key),QString::fromStdString(key));
        variant_->setCurrentIndex(variant_->findData(QString::fromStdString(value_.variant.empty()?definition_.default_variant:value_.variant)));
        form->addRow(tr("Varianta"),variant_);
        automatic_=new QCheckBox(tr("Podle nastavení výkresu"),this);automatic_->setObjectName("symbolCadVariant");
        automatic_->setChecked(value_.use_cad_variant);automatic_->setVisible(!definition_.variant_source.empty());form->addRow({},automatic_);
        variant_->setEnabled(!automatic_->isChecked());
        const std::array labels{tr("X"),tr("Y"),tr("Úhel"),tr("Měřítko")};
        const std::array values{value_.x,value_.y,value_.angle_degrees,value_.scale};
        for(std::size_t i=0;i<4;++i) {
            values_[i]=new QDoubleSpinBox(this);values_[i]->setObjectName(QString("symbolPlacement%1").arg(i));
            values_[i]->setRange(i==3?0.01:-100000,100000);values_[i]->setDecimals(ui::numeric_decimal_places(parent));values_[i]->setValue(values[i]);
            form->addRow(labels[i],values_[i]);connect(values_[i],&QDoubleSpinBox::valueChanged,this,[this]{update_preview();});
        }
        for(const auto& [key,field]:definition_.fields) {
            auto* input=new QComboBox(this);input->setEditable(field.allow_custom);input->setObjectName(QString::fromStdString("symbolField:"+key));
            for(const auto& choice:field.choices)input->addItem(QString::fromStdString(choice));
            const auto row=definition_.variants.at(variant_->currentData().toString().toStdString());
            const auto text=value_.text_values.contains(key)?value_.text_values.at(key):row.text_values.contains(key)?row.text_values.at(key):std::string{};
            input->setCurrentText(QString::fromStdString(text));fields_[key]=input;
            if(value_.text_values.contains(key))overrides_.insert(key);
            auto* row_widget=new QWidget(this);auto* layout=new QHBoxLayout(row_widget);layout->setContentsMargins(0,0,0,0);layout->addWidget(input,1);field_rows_[key]=row_widget;form->addRow(field_label(key),row_widget);field_labels_[key]=form->labelForField(row_widget);
            connect(input,&QComboBox::currentTextChanged,this,[this,key]{overrides_.insert(key);update_preview();});
        }
        connect(variant_,&QComboBox::currentIndexChanged,this,[this]{update_preview();});
        connect(automatic_,&QCheckBox::toggled,this,[this](bool on){variant_->setEnabled(!on);update_preview();});
        update_preview();
    }
    void set_anchor(double x,double y) {const QSignalBlocker a(values_[0]),b(values_[1]);values_[0]->setValue(x);values_[1]->setValue(y);update_preview();}
protected:
    bool submit() override {auto value=pending();value.validate();commit_(std::move(value));return true;}
private:
    QString field_label(const std::string& key) const {
        if(!definition_.id.starts_with("ze:"))return QString::fromStdString(key);
        if(key=="Specification")return tr("Drsnost");
        if(key=="External edges")return tr("Vnější hrany");
        if(key=="Internal edges")return tr("Vnitřní hrany");
        if(key=="All edges")return tr("Všechny hrany");
        if(key=="Exception")return tr("Výjimka");
        return QString::fromStdString(key);
    }
    QString variant_label(const std::string& key) const {
        if(definition_.id=="ze:general-edges:iso13715") {
            const auto scope=key.substr(0,key.find('_'));
            auto label=scope=="general"?tr("Vnější a vnitřní hrany"):scope=="external"?tr("Vnější hrany"):scope=="internal"?tr("Vnitřní hrany"):tr("Všechny hrany");
            if(key.ends_with("_exceptions"))return tr("%1 — více výjimek").arg(label);
            if(key.ends_with("_exception"))return tr("%1 — jedna výjimka").arg(label);
            return label;
        }
        if(definition_.id=="ze:surface-texture:iso21920"||definition_.id=="ze:general-surface-texture:iso21920") {
            auto label=key=="material_removal"?tr("Úběr materiálu požadován"):key=="no_material_removal"?tr("Úběr materiálu nepřípustný"):tr("Způsob výroby neurčen");
            return definition_.id=="ze:general-surface-texture:iso21920"?tr("Celková drsnost — %1").arg(label):label;
        }
        return QString::fromStdString(key);
    }
    Instance pending() const {
        auto value=value_;value.variant=variant_->currentData().toString().toStdString();value.use_cad_variant=automatic_->isChecked();
        value.x=values_[0]->value();value.y=values_[1]->value();value.angle_degrees=values_[2]->value();value.scale=values_[3]->value();
        value.text_values.clear();for(const auto& [key,input]:fields_)if(overrides_.contains(key))value.text_values[key]=input->currentText().toStdString();return value;
    }
    void update_preview(){
        const auto row=definition_.variants.at(variant_->currentData().toString().toStdString());
        for(const auto& [key,input]:fields_) {
            const auto& field=definition_.fields.at(key);
            const bool visible=std::ranges::find(row.sketches,field.sketch_id)!=row.sketches.end()&&std::ranges::find(row.hidden_texts,key)==row.hidden_texts.end();
            field_rows_.at(key)->setVisible(visible);if(field_labels_.at(key))field_labels_.at(key)->setVisible(visible);
        }
        for(const auto& [key,input]:fields_)if(!overrides_.contains(key)) {
            const auto& field=definition_.fields.at(key);const auto& sketch=*std::ranges::find(definition_.sketches,field.sketch_id,&sketcher::Sketch::id);
            const auto& text=*std::ranges::find(sketch.texts,field.text_id,&sketcher::SketchText::id);
            const QSignalBlocker blocked(input);input->setCurrentText(QString::fromStdString(row.text_values.contains(key)?row.text_values.at(key):text.value));
        }
        if(preview_)preview_(pending());
    }
    Instance value_;symbols::Definition definition_;std::function<void(const Instance&)> preview_;std::function<void(Instance)> commit_;
    QComboBox* variant_{};QCheckBox* automatic_{};std::array<QDoubleSpinBox*,4> values_{};std::map<std::string,QComboBox*> fields_;std::set<std::string> overrides_;std::map<std::string,QWidget*> field_rows_,field_labels_;
};
}
