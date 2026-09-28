#include <zima/document/part_document.hpp>
#include <zima/document/profile_targets.hpp>
#include <zima/document/feature_rotation_limit.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
#include <nlohmann/json.hpp>
#include <set>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
document::PartDocument profile_document(bool circle=false,bool hole=false) {
    auto sketch=sketcher::Sketch::create_default();
    if(circle)static_cast<void>(sketch.add_circle(0,0,20));
    else static_cast<void>(sketch.add_rectangle(-50,-25,50,25));
    if(hole)static_cast<void>(sketch.add_circle(0,0,5));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);
    feature.extrusion.length_forward=20;feature.extrusion.height=20;sketch.owner_container_id=feature.id;
    auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Draft"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    return part;
}
std::vector<kernel::HistoryOperation> fixture(bool circle=false,bool hole=false) {
    return profile_document(circle,hole).kernel_operations();
}
kernel::ExtrusionRequest& extrusion(std::vector<kernel::HistoryOperation>& operations) {
    for(auto& operation:operations)if(auto* request=std::get_if<kernel::ExtrusionRequest>(&operation.primitive))return *request;
    throw std::runtime_error("Missing extrusion");
}
std::set<std::string> references(const kernel::BodyResult& result) {
    std::set<std::string> ids;
    for(const auto& f:result.mesh.original_references.triangle_references)ids.insert("face:"+f.semantic_key);
    for(const auto& e:result.mesh.original_references.edges)ids.insert("edge:"+e.reference.semantic_key);
    for(const auto& v:result.mesh.original_references.points)ids.insert("point:"+v.reference.semantic_key);
    return ids;
}
}
int main(){try {
    kernel::OcctKernel kernel;
    for(bool circle:{false,true})for(bool hole:{false,true}) {
        auto operations=fixture(circle,hole);const auto straight=kernel.evaluate_history(operations).back();
        const auto original_ids=references(straight);
        for(double angle:{-5.,5.}) {
            auto& request=extrusion(operations);request.draft_angle_degrees=angle;
            const auto result=kernel.evaluate_history_incremental(operations,{straight}).back();
            const double h=20,t=std::tan(angle*std::numbers::pi/180.);
            const double outer=circle?std::numbers::pi*(400*h-20*t*h*h+t*t*h*h*h/3.):5000*h-150*t*h*h+4*t*t*h*h*h/3.;
            const double inner=hole?std::numbers::pi*(25*h+5*t*h*h+t*t*h*h*h/3.):0;
            near(result.volume,outer-inner);
            const auto actual_ids=references(result);
            if(actual_ids!=original_ids) {
                for(const auto& id:original_ids)if(!actual_ids.contains(id))std::cerr<<"Missing: "<<id<<'\n';
                for(const auto& id:actual_ids)if(!original_ids.contains(id))std::cerr<<"Added: "<<id<<'\n';
                throw std::runtime_error("Draft changed persisted source identities");
            }
        }
        extrusion(operations).draft_angle_degrees=0;
        near(kernel.evaluate_history(operations).back().volume,straight.volume);
    }
    auto operations=fixture();extrusion(operations).draft_angle_degrees=60;
    bool rejected=false;try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception&){rejected=true;}
    check(rejected,"Draft accepted a collapsed rectangle");
    {
        auto part=profile_document();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_circle(-6,0,5));static_cast<void>(sketch.add_circle(6,0,5));
        sketch.owner_container_id=part.history.front().id;part.sketches={sketch};part.history.front().extrusion.sketch_id=sketch.id;
        part.resolve_constructions();auto regions=part.kernel_operations();near(kernel.evaluate_history(regions).back().volume,1000*std::numbers::pi);
        extrusion(regions).draft_angle_degrees=-10;bool rejected=false;
        try{static_cast<void>(kernel.evaluate_history(regions));}catch(const std::exception&){rejected=true;}
        check(rejected,"Draft accepted intersecting independent profile regions");
    }
    for(bool circle:{false,true})for(double angle:{-5.,5.}) {
        auto surface=fixture(circle);auto& request=extrusion(surface);request.surface_result=true;request.draft_angle_degrees=angle;
        const auto result=kernel.evaluate_history(surface).back();
        near(result.volume,0);
        const double radians=angle*std::numbers::pi/180.;
        const double perimeter=circle?std::numbers::pi*(40-20*std::tan(radians)):300-80*std::tan(radians);
        near(result.surface_area,perimeter*20/std::cos(radians));
    }
    for(bool reverse:{false,true})for(const auto kind:{document::ProfileResultType::Thin,document::ProfileResultType::Surface}) {
        std::cout<<"Open profile kind="<<int(kind)<<'\n'<<std::flush;
        auto part=profile_document();auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_segment(-10,0,10,0));
        sketch.owner_container_id=part.history.front().id;part.sketches={sketch};
        auto& settings=part.history.front().extrusion;settings.sketch_id=sketch.id;settings.result_type=kind;settings.thin_thickness=2;
        settings.direction=reverse?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        part.resolve_constructions();auto operations=part.kernel_operations();extrusion(operations).draft_angle_degrees=1;
        if(kind==document::ProfileResultType::Surface){bool rejected=false;
            try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception& e){rejected=std::string(e.what())=="Surface draft requires a closed profile.";}
            check(rejected,"Open surface draft did not report its supported-profile limit");
            extrusion(operations).draft_angle_degrees=0;near(kernel.evaluate_history(operations).back().surface_area,400);continue;}
        const auto body=kernel.evaluate_history(operations).back();const double h=20,t=std::tan(std::numbers::pi/180.);
        if(kind==document::ProfileResultType::Thin)near(body.volume,40*h-22*t*h*h+4*t*t*h*h*h/3.);
        else {near(body.volume,0);near(body.surface_area,400/std::cos(std::numbers::pi/180.));}
        std::cout<<"Open profile preview\n"<<std::flush;
        const auto preview=part.extrusion_preview_edges(part.history.front(),1000,0,1);
        check(!preview.empty(),"Open drafted profile has no preview");
        for(const auto& edge:preview)for(const auto p:edge.points){double nearest=1e100;
            for(const auto q:body.mesh.vertices)nearest=std::min(nearest,std::hypot(p.x-q.x,p.y-q.y,p.z-q.z));
            if(nearest>=1e-6)throw std::runtime_error("Open-profile draft preview mismatch kind="+std::to_string(int(kind))+" distance="+std::to_string(nearest)+" at "+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z));}
    }
    {
        auto ellipse=fixture(true);auto& request=extrusion(ellipse);
        request.outer_profile=kernel::ExtrusionRequest::EllipseProfile{{0,0,0},{1,0,0},20,10};
        check(kernel.evaluate_history(ellipse).back().volume>0,"Undrafted ellipse regressed");request.draft_angle_degrees=5;
        bool rejected=false;try{static_cast<void>(kernel.evaluate_history(ellipse));}catch(const std::exception& error){
            rejected=std::string(error.what())=="Draft supports profiles made of lines and circular arcs.";}
        check(rejected,"Unsupported drafted curve did not return its specific validation message");
    }
    {
        auto thin=fixture(true);auto& request=extrusion(thin);request.wall=kernel::ProfileWall{0,2,{}};request.draft_angle_degrees=1;
        std::cout<<"Thin draft\n"<<std::flush;
        const double t=std::tan(std::numbers::pi/180.);
        near(kernel.evaluate_history(thin).back().volume,std::numbers::pi*(76*20-38*t*400));
        request.draft_angle_degrees=5;bool collapsed=false;
        try{static_cast<void>(kernel.evaluate_history(thin));}catch(const std::exception&){collapsed=true;}
        check(collapsed,"Draft accepted a collapsed Thin wall");
    }
    {
        auto curved=fixture();auto& request=extrusion(curved);
        std::cout<<"Arc draft\n"<<std::flush;
        kernel::ExtrusionRequest::CurvedProfile profile;
        profile.curves={kernel::ExtrusionRequest::LineCurve{{-10,-5,0},{10,-5,0}},
            kernel::ExtrusionRequest::ArcCurve{{10,-5,0},{15,0,0},{10,5,0}},
            kernel::ExtrusionRequest::LineCurve{{10,5,0},{-10,5,0}},
            kernel::ExtrusionRequest::ArcCurve{{-10,5,0},{-15,0,0},{-10,-5,0}}};
        request.outer_profile=profile;request.draft_angle_degrees=5;
        const double h=20,t=std::tan(5*std::numbers::pi/180.);
        near(kernel.evaluate_history(curved).back().volume,(200+25*std::numbers::pi)*h-(20+5*std::numbers::pi)*t*h*h+std::numbers::pi*t*t*h*h*h/3.);
    }
    for(bool circle:{false,true})for(bool hole:{false,true})for(bool reverse:{false,true})for(bool symmetric:{false,true}) {
        std::cout<<"Native draft circle="<<circle<<" hole="<<hole<<" reverse="<<reverse<<" symmetric="<<symmetric<<'\n'<<std::flush;
        auto sketch=sketcher::Sketch::create_default();
        if(circle)static_cast<void>(sketch.add_circle(0,0,20));
        else static_cast<void>(sketch.add_rectangle(-50,-25,50,25));
        if(hole)static_cast<void>(sketch.add_circle(0,0,5));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        sketch.owner_container_id=feature.id;
        feature.feature.symmetric=symmetric;
        for(std::size_t i=0;i<2;++i) {
            auto& side=feature.feature.sides[i];
            side.operation=(symmetric||i==std::size_t(reverse))?document::FeatureSideOperation::Extrusion:document::FeatureSideOperation::None;
            side.length=20;side.draft_angle_degrees=5;
        }
        auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Draft"));
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto restored=document::PartDocument::from_serialized(part.serialized());
        check(restored.history.front().feature==feature.feature,"Document lost authored draft parameters");
        auto straight=restored;for(auto& side:straight.history.front().feature.sides)side.draft_angle_degrees=0;
        check(kernel.evaluate_history(straight.kernel_operations()).back().volume>0,"Mixed profile failed without draft");
        const double h=20,t=std::tan(5*std::numbers::pi/180.);
        const double outer=circle?std::numbers::pi*(400*h-20*t*h*h+t*t*h*h*h/3.):5000*h-150*t*h*h+4*t*t*h*h*h/3.;
        const double inner=hole?std::numbers::pi*(25*h+5*t*h*h+t*t*h*h*h/3.):0;
        const double expected=(symmetric?2:1)*(outer-inner);
        near(kernel.evaluate_history(restored.kernel_operations()).back().volume,expected);
        const auto preview=restored.feature_preview_edges(restored.history.front());
        check(!preview.empty(),"Draft preview is empty");
        for(const auto& edge:preview)for(const auto& p:edge.points){
            const double offset=std::abs(p.z)*t;
            check((hole&&std::abs(std::hypot(p.x,p.y)-(5+offset))<1e-7)||
                (circle?std::abs(std::hypot(p.x,p.y)-(20-offset))<1e-7:
                std::abs(std::abs(p.x)-(50-offset))<1e-7||std::abs(std::abs(p.y)-(25-offset))<1e-7),
                "Draft preview does not follow the analytical walls");
        }
    }
    {
        auto operations=fixture();auto& request=extrusion(operations);request.draft_angle_degrees=5;
        request.extent=kernel::ExtrusionRequest::Extent::UpToPlane;request.target_is_datum=true;
        request.target_face={"datum","plane",{}};request.target_plane_origin={0,0,20};request.target_plane_normal={-.1,0,1};
        const auto body=kernel.evaluate_history(operations).back();
        check(body.volume>0,"Draft Up To produced an empty body");
        const auto& packet=body.mesh.original_references;
        for(std::size_t i=0;i<packet.triangle_references.size();++i)
            if(packet.triangle_references[i].semantic_key.starts_with("end:"))
                for(std::size_t j=0;j<3;++j){const auto p=packet.vertices[packet.triangles[3*i+j]];near(p.z-.1*p.x,20);}
        auto part=profile_document();auto& p=part.history.front().extrusion;
        p.end_condition_forward=document::EndCondition::UpTo;
        document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Plane;
        target.fallback_origin={0,0,20};target.fallback_normal={-.1,0,1};p.end_targets_forward={target};
        const auto preview=part.extrusion_preview_edges(part.history.front(),1000,0,5);
        for(const auto& edge:preview)if(edge.reference.semantic_key.starts_with("preview:end"))
            for(const auto point:edge.points){near(point.z-.1*point.x,20);
                const double offset=point.z*std::tan(5*std::numbers::pi/180.);
                check(std::abs(std::abs(point.x)-(50-offset))<1e-7||std::abs(std::abs(point.y)-(25-offset))<1e-7,
                    "Draft Up To preview left the analytical side wall");}
    }
    {
        auto operations=fixture();extrusion(operations).draft_angle_degrees=5;
        auto original=kernel.evaluate_history(operations).back();
        const auto& refs=original.mesh.original_references;
        const auto face=std::ranges::find_if(refs.triangle_references,[](const auto& f){return f.surface&&
            f.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&f.surface->origin.x>40&&std::abs(f.surface->axis.x)>.9;});
        check(face!=refs.triangle_references.end(),"Missing drafted original side face");
        document::ExtrusionParameters::EndTarget stored;stored.reference=*face;
        const auto target=document::resolve_profile_target(stored,refs);
        check(target&&target->kind==document::EndTargetKind::Plane,"Drafted plane cannot be selected as an Up To target");
        const double angle=document::feature_rotation_limit_angle({50,0,0},{0,1,0},{0,0,1},*target);
        check(std::abs(angle-85)<1e-7||std::abs(angle-265)<1e-7,"Rotation Up To lost the drafted plane angle or side");
        auto follower=operations.back();follower.owner_id="following-extrusion";
        auto seed=fixture(true);auto cut=extrusion(seed);
        cut.outer_profile=kernel::ExtrusionRequest::CircleProfile{{70,0,10},2};cut.direction={-40,0,0};
        cut.extent=kernel::ExtrusionRequest::Extent::UpToPlane;cut.target_face=*face;
        cut.target_plane_origin=target->fallback_origin;cut.target_plane_normal=target->fallback_normal;
        follower.primitive=cut;operations.push_back(follower);
        for(double draft:{5.,7.}){
            extrusion(operations).draft_angle_degrees=draft;
            const auto result=kernel.evaluate_history(operations);
            const double t=std::tan(draft*std::numbers::pi/180.);
            near(result.back().volume-result[result.size()-2].volume,4*std::numbers::pi*(20+10*t));
        }
    }
    {
        auto operations=fixture(true);extrusion(operations).draft_angle_degrees=5;
        const auto source=kernel.evaluate_history(operations).back();const auto& refs=source.mesh.original_references;
        const auto face=std::ranges::find_if(refs.triangle_references,[](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Cone;});
        check(face!=refs.triangle_references.end(),"Drafted circle has no original conical face");
        auto follower=operations.back();follower.owner_id="cone-follower";
        auto seed=fixture(true);auto cut=extrusion(seed);cut.outer_profile=kernel::ExtrusionRequest::CircleProfile{{30,0,10},2};cut.direction={-30,0,0};
        cut.extent=kernel::ExtrusionRequest::Extent::UpToSurface;cut.target_face=*face;
        for(std::size_t i=0;i<refs.triangle_references.size();++i)if(refs.triangle_references[i]==*face)
            for(std::size_t j=0;j<3;++j)cut.target_surface_triangles.push_back(refs.vertices[refs.triangles[3*i+j]]);
        follower.primitive=cut;operations.push_back(follower);
        for(double angle:{5.,7.}){
            extrusion(operations).draft_angle_degrees=angle;const auto result=kernel.evaluate_history(operations);
            check(result.back().volume>result[result.size()-2].volume,"Conical Up To follower has no material");
            const auto& packet=result.back().mesh.original_references;std::size_t sampled=0;
            for(std::size_t i=0;i<packet.triangle_references.size();++i){const auto& f=packet.triangle_references[i];
                if(f.owner_id==follower.owner_id&&f.semantic_key.starts_with("end:"))for(std::size_t j=0;j<3;++j){
                    const auto p=packet.vertices[packet.triangles[3*i+j]];near(std::hypot(p.x,p.y),20-p.z*std::tan(angle*std::numbers::pi/180.));++sampled;}}
            check(sampled>0,"Conical Up To follower lost its end face identity");
        }
    }
    std::cout<<"Feature draft contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
