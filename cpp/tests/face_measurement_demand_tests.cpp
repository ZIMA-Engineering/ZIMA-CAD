#include "profile_solid_fixture.hpp"
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/kernel/pattern_geometry.hpp>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
#include <iomanip>
#include <sstream>
#include <BRepTools_ShapeSet.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

using namespace zima;
using Json=nlohmann::json;
using Identity=std::tuple<std::string,std::string,std::string>;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
auto identity(const kernel::FaceReference& face){return Identity{face.owner_id,face.semantic_key,face.instance_path};}
auto faces(const kernel::BodyResult& body) {
    std::map<Identity,kernel::FaceReference> result;
    for(const auto& face:body.mesh.original_references.triangle_references)if(face.valid())result[identity(face)]=face;
    return result;
}
auto native_areas(const kernel::BodyResult& eager,const kernel::BodyResult& lazy) {
    require(eager.kernel_shape==lazy.kernel_shape,"Demand policy changed exact native geometry");
    BRep_Builder builder;BRepTools_ShapeSet archive(builder);std::istringstream stream(eager.kernel_shape.get());archive.Read(stream);
    std::map<Identity,double> result;
    for(const auto& binding:lazy.kernel_faces) {
        TopoDS_Shape face;std::istringstream address(binding.locator);archive.Read(face,address);
        require(!face.IsNull()&&face.ShapeType()==TopAbs_FACE,"Exact native measurement binding missing");
        GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);result[identity(binding.reference)]=std::abs(properties.Mass());
    }
    for(std::size_t source=lazy.measurement_sources.size();source>0;--source) {
        const auto& native=lazy.measurement_sources[source-1].get();
        BRepTools_ShapeSet original(builder);std::istringstream bytes(native.kernel_shape.get());original.Read(bytes);
        for(const auto& binding:native.kernel_faces)if(!result.contains(identity(binding.reference))) {
            TopoDS_Shape face;std::istringstream address(binding.locator);original.Read(face,address);
            require(!face.IsNull()&&face.ShapeType()==TopAbs_FACE,"Original native measurement binding missing");
            GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);
            result[identity(binding.reference)]=std::abs(properties.Mass());
        }
    }
    return result;
}
void compare(const kernel::OcctKernel& kernel,const kernel::BodyResult& eager,const kernel::BodyResult& lazy) {
    const auto expected=faces(eager),pending=faces(lazy);
    require(expected.size()==pending.size(),"Demand policy changed face identities");
    std::vector<kernel::FaceReference> requested;
    for(const auto& [key,face]:expected) {
        require(face.measured_area.has_value()&&pending.contains(key),"Eager area oracle missing");
        requested.push_back(pending.at(key));
    }
    // A packet can contain authored original faces from earlier history. This
    // direct snapshot check applies to the actual native faces of this result.
    const auto measured=kernel.measure_face_areas(lazy,requested);const auto oracle=native_areas(eager,lazy);
    std::size_t count=0;
    for(std::size_t i=0;i<requested.size();++i)if(measured[i]) {
        require(oracle.contains(identity(requested[i]))&&*measured[i]==oracle.at(identity(requested[i])),
            "Requested area differs from independent integration of the current native face");++count;
    }
    require(count>0,"No pending face resolved to its native binding");
    require(eager.mesh.vertices==lazy.mesh.vertices&&eager.mesh.triangles==lazy.mesh.triangles&&
        eager.volume==lazy.volume&&eager.surface_area==lazy.surface_area&&eager.surface_centroid==lazy.surface_centroid&&
        eager.volume_integrals==lazy.volume_integrals,"Demand policy changed geometry or physical properties");
}
int main(){try {
    namespace fs=std::filesystem;kernel::OcctKernel kernel;
    auto model=document::PartDocument::create_default();model.history.clear();model.sketches.clear();
    auto plate=test::rectangular_feature(model,{40,30,10});model.history.push_back(plate);
    auto hole=test::circular_feature(model,3,20);hole.combine_mode=document::CombineMode::Subtract;model.history.push_back(hole);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Demand Body"));
    for(const auto& feature:model.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    model.set_body_history(graph);model.resolve_constructions();
    const auto operations=model.kernel_operations();
    const auto eager=kernel.evaluate_history(operations);
    kernel::OcctKernel lazy_kernel;
    std::vector<kernel::BodyResult> lazy;
    {kernel::FaceMeasurementScope demand(false);lazy=lazy_kernel.evaluate_history(operations);}
    const auto body=model.body_history.active_body_id();
    const auto& eager_local=eager.back().body_boundaries.at(body).back();
    const auto& lazy_local=lazy.back().body_boundaries.at(body).back();
    compare(kernel,eager_local,lazy_local);
    const auto actual_areas=native_areas(eager_local,lazy_local);
    const double trimmed=40.*30.-std::numbers::pi*3.*3.;
    require(std::ranges::any_of(actual_areas,[&](const auto& entry){return std::abs(entry.second-trimmed)<1e-9;}),
        "Trimmed plane does not satisfy its independent area equation");
    require(std::ranges::all_of(faces(lazy_local),[](const auto& entry){return !entry.second.measured_area;}),"Ordinary operation integrated face areas");
    const auto original=document::serialize_body_result(lazy.back());
    for(int pass=0;pass<2;++pass) {
        workspace::Workspace live;live.add_part(model,lazy);const auto id=model.document_id;
        const auto state=live.open_part(id)->session.document().serialized();const auto revision=live.open_part(id)->session.revision();
        const auto scene=workspace::measurement_scene(live,id);
        for(const auto& [key,face]:faces(eager_local)) {
            const kernel::MeasurementReference request{kernel::MeasurementKind::Face,face.owner_id,face.semantic_key,{}};
            const auto measured=workspace::resolve_measurement(live,id,request,scene);
            const auto current=actual_areas.find(key);
            const auto expected_area=current==actual_areas.end()?*face.measured_area:current->second;
            if(!measured||!measured->values.area||measured->values.area->approximate||measured->values.area->value!=expected_area)
                std::cerr<<std::setprecision(17)<<"Measurement "<<face.owner_id<<" "<<face.semantic_key<<" expected="<<expected_area
                    <<" actual="<<(measured&&measured->values.area?measured->values.area->value:-1)
                    <<" approximate="<<(measured&&measured->values.area?measured->values.area->approximate:true)<<'\n';
            require(measured&&measured->values.area&&!measured->values.area->approximate&&
                measured->values.area->value==expected_area,"Measurement did not complete the exact requested area");
        }
        require(live.open_part(id)->session.revision()==revision&&live.open_part(id)->session.document().serialized()==state&&
            document::serialize_body_result(live.open_part(id)->session.calculated_boundaries().back())==original,
            "Read-only measurement changed the model, geometry or Undo state");
        if(pass==0) {
            const auto path=fs::temp_directory_path()/("zima-demand-"+model.document_id+".prtz");
            model.save(path,lazy);model=document::PartDocument::load(path,&lazy);fs::remove(path);
            require(document::serialize_body_result(lazy.back())==original,"Native reopen lost deferred measurement topology");
        }
    }
    // Reuse an unchanged prefix, invalidate the changed source, then restore
    // its definition. Explicit measurements must always follow the real input.
    {
        auto changed=model;changed.history.back().extrusion.length_forward=8;
        std::vector<kernel::BodyResult> edited,restored;
        {kernel::FaceMeasurementScope demand(false);
            edited=lazy_kernel.evaluate_history_recovering(changed.kernel_operations(),lazy);
            restored=lazy_kernel.evaluate_history_recovering(model.kernel_operations(),edited);}
        kernel::OcctKernel oracle;
        const auto expected=oracle.evaluate_history(changed.kernel_operations());
        compare(kernel,expected.back().body_boundaries.at(body).back(),edited.back().body_boundaries.at(body).back());
        compare(kernel,eager_local,restored.back().body_boundaries.at(body).back());
        const auto restored_packet=document::serialize_body_result(restored.back());
        if(restored_packet!=original)std::cerr<<Json::diff(original,restored_packet).dump().substr(0,5000)<<'\n';
        require(restored_packet==original,"Warm restore changed deferred native data or references");
    }
    const auto cylinder_model=[&] {
        auto doc=document::PartDocument::create_default();doc.history.clear();doc.sketches.clear();
        doc.history.push_back(test::circular_feature(doc,4,12));
        document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Cylinder Body"));
        bodies.insert({document::PartHistoryKind::Feature,doc.history.front().id});doc.set_body_history(bodies);
        doc.resolve_constructions();return doc;
    }();
    const auto cylinder_eager=kernel.evaluate_history(cylinder_model.kernel_operations());
    std::vector<kernel::BodyResult> cylinder_lazy;
    {kernel::FaceMeasurementScope demand(false);cylinder_lazy=lazy_kernel.evaluate_history(cylinder_model.kernel_operations());}
    const auto& source_eager=cylinder_eager.back().body_boundaries.begin()->second.back();
    const auto& source_lazy=cylinder_lazy.back().body_boundaries.begin()->second.back();
    compare(kernel,source_eager,source_lazy);
    for(double factor:{.5,2.})compare(kernel,kernel.scale_body(source_eager,factor,{3,2,1},"scale"),kernel.scale_body(source_lazy,factor,{3,2,1},"scale"));
    const kernel::MirrorPlane mirror{{2,3,4},{1,1,0}};
    compare(kernel,kernel.mirror_body(source_eager,mirror,"mirror",{9,8,7},{20,30,40}),
        kernel.mirror_body(source_lazy,mirror,"mirror",{9,8,7},{20,30,40}));
    kernel::PatternRequest pattern;pattern.circular=true;pattern.count=3;pattern.angle_degrees=80;pattern.axis={0,0,1};
    compare(kernel,kernel.pattern_body(source_eager,pattern,"pattern",{10,0,0},{}),kernel.pattern_body(source_lazy,pattern,"pattern",{10,0,0},{}));
    {
        workspace::Workspace live;const auto path=fs::temp_directory_path()/("zima-demand-assembly-source-"+model.document_id+".prtz");
        model.save(path,lazy);live.add_part(model,lazy,path);
        auto sub=assembly::AssemblyDocument::create_default();const auto sub_id=sub.document_id;
        live.add_assembly(std::move(sub));const auto part=live.insert_open_part(sub_id,model.document_id,"Measured Part");
        auto placement=live.open_assembly(sub_id)->session.document();placement.find_occurrence(part)->placement.x=37;
        placement.find_occurrence(part)->placement.rotation_z=45;live.open_assembly(sub_id)->session.commit(std::move(placement));
        auto top=assembly::AssemblyDocument::create_default();const auto top_id=top.document_id;live.add_assembly(std::move(top));
        const auto occurrence=live.insert_open_assembly(top_id,sub_id,"Measured Subassembly");
        const auto full=assembly::InstancePath{}.child(occurrence).child(part).encoded();
        const auto scene=workspace::measurement_scene(live,top_id);const auto revision=live.open_assembly(top_id)->session.revision();
        for(const auto& [key,face]:faces(eager_local)) {
            const kernel::MeasurementReference request{kernel::MeasurementKind::Face,face.owner_id,face.semantic_key,full};
            const auto measured=workspace::resolve_measurement(live,top_id,request,scene);
            const auto expected=actual_areas.contains(key)?actual_areas.at(key):*face.measured_area;
            require(measured&&measured->values.area&&!measured->values.area->approximate&&measured->values.area->value==expected,
                "Nested occurrence did not measure its exact source face");
        }
        require(live.open_assembly(top_id)->session.revision()==revision,"Nested measurement changed Assembly history");fs::remove(path);
    }
    std::cout<<"Deferred face measurement: exact trimmed/curved areas, copies, read-only inspection and native reopen passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
