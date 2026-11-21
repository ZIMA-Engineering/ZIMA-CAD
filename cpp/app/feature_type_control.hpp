#pragma once
#include <zima/document/feature_parameters.hpp>
#include <QComboBox>
#include "resource_icon.hpp"

namespace zima::app {
// One type control for standalone Features and geometry owned by another command.
// The owner fixes embedded geometry; only a standalone Feature may change type.
inline QComboBox* feature_type_control(QWidget* parent,document::FeatureType type,
        bool editable=true) {
    auto* control=new QComboBox(parent);control->setObjectName("featureType");
    control->addItem(resource_icon("point"),QObject::tr("Bod"));
    control->addItem(resource_icon("axis"),QObject::tr("Osa"));
    control->addItem(resource_icon("plane"),QObject::tr("Rovina"));
    control->addItem(resource_icon("sketch"),QObject::tr("Skica"));
    control->addItem(resource_icon("protrusion"),QObject::tr("Tažení"));
    control->addItem(resource_icon("projection"),QObject::tr("Promítnutí"));
    control->setCurrentIndex(static_cast<int>(type));control->setEnabled(editable);
    return control;
}
} // namespace zima::app
