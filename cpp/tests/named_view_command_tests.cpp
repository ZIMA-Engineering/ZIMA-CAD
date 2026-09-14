#include <zima/command_host/host.hpp>
#include <zima/workspace/named_view_operations.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
template<class Fn> void rejects(Fn fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}require(failed,"Invalid named view accepted");}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;bool editing=false;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction result;result.editing=editing;result.active_occurrence=live.active_occurrence_path();return result;};
    command_host::Host host(live,kernel,dir,options);
    const auto run=[&](const char* name,Json args=Json::object()){
        const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
        if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);
        return result.data;
    };
    std::string source,owner;
    const Json camera{{"rotation",{2,0,0,2}},{"zoom",3.25},{"pan_x",-17.5},{"pan_y",41.25},{"reference_scale",7.5}};
    for(const bool assembly:{false,true}) {
        const auto name=assembly?"named-assembly":"named-part";
        run("new",{{"type",assembly?"assembly":"part"},{"name",name}});
        const auto id=live.active_document_id();
        if(assembly){owner=id;run("component.insert",{{"source",source}});}
        else {source=id;run("box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});}
        const auto revision=[&]{return assembly?live.open_assembly(id)->session.revision():live.open_part(id)->session.revision();};
        const auto generation=[&]{return assembly?live.open_assembly(id)->session.data_generation():live.open_part(id)->session.data_generation();};
        const auto before_revision=revision(),before_generation=generation();
        const auto cached=live.open_part(source)->session.calculated_boundaries().back().kernel_shape;
        const auto source_revision=live.open_part(source)->session.revision();
        std::optional<assembly::AssemblyDocument> original;
        if(assembly)original=live.open_assembly(id)->session.document();
        const auto geometry=[&] {
            const auto& last=live.open_part(source)->session.calculated_boundaries().back();
            require(last.kernel_shape==cached && std::abs(last.volume-6000)<1e-7,"Named view recalculated or changed Part geometry");
            if(assembly) {
                require(live.open_part(source)->session.revision()==source_revision,"Named view edited source Part");
                const auto& before=original->components.front();const auto& after=live.open_assembly(id)->session.document().components.front();
                require(before.calculated_source.shares_with(after.calculated_source) && before.placement==after.placement &&
                    before.placement_references==after.placement_references,"Named view recalculated Assembly geometry or mates");
            }
        };
        require(run("view.named.list").at("views").empty(),"New template contains a custom view");
        require(revision()==before_revision && generation()==before_generation && !host.change(),"Read-only named view query mutated state");
        const auto saved=run("view.named.set",{{"name","Čtvrtotáčka"},{"camera",camera}});
        require(saved.at("changed")==true && saved.at("body_calculated")==false && revision()==before_revision+1 &&
            host.change() && host.change()->kind==command_host::ChangeKind::Metadata,"Named view did not commit exactly once as metadata");
        const auto view=workspace::named_view(live,id,"Čtvrtotáčka");
        // Quaternion (1/sqrt(2),0,0,1/sqrt(2)) rotates the X axis to +Y, independently of the codec.
        const double w=view.camera[0],x=view.camera[1],y=view.camera[2],z=view.camera[3];
        require(std::abs(1-2*(y*y+z*z))<1e-6 && std::abs(2*(x*y+w*z)-1)<1e-6 &&
            view.camera[4]==3.25F && view.camera[5]==-17.5F && view.camera[6]==41.25F && view.camera[7]==7.5F,
            "Saved quarter-turn orientation or camera scale/pan is incorrect");
        geometry();run("undo");require(workspace::named_views(live,id).empty(),"Named view Undo failed");
        run("redo");require(workspace::named_view(live,id,"Čtvrtotáčka")==view,"Named view Redo failed");
        const auto same_revision=revision(),same_generation=generation();
        require(run("view.named.set",{{"name","Čtvrtotáčka"},{"camera",camera}}).at("changed")==false &&
            revision()==same_revision && generation()==same_generation && !host.change(),"Repeated view save created a transaction");
        auto camera2=camera;camera2["zoom"]=9;run("view.named.set",{{"name","Second"},{"camera",camera2}});
        run("view.named.set",{{"name","Čtvrtotáčka"},{"camera",camera2}});
        const auto listed=run("view.named.list").at("views");
        require(listed.size()==2 && listed[0].at("name")=="Čtvrtotáčka" && listed[1].at("name")=="Second",
            "Replacing a named view changed its list position");
        const auto reject=[&](const char* command,Json args,const char* expected="invalid_arguments"){
            const auto r=revision(),g=generation();const auto before=workspace::named_views(live,id);
            const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
            if(result.ok || result.code!=expected)throw std::runtime_error(
                std::string(command)+" expected "+expected+", got "+result.code+": "+result.message);
            require(revision()==r && generation()==g && workspace::named_views(live,id)==before && !host.change(),
                "Failed view command changed document state");
        };
        reject("view.named.get",{{"name","missing"}},"named_view_not_found");
        reject("view.named.delete",{{"name","missing"}},"named_view_not_found");
        for(const auto* name:{""," leading","trailing ","bad\nname"})
            reject("view.named.set",{{"name",name},{"camera",camera}},std::string_view(name).empty()?"missing_argument":"invalid_arguments");
        for(const auto* field:{"rotation","zoom","pan_x","pan_y","reference_scale"}) {
            auto invalid=camera;invalid.erase(field);reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
            invalid=camera;invalid[field]="wrong";reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        }
        for(const auto* scale:{"zoom","reference_scale"}) {
            auto invalid=camera;invalid[scale]=0;reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
            invalid[scale]=-1;reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
            invalid[scale]=1e100;reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        }
        auto invalid=camera;invalid["rotation"]={0,0,0,0};reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        invalid=camera;invalid["rotation"]={1,0,0};reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        invalid=camera;invalid["name"]="nested";reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        invalid=camera;invalid["typo"]=42;reject("view.named.set",{{"name","Bad"},{"camera",invalid}});
        editing=true;reject("view.named.delete",{{"name","Second"}},"editing_in_progress");run("view.named.list");editing=false;
        const auto views=workspace::named_views(live,id);
        auto direct=views.front();direct.camera[5]=std::numeric_limits<float>::infinity();
        rejects([&]{workspace::set_named_view(live,id,direct);});
        direct=views.front();direct.camera[0]=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{workspace::set_named_view(live,id,direct);});
        require(workspace::named_views(live,id)==views,"Failed direct operation changed views");
        run("save");const auto path=dir/(std::string(name)+(assembly?".asmz":".prtz"));
        const auto persisted=assembly?assembly::AssemblyDocument::load(path).named_views:document::PartDocument::load(path).named_views;
        require(document::parse_named_views(persisted)==views,"Native reopen lost complete camera data");
        // A direct save rejects invalid camera metadata before writing a file.
        const auto bad_path=dir/(std::string(name)+"-invalid"+(assembly?".asmz":".prtz"));
        if(assembly){auto bad=live.open_assembly(id)->session.document();bad.named_views="{}";rejects([&]{bad.save(bad_path);});}
        else {auto bad=live.open_part(id)->session.document();bad.named_views="{}";rejects([&]{bad.save(bad_path);});}
        require(!fs::exists(bad_path),"Failed native validation left an output file");
        run("view.named.delete",{{"name","Second"}});
        require(workspace::named_views(live,id).size()==1,"Delete left a named view");
        run("undo");require(workspace::named_views(live,id)==views,"Named view deletion Undo failed");geometry();
    }
    run("new",{{"type","assembly"},{"name","named-parent"}});
    const auto parent=live.active_document_id();
    const std::string outer=run("component.insert",{{"source",owner}}).at("occurrence");
    const auto parent_revision=live.open_assembly(parent)->session.revision();
    rejects([&]{workspace::set_named_view(live,owner,{"Inactive",{1,0,0,0,1,0,0,1}});});
    require(!run("view.named.list",{{"document",owner}}).at("views").empty(),"Query of inactive document failed");
    const auto path=assembly::InstancePath{}.child(outer).encoded();
    run("component.activate",{{"instance_path",path}});
    run("view.named.set",{{"name","Nested view"},{"camera",camera}});
    require(live.active_occurrence_path()==path && live.displayed_document_id()==parent &&
        live.open_assembly(parent)->session.revision()==parent_revision &&
        workspace::named_views(live,parent).empty(),"Nested named view changed the parent or activation");
    const auto encoded=document::serialize_named_views(workspace::named_views(live,owner));
    auto duplicate=Json::parse(encoded);duplicate.push_back(duplicate[0]);
    rejects([&]{static_cast<void>(document::parse_named_views(duplicate.dump()));});
    rejects([&]{static_cast<void>(document::parse_named_views("broken"));});
}
}
int main(){try{
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto dir=root/("zima-named-views-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create named view test directory");verify(kernel,dir);
    require(fs::canonical(dir).parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Named view orientation, validation, transactions, ownership and Part/Assembly native persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
