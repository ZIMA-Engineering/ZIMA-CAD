#pragma once
#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <source_location>
namespace assembly_profile_test {
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline void near(double actual, double expected, const std::source_location where = std::source_location::current()) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-5)
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(actual) + " at line " + std::to_string(where.line()));
}
struct Fixture {
    workspace::Workspace live;
    const kernel::OcctKernel& kernel;
    fs::path directory;
    command_host::Interaction interaction;
    command_host::Host host;
    std::string source, owner, first, second, box;
    Fixture(const kernel::OcctKernel& kernel, const fs::path& directory, const std::string& name)
        : kernel(kernel), directory(directory), host(live, kernel, this->directory, options()) {
        run("new", {{"type","part"},{"name",name+"-source"}}); source=live.active_document_id();
        box=run("box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).at("container");run("save");
        run("new",{{"type","assembly"},{"name",name}});owner=live.active_document_id();
        first=run("component.insert",{{"source",source}}).at("occurrence");
        second=run("component.insert",{{"source",source}}).at("occurrence");
    }
    command_host::Options options() {
        command_host::Options o;
        o.settings=[]{return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
        o.interaction=[this]{return interaction;};return o;
    }
    Json run(const char* command, Json args=Json::object()) {
        const auto input=args.dump();
        auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(!result.ok)throw std::runtime_error(std::string(command)+" "+input+": "+result.code+": "+result.message);return result.data;
    }
    workspace::AssemblyState& state(){return *live.open_assembly(owner);}
    const assembly::AssemblyDocument& doc(){return state().session.document();}
    double volume(const std::string& target){return doc().find_occurrence(target)->calculated_source->volume;}
    std::string line(const std::string& sketch,double x1,double y1,double x2,double y2) {
        return run("sketch.segment.create",{{"sketch",sketch},{"first",{x1,y1}},{"second",{x2,y2}},{"snap_mm",0.000001}}).at("geometry");
    }
    std::string rectangle(double x,double y,double w,double h) {
        const std::string id=run("sketch.create",{{"name","Cut profile"},{"plane","XY"}}).at("sketch");
        line(id,x,y,x+w,y);line(id,x+w,y,x+w,y+h);line(id,x+w,y+h,x,y+h);line(id,x,y+h,x,y);return id;
    }
    void reject(const char* command,Json args,const char* code) {
        const auto before=doc();const auto revision=state().session.revision(),generation=state().session.data_generation();
        const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string(command)+" expected "+code+", got "+result.code+": "+result.message);
        require(state().session.revision()==revision&&state().session.data_generation()==generation&&doc().cuts==before.cuts&&doc().sketch_containers==before.sketch_containers&&!host.change(),"Rejected cut changed history");
        require(doc().sketches.size()==before.sketches.size(),"Rejected cut added a Sketch");
        for(std::size_t i=0;i<before.sketches.size();++i)require(doc().sketches[i].serialized()==before.sketches[i].serialized(),"Rejected cut altered a Sketch");
        for(const auto& part:before.components)require(doc().find_occurrence(part.occurrence_id)->calculated_source.shares_with(part.calculated_source),"Rejected cut replaced component geometry");
    }
};
}
