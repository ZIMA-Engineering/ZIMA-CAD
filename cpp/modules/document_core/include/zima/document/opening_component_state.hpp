#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
// Changes only the supplied draft. Retain original bore/profile identities and
// dimensions; removing a thread never enlarges the underlying cylindrical bore.
[[nodiscard]] inline bool disable_opening_component(HistoryContainer& value,const std::string& role) {
    if(value.feature_kind==FeatureKind::Hole) {
        if(role!="thread"||(!value.hole.thread_enabled&&value.hole.type==HoleType::Plain))return false;
        value.hole.thread_enabled=false;value.hole.type=HoleType::Plain;return true;
    }
    if(value.feature_kind!=FeatureKind::Thread)return false;
    if(role=="thread"&&value.thread.enabled){value.thread.enabled=false;value.thread.nominal_diameter=value.thread.profile_diameter;}
    else if(role=="chamfer"&&value.thread.chamfer_enabled)value.thread.chamfer_enabled=false;
    else if(role=="tip"&&value.hole.drill_point_enabled)value.hole.drill_point_enabled=false;
    else return false;
    return true;
}
}
