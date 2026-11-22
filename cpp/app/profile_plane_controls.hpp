#pragma once
#include "work_plane_selection.hpp"
#include "sketch_button_style.hpp"
#include "resource_icon.hpp"
#include <zima/ui/unit_spin_box.hpp>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QEvent>

namespace zima::app {
// The ordinary Feature and FEM own the same Plane/Sketch presentation.
// Their hosts supply persistence and the existing placement-reference route.
class ProfilePlaneControls final : public QWidget {
public:
    QGroupBox *plane_group{},*sketch_group{};
    QFormLayout* rows{};
    QLabel* plane_label{};
    QComboBox* plane{};
    QWidget* selector{};
    ui::UnitDoubleSpinBox* offset{};
    QPushButton* sketch{};
    ProfilePlaneControls(QWidget* parent,const QString& prefix,std::function<void()> request)
        :QWidget(parent) {
        auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);
        plane_group=new QGroupBox(this);plane_group->setObjectName(prefix+"PlaneGroup");
        sketch_group=new QGroupBox(this);sketch_group->setObjectName(prefix+"SketchGroup");
        rows=new QFormLayout(plane_group);
        plane=new QComboBox(plane_group);plane->setObjectName(prefix+"ProfilePlane");
        plane->addItem("XY",0);plane->addItem("XZ",1);plane->addItem("YZ",2);
        install_automatic_work_plane(plane,true);
        plane_label=new QLabel(plane_group);
        selector=compact_work_plane_selector(plane,std::move(request));
        rows->addRow(plane_label,selector);
        offset=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,plane_group);
        offset->setObjectName(prefix+"ProfileOffset");offset->setRange(-1000000,1000000);
        rows->addRow(tr("Odsazení roviny"),offset);
        sketch=new QPushButton(sketch_group);sketch->setObjectName(prefix+"SketchButton");
        style_sketch_button(sketch);sketch->setMinimumHeight(40);sketch->setMinimumWidth(100);
        sketch->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
        sketch->setIcon(resource_icon("sketch"));sketch->setIconSize({20,20});
        (new QHBoxLayout(sketch_group))->addWidget(sketch);
        row->addWidget(plane_group,1);row->addWidget(sketch_group);
        retranslate();
    }
    void retranslate() {
        plane_group->setTitle(tr("Rovina"));sketch_group->setTitle(tr("Skica"));
        plane_label->setText(tr("Výchozí rovina"));sketch->setText(tr("Skica"));
        if(auto* label=qobject_cast<QLabel*>(rows->labelForField(offset)))label->setText(tr("Odsazení roviny"));
    }
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&plane_group)retranslate();
    }
};
}
