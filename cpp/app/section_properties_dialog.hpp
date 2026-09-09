#pragma once
#include <zima/document/part_document.hpp>
#include "sweep_placement_dialog.hpp"
#include <zima/document/section.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QScrollArea>
#include <functional>
#include <cmath>
#include <numbers>
namespace zima::app {
class SectionComponentsWidget final : public QTableWidget {
public:
    explicit SectionComponentsWidget(QWidget* parent):QTableWidget(parent){
        setObjectName("sectionComponents");setColumnCount(6);setHorizontalHeaderLabels({tr("Díl / těleso"),tr("Řezání"),tr("Vlastní šrafy"),tr("Úhel"),tr("Rozteč [mm]"),tr("Typ")});
        horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        setSelectionBehavior(QAbstractItemView::SelectRows);setSelectionMode(QAbstractItemView::ExtendedSelection);setMinimumHeight(130);setMaximumHeight(220);
    }
    void set_components(const std::map<std::string,std::string>& names,const std::map<std::string,zima::document::SectionComponent>& settings){
        setRowCount(0);for(const auto& [id,name]:names){const int row=rowCount();insertRow(row);auto* item=new QTableWidgetItem(QString::fromStdString(name));item->setData(Qt::UserRole,QString::fromStdString(id));item->setFlags(item->flags()&~Qt::ItemIsEditable);setItem(row,0,item);
            const auto found=settings.find(id);const auto c=found==settings.end()?zima::document::SectionComponent{}:found->second;
            auto* mode=new QComboBox(this);mode->addItems({tr("Řezat + šrafovat"),tr("Řezat bez šraf"),tr("Neřezat")});mode->setCurrentIndex(c.mode);setCellWidget(row,1,mode);
            auto* custom=new QCheckBox(this);custom->setChecked(c.custom_hatch);setCellWidget(row,2,custom);
            auto* angle=new QDoubleSpinBox(this);angle->setRange(-360,360);angle->setValue(c.hatch.angle);setCellWidget(row,3,angle);
            auto* spacing=new QDoubleSpinBox(this);spacing->setRange(.1,100);spacing->setDecimals(2);spacing->setValue(c.hatch.spacing_mm);setCellWidget(row,4,spacing);
            auto* pattern=new QComboBox(this);pattern->addItems({tr("Rovnoběžné"),tr("Křížové"),tr("Čárkované")});pattern->setCurrentIndex(c.hatch.pattern);setCellWidget(row,5,pattern);
            offsets_[id]=c.hatch.offset_mm;
            connect(mode,&QComboBox::currentIndexChanged,this,[this,row](int value){propagate(row,1,value);notify();});
            connect(custom,&QCheckBox::toggled,this,[this,row](bool value){propagate(row,2,value);notify();});
            connect(angle,&QDoubleSpinBox::valueChanged,this,[this,row](double value){propagate(row,3,value);notify();});
            connect(spacing,&QDoubleSpinBox::valueChanged,this,[this,row](double value){propagate(row,4,value);notify();});
            connect(pattern,&QComboBox::currentIndexChanged,this,[this,row](int value){propagate(row,5,value);notify();});
        }setVisible(!names.empty());
    }
    std::map<std::string,zima::document::SectionComponent> values()const{
        std::map<std::string,zima::document::SectionComponent> result;
        for(int row=0;row<rowCount();++row){const auto id=item(row,0)->data(Qt::UserRole).toString().toStdString();auto& c=result[id];c.mode=static_cast<QComboBox*>(cellWidget(row,1))->currentIndex();c.custom_hatch=static_cast<QCheckBox*>(cellWidget(row,2))->isChecked();c.hatch.angle=static_cast<QDoubleSpinBox*>(cellWidget(row,3))->value();c.hatch.spacing_mm=static_cast<QDoubleSpinBox*>(cellWidget(row,4))->value();c.hatch.pattern=static_cast<QComboBox*>(cellWidget(row,5))->currentIndex();c.hatch.offset_mm=offsets_.contains(id)?offsets_.at(id):0;}
        return result;
    }
    void select_component(const std::string& id){for(int row=0;row<rowCount();++row)if(item(row,0)->data(Qt::UserRole).toString().toStdString()==id){selectRow(row);scrollToItem(item(row,0));return;}}
    std::function<void()> changed;
private:
    std::map<std::string,double> offsets_;
    void notify(){if(changed)changed();}
    void propagate(int source,int col,double value){
        if(!item(source,0)->isSelected())return;
        for(int row=0;row<rowCount();++row)if(row!=source&&item(row,0)->isSelected()){auto* w=cellWidget(row,col);QSignalBlocker block(w);if(auto* combo=qobject_cast<QComboBox*>(w))combo->setCurrentIndex(static_cast<int>(value));else if(auto* check=qobject_cast<QCheckBox*>(w))check->setChecked(value!=0);else if(auto* spin=qobject_cast<QDoubleSpinBox*>(w))spin->setValue(value);}
    }
};
class SectionPropertiesDialog final : public SweepPlacementDialog {
public:
    SectionPropertiesDialog(QWidget* parent,zima::document::SectionDefinition initial,
        std::function<bool(zima::document::SectionDefinition)> commit,std::function<void()> pick)
        :SweepPlacementDialog(tr("Řez"),container(initial),parent),value_(std::move(initial)),commit_(std::move(commit)){
        setObjectName("sectionProperties");auto* form=new QFormLayout;
        name_=new QLineEdit(QString::fromStdString(value_.name),this);name_->setObjectName("sectionName");form->addRow(tr("Název"),name_);content_layout()->addLayout(form);
        install_placement();
        auto* row=new QHBoxLayout;plane_=new QComboBox(this);plane_->setObjectName("sectionSketchPlane");plane_->addItems({"XY","XZ","YZ"});plane_->setCurrentIndex(static_cast<int>(value_.sketch.plane));
        row->addWidget(new QLabel(tr("Skicová rovina kontejneru"),this));row->addWidget(plane_);auto* sketch=new QPushButton(tr("Sketch…"),this);sketch->setObjectName("editSectionSketch");row->addWidget(sketch);content_layout()->addLayout(row);
        connect(plane_,&QComboBox::currentIndexChanged,this,[this](int i){value_.sketch.plane=static_cast<zima::sketcher::SketchPlane>(i);if(changed)changed();});
        connect(sketch,&QPushButton::clicked,this,[this]{if(edit_sketch)edit_sketch(0);});
        sketch_info_=new QLabel(this);content_layout()->addWidget(sketch_info_);update_sketch_info();
        reverse_=new QCheckBox(tr("Obrátit stranu řezu"),this);reverse_->setObjectName("sectionReverse");reverse_->setChecked(value_.reversed);content_layout()->addWidget(reverse_);
        show_=new QCheckBox(tr("Zobrazit řeznou plochu"),this);show_->setObjectName("sectionShowPlane");show_->setChecked(value_.show_plane);content_layout()->addWidget(show_);
        cut_=new QCheckBox(tr("Aktivní řez"),this);cut_->setObjectName("sectionShowCut");cut_->setChecked(value_.show_cut);content_layout()->addWidget(cut_);
        for(auto* box:{reverse_,show_,cut_})connect(box,&QCheckBox::toggled,this,[this]{if(changed)changed();});
        auto* choose=new QPushButton(tr("Vybrat díl ve View"),this);choose->setObjectName("pickSectionComponent");content_layout()->addWidget(choose);connect(choose,&QPushButton::clicked,this,std::move(pick));
        components_=new SectionComponentsWidget(this);components_->set_components(value_.component_names,value_.components);components_->changed=[this]{if(changed)changed();};content_layout()->addWidget(components_);choose->setVisible(!value_.component_names.empty());
        error_=new QLabel(this);error_->setWordWrap(true);content_layout()->addWidget(error_);
        // The shared placement controls keep their own contract inside one
        // scrollable body; OK/Cancel stay visible outside it.
        auto* body=new QWidget(this);auto* layout=new QVBoxLayout(body);while(auto* item=content_layout()->takeAt(0)){
            if(auto* widget=item->widget()){layout->addWidget(widget);delete item;}
            else if(auto* child=item->layout()){child->setParent(nullptr);layout->addLayout(child);}
            else layout->addItem(item);
        }
        auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setWidget(body);content_layout()->addWidget(scroll);
        setMinimumWidth(660);setAttribute(Qt::WA_DeleteOnClose);
    }
    zima::document::SectionDefinition values()const{
        auto s=value_;s.placement=pending.placement;s.name=name_->text().trimmed().toStdString();s.reversed=reverse_->isChecked();s.show_plane=show_->isChecked();s.show_cut=cut_->isChecked();s.components=components_->values();zima::document::reframe_section(s);return s;
    }
    void set_sketch(unsigned,const zima::sketcher::Sketch& sketch)override{value_.sketch=sketch;update_sketch_info();}
    void select_component(const std::string& id){components_->select_component(id);}
    void set_status(const QString& message)override{set_error(message);}
    void set_error(const QString& message){error_->setText(message);}
private:
    static zima::document::HistoryContainer container(const zima::document::SectionDefinition& s){zima::document::HistoryContainer c;c.id=s.id;c.feature_id=s.sketch.id;c.name=s.name;c.container_origin=s.container_origin;c.placement=s.placement;return c;}
    zima::document::SectionDefinition value_;std::function<bool(zima::document::SectionDefinition)> commit_;
    QLineEdit* name_{};QComboBox* plane_{};QCheckBox *reverse_{},*show_{},*cut_{};SectionComponentsWidget* components_{};QLabel *error_{},*sketch_info_{};
    void update_sketch_info(){sketch_info_->setText(tr("Skica řezu: %1 úseček. Kreslete jednu otevřenou čáru nebo lomenou čáru.").arg(value_.sketch.segments.size()));}
    bool submit() override{try{if(changed)changed();auto value=values();if(value.name.empty())throw std::runtime_error("Vyplňte název řezu.");static_cast<void>(zima::document::section_frame(value));return commit_(std::move(value));}catch(const std::exception& e){set_error(QString::fromUtf8(e.what()));return false;}}
};
} // namespace zima::app
