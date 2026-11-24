#include <zima/command_host/host.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/document/feature_serialization.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

// Author factory assets with the same native templates, commands and Relations
// used by the application. No external file is required to reopen these Parts.
namespace {
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-7,"Unexpected CAM library geometry");}
Json run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    require(result.ok,std::string(name)+": "+result.message);return result.data;
}
void parameter(document::PartDocument& doc,const std::string& key,const std::string& value,
        std::array<const char*,5> labels) {
    doc.user_parameters[key]=value;doc.user_parameter_values[key][""]=value;
    if(std::ranges::find(doc.user_parameter_order,key)==doc.user_parameter_order.end())doc.user_parameter_order.push_back(key);
    unsigned i{};for(const auto* language:{"cs","en","de","fr","ru"})doc.user_parameter_labels[key][language]=labels[i++];
}
void tool_parameters(document::PartDocument& doc) {
    parameter(doc,"cam_diameter_mm","10",{"prumer cam [mm]","CAM diameter [mm]","CAM-Durchmesser [mm]","Diamètre FAO [mm]","Диаметр CAM [мм]"});
    parameter(doc,"cam_cutting_length_mm","22",{"rezna delka [mm]","Cutting length [mm]","Schneidenlänge [mm]","Longueur de coupe [mm]","Длина режущей части [мм]"});
    parameter(doc,"cam_shank_diameter_mm","10",{"prumer stopky [mm]","Shank diameter [mm]","Schaftdurchmesser [mm]","Diamètre de queue [mm]","Диаметр хвостовика [мм]"});
    parameter(doc,"cam_overall_length_mm","72",{"celkova delka [mm]","Overall length [mm]","Gesamtlänge [mm]","Longueur totale [mm]","Общая длина [мм]"});
    parameter(doc,"cam_flutes","3",{"pocet britu","Flute count","Schneidenzahl","Nombre de dents","Число зубьев"});
    parameter(doc,"cam_spindle_rpm","7767",{"otacky [1/min]","Spindle speed [1/min]","Drehzahl [1/min]","Vitesse de broche [1/min]","Частота вращения [1/мин]"});
    parameter(doc,"cam_feed_mm_min","2663",{"posuv [mm/min]","Feed [mm/min]","Vorschub [mm/min]","Avance [mm/min]","Подача [мм/мин]"});
    parameter(doc,"cam_cutting_speed_m_min","244",{"rezna rychlost [m/min]","Cutting speed [m/min]","Schnittgeschwindigkeit [m/min]","Vitesse de coupe [m/min]","Скорость резания [м/мин]"});
    parameter(doc,"cam_feed_per_tooth_mm","0.114",{"posuv na zub [mm]","Feed per tooth [mm]","Zahnvorschub [mm]","Avance par dent [mm]","Подача на зуб [мм]"});
    parameter(doc,"cam_axial_depth_mm","15",{"axialni zaber [mm]","Axial depth [mm]","Axiale Zustellung [mm]","Profondeur axiale [mm]","Осевая глубина [мм]"});
    parameter(doc,"cam_radial_depth_mm","5",{"radialni zaber [mm]","Radial depth [mm]","Radiale Zustellung [mm]","Engagement radial [mm]","Радиальная глубина [мм]"});
    parameter(doc,"cam_preset_conditions","Side milling; cast aluminium; ISO N 23-25; example values to adjust",
        {"podminky rezneho predpisu","Cutting preset conditions","Bedingungen der Schnittwerte","Conditions des paramètres de coupe","Условия режимов резания"});
    // Free descriptions retain their ordinary Part meanings. In particular,
    // stock and standard/manufacturer text are never parsed as CAM dimensions.
}
std::string symbol(const document::PartDocument& doc,const std::string& owner,const std::string& key) {
    const auto name=doc.dimension_identifiers.identifier(owner,key);
    require(!name.empty(),"Missing dimension identifier: "+key);return name;
}
struct Author {
    workspace::Workspace live;kernel::OcctKernel kernel;fs::path directory;
    command_host::Host host;
    Author(fs::path dir):directory(std::move(dir)),host(live,kernel,directory){}
    std::string create(const fs::path& path,const workspace::NativeTemplateSettings& templates) {
        fs::create_directories(path.parent_path());
        auto id=workspace::insert_native_document(live,workspace::prepare_new_native_document(
            workspace::NativeDocumentType::Part,path.stem().string(),path,templates));
        live.activate(id);live.display_top_level(id);return id;
    }
    const document::PartDocument& doc()const{return live.open_part(live.active_document_id())->session.document();}
    void parameters(std::initializer_list<std::pair<std::string,std::string>> values) {
        auto data=workspace::user_parameters(live,live.active_document_id());
        for(const auto& [key,value]:values){
            data.flat[key]=value;data.values[key][""]=value;
            if(std::ranges::find(data.order,key)==data.order.end())data.order.push_back(key);
        }
        static_cast<void>(workspace::set_user_parameters(live,live.active_document_id(),std::move(data)));
    }
    void labeled(const std::string& key,const std::string& value,std::array<const char*,5> labels) {
        auto definition=doc();parameter(definition,key,value,labels);
        static_cast<void>(workspace::set_user_parameters(live,live.active_document_id(),
            {definition.user_parameters,definition.user_parameter_order,definition.user_parameter_labels,definition.user_parameter_values}));
    }
    std::string cylinder(const std::string& name,double diameter,double length,double offset) {
        const auto sketch=run(host,"sketch.create",{{"name",name},{"plane","XY"}}).at("sketch").get<std::string>();
        const auto circle=run(host,"sketch.circle.create",{{"sketch",sketch},{"center",{0,0}},{"radius_mm",diameter/2}}).at("geometry").get<std::string>();
        const auto dimension=run(host,"sketch.dimension.create",{{"sketch",sketch},{"kind","diameter"},{"geometry",{circle}},{"value",diameter}}).at("dimension").get<std::string>();
        document::FeatureParameters p;p.sides[0].length=length;p.profile_plane_offset=offset;
        auto data=document::serialize_feature_parameters(p);for(auto& side:data["sides"])if(side["operation"]=="extrusion")side["operation"]="extrude";data.erase("sketch_id");
        const auto feature=run(host,"feature.create",{{"sketch",sketch},{"parameters",data}}).at("container").get<std::string>();
        return symbol(doc(),sketch,"dimension:"+dimension)+" = "+(offset<0?"cam_diameter_mm":"cam_shank_diameter_mm")+"\n"+
            symbol(doc(),feature,"parameter:side0_length")+" = "+(offset<0?"cam_cutting_length_mm":"cam_overall_length_mm - cam_cutting_length_mm")+"\n"+
            symbol(doc(),feature,"parameter:profile_offset")+" = "+(offset<0?"-cam_cutting_length_mm":"0")+"\n";
    }
};
void verify_tool(const document::PartDocument& doc,double diameter,double length,double overall) {
    require(doc.body_history.bodies().size()==2&&doc.history.size()==2,"Tool does not have two ordinary feature Bodies");
    const auto cutting=doc.user_parameters.at("cam_cutting_body"),shank=doc.user_parameters.at("cam_shank_body");
    require(cutting!=shank&&doc.body_history.find(cutting)&&doc.body_history.find(shank),"Invalid tool roles");
    near(doc.history[0].feature.profile_plane_offset,-length);near(doc.history[0].feature.sides[0].length,length);
    near(doc.history[1].feature.profile_plane_offset,0);near(doc.history[1].feature.sides[0].length,overall-length);
    const auto circle=std::ranges::find(doc.sketches,doc.history[0].feature.sketch_id,&sketcher::Sketch::id);
    require(circle!=doc.sketches.end()&&circle->circles.size()==1,"Missing editable tool profile");
    near(circle->circles.front().radius,diameter/2);
}
void templates(const fs::path& root,const fs::path& output) {
    fs::create_directories(output/"templates");
    for(const auto* unit:{"mm","in"}) {
        auto doc=document::PartDocument::load(root/"config/templates"/(std::string("START_PART_")+unit+".prtz"));
        doc.document_id=document::PartDocument::create_default().document_id;doc.name=std::string("START_TOOL_")+unit;
        tool_parameters(doc);
        // Use the canonical unit-independent default. The source inch template
        // contains a historical cancelling division/multiplication; arithmetic
        // on unavailable mass prevents an empty new tool from being saved.
        doc.relations="mass = model.mass\n";
        // Numeric CAM inputs explicitly carry mm in their names, independently
        // of document display units. An empty template has no geometry Relations.
        doc.save(output/"templates"/(doc.name+".toolz"),{});
    }
}
void tool(const fs::path& output,workspace::NativeTemplateSettings settings) {
    Author a(output);const auto path=output/"lib/CAM/Tools/Milling/End_mill_10.toolz";
    const auto id=a.create(path,settings);const auto cutting=a.doc().user_parameters.at("cam_cutting_body"),shank=a.doc().user_parameters.at("cam_shank_body");
    auto relations=a.doc().relations+"\n"+a.cylinder("Cutting envelope",10,22,-22);
    run(a.host,"body.activate",{{"body",shank}});relations+=a.cylinder("Shank",10,50,0);
    run(a.host,"body.activate",{{"body",cutting}});
    run(a.host,"document.relations.set",{{"relations",relations}});run(a.host,"regenerate");
    verify_tool(a.doc(),10,22,72);run(a.host,"save");verify_tool(document::PartDocument::load(path),10,22,72);
    const auto copy_path=output/"verification/tool-copy.toolz";fs::create_directories(copy_path.parent_path());
    static_cast<void>(a.live.save_copy(id,copy_path,{}));
    const auto copy=document::PartDocument::load(copy_path);verify_tool(copy,10,22,72);
    require(copy.document_id!=id&&copy.user_parameters==a.doc().user_parameters,"Tool Save As lost identity or Parameters");
    require(!workspace::drawing_annotation_sources(nullptr,id,path).empty(),"Closed tool is unavailable as a native Drawing source");
    require(workspace::build_bom_rows_for_source(id,path,nullptr).size()==1,"Closed tool lost its Drawing BOM metadata");
    const auto revision=a.live.open_part(id)->session.revision();
    a.parameters({{"cam_diameter_mm","10"}});require(a.live.open_part(id)->session.revision()==revision,"Unchanged tool parameters created an Undo transaction");
    for(const auto diameter:{12.,8.,10.}) {
        a.parameters({{"cam_diameter_mm",std::to_string(diameter)},{"cam_cutting_length_mm","20"},{"cam_overall_length_mm","75"}});
        run(a.host,"regenerate");verify_tool(a.doc(),diameter,20,75);
    }
    a.parameters({{"cam_cutting_length_mm","22"},{"cam_overall_length_mm","72"}});run(a.host,"regenerate");verify_tool(a.doc(),10,22,72);
    document::FamilyTable family;family.columns={"Shank"};family.bindings["Shank"]={"body",shank,{}};
    family.instances={{"Cutting envelope only",{{"Shank","no"}}}};
    static_cast<void>(workspace::set_family_table(a.live,id,family));
    const auto member=workspace::open_family_instance(a.live,a.kernel,id,"Cutting envelope only");
    require(a.live.open_part(member)->session.document().body_history.find(shank)->suppressed,"Tool Family did not retain ordinary Body suppression");
    require(!a.live.open_part(id)->session.document().body_history.find(shank)->suppressed,"Tool Family modified the generic Body");
    // Keep the published file at its initial dimensions; edits above are tests.
    std::cout<<"Tool: native creation, Relations, repeated edits, unchanged parameters, Family and save/reopen passed\n";
}
void vise(const fs::path& output,workspace::NativeTemplateSettings settings) {
    Author a(output);const auto path=output/"lib/CAM/Fixtures/Vises/Parallel_jaws.prtz";
    const auto id=a.create(path,settings);std::string fixed,moving,relation;
    for(bool move:{false,true}) {
        const auto sketch=run(a.host,"sketch.create",{{"name",move?"Moving jaw contact":"Fixed jaw contact"},{"plane","XZ"}}).at("sketch").get<std::string>();
        const std::array<std::array<double,2>,4> p{{{-50.8,0},{50.8,0},{50.8,44.5},{-50.8,44.5}}};
        for(unsigned i=0;i<4;++i)run(a.host,"sketch.segment.create",{{"sketch",sketch},{"first",p[i]},{"second",p[(i+1)%4]},{"snap_mm",1e-6}});
        if(move) {
            moving=sketch;run(a.host,"sketch.set",{{"sketch",sketch},{"plane_offset_mm",100}});
            const auto& s=*std::ranges::find(a.doc().sketches,sketch,&sketcher::Sketch::id);
            relation=symbol(a.doc(),s.owner_container_id,"parameter:profile_offset")+" = cam_opening_mm\n";
        } else fixed=sketch;
    }
    a.parameters({{"cam_fixed_jaw_sketch",fixed},{"cam_moving_jaw_sketch",moving},{"cam_opening_mm","100"}});
    a.labeled("cam_opening_mm","100",{"rozevreni [mm]","Jaw opening [mm]","Backenöffnung [mm]","Ouverture des mors [mm]","Раскрытие губок [мм]"});
    run(a.host,"document.relations.set",{{"relations",a.doc().relations+"\n"+relation}});run(a.host,"regenerate");run(a.host,"save");
    const auto saved=document::PartDocument::load(path);
    require(saved.sketches.size()==2&&saved.body_history.bodies().size()==1,"Vise Sketch ownership failed");
    for(const auto& sketch:saved.sketches)for(const auto& point:sketch.points) {
        const auto world=sketch.world_point(point.x,point.y);
        near(world.y,sketch.id==fixed?0:100);near(world.z,-point.y);
        require(world.z<=1e-7&&world.z>=-44.5-1e-7,"Jaw extends above its top datum");
    }
    for(const auto opening:{80.,120.,100.}) {
        a.parameters({{"cam_opening_mm",std::to_string(opening)}});run(a.host,"regenerate");
        near(std::ranges::find(a.doc().sketches,fixed,&sketcher::Sketch::id)->plane_offset,0);
        near(std::ranges::find(a.doc().sketches,moving,&sketcher::Sketch::id)->plane_offset,opening);
    }
    std::cout<<"Vise: two native Sketches, fixed Y=0, moving Y=opening, upper Z=0, repeated edits and reopen passed\n";
}
void machine(const fs::path& output,workspace::NativeTemplateSettings settings) {
    Author a(output);const auto path=output/"lib/CAM/Machines/Milling/Three_axis_mill.prtz";
    a.create(path,settings);
    a.labeled("cam_machine_axes","3",{"pocet os","Axis count","Anzahl Achsen","Nombre d’axes","Число осей"});
    a.labeled("cam_maximum_spindle_rpm","12000",{"maximalni otacky [1/min]","Maximum spindle speed [1/min]","Maximale Drehzahl [1/min]","Vitesse maximale de broche [1/min]","Максимальная частота вращения [1/мин]"});
    a.labeled("cam_maximum_feed_mm_min","10000",{"maximalni posuv [mm/min]","Maximum feed [mm/min]","Maximaler Vorschub [mm/min]","Avance maximale [mm/min]","Максимальная подача [мм/мин]"});
    a.labeled("cam_rapid_mm_min","20000",{"rychloposuv [mm/min]","Rapid feed [mm/min]","Eilgang [mm/min]","Avance rapide [mm/min]","Быстрый ход [мм/мин]"});
    a.parameters({{"cam_machine_kind","milling"},{"cam_postprocessor","generic"},
        {"cam_minimum_x_mm","-500"},{"cam_maximum_x_mm","500"},
        {"cam_minimum_y_mm","-500"},{"cam_maximum_y_mm","500"},
        {"cam_minimum_z_mm","-500"},{"cam_maximum_z_mm","500"}});
    run(a.host,"regenerate");run(a.host,"save");const auto saved=document::PartDocument::load(path);
    require(saved.body_history.bodies().size()==1&&saved.history.empty()&&saved.user_parameters.at("cam_machine_axes")=="3","Machine definition lost metadata or editable Body");
    std::cout<<"Machine: generic parameters and ordinary editable Body saved/reopened\n";
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: zima_create_cam_library repository-root output-config-directory (new files only)");
        const auto root=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
        for(const auto* path:{"templates/START_TOOL_mm.toolz","templates/START_TOOL_in.toolz",
                "lib/CAM/Tools/Milling/End_mill_10.toolz","lib/CAM/Fixtures/Vises/Parallel_jaws.prtz",
                "lib/CAM/Machines/Milling/Three_axis_mill.prtz"})
            require(!fs::exists(output/path),"Refusing to overwrite existing library asset: "+(output/path).string());
        templates(root,output);workspace::NativeTemplateSettings settings{output/"templates","START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};
        // Ordinary Part creation reads the committed template, tool creation the
        // newly authored dedicated template. Never modify either source in place.
        settings.part_template=root/"config/templates/START_PART_mm.prtz";
        tool(output,settings);vise(output,settings);machine(output,settings);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
