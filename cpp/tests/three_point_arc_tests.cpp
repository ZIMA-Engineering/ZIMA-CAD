#include "sketch_mechanism_study.hpp"
#include <zima/sketcher/three_point_arc.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <chrono>
using namespace zima;
using namespace zima::sketcher;
using XY=std::array<double,2>;
namespace {
unsigned cases{},checks{},drags{},edits{};
std::string stage;
void check(bool ok,const char* text) {++checks;if(!ok)throw std::runtime_error(std::string(text)+" "+stage);}
void near(double a,double b,const char* text) {if(std::abs(a-b)>=2e-6)throw std::runtime_error(std::string(text)+" actual="+std::to_string(a)+" expected="+std::to_string(b)+" "+stage);++checks;}
void equations(const Sketch& s,const ThreePointArcResult& id) {
    s.validate();const auto a=std::ranges::find(s.arcs,id.arc_id,&SketchArc::id);
    check(a!=s.arcs.end(),"Arc identity lost");const auto* c=s.find_point(a->center_point_id);
    for(const auto& p:{id.start_point_id,id.end_point_id,id.through_point_id}) {
        const auto* point=s.find_point(p);check(point,"Point identity lost");
        near(std::hypot(point->x-c->x,point->y-c->y),a->radius,(std::string("Point left exact circle ")+ (p==id.start_point_id?"start":p==id.end_point_id?"end":"through")).c_str());
    }
    const auto* p=s.find_point(id.through_point_id);
    double angle=std::atan2(p->y-c->y,p->x-c->x);
    while(angle<a->start_angle-1e-8)angle+=2*std::numbers::pi;
    check(angle<=a->end_angle+1e-8,"Through point left the selected arc interval");
    check(std::ranges::count_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.geometry_id==id.arc_id&&c.first_point_id==id.through_point_id;})==1,
        "Through point does not own exactly one native contact");
    for(const auto& d:s.dimensions) {
        const auto* x=s.find_point(d.first_point_id);const auto* y=s.find_point(d.second_point_id);
        double measured{};
        if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter)measured=a->radius*(d.kind==DimensionKind::Diameter?2:1);
        else if(d.kind==DimensionKind::Distance)measured=std::hypot(y->x-x->x,y->y-x->y);
        else if(d.kind==DimensionKind::DistanceX)measured=std::abs(y?y->x-x->x:x->x);
        else if(d.kind==DimensionKind::DistanceY)measured=std::abs(y?y->y-x->y:x->y);
        else if(d.kind==DimensionKind::AngleThreePoint) {
            const auto* z=s.find_point(d.geometry_id);
            measured=std::acos(std::clamp(((x->x-y->x)*(z->x-y->x)+(x->y-y->y)*(z->y-y->y))/(std::hypot(x->x-y->x,x->y-y->y)*std::hypot(z->x-y->x,z->y-y->y)),-1.,1.))*180/std::numbers::pi;
        }else if(d.kind==DimensionKind::DistancePointLine)measured=std::abs(d.geometry_id=="sketch_axis:x"?x->y:x->x);
        else continue;
        near(measured,std::abs(d.value),"Independent dimension equation failed");
    }
    const auto reopened=Sketch::from_serialized(s.serialized());check(reopened.serialized()==s.serialized(),"Native round trip changed the definition");
}
void creation_and_drag() {
    for(int sign:{-1,1})for(bool reverse:{false,true})for(double rotation:{0.,.73,1.5707963267948966})
    for(double height:{4.,10.,18.})for(bool external:{false,true}) {
        const auto point=[&](XY p){return XY{37+std::cos(rotation)*p[0]-std::sin(rotation)*p[1],-29+std::sin(rotation)*p[0]+std::cos(rotation)*p[1]};};
        auto s=Sketch::create_default();auto start=point({-10,0}),end=point({10,0});if(reverse)std::swap(start,end);
        const auto existing=s.add_point(start[0],start[1]);const auto id=add_three_point_arc(s,start,end,point({0,sign*height}));
        check(id.start_point_id==existing,"Start snapping replaced native identity");equations(s,id);
        for(const auto& point_id:{id.start_point_id,id.end_point_id}) {
            if(!external)s.find_point(point_id)->fixed=true;
            else {
                auto reference=Sketch::create_external_reference(ExternalReferenceKind::Point);
                reference.source_document_id="three-point-arc-ground";reference.source_owner_id="ground";
                reference.source_semantic_key=point_id;
                const auto* p=s.find_point(point_id);reference.cached_points={{p->x,p->y}};
                s.external_references.push_back(reference);
                static_cast<void>(s.add_point_reference_constraint(point_id,reference.id));
            }
        }
        const auto references=s.external_references;
        for(double h:{6.,14.,8.}) {
            const auto old=*s.find_point(id.through_point_id);
            const auto target=point({1,sign*h});check(s.move_point(id.through_point_id,target[0],target[1]),"Free through point did not move");++drags;
            // This is an ordinary C contact: the established drag projects
            // onto the existing arc. Do not introduce a second arc editor.
            check(std::hypot(s.find_point(id.through_point_id)->x-old.x,s.find_point(id.through_point_id)->y-old.y)>1e-8,"Permitted contact movement was lost");
            equations(s,id);
            check(s.external_references==references,"Dragging changed the read-only external supports");
        }
        for(auto& p:s.points)p.fixed=true;
        const auto before=s.serialized();const auto* p=s.find_point(id.through_point_id);
        check(!s.move_point(p->id,p->x+3,p->y+2)&&s.serialized()==before,"Fully constrained arc escaped its constraints");++cases;
    }
}
void dimensions() {
    for(int kind=0;kind<9;++kind)for(bool reverse:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}) {
        stage="kind="+std::to_string(kind)+" reverse="+std::to_string(reverse)+" driving="+std::to_string(driving)+" locked="+std::to_string(locked);
        auto s=Sketch::create_default();const auto id=add_three_point_arc(s,{-10,0},{10,0},{2,8});
        const auto arc=s.arcs.front();const auto first=reverse?id.through_point_id:id.start_point_id;
        const auto second=reverse?id.start_point_id:id.through_point_id;
        auto dimension=kind==0?s.create_arc_radius_dimension(id.arc_id):kind==1?s.create_arc_diameter_dimension(id.arc_id):
            kind<5?s.create_point_dimension(first,second,kind==2?DimensionKind::Distance:kind==3?DimensionKind::DistanceX:DimensionKind::DistanceY):
            kind==5?s.create_three_point_angle_dimension(first,arc.center_point_id,second):
            kind==6?s.create_axis_dimension(id.through_point_id,"sketch_axis:x"):
            kind==7?s.create_axis_dimension(id.through_point_id,"sketch_axis:y"):
            s.create_point_dimension(reverse?id.end_point_id:id.start_point_id,reverse?id.start_point_id:id.end_point_id);
        dimension.driving=driving;dimension.locked=locked;const auto dim=dimension.id;
        if(!driving && locked) {
            const auto before=s.serialized();bool rejected=false;
            try{s.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&s.serialized()==before,"A locked reference dimension was not rejected atomically");++cases;continue;
        }
        s.apply_dimension(dimension);equations(s,id);
        const double initial_value=dimension_display_value(s.dimensions.front(),s.plane);
        if(driving)for(double factor:{1.08,.94,1.03}) {
            if(!s.set_dimension_value(dim,initial_value*factor))throw std::runtime_error("Driving dimension edit failed kind="+std::to_string(kind)+" reverse="+std::to_string(reverse)+" locked="+std::to_string(locked)+" factor="+std::to_string(factor));++edits;stage+=" edit";equations(s,id);
            near(dimension_display_value(s.dimensions.front(),s.plane),initial_value*factor,"Edited dimension value lost");
        }
        for(const auto& point_id:{arc.center_point_id,id.start_point_id,id.end_point_id,id.through_point_id})for(double delta:{.2,-.3,.1}) {
            const auto before=s.serialized();const auto p=*s.find_point(point_id);
            const bool moved=s.move_point(point_id,p.x+delta,p.y+delta*.37);++drags;stage+=" drag";
            if(!moved)check(s.serialized()==before,"Rejected drag partly changed the sketch");
            else check(std::hypot(s.find_point(point_id)->x-p.x,s.find_point(point_id)->y-p.y)>1e-8,"Accepted drag did not move");
            equations(s,id);
        }
        ++cases;
    }
}
void invalid_and_history() {
    auto s=Sketch::create_default();const auto initial=s.serialized();
    for(XY p:std::vector<XY>{{0,0},{-10,0},{10,0},{0,1e-12},{std::numeric_limits<double>::infinity(),1}}) {
        bool rejected=false;try{static_cast<void>(add_three_point_arc(s,{-10,0},{10,0},p));}catch(const std::exception&){rejected=true;}
        check(rejected&&s.serialized()==initial,"Degenerate creation was not atomic");
    }
    const auto id=add_three_point_arc(s,{-10,0},{10,0},{0,8});
    const auto original=test::mechanism::part(Sketch::create_default());document::DocumentSession session(original);
    auto changed=original;s.owner_container_id=original.sketches.front().owner_container_id;
    changed.sketches.front()=s;session.commit(changed);
    check(session.undo()&&session.document().sketches.front().serialized()==original.sketches.front().serialized(),"Creation Undo failed");
    check(session.redo(),"Creation Redo failed");equations(session.document().sketches.front(),id);
    for(double factor:{1.08,.94,1.03}) {
        auto edited=session.document();auto& value=edited.sketches.front();
        if(value.dimensions.empty())value.apply_dimension(value.create_arc_radius_dimension(id.arc_id));
        const auto before=session.document().sketches.front().serialized();
        check(value.set_dimension_value(value.dimensions.front().id,10.25*factor),"History radius edit failed");
        equations(value,id);const auto after=value.serialized();session.commit(edited);
        check(session.undo()&&session.document().sketches.front().serialized()==before,"Dimension Undo failed");
        check(session.redo()&&session.document().sketches.front().serialized()==after,"Dimension Redo failed");
        equations(session.document().sketches.front(),id);
    }
    const auto path=std::filesystem::temp_directory_path()/("zima-three-point-arc-"+original.document_id+".prtz");
    session.document().save(path);equations(document::PartDocument::load(path).sketches.front(),id);std::filesystem::remove(path);
    static_cast<void>(s.add_segment(0,-20,0,20));
    const auto pieces=sketch_trim_topology(s,false);
    unsigned trimmed{};
    for(const auto& piece:pieces)if(piece.geometry_id==id.arc_id) {
        auto copy=s;static_cast<void>(apply_sketch_trim(copy,{piece}));copy.validate();++trimmed;
        check(copy.find_point(id.through_point_id),"Trim removed the independent through point");
    }
    check(trimmed>=2,"Trim did not recognize the exact arc intersections");
}
}
int main() {
    try {
        const auto start=std::chrono::steady_clock::now();creation_and_drag();dimensions();invalid_and_history();
        std::cout<<"Three-point arc: "<<cases<<" cases, "<<drags<<" drags, "<<edits<<" dimension edits, "<<checks<<" checks; "
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
