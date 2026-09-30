#include <zima/drawing/measurement_dimension.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace zima::drawing;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void compare(MeasurementPicker& picker,const DrawingView& view,Point2 p,double tolerance,
             const MeasurementPickRequest& request) {
    const auto expected=measurement_candidates(view,p,tolerance,request);
    const auto actual=picker.candidates(view,p,tolerance,request);
    require(expected.size()==actual.size(),"Prepared picker changed candidate count");
    for(std::size_t i=0;i<actual.size();++i) {
        require(expected[i].attachment==actual[i].attachment,"Prepared picker changed reference/order/side");
        require(expected[i].position==actual[i].position&&expected[i].distance==actual[i].distance&&
                expected[i].point_target==actual[i].point_target,"Prepared picker changed exact contact or priority");
        require(std::signbit(expected[i].position.x)==std::signbit(actual[i].position.x)&&
                std::signbit(expected[i].position.y)==std::signbit(actual[i].position.y),"Prepared picker changed contact coordinate signs");
    }
}
DrawingView fixture() {
    DrawingView view;view.camera={{1,0,0},{0,1,0},{0,0,1}};
    MeasurementGeometry geometry;
    for(int i=0;i<40;++i) {
        const zima::kernel::EdgeReference ref{"source","edge-"+std::to_string(i),i%2?"one":"two"};
        const double x=(i%10)*20,y=(i/10)*20;
        MeasurementCurve curve;curve.source=ref;curve.line=i%3!=0;
        if(curve.line)curve.points={{x,y,0},{x+10,y+5,0}};
        else {
            curve.circle=MeasurementCircle{{x,y,0},{0,0,1},{1,0,0},5};
            for(int j=0;j<=32;++j){const double a=j*2*std::acos(-1.)/32;curve.points.push_back({x+5*std::cos(a),y+5*std::sin(a),0});}
        }
        ProjectedEdge edge;edge.source=ref;edge.hidden=i%5==0;edge.tangent=i%7==0;
        for(auto p:curve.points)edge.points.push_back({p.x,p.y});
        view.projected_edges.push_back(edge);geometry.curves.push_back(curve);
        geometry.points.push_back({{"source","point-"+std::to_string(i),ref.instance_path},curve.points.front()});
    }
    view.measurement_geometry=share_measurement_geometry(std::move(geometry));
    ModelAnnotation axis;axis.kind=ModelAnnotationKind::Axis;axis.visible=true;
    axis.source={"doc","axis","axis","one"};axis.model_axis=std::array<zima::kernel::Vec3,2>{{{0,0,0},{0,10,0}}};
    view.model_annotations.push_back(axis);return view;
}
void verify() {
    auto view=fixture();MeasurementPicker picker;std::mt19937 random(42);
    std::uniform_real_distribution<double> x(-10,210),y(-10,90);
    const auto check=[&] {
        for(int mode=-1;mode<=int(DimensionAttachmentKind::Intersection);++mode) {
            MeasurementPickRequest request;request.mode=mode;request.tangent_direction={.6,.8};request.tangent_origin=Point2{-5,-7};
            for(int i=0;i<35;++i)compare(picker,view,{x(random),y(random)},2,request);
            for(const auto& e:view.projected_edges)compare(picker,view,e.points.front(),.1,request);
        }
        MeasurementPickRequest request;request.circles_only=true;compare(picker,view,{0,0},2,request);
        request={};request.curve_points=true;compare(picker,view,{25,2.5},.1,request);
        const auto contact=picker.candidates(view,{25,2.5},.1,request);
        require(std::ranges::none_of(contact,[](const auto& c){return c.attachment.kind==DimensionAttachmentKind::Line;}),
                "Point-pair request still offers whole lines");
        request.mode=int(DimensionAttachmentKind::Line);compare(picker,view,{25,2.5},.1,request);
        request={};request.lines_only=true;request.parallel_line=view.projected_edges[1].source;compare(picker,view,{25,2},2,request);
        request={};request.intersection_first=view.projected_edges[1].source;
        for(int i=0;i<40;++i)compare(picker,view,{x(random),y(random)},2,request);
    };
    check();view.display_style=DisplayStyle::HiddenEdges;check();
    view.tangent_edge_style=TangentEdgeStyle::Hidden;check();
    view.breaks.push_back({"break",false,22,7,2});check();
    view.scale=.25;check();view.model_annotations.front().visible=false;check();
    view.projected_edges.front().points.front().x+=3;check();
    auto packet=*view.measurement_geometry;packet.curves.front().points.front().x+=4;
    view.measurement_geometry=share_measurement_geometry(std::move(packet));check();
    view.camera.horizontal={0,1,0};view.camera.vertical={1,0,0};check();
    view.camera.horizontal.z=-0.;check();
    view.projected_edges.front().vertex_depths.assign(view.projected_edges.front().points.size(),-0.);check();
    view.projected_edges.front().thread_leadin=true;check();view.show_thread_leadins=true;check();
    // Position/zoom do not alter view-local contacts; the caller transforms the cursor.
    view.x+=100;view.y-=100;check();
    // Depth data is read live even when prepared curve inputs remain identical.
    view=fixture();view.output_source=std::make_shared<zima::kernel::ViewerMesh>();
    for(auto& e:view.projected_edges)e.vertex_depths.assign(e.points.size(),0);
    compare(picker,view,{25,2.5},2,{});
    require(!picker.candidates(view,{25,2.5},2,{}).empty(),"Depth fixture has no visible contact");
    ProjectedTriangle triangle;triangle.points={Point2{0,-20},Point2{200,-20},Point2{0,200}};triangle.vertex_depths={1,1,1};
    view.projected_triangles.push_back(triangle);compare(picker,view,{25,2.5},2,{});
    require(picker.candidates(view,{25,2.5},2,{}).empty(),"New foreground triangle did not occlude cached contacts");
    view.projected_triangles.front().vertex_depths={-1,-1,-1};compare(picker,view,{25,2.5},2,{});
    require(!picker.candidates(view,{25,2.5},2,{}).empty(),"Changed depth did not restore cached contacts");
}
void benchmark(const char* path) {
    const auto document=DrawingDocument::load(path);
    double total_before=0,total_after=0,total_preparation=0;std::size_t count=0;
    for(const auto& sheet:document.sheets)for(const auto& view:sheet.views) {
        MeasurementPicker picker;std::vector<Point2> probes;
        const auto stride=std::max(std::size_t{1},(view.projected_edges.size()+23)/24);
        for(std::size_t i=0;i<view.projected_edges.size();i+=stride) {
            const auto& edge=view.projected_edges[i];
            if(!edge.points.empty())probes.push_back(edge.points[edge.points.size()/2]);
        }
        // Eight screen pixels at three pixels per paper millimetre.
        const double tolerance=8/(3*view.scale);
        const auto clock=[] {return std::chrono::steady_clock::now();};
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        if(probes.empty())continue;
        auto start=clock();picker.prepare(view);total_preparation+=ms(start,clock());
        for(auto p:probes)compare(picker,view,p,tolerance,{});
        for(int mode=0;mode<=int(DimensionAttachmentKind::Intersection);++mode) {
            MeasurementPickRequest request;request.mode=mode;
            for(std::size_t i=0;i<std::min(std::size_t{4},probes.size());++i)compare(picker,view,probes[i],tolerance,request);
        }
        start=clock();for(auto p:probes)measurement_candidates(view,p,tolerance,{});const auto before=ms(start,clock());
        start=clock();for(auto p:probes)picker.candidates(view,p,tolerance,{});const auto after=ms(start,clock());
        total_before+=before;total_after+=after;count+=probes.size();
        std::cout<<view.name<<": baseline="<<before/probes.size()<<" ms, prepared="<<after/probes.size()<<" ms per hover; tolerance="<<tolerance<<" model mm\n";
    }
    require(count>0,"Benchmark has no geometry");
    std::cout<<"Preparation="<<total_preparation<<" ms; "<<count<<" identical hover queries, speedup="<<total_before/total_after<<"x\n";
}
}
int main(int argc,char** argv) {
    try {verify();if(argc==2)benchmark(argv[1]);std::cout<<"Measurement picker equivalence and invalidation passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
