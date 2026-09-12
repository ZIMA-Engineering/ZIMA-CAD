#include <zima/workspace/drawing_projection.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <algorithm>
namespace zima::workspace {
struct DrawingProjection::Impl {
    struct Entry {
        Source source;
        std::map<std::array<double,9>,std::pair<std::vector<drawing::ProjectedEdge>,std::vector<drawing::ProjectedTriangle>>> cameras;
    };
    const Workspace* workspace;
    std::filesystem::path drawing_path;
    std::map<std::pair<std::string,std::filesystem::path>,Entry> sources;

    Entry& get(const drawing::DrawingView& view) {
        auto path=view.source_path;
        if(!path.empty()&&path.is_relative()&&!drawing_path.empty())path=drawing_path.parent_path()/path;
        path=path.lexically_normal();
        const auto key=std::make_pair(view.source_document_id,path);
        if(const auto found=sources.find(key);found!=sources.end())return found->second;
        auto [id,mesh]=read_drawing_source(workspace,path,view.source_document_id);
        auto bom=build_bom_rows_for_source(id,path,workspace);
        auto annotations=drawing_annotation_sources(workspace,id,path);
        auto sections=source_sections(workspace,id,path);
        return sources.emplace(key,Entry{Source{std::move(id),std::move(path),std::move(mesh),std::move(bom),std::move(annotations),std::move(sections)}, {}}).first->second;
    }
};
DrawingProjection::DrawingProjection(const Workspace* workspace,std::filesystem::path path):impl_(std::make_unique<Impl>()) {
    impl_->workspace=workspace;
    impl_->drawing_path=std::move(path);
}
DrawingProjection::~DrawingProjection()=default;
const DrawingProjection::Source& DrawingProjection::source(const drawing::DrawingView& view) {
    return impl_->get(view).source;
}
void DrawingProjection::project(drawing::DrawingView& view,Options options) {
    auto& cached=impl_->get(view);
    const auto& source=cached.source;
    if(options.require_geometry&&source.mesh.edges.empty()&&source.mesh.triangles.empty())
        throw DrawingOperationError("uncalculated_source","The source has no calculated geometry. Regenerate it first.");
    view.source_document_id=source.document_id;
    // Preserve the document's stored relative path; cache keys resolve it once.
    if(!view.section_id.empty()) {
        const auto section=std::ranges::find(source.sections,view.section_id,&document::SectionDefinition::id);
        if(section==source.sections.end())
            throw DrawingOperationError("section_not_found","The source Section no longer exists. Edit the drawing view first.");
        const auto pending=view.section_snapshot;
        view.section_snapshot=*section;
        if(options.pending_hatch&&pending&&pending->id==section->id)view.section_snapshot->components=pending->components;
        drawing::refresh_view_geometry(view,source.mesh);
    }else{
        const auto& c=view.camera;
        const std::array key{c.horizontal.x,c.horizontal.y,c.horizontal.z,c.vertical.x,c.vertical.y,c.vertical.z,c.depth.x,c.depth.y,c.depth.z};
        auto found=cached.cameras.find(key);
        if(found==cached.cameras.end())found=cached.cameras.emplace(key,std::make_pair(drawing::project_edges(source.mesh,c),drawing::project_triangles(source.mesh,c))).first;
        drawing::capture_measurement_geometry(view,source.mesh);
        view.projected_edges=found->second.first;
        view.projected_triangles=found->second.second;
    }
    if(options.refresh_markers)for(auto& marker:view.section_markers) {
        const auto current=std::ranges::find(source.sections,marker.id,&document::SectionDefinition::id);
        if(current==source.sections.end())
            throw DrawingOperationError("section_not_found","A source section trace no longer exists. Edit the drawing view first.");
        marker=*current;
    }
    drawing::refresh_model_annotations(view,source.annotations);
}
}
