#include "assembly_profile_test_support.hpp"
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <iostream>
using namespace assembly_profile_test;
int main(int argc,char** argv){try {
    const auto directory=(argc>1?fs::absolute(argv[1]):fs::temp_directory_path())/("zima-assembly-curves-"+document::PartDocument::create_default().document_id);
    fs::create_directories(directory);kernel::OcctKernel engine;Fixture f(engine,directory,"reference-curves");
    kernel::FaceReference target;double support_z{};
    for(const auto& face:f.doc().build_drawing_scene().triangle_references) {
        const auto surface=face.surface?face.surface:face.contour_surface;
        if(face.instance_path==assembly::InstancePath{}.child(f.first).encoded()&&face.owner_id==f.box&&surface&&surface->kind==kernel::SurfaceGeometry::Kind::Plane&&surface->axis.z>.9) {target=face;support_z=surface->origin.z;break;}
    }
    require(target.valid(),"No exact Assembly plane target");
    const auto source_before=f.live.open_part(f.source)->session.document().serialized();
    std::vector<std::string> created;
    for(const auto type:{document::FeatureType::Projection,document::FeatureType::Wrapping}) {
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;static_cast<void>(sketch.add_circle(0,0,2));
        auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.type=type;
        feature.combine_mode=document::CombineMode::Add;feature.placement.z=support_z+3;
        document::ExtrusionParameters::EndTarget selected;selected.reference=target;selected.kind=document::EndTargetKind::Face;
        if(type==document::FeatureType::Projection)feature.feature.projection.targets={selected};else feature.feature.wrapping.target=selected;
        const auto first=f.doc().find_occurrence(f.first)->calculated_source,second=f.doc().find_occurrence(f.second)->calculated_source;
        workspace::commit_assembly_profile(f.live,engine,f.owner,feature,{},workspace::ProfileEditMode::Create,sketch);
        const auto* stored=f.doc().find_cut(feature.id);require(stored,"Assembly reference feature was not committed");
        const auto& result=stored->reference_result.get();require(result.calculation_errors.empty()&&!result.mesh.original_references.edges.empty(),"Assembly curve calculation produced no exact edges");
        std::size_t exact_count{};
        for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id) {
            ++exact_count;require(edge.exact_spline.has_value(),"Assembly curve result only stores display chords");
            for(double t:{0.,.23,.57,1.})near(kernel::bspline_value(*edge.exact_spline,t).z,support_z);
        }
        require(exact_count>0,"Reference curve feature owns no exact output curves");
        require(!stored->preview_wire.empty(),"Reference curve has no authored selection preview");
        require(first.shares_with(f.doc().find_occurrence(f.first)->calculated_source)&&second.shares_with(f.doc().find_occurrence(f.second)->calculated_source),"Reference curve replaced component material");
        const auto revision=f.state().session.revision();
        workspace::commit_assembly_profile(f.live,engine,f.owner,stored->definition,{},workspace::ProfileEditMode::Replace);
        require(revision==f.state().session.revision(),"Unchanged reference curve committed a transaction");created.push_back(feature.id);
        const auto packet=document::serialize_body_result(result);f.run("undo");require(!f.doc().find_cut(feature.id),"Reference curve Undo retained the feature");f.run("redo");
        require(document::serialize_body_result(f.doc().find_cut(feature.id)->reference_result.get())==packet,"Reference curve Redo changed exact geometry");
        f.run("save");const auto reopened=assembly::AssemblyDocument::load(directory/"reference-curves.asmz");
        require(document::serialize_body_result(reopened.find_cut(feature.id)->reference_result.get())==packet,"Native Assembly lost reference curve topology");
        const auto scene=reopened.build_scene();require(std::ranges::any_of(scene.original_references.edges,[&](const auto& edge){return edge.reference.owner_id==feature.id&&edge.exact_spline.has_value();}),"Reopened Assembly did not expose exact curves");
        f.run("regenerate");require(f.doc().find_cut(feature.id)->reference_result->calculation_errors.empty(),"Reference curve regeneration failed");
    }
    // A radial wrap uses a positioned component's analytic cylinder, not
    // its tessellation. Changing the component pose invalidates the result.
    f.run("new",{{"type","part"},{"name","curve-cylinder"}});const auto cylinder_part=f.live.active_document_id();
    const auto cylinder=test::circular_commands([&](const char* n,commands::Json args){return f.run(n,std::move(args));},{{"radius_mm",10.},{"height_mm",20.}}).at("container").get<std::string>();f.run("save");
    f.live.activate(f.owner);f.live.display_top_level(f.owner);const auto instance=f.run("component.insert",{{"source",cylinder_part}}).at("occurrence").get<std::string>();
    {auto next=f.doc();next.find_occurrence(instance)->placement={30,15,7,0,0,0};f.state().session.commit(std::move(next));}f.run("regenerate");
    kernel::FaceReference cylinder_face;
    for(const auto& face:f.doc().build_drawing_scene().triangle_references) {
        const auto surface=face.surface?face.surface:face.contour_surface;
        if(face.instance_path==assembly::InstancePath{}.child(instance).encoded()&&face.owner_id==cylinder&&surface&&surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){cylinder_face=face;break;}
    }
    require(cylinder_face.valid(),"Positioned exact cylinder was not exposed");
    auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;static_cast<void>(sketch.add_circle(0,0,1));
    auto wrap=document::PartDocument::create_feature_container(sketch.id);wrap.feature.type=document::FeatureType::Wrapping;wrap.placement.x=40;wrap.placement.y=15;wrap.placement.z=12;wrap.placement.rotation_y=90;
    document::ExtrusionParameters::EndTarget target_cylinder;target_cylinder.reference=cylinder_face;target_cylinder.kind=document::EndTargetKind::Face;wrap.feature.wrapping.target=target_cylinder;
    workspace::commit_assembly_profile(f.live,engine,f.owner,wrap,{},workspace::ProfileEditMode::Create,sketch);
    const auto check_cylinder=[&](double center_x) {
        std::size_t samples{};for(const auto& edge:f.doc().find_cut(wrap.id)->reference_result->mesh.original_references.edges)if(edge.reference.owner_id==wrap.id&&edge.exact_spline)
            for(double t:{0.,.13,.37,.67,1.}) {const auto p=kernel::bspline_value(*edge.exact_spline,t);require(std::abs(std::hypot(p.x-center_x,p.y-15)-10.)<.001,"Wrapped exact curve left the analytic cylinder");++samples;}
        require(samples>0,"Cylindrical Assembly wrap has no exact curves");
    };check_cylinder(30);
    const auto& support=f.doc().find_cut(wrap.id)->reference_result->mesh.original_references.edges.front();
    auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
    point.origin=kernel::bspline_value(*support.exact_spline,.37);
    point.references={{{},support.reference.owner_id,support.reference.semantic_key}};
    {auto next=f.doc();next.constructions.push_back(point);next.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Construction,point.id);next.resolve_constructions();f.state().session.commit(std::move(next));}
    require(f.doc().find_construction(point.id)->reference_valid,"Point cannot attach to an Assembly result curve");
    const auto saved_curve=document::serialize_body_result(f.doc().find_cut(wrap.id)->reference_result.get());
    {auto next=f.doc();next.find_occurrence(instance)->placement.x=31;f.state().session.commit(std::move(next));}
    require(document::serialize_body_result(f.doc().find_cut(wrap.id)->reference_result.get())==saved_curve,"Component movement implicitly regenerated an Assembly operation");
    f.run("regenerate");check_cylinder(31);
    const auto* moved_point=f.doc().find_construction(point.id);
    require(moved_point->reference_valid&&std::abs(std::hypot(moved_point->origin.x-31,moved_point->origin.y-15)-10)<.001,"Dependent point remained on the previous wrapped curve");
    const auto good=f.doc().find_cut(wrap.id)->reference_result;
    auto broken=f.doc();broken.find_occurrence(instance)->suppressed=true;
    workspace::calculate_resolved_assembly_cuts(engine,broken,false,&f.live,true);
    require(!broken.find_cut(wrap.id)->reference_result->calculation_errors.empty(),"Missing target did not mark the reference curve broken");
    require(!broken.find_construction(point.id)->reference_valid&&broken.find_construction(point.id)->origin==moved_point->origin,"Broken curve did not retain and invalidate its dependent point");
    require(document::serialize_viewer_reference_geometry(broken.find_cut(wrap.id)->reference_result->mesh.original_references)==document::serialize_viewer_reference_geometry(good->mesh.original_references),"Missing target moved the last valid curves");
    f.run("save");
    require(f.live.open_part(f.source)->session.document().serialized()==source_before,"Assembly reference authoring changed the source Part");
    if(argc>1)f.doc().save(fs::absolute(argv[1])/"reference-curves.asmz");
    std::cout<<"Assembly Projection/Wrap exact curves, unchanged materials, native persistence, no-op OK, Undo/Redo and regeneration passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
