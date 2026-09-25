#include "profile_solid_fixture.hpp"
#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/measurement_edits.hpp>
#include <zima/document/file_path.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>
#include <iostream>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    const auto value=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!value.ok)throw std::runtime_error(std::string(command)+": "+value.code+": "+value.message);return value;
}
Json ref(const char* kind,const std::string& owner,const std::string& key={},const std::string& path={}) {
    return {{"kind",kind},{"owner",owner},{"key",key},{"instance_path",path}};
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;bool editing=false,is_template=false;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction value;value.editing=editing;value.template_document=is_template;return value;};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","measurement-edits"}});const auto id=live.active_document_id();
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    const auto shape=live.open_part(id)->session.calculated_boundaries().back().kernel_shape;
    const auto refs=Json::array({ref("plane",id+":origin","origin:plane:xy"),ref("face",box,test::profile_key(live.open_part(id)->session.document(),box,"z_max"))});
    const auto created=run(host,"measurement.create",{{"name"," Gap "},{"references",refs}}).data;
    const auto object=created.at("object").get<std::string>();
    require(created.at("name")=="Gap"&&created.at("changed")==true&&created.at("body_calculated")==false,"Measurement creation did not commit one normalized record");
    require(created["body_id"]==live.open_part(id)->session.document().body_history.active_body_id()&&created["after_object_id"]==box,"Measurement lost its history location");
    require(std::abs(created["distance"]["value"]["value"].get<double>()-15)<1e-8,"Saved plane/face distance wrong");
    const auto revision=live.open_part(id)->session.revision(),generation=live.open_part(id)->session.data_generation();
    require(!run(host,"measurement.set",{{"object",object}}).data.at("changed").get<bool>(),"Unchanged measurement refresh added history");
    require(!run(host,"measurement.set",{{"object",object},{"references",refs},{"name","Gap"}}).data.at("changed").get<bool>(),"Equivalent edit added history");
    const auto rejected=[&](const char* command,Json args,const char* code) {
        const auto before=live.open_part(id)->session.revision(),version=live.open_part(id)->session.data_generation();
        const auto count=live.open_part(id)->session.document().measurements.size();
        const auto value=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(value.ok||value.code!=code)throw std::runtime_error(std::string(command)+" unexpected result: "+value.code+": "+value.message);
        require(live.open_part(id)->session.revision()==before&&live.open_part(id)->session.data_generation()==version&&live.open_part(id)->session.document().measurements.size()==count&&!host.change(),"Rejected measurement changed the live document");
    };
    rejected("measurement.create",{{"name","Gap"},{"references",refs}},"duplicate_name");
    rejected("measurement.set",{{"object",object},{"name","   "}},"invalid_name");
    rejected("measurement.set",{{"object",object},{"references",Json::array()}},"invalid_arguments");
    rejected("measurement.set",{{"object",object},{"references",Json::array({ref("face",box,"missing")})}},"missing_reference");
    rejected("measurement.set",{{"object",object},{"values",Json::array()}},"invalid_arguments");
    rejected("measurement.delete",{{"object","absent"}},"measurement_not_found");
    require(live.open_part(id)->session.revision()==revision&&live.open_part(id)->session.data_generation()==generation,"Invalid/no-op edits changed history");
    editing=true;rejected("measurement.set",{{"object",object}},"editing_in_progress");editing=false;
    is_template=true;rejected("measurement.delete",{{"object",object}},"unsupported_document");is_template=false;
    run(host,"measurement.set",{{"object",object},{"name","Renamed"}});run(host,"undo");
    require(run(host,"measurement.get",{{"object",object}}).data.at("name")=="Gap","Measurement rename Undo failed");run(host,"redo");
    run(host,"measurement.delete",{{"object",object}});require(run(host,"measurement.list").data.at("total")==0,"Delete did not remove record");
    run(host,"undo");require(run(host,"measurement.get",{{"object",object}}).data.at("references")==refs,"Delete Undo lost original references");
    require(live.open_part(id)->session.calculated_boundaries().back().kernel_shape==shape,"Measurement edits recalculated the body");
    const auto stale=workspace::prepare_measurement_edit(live,id,object);
    zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"height_mm","40"}});run(host,"undo");
    require(live.open_part(id)->session.revision()==stale.revision,"Fixture did not restore the original revision");
    try {static_cast<void>(workspace::commit_measurement(live,stale,stale.initial));throw std::runtime_error("Stale edit survived Undo");}
    catch(const workspace::MeasurementOperationError& error){require(std::string(error.code)=="stale_edit","Stale error code changed");}
    auto identity=workspace::prepare_measurement_edit(live,id,object);auto forged=identity.initial;forged.body_id="other-body";
    try {static_cast<void>(workspace::commit_measurement(live,identity,forged));throw std::runtime_error("Measurement moved history ownership");}
    catch(const workspace::MeasurementOperationError& error){require(std::string(error.code)=="identity_changed","Identity error code changed");}
    zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"height_mm","40"}});
    require(run(host,"measurement.get",{{"object",object}}).data["distance"]["value"]["value"]==15,"Source edit overwrote saved result");
    const auto refreshed=run(host,"measurement.set",{{"object",object}}).data;
    require(refreshed.at("changed")==true&&std::abs(refreshed["distance"]["value"]["value"].get<double>()-20)<1e-8,"Refresh ignored changed source");
    run(host,"undo");require(run(host,"measurement.get",{{"object",object}}).data["distance"]["value"]["value"]==15,"Refresh Undo lost last value");run(host,"redo");
    const auto replacement=Json::array({ref("object",box)});
    const auto replaced=run(host,"measurement.set",{{"object",object},{"references",replacement}}).data;
    require(replaced["distance"].is_null()&&replaced["values"].size()==1&&std::abs(replaced["values"][0]["volume"]["value"].get<double>()-8000)<1e-8,"Reference replacement retained stale distance or values");
    run(host,"save");const auto before_close=workspace::prepare_measurement_edit(live,id,object);
    run(host,"close",{{"document",id}});run(host,"open",{{"path",document::path_to_utf8(dir/"measurement-edits.prtz")}});
    require(run(host,"measurement.get",{{"object",object}}).data.at("references")==replacement,"Native Part lost edited measurement");
    try {static_cast<void>(workspace::commit_measurement(live,before_close,before_close.initial));throw std::runtime_error("Reopened document accepted obsolete editor");}
    catch(const workspace::MeasurementOperationError& error){require(std::string(error.code)=="stale_edit","Runtime identity was not checked");}
    run(host,"new",{{"type","assembly"},{"name","measurement-assembly-edits"}});const auto owner=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",id}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",id}}).data.at("occurrence").get<std::string>();
    auto* assembly=live.open_assembly(owner);auto positioned=assembly->session.document();positioned.find_occurrence(second)->placement.x=50;assembly->session.commit(std::move(positioned));
    const auto a=assembly::InstancePath{}.child(first).encoded(),b=assembly::InstancePath{}.child(second).encoded();
    const auto source_packet=assembly->session.document().find_occurrence(first)->calculated_source;
    const auto pair=run(host,"measurement.create",{{"name","Separation"},{"references",Json::array({ref("object","",{},a),ref("object","",{},b)})}}).data;
    require(std::abs(pair["distance"]["value"]["value"].get<double>()-40)<1e-8,"Assembly gap wrong");
    const auto pair_id=pair.at("object").get<std::string>();
    run(host,"measurement.set",{{"object",pair_id},{"name","Clearance"}});run(host,"measurement.delete",{{"object",pair_id}});run(host,"undo");
    require(run(host,"measurement.get",{{"object",pair_id}}).data.at("name")=="Clearance"&&assembly->session.document().find_occurrence(first)->calculated_source.shares_with(source_packet),"Assembly measurement copied geometry or lost Undo");
    run(host,"save");run(host,"close",{{"document",owner}});run(host,"open",{{"path",document::path_to_utf8(dir/"measurement-assembly-edits.asmz")}});
    require(run(host,"measurement.get",{{"object",pair_id}}).data.at("references")==pair.at("references"),"Assembly file lost exact measurement paths");
    run(host,"component.activate",{{"instance_path",a}});
    require(host.execute({{"command","measurement.set"},{"arguments",{{"object",object}}}}).code=="unsupported_context","Activated Part wrote measurement in the wrong frame");
    run(host,"component.deactivate");
    run(host,"new",{{"type","part"},{"name","measurement-sketch"}});const auto sketch_part=live.active_document_id();
    const auto sketch=run(host,"sketch.create",{{"name","Measured sketch"},{"plane","XY"}}).data.at("sketch").get<std::string>();
    run(host,"sketch.segment.create",{{"sketch",sketch},{"first",{10,0}},{"second",{20,0}}});
    const auto sketch_measurement=run(host,"measurement.create",{{"references",Json::array({ref("object",sketch),
        ref("plane",sketch_part+":origin","origin:plane:yz")})}}).data;
    require(std::abs(sketch_measurement["distance"]["value"]["value"].get<double>()-10)<1e-8&&sketch_measurement["values"][0]["volume"].is_null(),
        "Whole Sketch measurement lost its finite geometry or invented volume");
    run(host,"new",{{"type","drawing"},{"name","measurement-edit-drawing"}});
    require(host.execute({{"command","measurement.create"},{"arguments",{{"references",replacement}}}}).code=="unsupported_document","Drawing accepted Part measurement mutation");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-measurement-edits-"+kernel::make_stable_id());
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
