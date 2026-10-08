#include <zima/workspace/workspace.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/document_copy_json.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/versioned_file.hpp>
#include <map>
#include <algorithm>
#include <cctype>
#include <type_traits>
#include <functional>
#include <set>
#include <stdexcept>

namespace zima::workspace {
namespace {
namespace fs = std::filesystem;
fs::path normalized(const fs::path& path) {
    return path.empty() ? fs::path{} : fs::absolute(path).lexically_normal();
}
struct StagingDirectory {
    fs::path path;
    ~StagingDirectory() { std::error_code error; fs::remove_all(path,error); }
};
}

std::vector<std::filesystem::path> Workspace::save_copy(
        const std::string& document_id, const fs::path& requested_target,
        const fs::path& drawing_search_directory, bool overwrite_library) const {
    const auto* source=find(document_id);
    if (!source) throw std::invalid_argument("Dokument není otevřený.");
    const auto* library=std::get_if<PartState>(source);
    overwrite_library=overwrite_library&&library&&library->native_drawing_template;
    const auto target=normalized(requested_target);
    auto extension=target.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return std::tolower(c);});
    std::string expected=std::holds_alternative<PartState>(*source)?".prtz":std::holds_alternative<AssemblyState>(*source)?".asmz":".drwz";
    if(const auto* part=std::get_if<PartState>(source);part&&(part->native_drawing_template||part->symbol_definition)) {
        expected=part->path.extension().string();
        std::ranges::transform(expected,expected.begin(),[](unsigned char c){return std::tolower(c);});
    }
    if(extension!=expected)throw std::invalid_argument("Kopie musí mít příponu odpovídající typu dokumentu.");
    const auto source_path=std::visit([](const auto& state) { return normalized(state.path); },*source);
    if (target.empty() || target==source_path)
        throw std::invalid_argument("Kopie musí mít jiný název než původní dokument.");
    const auto new_id=zima::document::PartDocument::create_default().document_id;
    struct PendingFile { fs::path target; std::function<void(const fs::path&)> write; };
    std::vector<PendingFile> pending;
    const auto append_copy=[&](const DocumentState& selected,const std::string& original_id,
            const fs::path& target,const std::string& copy_id) {
        const auto source_path=std::visit([](const auto& state){return normalized(state.path);},selected);
        const zima::document::DocumentCopyIdentity identity{copy_id,source_path,target};
        std::visit([&](const auto& state) {
            using T=std::decay_t<decltype(state)>;
            pending.push_back({target,[&state,identity,original_id,target](const fs::path& staged) {
                if constexpr (std::is_same_v<T,PartState>) {
                    // Rebase references and relative paths before assigning cache
                    // fingerprints. The geometry is unchanged; no kernel call is needed.
                    state.session.document().save(staged,{},identity);
                    const auto copied=zima::document::PartDocument::load(staged);
                    const auto old_ops=state.session.document().kernel_operations();
                    const auto new_ops=copied.kernel_operations();
                    std::map<std::string,std::string> fingerprints;
                    const auto map_prefixes=[&](const auto& before,const auto& after) {
                        if (before.size()!=after.size()) throw std::runtime_error("Kopie změnila historii modelu.");
                        const auto old_keys=zima::kernel::history_fingerprints(before),new_keys=zima::kernel::history_fingerprints(after);
                        for (std::size_t i=1;i<=before.size();++i)fingerprints[old_keys[i]]=new_keys[i];
                    };
                    map_prefixes(old_ops,new_ops);
                    std::map<std::string,std::vector<zima::kernel::HistoryOperation>> old_branches,new_branches;
                    for (auto op:old_ops) { const auto id=op.body.id;op.body={};old_branches[id].push_back(std::move(op)); }
                    for (auto op:new_ops) { const auto id=op.body.id;op.body={};new_branches[id].push_back(std::move(op)); }
                    for (const auto& [id,ops]:old_branches) map_prefixes(ops,new_branches.at(id));
                    const auto rekey=[&](const auto& self,nlohmann::json& value)->void {
                        if (value.is_object()) {
                            for (auto it=value.begin();it!=value.end();++it) {
                                if (it.key()=="source_fingerprint" && it->is_string()) {
                                    const auto old=it->get<std::string>();std::string result;
                                    for (std::size_t start=0;start<old.size();) {
                                        const auto end=old.find(':',start);
                                        const auto token=old.substr(start,end==std::string::npos ? end : end-start);
                                        const auto found=fingerprints.find(token);
                                        result+=found==fingerprints.end() ? token : found->second;
                                        if (end==std::string::npos) break;
                                        result+=':';start=end+1;
                                    }
                                    *it=std::move(result);
                                } else self(self,it.value());
                            }
                        } else if (value.is_array()) for (auto& item:value) self(self,item);
                    };
                    std::vector<zima::kernel::BodyResult> boundaries;
                    for (const auto& boundary:state.session.calculated_boundaries()) {
                        auto json=zima::document::serialize_body_result(boundary);
                        zima::document::remap_document_identity(json,original_id,identity.document_id);
                        rekey(rekey,json);
                        boundaries.push_back(zima::document::load_body_result(json));
                    }
                    copied.save(staged,boundaries);
                    const auto checked=zima::document::PartDocument::load(staged);
                    if (checked.document_id!=identity.document_id)
                        throw std::runtime_error("Kopie Partu má nesprávné ID.");
                } else if constexpr (std::is_same_v<T,AssemblyState>) {
                    state.session.document().save(staged,identity,target);
                    if (zima::assembly::AssemblyDocument::load(staged).document_id!=identity.document_id)
                        throw std::runtime_error("Kopie sestavy má nesprávné ID.");
                } else {
                    state.document().save(staged,identity);
                    if (zima::drawing::DrawingDocument::load(staged).document_id!=identity.document_id)
                        throw std::runtime_error("Kopie výkresu má nesprávné ID.");
                }
            }});
        },selected);
    };

    const auto append_drawing=[&](const DrawingState& state,const fs::path& drawing_target,
            const std::string& drawing_id,const std::string& document_id,
            const fs::path& target,const std::string& new_id,const fs::path& source_path) {
        auto drawing=state.document();
        const auto old_drawing_path=normalized(state.path);
        for(auto& source:drawing.sources)if(source.document_id==document_id){source.document_id=new_id;source.source_path=target;}
        drawing.source_document_id=new_id;
        drawing.source_path=target;
        drawing.source_name=zima::document::path_to_utf8(target.stem());
        const auto rebind_origin=[&](auto& reference) {
            if (reference.owner_id==document_id) reference.owner_id=new_id;
            else if (reference.owner_id==document_id+":origin") reference.owner_id=new_id+":origin";
        };
        for (auto& sheet:drawing.sheets) {
            if(sheet.bom_source_document_id==document_id)sheet.bom_source_document_id=new_id;
            if(sheet.selected_source_document_id==document_id)sheet.selected_source_document_id=new_id;
            for(auto& row:sheet.bom_rows)
                if(row.source_document_id==document_id ||
                    (row.source_document_id.empty() && !source_path.empty() && normalized(row.source_path)==source_path)) {
                    row.source_document_id=new_id;row.source_path=target;
                    row.file_stem=zima::document::path_to_utf8(target.stem());
                }
            for (auto& dimension:sheet.dimensions) {
                for(auto& attachment:dimension.attachments){rebind_origin(attachment.reference);rebind_origin(attachment.other_reference);}rebind_origin(dimension.parallel_reference);
            }
        }
        for (auto& sheet:drawing.sheets) for (auto& view:sheet.views) {
            auto measuring=*view.measurement_geometry;
            for(auto& curve:measuring.curves)rebind_origin(curve.source);
            for(auto& point:measuring.points)rebind_origin(point.source);
            view.measurement_geometry=zima::drawing::share_measurement_geometry(std::move(measuring));
            if (view.source_document_id==document_id ||
                (view.source_document_id.empty() && !source_path.empty() && normalized(view.source_path)==source_path)) {
                view.source_document_id=new_id;
                view.source_path=target;
                for (auto& edge:view.projected_edges) rebind_origin(edge.source);
                for (auto& triangle:view.projected_triangles) rebind_origin(triangle.source);
            }
        }
        pending.push_back({drawing_target,[drawing=std::move(drawing),drawing_id,
                old_drawing_path,drawing_target,new_id](const fs::path& staged) {
            drawing.save(staged,{drawing_id,old_drawing_path,drawing_target});
            const auto checked=zima::drawing::DrawingDocument::load(staged);
            if (checked.document_id!=drawing_id || checked.source_document_id!=new_id)
                throw std::runtime_error("Výkresová kopie není navázána na kopii modelu.");
        }});
    };

    // Resolve only the Drawing's primary owner. Other Drawing sources and
    // Assembly components retain their existing dependencies. Open models,
    // including unsaved edits, are authoritative; closed models use native cache.
    std::optional<Workspace> loaded_model;
    if(const auto* drawing=std::get_if<DrawingState>(source);drawing&&
            (!drawing->document().source_document_id.empty()||!drawing->document().source_path.empty())) {
        const auto& definition=drawing->document();
        auto model_id=definition.source_document_id;
        auto model_path=definition.source_path;
        if(!model_path.empty()&&model_path.is_relative())model_path=source_path.parent_path()/model_path;
        if(model_id.empty()&&!model_path.empty())
            if(const auto open=document_id_for_path(model_path))model_id=*open;
        const DocumentState* model=model_id.empty()?nullptr:find(model_id);
        if(!model) {
            if(model_path.empty())throw std::invalid_argument("Zdrojový dokument výkresu nelze otevřít.");
            const auto type=native_document_type(model_path);
            if(type==NativeDocumentType::Drawing)
                throw std::invalid_argument("Zdrojový dokument výkresu nelze otevřít.");
            loaded_model.emplace();
            if(type==NativeDocumentType::Part) {
                std::vector<zima::kernel::BodyResult> boundaries;
                auto definition=read_family_part(this,model_path,model_id,boundaries);
                model_id=definition.document_id;
                loaded_model->add_part(std::move(definition),std::move(boundaries),model_path);
            } else {
                auto definition=read_family_assembly(this,model_path,model_id,false);
                model_id=definition.document_id;
                loaded_model->add_assembly(std::move(definition),model_path);
            }
            model=loaded_model->find(model_id);
        }
        if(!model||std::holds_alternative<DrawingState>(*model))
            throw std::invalid_argument("Zdrojový dokument výkresu nelze otevřít.");
        model_path=std::visit([](const auto& state){return normalized(state.path);},*model);
        auto model_target=target;model_target.replace_extension(std::holds_alternative<PartState>(*model)?".prtz":".asmz");
        const auto model_copy_id=zima::document::PartDocument::create_default().document_id;
        append_copy(*model,model_id,model_target,model_copy_id);
        append_drawing(*drawing,target,new_id,model_id,model_target,model_copy_id,model_path);
    } else append_copy(*source,document_id,target,new_id);

    const bool family_member=std::visit([](const auto& value){if constexpr(requires{value.session;})return !value.session.document().family.parent_id.empty();else return false;},*source);
    if (!std::holds_alternative<DrawingState>(*source)&&!family_member&&!(library&&(library->native_drawing_template||library->symbol_definition))) {
        std::vector<DrawingState> drawings;
        std::set<fs::path> open_paths;
        std::set<std::string> seen_ids;
        const auto belongs=[&](const zima::drawing::DrawingDocument& drawing) {
            if (drawing.source_document_id==document_id ||
                (drawing.source_document_id.empty() && !source_path.empty() && normalized(drawing.source_path)==source_path)) return true;
            if (!drawing.source_document_id.empty()) return false;
            for (const auto& sheet:drawing.sheets) for (const auto& view:sheet.views)
                if (view.source_document_id==document_id) return true;
            return false;
        };
        const auto add=[&](const DrawingState& state) {
            if (belongs(state.document()) && seen_ids.insert(state.document().document_id).second)
                drawings.push_back(state);
        };
        // Open documents are authoritative, including unsaved drawing edits.
        for (const auto& state:documents()) if (const auto* drawing=std::get_if<DrawingState>(&state)) {
            if (!drawing->path.empty()) open_paths.insert(normalized(drawing->path));
            add(*drawing);
        }
        const std::set<fs::path> directories{source_path.parent_path(),normalized(drawing_search_directory)};
        for (const auto& directory:directories) {
            if (directory.empty() || !fs::is_directory(directory)) continue;
            for (const auto& entry:fs::directory_iterator(directory)) {
                const auto path=normalized(entry.path());
                auto extension=path.extension().string();
                std::transform(extension.begin(),extension.end(),extension.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (!entry.is_regular_file() || extension!=".drwz" || open_paths.contains(path)) continue;
                try { add({zima::drawing::DrawingDocument::load(path),path}); }
                catch (const std::exception&) {
                    auto companion=source_path; companion.replace_extension(".drwz");
                    if (path==companion) throw; // Never silently drop the companion drawing.
                }
            }
        }
        auto companion=source_path;companion.replace_extension(".drwz");
        std::sort(drawings.begin(),drawings.end(),[&](const auto& a,const auto& b) {
            const bool a_companion=normalized(a.path)==companion;
            const bool b_companion=normalized(b.path)==companion;
            return a_companion!=b_companion ? a_companion : a.path<b.path;
        });
        for (std::size_t index=0;index<drawings.size();++index) {
            const auto drawing_target=target.parent_path()/fs::u8path(zima::document::path_to_utf8(target.stem())+
                (index==0 ? std::string{} : "_"+std::to_string(index+1))+".drwz");
            const auto drawing_id=zima::drawing::DrawingDocument::create_default().document_id;
            append_drawing(drawings[index],drawing_target,drawing_id,document_id,target,new_id,source_path);
        }
    }
    // Preflight the whole set before any destination is written.
    for (const auto& file:pending)
        if ((fs::exists(file.target)&&(!overwrite_library||!fs::is_regular_file(file.target))) || document_id_for_path(file.target))
            throw std::invalid_argument("Cílový soubor již existuje: "+zima::document::path_to_utf8(file.target));
    const auto staging_path=target.parent_path()/(".zima-copy-"+new_id);
    if (!fs::create_directory(staging_path)) throw std::runtime_error("Nelze připravit adresář pro kopii.");
    StagingDirectory staging{staging_path};
    for (const auto& file:pending) reserve_file(file.target);
    for (const auto& file:pending) file.write(staging.path/file.target.filename());
    std::vector<fs::path> published;
    const auto previous=staging.path/"previous-library";
    if(overwrite_library&&fs::exists(target)) {
        zima::document::archive_existing_file(target);
        fs::rename(target,previous);
    }
    try {
        for (const auto& file:pending) {
            // Atomic publication without overwrite or a partially written
            // destination. Staging lives on the same filesystem; removing
            // its link afterwards leaves the complete destination file.
            fs::create_hard_link(staging.path/file.target.filename(),file.target);
            published.push_back(file.target);
        }
    } catch (...) {
        for (const auto& path:published) { std::error_code error; fs::remove(path,error); }
        if(fs::exists(previous))fs::rename(previous,target);
        throw;
    }
    return published;
}
} // namespace zima::workspace
