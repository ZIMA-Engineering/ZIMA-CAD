#pragma once
#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {
[[nodiscard]] BodyGraphEdit prepare_render_edit(const document::PartDocument&,const std::string& body_id={});
[[nodiscard]] std::vector<std::string> render_source_candidates(const document::DocumentSession&,const BodyGraphEdit&);
[[nodiscard]] std::vector<std::pair<std::string,kernel::BodySnapshot>> read_render_sources(const document::DocumentSession&,const std::string& body_id);
[[nodiscard]] kernel::ViewerReferenceGeometry render_placement_geometry(const document::DocumentSession&,const std::string& body_id);
[[nodiscard]] const document::RenderObject* find_render_object(const document::PartDocument&,
    const std::string& body_id,const std::string& object_id);
// Metadata-only transactions. Unchanged OK does not calculate or add Undo.
[[nodiscard]] bool commit_render(Workspace&,const BodyGraphEdit&,document::BodyHistory);
[[nodiscard]] bool commit_render_object(Workspace&,const BodyGraphEdit&,document::RenderObject);
[[nodiscard]] bool erase_render_object(Workspace&,const BodyGraphEdit&,const std::string& object_id);
[[nodiscard]] bool commit_render_images(Workspace&,const std::string& document_id,
    const std::string& body_id,std::vector<document::RenderImage>);
} // namespace zima::workspace
