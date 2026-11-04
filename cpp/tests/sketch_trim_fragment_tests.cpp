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
    std::cout<<"Trim fragment study: "<<passed<<" passed, "<<failed<<" failed; "<<edits<<" numeric edits and "<<drags<<" moving drags\n";
    return failed?1:0;
}
