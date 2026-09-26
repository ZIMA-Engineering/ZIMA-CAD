#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;



void AssemblyWorkspaceWindow::show_sketch_offset_properties(const std::string& id) {
    const auto* sketch=active_sketch();if(!sketch||properties_dialog_)return;
    zima::sketcher::SketchOffset initial;
    if(!id.empty()){const auto* offset=sketch->find_offset(id);if(!offset)return;initial=*offset;}
    else {
        for(const auto& selected:{selected_sketch_segment_id_,selected_sketch_circle_id_,selected_sketch_arc_id_,selected_sketch_ellipse_id_,selected_sketch_elliptical_arc_id_,selected_sketch_bspline_id_})
            if(!selected.empty()){initial.source_id=selected;break;}
    }
    cancel_sketch_segment();const auto sketch_id=active_sketch_id_;
    auto* dialog=new SketchOffsetDialog(initial,[this,sketch_id](auto value,bool free) {
        if(active_sketch_id_!=sketch_id)throw std::runtime_error("Sketch is no longer active");
        if(!mutate_active_sketch([&](auto& target) {
            if(free)target.free_offset(value.id);
            else if(value.id.empty())static_cast<void>(target.add_offset(value.source_id,value.distance,value.flipped));
            else {
                for(auto& offset:target.offsets)if(offset.id==value.id)offset.source_id=value.source_id;
                target.update_offset(value.id,value.distance,value.flipped);
            }
        }))throw std::runtime_error("Sketch no longer exists");
    },this);
    properties_dialog_=dialog;sketch_offset_dialog_=dialog;
    dialog->changed=[this]{update_sketch_offset_preview();};
    connect(dialog,&QObject::destroyed,this,[this,dialog] {
        if(properties_dialog_==dialog)properties_dialog_=nullptr;
        sketch_offset_dialog_=nullptr;viewer_->set_transient_edges({});viewer_->set_feature_selected_edges({});
        tree_->setProperty("commandSelectionActive",false);
        clear_selected_sketch_geometry();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    tree_->setProperty("commandSelectionActive",true);viewer_->clear_selection();
    dialog->show();update_sketch_offset_preview();
}

void AssemblyWorkspaceWindow::update_sketch_offset_preview() {
    auto* dialog=sketch_offset_dialog_.data();const auto* sketch=active_sketch();if(!dialog||!sketch)return;
    const auto pending=dialog->values();
    const auto label=[&](const auto& curves,const QString& type){int index=0;for(const auto& curve:curves){++index;if(curve.id==pending.source_id)dialog->set_source_label(type+QString::number(index));}};
    label(sketch->segments,tr("Úsečka "));label(sketch->circles,tr("Kružnice "));label(sketch->arcs,tr("Oblouk "));
    label(sketch->ellipses,tr("Elipsa "));label(sketch->elliptical_arcs,tr("Eliptický oblouk "));label(sketch->bsplines,tr("Spline "));
    viewer_->set_selection_contract(dialog->entering_reference()
        ?std::vector{zima::viewer::CandidateKind::SketchSegment,zima::viewer::CandidateKind::SketchCurve}
        :std::vector<zima::viewer::CandidateKind>{});
    std::vector<zima::kernel::ViewerEdge> edges;
    if(pending.source_id.empty()){viewer_->set_transient_edges({});dialog->set_error(tr("Vyberte zdrojovou křivku."));return;}
    try {
        auto preview=*sketch;std::string id=pending.id;
        if(!dialog->freeing()) {
            if(id.empty())id=preview.add_offset(pending.source_id,pending.distance,pending.flipped);
            else {for(auto& offset:preview.offsets)if(offset.id==id)offset.source_id=pending.source_id;preview.update_offset(id,pending.distance,pending.flipped);}
        }
        for(auto edge:preview.viewer_mesh().edges)if(edge.reference.semantic_key=="bspline:"+id){edge.color="#CA76FF";edges.push_back(std::move(edge));}
        if(!dialog->freeing()) {
            const auto curve=sketch->supporting_curve(pending.source_id);
            double start=0;if(const auto* offset=preview.find_offset(id))start=offset->start;
            const auto a=zima::kernel::bspline_value(curve,start);
            const auto normal=zima::sketcher::curve_offset_point(curve,start,pending.flipped?-1.0:1.0);
            const double nx=normal.x-a.x,ny=normal.y-a.y,n=std::hypot(nx,ny);
            const double arrow_length=std::max(pending.distance,viewer_->world_tolerance_for_pixels(24));
            const zima::kernel::Vec3 b{a.x+nx/n*arrow_length,a.y+ny/n*arrow_length,0};
            const double dx=b.x-a.x,dy=b.y-a.y,l=std::hypot(dx,dy),head=std::min(l*.35,viewer_->world_tolerance_for_pixels(10));
            zima::kernel::ViewerEdge arrow;arrow.overlay=true;arrow.color="#CA76FF";
            arrow.points={sketch->world_point(a.x,a.y),sketch->world_point(b.x,b.y)};edges.push_back(arrow);
            arrow.points={sketch->world_point(b.x-dx/l*head-dy/l*head*.45,b.y-dy/l*head+dx/l*head*.45),sketch->world_point(b.x,b.y),sketch->world_point(b.x-dx/l*head+dy/l*head*.45,b.y-dy/l*head-dx/l*head*.45)};edges.push_back(std::move(arrow));
        }
        if(dialog->inspecting())for(auto edge:sketch->viewer_mesh().edges) {
            const auto colon=edge.reference.semantic_key.find(':');
            if(colon!=std::string::npos&&edge.reference.semantic_key.substr(colon+1)==pending.source_id){edge.color="#00D1FF";edges.push_back(std::move(edge));}
        }
        dialog->set_error({});
    }catch(const std::exception& e){edges.clear();dialog->set_error(QString::fromUtf8(e.what()));}
    viewer_->set_transient_edges(std::move(edges));
}

} // namespace zima::app
