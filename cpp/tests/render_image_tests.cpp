#include "profile_solid_fixture.hpp"
#include <zima/render/renderer.hpp>
#include <zima/render/spot_cone.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <QImage>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numbers>

using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid render input accepted");}
void close(double actual,double expected){if(std::abs(actual-expected)>=1e-10)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
document::RenderObject camera() {
    document::RenderObject c;c.id="camera";c.name="Camera";c.orthographic=true;c.orthographic_height_mm=100;
    c.placement.x=120;c.placement.y=-160;c.placement.z=100;
    c.placement.rotation_y=std::atan2(90.,200.)*180/std::numbers::pi;
    c.placement.rotation_z=std::atan2(160.,-120.)*180/std::numbers::pi;return c;
}
void camera_equations() {
    document::RenderObject c;c.placement.x=10;c.placement.y=20;c.placement.z=30;
    auto ray=render::Scene::camera_ray(c,100,50,50,25);
    require(ray.origin==kernel::Vec3{10,20,30},"Camera origin changed");
    close(ray.direction.x,1);close(ray.direction.y,0);close(ray.direction.z,0);
    c.orthographic=true;c.orthographic_height_mm=40;
    ray=render::Scene::camera_ray(c,100,50,100,0);
    close(ray.origin.x,10);close(ray.origin.y,-20);close(ray.origin.z,50);
    c.placement.rotation_z=90;ray=render::Scene::camera_ray(c,100,50,50,25);
    close(ray.direction.x,0);close(ray.direction.y,1);close(ray.direction.z,0);
    c.placement.rotation_y=90;ray=render::Scene::camera_ray(c,100,50,50,25);close(ray.direction.z,-1);
}
render::SceneInput fixture() {
    auto doc=document::PartDocument::create_default();
    auto floor=test::rectangular_feature(doc,{130,100,4});floor.placement.z=-3;
    auto sphere=test::spherical_feature(doc,16);sphere.placement.x=-22;sphere.placement.z=15;
    auto box=test::rectangular_feature(doc,{26,26,32});box.placement.x=22;box.placement.y=8;box.placement.z=15;
    auto cylinder=test::circular_feature(doc,9,25);cylinder.placement.x=10;cylinder.placement.y=-28;cylinder.placement.z=-1;
    doc.history={floor,sphere,box,cylinder};document::BodyHistoryGraph graph;
    std::vector<std::string> bodies;
    for(const auto& f:doc.history){bodies.push_back(graph.create_body("Source"));graph.insert({document::PartHistoryKind::Feature,f.id});}
    doc.set_body_history(graph);doc.resolve_constructions({});kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(doc.kernel_operations());
    render::SceneInput input;
    for(const auto& id:bodies)input.sources.push_back({id,calculated.back().body_outputs.at(id)});
    input.appearance.bodies[bodies[0]]={"#80929F",.65,0};
    input.appearance.bodies[bodies[1]]={"#D09946",.24,.85};
    input.appearance.bodies[bodies[2]]={"#277B9C",.32,.05};
    input.appearance.bodies[bodies[3]]={"#CBCDD0",.18,.95};
    auto& def=input.definition;def.active_camera_id="camera";def.objects.push_back(camera());
    document::RenderObject light;light.id="light";light.name="Light";light.kind=document::RenderObjectKind::PointLight;
    light.placement.x=-30;light.placement.y=-70;light.placement.z=120;light.intensity=2200;light.radius_mm=22;
    def.objects.push_back(light);
    light.id="sun";light.name="Sun";light.kind=document::RenderObjectKind::Sun;light.intensity=1200;
    light.placement.rotation_y=50;light.placement.rotation_z=-100;light.angular_radius_degrees=3;
    def.objects.push_back(light);def.settings.width=96;def.settings.height=64;def.settings.samples=4;
    return input;
}
void image_contracts(const render::SceneInput& fixture) {
    const auto count=kernel::body_calculation_count.load();
    auto input=fixture;const auto original=input.sources.front().body;
    render::Scene scene(input);require(scene.triangle_count()>100,"Curved source was not tessellated");
    close(scene.bounds_min().z,-5);close(scene.bounds_max().x,65);
    unsigned completed{};auto image=scene.render({},[&](const auto& p){require(p.completed_samples==++completed,"Progress regressed");},1);
    require(!image.cancelled&&image.completed_samples==4&&image.rays>96*64*4,"Render did not trace the scene");
    require(image.rgba==scene.render({}, {},4).rgba,"Thread count changed deterministic samples");
    std::stop_source stop;const auto cancelled=scene.render(stop.get_token(),[&](const auto&){stop.request_stop();},2);
    require(cancelled.cancelled&&cancelled.completed_samples==1,"Cancellation did not stop before the next pass");
    rejects([&]{render::Scene bad(input,stop.get_token());});
    input.appearance.bodies.begin()->second.color="#FF0000";
    require(image.rgba==scene.render({}, {},2).rgba,"Private scene changed with source appearance");
    require(input.sources.front().body.shares_with(original)&&kernel::body_calculation_count.load()==count,"Rendering modified/recalculated CAD");
    auto no_lights=fixture;for(auto& object:no_lights.definition.objects)object.enabled=false;
    no_lights.definition.settings.environment_strength=0;
    require(render::Scene(no_lights).render({}, {},2).rgba!=image.rgba,"Lights did not illuminate the image");
    auto invalid=fixture;invalid.definition.active_camera_id="missing";rejects([&]{render::Scene bad(invalid);});
    invalid=fixture;invalid.definition.objects.front().placement.reference_valid=false;rejects([&]{render::Scene bad(invalid);});
    auto single=fixture;single.sources.resize(1);render::Scene one(single);
    require(one.triangle_count()<scene.triangle_count()&&one.render({}, {},2).rgba!=image.rgba,"Multiple source Bodies were ignored");
    auto transparent=fixture;
    for(auto& [id,style]:transparent.appearance.bodies)style.color="#00"+style.color.substr(1);
    transparent.definition.settings.exposure_ev=4;transparent.definition.settings.environment_strength=0;
    const auto background=render::Scene(transparent).render({}, {},2);
    for(unsigned y=0;y<background.height;++y)for(unsigned x=0;x<background.width;++x)for(unsigned c=0;c<3;++c) {
        const auto t=(y+.5)/background.height;const auto& settings=transparent.definition.settings;
        const auto expected=std::lround(255*((1-t)*settings.background_top[c]+t*settings.background_bottom[c]));
        require(background.rgba[(std::size_t(y)*background.width+x)*4+c]==expected,"Transparency or exposure changed the authored screen background");
    }
    for(auto& [id,style]:transparent.appearance.bodies)style.color="#80"+style.color.substr(3);
    require(render::Scene(transparent).render({}, {},2).rgba!=background.rgba,"Partial surface opacity was ignored");
}
void write_image(render::SceneInput input,const std::filesystem::path& path) {
    input.definition.settings.width=640;input.definition.settings.height=400;input.definition.settings.samples=128;
    const auto start=std::chrono::steady_clock::now();render::Scene scene(std::move(input));
    const auto result=scene.render({},[](const auto& image){if(image.completed_samples%16==0)std::cout<<"Samples "<<image.completed_samples<<'\n'<<std::flush;});
    QImage image(result.rgba.data(),static_cast<int>(result.width),static_cast<int>(result.height),QImage::Format_RGBA8888);
    require(image.save(QString::fromStdWString(path.wstring())),"Cannot save render evidence");
    std::cout<<"Triangles "<<scene.triangle_count()<<", rays "<<result.rays<<", seconds "
        <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
}
void spotlight_contracts(const render::SceneInput& fixture) {
    for(double angle:{.01,10.,45.,120.,179.99})for(double softness:{0.,.25,1.}) {
        const render::SpotCone cone(angle,softness);
        close(cone.attenuation(1),1);close(cone.attenuation(-1),0);
        close(cone.attenuation(std::cos((angle/2+.001)*std::numbers::pi/180)),0);
        // Numerically integrate intensity over a sphere independently of the
        // prepared cone's analytic normalization.
        constexpr unsigned samples=20000;double integral{};
        const double lower=std::cos(angle*std::numbers::pi/360),step=(1-lower)/samples;
        for(unsigned i=0;i<samples;++i)integral+=cone.attenuation(lower+(i+.5)*step)*step*2*std::numbers::pi;
        require(std::abs(integral/cone.solid_angle-1)<1e-6,"Spotlight cone or soft rim changed emitted flux");
        if(softness>0)require(std::abs(cone.attenuation((cone.inner_cos+cone.outer_cos)/2)-.5)<1e-7,"Spotlight soft rim is asymmetric");
    }
    auto input=fixture;input.sources.resize(1);input.definition.objects.resize(2);
    input.definition.settings.environment_strength=0;input.definition.settings.bounces=1;
    auto& spot=input.definition.objects[1];spot.kind=document::RenderObjectKind::SpotLight;
    spot.radius_mm=0;spot.intensity=80;spot.cone_angle_degrees=45;spot.edge_softness=0;
    spot.placement.rotation_y=std::atan2(120.,std::hypot(30.,70.))*180/std::numbers::pi;
    spot.placement.rotation_z=std::atan2(70.,30.)*180/std::numbers::pi;
    const auto calculations=kernel::body_calculation_count.load();
    const auto sharp=render::Scene(input).render({}, {},1);
    require(sharp.rgba==render::Scene(input).render({}, {},4).rgba,"Spotlight worker count changed the image");
    spot.edge_softness=.8;const auto soft=render::Scene(input).render({}, {},2);
    require(soft.rgba!=sharp.rgba,"Spotlight soft edge did not change illumination");
    spot.enabled=false;const auto dark=render::Scene(input).render({}, {},1);
    require(dark.rgba!=sharp.rgba&&dark.rgba!=soft.rgba,"Spotlight did not illuminate the surface");
    spot.enabled=true;spot.placement.rotation_y=-spot.placement.rotation_y;spot.placement.rotation_z+=180;
    require(render::Scene(input).render({}, {},2).rgba==dark.rgba,"Spotlight illuminated behind its cone");
    spot.placement.rotation_y=-spot.placement.rotation_y;spot.placement.rotation_z-=180;spot.color={1,0,0};
    require(render::Scene(input).render({}, {},2).rgba!=soft.rgba,"Spotlight ignored its color");
    spot.kind=document::RenderObjectKind::PointLight;const auto omni=render::Scene(input).render({}, {},2);
    spot.placement.rotation_y+=35;spot.placement.rotation_z+=160;
    require(render::Scene(input).render({}, {},2).rgba==omni.rgba,"Omnidirectional light acquired a spotlight orientation");
    spot.kind=document::RenderObjectKind::Sun;const auto sun=render::Scene(input).render({}, {},2);
    spot.placement.x+=700;spot.placement.y-=500;spot.placement.z+=250;
    require(render::Scene(input).render({}, {},2).rgba==sun.rgba,"Sun illumination acquired a position dependence");
    require(kernel::body_calculation_count.load()==calculations,"Light edits recalculated CAD geometry");

    // A small top-down patch keeps incidence constant to sub-pixel precision.
    // Doubling source distance needs four times the flux; replacing the cone
    // by an omnidirectional source needs its independent solid-angle ratio.
    auto photometry=fixture;photometry.sources.resize(1);photometry.definition.objects.resize(2);
    auto& options=photometry.definition.settings;options.width=options.height=16;options.samples=1;options.bounces=1;options.environment_strength=0;
    auto& eye=photometry.definition.objects[0];eye.placement={};eye.placement.z=300;eye.placement.rotation_y=90;
    eye.orthographic=true;eye.orthographic_height_mm=.001;
    auto& source=photometry.definition.objects[1];source.placement={};source.placement.z=99;source.placement.rotation_y=90;
    source.kind=document::RenderObjectKind::SpotLight;source.radius_mm=0;source.intensity=10;source.cone_angle_degrees=60;source.edge_softness=0;
    const auto near_image=render::Scene(photometry).render({}, {},1);
    require(near_image.rgba[0]>5&&near_image.rgba[0]<240,"Photometry fixture is dark or saturated");
    source.placement.z=199;
    const auto far_image=render::Scene(photometry).render({}, {},1);
    require(far_image.rgba[0]<near_image.rgba[0],"Spotlight lacks distance attenuation");
    source.intensity*=4;
    require(render::Scene(photometry).render({}, {},1).rgba==near_image.rgba,"Spotlight inverse-square attenuation is incorrect");
    source.kind=document::RenderObjectKind::PointLight;
    source.intensity*=4*std::numbers::pi/(2*std::numbers::pi*(1-std::cos(std::numbers::pi/6)));
    require(render::Scene(photometry).render({}, {},1).rgba==near_image.rgba,"Spotlight luminous-power normalization differs from the independent solid angle");
    source.kind=document::RenderObjectKind::SpotLight;source.intensity=10;
    source.placement.x=44;source.placement.y=16;source.placement.z=99;
    source.placement.rotation_y=std::atan2(100.,std::hypot(44.,16.))*180/std::numbers::pi;
    source.placement.rotation_z=std::atan2(-16.,-44.)*180/std::numbers::pi;
    require(render::Scene(photometry).render({}, {},1).rgba[0]>5,"Shadow fixture is not lit");
    // The fixture's box intersects the oblique source ray while remaining
    // outside the camera's narrow vertical footprint on the floor.
    photometry.sources.push_back(fixture.sources[2]);
    const auto occluded=render::Scene(photometry).render({}, {},1);
    for(std::size_t i=0;i<occluded.rgba.size();i+=4)
        require(occluded.rgba[i]==0&&occluded.rgba[i+1]==0&&occluded.rgba[i+2]==0,"Spotlight ignored shadow occlusion");
    require(kernel::body_calculation_count.load()==calculations,"Photometry recalculated CAD geometry");
}
}
int main(int argc,char** argv){try{camera_equations();const auto input=fixture();image_contracts(input);spotlight_contracts(input);
    if(argc>1)write_image(input,std::filesystem::u8path(argv[1]));
    std::cout<<"Render camera, multi-Body image, lighting, deterministic workers, cancellation and private snapshots passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
