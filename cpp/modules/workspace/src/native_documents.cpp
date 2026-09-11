#include <zima/workspace/native_documents.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <type_traits>

namespace zima::workspace {
namespace {
std::filesystem::path template_path(const std::filesystem::path& requested,
                                    const NativeTemplateSettings& settings) {
    const auto path=requested.is_absolute()?requested:settings.directory/requested;
    if(!std::filesystem::exists(path))throw std::runtime_error("Start document template does not exist: "+path.string());
    return path;
}
}
NativeDocumentType native_document_type(const std::filesystem::path& path) {
    auto extension=path.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".prtz")return NativeDocumentType::Part;
    if(extension==".asmz")return NativeDocumentType::Assembly;
    if(extension==".drwz")return NativeDocumentType::Drawing;
    throw std::invalid_argument("Unsupported native document extension");
}

document::PartDocument part_from_template(const NativeTemplateSettings& settings) {
    auto document=document::PartDocument::load(template_path(settings.part_template,settings));
    if(!document.history.empty() || !document.sketches.empty() || !document.constructions.empty() ||
       !document.history_order.empty() || !document.body_history.bodies().empty() || !document.body_history.booleans().empty())
        throw std::runtime_error("Start Part template must not contain persisted model object IDs");
    const auto template_id=document.document_id;
    do { document.document_id=document::PartDocument::create_default().document_id; } while(document.document_id==template_id);
    document::BodyHistoryGraph bodies;
    static_cast<void>(document::create_origin_bound_body(bodies,document.document_id,settings.first_body_name));
    document.set_body_history(std::move(bodies));
    return document;
}
assembly::AssemblyDocument assembly_from_template(const NativeTemplateSettings& settings) {
    auto document=assembly::AssemblyDocument::load(template_path(settings.assembly_template,settings));
    if(!document.components.empty() || !document.sketches.empty() || !document.cuts.empty() ||
       !document.constructions.empty() || !document.dependencies.empty())
        throw std::runtime_error("Start Assembly template must not contain persisted model object IDs");
    const auto template_id=document.document_id;
    do { document.document_id=assembly::AssemblyDocument::create_default().document_id; } while(document.document_id==template_id);
    return document;
}
NativeDocumentType PreparedNativeDocument::type() const {
    return static_cast<NativeDocumentType>(document_.index());
}
const std::string& PreparedNativeDocument::id() const {
    return std::visit([](const auto& value)->const std::string& {
        if constexpr(std::is_same_v<std::decay_t<decltype(value)>,Part>)return value.document.document_id;
        else return value.document_id;
    },document_);
}
PreparedNativeDocument read_native_document(const std::filesystem::path& path) {
    PreparedNativeDocument prepared;prepared.path_=path;
    switch(native_document_type(path)) {
        case NativeDocumentType::Part: {
            PreparedNativeDocument::Part part;
            part.document=document::PartDocument::load(path,&part.boundaries);
            prepared.document_=std::move(part);break;
        }
        case NativeDocumentType::Assembly: prepared.document_=assembly::AssemblyDocument::load(path);break;
        case NativeDocumentType::Drawing: prepared.document_=drawing::DrawingDocument::load(path);break;
    }
    return prepared;
}
PreparedNativeDocument prepare_new_native_document(NativeDocumentType type, const std::string& name,
    const std::filesystem::path& target, const NativeTemplateSettings& settings,
    const std::map<std::string,std::string>& units) {
    if(name.empty() || name=="." || name==".." || name.find_first_of("\\/:*?\"<>|")!=std::string::npos || name.back()=='.')
        throw std::invalid_argument("Invalid document name");
    if(target.empty() || native_document_type(target)!=type)throw std::invalid_argument("Document type does not match target path");
    if(std::filesystem::exists(target))throw std::invalid_argument("Document path already exists");
    PreparedNativeDocument prepared;prepared.path_=target;prepared.new_document_=true;
    switch(type) {
        case NativeDocumentType::Part: prepared.document_=PreparedNativeDocument::Part{part_from_template(settings),{}};break;
        case NativeDocumentType::Assembly: prepared.document_=assembly_from_template(settings);break;
        case NativeDocumentType::Drawing: prepared.document_=drawing::DrawingDocument::create_default();break;
        default: throw std::invalid_argument("Unsupported document type");
    }
    std::visit([&](auto& value) {
        if constexpr(std::is_same_v<std::decay_t<decltype(value)>,PreparedNativeDocument::Part>) {
            value.document.name=name;for(const auto& [key,unit]:units)value.document.document_units[key]=unit;
        } else {
            value.name=name;
            if constexpr(std::is_same_v<std::decay_t<decltype(value)>,assembly::AssemblyDocument>)
                for(const auto& [key,unit]:units)value.document_units[key]=unit;
        }
    },prepared.document_);
    return prepared;
}
std::string insert_native_document(Workspace& workspace, PreparedNativeDocument prepared) {
    if(const auto open=workspace.document_id_for_path(prepared.path_)) {
        if(!prepared.new_document_)return *open;
        throw std::invalid_argument("Document path is already open");
    }
    if(prepared.new_document_ && std::filesystem::exists(prepared.path_))throw std::invalid_argument("Document path already exists");
    const auto id=prepared.id();
    std::visit([&](auto& value) {
        if constexpr(std::is_same_v<std::decay_t<decltype(value)>,PreparedNativeDocument::Part>)
            workspace.add_part(std::move(value.document),std::move(value.boundaries),prepared.path_);
        else if constexpr(std::is_same_v<std::decay_t<decltype(value)>,assembly::AssemblyDocument>)
            workspace.add_assembly(std::move(value),prepared.path_);
        else workspace.add_drawing(std::move(value),prepared.path_);
    },prepared.document_);
    return id;
}
} // namespace zima::workspace
