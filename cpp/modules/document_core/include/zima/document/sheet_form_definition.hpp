#pragma once
#include <zima/document/part_document.hpp>
#include <array>

namespace zima::document {
// The four roles retain the identities of ordinary native Body containers.
// Names are consulted only on insertion/replacement, never on regeneration.
enum class SheetFormRole : unsigned { Cut, Shape, Flat, Symbol };
struct SheetFormDefinition {
    PartDocument part;
    std::array<std::string,4> bodies;
    std::string cut_sketch, flat_sketch, symbol_sketch;
    kernel::FaceReference surface;
    std::shared_ptr<const std::vector<kernel::BodyResult>> calculated;
};
[[nodiscard]] SheetFormDefinition read_sheet_form_definition(const std::filesystem::path&);
[[nodiscard]] SheetFormDefinition sheet_form_definition(
    PartDocument, const std::vector<kernel::BodyResult>&);
[[nodiscard]] SheetFormDefinition stored_sheet_form_definition(const SheetFormParameters&);
[[nodiscard]] std::vector<kernel::ViewerEdge> sheet_form_preview_edges(const SheetFormParameters&);
// FORM command policy composed from ordinary native placement references.
// No new general placement equations or persistence fields are introduced.
[[nodiscard]] Placement sheet_form_attachment(const kernel::FaceReference&,
    const std::string& body_origin, const kernel::ViewerReferenceGeometry&,
    Placement seed, kernel::Vec3 insertion_point);
// Feature-owned planar positioning. Row zero is the zero-offset support;
// rows one/two are signed line/plane distances or sheet-frame point X/Z.
// Source identities and offsets remain ordinary persisted native references.
[[nodiscard]] bool resolve_sheet_form_placement(Placement&,
    const kernel::ViewerReferenceGeometry&, kernel::Vec3* base_rotation=nullptr,
    bool* orientation_from_reference=nullptr);
[[nodiscard]] bool sheet_form_position_reference_available(ConstructionReference,
    const kernel::ViewerReferenceGeometry&,kernel::Vec3 sheet_normal);
[[nodiscard]] SheetFormParameters copy_sheet_form_definition(const SheetFormDefinition&,
    const std::vector<kernel::BodyResult>& calculated={});
[[nodiscard]] HistoryContainer create_sheet_form();
void validate_sheet_form_parameters(const SheetFormParameters&);
[[nodiscard]] kernel::HistoryOperation sheet_form_operation(const PartDocument&,const HistoryContainer&);
[[nodiscard]] kernel::SheetFormRequest sheet_form_request(const SheetFormDefinition&,
    kernel::FaceReference support,kernel::Vec3 position,kernel::Vec3 normal,
    kernel::Vec3 x_direction,double thickness);
} // namespace zima::document
