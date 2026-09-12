#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>std::max(1e-6,std::abs(expected)*1e-7))throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
struct Profile {
    document::PartDocument part=document::PartDocument::create_default();
    Profile(sketcher::Sketch sketch,bool revolve=false) {
        part.name=!sketch.bsplines.empty()?"Thin spline":!sketch.arcs.empty()?"Thin arc":"Thin profile";
        auto feature=revolve?document::PartDocument::create_revolution_container(sketch.id):document::PartDocument::create_extrusion_container(sketch.id);
        sketch.owner_container_id=feature.id;
        if(revolve){feature.revolution.result_type=document::ProfileResultType::Thin;feature.revolution.thin_thickness=1;}
        else {feature.extrusion.result_type=document::ProfileResultType::Thin;feature.extrusion.thin_thickness=1;feature.extrusion.length_forward=3;feature.extrusion.height=3;}
        part.history={feature};part.sketches={std::move(sketch)};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Thin"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    }
    auto calculate(const kernel::OcctKernel& kernel){auto resolved=part;resolved.resolve_constructions();try{return kernel.evaluate_history(resolved.kernel_operations());}catch(const std::exception& e){throw std::runtime_error(part.name+": "+e.what());}}
};
std::set<std::string> face_ids(const kernel::BodyResult& body){std::set<std::string> result;for(const auto& face:body.mesh.original_references.triangle_references)result.insert(face.semantic_key);return result;}
void check_preview(Profile& fixture,const kernel::BodyResult& body) {
    auto resolved=fixture.part;resolved.resolve_constructions();const auto& feature=resolved.history.front();
    const auto preview=feature.feature_kind==document::FeatureKind::Extrusion?resolved.extrusion_preview_edges(feature):resolved.revolution_preview_edges(feature);
    kernel::ModelEnvelope actual,shown;
    for(const auto& point:body.mesh.vertices)actual.include(point);
    for(const auto& edge:preview)for(const auto& point:edge.points)shown.include(point);
    require(shown.valid && actual.valid,"Thin profile has no preview or body");
    for(const auto delta:{actual.minimum.x-shown.minimum.x,actual.minimum.y-shown.minimum.y,actual.minimum.z-shown.minimum.z,
                         actual.maximum.x-shown.maximum.x,actual.maximum.y-shown.maximum.y,actual.maximum.z-shown.maximum.z})
        if(std::abs(delta)>.05)throw std::runtime_error("Thin preview differs from body bounds by "+std::to_string(delta));
}
void closed_circle(const kernel::OcctKernel& kernel) {
    auto sketch=sketcher::Sketch::create_default();const auto circle=sketch.add_circle(0,0,5);Profile fixture(sketch);
    std::set<std::string> stable;std::string fingerprint;
    for(const auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}) {
        auto& p=fixture.part.history.front().extrusion;p.thin_mode=mode;
        const double outer=mode==document::ThinMode::OneSide?5:mode==document::ThinMode::OtherSide?6:5.5;
        for(const auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse}) {
            p.direction=direction;const auto bodies=fixture.calculate(kernel);const auto& body=bodies.back();
            near(body.volume,std::numbers::pi*(outer*outer-(outer-1)*(outer-1))*3);check_preview(fixture,body);
            const auto ids=face_ids(body);
            require(ids.contains("generated:thin:outside:from:"+circle)&&ids.contains("generated:thin:inside:from:"+circle),"Thin circular sides lost their original Sketch curve parent");
            if(stable.empty())stable=ids;else require(stable==ids,"Changing Thin side/direction changed face identities");
            require(fingerprint!=body.source_fingerprint,"Thin side/direction did not invalidate calculated cache");fingerprint=body.source_fingerprint;
        }
    }
    auto& p=fixture.part.history.front().extrusion;p.thin_thickness=2;p.direction=document::ExtrusionDirection::Forward;
    near(fixture.calculate(kernel).back().volume,60*std::numbers::pi);
    p.extent_mode=document::ProfileExtentMode::TwoSides;p.length_forward=2;p.length_reverse=4;
    near(fixture.calculate(kernel).back().volume,120*std::numbers::pi);
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=root/("zima-thin-profile-"+fixture.part.document_id);
    require(std::filesystem::create_directory(directory),"Cannot create Thin fixture directory");
    const auto bodies=fixture.calculate(kernel);fixture.part.save(directory/"thin.prtz",bodies);
    std::vector<kernel::BodyResult> cache;const auto loaded=document::PartDocument::load(directory/"thin.prtz",&cache);
    require(loaded.history.front().extrusion==p,"Native Thin parameters changed on roundtrip");near(cache.back().volume,120*std::numbers::pi);
    require(directory.parent_path()==root,"Unexpected fixture path");std::filesystem::remove_all(directory);
    p.thin_mode=document::ThinMode::OneSide;p.thin_thickness=8;bool rejected=false;
    try{fixture.calculate(kernel);}catch(const std::exception&){rejected=true;}
    require(rejected,"Collapsed wall was accepted");
}
void open_line(const kernel::OcctKernel& kernel) {
    auto sketch=sketcher::Sketch::create_default();const auto line=sketch.add_segment(0,0,10,0);Profile fixture(sketch);
    fixture.part.history.front().extrusion.thin_thickness=2;
    auto bodies=fixture.calculate(kernel);near(bodies.back().volume,60);check_preview(fixture,bodies.back());
    const auto ids=face_ids(bodies.back());
    require(ids.contains("generated:thin:first:from:"+line)&&ids.contains("generated:thin:second:from:"+line),"Open wall lost its original curve parents");
    require(ids.size()==6,"Open wall has missing generated side or cap references");
    for(const auto& point:sketch.points) {
        require(ids.contains("generated:thin:closure:from:"+point.id),"Open wall closure lost its endpoint parent");
    }
    fixture.part.history.front().extrusion.direction=document::ExtrusionDirection::Reverse;
    near(fixture.calculate(kernel).back().volume,60);
}
void curved_profiles(const kernel::OcctKernel& kernel) {
    auto rectangle=sketcher::Sketch::create_default();static_cast<void>(rectangle.add_rectangle(0,0,10,8));Profile box(rectangle);
    for(const auto [mode,volume]:std::vector<std::pair<document::ThinMode,double>>{{document::ThinMode::OneSide,96},{document::ThinMode::OtherSide,120},{document::ThinMode::Symmetric,108}}) {
        box.part.history.front().extrusion.thin_mode=mode;const auto body=box.calculate(kernel).back();near(body.volume,volume);check_preview(box,body);
    }
    auto arc=sketcher::Sketch::create_default();static_cast<void>(arc.add_arc(0,0,5,0,0,5));Profile arc_wall(arc);
    arc_wall.part.history.front().extrusion.thin_mode=document::ThinMode::Symmetric;
    auto body=arc_wall.calculate(kernel).back();near(body.volume,7.5*std::numbers::pi);check_preview(arc_wall,body);
    auto spline=sketcher::Sketch::create_default();const auto curve=spline.add_bspline({{0,0},{3,4},{7,4},{10,0}});Profile spline_wall(spline);
    auto& p=spline_wall.part.history.front().extrusion;p.thin_mode=document::ThinMode::Symmetric;p.thin_thickness=.4;
    // Independent Simpson integration of the cubic Bezier speed. A symmetric
    // planar strip has area equal to thickness times its centreline length.
    double integral=0;constexpr int count=10000;
    for(int i=0;i<=count;++i){const double t=double(i)/count;const double speed=std::hypot(9+6*t-6*t*t,12-24*t);integral+=(i==0||i==count?1:i%2?4:2)*speed;}
    const double expected=integral/(3*count)*.4*3;
    body=spline_wall.calculate(kernel).back();
    std::cout << "Spline strip volume error: " << std::scientific << std::abs(body.volume-expected) << " mm^3\n";
    if(std::abs(body.volume-expected)>1e-4)throw std::runtime_error("Spline strip volume expected "+std::to_string(expected)+", got "+std::to_string(body.volume));
    require(face_ids(body).contains("generated:thin:first:from:"+curve),"Spline wall lost its source curve");check_preview(spline_wall,body);
    const auto& edges=body.mesh.original_references.edges;
    require(std::ranges::any_of(edges,[&](const auto& edge){return edge.reference.semantic_key=="start:thin:first:from:"+curve && edge.exact_spline.has_value();}),
        "Offset spline was reduced to a polyline instead of persisted exact curve data");
    p.direction=document::ExtrusionDirection::Reverse;const auto reversed=spline_wall.calculate(kernel).back();
    require(std::abs(reversed.volume-expected)<1e-4,"Reversed spline wall has the wrong volume");
    require(face_ids(reversed)==face_ids(body),"Reversing a spline wall changed its face parents");check_preview(spline_wall,reversed);
}
void revolution(const kernel::OcctKernel& kernel) {
    auto sketch=sketcher::Sketch::create_default();const auto line=sketch.add_segment(5,0,5,10);
    const auto axis=sketch.add_segment(0,0,0,10,1e-6,true);sketch.segments.back().centerline=true;
    Profile fixture(sketch,true);auto& p=fixture.part.history.front().revolution;p.axis_segment_id=axis;
    p.thin_mode=document::ThinMode::Symmetric;
    auto bodies=fixture.calculate(kernel);near(bodies.back().volume,100*std::numbers::pi);
    const auto ids=face_ids(bodies.back());require(ids.contains("generated:thin:first:from:"+line),"Thin Revolution lost source curve");
    p.angle_degrees=90;bodies=fixture.calculate(kernel);near(bodies.back().volume,25*std::numbers::pi);check_preview(fixture,bodies.back());
    p.direction=document::ExtrusionDirection::Reverse;near(fixture.calculate(kernel).back().volume,25*std::numbers::pi);
    p.extent_mode=document::ProfileExtentMode::TwoSides;p.angle_degrees=30;p.angle_reverse=60;
    near(fixture.calculate(kernel).back().volume,25*std::numbers::pi);
}
}
int main(){try{kernel::OcctKernel kernel;closed_circle(kernel);open_line(kernel);curved_profiles(kernel);revolution(kernel);std::cout<<"Thin profiles: exact wall volumes, directions, original ancestry and native cache passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
