#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <functional>
namespace zima::app {
class DrawingDimensionGuideFields final:public QGroupBox {
public:
    std::function<void()> changed;
    explicit DrawingDimensionGuideFields(const drawing::DrawingView& view,QWidget* parent):QGroupBox(tr("Vodítka kót"),parent) {
        auto* grid=new QGridLayout(this);grid->setSpacing(6);
        count_=new QSpinBox(this);count_->setObjectName("drawingGuideCount");count_->setRange(0,100);count_->setValue(view.dimension_guide_count);
        offset_=new QDoubleSpinBox(this);spacing_=new QDoubleSpinBox(this);
        offset_->setObjectName("drawingGuideOffset");spacing_->setObjectName("drawingGuideSpacing");
        offset_->setDecimals(3);offset_->setRange(.001,1000);offset_->setValue(view.dimension_guide_offset);
        spacing_->setDecimals(3);spacing_->setRange(.1,1000);spacing_->setValue(view.dimension_guide_spacing);
        shown_=new QCheckBox(tr("Zobrazovat vodítka i mimo přesouvání"),this);shown_->setObjectName("drawingDimensionGuides");shown_->setChecked(view.show_dimension_guides);
        const auto field=[&](int column,const QString& label,QWidget* widget){grid->addWidget(new QLabel(label,this),0,column);grid->addWidget(widget,1,column);};
        field(0,tr("Počet"),count_);field(1,tr("Odsazení [mm]"),offset_);field(2,tr("Rozteč [mm]"),spacing_);grid->addWidget(shown_,2,0,1,3);
        connect(count_,&QSpinBox::valueChanged,this,[this]{if(changed)changed();});
        connect(shown_,&QCheckBox::toggled,this,[this]{if(changed)changed();});
        for(auto* field:{offset_,spacing_})connect(field,&QDoubleSpinBox::valueChanged,this,[this]{if(changed)changed();});
    }
    void apply(drawing::DrawingView& view)const {
        view.dimension_guide_count=count_->value();view.show_dimension_guides=shown_->isChecked();
        view.dimension_guide_offset=offset_->value();view.dimension_guide_spacing=spacing_->value();
    }
private:
    QSpinBox* count_{};QDoubleSpinBox *offset_{},*spacing_{};QCheckBox* shown_{};
};
}
