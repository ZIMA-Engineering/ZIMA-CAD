#pragma once

#include <zima/sketcher/sketch.hpp>

namespace zima::test {

inline sketcher::Sketch broken_reference_fixture() {
    auto sketch = sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_segment(-20, -25, 20, -25));
    unsigned index{};
    for (const auto kind : {sketcher::ExternalReferenceKind::Edge,
            sketcher::ExternalReferenceKind::Axis,
            sketcher::ExternalReferenceKind::Face,
            sketcher::ExternalReferenceKind::Point,
            sketcher::ExternalReferenceKind::AxisPoint}) {
        auto reference = sketcher::Sketch::create_external_reference(kind);
        reference.source_document_id = "removed-document";
        reference.source_owner_id = "removed-owner";
        reference.source_semantic_key = "source-" + std::to_string(index);
        const auto y = -15.0 + 10.0 * index++;
        reference.cached_points = sketcher::is_external_point_kind(kind)
            ? std::vector<std::array<double, 2>>{{12, y}}
            : std::vector<std::array<double, 2>>{{-20, y}, {20, y}};
        reference.infinite = kind == sketcher::ExternalReferenceKind::Axis ||
            kind == sketcher::ExternalReferenceKind::Face;
        reference.broken = true;
        sketch.add_external_reference(std::move(reference));
    }
    return sketch;
}

} // namespace zima::test
