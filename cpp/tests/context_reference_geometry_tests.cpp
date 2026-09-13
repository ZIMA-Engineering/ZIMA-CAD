#include <zima/workspace/reference_sources.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <cmath>
using namespace zima;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-8)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void near(kernel::Vec3 actual,kernel::Vec3 expected){near(actual.x,expected.x);near(actual.y,expected.y);near(actual.z,expected.z);}
void verify(const fs::path& dir) {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto source=document::PartDocument::create_default();const auto source_id=source.document_id;
    auto box=document::PartDocument::create_box_container();box.box={10,10,10};source.history={box};
    auto boundaries=kernel.evaluate_history(source.kernel_operations());
    kernel::ViewerEdge curve;curve.reference={box.id,"original-test-quarter-circle",""};curve.points={{1,0,0},{0,1,0}};
    curve.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
    boundaries.back().mesh.original_references.edges.push_back(curve);
    const auto face=boundaries.back().mesh.original_references.triangle_references.front();
    require(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane,"Box fixture has no persisted analytic plane");
    source.save(dir/"reference-source.prtz",boundaries);live.add_part(source,boundaries,dir/"reference-source.prtz");
    auto dependent=document::PartDocument::create_default();dependent.history={document::PartDocument::create_box_container()};
    const auto dependent_id=dependent.document_id;auto dependent_bodies=kernel.evaluate_history(dependent.kernel_operations());
    live.add_part(dependent,dependent_bodies,dir/"reference-dependent.prtz");
    auto inner=assembly::AssemblyDocument::create_default();
    auto source_item=assembly::AssemblyDocument::create_part_occurrence("Source",source_id,dir/"reference-source.prtz",boundaries.back());
    auto dependent_item=assembly::AssemblyDocument::create_part_occurrence("Dependent",dependent_id,dir/"reference-dependent.prtz",dependent_bodies.back());
    source_item.placement={10,20,0,0,0,90};dependent_item.placement={2,3,0,0,0,0};
    inner.components={source_item,dependent_item};
    // This unfinished cutter is deliberately noncalculable. Reference reading
    // must use original persisted data even while an Assembly operation fails.
    auto empty_profile=sketcher::Sketch::create_default();
    assembly::AssemblyCut broken_cut;broken_cut.definition=document::PartDocument::create_extrusion_container(empty_profile.id);
    broken_cut.definition.combine_mode=document::CombineMode::Subtract;empty_profile.owner_container_id=broken_cut.definition.id;inner.sketches={empty_profile};
    broken_cut.target_occurrence_ids={source_item.occurrence_id};inner.cuts={broken_cut};
    live.add_assembly(inner,dir/"reference-inner.asmz");
    auto top=assembly::AssemblyDocument::create_default();
    auto group=assembly::AssemblyDocument::create_assembly_occurrence("Group",inner.document_id,dir/"reference-inner.asmz",inner);
    group.placement={30,40,50,10,20,30};top.components={group};live.add_assembly(top,dir/"reference-top.asmz");
    const auto from=assembly::InstancePath{{group.occurrence_id,source_item.occurrence_id}};
    const auto to=assembly::InstancePath{{group.occurrence_id,dependent_item.occurrence_id}};
    const auto revision=live.open_assembly(top.document_id)->session.revision(),generation=live.open_assembly(top.document_id)->session.data_generation();
    const auto snapshot=live.open_assembly(top.document_id)->session.document().components.front().calculated_source;
    bool calculation_rejected=false;
    try{static_cast<void>(live.prepare_assembly_calculation(top.document_id));}catch(const std::exception&){calculation_rejected=true;}
    require(calculation_rejected,"Broken-cut fixture would not detect hidden Assembly calculation");
    const auto read=[&]{return live.authoritative_external_reference_geometry(top.document_id,to,source_id);};
    auto geometry=read();
    const auto find_curve=[&](const auto& data)->const kernel::ViewerEdge& {
        const auto found=std::ranges::find_if(data.edges,[&](const auto& e){return e.reference.owner_id==box.id&&e.reference.semantic_key==curve.reference.semantic_key&&e.reference.instance_path==from.encoded();});
        require(found!=data.edges.end()&&found->exact_spline.has_value(),"Exact source curve was lost in nested reference conversion");return *found;
    };
    const auto& transformed=find_curve(geometry);near(transformed.points.front(),{8,18,0});near(transformed.points.back(),{7,17,0});
    for(unsigned i=0;i<=256;++i){const auto p=kernel::bspline_value(*transformed.exact_spline,i/256.);near((p.x-8)*(p.x-8)+(p.y-17)*(p.y-17),1);near(p.z,0);}
    const auto actual_face=std::ranges::find_if(geometry.triangle_references,[&](const auto& r){return r.owner_id==face.owner_id&&r.semantic_key==face.semantic_key;});
    require(actual_face!=geometry.triangle_references.end()&&actual_face->surface,"Context reference lost its analytic plane");
    const auto p=face.surface->origin,n=face.surface->axis;
    near(actual_face->surface->origin,{8-p.y,17+p.x,p.z});near(actual_face->surface->axis,{-n.y,n.x,n.z});
    const auto only_curve=workspace::context_original_reference_geometry(live,top.document_id,to,source_id,
        [&](auto kind,const auto& owner,const auto& key,const auto& path){return kind==workspace::OriginalReferenceKind::Edge&&owner==box.id&&key==curve.reference.semantic_key&&path==from.encoded();});
    require(only_curve.edges.size()==1&&only_curve.vertices.empty()&&only_curve.axes.empty()&&only_curve.points.empty(),"Single-edge query copied unrelated reference geometry");
    auto changed=live.open_part(source_id)->session.calculated_boundaries();
    auto& changed_curve=changed.back().mesh.original_references.edges.back();for(auto& q:changed_curve.points)q.x+=.01;for(auto& q:changed_curve.exact_spline->poles)q.x+=.01;
    live.open_part(source_id)->session.commit(source,std::move(changed));geometry=read();near(find_curve(geometry).exact_spline->poles.front(),{8,18.01,0});
    require(live.open_assembly(top.document_id)->session.revision()==revision&&live.open_assembly(top.document_id)->session.data_generation()==generation&&
        live.open_assembly(top.document_id)->session.document().components.front().calculated_source.shares_with(snapshot),
        "Reading current source references changed the parent Assembly cache or history");
    // A closed source is read from its native document without opening tabs or
    // calculating the broken cutter in an intermediate Assembly.
    static_cast<void>(live.remove(source_id));const auto count=live.size();geometry=read();near(find_curve(geometry).exact_spline->poles.front(),{8,18,0});
    require(live.size()==count&&!live.open_part(source_id),"Reading a closed reference source changed open documents");
    // A reflected occurrence must consume its calculated copy, never the
    // unreflected source document. The source is deliberately closed here.
    auto with_copy=live.open_assembly(top.document_id)->session.document();
    auto original=assembly::AssemblyDocument::create_part_occurrence("Original",source_id,dir/"reference-source.prtz",boundaries.back());
    auto mirrored=assembly::AssemblyDocument::create_part_occurrence("Mirror",source_id,dir/"reference-source.prtz",
        kernel.mirror_body(boundaries.back(),{{},{1,0,0}},{},{},{}));
    mirrored.derived_copy=document::DerivedCopyParameters{};mirrored.derived_copy->source_id=original.occurrence_id;
    mirrored.placement={5,6,7,0,0,0};with_copy.components.push_back(original);with_copy.components.push_back(mirrored);
    live.open_assembly(top.document_id)->session.commit(std::move(with_copy));
    const auto mirror_path=assembly::InstancePath{}.child(mirrored.occurrence_id).encoded();
    geometry=read();
    const auto reflected=std::ranges::find_if(geometry.edges,[&](const auto& e){return e.reference.semantic_key==curve.reference.semantic_key&&e.reference.instance_path==mirror_path;});
    require(reflected!=geometry.edges.end()&&reflected->exact_spline.has_value(),"Reflected occurrence lost its exact reference curve");
    near(reflected->exact_spline->poles.front(),live.occurrence_point_from_scene(top.document_id,to,{4,6,7}));
    const auto origin=std::ranges::find_if(geometry.points,[&](const auto& p){return p.reference.owner_id==source_id+":origin"&&p.reference.semantic_key=="origin:point"&&p.reference.instance_path==mirror_path;});
    require(origin!=geometry.points.end(),"Reflected Part origin was omitted from reference geometry");
    near(origin->position,live.occurrence_point_from_scene(top.document_id,to,{5,6,7}));
    const auto unrelated=document::PartDocument::create_default().document_id;
    require(live.authoritative_external_reference_geometry(top.document_id,to,unrelated).edges.empty(),"Unavailable source was guessed from another Part");
    auto wrong=document::PartDocument::create_default();wrong.save(dir/"reference-source.prtz",{});bool identity_rejected=false;
    try{static_cast<void>(read());}catch(const workspace::ReferenceQueryError& e){identity_rejected=std::string(e.code)=="dependency_identity";}
    require(identity_rejected,"Native reference source identity mismatch was accepted");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-context-geometry-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Nested reference frames, exact curves and planes, authoritative sources and no implicit Assembly calculation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
