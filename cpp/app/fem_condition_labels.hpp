#pragma once
#include <zima/document/fem_condition.hpp>
#include <QObject>
namespace zima::app {
inline QString fem_condition_name(document::FemConditionKind kind) {
    return kind==document::FemConditionKind::MeasurementDistance?QObject::tr("Distance measurement"):
        kind==document::FemConditionKind::MeasurementPoint?QObject::tr("Measurement point"):
        kind==document::FemConditionKind::HeatSource?QObject::tr("Heat source"):
        kind==document::FemConditionKind::Force?QObject::tr("Force"):
        kind==document::FemConditionKind::Pressure?QObject::tr("Pressure"):QObject::tr("Constraint");
}
inline const char* fem_condition_icon(document::FemConditionKind kind) {
    return kind==document::FemConditionKind::MeasurementDistance?"measure":kind==document::FemConditionKind::MeasurementPoint?"point":
        kind==document::FemConditionKind::HeatSource?"fem-heat":
        kind==document::FemConditionKind::Force?"fem-force":kind==document::FemConditionKind::Pressure?"fem-pressure":"fem-constraint";
}

}
