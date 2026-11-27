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
#include "wrapping_downstream_contract.inc"
int main(int argc,char** argv){try {
    if(const auto* path=std::getenv("ZIMA_VERIFY_WRAP_PARTIAL_MODEL")) {
        std::vector<kernel::BodyResult> cache;auto part=document::PartDocument::load(path,&cache);
        auto wrap=std::ranges::find_if(part.history,[](const auto& feature){return feature.feature.type==document::FeatureType::Wrapping;});
        auto limit=std::ranges::find_if(part.history,[](const auto& feature){return !feature.feature.sides[0].targets.empty();});
        require(wrap!=part.history.end()&&limit!=part.history.end(),"Partial target fixture is incomplete");
        auto pending=*wrap;pending.feature.wrapping.target.emplace();
        pending.feature.wrapping.target->reference=limit->feature.sides[0].targets.front().reference;
        const auto target=pending.feature.wrapping.target->reference;
        workspace::Workspace live;kernel::OcctKernel kernel;live.add_part(part,cache,path);live.activate(part.document_id);
        auto* state=live.open_part(part.document_id);
        const auto before=state->session.document().serialized();const auto revision=state->session.revision();
        const auto rollback_before=state->session.rollback_boundary(pending.id);
        static_cast<void>(workspace::materialize_part_history_input_boundary(live,kernel,part.document_id,pending.id));
        const auto rollback_after=state->session.rollback_boundary(pending.id);
        require(rollback_before&&rollback_after&&rollback_before->input_body&&rollback_after->input_body,
            "Partial target rollback is missing");
        require(rollback_after->input_body->kernel_shape==rollback_before->input_body->kernel_shape&&
            rollback_after->input_body->calculation_errors==rollback_before->input_body->calculation_errors&&
            state->session.document().serialized()==before&&state->session.revision()==revision,
            "Preparing target selection changed the retained model or failure state");
        require(std::ranges::all_of(rollback_after->input_body->mesh.triangle_references,
            [](const auto& face){return face.valid();}),"Partial target rollback has no selectable identities");
        require(!workspace::materialize_part_history_input_boundary(live,kernel,part.document_id,pending.id),
            "Complete target presentation was prepared twice");
        workspace::commit_profile(live,kernel,part.document_id,pending,workspace::ProfileEditMode::Replace);
        valid(state->session.calculated_boundaries());
        pending=*state->session.document().find_container(pending.id);
        const auto* input=workspace::calculated_operation_input(state->session,pending.id);require(input,"Partial target input missing");
        const auto face=std::ranges::find_if(input->mesh.triangle_references,[&](const auto& ref){
            return ref.owner_id==target.owner_id&&ref.semantic_key==target.semantic_key&&ref.surface;
        });
        require(face!=input->mesh.triangle_references.end()&&face->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder,
            "Partial target has no exact cylinder");
        const auto& cylinder=*face->surface;std::size_t checked{};
        for(const auto& edge:state->session.calculated_boundaries().back().mesh.original_references.edges)
            if(edge.reference.owner_id==pending.id)for(const auto point:edge.points) {
                const auto delta=kernel::wrap_detail::sub(point,cylinder.origin);
                const auto radial=kernel::wrap_detail::sub(delta,kernel::wrap_detail::scale(cylinder.axis,kernel::wrap_detail::dot(delta,cylinder.axis)));
                require(std::abs(kernel::wrap_detail::length(radial)-cylinder.radius)<1e-5,"Wrap escaped its selected analytic support");++checked;
            }
        require(checked>0,"Partial target Wrap has no output curves");
        const auto preview=state->session.document().wrapping_preview_mesh(pending,input->mesh,&state->session.document());
        const auto& markers=state->session.calculated_boundaries().back().mesh.original_references.points;
        require(!preview.edges.empty(),"Partial target Wrap preview is empty");
        for(const auto& point:markers)if(point.reference.owner_id==pending.id)
            require(std::ranges::any_of(preview.edges,[&](const auto& edge){return
                kernel::wrap_detail::length(kernel::wrap_detail::sub(point.position,edge.points.front()))<1e-5||
                kernel::wrap_detail::length(kernel::wrap_detail::sub(point.position,edge.points.back()))<1e-5;}),
                "Partial target preview omitted an exact authored endpoint");
        for(const auto& edge:preview.edges)for(const auto point:edge.points) {
            const auto delta=kernel::wrap_detail::sub(point,cylinder.origin);
            const auto radial=kernel::wrap_detail::sub(delta,kernel::wrap_detail::scale(cylinder.axis,kernel::wrap_detail::dot(delta,cylinder.axis)));
            require(std::abs(kernel::wrap_detail::length(radial)-cylinder.radius)<1e-7,"Partial target preview misses the exact cylinder");
        }
        require(state->session.undo()&&state->session.redo(),"Partial target Wrap Undo/Redo failed");
        const auto output=std::filesystem::temp_directory_path()/"zima-wrap-partial-verified.prtz";
        state->session.document().save(output,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(output,&saved);valid(saved);
        require(reopened.find_container(pending.id)->feature.wrapping.target->reference==target,"Wrap reopen changed the target identity");
        kernel::OcctKernel cold;valid(workspace::calculate_part_with_resolved_references(cold,reopened));
        std::cout<<"Partial analytic Wrap: exact cylinder equation, Undo/Redo, save/reopen and cold regeneration passed\n";
        return 0;
    }
    if(const auto* path=std::getenv("ZIMA_VERIFY_WRAP_DOWNSTREAM_MODEL")) {
        std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);
        const auto report=[](const char* label,const auto& part,const auto& results) {
            valid(results);
            std::cout<<label<<'\n';
            for(const auto& feature:part.history)std::cout<<feature.id<<" valid="<<feature.placement.reference_valid<<" frame="<<feature.placement.x<<','<<feature.placement.y<<','<<feature.placement.z<<" rotation="<<feature.placement.rotation_x<<','<<feature.placement.rotation_y<<','<<feature.placement.rotation_z<<'\n';
            if(!results.empty())for(const auto& [id,error]:results.back().calculation_errors)std::cout<<id<<": "<<error<<'\n';
            if(part.history.size()>1&&!part.history[1].placement.references.empty()) {
                const auto& ref=part.history[1].placement.references.front();
                for(std::size_t i=0;i<results.size();++i) {
                    const auto count=[&](const auto& faces) {int n=0,a=0;for(const auto& face:faces)if(face.owner_id==ref.owner_id&&face.semantic_key==ref.semantic_key){++n;if(face.surface)++a;}return std::to_string(n)+"/"+std::to_string(a);};
                    std::cout<<"boundary "<<i<<" display/analytic="<<count(results[i].mesh.triangle_references)<<" reference/analytic="<<count(results[i].mesh.original_references.triangle_references)<<'\n';
                }
            }
            std::cout<<std::flush;
        };
        report("saved",saved,cache);
        kernel::OcctKernel cold;
        auto replay=saved;auto fresh=workspace::calculate_part_with_resolved_references(cold,replay);
        report("cold",replay,fresh);
        workspace::PartCalculationPolicy policy;policy.history_reference_baseline=&cache;
        replay=saved;fresh=workspace::calculate_part_with_resolved_references(cold,replay,nullptr,policy);
        report("saved baseline",replay,fresh);
        if(const auto* previous=std::getenv("ZIMA_VERIFY_WRAP_DOWNSTREAM_BEFORE")) {
            std::vector<kernel::BodyResult> before_cache;auto before=document::PartDocument::load(previous,&before_cache);
            workspace::Workspace live;const auto id=before.document_id;
            live.add_part(before,before_cache);live.activate(id);
            auto feature=saved.history.back();
            const auto sketch=*std::ranges::find(saved.sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
            workspace::commit_profile(live,cold,id,feature,workspace::ProfileEditMode::Create,sketch,workspace::ProfileFailurePolicy::KeepDefinition);
            const auto unchanged=[&] {
                const auto& after=live.open_part(id)->session.document();
                for(const auto& old:before.history) {
                    const auto* current=after.find_container(old.id);require(current,"Downstream extrusion removed a preceding feature");
                    const auto& a=old.placement;const auto& b=current->placement;
                    require(b.reference_valid&&a.references==b.references,"Downstream extrusion broke preceding reference identity");
                    for(const auto delta:{a.x-b.x,a.y-b.y,a.z-b.z,a.rotation_x-b.rotation_x,a.rotation_y-b.rotation_y,a.rotation_z-b.rotation_z})
                        require(std::abs(delta)<1e-8,"Downstream extrusion changed a preceding frame");
                }
                const auto status=workspace::cached_history_placement_reference_status(after,live.open_part(id)->session.calculated_boundaries());
                for(const auto& [owner,value]:status)for(const auto& reference:after.find_container(owner)->placement.references)
                    if(value.requested.contains(reference.owner_id,reference.semantic_key,reference.instance_path))
                        require(value.missing(reference,{}).empty(),"Valid user input is marked missing after the downstream extrusion");
            };
            report("appended",live.open_part(id)->session.document(),live.open_part(id)->session.calculated_boundaries());
            unchanged();
            for(int i=0;i<3;++i) {
                static_cast<void>(workspace::regenerate_part(live,cold,id));
                report("regenerated",live.open_part(id)->session.document(),live.open_part(id)->session.calculated_boundaries());
                unchanged();
            }
            require(live.open_part(id)->session.undo()&&live.open_part(id)->session.redo(),"User downstream Undo/Redo unavailable");
            unchanged();
            if(const auto* output=std::getenv("ZIMA_VERIFY_WRAP_DOWNSTREAM_OUTPUT")) {
                const auto& session=live.open_part(id)->session;
                session.document().save(output,session.calculated_boundaries());
                std::vector<kernel::BodyResult> persisted;
                const auto reopened=document::PartDocument::load(output,&persisted);valid(persisted);
                require(reopened.find_container(feature.id)!=nullptr,"Repaired copy lost the final extrusion");
            }
        }
        return 0;
    }
    wrapping_downstream_contract();
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
        feature.placement.x=radius*std::cos(angle);feature.placement.y=radius*std::sin(angle);feature.placement.z=output==document::WrappingOutput::Curves?60:20;
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
