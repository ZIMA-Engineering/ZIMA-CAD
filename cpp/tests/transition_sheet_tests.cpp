#include "transition_sheet.hpp"
#include "transition_sketches.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <sstream>
using namespace zima;
using namespace research::transition;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double distance(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
void check_solid(const kernel::BodyResult& body){
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(body.kernel_shape);BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid transition B-Rep");
    std::size_t count=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++count;
    check(count==1,"Transition is not one connected solid");
}
int main()try {
    const auto start=std::chrono::steady_clock::now();HalfModel model;SheetOptions options;
    auto circle=sketcher::Sketch::create_default(),rectangle=sketcher::Sketch::create_default();
    static_cast<void>(circle.add_arc(0,0,80,0,-80,0));
    static_cast<void>(rectangle.add_segment(100,0,100,60));static_cast<void>(rectangle.add_arc(80,60,100,60,80,80));
    static_cast<void>(rectangle.add_segment(80,80,-80,80));static_cast<void>(rectangle.add_arc(-80,60,-80,80,-100,60));static_cast<void>(rectangle.add_segment(-100,60,-100,0));
    rectangle.plane_offset=150;
    const auto authored=read_sketches(circle,rectangle);check(authored.model.width==200&&authored.model.radius==80,"Native input dimensions");
    check(distance(authored.model.second_relative.origin,{0,0,150})<1e-7,"Native Sketch frame ignored");
    auto filleted=sketcher::Sketch::create_default();
    const auto right=filleted.add_segment(100,0,100,80),top=filleted.add_segment(100,80,-100,80),left=filleted.add_segment(-100,80,-100,0);
    static_cast<void>(filleted.add_corner_fillet(right,top,20));static_cast<void>(filleted.add_corner_fillet(top,left,20));filleted.plane_offset=150;
    const auto native_fillets=read_sketches(circle,filleted);
    check(native_fillets.model.corner_radius==20&&native_fillets.model.width==200,"Native corner fillets not supported");
    const auto sheet=manufacture(model,options);check(sheet.bends.size()==6,"Expected six real bends");
    for(const auto& bend:sheet.bends) {
        check(std::abs(bend.outer_radius-options.inside_radius-options.thickness)<1e-10,"Exterior radius");
        for(const auto& section:bend.sections)check(std::abs(distance(section[0],section[3])-options.thickness)<1e-8,"Nonconstant bend thickness");
        check(bend.material.thickness_sign==1,"Inner skin identity lost");
    }
    for(const auto& axis:sheet.axes)check(std::abs(axis.first.z-options.thickness)<1e-8&&std::abs(axis.second.z-options.thickness)<1e-8,"Axis not on developed inner skin");
    kernel::OcctKernel kernel;
    for(bool flat:{false,true}) {
        const auto request=sheet_request(sheet,flat);
        std::cout<<(flat?"unfolded":"formed")<<" children "<<request.children.size()<<std::endl;
        const auto body=kernel.evaluate_history({{"sheet-transition-study",request}}).back();
        check(body.volume>0,"No sheet volume");
        check_solid(body);
        std::cout<<"volume "<<body.volume<<" triangles "<<body.mesh.triangles.size()/3<<std::endl;
    }
    const auto operation=sheet_operation(sheet,"transition");
    {
        auto changed=operation;auto& group=std::get<kernel::FeatureGroupRequest>(changed.primitive);
        const auto bend=std::ranges::find_if(group.children,[](const auto& p){return std::holds_alternative<kernel::Sweep3DRequest>(p);});
        auto& sweep=std::get<kernel::Sweep3DRequest>(*bend);sweep.fixed_section_frames=false;
        check(kernel::history_fingerprint({operation},1)!=kernel::history_fingerprint({changed},1),"Fixed section frames missing from fingerprint");
        sweep.fixed_section_frames=true;sweep.smooth_loft=false;
        check(kernel::history_fingerprint({operation},1)!=kernel::history_fingerprint({changed},1),"Smooth loft missing from fingerprint");
    }
    std::cout<<"Native Unbend/Bend Back"<<std::endl;
    const auto states=kernel.evaluate_history({operation,{"unbend",kernel::SheetStateRequest{true,true,{}}},{"bend-back",kernel::SheetStateRequest{false,true,{}}}});
    check(states.size()==3,"Missing native state boundary");
    // Intermediate history boundaries intentionally omit their kernel snapshot.
    // Calculate the requested final boundary when inspecting its solid topology.
    check_solid(states.back());
    check_solid(kernel.evaluate_history({operation,{"unbend",kernel::SheetStateRequest{true,true,{}}}}).back());
    for(const auto& state:states)check(std::abs(state.volume-states[0].volume)<states[0].volume*1e-4,"Native sheet state changed volume");
    check(states[1].mesh.axes.size()>=6,"Unbend did not publish inner-skin bend axes");
    {
        using namespace kernel::sheet_material;
        Vec3 center{};for(auto p:sheet.panels.front().outer)center=add(center,p);
        center=mul(center,1./sheet.panels.front().outer.size());
        const kernel::HistoryOperation cut{"cut",kernel::SphereRequest{3,center},kernel::BooleanOperation::Subtract};
        const auto cut_states=kernel.evaluate_history({operation,cut,{"unbend-cut",kernel::SheetStateRequest{true,true,{}}},{"bend-cut-back",kernel::SheetStateRequest{false,true,{}}}});
        check(cut_states[1].volume<states.front().volume-1,"Cut removed no sheet material");
        check_solid(cut_states.back());
        check(std::abs(cut_states.back().volume-cut_states[1].volume)<1e-3,"Cut was lost after Unbend/Bend Back");
    }
    for(double angle:{-.3,.3}) {
        HalfModel tilted;tilted.second_relative.x={std::cos(angle),0,-std::sin(angle)};tilted.second_relative.z={std::sin(angle),0,std::cos(angle)};
        const auto bent=manufacture(tilted,{1,1,.35});
        check_solid(kernel.evaluate_history({sheet_operation(bent,"tilted")}).back());
        check_solid(kernel.evaluate_history({sheet_operation(bent,"tilted"),{"tilted-flat",kernel::SheetStateRequest{true,true,{}}}}).back());
    }
    std::cout<<"Finite-radius sheet study: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
