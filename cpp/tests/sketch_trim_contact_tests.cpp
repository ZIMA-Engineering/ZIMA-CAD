#include "sketch_copied_circle_contact_study.hpp"
#include <zima/sketcher/sketch_trim.hpp>
#include <fstream>
#include "sketch_mechanism_study.hpp"

using namespace zima::sketcher;
namespace {
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
void inspect(const Sketch& s) {
    const auto pieces=sketch_trim_topology(s,false);
    for(const auto& circle:s.circles) {
        std::cout<<"Circle "<<circle.id<<" pieces:";
        for(const auto& p:pieces)if(p.geometry_id==circle.id)std::cout<<" ["<<p.start<<","<<p.end<<"]";
        std::cout<<'\n';
    }
    for(const auto& arc:s.arcs) {
        std::cout<<"Arc "<<arc.id<<" pieces:";
        for(const auto& p:pieces)if(p.geometry_id==arc.id)std::cout<<" ["<<p.start<<","<<p.end<<"]";
        std::cout<<'\n';
    }
}
void verify_circles(const Sketch& s,const std::vector<SketchExternalReference>& references) {
    zima::test::verify_circle_contacts(s);
    require(s.external_references==references,"Trim changed read-only reference geometry");
    for(const auto& arc:s.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
        const auto* p=s.find_point(id);const auto* c=s.find_point(arc.center_point_id);
        require(p&&std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-7,"Trimmed endpoint escaped circular support");
    }
    for(const auto& c:s.constraints)if(!c.suppressed) {
        if(c.kind==ConstraintKind::EqualRadius)
            require(std::abs(*s.circular_radius(c.geometry_id)-*s.circular_radius(c.second_geometry_id))<1e-7,"Trim lost equal radius");
        if(c.kind==ConstraintKind::PointOnCircle) {
            const auto* p=s.find_point(c.first_point_id);
            std::optional<std::array<double,3>> support;
            for(const auto& curve:s.circles)if(curve.id==c.geometry_id) {const auto* q=s.find_point(curve.center_point_id);support=std::array{q->x,q->y,curve.radius};}
            for(const auto& curve:s.arcs)if(curve.id==c.geometry_id) {const auto* q=s.find_point(curve.center_point_id);support=std::array{q->x,q->y,curve.radius};}
            for(const auto& ref:s.external_references)if(ref.id==c.geometry_id)support=external_reference_circle(ref);
            require(support&&std::abs(std::hypot(p->x-(*support)[0],p->y-(*support)[1])-(*support)[2])<1e-7,"Trim lost C incidence");
        }
        if(c.kind==ConstraintKind::PointOnLine) {
            const auto line=std::ranges::find(s.segments,c.geometry_id,&SketchSegment::id);
            require(line!=s.segments.end(),"Trim cutter identity lost");
            const auto* a=s.find_point(line->first_point_id);const auto* b=s.find_point(line->second_point_id);const auto* p=s.find_point(c.first_point_id);
            require(std::abs((p->x-a->x)*(b->y-a->y)-(p->y-a->y)*(b->x-a->x))/std::hypot(b->x-a->x,b->y-a->y)<1e-7,"Trim endpoint left its cutter");
        }
        if(c.kind==ConstraintKind::PointReference&&c.second_point_id.starts_with("sketch_external_center:")) {
            const auto id=c.second_point_id.substr(std::string("sketch_external_center:").size());
            const auto ref=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
            require(ref!=s.external_references.end(),"Trim center reference identity lost");
            const auto center=external_reference_circle(*ref);const auto* p=s.find_point(c.first_point_id);
            require(center&&std::hypot(p->x-(*center)[0],p->y-(*center)[1])<1e-7,"Trim moved its referenced center");
        }
    }
}
void roundtrip(const Sketch& before,const Sketch& after) {
    auto part=zima::test::mechanism::part(before),changed=part;
    changed.sketches[0]=after;changed.sketches[0].owner_container_id=part.history[0].id;
    zima::document::DocumentSession session(part);session.commit_metadata(changed);
    require(session.undo()&&session.document().sketches[0].serialized()==part.sketches[0].serialized(),"Trim Undo changed native incidence");
    require(session.redo()&&session.document().sketches[0].serialized()==changed.sketches[0].serialized(),"Trim Redo changed native incidence");
    const auto path=std::filesystem::temp_directory_path()/("zima-trim-contact-"+part.document_id+".prtz");
    changed.save(path);const auto loaded=zima::document::PartDocument::load(path);std::filesystem::remove(path);
    require(loaded.sketches[0].serialized()==changed.sketches[0].serialized(),"Trim native save/reopen changed incidence");
}
}
int main(int argc,char** argv) { try {
    if(argc==2) {
        std::ifstream input(argv[1]);require(bool(input),"Missing private Sketch fixture");
        const auto s=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));inspect(s);
        unsigned checked{};
        for(const auto& piece:sketch_trim_topology(s,false)) {
            if(!std::ranges::any_of(s.external_references,[&](const auto& r){return s.coincident_circular_support(piece.geometry_id,r.id);}))continue;
            auto edited=s;static_cast<void>(apply_sketch_trim(edited,{piece}));edited.validate();
            const auto solved=edited.solve();require(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting,"Private trim cannot solve");
            require(edited.external_references==s.external_references,"Private trim changed references");roundtrip(s,edited);++checked;
        }
        std::cout<<"Private trim: "<<checked<<" pieces applied, solved and reopened\n";
        return 0;
    }
    unsigned cases{},edits{},drags{},failures{};
    for(bool reverse:{false,true})for(double mirror:{-1.,1.})for(double angle:{0.,.61})
    for(bool internal_first:{false,true})for(bool internal_second:{false,true}) {
        try {
            auto s=zima::test::make_copied_circle_contact_fixture(mirror,angle,internal_first,internal_second);
            const auto arc=s.arcs.back();const auto circle=s.circles[1];
            const auto support=s.external_references[1].id;
            static_cast<void>(s.add_tangent_constraint(reverse?arc.id:support,reverse?support:arc.id));
            const auto center=*s.find_point(circle.center_point_id);
            static_cast<void>(s.add_segment(center.x-20,center.y,center.x+20,center.y,1e-7,true));
            const auto original=s;const auto refs=s.external_references;
            const auto before=s.serialized();const auto pieces=sketch_trim_topology(s,false);
            require(std::ranges::count(pieces,circle.id,&SketchTrimPiece::geometry_id)==3,
                "External tangent contact did not split the coincident native circle");
            require(s.serialized()==before,"Trim topology changed the Sketch");
            const auto contact=mirror<0?arc.start_point_id:arc.end_point_id;
            const auto* p=s.find_point(contact);double parameter=std::atan2(p->y-center.y,p->x-center.x)/(2*std::numbers::pi);if(parameter<0)parameter+=1;
            const auto piece=std::ranges::find_if(pieces,[&](const auto& v){return v.geometry_id==circle.id&&std::abs(v.start-parameter)<1e-8;});
            require(piece!=pieces.end(),"Persisted contact not used as the exact Trim boundary");
            const auto mapping=apply_sketch_trim(s,{*piece});
            require(mapping.geometry_mapping.at(circle.id).size()==1,"Contact trim fragmented the surviving arc");
            const auto survivor=std::ranges::find(s.arcs,mapping.geometry_mapping.at(circle.id)[0],&SketchArc::id);
            require(survivor!=s.arcs.end()&&(survivor->start_point_id==contact||survivor->end_point_id==contact),"Trim duplicated the external contact point");
            verify_circles(s,refs);roundtrip(original,s);
            for(bool diameter:{false,true})for(int state=0;state<3;++state) {
                auto edited=s;auto dim=diameter?edited.create_arc_diameter_dimension(arc.id):edited.create_arc_radius_dimension(arc.id);
                dim.driving=state!=2;dim.locked=state==1;edited.apply_dimension(dim);
                if(dim.driving)for(double factor:{1.01,.99,1.02}) {
                    if(!edited.set_dimension_value(dim.id,dim.value*factor)) {
                        if(const auto* folder=std::getenv("ZIMA_TRIM_CONTACT_DIAGNOSTICS")) {
                            const auto root=std::filesystem::path(folder);
                            std::ofstream(root/"failed-trim.json")<<edited.serialized();
                            std::ofstream(root/"before-trim.json")<<original.serialized();
                        }
                        throw std::runtime_error("Trimmed contact R/D edit failed diameter="+std::to_string(diameter)+" state="+std::to_string(state)+" factor="+std::to_string(factor));
                    }
                    require(std::abs(*edited.circular_radius(arc.id)-dim.value*factor/(diameter?2:1))<1e-7,"Trimmed contact missed R/D value");
                    verify_circles(edited,refs);++edits;
                }
                for(const auto& id:{arc.center_point_id,arc.start_point_id,arc.end_point_id})for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto q=*edited.find_point(id);const auto snapshot=edited.serialized();
                    const bool moved=edited.move_point(id,q.x+.11,q.y+.17);
                    if(dim.locked)require(!moved&&snapshot==edited.serialized(),"Locked trimmed contact escaped on drag");
                    else {require(moved&&std::hypot(edited.find_point(id)->x-q.x,edited.find_point(id)->y-q.y)>1e-6,"Free trimmed contact did not move");++drags;}
                    verify_circles(edited,refs);
                }
                roundtrip(s,edited);
            }
            for(unsigned kind=0;kind<4;++kind)for(bool order:{false,true})for(int state=0;state<3;++state) {
                const auto measure=[&](const Sketch& value) {
                    if(kind==3)return value.create_three_point_angle_dimension(order?arc.end_point_id:arc.start_point_id,arc.center_point_id,order?arc.start_point_id:arc.end_point_id);
                    return value.create_point_dimension(order?arc.center_point_id:"sketch_origin",order?"sketch_origin":arc.center_point_id,
                        kind==0?DimensionKind::Distance:kind==1?DimensionKind::DistanceX:DimensionKind::DistanceY);
                };
                std::vector<double> targets;
                for(double factor:{1.01,.99,1.02}) {auto expected=s;auto size=expected.create_arc_radius_dimension(arc.id);size.value*=factor;expected.apply_dimension(size);targets.push_back(measure(expected).value);}
                const bool constant=std::abs(targets.front()-targets.back())<1e-7;
                auto edited=s;auto dim=measure(edited);dim.driving=state!=2;dim.locked=state==1;
                const auto before=edited.serialized();bool redundant=false;
                try{edited.apply_dimension(dim);}catch(const RedundantConstraint&){redundant=true;}
                if(constant&&dim.driving){require(redundant&&edited.serialized()==before,"Redundant trimmed dimension changed geometry");continue;}
                require(!redundant,"Movable trimmed dimension rejected");
                if(dim.driving)for(double target:targets) {
                    auto update=dim;update.value=target;edited.apply_dimension(update);
                    require(std::abs(measure(edited).value-target)<1e-6,"Trimmed positional/angular dimension missed its target");verify_circles(edited,refs);++edits;
                }
                for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto q=*edited.find_point(arc.center_point_id);const auto snapshot=edited.serialized();
                    const bool moved=edited.move_point(q.id,q.x+.1,q.y+.2);
                    if(dim.locked)require(!moved&&snapshot==edited.serialized(),"Locked trimmed positional/angular dimension escaped");
                    else {require(moved&&std::hypot(edited.find_point(q.id)->x-q.x,edited.find_point(q.id)->y-q.y)>1e-6,"Free trimmed positional/angular geometry did not move");++drags;}
                    verify_circles(edited,refs);
                }
                roundtrip(s,edited);
            }
            ++cases;
        }catch(const std::exception& e){++failures;std::cerr<<"Trim contact reverse="<<reverse<<" mirror="<<mirror<<" angle="<<angle<<" sides="<<internal_first<<internal_second<<": "<<e.what()<<'\n';}
    }
    for(bool native_source:{false,true})for(bool suppressed:{false,true})for(bool inside:{false,true}) {
        try {
            auto s=Sketch::create_default();const auto source=s.add_circle(0,0,10,true);std::string support=source;
            if(!native_source) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="source";ref.source_owner_id="circle";ref.source_semantic_key="boundary";
                ref.exact_spline=sketch_curve_geometry(s,source);
                for(unsigned i=0;i<=32;++i)ref.cached_points.push_back({10*std::cos(i*std::numbers::pi/16),10*std::sin(i*std::numbers::pi/16)});
                s.add_external_reference(ref);support=ref.id;
            }
            const auto arc=s.add_arc(0,0,10,0,0,10);const double a=inside?.61:3.7;
            const auto point=s.add_point(10*std::cos(a),10*std::sin(a));const auto relation=s.add_point_on_circle_constraint(point,support,true);
            std::ranges::find(s.constraints,relation,&SketchConstraint::id)->suppressed=suppressed;
            const auto pieces=sketch_trim_topology(s,false);
            require(std::ranges::count(pieces,arc,&SketchTrimPiece::geometry_id)==(inside&&!suppressed?2:1),"Trim used a suppressed or out-of-domain circular contact");
        }catch(const std::exception& e){++failures;std::cerr<<"Finite contact native="<<native_source<<" suppressed="<<suppressed<<" inside="<<inside<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Trim contact matrix: "<<cases<<" passed, "<<failures<<" failed; "<<edits<<" numeric edits, "<<drags<<" moving drags; R/D, distance/X/Y/angle, both orders, driving/reference/locked states, mirrored internal/external contacts, finite domains, Undo/Redo and reopen\n";return failures?1:0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
