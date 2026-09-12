#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <algorithm>
#include <cmath>
#include <zima/document/file_path.hpp>
namespace zima::workspace {
namespace {
void invalid_view() { throw DrawingOperationError("invalid_arguments","Invalid drawing view parameters."); }
void bounded(double value,double low,double high) { if(!std::isfinite(value)||value<low||value>high)invalid_view(); }
}
void validate_drawing_view(const drawing::DrawingView& view) {
    if(view.id.empty()||view.name.empty()||view.name.size()>256||std::ranges::all_of(view.name,[](unsigned char c){return c==' ';})||
       std::ranges::any_of(view.name,[](unsigned char c){return c<32||c==127;}))invalid_view();
    bounded(view.x,-10000,10000);bounded(view.y,-10000,10000);bounded(view.scale,.001,1000);
    bounded(view.dimension_guide_offset,0,1000);bounded(view.dimension_guide_spacing,.1,1000);
    if(view.orientation<drawing::ViewOrientation::Front||view.orientation>drawing::ViewOrientation::Isometric||
       view.display_style<drawing::DisplayStyle::VisibleEdges||view.display_style>drawing::DisplayStyle::Shaded||
       view.hidden_edge_style<drawing::HiddenEdgeStyle::Dashed||view.hidden_edge_style>drawing::HiddenEdgeStyle::Gray||
       view.tangent_edge_style<drawing::TangentEdgeStyle::Visible||view.tangent_edge_style>drawing::TangentEdgeStyle::Hidden||
       view.projection_direction<drawing::ProjectionDirection::None||view.projection_direction>drawing::ProjectionDirection::BottomRight)invalid_view();
    for(const auto& key:view.value_locks)if(key!="x"&&key!="y"&&key!="scale")invalid_view();
    const auto& c=view.camera;
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    for(const auto v:{c.horizontal,c.vertical,c.depth}) {for(double n:{v.x,v.y,v.z})bounded(n,-1.000001,1.000001);if(std::abs(dot(v,v)-1)>1e-6)invalid_view();}
    if(std::abs(dot(c.horizontal,c.vertical))>1e-6||std::abs(dot(c.horizontal,c.depth))>1e-6||std::abs(dot(c.vertical,c.depth))>1e-6)invalid_view();
    const kernel::Vec3 cross{c.horizontal.y*c.vertical.z-c.horizontal.z*c.vertical.y,c.horizontal.z*c.vertical.x-c.horizontal.x*c.vertical.z,c.horizontal.x*c.vertical.y-c.horizontal.y*c.vertical.x};
    if(std::abs(dot(cross,c.depth)+1)>1e-6)invalid_view();
}
drawing::Point2 projection_placement(drawing::ProjectionDirection direction,double distance) {
    constexpr double diagonal=.7071067811865475244;
    switch(direction) {
        case drawing::ProjectionDirection::Right:return {-distance,0};
        case drawing::ProjectionDirection::TopRight:return {-distance*diagonal,distance*diagonal};
        case drawing::ProjectionDirection::Top:return {0,distance};
        case drawing::ProjectionDirection::TopLeft:return {distance*diagonal,distance*diagonal};
        case drawing::ProjectionDirection::Left:return {distance,0};
        case drawing::ProjectionDirection::BottomLeft:return {distance*diagonal,-distance*diagonal};
        case drawing::ProjectionDirection::Bottom:return {0,-distance};
        case drawing::ProjectionDirection::BottomRight:return {-distance*diagonal,-distance*diagonal};
        case drawing::ProjectionDirection::None:return {};
    }
    invalid_view();return {};
}
void edit_drawing_view(drawing::DrawingDocument& document,const std::string& sheet_id,drawing::DrawingView accepted,bool creating,DrawingProjection& projection,bool pending_hatch) {
    auto next=document;auto* sheet=next.find_sheet(sheet_id);
    if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    const auto found=std::ranges::find(sheet->views,accepted.id,&drawing::DrawingView::id);
    if(creating?next.find_view(accepted.id)!=nullptr:found==sheet->views.end())
        throw DrawingOperationError("view_not_found","The drawing view does not exist.");
    if(!creating&&found->parent_view_id!=accepted.parent_view_id)invalid_view();
    if(accepted.use_sheet_scale)accepted.scale=sheet->default_scale;
    validate_drawing_view(accepted);
    if(!accepted.parent_view_id.empty()) {
        const auto parent=std::ranges::find(sheet->views,accepted.parent_view_id,&drawing::DrawingView::id);
        if(parent==sheet->views.end())throw DrawingOperationError("view_not_found","The parent drawing view is unavailable.");
        if(parent->id==accepted.id)throw DrawingOperationError("dependency_cycle","Drawing views contain a cyclic dependency.");
        if(accepted.projection_direction==drawing::ProjectionDirection::None)invalid_view();
        accepted.source_document_id=parent->source_document_id;accepted.source_path=parent->source_path;
        accepted.camera=drawing::projected_camera(parent->camera,accepted.projection_direction,sheet->projection_method);
    }
    if(accepted.section_id.empty()) {accepted.section_snapshot.reset();accepted.section_parent_id.clear();}
    projection.project(accepted,{.pending_hatch=pending_hatch,.refresh_markers=true});
    const double dx=creating?0:accepted.x-found->x,dy=creating?0:accepted.y-found->y;
    if(!accepted.section_id.empty()&&accepted.section_parent_id.empty())for(auto& parent:sheet->views)
        if(parent.id!=accepted.id&&parent.source_document_id==accepted.source_document_id&&parent.section_id.empty()) {
            accepted.section_parent_id=parent.id;
            if(accepted.section_snapshot&&std::ranges::none_of(parent.section_markers,[&](const auto& s){return s.id==accepted.section_id;}))parent.section_markers.push_back(*accepted.section_snapshot);
            break;
        }
    const auto id=accepted.id;
    if(creating)sheet->views.push_back(accepted);else *found=accepted;
    std::set<std::string> refreshed{id};
    const auto children=[&](const auto& self,const drawing::DrawingView& parent,std::size_t depth)->void {
        if(depth>256)throw DrawingOperationError("dependency_limit","The drawing projection hierarchy is too deep.");
        for(auto& child:sheet->views)if(child.parent_view_id==parent.id) {
            if(!refreshed.insert(child.id).second)throw DrawingOperationError("dependency_cycle","Drawing views contain a cyclic dependency.");
            child.source_document_id=parent.source_document_id;child.source_path=parent.source_path;
            child.camera=drawing::projected_camera(parent.camera,child.projection_direction,sheet->projection_method);
            child.x+=dx;child.y+=dy;validate_drawing_view(child);
            projection.project(child,{.refresh_markers=true});self(self,child,depth+1);
        }
    };
    children(children,*next.find_view(id),0);
    sheet->bom_rows=projection.source(accepted).bom;
    if(accepted.section_snapshot)for(auto& s:next.sheets)for(auto& other:s.views)
        if(other.source_document_id==accepted.source_document_id&&other.section_id==accepted.section_id) {
            other.section_snapshot=accepted.section_snapshot;
            projection.project(other,{.pending_hatch=pending_hatch});refreshed.insert(other.id);
        }
    for(auto& s:next.sheets)for(auto& dimension:s.dimensions)if(refreshed.contains(dimension.view_id))
        drawing::refresh_drawing_dimension(*next.find_view(dimension.view_id),dimension);
    if(next.source_document_id.empty()) {
        next.source_document_id=accepted.source_document_id;next.source_path=accepted.source_path;
        next.source_name=document::path_to_utf8(accepted.source_path.stem());
        if(next.source_name.empty()&&!sheet->bom_rows.empty())next.source_name=sheet->bom_rows.front().name;
    }
    document=std::move(next);
}
std::size_t regenerate_drawing_views(drawing::DrawingDocument& document,const Workspace* live,const std::filesystem::path& document_path) {
    auto next=document;
    DrawingProjection projection(live,document_path);
    std::size_t count=0;
    for(auto& sheet:next.sheets) {
        std::optional<std::vector<drawing::BomRow>> sheet_bom;
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
            projection.project(view,{.refresh_markers=true,.require_geometry=false});
            for(auto& dimension:sheet.dimensions)if(dimension.view_id==view.id)drawing::refresh_drawing_dimension(view,dimension);
            state=2;++count;
        };
        for(auto& view:sheet.views)refresh(refresh,view,0);
        for(const auto& view:sheet.views)if(!sheet_bom||view.source_document_id==next.source_document_id)sheet_bom=projection.source(view).bom;
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
