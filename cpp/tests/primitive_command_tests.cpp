#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <tuple>

using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Volume expected "+std::to_string(expected)+", got "+std::to_string(actual));}
auto faces(const kernel::ViewerMesh& mesh){
    std::set<std::tuple<std::string,std::string,std::string>> result;
    for(const auto& face:mesh.original_references.triangle_references)result.emplace(face.owner_id,face.semantic_key,face.instance_path);
    return result;
}
struct Case {const char* name;Json dimensions;const char* resized;double volume;double factor;};
void verify(const kernel::OcctKernel& kernel,fs::path directory){
    const double pi=std::numbers::pi;
    const std::vector<Case> cases{
        {"cylinder",{{"radius_mm","3"},{"height_mm","6"}},"height",54*pi,2},
        {"sphere",{{"radius_mm","3"}},"radius",36*pi,8},
        {"cone",{{"bottom_radius_mm","4"},{"top_radius_mm","1"},{"height_mm","6"}},"height",42*pi,2},
        {"pyramid",{{"length_mm","10"},{"width_mm","8"},{"height_mm","6"}},"height",160,2},
        {"wedge",{{"length_mm","10"},{"width_mm","8"},{"height_mm","6"},{"top_offset_mm","2"}},"height",288,2}};
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    for(const auto& test:cases){
        const std::string name=test.name;
        run(host,"new",{{"type","part"},{"name",name}});
        auto* state=live.open_part(live.active_document_id());
        const auto created=run(host,name+".create",test.dimensions).data;
        const auto id=created.at("container").get<std::string>();
        const auto original=*state->session.document().find_container(id);
        const auto* definition=workspace::primitive_definition(original.feature_kind);
        require(definition && definition->command_name==name,"Factory created the wrong feature kind");
        near(state->session.calculated_boundaries().back().volume,test.volume);
        const auto ids=faces(state->session.calculated_boundaries().back().mesh);require(!ids.empty(),"Missing primitive source faces");
        const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
        const auto queried=run(host,name+".get",{{"container",id}}).data;
        require(queried.at("feature")==original.feature_id && state->session.revision()==revision && state->session.calculated_boundaries().data()==cache,"Parameter query changed model");
        const auto field=std::string(test.resized)+"_mm";
        const auto old_value=queried.at(field).get<double>();
        const auto unchanged=run(host,name+".set",{{"container",id},{field,std::to_string(old_value)}});
        require(unchanged.data.at("changed")==false && state->session.calculated_boundaries().data()==cache,"Unchanged primitive recalculated");
        for(const auto* invalid:{"-1","nan","inf","1000001"}){
            const auto result=host.execute({{"command",name+".set"},{"arguments",{{"container",id},{field,invalid}}}});
            require(!result.ok && state->session.revision()==revision && state->session.calculated_boundaries().data()==cache,"Invalid dimension partly committed");
        }
        require(host.execute_text("box.get "+id).code=="wrong_feature","Wrong primitive query accepted");
        require(host.execute_text("box.set "+id+" 10").code=="wrong_feature","Wrong primitive patch accepted");
        run(host,name+".set",{{"container",id},{field,std::to_string(old_value*2)}});
        near(state->session.calculated_boundaries().back().volume,test.volume*test.factor);
        require(faces(state->session.calculated_boundaries().back().mesh)==ids,"Primitive resize replaced source face identities");
        run(host,"undo");near(state->session.calculated_boundaries().back().volume,test.volume);
        run(host,"redo");near(state->session.calculated_boundaries().back().volume,test.volume*test.factor);
        auto locked=*state->session.document().find_container(id);locked.value_locks.insert(test.resized);
        static_cast<void>(workspace::commit_primitive(live,kernel,live.active_document_id(),locked,workspace::PrimitiveEditMode::Replace));
        require(host.execute({{"command",name+".set"},{"arguments",{{"container",id},{field,"21"}}}}).code=="value_locked","Primitive lock ignored");
        run(host,"save");std::vector<kernel::BodyResult> loaded;
        const auto persisted=document::PartDocument::load(directory/(name+".prtz"),&loaded);
        require(*persisted.find_container(id)==locked && faces(loaded.back().mesh)==ids,"Primitive save/load lost parameters or identities");
        near(loaded.back().volume,test.volume*test.factor);
        if(name=="cone"){
            const auto before=state->session.revision();const auto* before_cache=state->session.calculated_boundaries().data();
            require(!host.execute({{"command","cone.set"},{"arguments",{{"container",id},{"top_radius_mm","4"}}}}).ok,"Degenerate equal-radius Cone accepted");
            require(state->session.revision()==before && state->session.calculated_boundaries().data()==before_cache,"Kernel failure mutated the live primitive");
            run(host,"cone.set",{{"container",id},{"top_radius_mm","0"}});near(state->session.calculated_boundaries().back().volume,64*pi);
        }
        if(name=="wedge"){
            const auto before=state->session.revision();
            require(host.execute({{"command","wedge.set"},{"arguments",{{"container",id},{"length_mm","1"}}}}).code=="invalid_arguments" && state->session.revision()==before,"Wedge accepted top offset outside length");
            run(host,"wedge.set",{{"container",id},{"top_offset_mm","0"}});near(state->session.calculated_boundaries().back().volume,480);
            run(host,"wedge.set",{{"container",id},{"top_offset_mm","10"}});near(state->session.calculated_boundaries().back().volume,960);
        }
    }
}
}
int main(){try{
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-primitives-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Primitive commands: five additional solids, independent volumes, reference identities, locks, atomic errors and history passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
