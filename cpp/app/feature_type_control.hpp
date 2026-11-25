#pragma once
#include <zima/document/feature_parameters.hpp>
#include <QComboBox>
#include <QAbstractItemView>
#include <QEvent>
#include <QScreen>
#include "resource_icon.hpp"

namespace zima::app {
inline constexpr int projection_relief_type_index=6;
inline constexpr int wrap_relief_type_index=8;
inline constexpr int planar_surface_type_index=9;
inline int feature_type_index(document::FeatureType type) {
    return type==document::FeatureType::Wrapping?7:static_cast<int>(type);
}
inline document::FeatureType selected_feature_type(const QComboBox* control) {
    return static_cast<document::FeatureType>(control->currentData().toInt());
}
class FeatureTypeControl final:public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup()override {
        QComboBox::showPopup();auto* popup=view()->window();
        QWidget* host=this;while(host->parentWidget())host=host->parentWidget();
        auto bounds=QRect(host->mapToGlobal(QPoint{}),host->size());
        if(screen())bounds=bounds.intersected(screen()->availableGeometry());
        const auto below=mapToGlobal(QPoint(0,height()));
        const int available=std::max(1,bounds.bottom()-below.y()+1);
        popup->resize(popup->width(),std::min(popup->height(),available));
        popup->move(std::clamp(below.x(),bounds.left(),std::max(bounds.left(),bounds.right()-popup->width()+1)),below.y());
        view()->scrollTo(model()->index(currentIndex(),0),QAbstractItemView::EnsureVisible);
    }
protected:
    void changeEvent(QEvent* event)override {
        QComboBox::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&count()==10) {
            const char* keys[]={"Bod","Osa","Rovina","Skica","Tažení","Project sketch","Project relief","Wrap sketch","Wrap relief","Plocha"};
            for(int i=0;i<10;++i)setItemText(i,QObject::tr(keys[i]));
        }
    }
};
// One type control for standalone Features and geometry owned by another command.
// The owner fixes embedded geometry; only a standalone Feature may change type.
inline QComboBox* feature_type_control(QWidget* parent,document::FeatureType type,
        bool editable=true) {
    auto* control=new FeatureTypeControl(parent);control->setObjectName("featureType");
    const auto add=[&](const char* icon,const QString& label,document::FeatureType value) {
        control->addItem(resource_foreground_icon(icon),label,static_cast<int>(value));
    };
    add("point",QObject::tr("Bod"),document::FeatureType::Point);
    add("axis",QObject::tr("Osa"),document::FeatureType::Axis);
    add("plane",QObject::tr("Rovina"),document::FeatureType::Plane);
    add("sketch",QObject::tr("Skica"),document::FeatureType::Sketch);
    add("protrusion",QObject::tr("Tažení"),document::FeatureType::Modeling);
    add("projection",QObject::tr("Project sketch"),document::FeatureType::Projection);
    add("projection",QObject::tr("Project relief"),document::FeatureType::Projection);
    add("wrap",QObject::tr("Wrap sketch"),document::FeatureType::Wrapping);
    add("wrap",QObject::tr("Wrap relief"),document::FeatureType::Wrapping);
    add("boundary-surface",QObject::tr("Plocha"),document::FeatureType::Modeling);
    control->setCurrentIndex(feature_type_index(type));control->setEnabled(editable);
    return control;
}
} // namespace zima::app
