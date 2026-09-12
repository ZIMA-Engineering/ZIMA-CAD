#include <zima/command_host/host.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/document/file_path.hpp>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json a=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(a)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
double step_volume(const fs::path& path){const auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},path);require(!imported.calculated.empty(),"Exported STEP is empty");return imported.calculated.back().volume;}
double stl_volume(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);in.seekg(80);unsigned char count_bytes[4]{};in.read(reinterpret_cast<char*>(count_bytes),4);
    const std::uint32_t count=count_bytes[0]|(std::uint32_t(count_bytes[1])<<8)|(std::uint32_t(count_bytes[2])<<16)|(std::uint32_t(count_bytes[3])<<24);
    require(count>0 && count<1000000 && fs::file_size(path)==84+50ull*count,"STL binary size/count is invalid");
    double volume=0;
    for(std::uint32_t i=0;i<count;++i) {
        float values[12];in.read(reinterpret_cast<char*>(values),48);in.ignore(2);require(bool(in),"Incomplete STL triangle");
        const double ax=values[3],ay=values[4],az=values[5],bx=values[6],by=values[7],bz=values[8],cx=values[9],cy=values[10],cz=values[11];
        volume+=(ax*(by*cz-bz*cy)+ay*(bz*cx-bx*cz)+az*(bx*cy-by*cx))/6.;
    }
    return std::abs(volume);
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options settings;settings.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,settings);run(host,"new",{{"type","part"},{"name","export-source"}});const auto part_id=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    auto* part=live.open_part(part_id);const auto revision=part->session.revision(),generation=part->session.data_generation();const auto shape=part->session.calculated_boundaries().back().kernel_shape;
    const auto step=dir/fs::path(u8"kvádr export.step"),stl=dir/fs::path(u8"kvádr export.stl");
    const auto report=run(host,"export.step",{{"path",document::path_to_utf8(step)}}).data;
    require(report.at("bytes")==fs::file_size(step) && report.at("source_revision")==revision && report.at("model_changed")==false && std::abs(step_volume(step)-6000)<1e-5,"STEP export changed volume or returned an invalid receipt");
    run(host,"export.stl",{{"path",document::path_to_utf8(stl)}});require(std::abs(stl_volume(stl)-6000)<1e-5,"STL triangulation changed the closed volume");
    require(part->session.revision()==revision && part->session.data_generation()==generation && part->session.calculated_boundaries().back().kernel_shape==shape,"Export changed document or calculated state");
    const auto original=bytes(step);
    require(host.execute({{"command","export.step"},{"arguments",{{"path",document::path_to_utf8(step)}}}}).code=="file_exists" && bytes(step)==original,"Unrequested export overwrote a file");
    auto stale=part->session.document();stale.find_container(box)->box.length=20;part->session.commit(std::move(stale),part->session.calculated_boundaries());const auto stale_revision=part->session.revision();
    run(host,"export.step",{{"path",document::path_to_utf8(step)},{"overwrite",true}});require(std::abs(step_volume(step)-6000)<1e-5 && part->session.revision()==stale_revision,"Export implicitly regenerated pending model data");
    run(host,"regenerate");run(host,"export.step",{{"path",document::path_to_utf8(step)},{"overwrite",true}});require(std::abs(step_volume(step)-12000)<1e-5,"Explicitly regenerated geometry did not reach export");
    const auto conflict=dir/"concurrent.step";bool rejected=false;
    try{static_cast<void>(workspace::export_file(live,part_id,conflict,{},[&](auto task){std::ofstream(conflict)<<"concurrent owner";task();}));}catch(const std::exception&){rejected=true;}
    require(rejected && bytes(conflict)=="concurrent owner","Concurrent target creation was overwritten");
    for(const auto& entry:fs::directory_iterator(dir))require(!entry.path().filename().string().starts_with(".zima-export-"),"Export failure left its staging directory");
    const auto sketch=run(host,"sketch.create",{{"name","DXF export"}}).data.at("sketch").get<std::string>();
    run(host,"sketch.rectangle.create",{{"sketch",sketch},{"first",{0,0}},{"second",{20,10}}});
    run(host,"sketch.circle.create",{{"sketch",sketch},{"center",{30,10}},{"radius_mm",3}});
    run(host,"sketch.arc.create",{{"sketch",sketch},{"center",{40,10}},{"start",{42,10}},{"end",{40,12}}});
    const auto dxf=dir/fs::path(u8"obrys export.dxf");run(host,"export.dxf",{{"path",document::path_to_utf8(dxf)},{"sketch",sketch}});
    auto restored=sketcher::Sketch::create_default();const auto imported=interchange::import_dxf(dxf,restored);
    require(imported.imported_entities==6 && restored.segments.size()==4 && restored.circles.size()==1 && restored.arcs.size()==1 && restored.circles.front().radius==3 && restored.arcs.front().radius==2,"DXF export lost native circular geometry");
    const auto dxf_before=bytes(dxf);run(host,"sketch.bspline.create",{{"sketch",sketch},{"points",{{50,0},{53,4},{57,-2},{60,0}}},{"degree",3}});
    require(host.execute({{"command","export.dxf"},{"arguments",{{"path",document::path_to_utf8(dxf)},{"sketch",sketch},{"overwrite",true}}}}).code=="unsupported_geometry" && bytes(dxf)==dxf_before,"DXF silently lost spline or replaced the destination on failure");
    require(!host.execute({{"command","export.step"},{"arguments",{{"path","wrong.igs"}}}}).ok && !fs::exists(dir/"wrong.igs"),"Mismatched export extension wrote a file");
    // The nested source is intentionally never saved. Export must consume its
    // persisted occurrence snapshot, not reopen dependencies or refresh them.
    auto flat=assembly::AssemblyDocument::create_default();const auto flat_id=flat.document_id;live.add_assembly(std::move(flat),dir/"missing-flat.asmz");static_cast<void>(live.insert_open_part(flat_id,part_id,"Inserted"));
    auto top=assembly::AssemblyDocument::create_default();const auto top_id=top.document_id;live.add_assembly(std::move(top),dir/"missing-top.asmz");static_cast<void>(live.insert_open_assembly(top_id,flat_id,"Nested"));
    const auto parent_revision=live.open_assembly(top_id)->session.revision();
    run(host,"activate",{{"document",part_id}});run(host,"box.set",{{"container",box},{"length_mm","30"}});
    run(host,"activate",{{"document",top_id}});const auto nested_path=dir/"nested.step";run(host,"export.step",{{"path",document::path_to_utf8(nested_path)}});
    require(std::abs(step_volume(nested_path)-12000)<1e-5 && live.open_assembly(top_id)->session.revision()==parent_revision,"Nested export refreshed dependencies or lost stored geometry");
    require(host.execute({{"command","export.stl"},{"arguments",{{"path","nested.stl"}}}}).code=="unsupported_assembly" && !fs::exists(dir/"nested.stl"),"Unsupported nested STL partially exported");
    run(host,"activate",{{"document",flat_id}});run(host,"export.stl",{{"path","flat.stl"}});require(std::abs(stl_volume(dir/"flat.stl")-12000)<1e-5,"Flat Assembly STL did not use calculated occurrence state");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-export-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);const auto unicode_dir=dir/fs::path(u8"český projekt");fs::create_directory(unicode_dir);verify(kernel,unicode_dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"STEP/STL volumes, DXF geometry, snapshot export, nested ownership, UTF-8, overwrite and atomic publication passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
