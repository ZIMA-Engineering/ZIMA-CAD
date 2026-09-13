#include "context_reference_test_support.hpp"
#include "sweep_test_support.hpp"
#include <zima/document/feature_sketches.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <zima/command_host/host.hpp>
#include <nlohmann/json.hpp>
using namespace zima;namespace fs=std::filesystem;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-7)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
const std::array kinds{document::FeatureKind::Sweep2D,document::FeatureKind::Sweep3D,document::FeatureKind::HelicalSweep,document::FeatureKind::Hole,document::FeatureKind::Thread};
document::HistoryContainer feature_with_references(document::FeatureKind kind,const sketcher::Sketch& payload){
    auto feature=kind==document::FeatureKind::Hole?document::PartDocument::create_hole_container():kind==document::FeatureKind::Thread?
        document::PartDocument::create_thread_container():test_support::sweep_fixture(kind);
    if(kind==document::FeatureKind::Hole||kind==document::FeatureKind::Thread){
        feature.hole.sketch_serialized=payload.serialized();feature.hole.chamfer_sketch_serialized=payload.serialized();feature.hole.tip_sketch_serialized=payload.serialized();
    }
    document::visit_feature_sketches(feature,[&](auto& data,std::size_t){
        auto sketch=payload;sketch.id=kernel::make_stable_id();sketch.owner_container_id=feature.id;data=sketch.serialized();
    });
    return feature;
}
std::size_t expected_profiles(document::FeatureKind kind){return kind==document::FeatureKind::Sweep2D?2:kind==document::FeatureKind::Sweep3D?1:3;}
sketcher::Sketch payload(const test_support::ContextReferenceFixture& fixture){
    auto sketch=fixture.target.sketches.front();
    static_cast<void>(sketch.add_offset(fixture.curve_id,.1,true));
    static_cast<void>(sketch.retain_curve_intervals(fixture.curve_id,{{.1,.9}}));return sketch;
}
void check_curve(const sketcher::Sketch& sketch,const test_support::ContextReferenceFixture& fixture,const sketcher::Sketch& expected,double x,double y){
    require(sketch.curve_trims==expected.curve_trims,"Refresh changed retained interval identities");
    require(sketch.offsets==expected.offsets,"Refresh changed offset identity, source or parameters");
    for(unsigned index=0;index<=256;++index){
        const auto p=kernel::bspline_value(sketch.supporting_curve(fixture.curve_id),index/256.);
        near((p.x-x)*(p.x-x)+(p.y-y)*(p.y-y),1);
    }
    const auto ref=std::ranges::find(sketch.external_references,fixture.reference_id,&sketcher::SketchExternalReference::id);
    require(ref!=sketch.external_references.end()&&!ref->broken,"Original rational edge became unavailable");
}
template<class Function>void each_owned(const document::HistoryContainer& feature,Function action){
    document::visit_feature_sketches(feature,[&](const auto& data,std::size_t){action(sketcher::Sketch::from_serialized(data));});
}
void verify(const fs::path& dir){
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture fixture(kernel,dir);
    auto moved=fixture.calculated;auto& edge=moved.back().mesh.original_references.edges.back();
    for(auto& p:edge.points)p.x+=.01;for(auto& p:edge.exact_spline->poles)p.x+=.01;
    const auto expected=payload(fixture);
    for(const auto kind:kinds){
        workspace::Workspace live;fixture.load(live);live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved);
        auto unrelated=test_support::sweep_fixture(document::FeatureKind::Sweep2D);
        document::visit_feature_sketches(unrelated,[](auto& data,std::size_t){data=nlohmann::json::parse(data).dump(2);});
        auto target=fixture.target;target.sketches.clear();target.history={feature_with_references(kind,expected),unrelated};
        const auto unchanged=live.open_assembly(fixture.top.document_id)->session.data_generation();
        require(live.refresh_context_external_references(target),"Owned context references were skipped for feature kind "+std::to_string(static_cast<int>(kind)));
        std::size_t count{};each_owned(target.history.front(),[&](const auto& sketch){check_curve(sketch,fixture,expected,8,17.01);++count;});
        require(count==expected_profiles(kind),"Fixture did not cover every owned Sketch slot");
        require(!live.refresh_context_external_references(target),"Equal context refresh changed serialized profiles");
        require(target.history.back()==unrelated,"Context refresh rewrote an unrelated owned profile");
        require(live.open_assembly(fixture.top.document_id)->session.data_generation()==unchanged,"Reference refresh regenerated the Assembly");
        auto absent=moved;absent.back().mesh.original_references.edges.pop_back();live.open_part(fixture.source.document_id)->session.commit(fixture.source,absent);
        std::vector<std::vector<sketcher::SketchPoint>> points;each_owned(target.history.front(),[&](const auto& sketch){points.push_back(sketch.points);});
        require(live.refresh_context_external_references(target),"Missing source was not reported");count=0;
        each_owned(target.history.front(),[&](const auto& sketch){
            const auto ref=std::ranges::find(sketch.external_references,fixture.reference_id,&sketcher::SketchExternalReference::id);
            require(ref!=sketch.external_references.end()&&ref->broken&&sketch.points==points.at(count++),"Lost source erased the retained native curve");
        });
        live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved);require(live.refresh_context_external_references(target),"Restored source did not repair owned references");
        each_owned(target.history.front(),[&](const auto& sketch){check_curve(sketch,fixture,expected,8,17.01);});

        live.open_part(target.document_id)->session.commit(target,{});
        live.synchronize_external_sketch_dependencies();
        require(live.open_assembly(fixture.inner.document_id)->session.document().dependencies==fixture.inner.dependencies,
            "Dependency synchronization dropped references inside owned profiles");
        auto current_directory=dir;command_host::Host host(live,kernel,current_directory);
        require(live.activate_occurrence(fixture.top.document_id,fixture.target_path).has_value(),"Cannot activate owned profile target");
        std::string owned_sketch;each_owned(target.history.front(),[&](const auto& sketch){if(owned_sketch.empty())owned_sketch=sketch.id;});
        auto moved_again=moved;auto& changed_edge=moved_again.back().mesh.original_references.edges.back();
        for(auto& point:changed_edge.points)point.x+=.01;for(auto& point:changed_edge.exact_spline->poles)point.x+=.01;
        live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved_again);
        const auto refreshed=host.execute({{"command","sketch.reference.refresh"},{"arguments",{{"sketch",owned_sketch}}}});
        require(refreshed.ok,"CLI could not refresh an owned profile");
        require(host.execute({{"command","undo"}}).ok,"Cannot undo owned reference refresh");
        require(live.open_assembly(fixture.inner.document_id)->session.document().dependencies==fixture.inner.dependencies,
            "Owned reference Undo lost its Assembly dependency");

        auto local_payload=expected;std::erase_if(local_payload.external_references,[](const auto& ref){return ref.kind!=sketcher::ExternalReferenceKind::Edge;});
        auto& reference=local_payload.external_references.front();reference.source_document_id=target.document_id;
        reference.context_assembly_document_id.clear();reference.context_instance_path.clear();reference.source_instance_path.clear();
        target.history={fixture.source.history.front(),feature_with_references(kind,local_payload)};
        require(workspace::refresh_sketch_external_references(target,moved),"Local reference refresh skipped owned profiles");
        each_owned(target.history.back(),[&](const auto& sketch){check_curve(sketch,fixture,expected,.01,0);});
        require(!workspace::refresh_sketch_external_references(target,moved),"Equal local refresh changed owned profiles");


    }
    workspace::Workspace live;fixture.load(live);live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved);
    auto target=fixture.target;target.history.clear();target.sketches.clear();
    document::SectionDefinition section;section.id=kernel::make_stable_id();section.sketch=expected;
    section.sketch.id=kernel::make_stable_id();section.sketch.owner_container_id=section.id;
    section.container_origin=document::PartDocument::create_sketch_container().container_origin;target.sections={section};
    require(live.refresh_context_external_references(target),"Context section references were skipped");
    check_curve(target.sections.front().sketch,fixture,expected,8,17.01);
    auto local=expected;std::erase_if(local.external_references,[](const auto& ref){return ref.kind!=sketcher::ExternalReferenceKind::Edge;});
    auto& reference=local.external_references.front();reference.source_document_id=target.document_id;
    reference.context_assembly_document_id.clear();reference.context_instance_path.clear();reference.source_instance_path.clear();
    target.sections.front().sketch=local;target.sections.front().sketch.owner_container_id=section.id;
    target.history=fixture.source.history;
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Source Body"));
    graph.insert({document::PartHistoryKind::Feature,target.history.front().id});target.set_body_history(std::move(graph));
    require(workspace::refresh_sketch_external_references(target,moved),"Section in a multibody Part lost local source eligibility");
    check_curve(target.sections.front().sketch,fixture,expected,.01,0);
    auto owner=fixture.inner;owner.cuts.clear();owner.sketches.clear();owner.components.front().calculated_source=moved.back();
    auto assembly_sketch=local;auto& source=assembly_sketch.external_references.front();source.source_document_id=fixture.source.document_id;
    source.source_instance_path=assembly::InstancePath{{fixture.inner.components.front().occurrence_id}}.encoded();
    assembly::AssemblyCut cut;cut.definition=document::PartDocument::create_extrusion_container(assembly_sketch.id);
    cut.definition.combine_mode=document::CombineMode::Subtract;assembly_sketch.owner_container_id=cut.definition.id;
    owner.sketches={assembly_sketch};owner.cuts={cut};section.sketch=assembly_sketch;section.sketch.id=kernel::make_stable_id();
    section.sketch.owner_container_id=section.id;owner.sections={section};
    require(workspace::refresh_assembly_sketch_external_references(owner),"Assembly section refresh was skipped");
    check_curve(owner.sketches.front(),fixture,expected,10,20.01);
    check_curve(owner.sections.front().sketch,fixture,expected,10,20.01);
    require(!workspace::refresh_assembly_sketch_external_references(owner),"Equal Assembly section refresh changed data");
    std::size_t count{};workspace::visit_document_sketches(owner,[&](const auto&){++count;return true;});
    require(count==2,"Read-only Assembly traversal omitted or duplicated cut/section Sketches");
    target.history={feature_with_references(document::FeatureKind::HelicalSweep,expected)};target.sections.clear();
    target.history.front().helical.sketches[1]="invalid unused profile";count=0;
    workspace::visit_document_sketches(target,[&](const auto&){++count;return false;});
    require(count==1,"Read-only candidate traversal did not stop before unrelated profiles");

}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-owned-reference-refresh-"+kernel::make_stable_id());
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Owned profile reference refresh preserves rational geometry, trims, offsets and broken-reference recovery\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
