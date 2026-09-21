#include <zima/workspace/native_documents.hpp>
#include <zima/assembly/file_relocation.hpp>
#include <zima/drawing/file_relocation.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/document/component_source.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/precision.hpp>
#include <zima/workspace/appearance_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <set>
#include <nlohmann/json.hpp>
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
    document::validate_sheet_cut_tolerance(settings.sheet_cut_tolerance);
    auto document=document::PartDocument::load(template_path(settings.part_template,settings));
    if(!document.history.empty() || !document.sketches.empty() || !document.constructions.empty() ||
       !document.history_order.empty() || !document.body_history.bodies().empty() || !document.body_history.booleans().empty())
        throw std::runtime_error("Start Part template must not contain persisted model object IDs");
    const auto template_id=document.document_id;
    do { document.document_id=document::PartDocument::create_default().document_id; } while(document.document_id==template_id);
    document::BodyHistoryGraph bodies;
    static_cast<void>(document::create_origin_bound_body(bodies,document.document_id,settings.first_body_name));
    document.set_body_history(std::move(bodies));
    char tolerance[64];const auto encoded=std::to_chars(tolerance,tolerance+sizeof(tolerance),settings.sheet_cut_tolerance);
    if(encoded.ec!=std::errc{})throw std::invalid_argument("Cannot encode Sheet Cut tolerance.");
    document.document_precision["sheet_cut_tolerance"]={tolerance,encoded.ptr};
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
bool PreparedNativeDocument::is_drawing_for(const std::string& source_id) const {
    const auto* value = std::get_if<drawing::DrawingDocument>(&document_);
    return value && !source_id.empty() && value->source_document_id == source_id;
}
bool PreparedNativeDocument::rebase_native_files(std::span<const document::FileRelocation> files) {
    document::FileRelocationEdits edits(files);
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Part>)
            edits.document_name(value.document.document_id, value.document.name);
        else if constexpr (std::is_same_v<T, assembly::AssemblyDocument>)
            assembly::collect_file_relocation_edits(value, edits, path_);
        else drawing::collect_file_relocation_edits(value, edits, path_);
    }, document_);
    const bool changed = !edits.empty();
    edits.apply();
    return changed;
}
void PreparedNativeDocument::write(const std::filesystem::path& target) const {
    if (native_document_type(target) != type())
        throw std::invalid_argument("Document type does not match target path");
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Part>) value.document.save(target, value.boundaries);
        else value.save(target);
    }, document_);
}
assembly::AssemblyDocument::SourceResolver native_source_resolver(const Workspace& live) {
    struct Sources {
        std::map<std::string,assembly::PartOccurrence> parts;
        std::map<std::string,document::FamilyDocument> part_families;
        std::map<std::string,std::pair<assembly::AssemblyDocument,std::filesystem::path>> assemblies;
        std::set<std::string> visiting;
        bool resolve(assembly::PartOccurrence& component) {
            const auto separator=component.source_document_id.find(":family:");
            if(separator!=std::string::npos && !parts.contains(component.source_document_id) && !assemblies.contains(component.source_document_id)) {
                const auto parent=component.source_document_id.substr(0,separator);
                if(const auto family=part_families.find(parent);family!=part_families.end()) {
                    for(const auto& [row,packet]:family->second.evaluated)if(packet->at("document_id")==component.source_document_id) {
                        std::vector<kernel::BodyResult> cache;
                        const auto doc=document::PartDocument::from_serialized(*packet,&cache);
                        auto source=assembly::AssemblyDocument::create_part_occurrence(doc.name,doc.document_id,component.source_path,document::component_source(doc,cache));
                        source.body_color=doc.body_color;source.face_colors=doc.face_colors;source.appearance=part_appearance(doc);
                        source.density_kg_mm3=document::material_density_kg_mm3(doc);source.mass_volume_mm3=std::abs(source.calculated_source->volume);
                        parts.emplace(doc.document_id,std::move(source));break;
                    }
                    if(!parts.contains(component.source_document_id))throw std::runtime_error("Open Part family has no requested evaluated member");
                } else if(const auto base=assemblies.find(parent);base!=assemblies.end()) {
                    for(const auto& [row,packet]:base->second.first.family.evaluated)if(packet->at("document_id")==component.source_document_id) {
                        assemblies.emplace(component.source_document_id,std::pair{assembly::AssemblyDocument::from_serialized(*packet),base->second.second});break;
                    }
                    if(!assemblies.contains(component.source_document_id))throw std::runtime_error("Open Assembly family has no requested evaluated member");
                }
            }
            if(const auto found=parts.find(component.source_document_id);found!=parts.end()) {
                if(component.source_kind!=assembly::ComponentSourceKind::Part)throw std::runtime_error("Component source type mismatch");
                const auto& source=found->second;
                component.calculated_source=source.calculated_source;
                component.body_color=source.body_color;component.face_colors=source.face_colors;
                component.appearance=source.appearance;component.density_kg_mm3=source.density_kg_mm3;
                component.mass_volume_mm3=source.mass_volume_mm3;
                return true;
            }
            const auto found=assemblies.find(component.source_document_id);
            if(found==assemblies.end())return false;
            if(component.source_kind!=assembly::ComponentSourceKind::Assembly)throw std::runtime_error("Component source type mismatch");
            if(!visiting.insert(component.source_document_id).second)throw std::runtime_error("Cyclic open Assembly source dependency");
            auto nested=found->second.first;
            try {nested.hydrate_sources(found->second.second,[this](auto& value){return resolve(value);});}
            catch(...) {visiting.erase(component.source_document_id);throw;}
            visiting.erase(component.source_document_id);
            const auto source=assembly::AssemblyDocument::create_assembly_occurrence(component.name,component.source_document_id,found->second.second,nested);
            component.calculated_source=source.calculated_source;component.nested_snapshot=source.nested_snapshot;
            assembly::capture_nested_mass(component,nested);
            return true;
        }
    };
    auto sources=std::make_shared<Sources>();
    for(const auto& state:live.documents())std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,PartState>) {
            const auto& doc=value.session.document();
            sources->part_families.emplace(doc.document_id,doc.family);
            kernel::BodySnapshot snapshot;
            if(value.source_geometry && value.source_generation==value.session.data_generation())snapshot=*value.source_geometry;
            else {
                auto body=document::component_source(doc,value.session.calculated_boundaries());
                body.body_boundaries.clear();body.body_inputs.clear();snapshot=std::move(body);
            }
            auto source=assembly::AssemblyDocument::create_part_occurrence(doc.name,doc.document_id,value.path,std::move(snapshot));
            source.body_color=doc.body_color;source.face_colors=doc.face_colors;source.appearance=part_appearance(doc);
            source.density_kg_mm3=document::material_density_kg_mm3(doc);source.mass_volume_mm3=std::abs(source.calculated_source->volume);
            sources->parts.emplace(doc.document_id,std::move(source));
        } else if constexpr(std::is_same_v<T,AssemblyState>) {
            sources->assemblies.emplace(value.session.document().document_id,std::pair{value.session.document(),value.path});
        }
    },state);
    return [sources](auto& component){return sources->resolve(component);};
}

PreparedNativeDocument read_native_document(const std::filesystem::path& path, const assembly::AssemblyDocument::SourceResolver& resolver, bool resolve_sources) {
    PreparedNativeDocument prepared;prepared.path_=path;
    switch(native_document_type(path)) {
        case NativeDocumentType::Part: {
            PreparedNativeDocument::Part part;
            part.document=document::PartDocument::load(path,&part.boundaries);
            prepared.document_=std::move(part);break;
        }
        case NativeDocumentType::Assembly: prepared.document_=assembly::AssemblyDocument::load(path,resolver,resolve_sources);break;
        case NativeDocumentType::Drawing: prepared.document_=drawing::DrawingDocument::load(path);break;
    }
    return prepared;
}
void PreparedNativeDocument::set_drawing_source(const std::string& id,
    const std::filesystem::path& path,const std::string& name) {
    auto* drawing=std::get_if<drawing::DrawingDocument>(&document_);
    if(!drawing||id.empty()||path.empty())throw std::invalid_argument("A Drawing requires a native source document.");
    const auto source_type=native_document_type(path);
    if(source_type==NativeDocumentType::Drawing)throw std::invalid_argument("Drawing sources must be Parts or Assemblies.");
    drawing->source_document_id=id;drawing->source_path=path;drawing->source_name=name;
    if(!drawing->sheets.empty()) {
        drawing->sheets.front().bom_source_document_id=id;
        drawing->sheets.front().bom_rows=build_bom_rows_for_source(id,path,nullptr);
    }
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
        case NativeDocumentType::Drawing: {
            auto drawing=drawing::DrawingDocument::create_default();
            drawing.sheets.front().name=settings.first_sheet_name;
            prepared.document_=std::move(drawing);break;
        }
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
        else {
            if(prepared.new_document_&&!value.sheets.empty()&&!value.source_document_id.empty()) {
                auto& sheet=value.sheets.front();
                sheet.bom_source_document_id=value.source_document_id;
                sheet.bom_rows=build_bom_rows_for_source(
                    value.source_document_id,value.source_path,&workspace);
            }
            workspace.add_drawing(std::move(value),prepared.path_);
        }
    },prepared.document_);
    return id;
}
} // namespace zima::workspace
