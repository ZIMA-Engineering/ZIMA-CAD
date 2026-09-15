#pragma once
#include <zima/workspace/measurement_edits.hpp>
namespace zima::workspace {
struct BodyPropertiesEdit {
    std::string document_id;
    std::uint64_t revision{},generation{};
    std::shared_ptr<const int> runtime_identity;
    bool creating{};
    document::BodyProperties initial;
};
[[nodiscard]] BodyPropertiesEdit prepare_body_properties_edit(const Workspace&,const std::string&,const std::string& object={},const std::string& prefix="Body properties");
[[nodiscard]] bool commit_body_properties(Workspace&,const BodyPropertiesEdit&,document::BodyProperties);
void remove_body_properties(Workspace&,const std::string&,const std::string&);
}
