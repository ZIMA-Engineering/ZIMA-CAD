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
static void close(double actual,double expected,const char* message){
    if(std::abs(actual-expected)>std::max(.02,std::abs(expected)*.002)) {
        std::cerr<<message<<": "<<actual<<" expected "<<expected<<std::endl;throw std::runtime_error(message);
    }
}
static std::size_t profile_at(document::HistoryContainer& c,const document::Curve3DStation& station,double radius,bool open=false) {
    const auto i=document::PartDocument::ensure_sweep2d_profile(c,station.point_id,station.incoming);
    auto sketch=sketcher::Sketch::from_serialized(c.sweep2d.profiles[i].sketch_serialized);
    if(open)static_cast<void>(sketch.add_segment(-radius,0,radius,0));
    else static_cast<void>(sketch.add_circle(0,0,radius));
    c.sweep2d.profiles[i].sketch_serialized=sketch.serialized();return i;
}
static document::HistoryContainer fixture(bool arc=false,bool open=false){
    auto c=document::PartDocument::create_sweep2d_container();
    auto path=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
    if(arc)static_cast<void>(path.add_arc(10,0,0,0,10,10,false,1e-6,true));
    else static_cast<void>(path.add_segment(0,0,0,20));
    c.sweep2d.path_sketch=path.serialized();document::PartDocument::reframe_sweep2d_sketches(c);
    profile_at(c,document::PartDocument::sweep2d_route(c).stations.front(),2,open);return c;
}
int main(){try{
    kernel::OcctKernel kernel;
    auto doc=document::PartDocument::create_default();
    const auto calculate=[&](const auto& c){doc.history={c};return kernel.evaluate_history(doc.kernel_operations()).back();};
    for(bool arc:{false,true}) {
        auto c=fixture(arc);const double length=arc?5*std::numbers::pi:20;
        const auto body=calculate(c);close(body.volume,4*std::numbers::pi*length,"Constant profile Sweep volume");
        const auto guide=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
        const auto source=arc?guide.arcs.front().id:guide.segments.front().id;
        require(std::ranges::count_if(body.mesh.edges,[&](const auto& e){return e.reference.semantic_key=="centerline:from:"+source&&e.dash_dot;})==1,
            "Source curve centerline was lost or split into approximation identities");
        std::set<std::string> caps;
        for(const auto& r:body.mesh.original_references.triangle_references)if(r.semantic_key.starts_with("sweep:cap:"))caps.insert(r.semantic_key);
        require(caps.size()==2,"Missing persisted Sweep end caps");
        c.placement.rotation_y=35;c.placement.x=7;
        close(calculate(c).volume,body.volume,"Container placement changed Sweep volume");
        const auto file=std::filesystem::temp_directory_path()/"zima-sweep2d-loft-contract.prtz";
        doc.save(file);auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
        close(kernel.evaluate_history(loaded.kernel_operations()).back().volume,body.volume,"Reload changed Sweep geometry");
        require(loaded.history.front().sweep2d.profiles.front().id==c.sweep2d.profiles.front().id,"Profile identity changed during persistence");
        profile_at(c,document::PartDocument::sweep2d_route(c).stations.back(),3);
        close(calculate(c).volume,std::numbers::pi*length*(4+6+9)/3,"Variable circular Loft volume");
    }
    for(bool open:{false,true})for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}) {
        auto c=fixture(false,open);c.sweep2d.result_type=document::ProfileResultType::Thin;c.sweep2d.thickness=.5;c.sweep2d.thin_mode=mode;
        const double area=open?2:mode==document::ThinMode::Symmetric?2*std::numbers::pi:mode==document::ThinMode::OneSide?1.75*std::numbers::pi:2.25*std::numbers::pi;
        close(calculate(c).volume,area*20,"Thin direction or thickness changed");
        const auto last=document::PartDocument::sweep2d_route(c).stations.back();profile_at(c,last,3,open);
        if(open) {
            for(auto& profile:c.sweep2d.profiles){auto sketch=sketcher::Sketch::from_serialized(profile.sketch_serialized);
                profile.correspondence_start_point_id=sketch.segments.front().first_point_id;}
            close(calculate(c).volume,2.5*20,"Open Thin Loft volume");
        } else {
            const double r=mode==document::ThinMode::OneSide?2:mode==document::ThinMode::OtherSide?2.5:2.25;
            const auto frustum=[](double a,double b){return std::numbers::pi*20*(a*a+a*b+b*b)/3;};
            close(calculate(c).volume,frustum(r,r+1)-frustum(r-.5,r+.5),"Closed Thin Loft volume");
        }
        const auto original=c.sweep2d.profiles.front().sketch_serialized;
        static_cast<void>(document::PartDocument::sweep2d_preview_mesh(c));
        require(c.sweep2d.profiles.front().sketch_serialized==original,"Preview mutated source Sketch");
    }
    // C and K correspondence share the same profile implementation as 3D.
    for(bool keypoints:{false,true}) {
        auto c=fixture();auto& profile=c.sweep2d.profiles.front();
        auto sketch=sketcher::Sketch::from_serialized(profile.sketch_serialized);
        for(unsigned q=0;q<4;++q){const double a=q*std::numbers::pi/2;const auto point=sketch.add_point(2*std::cos(a),2*std::sin(a));
            if(keypoints)static_cast<void>(sketch.add_point_reference_constraint(point,"sketch_keypoint:circle:"+sketch.circles.front().id+":"+std::to_string(q)));
            else static_cast<void>(sketch.add_point_on_circle_constraint(point,sketch.circles.front().id));}
        profile.sketch_serialized=sketch.serialized();
        const auto index=document::PartDocument::ensure_sweep2d_profile(c,document::PartDocument::sweep2d_route(c).stations.back().point_id,true);
        auto rectangle=sketcher::Sketch::from_serialized(c.sweep2d.profiles[index].sketch_serialized);static_cast<void>(rectangle.add_rectangle(-2,-2,2,2));
        c.sweep2d.profiles[index].sketch_serialized=rectangle.serialized();
        require(calculate(c).volume>0,"C/K circle to rectangle Loft failed");
        const auto preview=document::PartDocument::sweep2d_preview_mesh(c);
        require(preview.constraint_markers.size()==8&&preview.constraint_markers.front().label=="1 – začátek",
            "Numbered profile correspondence preview is missing");
        c.sweep2d.result_type=document::ProfileResultType::Thin;c.sweep2d.thickness=.2;
        require(calculate(c).volume>0,"C/K Thin Loft failed");
    }
    // Keep the original 2D Sweep support for closed profiles with holes.
    auto hollow=fixture();
    auto add_hole=[](document::Sweep3DProfile& p,double radius){auto sketch=sketcher::Sketch::from_serialized(p.sketch_serialized);
        static_cast<void>(sketch.add_circle(0,0,radius));p.sketch_serialized=sketch.serialized();};
    add_hole(hollow.sweep2d.profiles.front(),1);close(calculate(hollow).volume,60*std::numbers::pi,"Annular profile Sweep volume");
    profile_at(hollow,document::PartDocument::sweep2d_route(hollow).stations.back(),3);
    add_hole(hollow.sweep2d.profiles.back(),1.5);
    close(calculate(hollow).volume,std::numbers::pi*20/3*(4+6+9-1-1.5-2.25),"Annular profile Loft volume");
    // Real points inside a segment become persistent, independently editable stations.
    auto marked=fixture();auto guide=sketcher::Sketch::from_serialized(marked.sweep2d.path_sketch);
    const auto middle=guide.add_point(0,10);marked.sweep2d.path_sketch=guide.serialized();
    auto route=document::PartDocument::sweep2d_route(marked);
    require(route.stations.size()==4&&route.stations[1].point_id==middle&&route.stations[2].point_id==middle,"Interior path point did not create stations");
    profile_at(marked,route.stations[1],3);profile_at(marked,route.stations[2],3);profile_at(marked,route.stations.back(),4);
    close(calculate(marked).volume,std::numbers::pi*10/3*(4+6+9+9+12+16),"Piecewise multi-profile Loft volume");
    // Referencing a path plane is independent of all container placement rows.
    auto attached=fixture();const auto geometry=doc.origin_viewer_mesh().original_references;
    const auto plane_ref=std::ranges::find_if(geometry.triangle_references,[](const auto& r){return r.semantic_key=="origin:plane:xz";});
    require(plane_ref!=geometry.triangle_references.end(),"Missing origin plane fixture");
    attached.sweep2d.path_plane=document::ConstructionReference{plane_ref->instance_path,plane_ref->owner_id,plane_ref->semantic_key};
    const auto placement=attached.placement;const auto first_id=attached.sweep2d.profiles.front().id;
    document::PartDocument::resolve_sweep2d_planes(attached,geometry);
    auto plane=sketcher::Sketch::from_serialized(attached.sweep2d.path_sketch);
    require(std::abs(plane.resolved_normal.y)>1-1e-7,"Path plane reference did not determine the plane");
    require(attached.placement==placement,"Path plane change changed container placement");
    attached.sweep2d.path_plane->semantic_key="origin:plane:yz";
    document::PartDocument::resolve_sweep2d_planes(attached,geometry);
    plane=sketcher::Sketch::from_serialized(attached.sweep2d.path_sketch);
    require(std::abs(plane.resolved_normal.x)>1-1e-7&&attached.sweep2d.profiles.front().id==first_id,"Changing path plane lost profile identity");
    close(calculate(attached).volume,80*std::numbers::pi,"Path plane change altered volume");
    auto invalid=attached;invalid.sweep2d.path_plane->owner_id=invalid.id;
    bool rejected=false;try{document::PartDocument::resolve_sweep2d_planes(invalid,geometry);}catch(...){rejected=true;}
    require(rejected,"Self-referencing path plane was accepted");
    // Child path planes use the owning Origin without modifying placement.
    for(const auto* key:{"origin:plane:xy","origin:plane:yz","origin:plane:xz"}) {
        auto local=fixture();local.placement.x=12;local.placement.y=-3;local.placement.z=8;local.placement.rotation_y=25;
        const auto placement=local.placement;
        local.sweep2d.path_plane=document::ConstructionReference{{},local.container_origin.id,key};
        document::PartDocument::resolve_sweep2d_planes(local,{});
        const auto sketch=sketcher::Sketch::from_serialized(local.sweep2d.path_sketch);
        require(local.placement==placement,"Own path plane changed shared container placement");
        close(sketch.resolved_origin.x,12,"Own plane lost X translation");
        close(sketch.resolved_origin.y,-3,"Own plane lost Y translation");
        close(sketch.resolved_origin.z,8,"Own plane lost Z translation");
        close(calculate(local).volume,80*std::numbers::pi,"Own path plane changed sweep volume");
        local.placement.x=29;document::PartDocument::resolve_sweep2d_planes(local,{});
        close(sketcher::Sketch::from_serialized(local.sweep2d.path_sketch).resolved_origin.x,29,"Own path plane did not follow its parent");
    }
    // Arbitrary initial direction is valid; the profile plane follows its tangent.
    auto oblique=document::PartDocument::create_sweep2d_container();guide=sketcher::Sketch::from_serialized(oblique.sweep2d.path_sketch);
    static_cast<void>(guide.add_segment(0,0,12,16));oblique.sweep2d.path_sketch=guide.serialized();
    profile_at(oblique,document::PartDocument::sweep2d_route(oblique).stations.front(),2);
    close(calculate(oblique).volume,80*std::numbers::pi,"Oblique initial tangent changed profile plane");
    // A smooth Sketch spline remains curved across every approximation span.
    auto spline=document::PartDocument::create_sweep2d_container();guide=sketcher::Sketch::from_serialized(spline.sweep2d.path_sketch);
    static_cast<void>(guide.add_bspline({{0,0},{0,10},{10,20},{20,20}}));spline.sweep2d.path_sketch=guide.serialized();
    profile_at(spline,document::PartDocument::sweep2d_route(spline).stations.front(),1);
    require(calculate(spline).volume>0,"Spline Sweep failed");
    profile_at(spline,document::PartDocument::sweep2d_route(spline).stations.back(),2);
    require(calculate(spline).volume>0,"Spline Loft failed");
    auto collapsed=fixture();collapsed.sweep2d.result_type=document::ProfileResultType::Thin;collapsed.sweep2d.thickness=10;
    rejected=false;try{static_cast<void>(calculate(collapsed));}catch(...){rejected=true;}require(rejected,"Collapsing thickness was accepted");
    std::cout<<"2D Sweep/Loft contracts passed"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
