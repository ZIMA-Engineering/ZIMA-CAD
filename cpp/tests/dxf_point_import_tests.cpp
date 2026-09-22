#include "dxf_export_test_support.hpp"
#include <zima/interchange/dxf.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include <set>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double a,double b){require(std::abs(a-b)<1e-9,"DXF point coordinates differ from the expected millimetres");}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void model(const fs::path& dir) {
    const auto file=dir/"points.dxf";test::write_dxf_points(file);auto sketch=sketcher::Sketch::create_default();sketch.plane=sketcher::SketchPlane::YZ;
    const auto report=interchange::import_dxf(file,sketch,10);
    require(report.source_entities==3&&report.imported_entities==3&&report.warnings.empty()&&!report.import_block_id.empty(),"Point-only import has wrong receipt");
    require(sketch.plane==sketcher::SketchPlane::YZ&&sketch.points.size()==3&&sketch.import_blocks.size()==1&&sketch.import_blocks[0].geometry_ids.empty()&&sketch.import_blocks[0].point_ids.size()==3,"Points did not form a complete native block");
    require(sketch.points[0].id!=sketch.points[2].id&&!sketch.points[0].construction&&sketch.points[1].construction&&sketch.points[2].construction,"Coincident entities merged or construction flags changed");near(sketch.points[1].x,1);near(sketch.points[1].y,2);
    const auto mesh=sketch.viewer_mesh();for(const auto& point:sketch.points)require(std::ranges::any_of(mesh.points,[&](const auto& shown){return shown.reference.owner_id==sketch.id&&shown.reference.semantic_key=="point:"+point.id;}),"Imported point has no native viewer identity");
    auto restored=sketcher::Sketch::from_serialized(sketch.serialized());require(restored.import_blocks==sketch.import_blocks&&restored.points==sketch.points,"Native serialization lost point-only block");
    interchange::export_dxf(dir/"roundtrip.dxf",restored);const auto entities=test::read_dxf_entities(dir/"roundtrip.dxf");require(entities.size()==3&&std::ranges::all_of(entities,[](const auto& entity){return entity.type=="POINT";}),"Point-only DXF roundtrip lost entities");
    const auto original=sketch.points;const auto second=interchange::import_dxf(file,sketch);std::set<std::string> ids;for(const auto& point:sketch.points)require(ids.insert(point.id).second,"Repeated import reused point identity");
    sketch.transform_import_block(second.import_block_id,5,-2,std::numbers::pi/2);
    for(const auto& point:original)require(*sketch.find_point(point.id)==point,"Moving a new point block changed an older import");
    const auto* shifted=sketch.find_point(sketch.import_blocks[1].point_ids[1]);near(shifted->x,3);near(shifted->y,-1);
    const auto before_delete=sketch.serialized();bool rejected=false;try{sketch.remove_point(original[0].id);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&sketch.serialized()==before_delete,"Point deletion bypassed imported block ownership");
    auto merged=restored;const auto first=merged.points[0].id,last=merged.points[1].id;static_cast<void>(merged.merge_points(first,last));
    require(merged.import_blocks.size()==1&&merged.import_blocks[0].geometry_ids.empty()&&merged.import_blocks[0].point_ids.size()==2&&merged.points.size()==2,"Point merge discarded a point-only import block");
    test::write_dxf_points(dir/"mixed.dxf",true);auto mixed=sketcher::Sketch::create_default();const auto mix=interchange::import_dxf(dir/"mixed.dxf",mixed);
    require(mix.imported_entities==4&&mixed.points.size()==5&&mixed.segments.size()==1&&mixed.import_blocks[0].point_ids.size()==5,"Explicit POINTs were reused as line handles");
    require(mixed.segments[0].first_point_id!=mixed.points[0].id&&mixed.segments[0].first_point_id!=mixed.points[2].id,"A line borrowed an explicit point identity");
    auto collapsed=mixed;const auto collapsed_first=collapsed.segments[0].first_point_id,collapsed_second=collapsed.segments[0].second_point_id;
    static_cast<void>(collapsed.merge_points(collapsed_first,collapsed_second));
    require(collapsed.segments.empty()&&collapsed.points.size()==4&&collapsed.import_blocks.size()==1&&collapsed.import_blocks[0].geometry_ids.empty()&&collapsed.import_blocks[0].point_ids.size()==4,"Collapsing the final curve detached independent POINT entities from their block");
    require(sketcher::Sketch::from_serialized(collapsed.serialized()).import_blocks==collapsed.import_blocks,"Collapsed mixed block lost native persistence");
    static_cast<void>(merged.merge_points(merged.points[0].id,merged.points[1].id));
    require(merged.points.size()==1&&merged.import_blocks.size()==1&&merged.import_blocks[0].point_ids.size()==1,"A single remaining imported point lost its block");
    interchange::export_dxf(dir/"mixed-roundtrip.dxf",mixed);const auto mixed_entities=test::read_dxf_entities(dir/"mixed-roundtrip.dxf");require(mixed_entities.size()==4&&std::ranges::count_if(mixed_entities,[](const auto& e){return e.type=="POINT";})==3,"Mixed export lost coincident explicit points");
    test::write_dxf_points(dir/"inch.dxf",false,1);auto inch=sketcher::Sketch::create_default();static_cast<void>(interchange::import_dxf(dir/"inch.dxf",inch,10));near(inch.points[1].x,25.4);near(inch.points[1].y,50.8);
    test::write_dxf_points(dir/"unitless.dxf",false,0);auto scaled=sketcher::Sketch::create_default();static_cast<void>(interchange::import_dxf(dir/"unitless.dxf",scaled,10));near(scaled.points[1].x,10);near(scaled.points[1].y,20);
    for(const auto& bad:std::vector<std::string>{"10\n0\n20\n0\n30\n1\n","10\n0\n","10\nnan\n20\n0\n","10\n0\n20\n0\n210\n1\n"}) {
        std::ofstream(dir/"bad.dxf")<<"0\nSECTION\n2\nENTITIES\n0\nPOINT\n10\n1\n20\n2\n0\nPOINT\n"<<bad<<"0\nENDSEC\n0\nEOF\n";
        const auto before=sketch.serialized();rejected=false;try{static_cast<void>(interchange::import_dxf(dir/"bad.dxf",sketch));}catch(const std::exception&){rejected=true;}
        require(rejected&&sketch.serialized()==before,"Invalid late POINT partly imported a preceding valid entity");
    }
    const auto before=sketch.serialized();rejected=false;try{static_cast<void>(interchange::import_dxf(file,sketch,1,2));}catch(const std::exception&){rejected=true;}require(rejected&&sketch.serialized()==before,"POINT entity limit partly changed destination");
    rejected=false;try{static_cast<void>(interchange::import_dxf(dir/"unitless.dxf",sketch,1e308));}catch(const std::exception&){rejected=true;}require(rejected&&sketch.serialized()==before,"Overflowed scaled POINT partly changed destination");
}
void command_cases(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","point-import"}});const auto id=live.active_document_id();
    const auto count=live.open_part(id)->session.document().sketches.size(),revision=live.open_part(id)->session.revision();
    const auto result=run(host,"import.dxf",{{"path","points.dxf"}}).data;const auto sketch=result.at("sketch").get<std::string>();
    require(result.at("imported_entities")==3&&result.at("warnings").empty()&&result.at("body_calculated")==false,"Point-only import command failed");
    const auto current=[&]{return workspace::document_sketch(live,id,sketch);};require(current().points.size()==3&&current().import_blocks.size()==1,"Part import lost standalone points");
    const auto saved=current().serialized();run(host,"undo");require(live.open_part(id)->session.document().sketches.size()==count&&live.open_part(id)->session.revision()==revision,"Point import Undo lost its input");run(host,"redo");require(current().serialized()==saved,"Point import Redo changed native identities");
    run(host,"import.dxf",{{"path","points.dxf"},{"sketch",sketch}});require(current().points.size()==6&&current().import_blocks.size()==2,"Appending POINT import merged independent blocks");
    run(host,"save");const auto loaded=document::PartDocument::load(dir/"point-import.prtz");require(std::ranges::find(loaded.sketches,sketch,&sketcher::Sketch::id)->serialized()==current().serialized(),"Native Part lost point-only import blocks");
    const auto before=current().serialized();const auto before_revision=live.open_part(id)->session.revision();
    require(!host.execute({{"command","import.dxf"},{"arguments",{{"path","bad.dxf"},{"sketch",sketch}}}}).ok&&current().serialized()==before&&live.open_part(id)->session.revision()==before_revision,"Invalid POINT command changed document history");
    run(host,"new",{{"type","assembly"},{"name","point-assembly"}});const auto owner=live.active_document_id();const auto assembly_sketch=run(host,"sketch.create",{{"name","Points"},{"plane","XY"}}).data.at("sketch").get<std::string>();
    run(host,"import.dxf",{{"path","points.dxf"},{"sketch",assembly_sketch}});
    require(workspace::document_sketch(live,owner,assembly_sketch).points.size()==3&&live.open_assembly(owner)->session.document().components.empty(),"Assembly Sketch POINT import changed component ownership");
    run(host,"save");require(assembly::AssemblyDocument::load(dir/"point-assembly.asmz").sketches.back().points.size()==3,"Assembly did not persist point-only Sketch");
    const auto inserted=run(host,"import.dxf",{{"path","points.dxf"},{"output_directory",document::path_to_utf8(dir/"point-child")}}).data;
    require(inserted.at("parts").size()==1&&inserted.at("imported_entities")==3&&live.open_assembly(owner)->session.document().components.size()==1,"Assembly point import did not create one native Part component");
    const auto child=document::PartDocument::load(dir/"point-child/part-1.prtz");require(child.sketches.back().points.size()==3&&child.sketches.back().import_blocks[0].geometry_ids.empty(),"Assembly import wrote an incomplete point Part");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-dxf-point-"+kernel::make_stable_id());fs::create_directory(dir);model(dir);command_cases(kernel,dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);std::cout<<"DXF POINT identity, units, native blocks, transform, merge, commands and history passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
