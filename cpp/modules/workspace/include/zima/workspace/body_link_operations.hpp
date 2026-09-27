#pragma once
#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {
struct BodyLinkSource {
    document::PartDocument document;
    std::vector<kernel::BodyResult> calculated;
    std::filesystem::path path;
};
// Reads native/current source data only. No insertion, activation or OCCT.
BodyLinkSource read_body_link_source(const Workspace&,const std::filesystem::path&,const std::string& document_id = {});
std::vector<std::string> body_link_source_bodies(const BodyLinkSource&);
document::BodyLink body_link_from_source(const BodyLinkSource&,const std::string& source_body,const std::string& owner);
BodyGraphEdit prepare_body_link_edit(const document::PartDocument&,const std::string& id = {});
bool commit_body_link(Workspace&,const kernel::OcctKernel&,const BodyGraphEdit&,document::BodyHistory);
void refresh_body_links(const Workspace&,document::PartDocument&,const std::filesystem::path& owning_file);
}
