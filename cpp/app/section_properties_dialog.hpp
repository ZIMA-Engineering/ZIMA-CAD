#pragma once
#include "sketch_button_style.hpp"
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
#include <QStyle>
#include <QResizeEvent>
#include <functional>
#include <cmath>
#include <numbers>
namespace zima::app {
class SectionComponentsWidget final : public QTableWidget {
public:
    explicit SectionComponentsWidget(QWidget* parent):QTableWidget(parent){
        setObjectName("sectionComponents");setColumnCount(7);
        setHorizontalHeaderLabels({tr("Díl / těleso"),tr("Řezání"),tr("Úhel"),tr("Rozteč [mm]"),tr("Posunutí [mm]"),tr("Typ"),tr("Směr")});
        horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        setSelectionBehavior(QAbstractItemView::SelectRows);setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    void set_components(const std::map<std::string,std::string>& names,const std::map<std::string,zima::document::SectionComponent>& settings,
                        zima::document::HatchStyle defaults={}){
        setRowCount(0);settings_.clear();defaults_=defaults;
        for(const auto& [id,name]:names){
            const int row=rowCount();insertRow(row);auto* item=new QTableWidgetItem(QString::fromStdString(name));
            item->setData(Qt::UserRole,QString::fromStdString(id));item->setFlags(item->flags()&~Qt::ItemIsEditable);setItem(row,0,item);
            const auto found=settings.find(id);const auto c=found==settings.end()?zima::document::SectionComponent{}:found->second;
            settings_[id]=c;
            auto* mode=new QComboBox(this);mode->addItems({tr("Řezat + šrafovat"),tr("Řezat bez šraf"),tr("Neřezat")});mode->setCurrentIndex(c.mode);setCellWidget(row,1,mode);
            auto* angle=new QDoubleSpinBox(this);angle->setRange(-360,360);setCellWidget(row,2,angle);
            auto* spacing=new QDoubleSpinBox(this);spacing->setRange(.1,100);spacing->setDecimals(2);setCellWidget(row,3,spacing);
            auto* offset=new QDoubleSpinBox(this);offset->setRange(-100,100);offset->setDecimals(2);setCellWidget(row,4,offset);
            auto* pattern=new QComboBox(this);pattern->addItems({tr("Rovnoběžné"),tr("Křížové"),tr("Čárkované")});setCellWidget(row,5,pattern);
            auto* reverse=new QPushButton(tr("Obrátit"),this);reverse->setObjectName("reverseComponentHatch");reverse->setAutoDefault(false);
            reverse->setToolTip(tr("Otočit šrafování o 90°"));setCellWidget(row,6,reverse);
            display_style(row,c.custom_hatch?c.hatch:inherited_style(row));
            connect(mode,&QComboBox::currentIndexChanged,this,[this,row](int value){propagate(row,1,value);notify();});
            connect(angle,&QDoubleSpinBox::valueChanged,this,[this,row](double value){propagate(row,2,value);notify();});
            connect(spacing,&QDoubleSpinBox::valueChanged,this,[this,row](double value){propagate(row,3,value);notify();});
            connect(offset,&QDoubleSpinBox::valueChanged,this,[this,row](double value){propagate(row,4,value);notify();});
            connect(pattern,&QComboBox::currentIndexChanged,this,[this,row](int value){propagate(row,5,value);notify();});
            connect(reverse,&QPushButton::clicked,this,[this,row]{
                for(int target=0;target<rowCount();++target)if(target==row||(this->item(row,0)->isSelected()&&this->item(target,0)->isSelected())){
                    auto* angle=static_cast<QDoubleSpinBox*>(cellWidget(target,2));QSignalBlocker block(angle);
                    angle->setValue(std::remainder(angle->value()+90,180));mark_custom(target);
                }notify();
            });
        }
        resizeRowsToContents();fit_height();setVisible(!names.empty());
    }
    std::map<std::string,zima::document::SectionComponent> values()const{
        auto result=settings_;
        for(int row=0;row<rowCount();++row){auto& c=result.at(key(row));c.mode=static_cast<QComboBox*>(cellWidget(row,1))->currentIndex();
            if(c.custom_hatch){c.hatch.angle=static_cast<QDoubleSpinBox*>(cellWidget(row,2))->value();c.hatch.spacing_mm=static_cast<QDoubleSpinBox*>(cellWidget(row,3))->value();c.hatch.offset_mm=static_cast<QDoubleSpinBox*>(cellWidget(row,4))->value();c.hatch.pattern=static_cast<QComboBox*>(cellWidget(row,5))->currentIndex();}}
        return result;
    }
    void select_component(const std::string& id){for(int row=0;row<rowCount();++row)if(key(row)==id){selectRow(row);scrollToItem(item(row,0));return;}}
    std::function<void()> changed;
protected:
    void resizeEvent(QResizeEvent* event)override{QTableWidget::resizeEvent(event);fit_height();}
private:
    std::map<std::string,zima::document::SectionComponent> settings_;
    zima::document::HatchStyle defaults_;
    std::string key(int row)const{return item(row,0)->data(Qt::UserRole).toString().toStdString();}
    zima::document::HatchStyle inherited_style(int row)const{auto style=defaults_;style.angle=std::remainder(style.angle+(row%2)*90,360);return style;}
    void display_style(int row,const zima::document::HatchStyle& style){
        QSignalBlocker a(cellWidget(row,2)),b(cellWidget(row,3)),c(cellWidget(row,4)),d(cellWidget(row,5));
        static_cast<QDoubleSpinBox*>(cellWidget(row,2))->setValue(style.angle);
        static_cast<QDoubleSpinBox*>(cellWidget(row,3))->setValue(style.spacing_mm);
        static_cast<QDoubleSpinBox*>(cellWidget(row,4))->setValue(style.offset_mm);
        static_cast<QComboBox*>(cellWidget(row,5))->setCurrentIndex(style.pattern);
    }
    void fit_height(){const int height=std::min(220,horizontalHeader()->height()+verticalHeader()->length()+2*frameWidth()+(horizontalHeader()->length()>viewport()->width()?style()->pixelMetric(QStyle::PM_ScrollBarExtent):0));if(this->height()!=height||minimumHeight()!=height)setFixedHeight(height);}
    void notify(){if(changed)changed();}
    void mark_custom(int row){auto& c=settings_.at(key(row));if(!c.custom_hatch)c.hatch=inherited_style(row);c.custom_hatch=true;}
    void propagate(int source,int col,double value){
        if(col!=1)mark_custom(source);
        if(!item(source,0)->isSelected())return;
        for(int row=0;row<rowCount();++row)if(row!=source&&item(row,0)->isSelected()){
            auto* w=cellWidget(row,col);QSignalBlocker block(w);
            if(auto* combo=qobject_cast<QComboBox*>(w))combo->setCurrentIndex(static_cast<int>(value));
            else if(auto* spin=qobject_cast<QDoubleSpinBox*>(w))spin->setValue(value);
            if(col!=1)mark_custom(row);
        }
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
        row->addWidget(new QLabel(tr("Skicová rovina kontejneru"),this));row->addWidget(plane_);auto* sketch=new QPushButton(tr("Skica…"),this);sketch->setObjectName("editSectionSketch");style_sketch_button(sketch);row->addWidget(sketch);content_layout()->addLayout(row);
        connect(plane_,&QComboBox::currentIndexChanged,this,[this](int i){value_.sketch.plane=static_cast<zima::sketcher::SketchPlane>(i);if(changed)changed();});
        connect(sketch,&QPushButton::clicked,this,[this]{if(edit_sketch)edit_sketch(0);});
        sketch_info_=new QLabel(this);content_layout()->addWidget(sketch_info_);update_sketch_info();
        reverse_=new QCheckBox(tr("Obrátit stranu řezu"),this);reverse_->setObjectName("sectionReverse");reverse_->setChecked(value_.reversed);content_layout()->addWidget(reverse_);
        show_=new QCheckBox(tr("Zobrazit rovinu řezu"),this);show_->setObjectName("sectionShowPlane");show_->setChecked(value_.show_plane);content_layout()->addWidget(show_);
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
