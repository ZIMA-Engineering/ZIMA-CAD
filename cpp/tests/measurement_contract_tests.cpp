#include <zima/viewer/measurement.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima;
using P=kernel::Vec3;
using G=viewer::MeasurementGeometry;
using K=kernel::MeasurementKind;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message,double epsilon=1e-8){require(std::abs(value-expected)<=epsilon,message);}
G point(P p){G g;g.points={p};return g;}
G line(P a,P b){G g;g.segments={{a,b}};return g;}
G axis(P a,P direction){G g;g.axis={{a,direction}};return g;}
G plane(P a,P normal){G g;g.plane={{a,normal}};return g;}
void distance(const G& a,const G& b,double expected){
    const auto d=viewer::measure_distance(a,b),reverse=viewer::measure_distance(b,a);
    require(d.has_value()&&reverse.has_value(),"Distance unavailable");
    near(d->distance.value,expected,"Incorrect shortest distance");
    near(reverse->distance.value,expected,"Distance is not symmetric");
    near(std::hypot(d->first.x-d->second.x,d->first.y-d->second.y,d->first.z-d->second.z),expected,"Witness endpoints disagree");
}
int main(){
try{
    distance(point({0,0,0}),point({3,4,0}),5);
    distance(point({3,4,0}),line({0,0,0},{1,0,0}),std::sqrt(20.));
    distance(point({3,4,0}),axis({0,0,0},{1,0,0}),4);
    distance(line({-2,0,0},{2,0,0}),line({0,-2,3},{0,2,3}),3);
    distance(axis({0,0,0},{1,0,0}),axis({0,0,3},{0,1,0}),3);
    distance(plane({0,0,0},{0,0,1}),plane({0,0,7},{0,0,1}),7);
    distance(plane({0,0,0},{0,0,1}),plane({0,0,7},{0,1,0}),0);
    distance(plane({0,0,0},{0,0,1}),axis({0,0,7},{0,0,1}),0);
    distance(plane({0,0,0},{0,0,1}),axis({0,0,7},{1,0,0}),7);
    G triangle;triangle.triangles={{{{0,0,0},{2,0,0},{0,2,0}}}};
    distance(point({.5,.5,3}),triangle,3);
    distance(point({2,2,0}),triangle,std::sqrt(2.)); // Outside the trimmed face.
    distance(line({.5,.5,-1},{.5,.5,1}),triangle,0);
    G crossing;crossing.triangles={{{{.5,-1,-1},{.5,2,-1},{.5,.5,1}}}};
    distance(triangle,crossing,0);
    G grid;for(int x=0;x<50;++x)for(int y=0;y<50;++y)grid.triangles.push_back({P{double(x),double(y),0},P{double(x+1),double(y),0},P{double(x),double(y+1),0}});
    distance(point({20.25,20.25,8}),grid,8); // Exercises spatial pruning.
    kernel::OcctKernel kernel;
    auto part=document::PartDocument::create_default();
    auto box=document::PartDocument::create_box_container();box.box={10,20,30};
    part.history={box};
    auto bodies=kernel.evaluate_history(part.kernel_operations());
    const auto object=viewer::measure_entity(bodies.back().mesh,{K::Object,box.id,{},{}});
    require(object&&object->solid&&object->values.volume,"Closed solid was not measured");
    near(object->values.volume->value,6000,"Box volume incorrect");
    near(object->values.area->value,2200,"Box surface area incorrect");
    distance(*object,point({0,0,0}),0); // A contained point is in the solid.
    auto shifted=bodies.back().mesh;
    for(auto& p:shifted.original_references.vertices){p.x+=1e9;p.y-=1e9;p.z+=1e9;}
    const auto shifted_object=viewer::measure_entity(shifted,{K::Object,box.id,{},{}});
    require(shifted_object&&shifted_object->values.volume,"Translated solid disappeared");
    near(shifted_object->values.volume->value,6000,"Volume loses precision far from origin");
    auto cylinder=document::PartDocument::create_cylinder_container();cylinder.cylinder.radius=5;cylinder.cylinder.height=12;
    part.history={cylinder};bodies=kernel.evaluate_history(part.kernel_operations());
    const auto loaded=document::load_body_result(document::serialize_body_result(bodies.back()));
    bool circular_edge=false,cylindrical_face=false;
    for(const auto& edge:loaded.mesh.original_references.edges){
        require(edge.measured_length.has_value(),"Calculated edge length was not persisted");
        auto measured=viewer::measure_entity(loaded.mesh,{K::Curve,edge.reference.owner_id,edge.reference.semantic_key,edge.reference.instance_path});
        require(measured&&measured->values.length&&!measured->values.length->approximate,"Exact edge length unavailable");
        if(std::abs(*edge.measured_length-10*std::numbers::pi)<1e-6){
            circular_edge=true;near(measured->values.length->value,10*std::numbers::pi,"Circle length follows tessellation",1e-6);
        }
    }
    for(const auto& face:loaded.mesh.original_references.triangle_references){
        require(face.measured_area.has_value(),"Calculated face area was not persisted");
        if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){
            cylindrical_face=true;
            auto measured=viewer::measure_entity(loaded.mesh,{K::Face,face.owner_id,face.semantic_key,face.instance_path});
            require(measured&&measured->values.area&&!measured->values.area->approximate,"Cylinder area marked approximate");
            near(measured->values.area->value,120*std::numbers::pi,"Cylinder area follows tessellation",1e-5);
            require(measured->approximate,"Curved surface distance did not disclose approximation");
            break;
        }
    }
    require(circular_edge&&cylindrical_face,"Cylinder test did not exercise analytic metrics");
    const auto& edge=loaded.mesh.original_references.edges.front();
    kernel::SavedMeasurement saved;saved.id="measurement-test";saved.name="Saved measurement";saved.after_object_id=cylinder.id;
    saved.references={{K::Curve,edge.reference.owner_id,edge.reference.semantic_key,{}}};
    saved.values={viewer::measure_entity(loaded.mesh,saved.references[0])->values};
    require(document::parse_measurements(document::serialize_measurements({saved}))==std::vector{saved},"Record roundtrip changed measurement");
    part.measurements={saved};
    const auto directory=std::filesystem::temp_directory_path()/part.document_id;
    std::filesystem::create_directories(directory);
    part.save(directory/"measurement.prtz",bodies);
    std::vector<kernel::BodyResult> restored;
    const auto reloaded=document::PartDocument::load(directory/"measurement.prtz",&restored);
    require(reloaded.measurements==part.measurements,"Part lost saved measurement");
    require(!restored.empty()&&viewer::measure_entity(restored.back().mesh,saved.references[0]).has_value(),"Saved Part lost measurement reference");
    auto assembly=assembly::AssemblyDocument::create_default();assembly.measurements={saved};
    assembly.save(directory/"measurement.asmz");
    require(assembly::AssemblyDocument::load(directory/"measurement.asmz").measurements==assembly.measurements,"Assembly lost saved measurement");
    document::DocumentSession session(part,bodies);auto next=part;next.measurements.clear();session.commit(next,bodies);
    require(session.undo()&&session.document().measurements==part.measurements,"Measurement history cannot undo");
    auto missing=saved;missing.references[0].semantic_key="missing";
    require(!viewer::measure_entity(loaded.mesh,missing.references[0]),"Missing reference silently changed owner");
    kernel::ViewerDimension d;d.kind=kernel::ViewerDimensionKind::Diameter;d.label_prefix="⌀";d.value=12;d.unit_suffix=" mm";
    require(kernel::dimension_text(d,kernel::dimension_text_style(d))=="⌀12mm","Diameter is not U+2300, is duplicated, or unit spacing is wrong");
    require(kernel::dimension_number(10,3)=="10"&&kernel::dimension_number(10.5,3)=="10,5"&&
            kernel::dimension_number(10.52584,3)=="10,526"&&kernel::dimension_number(-.0001,3)=="0"&&
            kernel::dimension_number(9.9999,3)=="10"&&kernel::dimension_number(50.5,3)=="50,5",
            "Nominal dimension formatting lost comma, rounding or zero trimming");
    const viewer::ViewerCandidate external{viewer::CandidateKind::SketchExternalReference,0,0,"sketch","external_point:p",{}};
    require(viewer::measurement_reference(external)->kind==K::Point,"External Sketch point resolved as a curve");
    std::filesystem::remove_all(directory);
    std::cout<<"Measurement geometry, analytic cache, persistence and units passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
