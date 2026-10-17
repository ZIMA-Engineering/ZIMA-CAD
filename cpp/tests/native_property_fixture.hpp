#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>

namespace zima::test {
// Geometry assertions must explicitly complete deferred scalar analysis. This
// analyzes only the test's final loaded snapshot, never a live document or its
// reference identity. Tests of lazy loading itself use PartDocument::load.
inline document::PartDocument load_part_with_properties(const std::filesystem::path& path,
        std::vector<kernel::BodyResult>* calculated=nullptr) {
    auto part=document::PartDocument::load(path,calculated);
    if(calculated&&!calculated->empty()&&!calculated->back().physical_properties_current) {
        kernel::OcctKernel analysis;
        analysis.ensure_body_properties(calculated->back());
    }
    return part;
}
}
