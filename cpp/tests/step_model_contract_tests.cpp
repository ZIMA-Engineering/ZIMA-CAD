#include <zima/interchange/step_model.hpp>
#include <zima/interchange/step.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>
#include <sstream>
#include <STEPCAFControl_Writer.hxx>
#include <DESTEP_Parameters.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDataStd_Name.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
namespace {
using namespace zima;
void require(bool test,const char* message){if(!test)throw std::runtime_error(message);}
using V=kernel::Vec3;
V placed(V p,const kernel::StepProduct& n) {
    constexpr double r=3.14159265358979323846/180;
    const auto rotate=[&](double& a,double& b,double angle){const auto x=a,y=b;a=x*std::cos(angle)-y*std::sin(angle);b=x*std::sin(angle)+y*std::cos(angle);};
    rotate(p.y,p.z,n.rotation_degrees.x*r);rotate(p.z,p.x,n.rotation_degrees.y*r);rotate(p.x,p.y,n.rotation_degrees.z*r);
    return {p.x+n.translation.x,p.y+n.translation.y,p.z+n.translation.z};
}
std::pair<V,V> bounds(const std::vector<V>& points) {
    V lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};
    for(auto p:points){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}return {lo,hi};
}
void same_bounds(const std::vector<V>& a,const std::vector<V>& b) {
    const auto [al,ah]=bounds(a);const auto [bl,bh]=bounds(b);
    require(std::max({std::abs(al.x-bl.x),std::abs(al.y-bl.y),std::abs(al.z-bl.z),std::abs(ah.x-bh.x),std::abs(ah.y-bh.y),std::abs(ah.z-bh.z)})<1e-6,
        "STEP changed a rotation, translation or unit scale");
}
}
int main() {
    try {
        kernel::OcctKernel kernel;
        const auto directory=std::filesystem::temp_directory_path()/("zima-step-"+document::PartDocument::create_default().document_id);
        std::filesystem::create_directory(directory);
        kernel::StepProduct a;a.definition_id="part-a";a.name="Třmen";a.body=kernel.make_box({10,20,30});
        auto a2=a;a2.name="Třmen 2";a2.translation={30,20,10};a2.rotation_degrees={17,23,31};
        kernel::StepProduct sub;sub.definition_id="sub";sub.name="Podsestava";sub.children={a,a2};sub.translation={100,25,30};sub.rotation_degrees={20,35,47};
        auto sub2=sub;sub2.name="Podsestava 2";sub2.translation={-70,60,25};sub2.rotation_degrees={-25,10,80};
        kernel::StepProduct b;b.definition_id="part-b";b.name="Deska";b.body=kernel.make_box({8,5,3});b.translation={15,-35,5};
        kernel::StepProduct root;root.definition_id="root";root.name="STEP sestava";root.children={sub,sub2,b};
        const auto source=directory/"assembly.step";kernel.export_step(root,source.string());
        const auto nodes=interchange::inspect_step_parts(source);
        std::set<std::string> definitions;std::size_t leaves{};
        for(const auto& node:nodes)if(!node.assembly){++leaves;definitions.insert(node.definition_id);}
        require(leaves==5&&definitions.size()==2,"STEP lost repeated products or the hierarchy");
        std::vector<V> expected;
        for(const auto& group:{sub,sub2})for(const auto& leaf:group.children)for(auto p:leaf.body.mesh.vertices)expected.push_back(placed(placed(p,leaf),group));
        for(auto p:b.body.mesh.vertices)expected.push_back(placed(p,b));
        auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},source);
        require(imported.document.body_history.bodies().size()==5,"Part import did not create one Body per STEP Part occurrence");
        require(imported.calculated.back().body_outputs.size()==5,"STEP Bodies were merged in calculation");
        require(std::abs(imported.calculated.back().volume-(4*a.body.volume+b.body.volume))<1e-6,"STEP changed part volume or overlapped-body accounting");
        same_bounds(expected,imported.calculated.back().mesh.vertices);
        auto extended=interchange::import_step_part(imported.document,imported.calculated,source);
        require(extended.document.body_history.bodies().size()==10&&std::abs(extended.calculated.back().volume-2*imported.calculated.back().volume)<1e-6,
            "A second STEP import fused or replaced existing Bodies");
        const auto part_path=directory/"model.prtz";imported.document.save(part_path,imported.calculated);
        const auto package=interchange::import_step_assembly(source,directory,imported.document.document_precision);
        require(package.parts.size()==2&&package.assemblies.size()==2,"Assembly import duplicated definitions or flattened subassemblies");
        for(const auto& part:package.parts) {
            require(part.document.body_history.bodies().size()==1&&part.document.history.size()==1,"Imported STEP Part is outside a Body");
            part.document.save(part.path,part.calculated);
        }
        for(const auto& assembly:package.assemblies)assembly.document.save(assembly.path);
        require(!package.root_occurrence.calculated_source.kernel_shape.empty()&&
            std::abs(package.root_occurrence.calculated_source.volume-imported.calculated.back().volume)<1e-6,
            "Imported root assembly lacks a calculated solid snapshot");
        const auto& top=package.assemblies.at(package.root_index);
        require(top.document.components.size()==3&&top.document.components[0].source_document_id==top.document.components[1].source_document_id,
            "Repeated subassembly does not share its source document");
        const auto saved=assembly::AssemblyDocument::load(top.path);
        const auto again=directory/"roundtrip.step";
        kernel.export_step(interchange::step_product(saved),again.string());
        const auto roundtrip=interchange::import_step_part(document::PartDocument::create_default(),{},again);
        require(roundtrip.document.body_history.bodies().size()==5,"Saved Assembly lost its nested solids");
        same_bounds(expected,roundtrip.calculated.back().mesh.vertices);
        auto first_active=imported.document;auto export_graph=first_active.body_history;
        export_graph.activate(export_graph.bodies().front().scope.id);first_active.set_body_history(export_graph);
        require(interchange::step_product(first_active,imported.calculated).children.size()==5,
            "Part STEP export depends on the active Body");
        auto hidden=*export_graph.find(export_graph.active_body_id());hidden.visible=false;export_graph.update_body(hidden);
        auto hidden_doc=first_active;hidden_doc.set_body_history(export_graph);
        require(interchange::step_product(hidden_doc,imported.calculated).children.size()==4,
            "STEP export included a hidden active Body");
        kernel.export_step(interchange::step_product(first_active,imported.calculated),(directory/"bodies.step").string());
        const auto bodies=interchange::import_step_part(document::PartDocument::create_default(),{},directory/"bodies.step");
        require(bodies.document.body_history.bodies().size()==5,"Part export merged separate Bodies");
        same_bounds(expected,bodies.calculated.back().mesh.vertices);
        // One product with a solid and a sheet is a Part, even when XCAF
        // expands its compound representation. Repeated uses share that Part.
        Handle(TDocStd_Document) mixed_doc;
        const auto mixed_app=XCAFApp_Application::GetApplication();
        mixed_app->NewDocument("BinXCAF",mixed_doc);
        const auto mixed_shapes=XCAFDoc_DocumentTool::ShapeTool(mixed_doc->Main());
        BRep_Builder mixed_builder;TopoDS_Compound mixed_shape, mixed_root;
        mixed_builder.MakeCompound(mixed_shape);mixed_builder.MakeCompound(mixed_root);
        mixed_builder.Add(mixed_shape,BRepPrimAPI_MakeBox(10,20,30).Shape());
        mixed_builder.Add(mixed_shape,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,35),gp_Dir(0,0,1)),0,10,0,20).Shape());
        const auto screw_label=mixed_shapes->AddShape(mixed_shape,false);
        TDataStd_Name::Set(screw_label,"Solid with thread sheet");
        const auto mixed_root_label=mixed_shapes->AddShape(mixed_root,true);
        mixed_shapes->AddComponent(mixed_root_label,screw_label,TopLoc_Location());
        gp_Trsf second_screw;second_screw.SetTranslation(gp_Vec(40,0,0));
        mixed_shapes->AddComponent(mixed_root_label,screw_label,TopLoc_Location(second_screw));
        mixed_shapes->UpdateAssemblies();
        STEPCAFControl_Writer mixed_writer;
        const auto mixed_path=directory/"solid-and-sheet.step";
        require(mixed_writer.Transfer(mixed_doc)&&mixed_writer.Write(mixed_path.string().c_str())==IFSelect_RetDone,"Cannot write solid/sheet fixture");
        mixed_app->Close(mixed_doc);
        const auto mixed=interchange::import_step_assembly(mixed_path,directory,{});
        require(mixed.parts.size()==1&&mixed.assemblies.size()==1,"Solid and sheet became separate Parts or a fake subassembly");
        require(mixed.assemblies.front().document.components.size()==2,"Repeated mixed product occurrences were lost");
        TopoDS_Shape restored_mixed;
        std::istringstream mixed_brep(mixed.parts.front().calculated.back().kernel_shape);
        BRepTools::Read(restored_mixed,mixed_brep,mixed_builder);
        int solid_count=0,face_count=0;
        for(TopExp_Explorer e(restored_mixed,TopAbs_SOLID);e.More();e.Next())++solid_count;
        for(TopExp_Explorer e(restored_mixed,TopAbs_FACE);e.More();e.Next())++face_count;
        require(solid_count==1&&face_count==7,"Single Part lost its solid or auxiliary sheet");
        // An independently written inch STEP must become exactly 25.4 x 50.8 x 76.2 mm.
        Handle(TDocStd_Document) inch_doc;const auto app=XCAFApp_Application::GetApplication();app->NewDocument("BinXCAF",inch_doc);
        XCAFDoc_DocumentTool::SetLengthUnit(inch_doc,0.001);
        XCAFDoc_DocumentTool::ShapeTool(inch_doc->Main())->AddShape(BRepPrimAPI_MakeBox(25.4,50.8,76.2).Shape(),false);
        STEPCAFControl_Writer writer;DESTEP_Parameters parameters;parameters.WriteUnit=UnitsMethods_LengthUnit_Inch;
        const auto inch_path=directory/"inch.step";
        require(writer.Transfer(inch_doc,parameters)&&writer.Write(inch_path.string().c_str())==IFSelect_RetDone,"Cannot create inch fixture");app->Close(inch_doc);
        const auto inch=interchange::import_step_part(document::PartDocument::create_default(),{},inch_path);
        const auto [lo,hi]=bounds(inch.calculated.back().mesh.vertices);
        require(std::abs(hi.x-lo.x-25.4)<1e-6&&std::abs(hi.y-lo.y-50.8)<1e-6&&std::abs(hi.z-lo.z-76.2)<1e-6,"Inch STEP was not converted to millimetres");
        const auto flat_path=directory/"flat.step";
        kernel.export_step(std::vector<kernel::PlacedBody>{{a.body,{},{}},{b.body,{40,0,0},{}}},flat_path.string());
        const auto flat=interchange::import_step_part(document::PartDocument::create_default(),{},flat_path);
        require(flat.document.body_history.bodies().size()==2,"A flat STEP compound did not become separate Part Bodies");
        auto combined=flat.document;auto combined_graph=combined.body_history;
        const auto boolean_id=combined_graph.create_boolean("Součet",kernel::BodyCombination::Add,
            combined_graph.bodies()[0].scope.id,combined_graph.bodies()[1].scope.id);
        combined.set_body_history(combined_graph);const auto combined_result=kernel.evaluate_history(combined.kernel_operations());
        const auto boolean_export=interchange::step_product(combined,combined_result);
        require(boolean_export.children.size()==1&&boolean_export.children.front().definition_id==boolean_id,
            "STEP export repeated the consumed Boolean operands");
        const auto flat_assembly=interchange::import_step_assembly(flat_path,directory,imported.document.document_precision);
        require(flat_assembly.parts.size()==2&&flat_assembly.assemblies.at(flat_assembly.root_index).document.components.size()==2,
            "A flat STEP compound did not become a flat Assembly");
        std::filesystem::remove(source);
        const auto frozen=document::PartDocument::load(part_path);
        const auto regenerated=kernel.evaluate_history(frozen.kernel_operations());same_bounds(expected,regenerated.back().mesh.vertices);
        require(frozen.body_history.bodies().size()==5,"Body ownership did not survive save/reopen");
        std::filesystem::remove_all(directory);
        std::cout<<"STEP hierarchy, repeated definitions, Bodies, placements, units and frozen persistence passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
