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
// Feature, FEM and Holes own the same Plane/Sketch presentation.
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
        make_groups(prefix);
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
        finish_sketch();
    }
    // Reparent the host's existing fields and connections, including the
    // offset lock, without changing its placement or Sketch lifecycle.
    ProfilePlaneControls(QWidget* parent,const QString& prefix,QFormLayout* existing_rows,
        QComboBox* existing_plane,QWidget* existing_selector,ui::UnitDoubleSpinBox* existing_offset,
        QPushButton* existing_sketch,const char* sketch_key="Skica"):QWidget(parent),rows(existing_rows),plane(existing_plane),
        selector(existing_selector),offset(existing_offset),sketch(existing_sketch),sketch_key_(sketch_key) {
        make_groups(prefix);plane_group->setLayout(rows);
        plane_label=qobject_cast<QLabel*>(rows->labelForField(selector));
        finish_sketch();
    }
private:
    const char* sketch_key_{"Skica"};
    void make_groups(const QString& prefix) {
        auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);
        plane_group=new QGroupBox(this);plane_group->setObjectName(prefix+"PlaneGroup");
        sketch_group=new QGroupBox(this);sketch_group->setObjectName(prefix+"SketchGroup");
        row->addWidget(plane_group,1);row->addWidget(sketch_group);
    }
    void finish_sketch() {
        style_sketch_button(sketch);sketch->setMinimumHeight(40);sketch->setMinimumWidth(100);
        sketch->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
        sketch->setIcon(resource_icon("sketch"));sketch->setIconSize({20,20});
        (new QHBoxLayout(sketch_group))->addWidget(sketch);
        retranslate();
    }
public:
    void retranslate() {
        plane_group->setTitle(tr("Rovina"));sketch_group->setTitle(tr("Skica"));
        plane_label->setText(tr("Výchozí rovina"));sketch->setText(tr(sketch_key_));
        if(auto* label=qobject_cast<QLabel*>(rows->labelForField(offset)))label->setText(tr("Odsazení roviny"));
    }
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&plane_group)retranslate();
    }
};
}
