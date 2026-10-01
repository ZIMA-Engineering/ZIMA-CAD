#include "profile_solid_fixture.hpp"
#include <zima/measurement/measurement.hpp>
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
#include <chrono>
#include <numbers>
#include <stdexcept>

using namespace zima;
using P=kernel::Vec3;
using G=measurement::MeasurementGeometry;
using K=kernel::MeasurementKind;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message,double epsilon=1e-8){require(std::abs(value-expected)<=epsilon,message);}
G point(P p){G g;g.points={p};return g;}
G line(P a,P b){G g;g.segments={{a,b}};return g;}
G axis(P a,P direction){G g;g.axis={{a,direction}};return g;}
G plane(P a,P normal){G g;g.plane={{a,normal}};return g;}
void distance(const G& a,const G& b,double expected){
    const auto d=measurement::measure_distance(a,b),reverse=measurement::measure_distance(b,a);
    require(d.has_value()&&reverse.has_value(),"Distance unavailable");
    near(d->distance.value,expected,"Incorrect shortest distance");
    near(reverse->distance.value,expected,"Distance is not symmetric");
    near(std::hypot(d->first.x-d->second.x,d->first.y-d->second.y,d->first.z-d->second.z),expected,"Witness endpoints disagree");
}
int main(int argc,char** argv){
try{
    if(argc==3&&std::string(argv[1])=="--surface-file") {
        std::vector<kernel::BodyResult> calculated;
        const auto loaded=document::PartDocument::load(argv[2],&calculated);
        require(!calculated.empty(),"Native measurement fixture has no calculated body");
        const auto& mesh=calculated.back().mesh;
        const auto original=document::serialize_body_result(calculated.back(),false);
        std::optional<G> first;
        const auto start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<12;++repeat){
            auto measured=measurement::measure_entity(mesh,{K::Object,{},{},{}});
            require(measured&&measured->values.area,"Native object measurement unavailable");
            if(!first)first=measured;
            require(measured->values==first->values&&measured->solid==first->solid&&measured->approximate==first->approximate,
                "Repeated native measurement changed its result");
        }
        std::cout<<"Native object triangles="<<mesh.triangle_references.size()<<" mean_ms="
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/12<<"\n";
        std::cout<<"Native result solid="<<first->solid<<" approximate="<<first->approximate<<" area="<<std::hexfloat<<first->values.area->value;
        if(first->values.volume)std::cout<<" volume="<<first->values.volume->value;
        std::cout<<std::defaultfloat<<"\n";
        require(document::serialize_body_result(calculated.back(),false)==original,"Measurement changed source geometry");
    }
    if(argc==2&&std::string(argv[1])=="--benchmark")for(int side:{50,200}) {
        G mesh;
        for(int x=0;x<side;++x)for(int y=0;y<side;++y)
            mesh.triangles.push_back({P{double(x),double(y),0},P{double(x+1),double(y),0},P{double(x),double(y+1),0}});
        const auto start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<20;++repeat) {
            const auto result=measurement::measure_distance(point({20.25,20.25,8}),mesh);
            require(result.has_value(),"Repeated measurement unavailable");
            near(result->distance.value,8,"Repeated measurement changed exact distance");
            near(result->second.x,20.25,"Repeated measurement changed witness X");
            near(result->second.y,20.25,"Repeated measurement changed witness Y");
            near(result->second.z,0,"Repeated measurement changed witness Z");
        }
        std::cout<<"Repeated point/mesh measurement triangles="<<mesh.triangles.size()<<" mean_ms="<<
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20<<"\n";
    }
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
    auto box=zima::test::rectangular_feature(part,{10,20,30});
    part.history={box};
    auto bodies=kernel.evaluate_history(part.kernel_operations());
    const auto object=measurement::measure_entity(bodies.back().mesh,{K::Object,box.id,{},{}});
    require(object&&object->solid&&object->values.volume,"Closed solid was not measured");
    near(object->values.volume->value,6000,"Box volume incorrect");
    near(object->values.area->value,2200,"Box surface area incorrect");
    // Repeated disconnected closed boxes stress edge accounting without OCCT work
    // in the measured operation. Area/volume are independently known.
    if(argc==2&&std::string(argv[1])=="--surface-benchmark")for(int count:{64,2048}) {
        kernel::ViewerMesh mesh;
        const auto& base=bodies.back().mesh;
        for(int copy=0;copy<count;++copy) {
            const auto offset=static_cast<unsigned int>(mesh.vertices.size());
            for(auto vertex:base.vertices) {vertex.x+=100*(copy%64);vertex.y+=100*(copy/64);mesh.vertices.push_back(vertex);}
            for(auto index:base.triangles)mesh.triangles.push_back(offset+index);
            for(auto ref:base.triangle_references) {ref.measured_area.reset();mesh.triangle_references.push_back(std::move(ref));}
        }
        const auto start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<12;++repeat) {
            const auto measured=measurement::measure_entity(mesh,{K::Object,{},{},{}});
            require(measured&&measured->solid&&measured->values.volume&&measured->values.area,"Closed multi-solid mesh lost properties");
            near(measured->values.volume->value,6000.*count,"Repeated box volume incorrect",1e-4);
            near(measured->values.area->value,2200.*count,"Repeated box area incorrect",1e-6);
            if(repeat==0)std::cout<<"Surface result boxes="<<count<<" volume="<<std::hexfloat<<measured->values.volume->value
                <<" area="<<measured->values.area->value<<std::defaultfloat<<" approximate="<<measured->approximate<<"\n";
        }
        std::cout<<"Object measurement boxes="<<count<<" triangles="<<mesh.triangle_references.size()<<" mean_ms="
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/12<<"\n";
    }
    {
        const auto& base=bodies.back().mesh;
        const auto verify=[&](kernel::ViewerMesh mesh,bool closed,const char* message) {
            const auto measured=measurement::measure_entity(mesh,{K::Object,{},{},{}});
            require(measured&&measured->solid==closed,message);
            require(measured->values.volume.has_value()==closed,"Volume availability disagrees with closure");
            if(closed)near(measured->values.volume->value,6000,"Oriented closed mesh volume changed",1e-5);
        };
        auto mesh=base;mesh.triangles.resize(mesh.triangles.size()-3);mesh.triangle_references.pop_back();
        verify(mesh,false,"Open surface accepted as a solid");
        mesh=base;std::swap(mesh.triangles[0],mesh.triangles[1]);verify(mesh,false,"One reversed triangle accepted as a solid");
        mesh=base;for(std::size_t i=0;i<mesh.triangles.size();i+=3)std::swap(mesh.triangles[i],mesh.triangles[i+1]);
        verify(mesh,true,"Completely reversed closed surface rejected");
        mesh=base;for(int i=0;i<3;++i)mesh.triangles.push_back(base.triangles[i]);mesh.triangle_references.push_back(base.triangle_references.front());
        verify(mesh,false,"Non-manifold repeated triangle accepted as a solid");
        mesh=base;mesh.triangles.insert(mesh.triangles.end(),3,mesh.triangles.front());mesh.triangle_references.push_back(base.triangle_references.front());
        verify(mesh,true,"Collapsed triangle changed established closure classification");
        mesh=base;for(auto& vertex:mesh.vertices){vertex.x+=1e7;vertex.y-=1e7;vertex.z+=1e7;}
        verify(mesh,true,"Translated closed mesh lost volume or closure");
    }
    distance(*object,point({0,0,0}),0); // A contained point is in the solid.
    {
        kernel::ViewerMesh patch;
        patch.vertices={{0,0,0},{2,0,0},{0,2,0},{2,2,0}};
        patch.triangles={0,1,2,1,3,2};
        auto ref=bodies.back().mesh.triangle_references.front();
        ref.owner_id="face-owner";ref.semantic_key="face-key";ref.instance_path="root";ref.measured_area=4;
        patch.triangle_references={ref,ref};
        const auto check_area=[&](double expected,bool approximate){
            const auto measured=measurement::measure_entity(patch,{K::Object,{}, {},"root"});
            require(measured&&measured->values.area,"Repeated-face area unavailable");
            require(measured->values.area->value==expected&&measured->values.area->approximate==approximate,
                "Repeated-face area merged identities or lost the last known/unknown value");
        };
        check_area(4,false);
        patch.triangle_references[1].measured_area=5;check_area(5,false);
        patch.triangle_references[1].measured_area.reset();check_area(4,true);
        patch.triangle_references[0].measured_area.reset();patch.triangle_references[1].measured_area=4;check_area(4,false);
        for(int identity=0;identity<3;++identity){
            patch.triangle_references={ref,ref};
            patch.triangle_references[0].measured_area=2;patch.triangle_references[1].measured_area=3;
            auto& other=patch.triangle_references[1];
            if(identity==0)other.owner_id="other-owner";
            else if(identity==1)other.semantic_key="other-face";
            else other.instance_path="root/other-occurrence";
            check_area(5,false);
        }
        patch.triangle_references={ref,ref,ref};patch.triangles.insert(patch.triangles.end(),{0,1,2});
        patch.triangle_references[0].measured_area=2;
        patch.triangle_references[1].semantic_key="other-face";patch.triangle_references[1].measured_area=3;
        check_area(7,false); // The third triangle returns to the original face.
        patch.triangles[3]=static_cast<unsigned int>(patch.vertices.size());
        check_area(4,false); // An invalid triangle does not contribute metadata.
    }
    auto shifted=bodies.back().mesh;
    for(auto& p:shifted.original_references.vertices){p.x+=1e9;p.y-=1e9;p.z+=1e9;}
    const auto shifted_object=measurement::measure_entity(shifted,{K::Object,box.id,{},{}});
    require(shifted_object&&shifted_object->values.volume,"Translated solid disappeared");
    near(shifted_object->values.volume->value,6000,"Volume loses precision far from origin");
    auto cylinder=zima::test::circular_feature(part,5,12);
    part.history={cylinder};bodies=kernel.evaluate_history(part.kernel_operations());
    const auto loaded=document::load_body_result(document::serialize_body_result(bodies.back()));
    bool circular_edge=false,cylindrical_face=false;
    for(const auto& edge:loaded.mesh.original_references.edges){
        require(edge.measured_length.has_value(),"Calculated edge length was not persisted");
        auto measured=measurement::measure_entity(loaded.mesh,{K::Curve,edge.reference.owner_id,edge.reference.semantic_key,edge.reference.instance_path});
        require(measured&&measured->values.length&&!measured->values.length->approximate,"Exact edge length unavailable");
        if(std::abs(*edge.measured_length-10*std::numbers::pi)<1e-6){
            circular_edge=true;near(measured->values.length->value,10*std::numbers::pi,"Circle length follows tessellation",1e-6);
        }
    }
    for(const auto& face:loaded.mesh.original_references.triangle_references){
        require(face.measured_area.has_value(),"Calculated face area was not persisted");
        if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){
            cylindrical_face=true;
            auto measured=measurement::measure_entity(loaded.mesh,{K::Face,face.owner_id,face.semantic_key,face.instance_path});
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
    saved.values={measurement::measure_entity(loaded.mesh,saved.references[0])->values};
    require(document::parse_measurements(document::serialize_measurements({saved}))==std::vector{saved},"Record roundtrip changed measurement");
    part.measurements={saved};
    const auto directory=std::filesystem::temp_directory_path()/part.document_id;
    std::filesystem::create_directories(directory);
    part.save(directory/"measurement.prtz",bodies);
    std::vector<kernel::BodyResult> restored;
    const auto reloaded=document::PartDocument::load(directory/"measurement.prtz",&restored);
    require(reloaded.measurements==part.measurements,"Part lost saved measurement");
    require(!restored.empty()&&measurement::measure_entity(restored.back().mesh,saved.references[0]).has_value(),"Saved Part lost measurement reference");
    auto assembly=assembly::AssemblyDocument::create_default();assembly.measurements={saved};
    assembly.save(directory/"measurement.asmz");
    require(assembly::AssemblyDocument::load(directory/"measurement.asmz").measurements==assembly.measurements,"Assembly lost saved measurement");
    document::DocumentSession session(part,bodies);auto next=part;next.measurements.clear();session.commit(next,bodies);
    require(session.undo()&&session.document().measurements==part.measurements,"Measurement history cannot undo");
    auto missing=saved;missing.references[0].semantic_key="missing";
    require(!measurement::measure_entity(loaded.mesh,missing.references[0]),"Missing reference silently changed owner");
    kernel::ViewerDimension d;d.kind=kernel::ViewerDimensionKind::Diameter;d.label_prefix="⌀";d.value=12;d.unit_suffix=" mm";
    require(kernel::dimension_text(d,kernel::dimension_text_style(d))=="⌀12mm","Diameter is not U+2300, is duplicated, or unit spacing is wrong");
    require(kernel::dimension_number(10,3)=="10"&&kernel::dimension_number(10.5,3)=="10,5"&&
            kernel::dimension_number(10.52584,3)=="10,526"&&kernel::dimension_number(-.0001,3)=="0"&&
            kernel::dimension_number(9.9999,3)=="10"&&kernel::dimension_number(50.5,3)=="50,5",
            "Nominal dimension formatting lost comma, rounding or zero trimming");
    const viewer::ViewerCandidate external{viewer::CandidateKind::SketchExternalReference,0,0,"sketch","external_point:p",{}};
    require(viewer::measurement_reference(external)->kind==K::Point,"External Sketch point resolved as a curve");
    for(const auto* display_key:{"sketch","solid","plane","container:display"}) {
        const viewer::ViewerCandidate candidate{viewer::CandidateKind::Container,0,0,"source-object",display_key,{}};
        const auto reference=viewer::measurement_reference(candidate);
        require(reference&&reference->kind==K::Object&&reference->owner_id=="source-object"&&reference->semantic_key.empty(),
            "Whole-object measurement retained a display classification as topology identity");
    }
    std::filesystem::remove_all(directory);
    std::cout<<"Measurement geometry, analytic cache, persistence and units passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
