#include <zima/document/sheet_transition.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/sheet_transition_operations.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <iostream>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void solid(const kernel::BodyResult& body) {
    for(const auto& [owner,error]:body.calculation_errors)throw std::runtime_error("Attached transition calculation failed: "+error);
    check(body.volume>0,"Attached transition has no material");
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(body.kernel_shape);
    BRepTools::Read(shape,data,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid attached transition B-Rep");
    int count=0;for(TopExp_Explorer i(shape,TopAbs_SOLID);i.More();i.Next())++count;
    check(count==1,"Attachment or development split the sheet into separate solids");
}
kernel::ViewerEdge attachment(const kernel::BodyResult& body,const std::string& owner,bool transition,const std::string& endpoint={}) {
    for(const auto& edge:body.mesh.original_references.edges) {
        if(edge.reference.owner_id!=owner || !edge.measured_length || *edge.measured_length<5)continue;
        if(!endpoint.empty()&&edge.reference.semantic_key.find(endpoint)==std::string::npos)continue;
        if(transition && edge.reference.semantic_key.find("rectangle-rim")==std::string::npos &&
                edge.reference.semantic_key.find("first-rim")==std::string::npos)continue;
        try {if(document::bend_sheet_references(edge).size()==3)return edge;}catch(const std::exception&){}
    }
    throw std::runtime_error("No original attachable sheet boundary");
}
void verify(int mode,bool chain) {
    std::cout<<"Transition attachment mode="<<mode<<" Bend chain="<<chain<<std::endl;
    auto doc=document::PartDocument::create_default();static_cast<void>(doc.body_history.create_body("Attachments"));
    const auto id=doc.document_id;workspace::Workspace live;kernel::OcctKernel kernel;
    live.add_part(doc);live.activate(id);
    auto source=document::create_sheet_transition(mode>=2);
    if(mode>=2)document::set_rectangular_transition_sides(source,mode==2?2:3);
    if(mode>0)source.sheet_transition.end_rotation={8,-10,0};
    check(workspace::commit_sheet_transition(live,kernel,id,source),"Transition creation failed");
    auto* state=live.open_part(id);
    auto edge=attachment(state->session.calculated_boundaries().back(),source.id,true);
    std::string bend_id;
    if(chain) {
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        bend.bend.sheet_attachment=true;bend.bend.radius=3;bend.bend.angle_degrees=30;
        bend.placement.references=document::bend_sheet_references(edge);
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=bend.id;bend.bend.sketch_id=sketch.id;
        document::initialize_bend_start_profile(sketch,*edge.measured_length);
        check(workspace::commit_bend(live,kernel,id,bend,sketch),"Bend on transition failed");
        solid(state->session.calculated_boundaries().back());bend_id=bend.id;
        const auto* stored=state->session.document().find_container(bend.id);
        const auto trajectory=sketcher::Sketch::from_serialized(stored->bend.auxiliary_sketches[0]);
        edge=attachment(state->session.calculated_boundaries().back(),bend.id,false,trajectory.arcs.back().end_point_id);
    }
    auto twist=document::PartDocument::create_twisted_sheet_container();
    twist.twisted_sheet.sheet_attachment=true;twist.twisted_sheet.width=*edge.measured_length;
    twist.twisted_sheet.thickness=edge.edge_treatment_side_references.front().sheet_thickness;
    twist.twisted_sheet.length=80;twist.twisted_sheet.angle_degrees=30;
    twist.placement.references=document::bend_sheet_references(edge);
    check(workspace::commit_primitive(live,kernel,id,twist,workspace::PrimitiveEditMode::Create),"Twisted Sheet attachment failed");
    solid(state->session.calculated_boundaries().back());
    const double formed_volume=state->session.calculated_boundaries().back().volume;
    const auto authored=*state->session.document().find_container(twist.id);
    for(bool unfold:{true,false}) {
        auto change=document::PartDocument::create_sketch_container();
        std::cout<<(unfold?"Unbend":"Bend Back")<<std::endl;
        change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
        change.sheet_state.all=true;
        check(workspace::commit_sheet_state(live,kernel,id,change),"Attached sheet state change failed");
        solid(state->session.calculated_boundaries().back());
        check(*state->session.document().find_container(twist.id)==authored,"State operation modified authored Twisted Sheet");
        const auto path=std::filesystem::path("build/transition-model")/
            ("attachment-"+std::to_string(mode)+(chain?"-bend-twist":"-twist")+(unfold?"-flat.prtz":"-back.prtz"));
        std::filesystem::create_directories(path.parent_path());
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);
        kernel::OcctKernel cold;const auto recalculated=workspace::calculate_part_with_resolved_references(cold,reopened);
        solid(recalculated.back());
        check(std::abs(recalculated.back().volume-saved.back().volume)<1e-5,"Cold regeneration changed attached sheet volume");
        check(reopened.find_container(twist.id)->placement.references==authored.placement.references,"Reopen lost original attachment identities");
        if(!unfold)check(std::abs(recalculated.back().volume-formed_volume)<.05,"Bend Back changed formed sheet material");
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),"Attached state Undo/Redo failed");
    }
}
}
int main(int argc,char** argv)try {
    for(int mode=argc>1?std::stoi(argv[1]):0;mode<4;++mode)for(bool chain:{false,true})verify(mode,chain);
    std::cout<<"Transition/Twisted Sheet and transition/Bend/Twisted Sheet development matrix passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
