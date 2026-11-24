#pragma once
#include <zima/document/feature_parameters.hpp>
#include <QComboBox>
#include <QAbstractItemView>
#include <QEvent>
#include <QScreen>
#include "resource_icon.hpp"

namespace zima::app {
inline constexpr int wrap_relief_type_index=7;
inline document::FeatureType selected_feature_type(const QComboBox* control) {
    return control->currentIndex()==wrap_relief_type_index?document::FeatureType::Wrapping:
        static_cast<document::FeatureType>(control->currentIndex());
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
        if(event->type()==QEvent::LanguageChange&&count()==8) {
            const char* keys[]={"Bod","Osa","Rovina","Skica","Tažení","Promítnutí","Wrap sketch","Wrap relief"};
            for(int i=0;i<8;++i)setItemText(i,QObject::tr(keys[i]));
        }
    }
};
// One type control for standalone Features and geometry owned by another command.
// The owner fixes embedded geometry; only a standalone Feature may change type.
inline QComboBox* feature_type_control(QWidget* parent,document::FeatureType type,
        bool editable=true) {
    auto* control=new FeatureTypeControl(parent);control->setObjectName("featureType");
    control->addItem(resource_icon("point"),QObject::tr("Bod"));
    control->addItem(resource_icon("axis"),QObject::tr("Osa"));
    control->addItem(resource_icon("plane"),QObject::tr("Rovina"));
    control->addItem(resource_icon("sketch"),QObject::tr("Skica"));
    control->addItem(resource_icon("protrusion"),QObject::tr("Tažení"));
    control->addItem(resource_icon("projection"),QObject::tr("Promítnutí"));
    control->addItem(resource_icon("wrap"),QObject::tr("Wrap sketch"));
    control->addItem(resource_icon("wrap"),QObject::tr("Wrap relief"));
    control->setCurrentIndex(static_cast<int>(type));control->setEnabled(editable);
    return control;
}
} // namespace zima::app
