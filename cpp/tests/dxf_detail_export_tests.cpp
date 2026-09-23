#include "dxf_export_test_support.hpp"
#include <zima/interchange/dxf.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <fstream>
#include <set>
using namespace zima;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void verify_document_structure(const fs::path& path) {
    std::ifstream input(path);std::string code,value,type,section;
    std::set<std::string> handles,sections,layers;std::vector<std::string> pointers;
    bool layer=false,has_handle=false;std::size_t model_entities=0;
    while(std::getline(input,code)&&std::getline(input,value)) {
        if(!value.empty()&&value.back()=='\r')value.pop_back();const int group=std::stoi(code);
        if(group==0) {
            if(section=="ENTITIES"&&type!="SECTION")require(has_handle,"DXF model entity lacks a handle");
            type=value;has_handle=false;layer=value=="LAYER";
            if(value=="ENDSEC")section.clear();
        }
        if(group==2&&type=="SECTION"){section=value;sections.insert(value);}
        if(group==2&&layer)layers.insert(value);
        if((group==5||group==105)&&section!="HEADER") {require(handles.insert(value).second,"DXF duplicates a handle");has_handle=true;}
        if(group==330||group==340||group==350) {
            if(value!="0")pointers.push_back(value);
            if(group==330&&section=="ENTITIES"){require(value=="14","DXF entity has no Model_Space owner");++model_entities;}
        }
    }
    for(const auto& pointer:pointers)require(handles.contains(pointer),"DXF contains a dangling object reference");
    for(const auto* name:{"HEADER","TABLES","BLOCKS","ENTITIES","OBJECTS"})require(sections.contains(name),"DXF document section is missing");
    require(layers.contains("PROFILE")&&layers.contains("CONSTRUCTION")&&model_entities>0,"DXF layer table or model ownership is missing");
}
void verify(const fs::path& dir) {
    auto sketch=test::dxf_detail_fixture();const auto before=sketch.serialized();const auto path=dir/"detail.dxf";
    interchange::export_dxf(path,sketch);verify_document_structure(path);test::check_dxf_details(path,sketch);require(sketch.serialized()==before,"DXF export edited its source Sketch");
    auto restored=sketcher::Sketch::from_serialized(before);interchange::export_dxf(dir/"restored.dxf",restored);test::check_dxf_details(dir/"restored.dxf",restored);
    require(bytes(path)==bytes(dir/"restored.dxf"),"Native reopening changed deterministic DXF output");
    auto imported=sketcher::Sketch::create_default();const auto report=interchange::import_dxf(path,imported);
    std::size_t vertices=0;for(const auto& text:sketch.texts)for(const auto& contour:text.contours)vertices+=contour.size();
    require(imported.arcs.size()==1&&std::abs(imported.arcs[0].radius-2)<1e-8&&imported.segments.size()==vertices+2,"DXF outline/fillet roundtrip lost editable curves");
    require(report.warnings.empty(),"Unexpected unsupported DXF detail entity");
    const auto protected_bytes=bytes(path);
    for(int failure=0;failure<3;++failure) {
        auto invalid=sketch;
        if(failure==0)invalid.corner_radii[0].radius=20;
        if(failure==1)invalid.texts[0].contours.clear();
        if(failure==2)invalid.texts[0].contours[0][0][0]=std::numeric_limits<double>::infinity();
        bool rejected=false;try{interchange::export_dxf(path,invalid);}catch(const interchange::DxfExportError&){rejected=true;}
        require(rejected&&bytes(path)==protected_bytes,"Invalid detail export replaced an existing destination");
    }
    sketch.corner_radii[0].suppressed=true;interchange::export_dxf(dir/"suppressed.dxf",sketch);
    std::size_t arcs=0;double length=0;for(const auto& entity:test::read_dxf_entities(dir/"suppressed.dxf")) {
        if(entity.type=="ARC")++arcs;if(entity.type=="LINE")length+=std::hypot(entity.number(11)-entity.number(10),entity.number(21)-entity.number(20));
    }
    require(arcs==0&&std::abs(length-20)<1e-8,"Suppressed corner was materialized or lines remained shortened");
    auto two=sketcher::Sketch::create_default();const auto bottom=two.add_segment(0,0,10,0),left=two.add_segment(0,0,0,10),right=two.add_segment(10,0,10,10);
    static_cast<void>(two.add_corner_fillet(bottom,left,2));static_cast<void>(two.add_corner_fillet(bottom,right,3));interchange::export_dxf(dir/"two.dxf",two);
    double total_radius=0;length=0;for(const auto& entity:test::read_dxf_entities(dir/"two.dxf")) {
        if(entity.type=="ARC")total_radius+=entity.number(40);if(entity.type=="LINE")length+=std::hypot(entity.number(11)-entity.number(10),entity.number(21)-entity.number(20));
        require(entity.type!="POINT","Two corner treatments exported an orphan handle");
    }
    require(std::abs(total_radius-5)<1e-8&&std::abs(length-20)<1e-8,"Two corners on a shared segment lost their independent tangent trims");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-dxf-detail-"+kernel::make_stable_id());fs::create_directory(dir);verify(dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);std::cout<<"DXF text outlines, corner arcs, tangent trims and atomic failure passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
