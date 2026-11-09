#include "sketch_mechanism_study.hpp"
#include <zima/sketcher/sketch_trim.hpp>

using namespace zima::test::mechanism;
namespace sketcher=zima::sketcher;
int main() {
    unsigned passed{},failed{},edits{},drags{};
    for(unsigned count:{2u,3u})for(bool mirror:{false,true})for(bool external:{false,true})
    for(bool reverse:{false,true})for(bool diameter:{false,true})for(int state=0;state<3;++state) {
        try {
            auto s=Sketch::create_default();const auto source=s.add_circle(30,0,10);
            const auto center=s.circles.front().center_point_id;s.find_point(center)->fixed=true;
            if(mirror)static_cast<void>(s.mirror_geometry({source},"sketch_axis:y"));
            const auto contact=s.add_point(40,0);const auto tangent=s.add_segment(40,0,40,20);
            static_cast<void>(s.add_tangent_constraint(reverse?tangent:source,reverse?source:tangent,contact));
            static_cast<void>(s.add_segment_constraint(tangent,ConstraintKind::Vertical));
            const unsigned sectors=2*count;
            for(unsigned i=0;i<count;++i) {
                const double angle=i*std::numbers::pi/count,c=std::cos(angle),d=std::sin(angle);
                std::string guide;
                if(external) {
                    auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
                    ref.source_document_id="cutters";ref.source_owner_id="datum";ref.source_semantic_key="guide-"+std::to_string(i);
                    ref.infinite=true;ref.cached_points={{30-20*c,-20*d},{30+20*c,20*d}};guide=ref.id;s.external_references.push_back(ref);
                } else {
                    guide=s.add_segment(30-20*c,-20*d,30+20*c,20*d,1e-7,true);
                    const auto line=s.segments.back();s.find_point(line.first_point_id)->fixed=true;s.find_point(line.second_point_id)->fixed=true;
                }
                for(double sign:{-1.,1.}) {
                    const auto id=s.add_point(30+sign*10*c,sign*10*d);
                    if(id!=contact)static_cast<void>(s.add_point_on_circle_constraint(id,source));
                    static_cast<void>(s.add_point_on_line_constraint(id,guide));
                }
            }
            auto size=diameter?s.create_circle_diameter_dimension(source):s.create_circle_radius_dimension(source);
            size.driving=state!=0;size.locked=state==2;s.apply_dimension(size);
            const auto original=s;
            const auto topology=sketcher::sketch_trim_topology(s,false);std::vector<sketcher::SketchTrimPiece> removed;
            for(unsigned i=0;i<count;++i) {
                const double a=(2*i+.5)*2*std::numbers::pi/sectors;
                const auto cut=sketcher::nearest_sketch_trim_piece(topology,{30+10*std::cos(a),10*std::sin(a)},.01);
                require(cut&&cut->geometry_id==source,"Missing exact circle trim sector");removed.push_back(*cut);
            }
            const auto mapping=sketcher::apply_sketch_trim(s,removed);
            const auto fragments=mapping.geometry_mapping.at(source);require(fragments.size()==count,"Wrong number of circular survivors");
            const auto sources=s.external_references;
            const auto check=[&] {
                for(const auto& id:fragments) {
                    const auto arc=std::ranges::find(s.arcs,id,&SketchArc::id);
                    require(arc!=s.arcs.end()&&arc->center_point_id==center,"Trim duplicated or replaced the common center");
                    require(std::abs(arc->radius-s.arcs.front().radius)<1e-8,"Trimmed circular fragments lost equal radii");
                }
                require(std::ranges::count_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::EqualRadius&&
                    std::ranges::find(fragments,c.geometry_id)!=fragments.end()&&std::ranges::find(fragments,c.second_geometry_id)!=fragments.end();})==count-1,
                    "Trim did not persist a minimal equal-radius graph for its survivors");
                const auto radial=std::ranges::find(s.dimensions,size.id,&SketchDimension::id);
                require(radial!=s.dimensions.end()&&radial->driving==size.driving&&radial->locked==size.locked,"Trim discarded or changed the authored R/D dimension");
                for(std::size_t i=0;i<s.points.size();++i)for(std::size_t j=i+1;j<s.points.size();++j)
                    require(std::hypot(s.points[i].x-s.points[j].x,s.points[i].y-s.points[j].y)>1e-7,"Trim duplicated a native junction point");
                require(s.find_point(contact)&&std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::Tangent&&c.first_point_id==contact;}),"Trim lost native tangent incidence");
                require(s.external_references==sources,"Trim edit changed an external cutter");
                const auto line=[&](const std::string& id)->std::pair<XY,XY> {
                    const auto native=std::ranges::find(s.segments,id,&SketchSegment::id);
                    if(native!=s.segments.end())return {position(s,native->first_point_id),position(s,native->second_point_id)};
                    const auto ref=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
                    require(ref!=s.external_references.end(),"Lost trim support identity");return {ref->cached_points.front(),ref->cached_points.back()};
                };
                for(const auto& c:s.constraints)if(!c.suppressed) {
                    if(c.kind==ConstraintKind::PointOnLine) {
                        const auto p=position(s,c.first_point_id);const auto [a,b]=line(c.geometry_id);
                        require(std::abs((p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0]))/std::hypot(b[0]-a[0],b[1]-a[1])<1e-7,"Trim lost a cutter incidence");
                    } else if(c.kind==ConstraintKind::EqualRadius)
                        require(std::abs(*s.circular_radius(c.geometry_id)-*s.circular_radius(c.second_geometry_id))<1e-7,"Trim lost an authored or generated radius equality");
                    else if(c.kind==ConstraintKind::Symmetric) {
                        const auto a=position(s,c.first_point_id),b=position(s,c.second_point_id);
                        require(std::abs(a[0]+b[0])<1e-7&&std::abs(a[1]-b[1])<1e-7,"Trim lost mirror symmetry");
                    } else if(c.kind==ConstraintKind::Vertical) {
                        const auto a=position(s,c.first_point_id),b=position(s,c.second_point_id);
                        require(std::abs(a[0]-b[0])<1e-7,"Trim lost tangent-line orientation");
                    } else if(c.kind==ConstraintKind::Tangent) {
                        const bool first=s.circular_radius(c.geometry_id).has_value();
                        const auto support=std::ranges::find(s.arcs,first?c.geometry_id:c.second_geometry_id,&SketchArc::id);
                        require(support!=s.arcs.end(),"Tangent no longer references the native circular survivor");
                        const auto center=position(s,support->center_point_id),p=position(s,c.first_point_id);
                        const auto [a,b]=line(first?c.second_geometry_id:c.geometry_id);
                        require(std::abs((p[0]-center[0])*(b[0]-a[0])+(p[1]-center[1])*(b[1]-a[1]))/(support->radius*std::hypot(b[0]-a[0],b[1]-a[1]))<1e-8,"Trimmed junction lost tangency");
                    } else throw std::runtime_error("Unverified trim-fragment relation kind");
                }
                require(std::abs(radial->value-s.arcs.front().radius*(diameter?2:1))<1e-7,"Trimmed R/D no longer measures its survivor");
                auto solved=s;const auto result=solved.solve();require(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-8,"Trimmed native equations cannot solve");
                for(const auto& arc:s.arcs) {
                    const auto c=position(s,arc.center_point_id);
                    for(const auto& endpoint:{arc.start_point_id,arc.end_point_id}) {const auto p=position(s,endpoint);
                        require(std::abs(std::hypot(p[0]-c[0],p[1]-c[1])-arc.radius)<1e-7,"Trimmed arc endpoint left its circle");}
                }
            };
            check();
            if(size.driving)for(double radius:{11.,9.,10.}) {
                require(s.set_dimension_value(size.id,radius*(diameter?2:1)),"Trimmed source R/D edit rejected");check();++edits;
            }
            for(double radius:{10.5,9.5,10.}) {
                const auto before=s.serialized();const bool moved=s.move_point(contact,30+radius,.13);
                if(state==2)require(!moved&&s.serialized()==before,"Locked trimmed arcs escaped during drag");
                else {require(moved,"Editable trimmed tangent junction cannot drag");check();++drags;}
            }
            auto before=part(original),after=before;auto edited=s;edited.owner_container_id=before.history[0].id;after.sketches[0]=edited;
            zima::document::DocumentSession session(before);session.commit_metadata(after);
            require(session.undo()&&session.document().sketches[0].serialized()==before.sketches[0].serialized()&&session.redo()&&
                session.document().sketches[0].serialized()==edited.serialized(),"Trim Undo/Redo changed geometry or references");
            const auto file=std::filesystem::temp_directory_path()/("zima-trim-"+before.document_id+".prtz");after.save(file);
            const auto loaded=zima::document::PartDocument::load(file);std::filesystem::remove(file);
            require(loaded.sketches[0].serialized()==edited.serialized(),"Trim native save/reopen changed its graph");
            ++passed;
        } catch(const std::exception& e){++failed;std::cerr<<"FAIL trim fragments="<<count<<" mirror="<<mirror<<" external="<<external<<" reverse="<<reverse<<" diameter="<<diameter<<" state="<<state<<": "<<e.what()<<'\n';}
    }
    unsigned parallel_cases{};
    for(bool external_support:{false,true})for(bool reverse:{false,true})for(bool split:{false,true})
    for(double angle:{0.,37.,90.})for(bool fixed:{false,true})for(int dimension_state=0;dimension_state<4;++dimension_state) {
        if(external_support&&reverse)continue; // External geometry is always the read-only reference owner.
        try {
            auto s=Sketch::create_default();const double c=std::cos(angle*radians),d=std::sin(angle*radians);
            const auto rotate=[&](double x,double y){return XY{c*x-d*y,d*x+c*y};};
            std::string support;std::vector<SketchExternalReference> external_before;
            if(external_support) {
                auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
                reference.source_document_id="parallel-source";reference.source_owner_id="support";
                reference.source_semantic_key="edge";reference.infinite=true;
                reference.cached_points={rotate(-20,-8),rotate(20,-8)};support=reference.id;
                s.external_references.push_back(reference);external_before=s.external_references;
            } else {
                const auto a=rotate(-12,-8),b=rotate(12,-8);support=s.add_segment(a[0],a[1],b[0],b[1]);
                const auto line=std::ranges::find(s.segments,support,&SketchSegment::id);
                s.find_point(line->first_point_id)->fixed=true;s.find_point(line->second_point_id)->fixed=true;
            }
            const auto first=rotate(-15,4),second=rotate(15,4);
            const auto target=s.add_segment(first[0],first[1],second[0],second[1]);
            const auto cutter=[&](double x) {
                const auto a=rotate(x,-20),b=rotate(x,20);const auto id=s.add_segment(a[0],a[1],b[0],b[1],1e-7,true);
                const auto line=std::ranges::find(s.segments,id,&SketchSegment::id);
                s.find_point(line->first_point_id)->fixed=true;s.find_point(line->second_point_id)->fixed=true;
            };
            cutter(split?-5:5);if(split)cutter(5);
            static_cast<void>(s.add_segment_pair_constraint(reverse?target:support,reverse?support:target,ConstraintKind::Parallel));
            const auto original=s;const auto topology=sketcher::sketch_trim_topology(s,false);
            const auto hint=split?rotate(0,4):rotate(10,4);
            const auto piece=sketcher::nearest_sketch_trim_piece(topology,hint,.1);
            require(piece&&piece->geometry_id==target,"Parallel Trim piece is missing");
            const auto mapping=sketcher::apply_sketch_trim(s,{*piece});
            const auto survivors=mapping.geometry_mapping.at(target);
            require(survivors.size()==(split?2u:1u),"Parallel Trim produced the wrong survivor count");
            const auto line=[&](const std::string& id) {
                if(const auto native=std::ranges::find(s.segments,id,&SketchSegment::id);native!=s.segments.end())
                    return std::pair{position(s,native->first_point_id),position(s,native->second_point_id)};
                const auto reference=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
                require(reference!=s.external_references.end(),"Parallel support identity was lost");
                return std::pair{reference->cached_points.front(),reference->cached_points.back()};
            };
            const auto equations=[&] {
                const auto [a,b]=line(support);const double ax=b[0]-a[0],ay=b[1]-a[1];
                for(const auto& survivor:survivors) {
                    const auto [p,q]=line(survivor);const double px=q[0]-p[0],py=q[1]-p[1];
                    require(std::abs(ax*py-ay*px)/(std::hypot(ax,ay)*std::hypot(px,py))<1e-8,
                        "Trimmed segment escaped its Parallel equation");
                    require(std::ranges::any_of(s.constraints,[&](const auto& relation) {
                        return relation.kind==ConstraintKind::Parallel&&
                            ((relation.geometry_id==support&&relation.second_geometry_id==survivor)||
                             (relation.geometry_id==survivor&&relation.second_geometry_id==support));
                    }),"A straight survivor lost its persisted Parallel relation");
                }
                const auto result=s.solve();require(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-8,
                    "Trimmed Parallel graph does not solve");
                if(external_support)require(s.external_references==external_before,"Parallel Trim changed a read-only external support");
            };
            equations();
            auto dimension=s.create_segment_dimension(survivors.front());
            dimension.driving=dimension_state>=2;dimension.locked=dimension_state==1||dimension_state==3;
            if(!dimension.driving&&dimension.locked) {
                const auto before=s.serialized();bool rejected=false;try{s.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                require(rejected&&s.serialized()==before,"Locked reference length was accepted or changed a trimmed segment");
            } else {
                s.apply_dimension(dimension);equations();
                if(fixed)for(const auto& survivor:survivors) {
                    const auto segment=std::ranges::find(s.segments,survivor,&SketchSegment::id);
                    s.find_point(segment->first_point_id)->fixed=true;s.find_point(segment->second_point_id)->fixed=true;
                }
                for(double factor:{1.1,.9,1.05}) {
                    const auto before=s.serialized();const bool accepted=s.set_dimension_value(dimension.id,dimension.value*factor);
                    if(!dimension.driving||fixed)require(accepted==!dimension.driving&&s.serialized()==before,
                        "Reference or impossible Parallel length edit changed the Sketch");
                    else {require(accepted,"Editable Parallel survivor rejected a length change");++edits;}
                    equations();
                }
                const auto segment=std::ranges::find(s.segments,survivors.front(),&SketchSegment::id);
                const auto dragged_point=segment->second_point_id;
                for(double step:{.35,-.2,.4}) {
                    const auto* point=s.find_point(dragged_point);const auto before=s.serialized();
                    const bool moved=s.move_point(dragged_point,point->x+step*c,point->y+step*d);
                    if(fixed)require(!moved&&s.serialized()==before,"Fully constrained Parallel survivor escaped during drag");
                    else {require(moved&&s.serialized()!=before,"Underconstrained Parallel survivor did not move during drag");++drags;}
                    equations();
                }
            }
            auto before=part(original),after=before;auto edited=s;edited.owner_container_id=before.history[0].id;after.sketches[0]=edited;
            zima::document::DocumentSession session(before);session.commit_metadata(after);
            require(session.undo()&&session.document().sketches[0].serialized()==before.sketches[0].serialized()&&session.redo()&&
                session.document().sketches[0].serialized()==edited.serialized(),"Parallel Trim Undo/Redo changed relation identity");
            const auto file=std::filesystem::temp_directory_path()/("zima-parallel-trim-"+before.document_id+".prtz");after.save(file);
            const auto reopened=zima::document::PartDocument::load(file);std::filesystem::remove(file);
            require(reopened.sketches[0].serialized()==edited.serialized(),"Parallel Trim save/reopen changed its relation graph");
            ++parallel_cases;
        } catch(const std::exception& e) {++failed;std::cerr<<"FAIL parallel trim external="<<external_support<<" reverse="<<reverse<<" split="<<split<<" angle="<<angle<<" fixed="<<fixed<<" dimension_state="<<dimension_state<<": "<<e.what()<<'\n';}
    }
    unsigned point_hv_cases{};
    for(const auto kind:{ConstraintKind::Horizontal,ConstraintKind::Vertical})for(bool trim_start:{false,true})
    for(bool reverse:{false,true})for(bool aligned:{false,true}) {
        try {
            auto s=Sketch::create_default();
            const XY removed=kind==ConstraintKind::Horizontal?XY{trim_start?-15.:15.,5.}:XY{5.,trim_start?-15.:15.};
            const XY retained=kind==ConstraintKind::Horizontal?XY{trim_start?15.:-15.,aligned?5.:8.}:XY{aligned?5.:8.,trim_start?15.:-15.};
            const auto start=trim_start?removed:retained,end=trim_start?retained:removed;
            const auto target=s.add_segment(start[0],start[1],end[0],end[1]);
            const auto target_line=*std::ranges::find(s.segments,target,&SketchSegment::id);
            const auto removed_point=trim_start?target_line.first_point_id:target_line.second_point_id;
            const auto anchor=s.add_point(kind==ConstraintKind::Horizontal?0.:5.,kind==ConstraintKind::Horizontal?5.:0.);
            s.find_point(anchor)->fixed=true;
            const auto relation=s.add_point_pair_constraint(reverse?removed_point:anchor,reverse?anchor:removed_point,kind);
            const double station=trim_start?-5.:5.;std::string cutter;
            if(kind==ConstraintKind::Horizontal)cutter=s.add_segment(station,-20,station,20,1e-7,true);
            else cutter=s.add_segment(-20,station,20,station,1e-7,true);
            const auto cutter_line=std::ranges::find(s.segments,cutter,&SketchSegment::id);
            s.find_point(cutter_line->first_point_id)->fixed=true;s.find_point(cutter_line->second_point_id)->fixed=true;
            const auto original=s;const auto topology=sketcher::sketch_trim_topology(s,false);
            const XY hint=kind==ConstraintKind::Horizontal?XY{trim_start?-10.:10.,removed[1]}:XY{removed[0],trim_start?-10.:10.};
            const auto piece=sketcher::nearest_sketch_trim_piece(topology,hint,2.);
            require(piece&&piece->geometry_id==target,"Point H/V end Trim piece is missing");
            const auto mapping=sketcher::apply_sketch_trim(s,{*piece});require(mapping.geometry_mapping.at(target).size()==1,"Point H/V end Trim lost its survivor");
            const auto survivor=std::ranges::find(s.segments,target,&SketchSegment::id);require(survivor!=s.segments.end(),"Point H/V survivor lost stable identity");
            const auto replacement=trim_start?survivor->first_point_id:survivor->second_point_id;
            require(!s.find_point(removed_point)&&replacement!=removed_point,"Point H/V Trim retained its removed endpoint");
            const auto restored=std::ranges::find(s.constraints,relation,&SketchConstraint::id);
            if(aligned) {
                require(restored!=s.constraints.end()&&
                    ((restored->first_point_id==anchor&&restored->second_point_id==replacement)||
                     (restored->first_point_id==replacement&&restored->second_point_id==anchor)),
                    "Aligned replacement endpoint did not inherit its point H/V relation");
                const auto equation=[&] {const auto* a=s.find_point(anchor);const auto* b=s.find_point(replacement);
                    require(std::abs(kind==ConstraintKind::Horizontal?a->y-b->y:a->x-b->x)<1e-8,"Transferred point H/V equation is not satisfied");};
                equation();for(double step:{.4,-.2,.3}) {const auto* p=s.find_point(replacement);const auto before=s.serialized();
                    const bool moved=s.move_point(replacement,p->x+(kind==ConstraintKind::Horizontal?step:2),p->y+(kind==ConstraintKind::Vertical?step:2));
                    require(moved&&s.serialized()!=before,"Transferred point H/V blocked its permitted direction");equation();++drags;}
            } else {
                require(restored==s.constraints.end(),"Misaligned replacement endpoint inherited point H/V and would move geometry");
                const auto* a=s.find_point(anchor);const auto* b=s.find_point(replacement);
                require(std::abs(kind==ConstraintKind::Horizontal?a->y-b->y:a->x-b->x)>1e-4,
                    "Negative point H/V case did not create a genuinely misaligned replacement");
            }
            auto before=part(original),after=before;auto edited=s;edited.owner_container_id=before.history[0].id;after.sketches[0]=edited;
            zima::document::DocumentSession session(before);session.commit_metadata(after);
            require(session.undo()&&session.redo()&&session.document().sketches[0].serialized()==edited.serialized(),"Point H/V Trim Undo/Redo changed its relation decision");
            const auto file=std::filesystem::temp_directory_path()/("zima-point-hv-trim-"+before.document_id+".prtz");after.save(file);
            const auto reopened=zima::document::PartDocument::load(file);std::filesystem::remove(file);
            require(reopened.sketches[0].serialized()==edited.serialized(),"Point H/V Trim save/reopen changed its relation decision");
            ++point_hv_cases;
        } catch(const std::exception& e) {++failed;std::cerr<<"FAIL point H/V trim kind="<<int(kind)<<" trim_start="<<trim_start<<" reverse="<<reverse<<" aligned="<<aligned<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Trim fragment study: "<<passed<<" circular, "<<parallel_cases<<" Parallel and "<<point_hv_cases<<" point-H/V variants passed, "<<failed<<" failed; "<<edits<<" numeric edits and "<<drags<<" moving drags; native/external supports, both selection orders, split/end Trim, alignment-safe endpoint transfer, three orientations, driving/reference and lock/DOF states, Undo/Redo and reopen covered\n";
    return failed?1:0;
}
