#pragma once
#include "symbol_labels.hpp"
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
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <set>

namespace zima::app {
class SymbolDialog:public ui::PropertiesSubWindow {
public:
    using Instance=sketcher::SymbolInstance;
    SymbolDialog(Instance value,std::function<void(const Instance&)> preview,std::function<void(Instance)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti symbolu"),parent),value_(std::move(value)),definition_(symbols::Definition::from_serialized(value_.definition)),preview_(std::move(preview)),commit_(std::move(commit)) {
        setObjectName("symbolPropertiesDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("expandBottomTable",true);
        auto* scroll=new QScrollArea(this);scroll->setObjectName("symbolPropertiesScroll");
        scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        auto* editor=new QWidget(scroll);editor_layout_=new QVBoxLayout(editor);editor_layout_->setContentsMargins(0,0,0,0);
        editor_layout_->setSizeConstraint(QLayout::SetMinAndMaxSize);scroll->setWidget(editor);content_layout()->addWidget(scroll,1);
        auto* form=new QFormLayout;editor_layout_->addLayout(form);
        editor_layout_->addStretch();
        form->addRow(tr("Symbol"),new QLabel(QString::fromStdString(definition_.name),this));
        variant_=new QComboBox(this);variant_->setObjectName("symbolVariant");
        for(const auto& [key,row]:definition_.variants)variant_->addItem(variant_label(key),QString::fromStdString(key));
        variant_->setCurrentIndex(variant_->findData(QString::fromStdString(value_.variant.empty()?definition_.default_variant:value_.variant)));
        form->addRow(tr("Varianta"),variant_);
        if(definition_.id.starts_with("ze:geometric-tolerance:")) {
            datum_hint_=new QLabel(tr("Tolerance tvaru nepoužívají základny. Pro ostatní typy zadejte základny níže v pořadí priority."),this);
            datum_hint_->setWordWrap(true);datum_hint_->setObjectName("symbolDatumHint");form->addRow(datum_hint_);
        }
        automatic_=new QCheckBox(tr("Podle nastavení výkresu"),this);automatic_->setObjectName("symbolCadVariant");
        automatic_->setChecked(value_.use_cad_variant);automatic_->setVisible(!definition_.variant_source.empty());form->addRow({},automatic_);
        variant_->setEnabled(!automatic_->isChecked());
        std::vector<std::string> field_order;
        for(const auto* key:{"Tolerance","Primary datum","Secondary datum","Tertiary datum"})if(definition_.fields.contains(key))field_order.emplace_back(key);
        for(const auto& [key,field]:definition_.fields)if(std::ranges::find(field_order,key)==field_order.end())field_order.push_back(key);
        for(const auto& key:field_order) {
            const auto& field=definition_.fields.at(key);
            auto* input=new QComboBox(this);input->setEditable(field.allow_custom);input->setObjectName(QString::fromStdString("symbolField:"+key));
            for(const auto& choice:field.choices)input->addItem(QString::fromStdString(choice));
            const auto row=definition_.variants.at(variant_->currentData().toString().toStdString());
            const auto text=value_.text_values.contains(key)?value_.text_values.at(key):row.text_values.contains(key)?row.text_values.at(key):std::string{};
            input->setCurrentText(QString::fromStdString(text));fields_[key]=input;
            if(value_.text_values.contains(key))overrides_.insert(key);
            auto* row_widget=new QWidget(this);auto* layout=new QHBoxLayout(row_widget);layout->setContentsMargins(0,0,0,0);layout->addWidget(input,1);field_rows_[key]=row_widget;form->addRow(field_label(key),row_widget);field_labels_[key]=form->labelForField(row_widget);
            connect(input,&QComboBox::currentTextChanged,this,[this,key]{overrides_.insert(key);update_preview();});
        }
        const std::array labels{tr("X"),tr("Y"),tr("Úhel"),tr("Měřítko")};
        const std::array values{value_.x,value_.y,value_.angle_degrees,value_.scale};
        for(std::size_t i=0;i<4;++i) {
            values_[i]=new QDoubleSpinBox(this);values_[i]->setObjectName(QString("symbolPlacement%1").arg(i));
            values_[i]->setRange(i==3?0.01:-100000,100000);values_[i]->setDecimals(ui::numeric_decimal_places(parent));values_[i]->setValue(values[i]);
            form->addRow(labels[i],values_[i]);connect(values_[i],&QDoubleSpinBox::valueChanged,this,[this]{update_preview();});
        }
        connect(variant_,&QComboBox::currentIndexChanged,this,[this]{update_preview();});
        connect(automatic_,&QCheckBox::toggled,this,[this](bool on){variant_->setEnabled(!on);update_preview();});
        update_preview();
    }
    void set_anchor(double x,double y) {const QSignalBlocker a(values_[0]),b(values_[1]);values_[0]->setValue(x);values_[1]->setValue(y);update_preview();}
    void set_preview_callback(std::function<void(const Instance&)> callback) {preview_=std::move(callback);update_preview();}
protected:
    void showEvent(QShowEvent* event) override {
        for(auto* widget:findChildren<QWidget*>())widget->ensurePolished();
        editor_layout_->activate();
        auto* scroll=findChild<QScrollArea*>("symbolPropertiesScroll");
        const auto content=editor_layout_->sizeHint().expandedTo(editor_layout_->minimumSize());
        const auto chrome=sizeHint()-scroll->sizeHint();
        const int frame=2*scroll->frameWidth();
        // Use the complete form rather than QScrollArea's capped size hint.
        // The shared window helper bounds it to the owner; scrolling remains
        // available when the owner's viewport cannot contain the whole form.
        set_initial_size((content+chrome+QSize(frame,frame)).expandedTo(QSize(420,360)));
        ui::PropertiesSubWindow::showEvent(event);
    }
    QVBoxLayout* editor_layout()const{return editor_layout_;}
    bool submit() override {
        auto value=pending();value.validate();
        if(definition_.id.starts_with("ze:geometric-tolerance:")) {
            auto tolerance=fields_.at("Tolerance")->currentText().trimmed();tolerance.replace(',', '.');bool numeric=false;
            const double number=tolerance.toDouble(&numeric);
            if(!numeric||!std::isfinite(number)||number<=0)throw std::invalid_argument(tr("Tolerance musí být kladné číslo.").toStdString());
            value.text_values["Tolerance"]=tolerance.toStdString();
            const auto& selected=definition_.variants.at(value.variant);
            if(fields_.contains("Primary datum")&&std::ranges::find(selected.sketches,definition_.fields.at("Primary datum").sketch_id)!=selected.sketches.end()) {
                bool gap=false;std::set<QString> datums;
                for(const auto* key:{"Primary datum","Secondary datum","Tertiary datum"}) {
                    const auto datum=fields_.at(key)->currentText().trimmed().toUpper();
                    if(datum.isEmpty())gap=true;
                    else if(gap||!QRegularExpression("^[A-Z]+(?:-[A-Z]+)*$").match(datum).hasMatch()||!datums.insert(datum).second)
                        throw std::invalid_argument(tr("Základny zadejte v pořadí bez mezer, velkými písmeny.").toStdString());
                    value.text_values[key]=datum.toStdString();
                }
                const bool profile=definition_.id.ends_with("LINE-PROFILE")||definition_.id.ends_with("SURFACE-PROFILE")||value.variant.starts_with("LINE-PROFILE/")||value.variant.starts_with("SURFACE-PROFILE/");
                if(!profile&&value.text_values["Primary datum"].empty())throw std::invalid_argument(tr("Tato tolerance vyžaduje primární základnu.").toStdString());
            }
        }
        if(definition_.reference_line_layout) {
            for(const std::string key:{"Arrow size","Other size"}) {
                const auto& field=definition_.fields.at(key);
                const auto& row=definition_.variants.at(value.variant);
                if(std::ranges::find(row.sketches,field.sketch_id)==row.sketches.end()||std::ranges::find(row.hidden_texts,key)!=row.hidden_texts.end())continue;
                auto size=fields_.at(key)->currentText().trimmed();size.replace(',', '.');
                const auto match=QRegularExpression("^[asz]?([0-9]+(?:[.][0-9]+)?)$").match(size);
                if(!match.hasMatch()||match.captured(1).toDouble()<=0)
                    throw std::invalid_argument(tr("Rozměr svaru musí být kladné číslo s volitelnou značkou a, z nebo s.").toStdString());
                value.text_values[key]=size.toStdString();
            }
        }
        commit_(std::move(value));return true;
    }
private:
    QString field_label(const std::string& key) const { return symbol_field_name(definition_,key); }
    QString variant_label(const std::string& key) const {
        if(key.find('/')!=std::string::npos)return symbol_variant_name(definition_,key);
        if(definition_.reference_line_layout&&(key=="other_side"||key=="arrow_side"))return key=="other_side"?tr("ISO 2553 A — opačná strana"):tr("ISO 2553 A — strana šipky");
        if(definition_.id=="ze:annotation:text"&&key=="text")return tr("Text");
        if(definition_.id.starts_with("ze:geometric-tolerance:")&&key=="default")return tr("Geometrická tolerance");
        if(definition_.id=="ze:general-edges:iso13715") {
            const auto scope=key.substr(0,key.find('_'));
            if((scope!="general"&&scope!="external"&&scope!="internal"&&scope!="all")||
                (key!=scope&&key!=scope+"_exception"&&key!=scope+"_exceptions"))return QString::fromStdString(key);
            auto label=scope=="general"?tr("Vnější a vnitřní hrany"):scope=="external"?tr("Vnější hrany"):scope=="internal"?tr("Vnitřní hrany"):tr("Všechny hrany");
            if(key.ends_with("_exceptions"))return tr("%1 — více výjimek").arg(label);
            if(key.ends_with("_exception"))return tr("%1 — jedna výjimka").arg(label);
            return label;
        }
        if(is_surface_texture(definition_))return symbol_variant_name(definition_,key);
        return QString::fromStdString(key);
    }
    Instance pending() const {
        auto value=value_;value.variant=variant_->currentData().toString().toStdString();value.use_cad_variant=automatic_->isChecked();
        value.x=values_[0]->value();value.y=values_[1]->value();value.angle_degrees=values_[2]->value();value.scale=values_[3]->value();
        value.text_values.clear();for(const auto& [key,input]:fields_)if(overrides_.contains(key))value.text_values[key]=input->currentText().toStdString();return value;
    }
    void update_preview(){
        const auto row=definition_.variants.at(variant_->currentData().toString().toStdString());
        if(datum_hint_)datum_hint_->setVisible(!definition_.fields.contains("Primary datum")||std::ranges::find(row.sketches,definition_.fields.at("Primary datum").sketch_id)==row.sketches.end());
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
    QVBoxLayout* editor_layout_{};
    QLabel* datum_hint_{};
    QComboBox* variant_{};QCheckBox* automatic_{};std::array<QDoubleSpinBox*,4> values_{};std::map<std::string,QComboBox*> fields_;std::set<std::string> overrides_;std::map<std::string,QWidget*> field_rows_,field_labels_;
};
}
