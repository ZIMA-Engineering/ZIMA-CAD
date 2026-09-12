#pragma once
#include <zima/commands/dispatcher.hpp>
#include <zima/document/part_document.hpp>
#include <functional>
#include <stdexcept>

namespace zima::command_host {
struct ConstructionParameterError : std::runtime_error {
    ConstructionParameterError(const char* code, const char* message) : std::runtime_error(message), code(code) {}
    const char* code;
};
const char* construction_tangent_name(document::Curve3DTangentMode);
// Mutate only a caller-owned draft. Geometry is in that object's editing frame.
void apply_construction_properties(document::ConstructionObject&, const commands::Json&,
    const kernel::ViewerReferenceGeometry&, const document::ConstructionObject* parent = nullptr);
using CurvePointGeometry = std::function<kernel::ViewerReferenceGeometry(document::ConstructionObject&, std::size_t)>;
// Complete ordered point list; retain native identities for entries with an ID.
// The callback supplies references in the proposed parent frame, only if needed.
void apply_curve_points(document::ConstructionObject&, const commands::Json&, const CurvePointGeometry&);
} // namespace zima::command_host
