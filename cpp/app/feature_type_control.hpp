#pragma once
#include <zima/document/feature_parameters.hpp>
#include <QComboBox>

namespace zima::app {
// One type control for standalone Features and geometry owned by another command.
// The owner fixes embedded geometry; only a standalone Feature may change type.
inline QComboBox* feature_type_control(QWidget* parent,document::FeatureType type,
        bool editable=true) {
    auto* control=new QComboBox(parent);control->setObjectName("featureType");
    control->addItems({QObject::tr("Bod"),QObject::tr("Osa"),QObject::tr("Rovina"),
        QObject::tr("Skica"),QObject::tr("Vytažení / Rotace")});
    control->setCurrentIndex(static_cast<int>(type));control->setEnabled(editable);
    return control;
}
} // namespace zima::app
