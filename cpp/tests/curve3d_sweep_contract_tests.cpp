#include <zima/document/part_document.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <filesystem>
using namespace zima;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static document::HistoryContainer fixture(double r){
    auto c=document::PartDocument::create_sweep3d_container();
    for(auto v:std::vector<kernel::Vec3>{{0,0,0},{0,0,30},{30,0,30}}){
        auto p=document::PartDocument::create_construction(document::ConstructionKind::Point);
        p.parent_construction_id=c.sweep3d.path.id;p.origin=v;c.sweep3d.path.curve_points.push_back(p);
    }
    c.sweep3d.path.curve_rounding_enabled=r>0;c.sweep3d.path.curve_points[1].curve_radius=r;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=c.id;
    static_cast<void>(sketch.add_circle(0,0,1));
    c.sweep3d.profiles.push_back({kernel::make_stable_id(),c.sweep3d.path.curve_points[0].id,sketch.id,sketch.serialized()});
    return c;
}
static void mark_circle(sketcher::Sketch& sketch,std::size_t count,double phase=0) {
    const auto circle=sketch.circles.front();
    const auto center=*sketch.find_point(circle.center_point_id);
    for(std::size_t i=0;i<count;++i) {
        const double a=phase+2*std::numbers::pi*i/count;
        const auto point=sketch.add_point(center.x+circle.radius*std::cos(a),
            center.y+circle.radius*std::sin(a));
        static_cast<void>(sketch.add_point_on_circle_constraint(point,circle.id));
    }
}
int main(){try{
    kernel::OcctKernel k;
    auto c=fixture(5);auto route=document::curve3d_route(c.sweep3d.path);
    require(route.stations.size()==4&&route.segments.size()==3,"rounded route count");
    require(route.stations[1].label=="2.1"&&route.stations[2].label=="2.2","station labels");
    require(std::abs(route.stations[1].origin.z-25)<1e-9&&std::abs(route.stations[2].origin.x-5)<1e-9,"tangency locations");
    require(route.segments[1].arc_midpoint.has_value(),"exact arc missing");
    require(route.stations[0].tangent.z==1&&route.stations[1].tangent.z==1&&
        route.stations[2].tangent.x==1&&route.stations[3].tangent.x==1,"station normals do not follow their respective straight segments");
    // Annotation geometry follows the exact route in both committed and editing views.
    auto annotated = c.sweep3d.path;
    const auto radius_owner = annotated.curve_points[1].id;
    auto radii = document::curve3d_radius_dimensions(annotated);
    require(radii.size()==1 && radii[0].value==5 && radii[0].label_prefix=="R" &&
        radii[0].reference.owner_id==radius_owner && radii[0].reference.semantic_key=="parameter:radius",
        "Radius annotation lost its value or persisted point identity");
    require(std::abs(radii[0].witness_first.x-5)<1e-9 &&
        std::abs(radii[0].witness_first.z-25)<1e-9,
        "Radius annotation center does not match the fillet");
    annotated.origin={10,20,30}; annotated.rotation={0,0,90};
    radii=document::curve3d_radius_dimensions(annotated);
    require(std::abs(radii[0].witness_first.x-10)<1e-9 &&
        std::abs(radii[0].witness_first.y-25)<1e-9 &&
        std::abs(radii[0].witness_first.z-55)<1e-9 &&
        std::abs(radii[0].plane_normal.x+1)<1e-9,
        "Placed radius annotation has the wrong center or plane");
    document::PartDocument annotations; annotations.constructions={annotated};
    for(const auto& editing:std::vector<std::string>{{},annotated.id}) {
        for(bool sweep_stations:{false,true}) {
            const auto mesh=annotations.construction_viewer_mesh(editing,0,sweep_stations);
            const auto count=std::count_if(mesh.dimensions.begin(),mesh.dimensions.end(),
                [&](const auto& d){return d.reference.owner_id==radius_owner && d.reference.semantic_key=="parameter:radius";});
            require(count==(editing.empty()?0:1),"Radius annotation did not follow the Curve/Sweep editing lifetime");
        }
    }
    annotated.curve_points[1].curve_radius=3;
    radii=document::curve3d_radius_dimensions(annotated);
    require(radii.size()==1 && radii[0].value==3 &&
        std::abs(std::hypot(radii[0].witness_second.x-radii[0].witness_first.x,
                           radii[0].witness_second.y-radii[0].witness_first.y,
                           radii[0].witness_second.z-radii[0].witness_first.z)-3)<1e-9,
        "Pending radius annotation did not update");
    annotated.curve_rounding_enabled=false;
    require(document::curve3d_radius_dimensions(annotated).empty(),"Disabled rounding retained radius annotations");
    annotated.curve_rounding_enabled=true;annotated.curve_points[1].curve_radius=0;
    require(document::curve3d_radius_dimensions(annotated).empty(),"Sharp corner received a fillet annotation");
    annotated.curve_points[1].curve_radius=3;annotated.curve_type=document::Curve3DType::InterpolatingSpline;
    require(document::curve3d_radius_dimensions(annotated).empty(),"Spline displayed inactive polyline radii");
    auto spatial=c.sweep3d.path;
    spatial.curve_points[2].origin={15,20,30};
    auto spatial_route=document::curve3d_route(spatial);
    require(std::abs(spatial_route.stations[2].tangent.x-.6)<1e-9&&
        std::abs(spatial_route.stations[2].tangent.y-.8)<1e-9,"3D outgoing station normal");
    auto fourth=document::PartDocument::create_construction(document::ConstructionKind::Point);
    fourth.origin={15,20,60};fourth.parent_construction_id=spatial.id;
    spatial.curve_points.push_back(fourth);
    spatial.curve_points[1].curve_radius=13;spatial.curve_points[2].curve_radius=13;
    bool overlap=false;try{static_cast<void>(document::curve3d_route(spatial));}catch(const std::exception&){overlap=true;}
    require(overlap,"overlapping neighboring fillets accepted");
    auto doc=document::PartDocument::create_default();doc.history={c};
    auto body=k.evaluate_history(doc.kernel_operations());
    const double expected=std::numbers::pi*(50+5*std::numbers::pi/2);
    std::cout.precision(14); std::cout<<"Rounded volume "<<body[0].volume<<" expected "<<expected<<std::endl;
    require(body.size()==1&&std::abs(body[0].volume-expected)<1e-4,"rounded pipe volume");
    c.sweep3d.path.curve_rounding_enabled=false;
    route=document::curve3d_route(c.sweep3d.path);
    require(!route.stations[1].active&&route.stations[2].active&&route.segments.size()==2,"disabled radius stations");
    require(c.sweep3d.path.curve_points[1].curve_radius==5,"disabled radius forgotten");
    doc.history={c};body=k.evaluate_history(doc.kernel_operations());
    std::cout<<"Sharp volume "<<body[0].volume<<" expected "<<60*std::numbers::pi<<std::endl;
    require(std::abs(body[0].volume-60*std::numbers::pi)<1e-4,"sharp pipe volume");
    c.sweep3d.path.curve_rounding_enabled=true;c.sweep3d.path.curve_points[1].curve_radius=31;
    bool rejected=false;try{static_cast<void>(document::curve3d_route(c.sweep3d.path));}catch(const std::exception&){rejected=true;}
    require(rejected,"oversized radius accepted");
    c.sweep3d.path.curve_points[1].curve_radius=5;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=c.id;static_cast<void>(sketch.add_circle(0,0,1));
    c.sweep3d.profiles.push_back({kernel::make_stable_id(),c.sweep3d.path.curve_points[1].id,sketch.id,sketch.serialized(),true});
    require(document::PartDocument::reframe_sweep3d_profile(c,1),"incoming frame failed");
    const auto id=c.sweep3d.profiles[1].sketch_id;c.sweep3d.path.curve_rounding_enabled=false;
    doc.history={c};const auto file=std::filesystem::temp_directory_path()/"zima-rounded-sweep-test.prtz";
    doc.save(file);auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
    require(loaded.history[0].sweep3d.profiles[1].incoming&&loaded.history[0].sweep3d.profiles[1].sketch_id==id,"inactive station not persisted");
    require(loaded.history[0].sweep3d.path.curve_points[1].curve_radius==5,"radius not persisted");
    body=k.evaluate_history(loaded.kernel_operations());require(body.size()==1,"inactive profile prevented calculation");
    for(double radius:{0.0,5.0})for(bool change_shape:{false,true}){
        auto varied=fixture(radius);const auto stations=document::curve3d_route(varied.sweep3d.path).stations;
        for(std::size_t i=1;i<stations.size();++i){const auto& station=stations[i];if(!station.active)continue;
            auto profile=sketcher::Sketch::create_default();profile.owner_container_id=varied.id;
            if(change_shape&&i+1==stations.size())static_cast<void>(profile.add_rectangle(-1.2,-.8,1.2,.8));
            else static_cast<void>(profile.add_circle(0,0,1+0.1*i));
            varied.sweep3d.profiles.push_back({kernel::make_stable_id(),station.point_id,profile.id,profile.serialized(),station.incoming});
        }
        if(change_shape) for(auto& definition:varied.sweep3d.profiles) {
            auto profile=sketcher::Sketch::from_serialized(definition.sketch_serialized);
            if(!profile.circles.empty())mark_circle(profile,4);
            definition.sketch_serialized=profile.serialized();
        }
        std::cout<<"Variable sweep radius="<<radius<<" shape="<<change_shape<<std::endl;
        doc.history={varied};const auto result=k.evaluate_history(doc.kernel_operations());
        require(result.size()==1&&result.front().volume>0,"Variable profile sweep failed");
    }
    // Opening a station Sketch without drawing must behave like an absent
    // Sketch, while a later explicit profile becomes the next inheritance source.
    for (double radius : {0.0, 5.0}) {
        auto inherited = fixture(radius);
        for (const auto& station : document::curve3d_route(inherited.sweep3d.path).stations) {
            if (station.point_id == inherited.sweep3d.path.curve_points.front().id) continue;
            auto empty = sketcher::Sketch::create_default();
            empty.owner_container_id = inherited.id;
            inherited.sweep3d.profiles.push_back({kernel::make_stable_id(),
                station.point_id, empty.id, empty.serialized(), station.incoming});
        }
        doc.history = {inherited};
        const auto result = k.evaluate_history(doc.kernel_operations());
        const double length = radius == 0 ? 60 : 50 + 5*std::numbers::pi/2;
        require(std::abs(result.front().volume-length*std::numbers::pi)<1e-4,
            "Empty station Sketch interrupted profile inheritance");
        auto larger = sketcher::Sketch::create_default();
        larger.owner_container_id = inherited.id;
        static_cast<void>(larger.add_circle(0,0,2));
        inherited.sweep3d.profiles.front().sketch_id = larger.id;
        inherited.sweep3d.profiles.front().sketch_serialized = larger.serialized();
        doc.history = {inherited};
        const auto updated = k.evaluate_history(doc.kernel_operations());
        require(std::abs(updated.front().volume-4*length*std::numbers::pi)<1e-3,
            "Inherited profiles did not follow the source edit");
        auto missing_first = inherited;
        missing_first.sweep3d.profiles.erase(missing_first.sweep3d.profiles.begin());
        doc.history = {missing_first};
        bool missing_rejected = false;
        try { static_cast<void>(doc.kernel_operations()); }
        catch (const std::exception&) { missing_rejected = true; }
        require(missing_rejected, "Sweep accepted an empty first profile");
    }
    auto switched = fixture(0);
    switched.sweep3d.path.curve_points[2].origin = {0,0,60};
    auto endpoint = document::PartDocument::create_construction(document::ConstructionKind::Point);
    endpoint.parent_construction_id = switched.sweep3d.path.id;
    endpoint.origin = {0,0,90};
    switched.sweep3d.path.curve_points.push_back(endpoint);
    auto second = sketcher::Sketch::create_default();
    second.owner_container_id = switched.id;
    static_cast<void>(second.add_circle(0,0,2));
    switched.sweep3d.profiles.push_back({kernel::make_stable_id(),
        switched.sweep3d.path.curve_points[1].id,second.id,second.serialized(),false});
    auto blank = sketcher::Sketch::create_default();
    blank.owner_container_id = switched.id;
    switched.sweep3d.profiles.push_back({kernel::make_stable_id(),
        switched.sweep3d.path.curve_points[2].id,blank.id,blank.serialized(),false});
    // Profile creation order must not control inheritance.
    std::reverse(switched.sweep3d.profiles.begin(),switched.sweep3d.profiles.end());
    doc.history = {switched};
    auto switch_body = k.evaluate_history(doc.kernel_operations());
    require(std::abs(switch_body.front().volume-310*std::numbers::pi)<1e-3,
        "Empty stations did not inherit the latest profile in path order");
    static_cast<void>(blank.add_segment(0,0,1,0));
    switched.sweep3d.profiles.front().sketch_serialized=blank.serialized();
    doc.history={switched};
    bool unfinished_rejected=false;
    try { static_cast<void>(doc.kernel_operations()); }
    catch (const std::exception&) { unfinished_rejected=true; }
    require(unfinished_rejected,"Unfinished profile was silently treated as empty");
    // Explicit circular sections must behave exactly like inherited ones:
    // their arbitrary circle parameter origins must not narrow the solid.
    for(double radius:{0.0,5.0}) {
        auto explicit_circles=fixture(radius);
        doc.history={explicit_circles};
        const double reference_volume=k.evaluate_history(doc.kernel_operations()).front().volume;
        const auto stations=document::curve3d_route(explicit_circles.sweep3d.path).stations;
        for(std::size_t i=1;i<stations.size();++i) {
            if(!stations[i].active)continue;
            auto profile=sketcher::Sketch::create_default();profile.owner_container_id=explicit_circles.id;
            static_cast<void>(profile.add_circle(0,0,1));
            explicit_circles.sweep3d.profiles.push_back({kernel::make_stable_id(),stations[i].point_id,
                profile.id,profile.serialized(),stations[i].incoming});
        }
        doc.history={explicit_circles};
        const auto independent=k.evaluate_history(doc.kernel_operations());
        require(std::abs(independent.front().volume-reference_volume)<1e-3,
            "Independent circle seams twisted or narrowed the sweep");
        const auto preview=document::sweep3d_profiles_viewer_mesh(explicit_circles);
        require(preview.edges.size()==explicit_circles.sweep3d.profiles.size(),
            "Sweep Properties omitted section Sketch geometry");
    }
    auto mapping_fixture=fixture(0);
    mapping_fixture.sweep3d.path.curve_points.resize(2);
    auto start_sketch=sketcher::Sketch::from_serialized(mapping_fixture.sweep3d.profiles[0].sketch_serialized);
    mark_circle(start_sketch,4);
    const auto start_mapping=document::sweep3d_profile_correspondence(start_sketch);
    require(start_mapping.point_ids.size()==4,"Circle C points were not found for correspondence");
    mapping_fixture.sweep3d.profiles[0].sketch_serialized=start_sketch.serialized();
    auto rectangle=sketcher::Sketch::create_default();rectangle.owner_container_id=mapping_fixture.id;
    static_cast<void>(rectangle.add_rectangle(-2,-2,2,2));
    const auto rectangle_mapping=document::sweep3d_profile_correspondence(rectangle);
    mapping_fixture.sweep3d.profiles.push_back({kernel::make_stable_id(),
        mapping_fixture.sweep3d.path.curve_points.back().id,rectangle.id,rectangle.serialized(),false});
    doc.history={mapping_fixture};
    auto mapped_body=k.evaluate_history(doc.kernel_operations());
    require(mapped_body.front().volume>0,"Four circle points could not loft to four rectangle corners");
    const auto mapping_preview=document::sweep3d_profiles_viewer_mesh(mapping_fixture);
    require(mapping_preview.constraint_markers.size()==8 &&
        mapping_preview.constraint_markers.front().label=="1 – začátek",
        "Mapping preview omitted first-point labels");
    mapping_fixture.sweep3d.profiles.back().correspondence_start_point_id=rectangle_mapping.point_ids[1];
    const auto rotated_mapping=document::sweep3d_profile_correspondence(rectangle,
        mapping_fixture.sweep3d.profiles.back().correspondence_start_point_id);
    require(rotated_mapping.point_ids.front()==rectangle_mapping.point_ids[1],
        "Selected first corner did not rotate correspondence order");
    doc.history={mapping_fixture};
    const auto mapping_file=std::filesystem::temp_directory_path()/"zima-sweep-mapping.prtz";
    doc.save(mapping_file);auto mapping_loaded=document::PartDocument::load(mapping_file);
    std::filesystem::remove(mapping_file);
    require(mapping_loaded.history.front().sweep3d.profiles.back().correspondence_start_point_id==
        rectangle_mapping.point_ids[1],"Profile correspondence start did not persist");
    mapped_body=k.evaluate_history(mapping_loaded.kernel_operations());
    require(mapped_body.front().volume>0,"Rotated correspondence did not calculate");
    auto missing_markers=fixture(0);missing_markers.sweep3d.path.curve_points.resize(2);
    missing_markers.sweep3d.profiles.push_back(mapping_fixture.sweep3d.profiles.back());
    missing_markers.sweep3d.profiles.back().point_id=missing_markers.sweep3d.path.curve_points.back().id;
    rectangle.owner_container_id=missing_markers.id;
    missing_markers.sweep3d.profiles.back().sketch_serialized=rectangle.serialized();
    doc.history={missing_markers};bool mismatch_rejected=false;
    try{static_cast<void>(doc.kernel_operations());}catch(const std::exception&){mismatch_rejected=true;}
    require(mismatch_rejected,"Unmarked circle was matched arbitrarily to rectangle corners");
    for(double phase:{0.0,std::numbers::pi/3}) {
        auto controlled=fixture(0);controlled.sweep3d.path.curve_points.resize(2);
        auto first=sketcher::Sketch::from_serialized(controlled.sweep3d.profiles.front().sketch_serialized);
        mark_circle(first,1);
        controlled.sweep3d.profiles.front().sketch_serialized=first.serialized();
        auto last=sketcher::Sketch::create_default();last.owner_container_id=controlled.id;
        static_cast<void>(last.add_circle(0,0,1));mark_circle(last,1,phase);
        controlled.sweep3d.profiles.push_back({kernel::make_stable_id(),
            controlled.sweep3d.path.curve_points.back().id,last.id,last.serialized(),false});
        doc.history={controlled};const auto result=k.evaluate_history(doc.kernel_operations());
        if(phase==0)require(std::abs(result.front().volume-30*std::numbers::pi)<1e-3,
            "Aligned single circle markers narrowed the sweep");
        else require(result.front().volume<30*std::numbers::pi-.1,
            "Explicit circle markers failed to control twist");
    }
    auto multi=fixture(5);
    auto last=document::PartDocument::create_construction(document::ConstructionKind::Point);
    last.parent_construction_id=multi.sweep3d.path.id;last.origin={30,25,45};
    multi.sweep3d.path.curve_points[2].curve_radius=4;
    multi.sweep3d.path.curve_points.push_back(last);
    doc.history={multi};const auto spatial_body=k.evaluate_history(doc.kernel_operations());
    require(spatial_body.size()==1&&spatial_body[0].volume>0,"Spatial multi-corner sweep failed");
    std::cout<<"3D curve/Sweep contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
