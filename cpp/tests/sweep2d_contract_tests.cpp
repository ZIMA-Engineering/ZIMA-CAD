#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/helical_geometry.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static document::HistoryContainer fixture(bool arc=false,bool open=false){
    auto c=document::PartDocument::create_sweep2d_container();
    auto section=sketcher::Sketch::from_serialized(c.sweep2d.sketches[0]);
    section.plane=sketcher::SketchPlane::XY;section.refresh_default_frame();
    if(open)static_cast<void>(section.add_segment(-2,0,2,0));else static_cast<void>(section.add_circle(0,0,2));
    c.sweep2d.sketches[0]=section.serialized();
    auto path=sketcher::Sketch::from_serialized(c.sweep2d.sketches[1]);
    if(arc)static_cast<void>(path.add_arc(10,0,0,0,10,10,false,1e-6,true));
    else static_cast<void>(path.add_segment(0,0,0,20));
    c.sweep2d.sketches[1]=path.serialized();document::PartDocument::reframe_sweep2d_sketches(c);return c;
}
int main(){try{
    kernel::OcctKernel kernel;
    auto doc=document::PartDocument::create_default();
    for(bool arc:{false,true}){
        auto c=fixture(arc);doc.history={c};
        auto bodies=kernel.evaluate_history(doc.kernel_operations());
        const double expected=4*std::numbers::pi*(arc?5*std::numbers::pi:20);
        std::cout<<"solid "<<arc<<" volume "<<bodies.back().volume<<" expected "<<expected<<std::endl;
        require(std::abs(bodies.back().volume-expected)<expected*.002,"Solid volume mismatch");
        std::set<std::string> caps;for(const auto& r:bodies.back().mesh.original_references.triangle_references)if(r.semantic_key.starts_with("start:from:")||r.semantic_key.starts_with("end:from:"))caps.insert(r.semantic_key);
        require(caps.size()==2,"Missing persistent end caps");
        const auto file=std::filesystem::temp_directory_path()/"zima-sweep2d-contract.prtz";doc.save(file,bodies);std::vector<kernel::BodyResult> loaded_bodies;
        auto loaded=document::PartDocument::load(file,&loaded_bodies);require(loaded.history.back().sweep2d==c.sweep2d,"Sweep parameters changed after save/load");
        c.placement.rotation_y=35;c.placement.x=7;doc.history={c};auto rotated=kernel.evaluate_history(doc.kernel_operations());require(std::abs(rotated.back().volume-expected)<expected*.002,"Container rotation changed volume");
        for(const auto& cap:caps)require(std::ranges::any_of(rotated.back().mesh.original_references.triangle_references,[&](const auto& r){return r.semantic_key==cap;}),"Cap identity changed on placement edit");
    }
    for(bool open:{false,true})for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}){
        auto c=fixture(false,open);c.sweep2d.result_type=document::ProfileResultType::Thin;c.sweep2d.thickness=.5;c.sweep2d.thin_mode=mode;doc.history={c};
        auto bodies=kernel.evaluate_history(doc.kernel_operations());std::cout<<"thin "<<open<<" mode "<<int(mode)<<" volume "<<bodies.back().volume<<std::endl;
        const double area=open?2:mode==document::ThinMode::Symmetric?2*std::numbers::pi:mode==document::ThinMode::OneSide?1.75*std::numbers::pi:2.25*std::numbers::pi;
        require(std::abs(bodies.back().volume-area*20)<.02,"Thin volume mismatch");
    }
    // Plane references must be perpendicular and pass through the shared origin.
    auto box=document::PartDocument::create_box_container();
    box.box.length=40;box.box.width=40;box.box.height=40;box.placement.x=20;box.placement.y=20;box.placement.z=20;
    auto base_doc=document::PartDocument::create_default();base_doc.history={box};
    auto base_bodies=kernel.evaluate_history(base_doc.kernel_operations());
    const auto& geometry=base_bodies.back().mesh.original_references;
    const auto face=[&](const std::string& key){for(const auto& ref:geometry.triangle_references)if(ref.semantic_key==key)return ref;throw std::runtime_error("Missing box reference "+key);};
    auto attached=fixture();attached.sweep2d.path_plane=face("y_min");
    document::PartDocument::resolve_sweep2d_planes(attached,geometry);
    static_cast<void>(document::PartDocument::sweep2d_request(attached));
    for(const auto& key:{"z_max","x_max"}){auto invalid=attached;invalid.sweep2d.path_plane=face(key);bool rejected=false;try{document::PartDocument::resolve_sweep2d_planes(invalid,geometry);}catch(...){rejected=true;}require(rejected,"Invalid path plane accepted");}
    auto subtract=fixture();subtract.placement.x=10;subtract.placement.y=10;subtract.combine_mode=document::CombineMode::Subtract;base_doc.history.push_back(subtract);
    auto cut=kernel.evaluate_history(base_doc.kernel_operations());require(std::abs(cut.front().volume-cut.back().volume-80*std::numbers::pi)<.01,"Sweep subtraction failed");
    // Smooth curved law and a tangent line/arc chain.
    for(bool spline:{false,true}){
        auto c=fixture();auto path=sketcher::Sketch::from_serialized(c.sweep2d.sketches[1]);path.segments.clear();
        if(spline)static_cast<void>(path.add_bspline({{0,0},{0,5},{5,10},{10,10}}));
        else{static_cast<void>(path.add_segment(0,0,0,10));static_cast<void>(path.add_arc(10,10,0,10,10,20,false,1e-6,true));}
        c.sweep2d.sketches[1]=path.serialized();doc.history={c};require(kernel.evaluate_history(doc.kernel_operations()).back().volume>0,"Curved path failed");
    }
    for(bool rectangle:{false,true}){
        auto c=fixture();auto profile=sketcher::Sketch::from_serialized(c.sweep2d.sketches[0]);profile.circles.clear();
        if(rectangle){for(const auto& p:std::vector<std::array<double,4>>{{-3,-2,3,-2},{3,-2,3,2},{3,2,-3,2},{-3,2,-3,-2}})static_cast<void>(profile.add_segment(p[0],p[1],p[2],p[3]));}
        else static_cast<void>(profile.add_arc(0,0,3,0,0,3));
        c.sweep2d.sketches[0]=profile.serialized();c.sweep2d.result_type=document::ProfileResultType::Thin;c.sweep2d.thickness=.5;doc.history={c};
        const double expected=rectangle?200:15*std::numbers::pi;
        auto body=kernel.evaluate_history(doc.kernel_operations()).back();std::cout<<"Thin shaped "<<rectangle<<" volume "<<body.volume<<" expected "<<expected<<std::endl;
        require(std::abs(body.volume-expected)<.02,"Curved/rectangular Thin volume mismatch");
    }
    auto collapsed=fixture();collapsed.sweep2d.result_type=document::ProfileResultType::Thin;collapsed.sweep2d.thickness=10;collapsed.sweep2d.thin_mode=document::ThinMode::OneSide;doc.history={collapsed};
    bool collapsed_rejected=false;try{static_cast<void>(kernel.evaluate_history(doc.kernel_operations()));}catch(...){collapsed_rejected=true;}require(collapsed_rejected,"Collapsed Thin contour accepted");
    auto preview=fixture(false,true);preview.sweep2d.result_type=document::ProfileResultType::Thin;preview.sweep2d.thin_mode=document::ThinMode::OneSide;preview.sweep2d.thickness=.5;
    const auto request=document::PartDocument::sweep2d_request(preview);const auto& loop=std::get<kernel::ExtrusionRequest::CurvedProfile>(request.sections.front().profile.outer_profile);const auto line=std::get<kernel::ExtrusionRequest::LineCurve>(loop.curves.front());
    const double side=line.end.x>line.start.x?1:-1;
    for(const auto& edge:document::PartDocument::sweep2d_preview_edges(preview))for(const auto& p:edge.points)require(p.y*side>=-1e-6,"Thin preview reversed thickness side");
    auto bad=fixture();auto path=sketcher::Sketch::from_serialized(bad.sweep2d.sketches[1]);path.segments.clear();static_cast<void>(path.add_segment(0,0,10,20));bad.sweep2d.sketches[1]=path.serialized();bool rejected=false;try{static_cast<void>(document::PartDocument::sweep2d_request(bad));}catch(...){rejected=true;}require(rejected,"Oblique initial tangent accepted");
    std::cout<<"2D Sweep contracts passed"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
