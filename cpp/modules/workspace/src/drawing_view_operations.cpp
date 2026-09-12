#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <algorithm>
namespace zima::workspace {
std::size_t regenerate_drawing_views(drawing::DrawingDocument& document,const Workspace* live,const std::filesystem::path& document_path) {
    auto next=document;
    struct Source {
        kernel::ViewerMesh mesh;
        std::vector<drawing::BomRow> bom;
        std::vector<drawing::ModelAnnotationSource> annotations;
        std::vector<document::SectionDefinition> sections;
    };
    using Key=std::pair<std::string,std::filesystem::path>;
    std::map<Key,Source> sources;
    const auto source_path=[&](const drawing::DrawingView& view){
        auto path=view.source_path;
        if(!path.empty()&&path.is_relative()&&!document_path.empty())path=document_path.parent_path()/path;
        return path.lexically_normal();
    };
    std::size_t count=0;
    for(auto& sheet:next.sheets) {
        std::optional<std::vector<drawing::BomRow>> sheet_bom;
        for(auto& view:sheet.views) {
            const auto path=source_path(view);const Key key{view.source_document_id,path};
            auto found=sources.find(key);
            if(found==sources.end()) {
                auto [id,mesh]=read_drawing_source(live,path,view.source_document_id);
                auto bom=build_bom_rows_for_source(id,path,live);
                auto annotations=drawing_annotation_sources(live,id,path);
                auto sections=source_sections(live,id,path);
                found=sources.emplace(key,Source{std::move(mesh),std::move(bom),std::move(annotations),std::move(sections)}).first;
            }
            const auto& sections=found->second.sections;
            if(!view.section_id.empty()) {
                const auto section=std::ranges::find(sections,view.section_id,&document::SectionDefinition::id);
                if(section==sections.end())throw DrawingOperationError("section_not_found","The source Section no longer exists. Edit the drawing view first.");
                view.section_snapshot=*section;
            }
            for(auto& marker:view.section_markers) {
                const auto current=std::ranges::find(sections,marker.id,&document::SectionDefinition::id);
                if(current==sections.end())throw DrawingOperationError("section_not_found","A source section trace no longer exists. Edit the drawing view first.");
                marker=*current;
            }
            if(!sheet_bom||view.source_document_id==next.source_document_id)sheet_bom=found->second.bom;
        }
        // Parent-first, independent of file order, with an explicit cycle guard.
        std::map<std::string,int> visit;
        const auto refresh=[&](const auto& self,drawing::DrawingView& view,std::size_t depth)->void {
            if(depth>256)throw DrawingOperationError("dependency_limit","The drawing projection hierarchy is too deep.");
            auto& state=visit[view.id];if(state==2)return;
            if(state==1)throw DrawingOperationError("dependency_cycle","Drawing views contain a cyclic dependency.");
            state=1;
            if(!view.parent_view_id.empty()) {
                const auto parent=std::ranges::find(sheet.views,view.parent_view_id,&drawing::DrawingView::id);
                if(parent==sheet.views.end())throw DrawingOperationError("view_not_found","The parent drawing view is unavailable.");
                if(parent->source_document_id!=view.source_document_id)throw DrawingOperationError("source_identity","A projected view must use its parent source document.");
                self(self,*parent,depth+1);
                view.camera=drawing::projected_camera(parent->camera,view.projection_direction,sheet.projection_method);
            }
            auto& source=sources.at({view.source_document_id,source_path(view)});
            next.refresh_view(view.id,source.mesh);
            drawing::refresh_model_annotations(view,source.annotations);
            state=2;++count;
        };
        for(auto& view:sheet.views)refresh(refresh,view,0);
        if(sheet_bom)sheet.bom_rows=std::move(*sheet_bom);
    }
    document=std::move(next);return count;
}
std::vector<std::string> delete_drawing_view(drawing::DrawingDocument& document,const std::string& id) {
    if(!document.find_view(id))throw DrawingOperationError("view_not_found","The drawing view does not exist.");
    std::set<std::string> removed{id};std::vector<std::string> order{id};
    for(std::size_t i=0;i<order.size();++i)for(const auto& sheet:document.sheets)for(const auto& view:sheet.views)
        if(view.parent_view_id==order[i]&&removed.insert(view.id).second)order.push_back(view.id);
    for(auto& sheet:document.sheets) {
        std::erase_if(sheet.views,[&](const auto& view){return removed.contains(view.id);});
        std::erase_if(sheet.dimensions,[&](const auto& dimension){return removed.contains(dimension.view_id);});
        // A section is an independent source view; only its removed trace link ends.
        for(auto& view:sheet.views)if(removed.contains(view.section_parent_id))view.section_parent_id.clear();
    }
    return order;
}
}
