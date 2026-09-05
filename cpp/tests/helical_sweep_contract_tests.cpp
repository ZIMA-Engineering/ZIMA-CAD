#include <zima/document/part_document.hpp>
#include <zima/document/helical_geometry.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <numbers>
#include <filesystem>
#include <set>
using namespace zima;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static document::HistoryContainer fixture(double end_radius=10,bool rectangle=false){
    auto c=document::PartDocument::create_helical_sweep_container();
    auto base=sketcher::Sketch::from_serialized(c.helical.sketches[0]);
    base.plane=sketcher::SketchPlane::XY;base.refresh_default_frame();
    c.helical.circle_id=base.add_circle(0,0,10);c.helical.start_point_id=base.add_point(10,0);
    c.helical.sketches[0]=base.serialized();
    auto law=sketcher::Sketch::from_serialized(c.helical.sketches[1]);
    static_cast<void>(law.add_segment(0,0,end_radius-10,12.5));c.helical.sketches[1]=law.serialized();
    auto section=sketcher::Sketch::from_serialized(c.helical.sketches[2]);
    if(rectangle){for(auto a:std::vector<std::array<double,4>>{{-.5,-.3,.5,-.3},{.5,-.3,.5,.3},{.5,.3,-.5,.3},{-.5,.3,-.5,-.3}})static_cast<void>(section.add_segment(a[0],a[1],a[2],a[3]));}
    else static_cast<void>(section.add_circle(0,0,.5));
    c.helical.sketches[2]=section.serialized();return c;
}
int main(){try{
    kernel::OcctKernel k;
    {
        auto doc=document::PartDocument::create_default();doc.history={fixture()};
        doc.document_precision["linear_tolerance"]="0.01";
        const auto coarse=doc.kernel_operations();
        doc.document_precision["linear_tolerance"]="0.00001";
        const auto fine=doc.kernel_operations();
        const auto& a=std::get<kernel::Sweep3DRequest>(coarse.front().primitive);
        const auto& b=std::get<kernel::Sweep3DRequest>(fine.front().primitive);
        require(a.linear_tolerance==0.01&&b.linear_tolerance==0.00001,
            "Document tolerance did not reach sweep request");
        require(b.path_segments.size()>a.path_segments.size(),
            "Sweep approximation ignored document precision");
        require(kernel::history_fingerprint(coarse,1)!=kernel::history_fingerprint(fine,1),
            "Precision change reused a stale body fingerprint");
        auto cylinder=document::PartDocument::create_cylinder_container();doc.history={cylinder};
        doc.document_precision["mesh_deflection"]="0.1";
        const auto rough=k.evaluate_history(doc.kernel_operations());
        doc.document_precision["mesh_deflection"]="0.001";
        const auto smooth=k.evaluate_history_incremental(doc.kernel_operations(),rough);
        const auto point_count=[](const auto& body){std::size_t count=0;for(const auto& edge:body.mesh.edges)count+=edge.points.size();return count;};
        require(point_count(smooth.back())>point_count(rough.back()),
            "Edge sampling ignored mesh deviation or reused coarse cache");
        require(smooth.back().mesh.triangles.size()>rough.back().mesh.triangles.size(),
            "Surface tessellation ignored mesh deviation");
        require(std::abs(smooth.back().volume-rough.back().volume)<1e-8,
            "Display precision changed body geometry");
    }

    for(bool left:{false,true})for(bool rectangle:{false,true}){
        auto c=fixture(10,rectangle);c.helical.left_handed=left;
        auto p=document::helical_geometry::path(c);
        require(std::abs(p.at(0,1).x+10)<1e-7&&std::abs(p.at(0,1).z-12.5)<1e-7,"Partial-turn endpoint incorrect");
        auto doc=document::PartDocument::create_default();doc.history={c};
        const auto result=k.evaluate_history(doc.kernel_operations());
        if(!rectangle){
            require(std::ranges::any_of(result.back().mesh.original_references.edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("seam:generated:")&&edge.points.size()>33;}),
                "Long helical wire still uses a fixed 33-point approximation");
        }
        std::set<std::string> caps;
        for(const auto& ref:result.back().mesh.original_references.triangle_references)
            if(ref.owner_id==c.id&&(ref.semantic_key.starts_with("start:from:")||ref.semantic_key.starts_with("end:from:"))){
                caps.insert(ref.semantic_key);
                const auto tangent=document::helical_geometry::unit(p.derivative(0,ref.semantic_key.starts_with("start:")?0:1));
                require(ref.surface&&std::abs(document::helical_geometry::dot(tangent,ref.surface->axis))>1-1e-6,"Cap is not normal to the endpoint tangent");
            }
        require(caps.size()==2,"Start/end caps lost their persisted profile-region parents");
        if(!left&&!rectangle){
            const auto file=std::filesystem::temp_directory_path()/"zima-helical-contract.prtz";
            doc.save(file,result);std::vector<kernel::BodyResult> loaded_bodies;
            auto loaded=document::PartDocument::load(file,&loaded_bodies);
            require(loaded.history.back().feature_kind==document::FeatureKind::HelicalSweep,"Helical type not persisted");
            require(loaded.history.back().helical.start_point_id==c.helical.start_point_id,"Start Point identity not persisted");
            std::set<std::string> loaded_caps;
            for(const auto& ref:loaded_bodies.back().mesh.original_references.triangle_references)if(caps.contains(ref.semantic_key))loaded_caps.insert(ref.semantic_key);
            require(loaded_caps==caps,"Cap references changed after save/load");
            auto changed=c;changed.helical.pitch=6;changed.helical.left_handed=true;
            auto guide=sketcher::Sketch::from_serialized(changed.helical.sketches[1]);
            for(auto& point:guide.points)if(point.id!=changed.helical.guide_start_point_id)point.y*=1.2;
            changed.helical.sketches[1]=guide.serialized();doc.history={changed};
            auto regenerated=k.evaluate_history(doc.kernel_operations());
            std::set<std::string> new_caps;
            for(const auto& ref:regenerated.back().mesh.original_references.triangle_references)if(caps.contains(ref.semantic_key)){
                new_caps.insert(ref.semantic_key);require(ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane,"Cap has no saved plane for downstream attachment");
                auto next=fixture();auto base=sketcher::Sketch::from_serialized(next.helical.sketches[0]);base.plane=sketcher::SketchPlane::XZ;next.helical.sketches[0]=base.serialized();document::ConstructionReference attachment{ref.instance_path,ref.owner_id,ref.semantic_key};attachment.orientation_drives_rotation=true;attachment.orientation_role="front";next.placement.references={attachment};
                require(document::resolve_placement(next.placement,regenerated.back().mesh.original_references),"Cap placement failed");document::PartDocument::reframe_helical_sketches(next,0);
                const auto plane=sketcher::Sketch::from_serialized(next.helical.sketches[0]);
                require(std::abs(document::helical_geometry::dot(plane.resolved_normal,ref.surface->axis))>1-1e-6,"FRONT placement did not orient the base Sketch");
                require(std::abs(document::helical_geometry::dot(document::helical_geometry::sub(plane.resolved_origin,ref.surface->origin),ref.surface->axis))<1e-5,"Downstream plane attachment lost changed cap");
            }
            require(new_caps==caps,"Pitch, height or handedness exchanged cap identities");
        }
        const double length=2.5*std::hypot(20*std::numbers::pi,5.);
        const double expected=length*(rectangle?.6:std::numbers::pi*.25);
        std::cout<<"volume "<<result.back().volume<<" expected "<<expected<<std::endl;
        require(std::abs(result.back().volume-expected)<expected*.002,"Helical solid volume mismatch");
    }
    auto c=fixture(7);auto p=document::helical_geometry::path(c);require(std::abs(p.at(0,1).x+7)<1e-7,"Cone radius lost");
    auto doc=document::PartDocument::create_default();doc.history={c};require(k.evaluate_history(doc.kernel_operations()).back().volume>0,"Conical sweep failed");
    auto reverse=fixture();auto reverse_law=sketcher::Sketch::from_serialized(reverse.helical.sketches[1]);
    for(auto& pt:reverse_law.points)pt.y=-pt.y;reverse.helical.sketches[1]=reverse_law.serialized();
    doc.history={reverse};require(k.evaluate_history(doc.kernel_operations()).back().volume>0,"Negative axial guide failed");
    auto subtract=fixture();subtract.combine_mode=document::CombineMode::Subtract;
    auto block=document::PartDocument::create_box_container();block.box={40,40,40};
    doc.history={block,subtract};const auto subtraction=k.evaluate_history(doc.kernel_operations());
    require(subtraction.back().volume<subtraction.front().volume,"Helical subtract did not remove material");
    for(int shape:{0,1}){
        auto curved=fixture();auto law=sketcher::Sketch::create_default();law.owner_container_id=curved.id;
        curved.helical.guide_start_point_id=law.add_point(0,0);
        if(shape==0)static_cast<void>(law.add_arc(-5,0,0,0,-1,3));
        else static_cast<void>(law.add_bspline({{0,0},{1,3},{0,6},{2,8}}));
        curved.helical.sketches[1]=law.serialized();doc.history={curved};
        require(k.evaluate_history(doc.kernel_operations()).back().volume>0,"Curved radial law failed");
    }
    auto hollow=fixture();auto section=sketcher::Sketch::from_serialized(hollow.helical.sketches[2]);
    static_cast<void>(section.add_circle(0,0,.25));hollow.helical.sketches[2]=section.serialized();doc.history={hollow};
    const auto hollow_body=k.evaluate_history(doc.kernel_operations()).back();
    require(std::abs(hollow_body.volume-2.5*std::hypot(20*std::numbers::pi,5.)*std::numbers::pi*(.25-.0625))<.1,"Hollow section volume incorrect");
    for(int invalid:{0,1,2}){
        auto bad=fixture();auto law=sketcher::Sketch::from_serialized(bad.helical.sketches[1]);
        if(invalid==0)for(auto& point:law.points)point.y=0;
        if(invalid==1)static_cast<void>(law.add_segment(0,0,2,4));
        if(invalid==2)for(auto& point:law.points)if(point.y>0)point.x=-11;
        bad.helical.sketches[1]=law.serialized();bool rejected=false;
        try{static_cast<void>(document::PartDocument::helical_sweep_request(bad));}catch(const std::exception&){rejected=true;}
        require(rejected,"Invalid radial law was accepted");
    }
    auto crossing=fixture();crossing.helical.pitch=.5;
    auto short_law=sketcher::Sketch::from_serialized(crossing.helical.sketches[1]);for(auto& pt:short_law.points)pt.y*=.06;crossing.helical.sketches[1]=short_law.serialized();
    doc.history={crossing};bool rejected=false;
    try{static_cast<void>(k.evaluate_history(doc.kernel_operations()));}catch(const std::exception&){rejected=true;}
    require(rejected,"Self-intersecting spring accepted");
    std::cout<<"Helical Sweep contracts passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
