#include "profile_solid_fixture.hpp"
#include <zima/workspace/model_calculation.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <iostream>

using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void verify(document::PartDocument part,std::vector<kernel::BodyResult> calculated,
        const kernel::OcctKernel& kernel) {
    workspace::Workspace live;const auto id=part.document_id;
    live.add_part(std::move(part),std::move(calculated));auto& session=live.open_part(id)->session;
    const auto revision=session.revision();const auto authored=session.document().serialized();
    const auto final_mesh=session.calculated_boundaries().back().mesh;
    const auto final_errors=session.calculated_boundaries().back().calculation_errors;
    unsigned holes{};
    for(const auto& feature:session.document().history) {
        std::cout<<"Feature "<<feature.name<<" kind="<<int(feature.feature_kind)<<'\n';
        if(feature.feature_kind!=document::FeatureKind::Hole&&feature.feature_kind!=document::FeatureKind::Thread)continue;
        ++holes;
        const auto report=[&](const char* phase){
            const auto input=session.rollback_boundary(feature.id);
            std::cout<<phase<<": input="<<(input&&input->input_body?input->input_body->mesh.triangles.size():0)<<'\n';
            const auto& result=session.calculated_boundaries().back();
            for(const auto& [body,rows]:result.body_boundaries)for(std::size_t i=0;i<rows.size();++i)
                std::cout<<"  row "<<i<<": triangles="<<rows[i].mesh.triangles.size()<<" shape="<<rows[i].kernel_shape.size()
                    <<" errors="<<rows[i].calculation_errors.size()<<" volume="<<rows[i].volume<<'\n';
        };
        report("Before opening");
        static_cast<void>(workspace::materialize_part_history_input_boundary(live,kernel,id,feature.id));
        report("After opening");
        const auto input=session.rollback_boundary(feature.id);
        require(input&&input->input_body&&!input->input_body->mesh.triangles.empty(),"Hole Properties lost its preceding material");
        auto graph=session.document().body_history;const auto boundary=graph.rollback_before(feature.id);
        graph.activate(boundary.body_id);graph.set_history_cursor(boundary.body_id,boundary.entry_count);
        require(!session.body_context_mesh(&graph).triangles.empty(),"Hole rollback context lost its preceding material");
        const auto calculations=kernel::body_calculation_count.load();
        require(!workspace::materialize_part_history_input_boundary(live,kernel,id,feature.id)&&
            kernel::body_calculation_count.load()==calculations,"Reopening Hole recalculated its cached input");
        require(session.calculated_boundaries().back().calculation_errors==final_errors&&
            session.calculated_boundaries().back().mesh.triangles==final_mesh.triangles&&
            session.calculated_boundaries().back().mesh.vertices==final_mesh.vertices,
            "Opening Hole changed final geometry or suppressed its calculation errors");
        require(session.revision()==revision&&session.document().serialized()==authored&&!session.can_undo(),"Opening Hole changed its definition/Undo");
    }
    require(holes!=0,"Fixture has no Hole; the reported state is not present in this saved document");
}
}
int main(int argc,char** argv){try{
    kernel::OcctKernel kernel;
    if(argc>1){std::vector<kernel::BodyResult> calculated;auto part=document::PartDocument::load(argv[1],&calculated);
        verify(std::move(part),std::move(calculated),kernel);return 0;}
    for(const bool threaded:{false,true}) {
    auto part=document::PartDocument::create_default();auto first=test::rectangular_feature(part,{20,20,20});
    auto broken=document::PartDocument::create_extrusion_container("missing-sketch");
    auto hole=threaded?document::PartDocument::create_thread_container():document::PartDocument::create_hole_container();hole.hole.diameter=2;hole.hole.bore_length=5;
    part.history={first,broken,hole};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Stock"));
    for(const auto& f:part.history)graph.insert({document::PartHistoryKind::Feature,f.id});part.set_body_history(graph);
    auto calculated=workspace::calculate_part(kernel,part);verify(part,calculated,kernel);
    const auto path=std::filesystem::temp_directory_path()/(part.document_id+".prtz");
    part.save(path,calculated);calculated.clear();part=document::PartDocument::load(path,&calculated);std::filesystem::remove(path);
    verify(std::move(part),std::move(calculated),kernel);
    }
    {
        auto part=document::PartDocument::create_default();
        auto broken=document::PartDocument::create_extrusion_container("missing-sketch");
        auto hole=document::PartDocument::create_thread_container();part.history={broken,hole};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Empty input"));
        for(const auto& f:part.history)graph.insert({document::PartHistoryKind::Feature,f.id});part.set_body_history(graph);
        workspace::Workspace live;const auto id=part.document_id;
        live.add_part(part,workspace::calculate_part(kernel,part));
        const auto calculations=kernel::body_calculation_count.load();
        require(!workspace::materialize_part_history_input_boundary(live,kernel,id,hole.id)&&
            kernel::body_calculation_count.load()==calculations,
            "Opening a Hole with intentionally empty failed input recalculated or threw");
    }
    std::cout<<"Hole rollback after invalid predecessor passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
