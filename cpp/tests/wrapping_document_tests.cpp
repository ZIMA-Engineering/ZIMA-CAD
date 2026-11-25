#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/kernel/surface_wrap.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double volume_of(const kernel::BodyResult& result) {
    const auto shape=kernel::read_calculated_body_shape(result);double volume=0;
    for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next()) {
        GProp_GProps mass;BRepGProp::VolumeProperties(solid.Current(),mass);volume+=mass.Mass();
    }
    return volume;
}
void valid(const std::vector<kernel::BodyResult>& results) {
    require(!results.empty(),"Missing Wrap calculation");
    for(const auto& result:results)for(const auto& [id,error]:result.calculation_errors)
        throw std::runtime_error(id+": "+error);
}
#include "wrapping_chain_document_contract.inc"
int main(int argc,char** argv){try {
    wrapping_chain_document_contract();
    for(bool internal:{false,true})
    for(const auto output:{document::WrappingOutput::Curves,document::WrappingOutput::Surface,document::WrappingOutput::Relief,document::WrappingOutput::Split})
    for(bool inward:{false,true}) {
        workspace::Workspace live;kernel::OcctKernel kernel;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        live.add_part(part);live.activate(id);
        auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
        static_cast<void>(base_sketch.add_circle(0,0,30));
        if(internal)static_cast<void>(base_sketch.add_circle(0,0,24));
        auto base=document::PartDocument::create_feature_container(base_sketch.id);base.feature.sides[0].length=40;
        workspace::commit_profile(live,kernel,id,base,workspace::ProfileEditMode::Create,base_sketch);
        auto* state=live.open_part(id);const double volume=volume_of(state->session.calculated_boundaries().back());
        kernel::FaceReference target;
        for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references)
            if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                face.surface->reversed==internal)target=face;
        require(target.valid(),"Native cylindrical target missing");
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_regular_polygon(1,4,5,4,6));
        require(!sketch.constraints.empty()&&!sketch.circles.empty()&&sketch.circles.front().construction,
            "Polygon fixture lacks its actual constraint supports");
        const auto authored=sketch.serialized();
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.feature.type=document::FeatureType::Wrapping;feature.feature.wrapping.output=output;
        feature.feature.wrapping.target.emplace();feature.feature.wrapping.target->reference=target;
        feature.feature.wrapping.height=1.25;
        if(inward&&output==document::WrappingOutput::Relief)feature.combine_mode=document::CombineMode::Subtract;
        const double angle=-.37,radius=internal?24.:30.;
        feature.placement.x=radius*std::cos(angle);feature.placement.y=radius*std::sin(angle);feature.placement.z=20;
        feature.placement.rotation_y=90;feature.placement.rotation_z=angle*180/std::numbers::pi;
        feature.placement.absolute_rotation_y=90;
        workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Create,sketch);
        valid(state->session.calculated_boundaries());
        require(sketch.serialized()==authored,"Wrap changed the caller's authored constraint graph");
        feature=*state->session.document().find_container(feature.id);
        sketch=*std::ranges::find(state->session.document().sketches,sketch.id,&sketcher::Sketch::id);
        sketch.validate();
        const auto* input=workspace::calculated_operation_input(state->session,feature.id);require(input,"Wrap input missing");
        const auto revision=state->session.document().serialized();const auto generation=state->session.data_generation();
        const auto calculations=kernel::body_calculation_count.load();
        const auto preview=state->session.document().wrapping_preview_mesh(feature,input->mesh,&state->session.document());
        require(!preview.edges.empty(),"Wrap preview is empty");
        if(output==document::WrappingOutput::Curves) {
            const auto& points=state->session.calculated_boundaries().back().mesh.original_references.points;
            for(const auto& edge:preview.edges)for(auto endpoint:{edge.points.front(),edge.points.back()})
                require(std::ranges::any_of(points,[&](const auto& p){return p.reference.owner_id==feature.id&&
                    kernel::wrap_detail::length(kernel::wrap_detail::sub(p.position,endpoint))<1e-5;}),
                    "Committed wrapped polygon shifted from the cyan preview");
        }
        const double result_volume=volume_of(state->session.calculated_boundaries().back());
        if(output==document::WrappingOutput::Split) {
            const auto face_count=[](const kernel::BodyResult& result) {
                unsigned count{};for(TopExp_Explorer it(kernel::read_calculated_body_shape(result),TopAbs_FACE);it.More();it.Next())++count;return count;
            };
            require(face_count(state->session.calculated_boundaries().back())>face_count(*input),"Wrap Split did not divide the target face");
        }
        if(output==document::WrappingOutput::Surface) {
            auto shape=kernel::read_calculated_body_shape(state->session.calculated_boundaries().back());
            for(TopExp_Explorer it(shape,TopAbs_FACE,TopAbs_SOLID);it.More();it.Next()) {
                auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
                double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                gp_Pnt p;gp_Vec du,dv;surface.D1((u0+u1)/2,(v0+v1)/2,p,du,dv);
                auto n=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
                require(n.Dot(gp_Vec(p.X(),p.Y(),0))*(internal?-1.:1.)>0,
                    "Wrapped surface reversed the selected material side");
            }
        }
        require(output==document::WrappingOutput::Relief?(inward?result_volume<volume:result_volume>volume):std::abs(result_volume-volume)<1e-5,
            "Wrap changed the wrong material side");
        if(output==document::WrappingOutput::Relief) {
            const double h=feature.feature.wrapping.height,area=24*std::sqrt(3.);
            const double radial_sign=(internal?-1.:1.)*(inward?-1.:1.);
            const double expected=volume+(inward?-1.:1.)*area*(h+radial_sign*h*h/(2*radius));
            require(std::abs(result_volume-expected)<1e-3,"Relief volume disagrees with the independent cylindrical integral");
        }
        workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Replace,sketch);
        require(state->session.data_generation()==generation&&state->session.document().serialized()==revision&&
            kernel::body_calculation_count.load()==calculations,"Preview/unchanged OK calculated or changed history");
        require(workspace::step_part_document_history(live,id,false)&&
            !state->session.document().find_container(feature.id)&&
            std::abs(volume_of(state->session.calculated_boundaries().back())-volume)<1e-6,
            "Wrap Undo did not restore the source material");
        require(workspace::step_part_document_history(live,id,true)&&
            state->session.document().find_container(feature.id)&&
            std::abs(volume_of(state->session.calculated_boundaries().back())-result_volume)<1e-6&&
            kernel::body_calculation_count.load()==calculations,"Wrap Redo changed material or calculated geometry");
        const auto file=std::filesystem::temp_directory_path()/("zima-wrapped-polygon-"+id+".prtz");
        const auto identities=[&](const kernel::BodyResult& result) {
            std::set<std::pair<std::string,std::string>> keys;
            for(const auto& face:result.mesh.triangle_references)keys.emplace(face.owner_id,face.semantic_key);
            for(const auto& edge:result.mesh.edges)keys.emplace(edge.reference.owner_id,edge.reference.semantic_key);
            for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id)
                keys.emplace(edge.reference.owner_id,edge.reference.semantic_key);
            return keys;
        };
        const auto before_keys=identities(state->session.calculated_boundaries().back());
        state->session.document().save(file,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cached;auto restored=document::PartDocument::load(file,&cached);valid(cached);
        kernel::OcctKernel cold;const auto fresh=workspace::calculate_part_with_resolved_references(cold,restored);valid(fresh);
        require(std::abs(volume_of(fresh.back())-result_volume)<1e-6,"Cold native reopen changed wrapped material");
        require(identities(fresh.back())==before_keys,"Cold native reopen changed wrapped face or edge identities");
        std::filesystem::remove(file);
        std::cout<<"PASS constrained polygon Wrap output="<<int(output)<<" inward="<<inward<<" internal="<<internal<<'\n';
    }
    if(argc>1) {
        std::vector<kernel::BodyResult> cached;auto part=document::PartDocument::load(argv[1],&cached);
        for(const auto& sketch:part.sketches)sketch.validate();
        kernel::OcctKernel cold;auto fresh=workspace::calculate_part_with_resolved_references(cold,part);valid(fresh);
        require(!fresh.back().kernel_shape.empty(),"User copy lost its native body");
        for(const auto& feature:part.history)if(feature.feature_kind==document::FeatureKind::Feature&&
                feature.feature.type==document::FeatureType::Wrapping&&feature.feature.wrapping.output==document::WrappingOutput::Curves) {
            const auto preview=part.wrapping_preview_mesh(feature,fresh.back().mesh,&part);
            require(!preview.edges.empty(),"User copy lost its wrapped preview");
            for(const auto& edge:preview.edges)for(auto endpoint:{edge.points.front(),edge.points.back()})
                require(std::ranges::any_of(fresh.back().mesh.original_references.points,[&](const auto& p){return
                    p.reference.owner_id==feature.id&&kernel::wrap_detail::length(kernel::wrap_detail::sub(p.position,endpoint))<1e-4;}),
                    "User copy committed curves differ from the preview");
        }
        if(argc>2)part.save(argv[2],fresh);
        std::cout<<"PASS read-only user copy regeneration: "<<argv[1]<<'\n';
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
