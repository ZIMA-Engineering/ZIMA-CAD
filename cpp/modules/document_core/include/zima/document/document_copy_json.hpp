#pragma once
#include <nlohmann/json.hpp>
#include <zima/document/document_copy.hpp>
#include <zima/document/file_path.hpp>
#include <string>
#include <string_view>
#include <stdexcept>

namespace zima::document {
// Re-identify a document namespace, preserving feature/source-curve identities.
// Only identity fields are rewritten: names, expressions, B-Rep bytes and other
// user text must never be searched and replaced as arbitrary strings.
inline void remap_document_identity(nlohmann::json& value,
        const std::string& old_id, const std::string& new_id,
        std::string_view field = {},
        const std::filesystem::path& source_directory = {},
        const std::filesystem::path& target_path = {}) {
    if (field=="user_parameters" || field.starts_with("user_parameter_") ||
        field=="physical_parameters" || field=="physical_parameter_units" ||
        field=="material_parameter_descriptions" || field=="document_units" ||
        field=="document_precision") return;
    if (value.is_object()) {
        if (!target_path.empty() && value.contains("source_path") && value["source_path"].is_string()) {
            const auto path=std::filesystem::u8path(value["source_path"].get<std::string>());
            if (value.value("source_document_id", std::string{})==old_id)
                value["source_path"]=path_to_utf8(target_path);
            else if (!path.empty() && path.is_relative())
                value["source_path"]=path_to_utf8(std::filesystem::absolute(source_directory/path).lexically_normal());
        }
        for (auto it=value.begin();it!=value.end();++it)
            remap_document_identity(it.value(),old_id,new_id,it.key(),source_directory,target_path);
    } else if (value.is_array()) {
        for (auto& item:value) remap_document_identity(item,old_id,new_id,field,source_directory,target_path);
    } else if (value.is_string()) {
        auto text=value.get<std::string>();
        if (field=="id" || field=="document_id" || field=="source_document_id" ||
            field=="context_assembly_document_id" || field=="owner" ||
            field=="owner_id" || field=="source_owner_id" ||
            field=="plane_reference_owner_id" || field=="display_owner") {
            if (text==old_id) value=new_id;
            else if (text==old_id+":origin") value=new_id+":origin";
        } else if (!text.empty() && (field.ends_with("sketch_serialized") ||
                field=="sketches")) {
            auto sketch=nlohmann::json::parse(text);
            remap_document_identity(sketch,old_id,new_id,{},source_directory,target_path);
            value=sketch.dump();
        }
    }
}
inline void apply_document_copy_identity(nlohmann::json& root,
        const DocumentCopyIdentity& copy) {
    const auto& id=copy.document_id;
    if (id.empty()) return;
    const auto old_id=root.at("document_id").get<std::string>();
    if (id==old_id) throw std::invalid_argument("Kopie musí mít nové ID dokumentu.");
    remap_document_identity(root,old_id,id,{},copy.source_path.parent_path(),copy.target_path);
    root["name"]=path_to_utf8(copy.target_path.stem());
}
} // namespace zima::document
