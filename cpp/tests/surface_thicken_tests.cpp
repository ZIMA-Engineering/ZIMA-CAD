#include <zima/document/surface_thicken.hpp>
#include <zima/document/boundary_surface.hpp>
#include <zima/workspace/surface_thicken_operations.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/kernel/surface_results.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include "../app/surface_thicken_selection.hpp"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-4){if(std::abs(a-b)>tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
kernel::BoundarySurfaceRequest rectangle(double height=0.) {
    kernel::BoundarySurfaceRequest r;r.region_id="region";
    const std::array<kernel::Vec3,4> p{{{0,0,height},{10,0,height},{10,20,height},{0,20,height}}};
    for(unsigned i=0;i<4;++i){auto& b=r.boundaries[i];r.source_owners[i]="source"+std::to_string(i);
        b.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{p[i],p[(i+1)%4]}}};
        b.outer_edge_source_ids={"curve"};b.outer_vertex_source_ids={"a"};b.open_profile_end_id="b";b.direction={0,0,1};}
    return r;
}
std::set<std::string> identities(const kernel::BodyResult& result) {
    std::set<std::string> keys;
    for(const auto& face:result.mesh.original_references.triangle_references)if(face.owner_id=="thick")keys.insert("f:"+face.semantic_key);
    for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="thick")keys.insert("e:"+edge.reference.semantic_key);
    for(const auto& point:result.mesh.original_references.points)if(point.reference.owner_id=="thick")keys.insert("v:"+point.reference.semantic_key);
    return keys;
}
void valid(const kernel::BodyResult& result,unsigned solid_count=1) {
    check(result.calculation_errors.empty(),"Thickening reported a calculation failure");
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(result.kernel_shape);BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape,true,false,true).IsValid(),"Thickening produced an invalid exact B-Rep");
    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    check(solids==solid_count&&result.volume>0.,"Thickening did not create the expected solids");
}
}
int main(){try{
    kernel::OcctKernel kernel;kernel::SurfaceThickenRequest request{{"surface","surface:from:region",{}},2.};
    std::set<std::string> keys;std::array<double,2> centers{};
    for(unsigned mode=0;mode<3;++mode) {
        request.side=static_cast<kernel::SurfaceThicknessSide>(mode);
        const std::vector<kernel::HistoryOperation> history{{"surface",rectangle()},{"thick",request}};
        const auto result=kernel.evaluate_history(history).back();valid(result);near(result.volume,400.);
        check(std::ranges::none_of(result.mesh.triangle_references,[](const auto& face){return face.surface_result;}),"The consumed surface remained in the calculated free-surface result");
        check(std::ranges::none_of(result.mesh.original_references.triangle_references,[](const auto& face){return face.owner_id=="thick"&&face.surface_result;}),"New solid original faces remained classified as surfaces");
        check(result.volume_integrals.has_value(),"Thickened solid lost mass properties");
        if(mode<2)centers[mode]=result.volume_integrals->centroid.z;else near(result.volume_integrals->centroid.z,0.);
        const auto ids=identities(result);check(ids.size()==26,"Thickening lost cap, wall, rim, longitudinal or point identities");
        if(keys.empty())keys=ids;else check(ids==keys,"Changing side renamed native children");
        const auto solid_edge=std::ranges::find_if(result.mesh.original_references.edges,[](const auto& edge){return edge.reference.owner_id=="thick"&&edge.reference.semantic_key.starts_with("thicken:longitudinal:");});
        check(solid_edge!=result.mesh.original_references.edges.end(),"New solid has no native edge for a dependent Fillet");
        auto rounded_history=history;rounded_history.push_back({"round",kernel::FilletRequest{{solid_edge->reference},0.2}});
        const auto rounded=kernel.evaluate_history(rounded_history).back();valid(rounded);
        check(rounded.volume<400.&&rounded.volume>399.,"Dependent Fillet failed on the thickened solid");
        kernel::OcctKernel cold;const auto rebuilt=cold.evaluate_history_incremental(history,kernel.evaluate_history(history)).back();
        valid(rebuilt);near(rebuilt.volume,400.);check(identities(rebuilt)==keys,"Cold topology lost its parent identities");
    }
    near(centers[0]+centers[1],0.);near(std::abs(centers[0]),1.);
    std::cout<<"Planar offsets and native children passed"<<std::endl;
    // Normal thickness is independently measured against an exact annular sector,
    // rather than approximating its volume as source area times thickness.
    kernel::ExtrusionRequest arc;arc.surface_result=true;arc.direction={0,0,10};arc.profile_region_id="arc-region";
    arc.outer_edge_source_ids={"arc"};arc.outer_vertex_source_ids={"p0"};arc.open_profile_end_id="p1";
    arc.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{10,0,0},{10/std::sqrt(2.),10/std::sqrt(2.),0},{0,10,0}}}};
    request.face={"curve","generated:arc",{}};
    std::array<double,2> volumes{};
    for(unsigned mode=0;mode<3;++mode){request.side=static_cast<kernel::SurfaceThicknessSide>(mode);
        const auto result=kernel.evaluate_history({{"curve",arc},{"thick",request}}).back();valid(result);
        if(mode<2)volumes[mode]=result.volume;else near(result.volume,100*std::numbers::pi);
    }
    near(std::min(volumes[0],volumes[1]),90*std::numbers::pi);near(std::max(volumes[0],volumes[1]),110*std::numbers::pi);
    auto cylinder=arc;cylinder.open_profile_end_id.clear();cylinder.outer_edge_source_ids={"circle"};cylinder.outer_vertex_source_ids={"circle-point"};
    cylinder.outer_profile=kernel::ExtrusionRequest::CircleProfile{{0,0,0},10.};
    auto cylindrical_thickness=request;cylindrical_thickness.face={"cylinder","generated:circle",{}};std::array<double,2> cylinder_volumes{};
    for(unsigned mode=0;mode<3;++mode){cylindrical_thickness.side=static_cast<kernel::SurfaceThicknessSide>(mode);
        const auto result=kernel.evaluate_history({{"cylinder",cylinder},{"thick",cylindrical_thickness}}).back();valid(result);
        if(mode<2)cylinder_volumes[mode]=result.volume;else near(result.volume,400*std::numbers::pi);}
    near(std::min(cylinder_volumes[0],cylinder_volumes[1]),360*std::numbers::pi);near(std::max(cylinder_volumes[0],cylinder_volumes[1]),440*std::numbers::pi);
    std::cout<<"Curved and periodic-cylinder offsets passed"<<std::endl;
    auto spatial=rectangle();
    for(auto& boundary:spatial.boundaries){auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves[0]);
        if(line.start.x==10.&&line.start.y==20.)line.start.z=1.;if(line.end.x==10.&&line.end.y==20.)line.end.z=1.;}
    auto spatial_thickness=request;spatial_thickness.face={"patch","surface:from:region",{}};spatial_thickness.thickness=1.;
    for(unsigned mode=0;mode<3;++mode){spatial_thickness.side=static_cast<kernel::SurfaceThicknessSide>(mode);
        const auto result=kernel.evaluate_history({{"patch",spatial},{"thick",spatial_thickness}}).back();valid(result);check(result.volume>195.&&result.volume<205.,"Spatial patch thickness has an implausible volume");}
    std::cout<<"Spatial B-spline offsets passed"<<std::endl;
    // Reversing the source orientation reverses First / Second, not stored mode.
    arc.direction={0,0,-10};request.side=kernel::SurfaceThicknessSide::First;
    const auto reversed=kernel.evaluate_history({{"curve",arc},{"thick",request}}).back();valid(reversed);near(reversed.volume,volumes[1]);
    request.face={"surface","surface:from:region",{}};request.side=kernel::SurfaceThicknessSide::Symmetric;
    const auto mixed=kernel.evaluate_history({{"surface",rectangle()},{"other",rectangle(30)},{"thick",request}}).back();valid(mixed);near(mixed.volume,400.);
    check(kernel::has_surface_results(mixed.mesh)&&std::ranges::any_of(mixed.mesh.triangle_references,[](const auto& f){return f.owner_id=="other"&&f.surface_result;}),"Thickening consumed an unrelated surface");
    auto source_edit=rectangle();
    for(auto& boundary:source_edit.boundaries){auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves[0]);
        if(line.start.x==10.)line.start.x=15.;if(line.end.x==10.)line.end.x=15.;}
    const auto resized=kernel.evaluate_history_incremental({{"surface",source_edit},{"thick",request}},kernel.evaluate_history({{"surface",rectangle()},{"thick",request}})).back();
    valid(resized);near(resized.volume,600.);check(identities(resized)==keys,"Source edit reused stale geometry or renamed native children");
    check(std::ranges::any_of(resized.mesh.original_references.points,[](const auto& p){return p.reference.owner_id=="thick"&&p.position.x>14.9;}),"Source edit reused a stale original-reference mesh");
    // A selected sheet adds material through the ordinary Boolean path. Contact
    // and overlap fuse; disconnected solids and unselected loose sheets remain.
    kernel::ExtrusionRequest box;box.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{0,0,0},{10,0,0},{10,20,0},{0,20,0}}};box.direction={0,0,4};
    box.profile_region_id="box-region";box.outer_edge_source_ids={"a","b","c","d"};box.outer_vertex_source_ids={"p0","p1","p2","p3"};
    auto add=request;add.side=kernel::SurfaceThicknessSide::First;
    const auto direction_probe=kernel.evaluate_history({{"surface",rectangle(4)},{"thick",add}}).back();
    if(direction_probe.volume_integrals->centroid.z<4.)add.side=kernel::SurfaceThicknessSide::Second;
    const auto touching=kernel.evaluate_history({{"box",box},{"surface",rectangle(4)},{"thick",add}}).back();valid(touching);near(touching.volume,1200.);
    const auto overlapping=kernel.evaluate_history({{"box",box},{"surface",rectangle(3)},{"thick",add}}).back();valid(overlapping);near(overlapping.volume,1000.);
    kernel::ShellRequest skin;skin.thickness=0.;const auto skin_input=kernel.evaluate_history({{"box",box},{"skin",skin}});
    const auto face=std::ranges::find_if(skin_input.back().mesh.triangle_references,[](const auto& value){return value.owner_id=="skin"&&value.surface_result;});check(face!=skin_input.back().mesh.triangle_references.end(),"Skin test has no selectable surface");
    auto partial=request;partial.face=*face;
    const auto partial_skin=kernel.evaluate_history({{"box",box},{"skin",skin},{"thick",partial}}).back();valid(partial_skin);
    std::set<std::string> remaining_skin;
    for(const auto& value:partial_skin.mesh.triangle_references)if(value.owner_id=="skin"&&value.surface_result)remaining_skin.insert(value.semantic_key);
    check(remaining_skin.empty(),"Thickening a connected skin left unthickened sibling faces");
    // Selecting either patch of one sewn shell consumes the entire sheet.
    // Independent sheets stay surfaces and native children do not depend on
    // which patch was used as the selection anchor.
    auto adjacent=rectangle();
    for(auto& boundary:adjacent.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.x+=10.;line.end.x+=10.;
    }
    kernel::SurfaceSewingRequest sew;sew.faces={{"left","surface:from:region",{}},{"right","surface:from:region",{}}};
    const std::vector<kernel::HistoryOperation> sewn_history{{"left",rectangle()},{"right",adjacent},{"sewn",sew}};
    const auto sewn_input=kernel.evaluate_history(sewn_history);
    std::map<std::string,kernel::FaceReference> anchors;
    for(const auto& f:sewn_input.back().mesh.triangle_references)if(f.owner_id=="sewn")anchors.emplace(f.semantic_key,f);
    check(anchors.size()==2,"Sewn thickening fixture did not preserve two patches");
    app::SurfaceThickenSelection selected_shell(sewn_input.back().mesh,{});
    for(const auto& [name,anchor]:anchors)check(selected_shell.faces(anchor).size()==2,"Persisted shell selection omitted an adjacent sewn patch");
    std::set<std::string> sewn_keys;
    for(unsigned mode=0;mode<3;++mode)for(const auto& [name,anchor]:anchors) {
        auto history=sewn_history;auto whole=request;whole.face=anchor;whole.side=static_cast<kernel::SurfaceThicknessSide>(mode);
        history.push_back({"thick",whole});const auto result=kernel.evaluate_history(history).back();valid(result);near(result.volume,800.);
        check(std::ranges::none_of(result.mesh.triangle_references,[](const auto& f){return f.surface_result;}),"Sewn sibling remained a loose sheet after thickening");
        const auto ids=identities(result);if(sewn_keys.empty())sewn_keys=ids;else check(ids==sewn_keys,"Changing the selected sewn patch or side renamed thickening children");
        kernel::OcctKernel rebuilt_kernel;const auto rebuilt=rebuilt_kernel.evaluate_history_incremental(history,kernel.evaluate_history(history)).back();
        valid(rebuilt);near(rebuilt.volume,800.);check(identities(rebuilt)==ids,"Cold sewn-shell calculation changed native children");
    }
    auto corner=rectangle();const std::array<kernel::Vec3,4> corner_points{{{10,0,0},{10,20,0},{10,20,10},{10,0,10}}};
    for(unsigned i=0;i<4;++i)corner.boundaries[i].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{corner_points[i],corner_points[(i+1)%4]}}};
    auto bent_history=sewn_history;bent_history[1].primitive=corner;
    const auto bent_input=kernel.evaluate_history(bent_history);
    auto whole=request;whole.face=*std::ranges::find_if(bent_input.back().mesh.triangle_references,[](const auto& f){return f.owner_id=="sewn";});
    std::array<double,3> bent_volumes{};
    for(unsigned mode=0;mode<3;++mode) {
        whole.side=static_cast<kernel::SurfaceThicknessSide>(mode);auto history=bent_history;history.push_back({"thick",whole});
        const auto result=kernel.evaluate_history(history).back();valid(result);bent_volumes[mode]=result.volume;
        check(result.volume>700&&result.volume<900,"Corner shell thickening did not cover both sheets");
    }
    near(bent_volumes[0]+bent_volumes[1],1600.);near(bent_volumes[2],800.);
    std::cout<<"Connected skin and sewn-shell offsets passed"<<std::endl;
    const auto invalid=[&](auto bad){bool rejected=false;try{static_cast<void>(kernel.evaluate_history({{"surface",rectangle()},{"thick",bad}}));}catch(const std::exception&){rejected=true;}check(rejected,"Invalid thickening was accepted");};
    auto bad=request;bad.thickness=0.;invalid(bad);bad.thickness=-1.;invalid(bad);bad=request;bad.face.semantic_key="missing";invalid(bad);bad=request;bad.side=static_cast<kernel::SurfaceThicknessSide>(3);invalid(bad);
    // Native history, exact save/reopen, prerequisites, no-op and atomic Undo/Redo.
    auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Thickening"));part.set_body_history(graph);
    auto sketch=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;
    auto surface=document::create_boundary_surface();const std::array<std::array<double,4>,4> lines{{{0,0,10,0},{10,0,10,20},{10,20,0,20},{0,20,0,0}}};
    for(unsigned i=0;i<4;++i)surface.boundary_surface.boundaries[i]={owner.id,sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3])};
    for(const auto& f:{owner,surface}){part.insert_history_entry(document::PartHistoryKind::Feature,f.id);part.history.push_back(f);}part.sketches.push_back(sketch);part.resolve_constructions();
    workspace::Workspace live;live.add_part(part,kernel.evaluate_history(part.kernel_operations()));live.activate(part.document_id);
    auto feature=document::create_surface_thicken();feature.surface_thicken={{surface.id,"surface:from:"+surface.feature_id,{}},2.,kernel::SurfaceThicknessSide::Symmetric};
    check(workspace::commit_surface_thicken(live,kernel,part.document_id,feature),"Native thickening did not commit");
    auto* state=live.open_part(part.document_id);const auto before=state->session.document().serialized();
    check(!workspace::commit_surface_thicken(live,kernel,part.document_id,feature)&&before==state->session.document().serialized(),"Unchanged thickening created a transaction");
    near(state->session.calculated_boundaries().back().volume,400.);
    check(workspace::part_history_dependencies(state->session.document()).contains({surface.id,feature.id}),"Thickening lost its prerequisite");
    check(!state->session.document().dimension_identifiers.identifier(feature.id,"parameter:thickness").empty(),"Thickening thickness has no dimension identity");
    const auto family=workspace::family_references(live,part.document_id);
    check(std::ranges::any_of(family,[&](const auto& value){return value.binding.owner_id==feature.id&&value.binding.semantic_key=="parameter:thickness";}),"Family did not expose thickening thickness");
    auto changed=feature;changed.surface_thicken.thickness=3.;check(workspace::commit_surface_thicken(live,kernel,part.document_id,changed),"Thickness edit did not commit");near(state->session.calculated_boundaries().back().volume,600.);
    check(workspace::step_part_document_history(live,part.document_id,false),"Thickness Undo failed");near(state->session.calculated_boundaries().back().volume,400.);
    check(workspace::step_part_document_history(live,part.document_id,true),"Thickness Redo failed");near(state->session.calculated_boundaries().back().volume,600.);
    changed.surface_thicken.thickness=3.5;changed.value_locks.insert("thickness");
    check(workspace::commit_surface_thicken(live,kernel,part.document_id,changed),"Edited thickness could not be locked on the same confirmation");
    near(state->session.calculated_boundaries().back().volume,700.);
    const auto locked_family=workspace::family_references(live,part.document_id);
    check(std::ranges::none_of(locked_family,[&](const auto& value){return value.binding.owner_id==feature.id&&value.binding.semantic_key=="parameter:thickness";}),"Family offered a locked thickness");
    auto locked_edit=changed;locked_edit.surface_thicken.thickness=4.;bool lock_rejected=false;
    try{static_cast<void>(workspace::commit_surface_thicken(live,kernel,part.document_id,locked_edit));}catch(const std::exception&){lock_rejected=true;}
    check(lock_rejected,"Locked native thickness could be changed");
    const auto path=std::filesystem::temp_directory_path()/"zima-surface-thicken.prtz";state->session.document().save(path,state->session.calculated_boundaries());std::vector<kernel::BodyResult> cache;
    const auto reopened=document::PartDocument::load(path,&cache);std::filesystem::remove(path);
    check(reopened.find_container(feature.id)->surface_thicken==changed.surface_thicken,"Native side or thickness did not round trip");
    kernel::OcctKernel cold;const auto rebuilt=cold.evaluate_history_incremental(reopened.kernel_operations(),cache).back();valid(rebuilt);near(rebuilt.volume,700.);
    changed.surface_thicken.face.semantic_key="missing";bool rejected=false;try{static_cast<void>(workspace::commit_surface_thicken(live,kernel,part.document_id,changed));}catch(const std::exception&){rejected=true;}
    check(rejected&&state->session.document().serialized()==reopened.serialized(),"Invalid edit changed native history");
    std::cout<<"Surface thickening: exact planes and curved offsets, all sides, native ancestry, context, persistence, no-op and Undo/Redo passed\n";
    return EXIT_SUCCESS;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}
