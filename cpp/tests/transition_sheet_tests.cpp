#include "profile_request_fixture.hpp"
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
void check_reliefs(const SheetResult& sheet) {
    std::size_t count=0;
    for(const auto& panel:sheet.panels) {
        std::set<std::string> roles;
        for(std::size_t i=0;i<panel.edge_roles.size();++i) {
            check(roles.insert(panel.edge_roles[i]).second,"Duplicate authored panel edge identity");
            if(panel.edge_roles[i]!="apex-relief")continue;
            ++count;
            const auto next=(i+1)%panel.outer.size();
            const double formed=distance(panel.outer[i],panel.outer[next]);
            check(formed>1e-5,"Apex relief collapsed to a sharp point");
            check(std::abs(formed-distance(panel.developed[i],panel.developed[next]))<1e-7,"Apex cut stretched in development");
        }
    }
    check(count>0,"Converging bends retained their sharp apex");
}
void check_solid(const kernel::BodyResult& body){
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(body.kernel_shape);BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid transition B-Rep");
    std::size_t count=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++count;
    if(count!=1)throw std::runtime_error("Transition solid count: "+std::to_string(count));
}
int main()try {
    const auto start=std::chrono::steady_clock::now();HalfModel model;SheetOptions options;
    // Verify the clipped polygon identities before the more expensive solid checks.
    {RectangularModel probe;const double c=std::cos(.2),s=std::sin(.2);probe.second_relative.x={c,s,0};probe.second_relative.y={-s,c,0};check_reliefs(manufacture(probe,options));}
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
        const kernel::HistoryOperation cut{"cut",zima::test::SphericalRevolution{3,center},kernel::BooleanOperation::Subtract};
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
    for(unsigned sides:{2u,3u}) {
        RectangularModel rectangular;rectangular.sides=sides;
        const auto made=manufacture(rectangular,options);
        const auto op=sheet_operation(made,"rectangle");
        check_solid(kernel.evaluate_history({op}).back());
        check_solid(kernel.evaluate_history({op,{"flat",kernel::SheetStateRequest{true,true,{}}}}).back());
    }
    for(const auto angles:std::array<std::array<double,2>,3>{{{.15,0},{0,-.2},{.15,-.2}}}) {
        HalfModel tilted;const double x=angles[0],y=angles[1];
        tilted.second_relative.x={std::cos(y),0,-std::sin(y)};
        tilted.second_relative.y={std::sin(y)*std::sin(x),std::cos(x),std::cos(y)*std::sin(x)};
        tilted.second_relative.z={std::sin(y)*std::cos(x),-std::sin(x),std::cos(y)*std::cos(x)};
        std::cout<<"Combined tilt "<<x<<", "<<y<<std::endl;
        const auto made=manufacture(tilted,options);
        if(x!=0)check_reliefs(made);
        const auto combined=sheet_operation(made,"combined");
        std::cout<<"Direct developed combined tilt"<<std::endl;
        check_solid(kernel.evaluate_history({{"direct-flat",sheet_request(made,true)}}).back());
        auto material=kernel::sheet_material::regions_before({combined},1);
        const auto source_regions=material.regions;
        static_cast<void>(kernel::sheet_material::change(material,{true,true,{}}));
        for(std::size_t bend=0;bend<made.bends.size();++bend) {
            const auto& b=made.bends[bend];
            const auto owner="combined:bend:"+std::to_string(bend);
            const auto source=std::ranges::find(source_regions,owner,&kernel::SheetMaterialDefinition::owner_id);
            const auto target=std::ranges::find(material.regions,owner,&kernel::SheetMaterialDefinition::owner_id);
            for(unsigned end=0;end<2;++end) {
                const auto panel_owner="combined:panel:"+std::to_string(b.boundary_index+end);
                const auto psource=std::ranges::find(source_regions,panel_owner,&kernel::SheetMaterialDefinition::owner_id);
                const auto ptarget=std::ranges::find(material.regions,panel_owner,&kernel::SheetMaterialDefinition::owner_id);
                const auto& section=end?b.sections.back():b.sections.front();
                for(auto p:section) {
                    const auto on_bend=kernel::sheet_material::Transition{*source,*target}.map(p);
                    const auto on_panel=kernel::sheet_material::Transition{*psource,*ptarget}.map(p);
                    const double gap=distance(on_bend,on_panel);
                    if(gap>1e-6)throw std::runtime_error("Developed tangent gap at bend "+std::to_string(bend)+" end "+std::to_string(end)+": "+std::to_string(gap));
                }
            }
        }
        const auto formed=kernel.evaluate_history({combined}).back();check_solid(formed);
        const kernel::HistoryOperation flatten{"flat",kernel::SheetStateRequest{true,true,{}}};
        const kernel::HistoryOperation restore{"restore",kernel::SheetStateRequest{false,true,{}}};
        std::cout<<"Unfolding combined tilt"<<std::endl;
        const auto flat=kernel.evaluate_history({combined,flatten}).back();check_solid(flat);
        std::cout<<"Restoring combined tilt"<<std::endl;
        const auto restored=kernel.evaluate_history({combined,flatten,restore}).back();check_solid(restored);
        check(std::abs(formed.volume-restored.volume)<1e-5,"Combined tilt did not restore its material");
        check(std::abs(formed.volume-flat.volume)<formed.volume*2e-4,"Combined tilt lost material on unfolding");
    }
    for(unsigned sides:{2u,3u})for(bool combined:{false,true}) {
        RectangularModel rectangular;rectangular.sides=sides;
        const double x=combined?.13:0,y=combined?-.17:0,z=.2;
        const double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
        rectangular.second_relative={{0,0,150},{cz*cy,sz*cy,-sy},{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx},{cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx}};
        std::cout<<"Rectangular twist, sides "<<sides<<", combined "<<combined<<std::endl;
        const auto made=manufacture(rectangular,options);const auto op=sheet_operation(made,"rectangular-twist");
        check_reliefs(made);
        const auto formed=kernel.evaluate_history({op}).back();check_solid(formed);
        const kernel::HistoryOperation unfold{"rectangle-flat",kernel::SheetStateRequest{true,true,{}}};
        const kernel::HistoryOperation restore{"rectangle-restore",kernel::SheetStateRequest{false,true,{}}};
        const auto flat=kernel.evaluate_history({op,unfold}).back();check_solid(flat);
        const auto restored=kernel.evaluate_history({op,unfold,restore}).back();check_solid(restored);
        check(std::abs(formed.volume-flat.volume)<formed.volume*2e-4,"Rectangular twist lost material in development");
        check(std::abs(formed.volume-restored.volume)<1e-5,"Rectangular twist did not restore authored material");
    }
    std::cout<<"Finite-radius sheet study: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
