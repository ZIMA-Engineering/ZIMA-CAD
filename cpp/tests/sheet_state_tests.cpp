#include "profile_solid_fixture.hpp"
#include <zima/document/bend.hpp>
#include <zima/document/sheet_state.hpp>
#include <zima/document/flat.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <iostream>
#include <chrono>
using namespace zima;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void close(double value,double expected,double tolerance){if(std::abs(value-expected)>tolerance)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
void references_close(const kernel::BodyResult& a,const kernel::BodyResult& b) {
    for(const auto& point:a.mesh.original_references.points) {
        const auto found=std::ranges::find_if(b.mesh.original_references.points,[&](const auto& other){return other.reference==point.reference;});
        check(found!=b.mesh.original_references.points.end(),"Round trip lost an original vertex identity.");
        const auto delta=kernel::sheet_material::sub(point.position,found->position);
        close(std::sqrt(kernel::sheet_material::dot(delta,delta)),0,.05);
    }
}
void vertices_close(const kernel::BodyResult& source,const kernel::BodyResult& result,double tolerance) {
    check(!source.mesh.points.empty()&&!result.mesh.points.empty(),"State geometry has no persisted vertices.");
    for(const auto& point:source.mesh.points) {
        double distance=INFINITY;
        for(const auto& other:result.mesh.points)distance=std::min(distance,std::hypot(
            point.position.x-other.position.x,point.position.y-other.position.y,point.position.z-other.position.z));
        close(distance,0,tolerance);
    }
}
kernel::HistoryOperation cut_at(const kernel::SheetMaterialDefinition& frame,double along,double length,
        double half_width,double half_length,const std::string& owner,bool sheet_cut=false) {
    using namespace kernel::sheet_material;
    const Coordinate station{along,length,0};const auto center=point(frame,station);const auto axes=basis(frame,station);
    kernel::ExtrusionRequest cutter;kernel::ExtrusionRequest::PolygonProfile rectangle;
    for(auto xy:{std::pair{-half_width,-half_length},std::pair{half_width,-half_length},
            std::pair{half_width,half_length},std::pair{-half_width,half_length}})
        rectangle.vertices.push_back(add(center,add(mul(axes[0],xy.first),mul(axes[1],xy.second))));
    cutter.outer_profile=std::move(rectangle);cutter.direction=mul(axes[2],8);cutter.start_offset=-4;
    cutter.profile_region_id=owner+"-profile";cutter.outer_boundary_id=owner+"-boundary";
    cutter.sheet_cut=sheet_cut;cutter.sheet_cut_tolerance=.05;
    for(const auto* key:{"a","b","c","d"})cutter.outer_edge_source_ids.push_back(owner+"-edge-"+key);
    for(const auto* key:{"p","q","r","s"})cutter.outer_vertex_source_ids.push_back(owner+"-point-"+key);
    kernel::HistoryOperation cut;cut.owner_id=owner;cut.primitive=std::move(cutter);cut.operation=kernel::BooleanOperation::Subtract;
    return cut;
}
document::PartDocument part(double angle) {
    auto result=document::PartDocument::create_default();
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=bend.id;
    static_cast<void>(sketch.add_segment(0,0,40,0));bend.bend.sketch_id=sketch.id;
    bend.bend.thickness_override=true;bend.bend.thickness=2;
    bend.bend.radius_follows_thickness=false;bend.bend.radius=8;
    bend.bend.angle_degrees=angle;
    result.history={bend};result.sketches={sketch};result.resolve_constructions();return result;
}
}
int main(){try {
    kernel::OcctKernel kernel;
    {
        auto source=part(77).kernel_operations();const auto parent=*source.front().sheet_material;
        kernel::SheetMaterialDefinition child;child.owner_id="attached-flat";child.parent_owner_id=parent.owner_id;
        child.thickness=2;const kernel::sheet_material::Coordinate end{13,parent.angle*parent.neutral_radius,0};
        child.origin=kernel::sheet_material::point(parent,end);const auto axes=kernel::sheet_material::basis(parent,end);
        child.along=axes[0];child.tangent=axes[1];child.radial=axes[2];
        kernel::HistoryOperation attached;attached.owner_id=child.owner_id;attached.sheet_material=child;source.push_back(attached);
        auto material=kernel::sheet_material::regions_before(source,source.size());const auto original=material.regions;
        for(int cycle=0;cycle<50;++cycle) {
            static_cast<void>(kernel::sheet_material::change(material,{}));
            check(material.regions.back().origin!=child.origin,"Attached origin did not follow unfolding.");
            static_cast<void>(kernel::sheet_material::change(material,{false,true,{}}));
            check(material.regions==original,"Bend Back recalculated an original frame instead of restoring source values exactly.");
        }
    }
    for(double angle:{30.,90.,180.}) {
        std::cout<<"Angle "<<angle<<std::endl;
        auto document=part(angle);auto operations=document.kernel_operations();
        auto before=kernel.evaluate_history(operations).back();
        const auto frame=*operations.front().sheet_material;
        auto flat=frame;flat.unfolded=true;
        for(double theta:{0.,.25,.75,1.})for(double depth:{-2.,-1.,0.,1.}) {
            kernel::sheet_material::Coordinate c{17,theta*frame.angle*frame.neutral_radius,depth};
            const auto p=kernel::sheet_material::point(frame,c);
            const auto restored=kernel::sheet_material::Transition{flat,frame}.map(kernel::sheet_material::Transition{frame,flat}.map(p));
            close(std::hypot(p.x-restored.x,p.y-restored.y,p.z-restored.z),0,1e-9);
        }
        kernel::HistoryOperation unbend;unbend.owner_id="unbend";unbend.primitive=kernel::SheetStateRequest{};
        operations.push_back(unbend);
        auto unfolded=kernel.evaluate_history(operations).back();
        const auto authored_owner=operations.front().owner_id;
        check(std::ranges::any_of(unfolded.mesh.triangle_references,[&](const auto& reference) {
                return reference.owner_id==unbend.owner_id&&
                    reference.display_owner_id==authored_owner;
            }),"Unbend does not expose its transformed faces through the authored feature.");
        const auto line=std::ranges::find_if(unfolded.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);});
        check(line!=unfolded.mesh.axes.end(),"Unbend did not display a bend line.");
        const auto station=kernel::sheet_material::coordinates(flat,line->point);
        close(station.along,20,1e-9);close(station.length,frame.neutral_radius*frame.angle/2,1e-9);
        close(station.depth,-2,1e-9);close(line->display_length,40,1e-9);
        check(line->reference.owner_id=="unbend","Bend line is not owned by the derived state.");
        const auto reopened_flat=document::load_body_result(document::serialize_body_result(unfolded));
        check(std::ranges::any_of(reopened_flat.mesh.triangle_references,[&](const auto& reference) {
                return reference.owner_id==unbend.owner_id&&
                    reference.display_owner_id==authored_owner;
            }),"Native cache lost transparent sheet-state View ownership.");
        check(std::ranges::any_of(reopened_flat.mesh.original_references.axes,[&](const auto& axis){return axis.reference==line->reference&&axis.point==line->point;}),"Native cache lost the bend line reference.");
        std::cout<<"Flat volume "<<unfolded.volume<<std::endl;
        close(unfolded.volume,40*2*frame.neutral_radius*frame.angle,.05);
        kernel::HistoryOperation back;back.owner_id="back";back.primitive=kernel::SheetStateRequest{false,true,{}};
        operations.push_back(back);auto boundaries=kernel.evaluate_history(operations);auto restored=boundaries.back();
        check(std::ranges::none_of(restored.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);}),"Bend Back left a flat bend line visible.");
        close(restored.volume,before.volume,.1);
        vertices_close(before,restored,1e-9);
        references_close(before,restored);
        for(const auto& reference:unfolded.mesh.triangle_references) {
            if(reference.owner_id!=unbend.owner_id)throw std::runtime_error("Unbend face owner: "+reference.owner_id+", key: "+reference.semantic_key);
            check(reference.semantic_key.starts_with("sheet-state:from:"),"Unbend lost source ancestry.");
        }
        check(std::ranges::any_of(unfolded.mesh.original_references.edges,[&](const auto& edge){return edge.reference.owner_id==unbend.owner_id;}),
            "Unbend did not publish its own original reference geometry.");
        const auto reopened=document::load_body_result(document::serialize_body_result(restored));
        references_close(restored,reopened);
        for(int mode=0;mode<4;++mode) {
            auto edited=operations;edited.pop_back();
            zima::test::ProfilePrism box(6,mode==0?4.:mode==3?.7:3.,frame.neutral_radius*frame.angle*.2);
            box.translation={10,mode==0?-1.:mode==3?.65:1.,frame.neutral_radius*frame.angle*.3};
            kernel::HistoryOperation cut;cut.owner_id="material-edit";cut.primitive=box;
            cut.operation=mode==2?kernel::BooleanOperation::Add:kernel::BooleanOperation::Subtract;
            edited.push_back(cut);auto changed=kernel.evaluate_history(edited).back();
            check(std::ranges::any_of(changed.mesh.axes,[&](const auto& axis){return axis.reference==line->reference;}),"Material edit lost the active bend line.");
            edited.push_back(back);auto refolded=kernel.evaluate_history(edited).back();
            auto second=unbend;second.owner_id="second-unbend";edited.push_back(second);
            auto second_flat=kernel.evaluate_history(edited).back();
            check(std::ranges::any_of(second_flat.mesh.triangle_references,[&](const auto& reference) {
                    return reference.owner_id==second.owner_id&&
                        reference.display_owner_id==authored_owner;
                }),"Repeated Unbend/Bend Back replaced the original View owner with a state feature.");
            std::cout<<"Edit "<<mode<<" flat volume "<<changed.volume<<" round trip "<<second_flat.volume<<std::endl;
            close(second_flat.volume,changed.volume,.05);
            vertices_close(changed,second_flat,1e-6);
            check(refolded.volume>0,"Modified refolded solid lost its volume.");
            check(mode==2?refolded.volume>before.volume:refolded.volume<before.volume-1,
                "A material edit disappeared while refolding.");
            if(angle==90&&mode==0)for(int repeat=0;repeat<4;++repeat) {
                auto next_back=back;next_back.owner_id="repeat-back-"+std::to_string(repeat);edited.push_back(next_back);
                auto next_flat=unbend;next_flat.owner_id="repeat-flat-"+std::to_string(repeat);edited.push_back(next_flat);
                const auto repeated=kernel.evaluate_history(edited).back();
                check(std::ranges::none_of(repeated.mesh.triangle_references,[&](const auto& reference) {
                        return reference.display_owner_id==next_back.owner_id||
                            reference.display_owner_id==next_flat.owner_id;
                    }),"Repeated state cycle became selectable instead of the authored feature.");
                vertices_close(changed,repeated,1e-6);close(repeated.volume,changed.volume,1e-6);
            }
        }
    }
    for(const auto extensions:{std::pair{-5.,10.},std::pair{6.,-4.},std::pair{7.,-7.}}) {
        std::cout<<"Variable width "<<extensions.first<<","<<extensions.second<<std::endl;
        auto document=part(90);document::set_bend_profile_extensions(document.history.front(),extensions.first,extensions.second);
        document.resolve_constructions();auto operations=document.kernel_operations();const auto before=kernel.evaluate_history(operations).back();
        kernel::HistoryOperation unbend;unbend.owner_id="width-unbend";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        auto back=unbend;back.owner_id="width-back";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        const auto restored=kernel.evaluate_history(operations).back();references_close(before,restored);
        close(restored.volume,before.volume,.1);
        operations.pop_back();auto frame=*operations.front().sheet_material;frame.unfolded=true;
        const auto flat_width=kernel.evaluate_history(operations).back();
        const auto line=std::ranges::find_if(flat_width.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);});
        check(line!=flat_width.mesh.axes.end(),"Variable-width Unbend lost its bend line.");
        close(line->display_length,40+(extensions.first+extensions.second)/2,1e-8);
        close(kernel::sheet_material::coordinates(frame,line->point).along,20+(extensions.second-extensions.first)/4,1e-8);
        operations.push_back(cut_at(frame,20,frame.angle*frame.neutral_radius/2,3,2,"width-cut",true));
        const auto cut_flat=kernel.evaluate_history(operations).back();operations.push_back(back);
        check(kernel.evaluate_history(operations).back().volume<before.volume-1,"Variable-width Bend Back lost its cut.");
        unbend.owner_id="width-flat-again";operations.push_back(unbend);
        vertices_close(cut_flat,kernel.evaluate_history(operations).back(),1e-6);
    }
    for(double angle:{45.,90.,180.}) {
        std::cout<<"Straight continuation "<<angle<<std::endl;
        auto document=part(angle);auto& feature=document.history.front();auto& start=document.sketches.front();
        auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
        const auto& arc=path.arcs.front();const auto join=*path.find_point(arc.end_point_id);
        const double radians=angle*std::numbers::pi/180;
        static_cast<void>(path.add_segment(join.x,join.y,join.x+15*std::cos(radians),join.y+15*std::sin(radians)));
        document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(document));
        document.resolve_constructions();auto operations=document.kernel_operations();const auto before=kernel.evaluate_history(operations).back();
        kernel::HistoryOperation unbend;unbend.owner_id="continuation-unbend";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        const auto flat=kernel.evaluate_history(operations).back();
        close(flat.volume,40*2*(operations.front().sheet_material->neutral_radius*radians+15),.05);
        auto back=unbend;back.owner_id="continuation-back";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        vertices_close(before,kernel.evaluate_history(operations).back(),1e-9);
    }
    {
        std::cout<<"Selective independent regions"<<std::endl;
        auto document=part(65),second=part(110);second.history.front().placement.x=80;
        document.history.push_back(second.history.front());document.sketches.push_back(second.sketches.front());document.resolve_constructions();
        auto operations=document.kernel_operations();const auto before=kernel.evaluate_history(operations).back();
        const auto first_id=document.history.front().id,second_id=document.history.back().id;
        kernel::HistoryOperation unbend;unbend.owner_id="selective-first";unbend.primitive=kernel::SheetStateRequest{true,false,{first_id}};operations.push_back(unbend);
        const auto selective_first=kernel.evaluate_history(operations).back();
        check(std::ranges::count_if(selective_first.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);})==1,"Selective Unbend has the wrong bend lines.");
        auto state=kernel::sheet_material::regions_before(operations,operations.size());
        check(state.regions.front().unfolded&&!state.regions.back().unfolded,"Selective Unbend changed an unselected region.");
        unbend.owner_id="selective-second";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        const auto selective_both=kernel.evaluate_history(operations).back();
        check(std::ranges::count_if(selective_both.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);})==2,"Second Unbend lost the earlier bend line.");
        auto back=unbend;back.owner_id="selective-back";back.primitive=kernel::SheetStateRequest{false,false,{second_id}};operations.push_back(back);
        const auto selective_back=kernel.evaluate_history(operations).back();
        check(std::ranges::count_if(selective_back.mesh.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);})==1,"Selective Bend Back did not retire only its own line.");
        state=kernel::sheet_material::regions_before(operations,operations.size());
        check(state.regions.front().unfolded&&!state.regions.back().unfolded,"Selective Bend Back changed an unselected region.");
        back.owner_id="selective-back-all";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        const auto restored=kernel.evaluate_history(operations).back();vertices_close(before,restored,1e-9);close(before.volume,restored.volume,1e-6);
    }
    for(bool conical:{false,true})for(bool reverse:{false,true}) {
        std::cout<<"Revolved sheet cone="<<conical<<" reverse="<<reverse<<std::endl;
        auto document=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_revolution_container(sketch.id);
        document::initialize_sheet_revolution(feature,sketch,{});
        feature.revolution.thickness_override=true;feature.revolution.thin_thickness=2;
        feature.revolution.angle_degrees=110;
        feature.revolution.direction=reverse?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        if(conical) {
            const auto axis=std::ranges::find(sketch.segments,feature.revolution.axis_segment_id,&sketcher::SketchSegment::id);
            sketch.find_point(axis->second_point_id)->y=20;
        }
        document.history={feature};document.sketches={sketch};document.resolve_constructions();
        auto operations=document.kernel_operations();const auto before=kernel.evaluate_history(operations).back();
        const auto frame=*operations.front().sheet_material;auto flat_frame=frame;flat_frame.unfolded=true;
        for(double s:{0.,3.,15.})for(double fraction:{0.,.13,.5,.87,1.})for(double depth:{-2.,0.,2.}) {
            const kernel::sheet_material::Coordinate station{s,fraction*frame.angle*frame.neutral_radius,depth};
            const auto p=kernel::sheet_material::point(frame,station);
            const auto flat_point=kernel::sheet_material::Transition{frame,flat_frame}.map(p);
            const auto restored=kernel::sheet_material::Transition{flat_frame,frame}.map(flat_point);
            close(std::hypot(p.x-restored.x,p.y-restored.y,p.z-restored.z),0,1e-9);
        }
        kernel::HistoryOperation unbend;unbend.owner_id="revolved-unbend";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        const auto flat=kernel.evaluate_history(operations).back();check(flat.volume>0,"Revolved sheet failed to unfold.");
        auto back=unbend;back.owner_id="revolved-back";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        const auto restored=kernel.evaluate_history(operations).back();close(restored.volume,before.volume,.1);
        references_close(before,restored);
        if(conical) {
            using namespace kernel::sheet_material;
            const auto& generator=*std::ranges::find_if(document.sketches.front().segments,[](const auto& value){return !value.construction;});
            const auto* a=document.sketches.front().find_point(generator.first_point_id);
            const auto* b=document.sketches.front().find_point(generator.second_point_id);
            auto station=coordinates(frame,document.sketches.front().world_point((a->x+b->x)/2,(a->y+b->y)/2));
            station.length=frame.angle*frame.neutral_radius*.5;station.depth=0;
            const auto center=point(flat_frame,station);const auto axes=basis(flat_frame,station);
            kernel::ExtrusionRequest cutter;kernel::ExtrusionRequest::PolygonProfile rectangle;
            for(auto xy:{std::pair{-2.,-3.},std::pair{2.,-3.},std::pair{2.,3.},std::pair{-2.,3.}})
                rectangle.vertices.push_back(add(center,add(mul(axes[0],xy.first),mul(axes[1],xy.second))));
            cutter.outer_profile=std::move(rectangle);cutter.direction=mul(axes[2],8);cutter.start_offset=-4;
            cutter.profile_region_id="cone-cut-profile";cutter.outer_boundary_id="cone-cut-boundary";
            cutter.outer_edge_source_ids={"cone-cut-a","cone-cut-b","cone-cut-c","cone-cut-d"};
            cutter.outer_vertex_source_ids={"cone-cut-p","cone-cut-q","cone-cut-r","cone-cut-s"};
            kernel::HistoryOperation cut;cut.owner_id="cone-flat-cut";cut.primitive=cutter;cut.operation=kernel::BooleanOperation::Subtract;
            operations.pop_back();operations.push_back(cut);const auto cut_flat=kernel.evaluate_history(operations).back();
            check(cut_flat.volume<flat.volume-1,"Flat cone cutter missed the source material.");
            operations.push_back(back);check(kernel.evaluate_history(operations).back().volume<before.volume,"Cone Bend Back lost its cut.");
            auto again=unbend;again.owner_id="cone-flat-again";operations.push_back(again);
            const auto second_flat=kernel.evaluate_history(operations).back();vertices_close(cut_flat,second_flat,1e-6);
        }
    }
    for(double angle:{45.,90.,180.}) {
        std::cout<<"Attached Flat angle "<<angle<<std::endl;
        auto document=part(angle);const auto source=kernel.evaluate_history(document.kernel_operations()).back();
        const auto& geometry=source.mesh.original_references;
        const auto edge=std::ranges::find_if(geometry.edges,[](const auto& value) {
            return std::ranges::any_of(value.edge_treatment_side_references,[](const auto& face){return face.sheet_role==kernel::SheetFaceRole::SideA;})&&
                std::ranges::any_of(value.edge_treatment_side_references,[](const auto& face){return face.semantic_key.starts_with("sweep:cap:end:from:");});
        });
        check(edge!=geometry.edges.end(),"Attachment test has no Sheet Profile end edge.");
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=flat.id;flat.flat.sketch_id=sketch.id;
        flat.flat.sheet_attachment=true;flat.flat.direction=document::ExtrusionDirection::Reverse;
        flat.placement.references=document::flat_sheet_references(*edge);
        document.history.push_back(flat);document.sketches.push_back(sketch);document.resolve_constructions(geometry);
        auto& outline=document.sketches.back();static_cast<void>(outline.add_rectangle(0,0,40,15));document.resolve_constructions(geometry);
        auto operations=document.kernel_operations();const auto before=kernel.evaluate_history(operations).back();
        const auto stored_placement=document.history.back().placement;
        kernel::HistoryOperation unbend;unbend.owner_id="attached-unbend";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        const auto unfolded=kernel.evaluate_history(operations).back();
        auto back=unbend;back.owner_id="attached-back";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        const auto restored=kernel.evaluate_history(operations).back();
        close(before.volume,restored.volume,.05);vertices_close(before,restored,1e-9);
        check(document.history.back().placement==stored_placement,"Unfolding modified shared container placement.");
        operations.pop_back();auto frame=*operations.front().sheet_material;frame.unfolded=true;
        operations.push_back(cut_at(frame,20,frame.angle*frame.neutral_radius,3,4,"joint-cut"));
        const auto cut_flat=kernel.evaluate_history(operations).back();
        close(unfolded.volume-cut_flat.volume,6*8*2,.05);
        operations.push_back(back);const auto cut_folded=kernel.evaluate_history(operations).back();
        check(cut_folded.volume<before.volume-1,"Cross-region cut disappeared during Bend Back.");
        unbend.owner_id="joint-flat-again";operations.push_back(unbend);
        const auto returned_flat=kernel.evaluate_history(operations).back();
        vertices_close(cut_flat,returned_flat,1e-6);close(cut_flat.volume,returned_flat.volume,.05);
    }
    {
        std::cout<<"Zero-radius hem"<<std::endl;
        auto document=part(180);document.history.front().bend.radius=0;
        document.resolve_constructions();auto operations=document.kernel_operations();
        static_cast<void>(kernel.evaluate_history(operations));
        kernel::HistoryOperation unbend;unbend.owner_id="hem-unbend";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
        const auto flat=kernel.evaluate_history(operations).back();
        close(flat.volume,40*2*operations.front().sheet_material->neutral_radius*std::numbers::pi,.05);
        auto back=unbend;back.owner_id="hem-back";back.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(back);
        check(kernel.evaluate_history(operations).back().volume>0,"Hem did not refold.");
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,.5,20,1.5));
        auto cut=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=cut.id;
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
        cut.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
        cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        document.history.push_back(cut);document.sketches.push_back(sketch);document.resolve_constructions();
        auto cut_operations=document.kernel_operations();const auto cut_folded=kernel.evaluate_history(cut_operations).back();
        cut_operations.push_back(unbend);const auto cut_flat=kernel.evaluate_history(cut_operations).back();
        check(cut_flat.volume<flat.volume,"Hem Sheet Cut disappeared during unfolding.");
        cut_operations.push_back(back);const auto cut_refolded=kernel.evaluate_history(cut_operations).back();
        close(cut_refolded.volume,cut_folded.volume,.05);
        auto flat_edits=operations;flat_edits.pop_back();
        zima::test::ProfilePrism tool(10,4,.4);tool.translation={10,-1,.4};
        kernel::HistoryOperation pierce;pierce.owner_id="flat-hem-cut";pierce.primitive=tool;pierce.operation=kernel::BooleanOperation::Subtract;
        flat_edits.push_back(pierce);const auto pierced_flat=kernel.evaluate_history(flat_edits).back();
        check(pierced_flat.volume<flat.volume,"Flat hem test did not cut any material.");
        flat_edits.push_back(back);check(kernel.evaluate_history(flat_edits).back().volume>0,"Flat-cut hem did not refold.");
        auto next_flat=unbend;next_flat.owner_id="hem-again";flat_edits.push_back(next_flat);
        const auto repeated=kernel.evaluate_history(flat_edits).back();vertices_close(pierced_flat,repeated,1e-6);
        close(pierced_flat.volume,repeated.volume,1e-6);
    }
    std::cout<<"Sheet state tests passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
