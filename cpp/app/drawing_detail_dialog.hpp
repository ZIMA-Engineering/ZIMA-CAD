#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/drawing/detail_view.hpp>
#include "drawing_view_description_fields.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
#include <tuple>
namespace zima::app {
class DrawingDetailDialog final:public ui::PropertiesSubWindow {
public:
    std::function<void()> choose_source, edit_boundary, place;
    std::function<void(drawing::DrawingView)> preview;
    std::function<void(drawing::DrawingView)> commit;
    explicit DrawingDetailDialog(drawing::DrawingView view,QWidget* parent)
        :PropertiesSubWindow(tr("Detail pohledu"),parent),value_(std::move(view)),placed_(!value_.parent_view_id.empty()) {
        setObjectName("drawingDetailProperties");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        source_=new QPushButton(tr("Vybrat bod v pohledu"),this);source_->setObjectName("detailSource");form->addRow(tr("Zdrojový pohled"),source_);
        name_=new QLineEdit(QString::fromStdString(value_.name),this);name_->setObjectName("detailName");form->addRow(tr("Název"),name_);
        shape_=new QComboBox(this);shape_->setObjectName("detailShape");shape_->addItems({tr("Kruh"),tr("Elipsa"),tr("Uzavřená spline")});
        shape_->setCurrentIndex(value_.crop?int(value_.crop->shape):0);form->addRow(tr("Hranice"),shape_);
        auto* boundary=new QPushButton(tr("Upravit hranici"),this);boundary->setObjectName("detailBoundary");form->addRow(boundary);
        auto* position=new QPushButton(tr("Umístit detail"),this);position->setObjectName("detailPosition");form->addRow(position);
        scale_=new QDoubleSpinBox(this);scale_->setObjectName("detailScale");scale_->setRange(.001,1000);scale_->setDecimals(3);scale_->setValue(value_.scale);form->addRow(tr("Měřítko"),scale_);
        presented_scale_=scale_->value();
        caption_=new QCheckBox(tr("Zobrazit popis pohledu"),this);caption_->setChecked(value_.show_caption);form->addRow(caption_);
        descriptions_=new ViewDescriptionFields(value_.description_rows,this);form->addRow(descriptions_);descriptions_->changed=[this]{changed();};
        descriptions_->set_computed_text(name_->text(),scale_->value());
        boundary_=new QCheckBox(tr("Zobrazit hranici ve zdrojovém pohledu"),this);boundary_->setObjectName("detailShowBoundary");boundary_->setChecked(value_.show_detail_boundary);form->addRow(boundary_);
        label_=new QCheckBox(tr("Zobrazit označení ve zdrojovém pohledu"),this);label_->setObjectName("detailShowLabel");label_->setChecked(value_.show_detail_label);form->addRow(label_);
        connect(source_,&QPushButton::clicked,this,[this]{if(choose_source)choose_source();});
        connect(boundary,&QPushButton::clicked,this,[this]{if(edit_boundary)edit_boundary();});
        connect(position,&QPushButton::clicked,this,[this]{if(place)place();});
        connect(name_,&QLineEdit::textChanged,this,[this]{changed();});
        connect(scale_,&QDoubleSpinBox::valueChanged,this,[this]{changed();});
        for(auto* check:{caption_,boundary_,label_})connect(check,&QCheckBox::toggled,this,[this]{changed();});
        set_initial_size({800,650});update_ready();if(ready())initial_=definition(values());
    }
    drawing::DrawingView values()const {
        auto value=value_;value.name=name_->text().trimmed().toStdString();
        value.show_caption=caption_->isChecked();value.show_detail_boundary=boundary_->isChecked();value.show_detail_label=label_->isChecked();
        value.description_rows=descriptions_->values();
        value.use_sheet_scale=false;value.detail_view=true;return value;
    }
    int shape()const{return shape_->currentIndex();}
    void set_source(const drawing::DrawingView& parent) {value_.parent_view_id=parent.id;source_->setText(QString::fromStdString(parent.name));placed_=false;update_ready();}
    void set_boundary(drawing::ViewCrop crop) {value_.crop=std::move(crop);update_ready();}
    void set_placed(drawing::DrawingView view) {value_=std::move(view);placed_=true;changed();}
    void move(drawing::Point2 position) {value_.x=position.x;value_.y=position.y;changed();}
    void resume(){changed();}
protected:
    bool submit()override {if(!ready())return false;const auto value=values();if(initial_&&*initial_==definition(value))return true;if(commit)commit(value);return true;}
private:
    using Definition=std::tuple<std::string,std::string,double,double,double,bool,bool,bool,std::optional<drawing::ViewCrop>,std::vector<drawing::ViewDescriptionRow>>;
    static Definition definition(const drawing::DrawingView& v){return {v.name,v.parent_view_id,v.x,v.y,v.scale,v.show_caption,v.show_detail_boundary,v.show_detail_label,v.crop,v.description_rows};}
    std::optional<Definition> initial_;ViewDescriptionFields* descriptions_{};
    drawing::DrawingView value_;bool placed_{};double presented_scale_{};
    QPushButton* source_{};QComboBox* shape_{};QLineEdit* name_{};QDoubleSpinBox* scale_{};
    QCheckBox *caption_{},*boundary_{},*label_{};
    bool ready()const{return placed_&&value_.crop&&!value_.parent_view_id.empty()&&!name_->text().trimmed().isEmpty();}
    void update_ready(){buttons()->button(QDialogButtonBox::Ok)->setEnabled(ready());}
    void changed(){
        if(scale_->value()!=presented_scale_) {
            if(value_.crop){const auto delta=scale_->value()-value_.scale;value_.x+=value_.crop->anchor.x*delta;value_.y-=value_.crop->anchor.y*delta;}
            value_.scale=scale_->value();presented_scale_=scale_->value();
        }
        descriptions_->set_computed_text(name_->text(),value_.scale);update_ready();if(ready()&&preview)preview(values());
    }
};
}
