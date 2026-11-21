#pragma once
#include <zima/workspace/fem_operations.hpp>
#include <zima/document/fem_condition.hpp>

namespace zima::workspace {
// A Boolean retains its target's frame. Analysis uses that existing material
// Origin for placement; it does not fabricate another editable CAD Body.
std::string fem_source_frame_owner(const document::BodyHistoryGraph&,const std::string& fem_body);
[[nodiscard]] const document::FemCondition* find_fem_condition(const document::PartDocument&,const std::string& body,const std::string& condition);
[[nodiscard]] kernel::ViewerReferenceGeometry fem_placement_geometry(const document::DocumentSession&,const std::string& body);
[[nodiscard]] bool commit_fem_condition(Workspace&,const BodyGraphEdit&,document::FemCondition);
[[nodiscard]] bool erase_fem_condition(Workspace&,const BodyGraphEdit&,const std::string& condition);
} // namespace zima::workspace
