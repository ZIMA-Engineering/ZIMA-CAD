#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/part_document.hpp>
#include <zima/sketcher/external_curve_center.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <numbers>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace zima::test {
inline void verify_circle_contacts(const sketcher::Sketch& s) {
    using namespace sketcher;
    const auto circle=[&](const std::string& id) {
        for(const auto& c:s.circles)if(c.id==id) {
            const auto* p=s.find_point(c.center_point_id);return std::array{p->x,p->y,c.radius};
        }
        for(const auto& c:s.arcs)if(c.id==id) {
            const auto* p=s.find_point(c.center_point_id);return std::array{p->x,p->y,c.radius};
        }
        for(const auto& r:s.external_references)if(r.id==id) {
            if(const auto c=external_reference_circle(r))return *c;
        }
        throw std::runtime_error("Missing circular support");
    };
    for(const auto& t:s.constraints)if(!t.suppressed&&t.kind==ConstraintKind::Tangent) {
        const auto a=circle(t.geometry_id),b=circle(t.second_geometry_id);
        const auto* p=s.find_point(t.first_point_id);
        if(!p)throw std::runtime_error("T lost its persistent contact");
        const double tolerance=1e-7*std::max({1.,a[2],b[2]});
        if(std::abs(std::hypot(a[0]-b[0],a[1]-b[1])-(t.tangent_internal?std::abs(a[2]-b[2]):a[2]+b[2]))>tolerance||
           std::abs(std::hypot(p->x-a[0],p->y-a[1])-a[2])>tolerance||
           std::abs(std::hypot(p->x-b[0],p->y-b[1])-b[2])>tolerance||
           std::abs((p->x-a[0])*(p->y-b[1])-(p->y-a[1])*(p->x-b[0]))/std::max({1.,a[2],b[2]})>tolerance)
            throw std::runtime_error("Independent circular C/T/side equation failed");
    }
}

inline int run_double_circle_tangent_study() {
    using namespace sketcher;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    unsigned cases=0,failures=0;
    std::ifstream fixture(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/double-tangent-projected-circles.json");
    check(bool(fixture),"Double circle fixture missing");
    const auto source=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(fixture),{}));
    for(bool native:{false,true})for(bool reverse:{false,true})for(unsigned support:{1u,2u}) {
        ++cases;
        try {
            auto s=source;const auto references=s.external_references;
            check(s.solve().remaining_degrees_of_freedom==3,"Duplicate projected C changed the free-point rank");
            const auto driven=s.circles.back().id,reference=native?s.circles[support].id:s.external_references[support].id;
            const auto tangent=s.add_tangent_constraint(reverse?driven:reference,reverse?reference:driven);
            verify_circle_contacts(s);check(s.solve().remaining_degrees_of_freedom==2,"Second projected tangent did not remove one DOF");
            const auto contact=std::ranges::find(s.constraints,tangent,&SketchConstraint::id)->first_point_id;
            auto size=s.create_circle_radius_dimension(driven);s.apply_dimension(size);
            for(double factor:{1.01,.99,1.02}) {check(s.set_dimension_value(size.id,size.value*factor),"Fixture radius edit failed");verify_circle_contacts(s);}
            for(const auto& id:{s.circles.back().center_point_id,contact})for(unsigned repeat=0;repeat<3;++repeat) {
                const auto q=*s.find_point(id);check(s.move_point(id,q.x-.2,q.y+.3),"Fixture tangent handle drag failed");verify_circle_contacts(s);
            }
            check(s.external_references==references,"Fixture source geometry was edited");
            auto part=document::PartDocument::create_default();const auto feature=document::PartDocument::create_sketch_container();
            s.owner_container_id=feature.id;part.history={feature};part.sketches={s};document::BodyHistoryGraph graph;
            static_cast<void>(graph.create_body("Double circular tangency"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            document::DocumentSession session(part);auto changed=part;changed.sketches.front().remove_constraint(tangent);session.commit(changed);
            check(session.undo()&&session.document().sketches.front().constraints==s.constraints,"Double tangent Undo changed contacts");
            check(session.redo()&&session.document().sketches.front().constraints==changed.sketches.front().constraints,"Double tangent Redo changed contacts");
            check(session.undo(),"Double tangent restore failed");
            const auto file=std::filesystem::temp_directory_path()/("zima-double-circle-"+feature.id+".prtz");session.document().save(file);
            const auto reopened=document::PartDocument::load(file).sketches.front();verify_circle_contacts(reopened);check(reopened.constraints==s.constraints,"Native reopen changed C/T identity");std::filesystem::remove(file);
        }catch(const std::exception& e){++failures;std::cout<<"Saved fixture native="<<native<<" reverse="<<reverse<<" support="<<support<<": "<<e.what()<<'\n';}
    }
    for(unsigned sources=0;sources<4;++sources)for(unsigned sides=0;sides<4;++sides)
    for(bool reverse:{false,true})for(double angle:{0.,.7,1.6})for(double mirror:{-1.,1.}) {
        if(std::getenv("ZIMA_DOUBLE_CIRCLE_FOCUS")&&(sources!=0||sides!=0||!reverse||angle!=1.6||mirror!=1))continue;
        ++cases;std::string stage="creation";
        try {
            auto s=Sketch::create_default();std::array<std::string,2> supports;
            const double radius=60.,support_radius=30.;
            const bool internal0=(sides&1)!=0,internal1=(sides&2)!=0;
            const double separation=internal0==internal1?(internal0?40.:60.):90.;
            const double d0=internal0?radius-support_radius:radius+support_radius;
            const double d1=internal1?radius-support_radius:radius+support_radius;
            const double y=(d0*d0-d1*d1+separation*separation)/(2*separation);
            const double x=mirror*std::sqrt(d0*d0-y*y);
            const auto transform=[&](double a,double b){return std::array{130.+a*std::cos(angle)-b*std::sin(angle),-70.+a*std::sin(angle)+b*std::cos(angle)};};
            std::array<std::array<double,2>,2> centers{transform(0,0),transform(0,separation)};
            for(unsigned i=0;i<2;++i) {
                const auto c=centers[i];
                if(sources&(1u<<i)) {
                    SketchExternalReference ref;ref.id="support-"+std::to_string(i);ref.kind=ExternalReferenceKind::Edge;
                    ref.source_document_id="source";ref.source_owner_id="source-body";ref.source_semantic_key=ref.id;
                    for(unsigned n=0;n<=64;++n)ref.cached_points.push_back({c[0]+support_radius*std::cos(n*std::numbers::pi/32),c[1]+support_radius*std::sin(n*std::numbers::pi/32)});
                    supports[i]=ref.id;s.external_references.push_back(ref);
                } else {
                    supports[i]=s.add_circle(c[0],c[1],support_radius);s.find_point(s.circles.back().center_point_id)->fixed=true;
                    auto dim=s.create_circle_radius_dimension(supports[i]);dim.locked=true;s.apply_dimension(dim);
                }
            }
            const auto p=transform(x+mirror*2,y+1);const auto driven=s.add_circle(p[0],p[1],radius);
            const auto center_id=s.circles.back().center_point_id;
            const auto external=s.external_references;
            std::array<std::string,2> tangents,contacts;
            for(unsigned i=0;i<2;++i) {
                tangents[i]=s.add_tangent_constraint(reverse?driven:supports[i],reverse?supports[i]:driven);
                const auto t=std::ranges::find(s.constraints,tangents[i],&SketchConstraint::id);
                check(t->tangent_internal==(i?internal1:internal0),"Creation chose the wrong contact side");contacts[i]=t->first_point_id;
            }
            const auto verify=[&](Sketch& value) {
                verify_circle_contacts(value);check(value.external_references==external,"Edited read-only circular source");
                const auto state=value.solve();check(state.status!=SolveStatus::Conflicting&&state.status!=SolveStatus::Invalid,"Circular graph no longer solves");
                for(unsigned i=0;i<2;++i)if(!(sources&(1u<<i))) {
                    const auto c=std::ranges::find(value.circles,supports[i],&SketchCircle::id);const auto* q=value.find_point(c->center_point_id);
                    check(q->x==centers[i][0]&&q->y==centers[i][1]&&std::abs(c->radius-support_radius)<1e-8,"Changed fixed native support");
                }
                for(const auto& id:contacts)check(value.find_point(id),"Contact identity changed");
            };verify(s);check(s.solve().remaining_degrees_of_freedom==1,"Double tangency must retain one radial freedom");
            stage="dimensions and drags";
            for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
                auto edited=s;auto dim=diameter?edited.create_circle_diameter_dimension(driven):edited.create_circle_radius_dimension(driven);
                dim.driving=!reference;dim.locked=locked;
                if(reference&&locked) {
                    const auto before=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dim);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==before,"Locked reference dimension was accepted or changed geometry");continue;
                }
                edited.apply_dimension(dim);verify(edited);
                if(!reference)for(double r:{61.,58.,60.5}) {
                    check(edited.set_dimension_value(dim.id,diameter?2*r:r),"Successive radius/diameter edit rejected");verify(edited);
                    check(std::abs(std::ranges::find(edited.circles,driven,&SketchCircle::id)->radius-r)<1e-7,"Dimension value not applied");
                }
                for(const auto& handle:{center_id,contacts[0],contacts[1]})for(unsigned drag=0;drag<3;++drag) {
                    const auto before=edited.serialized();const auto q=*edited.find_point(handle);
                    const bool moved=edited.move_point(handle,q.x+.11,q.y+.17);
                    if(!reference&&locked)check(!moved&&edited.serialized()==before,"Fully constrained circle escaped on drag");
                    else {check(moved,"Free circular handle cannot drag");const auto* after=edited.find_point(handle);check(std::hypot(after->x-q.x,after->y-q.y)>1e-6,"Drag reported success without movement");}
                    verify(edited);
                }
                const auto encoded=edited.serialized();edited=Sketch::from_serialized(encoded);check(edited.serialized()==encoded,"Reopen changed C/T identities");verify(edited);
            }
            stage="coordinate dimensions";
            for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})for(bool order:{false,true}) {
                auto edited=s;auto dim=edited.create_point_dimension(order?center_id:"sketch_origin",order?"sketch_origin":center_id,kind);
                std::vector<double> targets;
                for(double r:{61.,58.,60.5}) {
                    auto target=s;auto size=target.create_circle_radius_dimension(driven);size.value=r;target.apply_dimension(size);
                    targets.push_back(target.create_point_dimension(dim.first_point_id,dim.second_point_id,kind).value);
                }
                const bool constant=std::ranges::all_of(targets,[&](double v){return std::abs(v-dim.value)<1e-7;});
                const auto before=edited.serialized();bool rejected=false;
                try{edited.apply_dimension(dim);}catch(const RedundantConstraint&){rejected=true;}
                if(constant){check(rejected&&edited.serialized()==before,"Constant positional dimension was not rejected atomically");continue;}
                check(!rejected,"A movable positional dimension was called redundant");
                for(double target:targets) {
                    stage="coordinate "+std::to_string(int(kind))+" order="+std::to_string(order)+" target="+std::to_string(target);
                    auto changed=dim;changed.value=target;edited.apply_dimension(changed);verify(edited);
                    check(std::abs(edited.create_point_dimension(dim.first_point_id,dim.second_point_id,kind).value-target)<1e-6,"Positional dimension missed target");
                }
            }
            stage="atomic rejection and contact removal";
            const auto pieces=sketch_trim_topology(s,false);
            unsigned trim_cases=0;
            for(const auto& piece:pieces)if(piece.geometry_id==driven) {
                stage="trim and arc dragging";++trim_cases;
                auto trimmed=s;static_cast<void>(apply_sketch_trim(trimmed,{piece}));
                check(trimmed.arcs.size()==1,"Trim did not produce one tangent arc");
                verify(trimmed);const auto arc=trimmed.arcs.front();
                check((arc.start_point_id==contacts[0]&&arc.end_point_id==contacts[1])||
                      (arc.start_point_id==contacts[1]&&arc.end_point_id==contacts[0]),"Trim replaced the existing tangent contacts");
                for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
                    if(reference&&locked)continue;auto edited=trimmed;
                    auto dim=diameter?edited.create_arc_diameter_dimension(arc.id):edited.create_arc_radius_dimension(arc.id);
                    dim.driving=!reference;dim.locked=locked;edited.apply_dimension(dim);
                    if(!reference)for(double r:{61.,58.,60.5}) {check(edited.set_dimension_value(dim.id,diameter?2*r:r),"Trimmed tangent arc rejects radius/diameter edit");verify(edited);}
                    for(const auto& handle:{arc.center_point_id,arc.start_point_id,arc.end_point_id})for(unsigned drag=0;drag<3;++drag) {
                        stage="trim "+std::to_string(trim_cases)+" diameter="+std::to_string(diameter)+" reference="+std::to_string(reference)+" locked="+std::to_string(locked)+" handle="+(handle==arc.center_point_id?"center":handle==arc.start_point_id?"start":"end")+" drag="+std::to_string(drag);
                        const auto before=edited.serialized();const auto q=*edited.find_point(handle);
                        const bool moved=edited.move_point(handle,q.x+.11,q.y+.17);
                        if(!reference&&locked)check(!moved&&edited.serialized()==before,"Fully constrained trimmed arc escaped on drag");
                        else {check(moved,"Trimmed tangent arc handle cannot drag");const auto* after=edited.find_point(handle);
                            if(std::hypot(after->x-q.x,after->y-q.y)<=1e-6) {
                                std::cout<<"Trim delta "<<std::scientific<<after->x-q.x<<","<<after->y-q.y<<" start "<<q.x<<","<<q.y<<'\n';
                                throw std::runtime_error("Trimmed arc drag did not move");
                            }
                        }
                        verify(edited);
                    }
                    const auto encoded=edited.serialized();edited=Sketch::from_serialized(encoded);check(edited.serialized()==encoded,"Trimmed arc reopen changed identities");verify(edited);
                }
            }
            check(trim_cases==2,"Double tangent circle must have two trim intervals");
            stage="atomic rejection and contact removal";
            const auto unchanged=s.serialized();bool rejected=false;
            try{static_cast<void>(s.add_tangent_constraint(supports[1],driven));}catch(const std::exception&){rejected=true;}
            check(rejected&&s.serialized()==unchanged,"Redundant T modified the document");
            s.remove_constraint(tangents[1]);check(s.find_point(contacts[1]),"Removing T deleted its point");
            check(std::ranges::count_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contacts[1];})==2,"Removing T deleted independent C");
        } catch(const std::exception& e) {
            ++failures;std::cout<<"Double circle sources="<<sources<<" sides="<<sides<<" reverse="<<reverse<<" angle="<<angle<<" mirror="<<mirror<<" stage="<<stage<<": "<<e.what()<<'\n';
        }
    }
    std::cout<<"Double circular tangent matrix: "<<cases<<" cases, "<<failures<<" failures\n";
    return failures?1:0;
}
}
