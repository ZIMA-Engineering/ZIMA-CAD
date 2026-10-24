#include "profile_solid_fixture.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <set>
#include <tuple>
#include <iostream>
#include <cmath>
#include <functional>
using namespace zima;
namespace {
using Identity=std::tuple<int,std::string,std::string,std::string>;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
auto identities(const kernel::BodyResult& body) {
    std::set<Identity> result;
    const auto add=[&](int kind,const auto& bindings){for(const auto& item:bindings)
        result.emplace(kind,item.reference.owner_id,item.reference.semantic_key,item.reference.instance_path);};
    std::set<const kernel::BodyResult*> visited;
    std::function<void(const kernel::BodyResult&)> walk=[&](const auto& value){
        if(!visited.insert(&value).second)return;
        add(0,value.kernel_faces);add(1,value.kernel_edges);add(2,value.kernel_vertices);
        for(const auto& [id,snapshot]:value.body_outputs)walk(snapshot.get());
    };walk(body);return result;
}
}
int main(){try{
    auto part=document::PartDocument::create_default();
    const auto block=test::rectangular_feature(part,{100,80,50});
    const auto edge=[&](const char* key){return kernel::EdgeReference{block.id,test::profile_key(part,block,key),{}};};
    auto first=document::PartDocument::create_fillet_container({edge("edge:x_max:y_min:z_max--x_max:y_min:z_min"),edge("edge:x_min:y_min:z_max--x_min:y_min:z_min")});
    first.edge_treatment.primary_size=5;
    auto second=document::PartDocument::create_fillet_container({edge("edge:x_min:y_min:z_max--x_max:y_min:z_max")});second.edge_treatment.primary_size=3;
    auto third=document::PartDocument::create_fillet_container({edge("edge:x_min:y_min:z_min--x_max:y_min:z_min")});third.edge_treatment.primary_size=2;
    auto shell=document::PartDocument::create_shell_container({{block.id,test::profile_key(part,block,"z_max"),{}}});shell.shell.thickness=1;
    part.history={block,first,second,third,shell};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Rounded stock"));
    for(const auto& feature:part.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(std::move(graph));
    part.resolve_constructions();part.synchronize_dimension_identifiers();
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-shell-identity-"+part.document_id);std::filesystem::create_directory(directory);
    std::set<Identity> expected;double volume{},area{};
    for(unsigned repeat=0;repeat<4;++repeat) {
        kernel::OcctKernel engine;auto result=engine.evaluate_history(part.kernel_operations());
        check(!result.empty()&&result.back().calculation_errors.empty(),"Rounded stock Shell fixture cannot calculate");
        engine.ensure_body_properties(result.back());
        const auto current=identities(result.back());check(!current.empty(),"Shell has no persisted topology bindings");
        if(repeat==0){expected=current;volume=result.back().volume;area=result.back().surface_area;}
        else check(current==expected&&std::abs(result.back().volume-volume)<1e-7&&std::abs(result.back().surface_area-area)<1e-7,
            "Unchanged Shell regeneration changed topology identity or geometry");
        const auto file=directory/("roundtrip-"+std::to_string(repeat)+".prtz");part.save(file,result);
        std::vector<kernel::BodyResult> restored;auto reopened=document::PartDocument::load(file,&restored);
        check(identities(restored.back())==current,"Native Shell readback lost topology identity");part=std::move(reopened);
    }
    unsigned ambiguous{};for(const auto& [kind,owner,key,path]:expected)
        if(kind==0&&owner==shell.id&&key.find("transition-surface-kind:")!=std::string::npos)++ambiguous;
    std::cout<<"Shell topology bindings="<<expected.size()<<" transition discriminators="<<ambiguous<<'\n';
    check(directory.parent_path()==parent,"Unsafe test cleanup");std::filesystem::remove_all(directory);
    std::cout<<"Rounded stock Shell: four independent calculations preserve all face/edge/vertex identities and physical geometry\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
