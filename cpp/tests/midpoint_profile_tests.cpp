#include "midpoint_profile_study.hpp"
#include <zima/document/document_session.hpp>
#include <filesystem>

using namespace midpoint_profile_study;
using namespace zima::sketcher;
int main(){try{
    int cases=0;
    for(double angle:{0.,4.,14.,45.,90.,-14.,-90.,166.})for(bool reversed:{false,true})for(bool swap:{false,true})for(int anchor:{0,1,2,3}) {
        auto f=make(angle,reversed,swap,anchor);const auto sources=f.sketch.external_references;
        const auto verify=[&](Fixture& value,double width,double height){midpoint_profile_study::verify(value,width,height);check(value.sketch.external_references==sources,"Midpoint edit changed its read-only source");};
        verify(f,2.9,.8);
        for(double width:{2.93,2.96,2.9}) {
            check(f.sketch.set_dimension_value(f.width,width),"Cannot edit centred rectangle width");verify(f,width,.8);
            f.sketch=Sketch::from_serialized(f.sketch.serialized());verify(f,width,.8);
        }
        for(double height:{.82,.84,.8}){check(f.sketch.set_dimension_value(f.height,height),"Cannot edit centred rectangle height");verify(f,2.9,height);}
        if(std::abs(std::sin(angle*std::numbers::pi/180.))>1e-10&&std::abs(std::cos(angle*std::numbers::pi/180.))>1e-10) {
            const auto driver=std::ranges::find(f.sketch.dimensions,DimensionKind::AngleBetween,&SketchDimension::kind);
            check(driver!=f.sketch.dimensions.end(),"Midpoint angular driver missing");const auto id=driver->id;const double base=driver->value;
            const bool supplement=std::abs(std::abs(angle)-base)>1e-7;
            for(double factor:{1.01,1.02,1.}) {
                check(f.sketch.set_dimension_value(id,base*factor),"Cannot edit centred profile angle");
                f.angle=std::copysign(supplement?180-base*factor:base*factor,angle);verify(f,2.9,.8);
            }
        }
        if(anchor!=3) {
            auto locked=f;
            for(auto& d:locked.sketch.dimensions)if(d.driving)d.locked=true;
            const auto before=locked.sketch.serialized();const auto* p=locked.sketch.find_point(f.vertices[2]);
            static_cast<void>(locked.sketch.move_point(f.vertices[2],p->x+1,p->y+1));
            verify(locked,2.9,.8);check(locked.sketch.serialized()==before,"Locked fully constrained midpoint profile escaped during drag");
            for(bool driving:{false,true}) {
            auto free=f;
            for(auto& d:free.sketch.dimensions) {
                if(d.id==free.width)d.locked=true;
                else if(d.id==free.height){d.driving=driving;d.locked=false;}
                else d.locked=true;
            }
            const auto reference_before=free.sketch;
            if(!driving) {
            check(free.sketch.set_dimension_value(free.height,10),"Reference dimension update failed");
            for(const auto& before:reference_before.points) {
                const auto* after=free.sketch.find_point(before.id);check(after,"Reference edit lost a point");near(after->x,before.x);near(after->y,before.y);
            }
            verify(free,2.9,.8);check(free.sketch.solve().remaining_degrees_of_freedom==1,"Reference height did not leave one degree of freedom");
            }
            double height=.8;
            for(double step:{.01,.02,-.01}) {
                const auto* p=free.sketch.find_point(free.vertices[2]);const double a=angle*std::numbers::pi/180.,side=reversed?-1.:1.;
                const double x=p->x-side*step*std::sin(a),y=p->y+side*step*std::cos(a);
                check(free.sketch.move_point(free.vertices[2],x+1e-5*std::cos(a),y+1e-5*std::sin(a)),"Off-path centred profile did not project and move");
                height+=2*step;verify(free,2.9,height);
                near(free.sketch.find_point(free.vertices[2])->x,x);near(free.sketch.find_point(free.vertices[2])->y,y);
            }
            }
        } else {
            auto translated=f;for(auto& d:translated.sketch.dimensions)if(d.driving)d.locked=true;
            for(const auto& step:std::array<std::array<double,2>,3>{{{.01,.02},{-.02,.01},{.01,-.03}}}) {
                const auto before=translated.sketch.points;const auto* p=translated.sketch.find_point(f.vertices[2]);
                check(translated.sketch.move_point(f.vertices[2],p->x+step[0],p->y+step[1]),"Free centre did not translate");
                verify(translated,2.9,.8);
                for(const auto& original:before){const auto* moved=translated.sketch.find_point(original.id);if(std::abs(moved->x-original.x-step[0])>=1e-7||std::abs(moved->y-original.y-step[1])>=1e-7)throw std::runtime_error("Free translation differs at angle "+std::to_string(angle)+" point "+original.id+" expected "+std::to_string(original.x+step[0])+","+std::to_string(original.y+step[1])+" actual "+std::to_string(moved->x)+","+std::to_string(moved->y));}
            }
        }
        const auto before_impossible=f.sketch.serialized();
        check(!f.sketch.set_dimension_value(f.width,0),"Impossible midpoint profile collapse was accepted");
        check(f.sketch.serialized()==before_impossible,"Impossible edit changed midpoint profile");
        if(!reversed&&!swap&&anchor==0) {
            auto doc=zima::document::PartDocument::create_default();auto owner=zima::document::PartDocument::create_sketch_container();
            owner.feature.sketch_id=f.sketch.id;f.sketch.owner_container_id=owner.id;
            doc.history={owner};doc.sketches={f.sketch};zima::document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Midpoint profile"));
            graph.insert({zima::document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
            zima::document::DocumentSession session(doc);auto edited=doc;
            check(edited.sketches.front().set_dimension_value(f.width,2.93),"Native midpoint edit failed");session.commit(edited);
            check(session.undo(),"Native midpoint Undo failed");f.sketch=session.document().sketches.front();verify(f,2.9,.8);
            check(session.redo(),"Native midpoint Redo failed");f.sketch=session.document().sketches.front();verify(f,2.93,.8);
            const auto path=std::filesystem::temp_directory_path()/("zima-midpoint-"+doc.document_id+".prtz");
            session.document().save(path);f.sketch=zima::document::PartDocument::load(path).sketches.front();std::filesystem::remove(path);verify(f,2.93,.8);
            static_cast<void>(f.sketch.add_corner_fillet(f.edges[0],f.edges[1],.05));
            check(f.sketch.set_dimension_value(f.width,2.96),"Midpoint corner dependent edit failed");verify(f,2.96,.8);
            check(f.sketch.evaluated_profile_sketch().arcs.size()==1,"Midpoint dependent corner vanished");
        }
        ++cases;
    }
    int other_dimensions=0;
    for(double angle:{0.,90.,-90.})for(bool reversed:{false,true})for(bool swap:{false,true})for(int anchor:{0,1,2})for(int kind:{0,1,2,3,4}) {
        auto f=make(angle,reversed,swap,anchor);const auto sources=f.sketch.external_references;
        const auto verify=[&](Fixture& value,double width,double height){midpoint_profile_study::verify(value,width,height);check(value.sketch.external_references==sources,"Projected midpoint edit changed its read-only source");};
        f.sketch.remove_dimension(f.width);
        const bool horizontal=angle==0;const auto first=f.vertices[swap?1:0],second=f.vertices[swap?0:1];
        auto d=kind==0?f.sketch.create_point_dimension(first,second,horizontal?DimensionKind::DistanceX:DimensionKind::DistanceY):
            kind==1?f.sketch.create_symmetric_dimension(first,second,horizontal?"sketch_axis:y":"sketch_axis:x"):
            kind==2?f.sketch.create_line_symmetric_dimension(horizontal?"sketch_axis:y":"sketch_axis:x",f.edges[swap?1:3],f.edges[swap?3:1],DimensionKind::DistanceLineSymmetric):
            kind==3?f.sketch.create_line_pair_dimension(f.edges[swap?1:3],f.edges[swap?3:1],DimensionKind::DistanceLine):
            f.sketch.create_point_line_dimension(f.vertices[swap?1:0],f.edges[swap?3:1]);
        f.width=d.id;f.sketch.apply_dimension(d);const double initial=dimension_display_value(d,f.sketch.plane);
        for(double factor:{1.01,1.02,1.}){check(f.sketch.set_dimension_value(f.width,initial*factor),"Cannot edit projected or symmetric midpoint width");verify(f,2.9*factor,.8);}
        auto locked=f;for(auto& dim:locked.sketch.dimensions)if(dim.driving)dim.locked=true;
        const auto fixed=locked.sketch.serialized();const auto* point=locked.sketch.find_point(locked.vertices[2]);
        static_cast<void>(locked.sketch.move_point(point->id,point->x+1,point->y+1));verify(locked,2.9,.8);
        check(locked.sketch.serialized()==fixed,"Locked projected midpoint width escaped");
        for(bool driving:{false,true}) {
        auto free=f;
        auto reference=std::ranges::find(free.sketch.dimensions,free.width,&SketchDimension::id);auto measured=*reference;measured.driving=driving;measured.locked=false;
        free.sketch.apply_dimension(measured);for(auto& dim:free.sketch.dimensions)if(dim.driving&&dim.id!=free.width)dim.locked=true;
        if(!driving) {
        const auto before_reference=free.sketch.points;check(free.sketch.set_dimension_value(free.width,100),"Projected reference update failed");
        for(const auto& p:before_reference){const auto* q=free.sketch.find_point(p.id);near(q->x,p.x);near(q->y,p.y);}
        }
        double width=2.9;
        for(double step:{.01,.02,-.01}) {
            const auto* p=free.sketch.find_point(free.vertices[2]);const double a=angle*std::numbers::pi/180.;
            check(free.sketch.move_point(free.vertices[2],p->x+step*std::cos(a)-1e-5*std::sin(a),p->y+step*std::sin(a)+1e-5*std::cos(a)),"Off-path reference-width profile did not project and move");
            width+=2*step;verify(free,width,.8);
        }
        }
        ++other_dimensions;
    }
    int diagonal_cases=0;
    for(double angle:{0.,4.,14.,45.,90.,-14.,-90.,166.})for(bool reversed:{false,true})for(bool swap:{false,true}) {
        auto f=make(angle,reversed,swap,0);f.sketch.remove_dimension(f.width);
        auto d=f.sketch.create_point_dimension(f.vertices[swap?2:0],f.vertices[swap?0:2],DimensionKind::Distance);f.width=d.id;f.sketch.apply_dimension(d);
        for(double factor:{1.01,1.02,1.}) {
            check(f.sketch.set_dimension_value(d.id,d.value*factor),"Cannot edit centred diagonal length");
            verify(f,std::sqrt(d.value*d.value*factor*factor-.8*.8),.8);
        }
        ++diagonal_cases;
    }
    int angular_drags=0;
    for(double angle:{4.,14.,45.,-14.,166.})for(bool reversed:{false,true})for(bool driving:{false,true}) {
        auto f=make(angle,reversed,false,0);
        for(auto& d:f.sketch.dimensions)if(d.kind==DimensionKind::AngleBetween){d.driving=driving;d.locked=false;}else d.locked=true;
        for(double degrees:{2.,-3.,1.5}) {
            const auto* p=f.sketch.find_point(f.vertices[2]);const double r=degrees*std::numbers::pi/180.;
            const double x=p->x*std::cos(r)-p->y*std::sin(r),y=p->x*std::sin(r)+p->y*std::cos(r);
            check(f.sketch.move_point(f.vertices[2],x,y),"Unlocked angular midpoint profile did not rotate");f.angle+=degrees;verify(f,2.9,.8);
            near(f.sketch.find_point(f.vertices[2])->x,x);near(f.sketch.find_point(f.vertices[2])->y,y);
        }
        ++angular_drags;
    }
    std::cout<<cases<<" midpoint rectangle orientation/selection/anchor cases, "<<other_dimensions<<" projected/symmetric cases, "<<diagonal_cases<<" diagonal cases and "<<angular_drags<<" angular drag cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
