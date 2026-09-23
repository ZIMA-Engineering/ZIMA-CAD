#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/profile_status.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <iostream>
#include <map>
#include <chrono>
#include <numbers>
#include <iomanip>

namespace {
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-5){if(std::abs(a-b)>tolerance)throw std::runtime_error("Mismatch: "+std::to_string(a)+" versus "+std::to_string(b));}
void contract() {
    kernel::OcctKernel kernel;
    for(bool reverse:{false,true}) {
        auto part=document::PartDocument::create_default();const auto source=part.body_history.create_body("Source");
        auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=feature.id;feature.flat.sketch_id=profile.id;
        feature.flat.thickness_override=true;feature.flat.thickness=3;feature.flat.direction=reverse?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        static_cast<void>(profile.add_segment(0,0,100,0));static_cast<void>(profile.add_segment(100,0,100,80));
        static_cast<void>(profile.add_segment(100,80,0,80));static_cast<void>(profile.add_segment(0,80,0,0));
        for(double y:{20.,50.}) {
            static_cast<void>(profile.add_segment(20,y-5,40,y-5));
            static_cast<void>(profile.add_arc(40,y,40,y-5,40,y+5));
            static_cast<void>(profile.add_segment(40,y+5,20,y+5));
            static_cast<void>(profile.add_arc(20,y,20,y+5,20,y-5));
        }
        static_cast<void>(profile.add_circle(70,40,6));
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);part.sketches.push_back(profile);
        if(reverse) {
            auto body=*part.body_history.find(source);body.scope.placement.x=17;body.scope.placement.y=-23;
            body.scope.placement.rotation_x=body.scope.placement.absolute_rotation_x=27;
            body.scope.placement.rotation_y=body.scope.placement.absolute_rotation_y=-19;
            body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=41;part.body_history.update_body(body);
        }
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const double expected=(8000-2*(200+25*std::numbers::pi)-36*std::numbers::pi)*3;
        near(cache.back().body_outputs.at(source)->volume,expected,.001);
        check(document::profile_status(part.sketches.front())==document::ProfileStatus::Closed,"Slot profile is not editable as a closed profile");
        check(!document::flat_preview(part.history.front(),part.sketches.front(),document::sheet_metal_defaults(part)).edges.empty(),"Multi-slot Flat lacks an edit preview");
        const auto seed=*std::ranges::max_element(cache.back().mesh.original_references.triangle_references,{},[](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Plane?f.measured_area.value_or(0):0;});
        near(workspace::suggest_sheet_thickness(cache.back().mesh,seed),3);
        const auto target=part.body_history.create_body("Sheet");
        if(reverse) {
            auto body=*part.body_history.find(target);body.scope.placement.x=-30;body.scope.placement.z=17;
            body.scope.placement.rotation_x=body.scope.placement.absolute_rotation_x=-31;
            body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=13;part.body_history.update_body(body);
        }
        const auto before=part.serialized();
        auto wrong_order=part;wrong_order.body_history.move_body(target,0);
        bool order_rejected=false;try{static_cast<void>(workspace::prepare_sheet_from_body(wrong_order,cache,seed,3,kernel));}catch(const std::invalid_argument&){order_rejected=true;}
        check(order_rejected,"Conversion accepted a source after its destination");
        auto converted=workspace::prepare_sheet_from_body(part,cache,seed,3,kernel);
        check(part.serialized()==before,"Conversion mutated its input");
        check(converted.created.size()==1&&converted.skipped==0,"Planar conversion created unexpected features");
        const auto& sketch=converted.document.sketches.back();
        check(sketch.bsplines.empty()&&sketch.arcs.size()>=4,"Native slots were replaced with splines");
        check(converted.document.history.back().placement.references.empty()&&sketch.external_references.empty(),"Root sheet retained source references");
        near(converted.calculated.back().body_outputs.at(target)->volume,expected,.001);
        const auto& source_mesh=cache.back().body_outputs.at(source)->mesh;
        const auto& converted_mesh=converted.calculated.back().body_outputs.at(target)->mesh;
        for(const auto& point:source_mesh.points) {
            double error=INFINITY;for(const auto& other:converted_mesh.points)
                error=std::min(error,std::hypot(point.position.x-other.position.x,point.position.y-other.position.y,point.position.z-other.position.z));
            near(error,0,.001);
        }
        auto exported=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        check(exported.contour.bsplines.empty(),"Circular DXF contour became splines");
        check(exported.contour.arcs.size()>=4,"DXF lost the slot arcs");near(exported.volume,expected,.001);
        // The new Body must be independent of the source's lifetime.
        converted.document.erase_history_object(feature.id);converted.document.body_history.erase_step(source);
        const auto independent=workspace::calculate_part_with_resolved_references(kernel,converted.document);
        near(independent.back().body_outputs.at(target)->volume,expected,.001);
        const auto serialized=converted.document.serialized();
        auto reopened=document::PartDocument::from_serialized(serialized);
        const auto recalculated=workspace::calculate_part_with_resolved_references(kernel,reopened);
        near(recalculated.back().body_outputs.at(target)->volume,expected,.001);
        bool rejected=false;try{static_cast<void>(workspace::prepare_sheet_from_body(reopened,recalculated,seed,3,kernel));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Conversion accepted a non-empty target Body");
    }
    {
        auto part=document::PartDocument::create_default();const auto source=part.body_history.create_body("Bent source");
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=flat.id;flat.flat.sketch_id=profile.id;
        flat.flat.thickness_override=true;flat.flat.thickness=2;flat.flat.direction=document::ExtrusionDirection::Reverse;
        static_cast<void>(profile.add_segment(0,0,60,0));static_cast<void>(profile.add_segment(60,0,60,40));
        static_cast<void>(profile.add_segment(60,40,0,40));static_cast<void>(profile.add_segment(0,40,0,0));
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);part.history.push_back(flat);part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& edges=cache.back().mesh.original_references.edges;
        const auto edge=std::ranges::find_if(edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
            std::ranges::all_of(e.points,[](auto p){return std::abs(p.y-40)<1e-6&&std::abs(p.z)<1e-6;});});
        check(edge!=edges.end(),"Bend fixture has no joining edge");
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        auto section=sketcher::Sketch::create_default();section.owner_container_id=bend.id;document::initialize_bend_start_profile(section,60);
        bend.bend.sketch_id=section.id;bend.bend.sheet_attachment=true;bend.bend.radius=4;bend.bend.angle_degrees=90;
        bend.placement.references=document::bend_sheet_references(*edge);
        part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);part.history.push_back(bend);part.sketches.push_back(section);
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache);
        auto wall=document::PartDocument::create_sketch_container();wall.feature_kind=document::FeatureKind::Flat;
        auto wall_profile=sketcher::Sketch::create_default();wall_profile.owner_container_id=wall.id;wall.flat.sketch_id=wall_profile.id;
        wall.flat.sheet_attachment=true;wall.flat.direction=document::ExtrusionDirection::Reverse;
        double end_width=0;
        for(const auto& candidate:cache.back().mesh.original_references.edges)if(candidate.reference.owner_id==bend.id) {
            if(std::ranges::none_of(candidate.edge_treatment_side_references,[](const auto& face){return face.semantic_key.starts_with("sweep:cap:end:");}))continue;
            try{wall.placement.references=document::flat_sheet_references(candidate);}
            catch(const std::invalid_argument&){continue;}
            const auto a=candidate.points.front(),b=candidate.points.back();end_width=std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);break;
        }
        check(end_width>0,"Bend has no continuation edge");
        static_cast<void>(wall_profile.add_rectangle(0,0,end_width,25));
        part.insert_history_entry(document::PartHistoryKind::Feature,wall.id);part.history.push_back(wall);part.sketches.push_back(wall_profile);
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[&](const auto& f){return f.owner_id==flat.id&&f.sheet_role==kernel::SheetFaceRole::SideA;});
        check(seed!=faces.end(),"Bend fixture has no base skin");
        const auto target=part.body_history.create_body("Converted");
        auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,2,kernel);
        check(converted.created.size()==3&&converted.skipped==0,"Converter failed to follow the cylinder and its next wall");
        const double folded_volume=converted.calculated.back().body_outputs.at(target)->volume;
        near(folded_volume,cache.back().body_outputs.at(source)->volume,.01);
        const auto dxf=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        near(dxf.volume,folded_volume,.05);
        converted.document.erase_history_object(wall.id);converted.document.erase_history_object(bend.id);converted.document.erase_history_object(flat.id);
        converted.document.body_history.erase_step(source);
        converted.calculated=workspace::calculate_part_with_resolved_references(kernel,converted.document);
        near(converted.calculated.back().body_outputs.at(target)->volume,folded_volume,.01);
        for(bool unfold:{true,false}) {
            auto state=document::PartDocument::create_sketch_container();state.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            converted.document.insert_history_entry(document::PartHistoryKind::Feature,state.id);converted.document.history.push_back(state);
            converted.calculated=workspace::calculate_part_with_resolved_references(kernel,converted.document,&converted.calculated);
            check(converted.calculated.back().calculation_errors.empty(),"Converted Bend failed a sheet-state operation");
            near(converted.calculated.back().body_outputs.at(target)->volume,folded_volume,.05);
        }
        auto cut=document::PartDocument::create_sketch_container();cut.feature_kind=document::FeatureKind::Extrusion;
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
        cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        cut.extrusion.end_condition_forward=cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
        auto opening=sketcher::Sketch::create_default();opening.owner_container_id=cut.id;cut.extrusion.sketch_id=opening.id;
        static_cast<void>(opening.add_segment(10,10,15,10));static_cast<void>(opening.add_segment(15,10,15,15));
        static_cast<void>(opening.add_segment(15,15,10,15));static_cast<void>(opening.add_segment(10,15,10,10));
        converted.document.insert_history_entry(document::PartHistoryKind::Feature,cut.id);
        converted.document.history.push_back(cut);converted.document.sketches.push_back(opening);
        converted.calculated=workspace::calculate_part_with_resolved_references(kernel,converted.document,&converted.calculated);
        check(converted.calculated.back().calculation_errors.empty(),"Converted sheet rejected Sheet Cut");
        near(converted.calculated.back().body_outputs.at(target)->volume,folded_volume-50,.05);
        const auto cut_dxf=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        near(cut_dxf.volume,folded_volume-50,.05);
        check(cut_dxf.contour.segments.size()==dxf.contour.segments.size()+4,"DXF omitted the Sheet Cut opening");
    }
    std::cout<<"Sheet exchange contracts passed\n";
}
}

int main(int argc,char** argv) {
    try {
        if(argc==1){contract();return 0;}
        if(argc!=2&&argc!=3)return 2;
        std::vector<zima::kernel::BodyResult> cache;
        auto part=zima::document::PartDocument::load(argv[1],&cache);
        std::cout<<"Part "<<part.name<<" features="<<part.history.size()<<" bodies="<<part.body_history.bodies().size()<<" cached="<<cache.size()<<'\n';
        if(cache.empty())return 1;
        if(argc==3) {
            const auto before=part.serialized();zima::kernel::OcctKernel kernel;
            if(std::filesystem::path(argv[2]).extension()==".prtz") {
                std::string target;for(const auto& body:part.body_history.bodies())if(body.entries.empty()){target=body.scope.id;break;}
                if(target.empty())target=part.body_history.create_body("Conversion verification");
                if(!target.empty()){part.body_history.move_body(target,part.body_history.order().size()-1);part.body_history.activate(target);}
                const auto& mesh=cache.back().body_outputs.at(part.body_history.order().front())->mesh;
                const auto first=std::ranges::max_element(mesh.triangle_references,{},[](const auto& face){return face.surface&&face.surface->kind==zima::kernel::SurfaceGeometry::Kind::Plane?face.measured_area.value_or(0):0;});
                const double thickness=zima::workspace::suggest_sheet_thickness(mesh,*first);
                std::cout<<"seed="<<first->semantic_key<<" thickness="<<thickness<<std::endl;
                const auto started=std::chrono::steady_clock::now();
                const auto converted=zima::workspace::prepare_sheet_from_body(part,cache,*first,thickness,kernel);
                converted.document.save(argv[2],converted.calculated);
                std::size_t lines=0,arcs=0,circles=0,splines=0;for(const auto& sketch:converted.document.sketches)if(std::ranges::find(converted.created,sketch.owner_container_id)!=converted.created.end()){lines+=sketch.segments.size();arcs+=sketch.arcs.size();circles+=sketch.circles.size();splines+=sketch.bsplines.size();}
                std::cout<<"Created="<<converted.created.size()<<" skipped="<<converted.skipped<<" lines="<<lines<<" arcs="<<arcs<<" circles="<<circles<<" splines="<<splines
                    <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<std::endl;
                auto reopened=zima::document::PartDocument::load(argv[2]);zima::kernel::OcctKernel cold;
                const auto regenerated=zima::workspace::calculate_part_with_resolved_references(cold,reopened);
                check(regenerated.back().calculation_errors.empty(),"Converted fixture failed cold regeneration");
                const auto volume=converted.calculated.back().body_outputs.at(target)->volume;
                near(regenerated.back().body_outputs.at(target)->volume,volume,.001);
                std::cout<<std::setprecision(12)<<"source_volume="<<cache.back().body_outputs.at(part.body_history.order().front())->volume<<" converted_volume="<<volume<<" cold_regeneration=passed\n";
                return 0;
            }
            const auto result=zima::workspace::prepare_sheet_dxf(part,cache,kernel);
            if(before!=part.serialized())throw std::runtime_error("Sheet DXF changed its input document");
            zima::interchange::export_dxf(argv[2],result.contour);
            std::cout<<"Exported contour lines="<<result.contour.segments.size()<<" arcs="<<result.contour.arcs.size()<<" circles="<<result.contour.circles.size()<<" splines="<<result.contour.bsplines.size()<<" volume="<<result.volume<<'\n';return 0;
        }
        const auto& packet=cache.back().mesh.original_references;
        std::map<std::string,zima::kernel::FaceReference> faces;
        for(const auto& face:packet.triangle_references)faces.emplace(face.semantic_key,face);
        for(const auto& [key,face]:faces) {
            std::cout<<key;
            if(face.surface){const auto& s=*face.surface;std::cout<<" kind="<<int(s.kind)<<" r="<<s.radius<<" p="<<s.origin.x<<","<<s.origin.y<<","<<s.origin.z<<" axis="<<s.axis.x<<","<<s.axis.y<<","<<s.axis.z<<" reversed="<<s.reversed;}
            if(face.measured_area)std::cout<<" area="<<*face.measured_area;
            std::cout<<'\n';
        }
        std::cout<<"edges="<<packet.edges.size()<<'\n';
        std::size_t adjacent=0;for(const auto& edge:cache.back().mesh.edges)if(!edge.edge_treatment_side_references.empty())++adjacent;
        std::cout<<"body edges="<<cache.back().mesh.edges.size()<<" with adjacency="<<adjacent<<'\n';
        for(const auto& edge:packet.edges){std::cout<<edge.reference.semantic_key<<" adjacency=";for(const auto& face:edge.edge_treatment_side_references)std::cout<<face.semantic_key<<" ";std::cout<<'\n';}
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
