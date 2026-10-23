#pragma once
#include <zima/kernel/ordered_up_to_diagnostic.hpp>
#include <QObject>
#include <QString>

namespace zima::app {
inline QString feature_limit_diagnostic(const kernel::OrderedUpToError& error) {
    QString reason;
    switch(error.failure) {
    case kernel::OrderedUpToFailure::InvalidInput:reason=QObject::tr("The target support is unavailable or cannot be extended uniquely.");break;
    case kernel::OrderedUpToFailure::AmbiguousAdjacency:reason=QObject::tr("Adjacent supports do not define one unambiguous intersection.");break;
    case kernel::OrderedUpToFailure::AmbiguousRetainedPatch:reason=QObject::tr("Select the retained side relative to the adjacent support.");break;
    case kernel::OrderedUpToFailure::EmptyPatch:reason=QObject::tr("The selected sides leave no single connected target patch.");break;
    case kernel::OrderedUpToFailure::DisconnectedChain:reason=QObject::tr("The target patches do not form a connected chain.");break;
    case kernel::OrderedUpToFailure::SelfIntersection:reason=QObject::tr("Non-adjacent target patches intersect.");break;
    }
    return error.row==error.other_row?QObject::tr("Up To row %1: %2").arg(error.row+1).arg(reason):
        QObject::tr("Up To rows %1 and %2: %3").arg(error.row+1).arg(error.other_row+1).arg(reason);
}
} // namespace zima::app
