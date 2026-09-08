#pragma once
#include <zima/ui/properties_subwindow.hpp>
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
class SectionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    SectionPropertiesDialog(QWidget* parent,zima::document::SectionDefinition initial,
        std::function<bool(zima::document::SectionDefinition)> commit,std::function<void()> redraw,
        std::function<void()> pick,std::function<void()> preview)
        :PropertiesSubWindow(tr("Řez"),parent),value_(std::move(initial)),commit_(std::move(commit)),preview_(std::move(preview)){
        setObjectName("sectionProperties");auto* body=new QWidget(this);auto* layout=new QVBoxLayout(body);auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setWidget(body);scroll->setMinimumHeight(400);content_layout()->addWidget(scroll);auto* form=new QFormLayout;
        name_=new QLineEdit(QString::fromStdString(value_.name),this);name_->setObjectName("sectionName");form->addRow(tr("Název"),name_);
        auto* draw=new QPushButton(tr("Nakreslit čáru ve View"),this);draw->setObjectName("drawSectionLine");form->addRow(draw);connect(draw,&QPushButton::clicked,this,std::move(redraw));
        const auto& line=value_.sketch.segments.front();const auto* a=value_.sketch.find_point(line.first_point_id);const auto* b=value_.sketch.find_point(line.second_point_id);const std::array coordinates{a->x,a->y,b->x,b->y};
        const std::array labels{tr("Začátek X"),tr("Začátek Y"),tr("Konec X"),tr("Konec Y")};
        for(int i=0;i<4;++i){auto* field=new QDoubleSpinBox(this);field->setObjectName(QString("sectionCoordinate%1").arg(i));field->setRange(-1e9,1e9);field->setDecimals(4);field->setValue(coordinates[i]);line_[i]=field;form->addRow(labels[i],field);connect(field,&QDoubleSpinBox::valueChanged,this,[this]{changed();});}
        length_=new QDoubleSpinBox(this);length_->setObjectName("sectionLength");length_->setDecimals(4);length_->setRange(.0001,1e9);
        angle_=new QDoubleSpinBox(this);angle_->setObjectName("sectionAngle");angle_->setRange(-360,360);angle_->setDecimals(3);
        form->addRow(tr("Délka čáry"),length_);form->addRow(tr("Úhel čáry"),angle_);
        const auto polar=[this]{const double t=angle_->value()*std::numbers::pi/180;set_line(line_[0]->value(),line_[1]->value(),line_[0]->value()+length_->value()*std::cos(t),line_[1]->value()+length_->value()*std::sin(t));};
        connect(length_,&QDoubleSpinBox::valueChanged,this,polar);connect(angle_,&QDoubleSpinBox::valueChanged,this,polar);
        reverse_=new QCheckBox(tr("Obrátit směr řezu"),this);reverse_->setObjectName("sectionReverse");reverse_->setChecked(value_.reversed);form->addRow(reverse_);
        plane_=new QCheckBox(tr("Zobrazit rovinu"),this);plane_->setObjectName("sectionShowPlane");plane_->setChecked(value_.show_plane);form->addRow(plane_);
        cut_=new QCheckBox(tr("Zobrazit model v řezu"),this);cut_->setObjectName("sectionShowCut");cut_->setChecked(value_.show_cut);form->addRow(cut_);
        for(auto* box:{reverse_,plane_,cut_})connect(box,&QCheckBox::toggled,this,[this]{changed();});
        layout->addLayout(form);
        auto* choose=new QPushButton(tr("Vybrat díl ve View"),this);choose->setObjectName("pickSectionComponent");layout->addWidget(choose);connect(choose,&QPushButton::clicked,this,std::move(pick));
        components_=new SectionComponentsWidget(this);components_->set_components(value_.component_names,value_.components);components_->changed=[this]{changed();};layout->addWidget(components_);choose->setVisible(!value_.component_names.empty());
        error_=new QLabel(this);error_->setWordWrap(true);layout->addWidget(error_);
        setMinimumWidth(660);setAttribute(Qt::WA_DeleteOnClose);update_polar();
    }
    zima::document::SectionDefinition values()const{
        auto s=value_;s.name=name_->text().trimmed().toStdString();s.reversed=reverse_->isChecked();s.show_plane=plane_->isChecked();s.show_cut=cut_->isChecked();s.components=components_->values();
        const auto& line=s.sketch.segments.front();for(auto& p:s.sketch.points){if(p.id==line.first_point_id){p.x=line_[0]->value();p.y=line_[1]->value();}if(p.id==line.second_point_id){p.x=line_[2]->value();p.y=line_[3]->value();}}
        return s;
    }
    void set_line(double x1,double y1,double x2,double y2){const std::array values{x1,y1,x2,y2};for(int i=0;i<4;++i){QSignalBlocker block(line_[i]);line_[i]->setValue(values[i]);}changed();}
    void select_component(const std::string& id){components_->select_component(id);}
    void set_error(const QString& message){error_->setText(message);}
private:
    zima::document::SectionDefinition value_;std::function<bool(zima::document::SectionDefinition)> commit_;std::function<void()> preview_;
    QLineEdit* name_{};std::array<QDoubleSpinBox*,4> line_{};QDoubleSpinBox *length_{},*angle_{};QCheckBox *reverse_{},*plane_{},*cut_{};SectionComponentsWidget* components_{};QLabel* error_{};
    void update_polar(){if(!length_)return;QSignalBlocker b1(length_),b2(angle_);const double x=line_[2]->value()-line_[0]->value(),y=line_[3]->value()-line_[1]->value();length_->setValue(std::hypot(x,y));angle_->setValue(std::atan2(y,x)*180/std::numbers::pi);}
    void changed(){update_polar();if(preview_)preview_();}
    bool submit() override{try{auto value=values();if(value.name.empty())throw std::runtime_error("Vyplňte název řezu.");static_cast<void>(zima::document::section_frame(value));return commit_(std::move(value));}catch(const std::exception& e){set_error(QString::fromUtf8(e.what()));return false;}}
};
} // namespace zima::app
