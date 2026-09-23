#include "transition_model.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace zima::research::transition;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void write_obj(const Result& result,const std::filesystem::path& path,bool flat) {
    std::ofstream out(path);out.precision(17);out<<"# Surface-only transition experiment. No thickness or finite bend radii.\n";
    std::size_t index=1;
    for(const auto& f:result.facets){for(auto p:flat?f.unfolded:f.folded)out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';out<<"f "<<index<<' '<<index+1<<' '<<index+2<<' '<<index+3<<'\n';index+=4;}
}
int main(int argc,char** argv)try {
    auto model=Model::example();auto before=model.evaluate();check(before.valid(),"Initial model");
    const auto original0=model.profiles[0].sketch.serialized(),original1=model.profiles[1].sketch.serialized();
    model.second_relative.origin.z=220;
    const auto taller=model.evaluate();check(taller.valid()&&taller.folded_area>before.folded_area,"Editable second Origin must regenerate model");
    check(model.profiles[0].sketch.serialized()==original0&&model.profiles[1].sketch.serialized()==original1,"Placement must not rewrite sketches");
    model.first_origin.origin={12,34,56};model.first_origin.x={0,1,0};model.first_origin.y={-1,0,0};
    const auto moved=model.evaluate();check(moved.valid()&&std::abs(moved.folded_area-taller.folded_area)<1e-7,"Root Origin must move both profiles rigidly");
    const auto source_id=model.profiles[1].curve_id;
    // Change an existing authored circular radius while preserving all point/curve IDs.
    auto& s=model.profiles[0].sketch;auto& arc=s.arcs.front();arc.radius=30;
    s.find_point(arc.start_point_id)->x=30;s.find_point(arc.end_point_id)->y=30;
    const auto edited=model.evaluate();check(edited.valid()&&std::abs(edited.folded_area-moved.folded_area)>1,"Sketch edit must change transition");
    check(model.profiles[1].curve_id==source_id,"Editing one profile changed other identity");
    auto reopened=model;
    for(auto& p:reopened.profiles)p.sketch=zima::sketcher::Sketch::from_serialized(p.sketch.serialized());
    const auto roundtrip=reopened.evaluate();check(roundtrip.valid()&&std::abs(roundtrip.folded_area-edited.folded_area)<1e-7,"Native sketch roundtrip changed model");
    model.options.facets=8;check(model.evaluate().facets.size()==8,"Editable facet count");
    model.second_relative.x={std::cos(.5),0,-std::sin(.5)};model.second_relative.z={std::sin(.5),0,std::cos(.5)};
    const auto rejected=model.evaluate();check(!rejected.valid()&&mesh(rejected).vertices.empty(),"Invalid edit must not present stale model");
    check(mesh(before).triangles.size()==24&&mesh(before,true).vertices.size()==16,"Presentation packet");
    if(argc==2){std::filesystem::path output(argv[1]);std::filesystem::create_directories(output);write_obj(before,output/"transition-surface.obj",false);write_obj(before,output/"transition-unfolded.obj",true);}
    std::cout<<"Editable native Sketch / relative Origin transition model contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
