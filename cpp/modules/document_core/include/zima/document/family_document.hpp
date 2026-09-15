#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>

namespace zima::document {
// A family member is a view of its owning native document. Evaluated packets
// are immutable derived data embedded in that same file, never separate files.
struct FamilyDocument {
    std::string parent_id, row_id;
    std::map<std::string, std::shared_ptr<const nlohmann::json>> evaluated;
};
inline nlohmann::json family_document_json(const FamilyDocument& value) {
    auto packets=nlohmann::json::object();
    for(const auto& [id,packet]:value.evaluated)packets[id]=*packet;
    return {{"parent",value.parent_id},{"row",value.row_id},{"evaluated",std::move(packets)}};
}
inline FamilyDocument family_document_from_json(const nlohmann::json& value) {
    FamilyDocument result{value.at("parent").get<std::string>(),value.at("row").get<std::string>(),{}};
    if(result.parent_id.find(":family:")!=std::string::npos || result.parent_id.empty()!=result.row_id.empty() || !value.at("evaluated").is_object() || value.at("evaluated").size()>4096)
        throw std::invalid_argument("Invalid native family ownership.");
    for(const auto& [id,packet]:value.at("evaluated").items()) {
        if(!result.parent_id.empty() || !packet.at("family").at("evaluated").empty() || packet.at("family").at("row")!=id || packet.at("document_id")!=packet.at("family").at("parent").get<std::string>()+":family:"+id)
            throw std::invalid_argument("Nested family snapshots are not allowed.");
        result.evaluated.emplace(id,std::make_shared<const nlohmann::json>(packet));
    }
    return result;
}
}
