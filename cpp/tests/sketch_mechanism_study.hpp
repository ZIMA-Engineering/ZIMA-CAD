#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::test::mechanism {
using namespace sketcher;
using XY = std::array<double,2>;
constexpr double radians = std::numbers::pi / 180.;
inline void require(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error(message);
}
inline XY circle_intersection(XY a,double r,XY d,double l,int branch) {
    const double x=d[0]-a[0],y=d[1]-a[1],distance=std::hypot(x,y);
    const double along=(r*r-l*l+distance*distance)/(2*distance);
    const double height2=r*r-along*along;
    require(distance>1e-9&&height2>=-1e-9,"Unreachable analytic four-bar pose");
    const double height=branch*std::sqrt(std::max(0.,height2));
    return {a[0]+(along*x-height*y)/distance,a[1]+(along*y+height*x)/distance};
}
enum class Kind { SliderCrank, FourBar, MidpointBranch, Watt, ScottRussell };
inline const char* name(Kind kind) {
    switch(kind) {
    case Kind::SliderCrank:return "slider-crank";
    case Kind::FourBar:return "four-bar";
    case Kind::MidpointBranch:return "midpoint-branch";
    case Kind::Watt:return "Watt";
    default:return "Scott-Russell";
    }
}
struct Fixture {
    Sketch sketch = Sketch::create_default();
    Kind kind{};
    int branch=1;
    double rotation{};
    bool reverse{};
    std::string o,a,b,d,p,slider,crank,angle;
    std::vector<std::pair<std::string,double>> lengths;
    std::vector<std::pair<std::string,XY>> anchors;
    XY world(XY point) const {
        const double c=std::cos(rotation*radians),s=std::sin(rotation*radians);
        return {c*point[0]-s*point[1],s*point[0]+c*point[1]};
    }
    std::vector<std::pair<std::string,XY>> pose(double degrees) const {
        const double c=std::cos(degrees*radians),s=std::sin(degrees*radians);
        const double r=kind==Kind::Watt?100:30;
        const XY first{r*c,r*s};
        XY second{},middle{},output{};
        if(kind==Kind::SliderCrank)second={first[0]+branch*std::sqrt(10000-first[1]*first[1]),0};
        else if(kind==Kind::ScottRussell) {
            second={0,60*s};middle=first;output={60*c,0};
        } else {
            second=circle_intersection(first,kind==Kind::Watt?160:90,
                {kind==Kind::Watt?120.:100.,0},kind==Kind::Watt?100:70,branch);
            middle={(first[0]+second[0])*.5,(first[1]+second[1])*.5};
            if(kind==Kind::MidpointBranch)
                output={middle[0]+branch*std::sqrt(140*140-std::pow(-50-middle[1],2)),-50};
        }
        std::vector<std::pair<std::string,XY>> result{{a,world(first)},{b,world(second)}};
        if(!p.empty())result.emplace_back(p,world(middle));
        if(!slider.empty())result.emplace_back(slider,world(output));
        return result;
    }
    double angle_value(double degrees) const {
        return std::remainder(degrees+rotation+(reverse?180:0),360.);
    }
};
inline Fixture make(Kind kind,int branch,double rotation,bool reverse,bool external) {
    Fixture f;f.kind=kind;f.branch=branch;f.rotation=rotation;f.reverse=reverse;
    auto& s=f.sketch;
    const auto point=[&](XY p){p=f.world(p);return s.add_point(p[0],p[1]);};
    const auto segment=[&](const std::string& a,const std::string& b) {
        const auto *first=s.find_point(reverse?b:a),*second=s.find_point(reverse?a:b);
        return s.add_segment(first->x,first->y,second->x,second->y);
    };
    const auto anchor=[&](const std::string& id) {
        const auto* p=s.find_point(id);f.anchors.emplace_back(id,XY{p->x,p->y});
        if(!external)s.find_point(id)->fixed=true;
        else {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
            ref.source_document_id="mechanism-ground";ref.source_owner_id="ground";
            ref.source_semantic_key="pivot-"+std::to_string(s.external_references.size());
            ref.cached_points={{p->x,p->y}};s.external_references.push_back(ref);
            static_cast<void>(s.add_point_reference_constraint(id,ref.id));
        }
    };
    const auto guide=[&](const std::string& id,double height) {
        std::string support;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
            ref.source_document_id="mechanism-ground";ref.source_owner_id="ground";
            ref.source_semantic_key="slider-guide";ref.infinite=true;
            ref.cached_points={f.world({-300,height}),f.world({300,height})};
            support=ref.id;s.external_references.push_back(ref);
        } else if(rotation==0&&height==0)support="sketch_axis:x";
        else {
            const auto first=point({-300,height}),second=point({300,height});
            s.find_point(first)->fixed=true;s.find_point(second)->fixed=true;
            support=segment(first,second);s.set_geometry_construction(support,true);
        }
        static_cast<void>(s.add_point_on_line_constraint(id,support));
    };
    const auto length=[&](const std::string& id) {
        auto dim=s.create_segment_dimension(id);dim.locked=true;
        s.apply_dimension(dim);f.lengths.emplace_back(dim.id,dim.value);
    };
    f.o=point({0,0});anchor(f.o);
    // Create the exact initial assembly from closed-form geometry, then use
    // only native editing operations. The oracle is never used to seed edits.
    f.a="a";f.b="b";
    if(kind==Kind::ScottRussell){f.p="p";f.slider="slider";}
    else if(kind==Kind::MidpointBranch||kind==Kind::Watt)f.p="p";
    if(kind==Kind::MidpointBranch)f.slider="slider";
    const auto initial=f.pose(35);
    f.a=s.add_point(initial[0].second[0],initial[0].second[1]);
    f.b=s.add_point(initial[1].second[0],initial[1].second[1]);
    if(!f.p.empty())f.p=s.add_point(initial[2].second[0],initial[2].second[1]);
    if(!f.slider.empty())f.slider=s.add_point(initial.back().second[0],initial.back().second[1]);
    f.crank=segment(f.o,f.a);length(f.crank);
    if(kind==Kind::SliderCrank) {length(segment(f.a,f.b));guide(f.b,0);}
    else if(kind==Kind::ScottRussell) {
        // A is the radius-link endpoint and the real midpoint of the long rod.
        f.p.clear();const auto rod=segment(f.slider,f.b);length(rod);
        static_cast<void>(s.add_midpoint_constraint(f.a,rod));guide(f.slider,0);
    } else {
        f.d=point({kind==Kind::Watt?120.:100.,0});anchor(f.d);
        const auto coupler=segment(f.a,f.b);length(coupler);length(segment(f.b,f.d));
        if(!f.p.empty())static_cast<void>(s.add_midpoint_constraint(f.p,coupler));
        if(!f.slider.empty()){length(segment(f.p,f.slider));guide(f.slider,-50);}
    }
    return f;
}
inline XY position(const Sketch& s,const std::string& id) {
    const auto* p=s.find_point(id);require(p!=nullptr,"Lost mechanism point identity");return {p->x,p->y};
}
inline double input_angle(const Fixture& f) {
    const auto p=position(f.sketch,f.a);
    return std::atan2(p[1],p[0])/radians-f.rotation;
}
inline void check(const Fixture& f,double degrees) {
    const auto& s=f.sketch;
    for(const auto& [id,expected]:f.pose(degrees)) {
        const auto actual=position(s,id);
        require(std::hypot(actual[0]-expected[0],actual[1]-expected[1])<2e-6,
            "Wrong mechanism pose or assembly branch at "+std::to_string(degrees)+" point "+id);
    }
    for(const auto& [id,value]:f.lengths) {
        const auto dim=std::ranges::find(s.dimensions,id,&SketchDimension::id);
        require(dim!=s.dimensions.end()&&dim->driving&&dim->locked&&dim->value==value,"Changed locked link dimension");
        const auto a=position(s,dim->first_point_id),b=position(s,dim->second_point_id);
        require(std::abs(std::hypot(b[0]-a[0],b[1]-a[1])-value)<1e-7,"Changed rigid link length");
    }
    for(const auto& [id,expected]:f.anchors) {
        const auto actual=position(s,id);
        require(std::hypot(actual[0]-expected[0],actual[1]-expected[1])<1e-8,"Moved ground pivot");
    }
}
inline document::PartDocument part(Sketch sketch) {
    auto result=document::PartDocument::create_default();
    auto owner=document::PartDocument::create_sketch_container();
    sketch.owner_container_id=owner.id;
    result.history={owner};result.sketches={std::move(sketch)};
    document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Mechanism"));
    bodies.insert({document::PartHistoryKind::Feature,owner.id});result.set_body_history(bodies);
    return result;
}
inline int run() {
    unsigned passed{},failed{},edits{},drags{},projected_drags{},roundtrips{};
    for(const auto kind:{Kind::SliderCrank,Kind::FourBar,Kind::MidpointBranch,Kind::Watt,Kind::ScottRussell})
    for(const int branch:{1,-1})for(const double rotation:{0.,90.,27.})
    for(const bool reverse:{false,true})for(const bool external:{false,true})for(int state=0;state<3;++state) {
        // Scott-Russell has two merging branches at 90 degrees; this regular
        // branch is tested on each side separately, without inventing a guide
        // for the output. Watt is a rocker, not a full-turn crank.
        if(kind==Kind::ScottRussell&&branch<0)continue;
        const std::string label=std::string(name(kind))+" branch="+std::to_string(branch)+" rotation="+
            std::to_string(rotation)+" reverse="+std::to_string(reverse)+" external="+std::to_string(external)+" state="+std::to_string(state);
        if(const auto* filter=std::getenv("ZIMA_MECHANISM_FILTER");filter&&label.find(filter)==std::string::npos)continue;
        try {
            auto f=make(kind,branch,rotation,reverse,external);auto& s=f.sketch;check(f,35);
            const auto sources=s.external_references;
            const auto contacts=s.constraints;
            auto status=s.solve();require(status.status==SolveStatus::UnderConstrained&&status.remaining_degrees_of_freedom==1,"Expected one free mechanism input");
            auto angle=s.create_segment_dimension(f.crank,DimensionKind::Angle);
            angle.driving=state!=0;angle.locked=state==2;f.angle=angle.id;s.apply_dimension(angle);check(f,35);
            if(const auto* path=std::getenv("ZIMA_MECHANISM_ARTIFACT_DIRECTORY");path&&branch==1&&rotation==0&&!reverse&&!external&&state==1) {
                std::filesystem::create_directories(std::filesystem::u8path(path));
                part(s).save(std::filesystem::u8path(path)/(std::string(name(kind))+".prtz"));
            }
            status=s.solve();require(status.remaining_degrees_of_freedom==(state==0?1:0),"Wrong dimensioned mechanism DOF");
            if(state) {
                const std::vector<double> targets=kind==Kind::Watt?std::vector<double>{40,45,50,55,60,55,45,35}:
                    kind==Kind::ScottRussell?std::vector<double>{40,50,65,75,85,75,55,35}:
                    std::vector<double>{40,60,90,120,150,180,210,240,270,300,330,360,395,365,335,305,275,245,215,185,155,125,95,65,35};
                for(const double theta:targets) {
                    require(s.set_dimension_value(angle.id,f.angle_value(theta)),"Rejected reachable input angle "+std::to_string(theta));
                    check(f,theta);++edits;
                }
            }
            std::vector<std::string> handles{f.a,f.b};
            if(!f.p.empty())handles.push_back(f.p);
            if(!f.slider.empty())handles.push_back(f.slider);
            for(const auto& handle:handles)for(const double theta:{40.,45.,30.,35.}) {
                if(state==2&&theta==35)continue; // This is the unchanged pose, not a rejected gesture.
                const auto poses=f.pose(theta);
                const auto target=std::ranges::find(poses,handle,[](const auto& p){return p.first;})->second;
                const auto before=s.serialized();const bool moved=s.move_point(handle,target[0],target[1]);
                if(state==2) {
                    const auto actual=position(s,f.a),expected=f.pose(35).front().second;
                    require(!moved&&s.serialized()==before,"Fully locked mechanism escaped or changed on rejected drag: accepted="+
                        std::to_string(moved)+" equal="+std::to_string(s.serialized()==before)+" displacement="+
                        std::to_string(std::hypot(actual[0]-expected[0],actual[1]-expected[1])));
                }
                else {
                    const std::string role=handle==f.a?"crank":handle==f.b?"coupler":handle==f.p?"midpoint":"slider";
                    require(moved,"Rejected reachable "+role+" drag theta="+std::to_string(theta));
                    const auto actual=position(s,handle);
                    require(std::hypot(actual[0]-target[0],actual[1]-target[1])<2e-6,
                        "Attenuated "+role+" drag theta="+std::to_string(theta)+" actual input="+std::to_string(input_angle(f)));
                    // A slider position may admit two different input angles.
                    // Verify its actual assembly against all native equations;
                    // do not invent a crank-angle constraint for a point drag.
                    check(f,input_angle(f));++drags;
                }
            }
            require(s.external_references==sources&&s.constraints==contacts,"Mechanism edit changed contacts or external sources");
            const auto final_angle=input_angle(f);s=Sketch::from_serialized(s.serialized());check(f,final_angle);
            if(state!=2)for(const auto& handle:handles)for(const double theta:{40.,45.,30.}) {
                const auto poses=f.pose(theta);
                auto target=std::ranges::find(poses,handle,[](const auto& p){return p.first;})->second;
                target[0]+=.137;target[1]-=.093;
                const auto old=position(s,handle);
                require(s.move_point(handle,target[0],target[1]),"Rejected ordinary off-path mouse drag");
                const auto actual=position(s,handle);
                require(std::hypot(actual[0]-old[0],actual[1]-old[1])>1e-5,"Off-path drag reported no movement");
                require(std::hypot(actual[0]-target[0],actual[1]-target[1])<std::hypot(old[0]-target[0],old[1]-target[1]),"Projected drag moved away from cursor");
                check(f,input_angle(f));++projected_drags;
            }
            auto original=part(s);document::DocumentSession session(original);auto changed=original;
            auto edited=s;
            auto driver=*std::ranges::find(edited.dimensions,angle.id,&SketchDimension::id);
            driver.driving=true;driver.value=f.angle_value(42);edited.apply_dimension(driver);
            auto evaluated=f;evaluated.sketch=edited;check(evaluated,42);
            edited.owner_container_id=changed.history.front().id;changed.sketches.front()=edited;
            session.commit_metadata(changed);
            require(session.undo()&&session.document().sketches.front().serialized()==original.sketches.front().serialized(),"Mechanism edit Undo failed");
            require(session.redo()&&session.document().sketches.front().serialized()==edited.serialized(),"Mechanism edit Redo failed");
            const auto file=std::filesystem::temp_directory_path()/("zima-mechanism-"+original.document_id+".prtz");
            changed.save(file);const auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
            require(loaded.sketches.front().serialized()==edited.serialized()&&loaded.body_owner_for_object(edited.owner_container_id),"Mechanism native save/reopen changed geometry or ownership");
            evaluated.sketch=loaded.sketches.front();check(evaluated,42);++roundtrips;
            ++passed;
        } catch(const std::exception& e) {++failed;std::cerr<<"FAIL "<<label<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Mechanism study: "<<passed<<" passed, "<<failed<<" failed; "<<edits<<" angle edits, "<<drags<<" moving drags, "<<projected_drags<<" projected drags, "<<roundtrips<<" native reopen and Undo/Redo cases\n";
    return failed?1:0;
}
}
