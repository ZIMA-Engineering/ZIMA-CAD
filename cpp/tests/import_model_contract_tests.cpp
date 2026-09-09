#include <zima/interchange/model_import.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/workspace/workspace.hpp>
#include <IGESControl_Writer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
using namespace zima;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int main() {
 try {
    const auto temp=std::filesystem::temp_directory_path()/("zima-import-"+document::PartDocument::create_default().document_id);
    std::filesystem::create_directories(temp);
    const auto fixtures=std::filesystem::path(ZIMA_IMPORT_FIXTURES);
    require(interchange::format_from_path("part.IGES")==interchange::Format::Iges,"IGES extension");
    require(interchange::supports(interchange::Format::Iges,interchange::Direction::Import,interchange::Context::Assembly),"IGES assembly support");
    require(!interchange::supports(interchange::Format::Iges,interchange::Direction::Import,interchange::Context::Sketch),"IGES inside sketch");
    require(!interchange::supports(interchange::Format::Iges,interchange::Direction::Export,interchange::Context::Part),"IGES export accidentally enabled");
    auto source=sketcher::Sketch::create_default();
    source.add_segment(0,0,20,0);source.add_segment(20,0,20,10);source.add_segment(20,10,0,10);source.add_segment(0,10,0,0);
    interchange::export_dxf(temp/"rectangle.dxf",source);
    auto first=interchange::import_dxf_part(document::PartDocument::create_default(),{},temp/"rectangle.dxf");
    auto& doc=first.part.document;
    require(doc.body_history.bodies().size()==1 && doc.history.size()==1 && doc.sketches.size()==1,"DXF must create Body/Sketch");
    require(doc.history.front().feature_kind==document::FeatureKind::Sketch &&
        doc.sketches.front().owner_container_id==doc.history.front().id && doc.sketches.front().import_blocks.size()==1,"DXF block ownership");
    doc.validate_body_ownership();
    const auto body_id=doc.body_history.active_body_id();
    auto second=interchange::import_dxf_part(doc,{},temp/"rectangle.dxf");
    require(second.part.document.body_history.bodies().size()==1 && second.part.document.history.size()==2,"Active Body must receive new Sketch");
    auto graph=doc.body_history;graph.activate({});doc.set_body_history(std::move(graph));
    auto third=interchange::import_dxf_part(doc,{},temp/"rectangle.dxf");
    require(third.part.document.body_history.bodies().size()==2,"No active Body must create one");
    auto active=interchange::import_dxf_part(doc,{},temp/"rectangle.dxf",first.sketch_id);
    require(active.part.document.history.size()==1 && active.part.document.sketches.front().import_blocks.size()==2,"Active Sketch import");
    const auto& blocks=active.part.document.sketches.front().import_blocks;
    for(const auto& id:blocks[0].point_ids)require(std::ranges::find(blocks[1].point_ids,id)==blocks[1].point_ids.end(),
        "Independent DXF blocks share writable points");
    auto extruded=first.part.document;
    auto& extrusion=extruded.history.front();extrusion.feature_kind=document::FeatureKind::Extrusion;
    extrusion.extrusion.sketch_id=extruded.sketches.front().id;
    extrusion.extrusion.length_forward=5;extrusion.extrusion.height=5;
    kernel::OcctKernel extrusion_kernel;
    auto extrusion_result=extrusion_kernel.evaluate_history(extruded.kernel_operations());
    require(std::abs(extrusion_result.back().volume-1000)<1e-5,"DXF rectangle must form a usable extrusion profile");
    doc.save(temp/"profile.prtz",{});
    auto loaded=document::PartDocument::load(temp/"profile.prtz");
    require(loaded.sketches.front().import_blocks==doc.sketches.front().import_blocks,"DXF persistence");
    const auto online=interchange::import_dxf_part(document::PartDocument::create_default(),{},fixtures/"ac1003-line.dxf");
    require(online.report.imported_entities>0 && online.report.warnings.empty(),"Online DXF did not import fully");
    std::cout<<"Online DXF: "<<online.report.imported_entities<<" entities\n";
    // Header units and both polyline encodings, including clockwise bulges.
    const auto write=[&](const char* name,const std::string& data){std::ofstream(temp/name)<<data;};
    const std::string header="0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    const std::string end="0\nENDSEC\n0\nEOF\n";
    write("poly.dxf",header+"0\nLWPOLYLINE\n90\n2\n70\n0\n10\n0\n20\n0\n42\n-1\n10\n2\n20\n0\n"+end);
    auto poly=sketcher::Sketch::create_default();interchange::import_dxf(temp/"poly.dxf",poly);
    require(poly.arcs.size()==1 && std::abs(poly.find_point(poly.arcs[0].center_point_id)->x-25.4)<1e-9,"Polyline bulge or inch conversion");
    write("oldpoly.dxf",header+"0\nPOLYLINE\n70\n1\n0\nVERTEX\n10\n0\n20\n0\n0\nVERTEX\n10\n2\n20\n0\n0\nVERTEX\n10\n0\n20\n1\n0\nSEQEND\n"+end);
    auto oldpoly=sketcher::Sketch::create_default();interchange::import_dxf(temp/"oldpoly.dxf",oldpoly);
    require(oldpoly.segments.size()==3,"Closed R12 polyline");
    for(const auto& bad : {std::string("0\nSECTION\n2"),header+"0\nLINE\n10\nnan\n20\n0\n11\n1\n21\n0\n"+end,
            header+"0\nLINE\n10\n0\n20\n0\n30\n5\n11\n1\n21\n0\n"+end}) {
        write("bad.dxf",bad);const auto before=poly.serialized();bool rejected=false;
        try{interchange::import_dxf(temp/"bad.dxf",poly);}catch(const std::exception&){rejected=true;}
        require(rejected && poly.serialized()==before,"Invalid DXF changed target");
    }
    // Sketch-only Part must be a visible, selectable Assembly occurrence and
    // dependency changes are pulled only by explicit Regenerate.
    workspace::Workspace workspace;
    auto assembly=assembly::AssemblyDocument::create_default();const auto asm_id=assembly.document_id;
    const auto part_id=doc.document_id;
    workspace.add_assembly(assembly,temp/"owner.asmz"); workspace.add_part(doc,{},temp/"profile.prtz");
    const auto occurrence=workspace.insert_open_part(asm_id,part_id,"DXF");
    const auto sum_x=[](const kernel::ViewerMesh& mesh){double sum=0;for(const auto& edge:mesh.edges)for(const auto& p:edge.points)sum+=p.x;return sum;};
    const auto before_scene=workspace.open_assembly(asm_id)->session.document().build_scene();
    require(!before_scene.edges.empty(),"DXF Part invisible in Assembly");
    require(std::ranges::any_of(before_scene.edges,[&](const auto& edge){return !edge.reference.instance_path.empty();}),"DXF occurrence identity missing");
    auto changed=workspace.open_part(part_id)->session.document();
    changed.sketches.front().transform_import_block(changed.sketches.front().import_blocks.front().id,5,0,0);
    workspace.open_part(part_id)->session.commit(changed,{});
    require(workspace.open_assembly(asm_id)->session.document().build_scene().edges.front().points.front().x==before_scene.edges.front().points.front().x,"Implicit dependent regeneration");
    workspace.regenerate_assembly_from_open_dependencies(asm_id);
    require(sum_x(workspace.open_assembly(asm_id)->session.document().build_scene())!=sum_x(before_scene),"Explicit regeneration missed DXF");
    auto top=assembly::AssemblyDocument::create_default();const auto top_id=top.document_id;
    workspace.add_assembly(top,temp/"top.asmz");
    workspace.insert_open_assembly(top_id,asm_id,"Nested");
    workspace.display_top_level(top_id);workspace.activate(asm_id);
    const auto online_id=online.part.document.document_id;
    workspace.add_part(online.part.document,{},temp/"online.prtz");
    workspace.insert_open_part(asm_id,online_id,"Online DXF");
    require(workspace.open_assembly(top_id)->session.document().components.size()==1 &&
        workspace.open_assembly(asm_id)->session.document().components.size()==2 && workspace.displayed_document_id()==top_id,"Nested ownership or display changed");
    // External IGES plus a known closed solid. Persist source identities and
    // regenerate successfully after deleting the source IGES.
    kernel::OcctKernel kernel;
    auto cube=interchange::import_iges_part(document::PartDocument::create_default(),{},fixtures/"cube-10mm.igs");
    require(!cube.calculated.empty() && !cube.calculated.back().mesh.triangles.empty(),"Online IGES empty");
    std::cout<<"Online IGES: volume="<<cube.calculated.back().volume<<", topology="<<cube.document.history[0].imported_step.topology.size()<<"\n";
    require(std::abs(cube.calculated.back().volume-1000)<1e-4,"Online 10mm cube scale");
    IGESControl_Writer writer("MM",1);writer.AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    require(writer.Write((temp/"box.igs").string().c_str()),"IGES fixture write");
    auto solid=interchange::import_iges_part(document::PartDocument::create_default(),{},temp/"box.igs");
    require(std::abs(solid.calculated.back().volume-6000)<1e-5,"IGES solid volume");
    const auto identities=solid.document.history.front().imported_step.topology;
    require(identities.size()==26,"IGES box needs six faces, twelve edges and eight vertices");
    for(const auto& identity:identities)require(identity.semantic_key.starts_with("iges:") && identity.semantic_key.find(":de:")!=std::string::npos,"IGES identity lacks source parent");
    solid.document.save(temp/"box.prtz",solid.calculated);std::filesystem::remove(temp/"box.igs");
    std::vector<kernel::BodyResult> snapshots;auto saved=document::PartDocument::load(temp/"box.prtz",&snapshots);
    require(saved.history.front().imported_step.topology==identities,"IGES topology persistence");
    auto regenerated=kernel.evaluate_history(saved.kernel_operations());
    require(std::abs(regenerated.back().volume-6000)<1e-5,"Frozen IGES regeneration");
    require(!regenerated.back().mesh.original_references.edges.empty(),"IGES references lost on regeneration");
    auto added=interchange::import_iges_part(saved,regenerated,fixtures/"cube-10mm.igs");
    require(added.document.body_history.bodies().size()==2,"IGES import overwrote existing Part");
    const auto solid_id=saved.document_id;workspace.add_part(saved,regenerated,temp/"box.prtz");
    workspace.insert_open_part(asm_id,solid_id,"IGES");
    require(workspace.open_assembly(asm_id)->session.document().components.back().calculated_source.volume>5999,"IGES Assembly snapshot");
    IGESControl_Writer wire_writer("MM",0);
    wire_writer.AddShape(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(12,0,0)).Shape());
    require(wire_writer.Write((temp/"wire.igs").string().c_str()),"Wire IGES write");
    auto wire=interchange::import_iges_part(document::PartDocument::create_default(),{},temp/"wire.igs");
    require(wire.calculated.back().mesh.triangles.empty() && !wire.calculated.back().mesh.edges.empty(),"Wire IGES lost or fabricated a solid");
    // Invalid IGES never changes the input transaction.
    write("bad.igs","invalid IGES");bool rejected_iges=false;
    try{interchange::import_iges_part(saved,regenerated,temp/"bad.igs");}catch(const std::exception&){rejected_iges=true;}
    require(rejected_iges && saved.history.size()==1,"Invalid IGES mutated target");
    std::filesystem::remove_all(temp);
    std::cout<<"IGES/DXF model contracts passed\n";return 0;
 } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
