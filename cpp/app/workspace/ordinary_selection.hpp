#pragma once
#include <zima/viewer/picking.hpp>

namespace zima::app::workspace_detail {
inline std::vector<viewer::CandidateKind> ordinary_selection_kinds(
    viewer::SelectionFilter filter, bool part) {
    using Kind = viewer::CandidateKind;
    using Filter = viewer::SelectionFilter;
    switch (filter) {
        case Filter::Faces: return {Kind::Face};
        case Filter::Planes: return {Kind::Plane};
        case Filter::Curves: return {Kind::Edge};
        case Filter::Origins:
        case Filter::Points: return {Kind::Vertex};
        case Filter::Axes: return {Kind::Axis};
        default: return {Kind::Dimension, Kind::Symbol,
            part ? Kind::Container : Kind::Occurrence};
    }
}
}
