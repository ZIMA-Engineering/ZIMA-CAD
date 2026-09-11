#include <zima/workspace/workspace.hpp>
#include <zima/document/document_copy_json.hpp>
#include <zima/document/viewer_packet_json.hpp>
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
        const fs::path& drawing_search_directory) const {
    const auto* source=find(document_id);
    if (!source) throw std::invalid_argument("Dokument není otevřený.");
    const auto target=normalized(requested_target);
    const auto source_path=std::visit([](const auto& state) { return normalized(state.path); },*source);
    if (target.empty() || target==source_path)
        throw std::invalid_argument("Kopie musí mít jiný název než původní dokument.");
    const auto new_id=zima::document::PartDocument::create_default().document_id;
    const zima::document::DocumentCopyIdentity identity{new_id,source_path,target};
    struct PendingFile { fs::path target; std::function<void(const fs::path&)> write; };
    std::vector<PendingFile> pending;
    std::visit([&](const auto& state) {
        using T=std::decay_t<decltype(state)>;
        pending.push_back({target,[&,identity](const fs::path& staged) {
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
                    for (std::size_t i=1;i<=before.size();++i)
                        fingerprints[zima::kernel::history_fingerprint(before,i)]=
                            zima::kernel::history_fingerprint(after,i);
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
                    zima::document::remap_document_identity(json,document_id,identity.document_id);
                    rekey(rekey,json);
                    boundaries.push_back(zima::document::load_body_result(json));
                }
                copied.save(staged,boundaries);
                const auto checked=zima::document::PartDocument::load(staged);
                if (checked.document_id!=identity.document_id)
                    throw std::runtime_error("Kopie Partu má nesprávné ID.");
            } else if constexpr (std::is_same_v<T,AssemblyState>) {
                state.session.document().save(staged,identity);
                if (zima::assembly::AssemblyDocument::load(staged).document_id!=identity.document_id)
                    throw std::runtime_error("Kopie sestavy má nesprávné ID.");
            } else {
                state.document.save(staged,identity);
                if (zima::drawing::DrawingDocument::load(staged).document_id!=identity.document_id)
                    throw std::runtime_error("Kopie výkresu má nesprávné ID.");
            }
        }});
    },*source);

    if (!std::holds_alternative<DrawingState>(*source)) {
        std::vector<DrawingState> drawings;
        std::set<fs::path> open_paths;
        std::set<std::string> seen_ids;
        const auto belongs=[&](const zima::drawing::DrawingDocument& drawing) {
            if (drawing.source_document_id==document_id ||
                (!source_path.empty() && normalized(drawing.source_path)==source_path)) return true;
            if (!drawing.source_document_id.empty()) return false;
            for (const auto& sheet:drawing.sheets) for (const auto& view:sheet.views)
                if (view.source_document_id==document_id) return true;
            return false;
        };
        const auto add=[&](const DrawingState& state) {
            if (belongs(state.document) && seen_ids.insert(state.document.document_id).second)
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
            return a_companion!=b_companion ? a_companion : a.path.generic_string()<b.path.generic_string();
        });
        for (std::size_t index=0;index<drawings.size();++index) {
            auto drawing=drawings[index].document;
            const auto drawing_target=target.parent_path()/(target.stem().string()+
                (index==0 ? std::string{} : "_"+std::to_string(index+1))+".drwz");
            const auto drawing_id=zima::drawing::DrawingDocument::create_default().document_id;
            const auto old_drawing_path=normalized(drawings[index].path);
            drawing.source_document_id=new_id;
            drawing.source_path=target;
            drawing.source_name=target.stem().string();
            const auto rebind_origin=[&](auto& reference) {
                if (reference.owner_id==document_id) reference.owner_id=new_id;
                else if (reference.owner_id==document_id+":origin") reference.owner_id=new_id+":origin";
            };
            for (auto& sheet:drawing.sheets) {
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
                    (!source_path.empty() && normalized(view.source_path)==source_path)) {
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
        }
    }
    // Preflight the whole set before any destination is written.
    for (const auto& file:pending)
        if (fs::exists(file.target) || document_id_for_path(file.target))
            throw std::invalid_argument("Cílový soubor již existuje: "+file.target.string());
    const auto staging_path=target.parent_path()/(".zima-copy-"+new_id);
    if (!fs::create_directory(staging_path)) throw std::runtime_error("Nelze připravit adresář pro kopii.");
    StagingDirectory staging{staging_path};
    for (const auto& file:pending) file.write(staging.path/file.target.filename());
    std::vector<fs::path> published;
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
        throw;
    }
    return published;
}
} // namespace zima::workspace
