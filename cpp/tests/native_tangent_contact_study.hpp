#pragma once
#include <zima/document/document_session.hpp>

// Exact equations and editability, independently of solver residual reports.
static int run_native_tangent_contact_study() {
    using namespace zima::sketcher;
    unsigned failures=0,cases=0;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(const bool internal:{false,true})for(const bool reverse:{false,true})
    for(const double angle:{0.,.7,1.8,3.5}) {
        ++cases;
        std::string stage="initial";
        try {
            auto s=Sketch::create_default();
            const auto a=s.add_circle(0,0,10),b=s.add_circle((internal?7.2:13.2)*std::cos(angle),(internal?7.2:13.2)*std::sin(angle),3);
            const auto tangent=s.add_tangent_constraint(reverse?b:a,reverse?a:b);
            const auto constraint=std::ranges::find(s.constraints,tangent,&SketchConstraint::id);
            check(constraint!=s.constraints.end()&&!constraint->first_point_id.empty(),"Native circular pair has no real C+T contact point");
            const auto contact=constraint->first_point_id;
            const auto verify=[&] {
                const auto* point=s.find_point(contact);check(point,"Contact identity disappeared");
                for(const auto& circle:s.circles) {
                    const auto* center=s.find_point(circle.center_point_id);
                    check(std::abs(std::hypot(point->x-center->x,point->y-center->y)-circle.radius)<1e-6,"C equation failed");
                    check(std::ranges::any_of(s.constraints,[&](const auto& c){return !c.suppressed&&c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contact&&c.geometry_id==circle.id;}),"Contact C relation missing");
                }
                const auto* x=s.find_point(s.circles[0].center_point_id);const auto* y=s.find_point(s.circles[1].center_point_id);
                const auto scale=std::max(1.,s.circles[0].radius*s.circles[1].radius);
                check(std::abs((point->x-x->x)*(point->y-y->y)-(point->y-x->y)*(point->x-y->x))/scale<1e-6,"Independent T equation failed");
                const auto expected=internal?std::abs(s.circles[0].radius-s.circles[1].radius):s.circles[0].radius+s.circles[1].radius;
                check(std::abs(std::hypot(x->x-y->x,x->y-y->y)-expected)<1e-6,"Native circular tangent side changed");
                const auto result=s.solve();check(result.status!=SolveStatus::Conflicting&&result.status!=SolveStatus::Invalid,"Contact does not solve");
            };
            verify();
            auto dimension=s.create_circle_radius_dimension(b);s.apply_dimension(dimension);
            stage="radius edits";
            for(const double radius:{3.5,2.5,3.}) {check(s.set_dimension_value(dimension.id,radius),"Contact radius edit rejected");verify();}
            const auto* p=s.find_point(contact);const auto before=std::array{p->x,p->y};
            stage="drag";check(s.move_point(contact,p->x+.3,p->y+.6),"Free C+T contact cannot be dragged");verify();
            p=s.find_point(contact);check(std::hypot(p->x-before[0],p->y-before[1])>1e-5,"Free contact drag did not move geometry");
            const auto encoded=s.serialized();check(Sketch::from_serialized(encoded).serialized()==encoded,"Contact save/reopen changed identities");
            s.remove_constraint(tangent);check(s.find_point(contact),"Removing T deleted the shared contact");
            check(std::count_if(s.constraints.begin(),s.constraints.end(),[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contact;})==2,"Removing T deleted C");
        }catch(const std::exception& e){++failures;std::cout<<"Native circular contact internal="<<internal<<" reverse="<<reverse<<" angle="<<angle<<" stage="<<stage<<": "<<e.what()<<'\n';}
    }
    for(const bool internal:{false,true})for(const bool reverse:{false,true}) {
        ++cases;
        try {
            auto s=Sketch::create_default();const auto outer=s.add_circle(0,0,10),inner=s.add_circle(internal?7.2:13.2,0,3);
            const auto center_id=s.circles[0].center_point_id;
            auto support=s.create_circle_radius_dimension(outer);support.locked=true;s.apply_dimension(support);
            const auto tangent=s.add_tangent_constraint(reverse?inner:outer,reverse?outer:inner);
            const auto contact=std::ranges::find(s.constraints,tangent,&SketchConstraint::id)->first_point_id;
            const auto offset=*s.find_point(center_id);
            for(auto& point:s.points){point.x-=offset.x;point.y-=offset.y;}
            s.find_point(center_id)->fixed=true;
            check(!contact.empty(),"Fixed native circular support lost its contact");
            std::cout<<"Fixed contact internal="<<internal<<" reverse="<<reverse<<" point="<<contact<<std::endl;
            for(unsigned drag=0;drag<3;++drag) {
                const auto before=*s.find_point(contact);check(s.move_point(contact,before.x-.01,before.y+.15),"Contact on fixed native circular support cannot slide");
                const auto* p=s.find_point(contact);const auto* center=s.find_point(center_id);
                check(center->x==0&&center->y==0&&std::abs(s.circles[0].radius-10)<1e-8,"Native circular support was modified");
                check(std::abs(std::hypot(p->x,p->y)-10)<1e-6,"Contact escaped fixed native circle");
                const auto* driven=s.find_point(s.circles[1].center_point_id);
                check(std::abs(p->x*(p->y-driven->y)-p->y*(p->x-driven->x))/std::max(1.,10*s.circles[1].radius)<1e-6,"Fixed native circular contact lost T");
                check(std::hypot(p->x-before.x,p->y-before.y)>1e-5,"Fixed native circular contact did not move");
            }
        }catch(const std::exception& e){++failures;std::cout<<"Fixed native circular support internal="<<internal<<" reverse="<<reverse<<": "<<e.what()<<std::endl;}
    }
    const auto file=std::filesystem::absolute("Projects/FORM-KOULE.prtz");
    std::ifstream fixture(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/form-koule-shared-arcs.json");
    check(bool(fixture),"Shared native FORM-KOULE regression fixture missing");
    const auto source=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(fixture),{}));
    if(std::getenv("ZIMA_CPP_NATIVE_CONTACT_USER_FILE")&&std::filesystem::exists(file)) {
        const auto document=zima::document::PartDocument::load(file);
        const auto body=std::ranges::find_if(document.body_history.bodies(),[](const auto& b){return b.name=="FORM_FLAT";});
        check(body!=document.body_history.bodies().end(),"FORM_FLAT missing");
        const auto found=std::ranges::find_if(document.sketches,[&](const auto& s){const auto* owner=document.body_history.owner(s.owner_container_id);return owner&&owner->scope.id==body->scope.id&&s.arcs.size()==2;});
        check(found!=document.sketches.end()&&found->serialized()==source.serialized(),"Current FORM-KOULE differs from its read-only regression fixture");
    }
    for(const bool reverse:{false,true}) {
        ++cases;
        try {
            auto s=source;
            const auto original_points=s.points;
            const auto original_constraints=s.constraints;
            const auto id=s.add_tangent_constraint(s.arcs[reverse?1:0].id,s.arcs[reverse?0:1].id);
            const auto tangent=std::ranges::find(s.constraints,id,&SketchConstraint::id);
            for(unsigned n=0;n<2;++n)check(std::abs(s.arcs[n].radius-source.arcs[n].radius)<1e-6,"Adding shared arc T unnecessarily changed an entered radius");
            check(tangent!=s.constraints.end()&&!tangent->first_point_id.empty(),"Shared arc contact was not preserved");
            check(s.points.size()==original_points.size()&&s.constraints.size()==original_constraints.size()+1,
                "Shared native arc endpoint must add only T, without a new point or redundant C");
            check(std::ranges::all_of(original_constraints,[&](const auto& c){return std::ranges::find(s.constraints,c.id,&SketchConstraint::id)!=s.constraints.end();}),
                "Adding T replaced an existing constraint identity");
            check((s.arcs[0].start_point_id==tangent->first_point_id||s.arcs[0].end_point_id==tangent->first_point_id)&&
                (s.arcs[1].start_point_id==tangent->first_point_id||s.arcs[1].end_point_id==tangent->first_point_id),"T did not use the shared native endpoint identity");
            const auto result=s.solve();check(result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-6,"FORM_FLAT T equations failed");
            const auto* p=s.find_point(tangent->first_point_id);const auto* a=s.find_point(s.arcs[0].center_point_id);const auto* b=s.find_point(s.arcs[1].center_point_id);
            check(std::abs((p->x-a->x)*(p->y-b->y)-(p->y-a->y)*(p->x-b->x))/(s.arcs[0].radius*s.arcs[1].radius)<1e-6,"FORM_FLAT common endpoint is not tangent");
            const auto contact=tangent->first_point_id;
            const auto verify=[&](Sketch& edited) {
                const auto* point=edited.find_point(contact);check(point,"Shared endpoint identity was lost");
                for(const auto& arc:edited.arcs) {
                    const auto* center=edited.find_point(arc.center_point_id);
                    for(const auto& endpoint:{arc.start_point_id,arc.end_point_id}) {
                        const auto* end=edited.find_point(endpoint);
                        check(std::abs(std::hypot(end->x-center->x,end->y-center->y)-arc.radius)<1e-6,"Native arc endpoint incidence failed");
                    }
                }
                const auto* first=edited.find_point(edited.arcs[0].center_point_id);const auto* second=edited.find_point(edited.arcs[1].center_point_id);
                check(std::abs((point->x-first->x)*(point->y-second->y)-(point->y-first->y)*(point->x-second->x))/(edited.arcs[0].radius*edited.arcs[1].radius)<1e-6,"Shared native arcs lost T after editing");
                check(edited.constraints.size()==original_constraints.size()+1,"Shared endpoint acquired redundant C");
                check(std::abs(edited.find_point(edited.arcs[0].start_point_id)->y)<1e-6&&
                    std::abs(edited.find_point(edited.arcs[1].center_point_id)->y)<1e-6,"Shared arc edit broke its axis C");
                check(std::abs(edited.find_point(edited.arcs[0].start_point_id)->x-first->x)<1e-6,"Shared arc edit broke its axis T");
                const auto solved=edited.solve();check(solved.status!=SolveStatus::Conflicting&&solved.status!=SolveStatus::Invalid,"Edited shared arcs do not solve");
            };
            for(unsigned arc_index=0;arc_index<2;++arc_index)for(const bool diameter:{false,true})
            for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;auto edited=s;const auto curve=edited.arcs[arc_index].id;
                std::cout<<"FORM-KOULE R/D reverse="<<reverse<<" arc="<<arc_index<<" diameter="<<diameter<<" driving="<<driving<<" locked="<<locked<<std::endl;
                auto dimension=diameter?edited.create_arc_diameter_dimension(curve):edited.create_arc_radius_dimension(curve);
                dimension.driving=driving;dimension.locked=locked;
                if(!driving&&locked) {
                    const auto unchanged=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==unchanged,"Locked reference R/D was not rejected transactionally");continue;
                }
                edited.apply_dimension(dimension);
                for(const double scale:{1.05,.95,1.02}) {
                    auto changed=dimension;changed.value=dimension.value*scale;edited.apply_dimension(changed);verify(edited);
                    const auto actual=diameter?2*edited.arcs[arc_index].radius:edited.arcs[arc_index].radius;
                    check(std::abs(actual-edited.dimensions.front().value)<1e-6,"Edited R/D does not measure the native arc");
                    if(driving)check(std::abs(actual-changed.value)<1e-6,"R/D edit did not move geometry");
                }
                const auto point_id=edited.arcs[arc_index].center_point_id;
                for(unsigned drag=0;drag<3;++drag) {
                    const auto before=*edited.find_point(point_id);
                    check(edited.move_point(point_id,before.x+.12,before.y+(arc_index||(driving&&locked)?0:.08)),"Shared arc centre with a dimension cannot be dragged");verify(edited);
                    const auto* moved=edited.find_point(point_id);check(std::hypot(moved->x-before.x,moved->y-before.y)>1e-5,"Shared arc centre drag did not move");
                }
                auto reopened=Sketch::from_serialized(edited.serialized());verify(reopened);
            }
            std::set<std::string> point_ids;
            for(const auto& arc:s.arcs)for(const auto& point:{arc.center_point_id,arc.start_point_id,arc.end_point_id})point_ids.insert(point);
            for(const auto& point_id:point_ids) {
                ++cases;auto edited=s;
                std::cout<<"FORM-KOULE drag reverse="<<reverse<<" point="<<point_id<<std::endl;
                for(unsigned drag=0;drag<3;++drag) {
                    const auto before=*edited.find_point(point_id);
                    // Axis-constrained endpoints and centres move along X.
                    const auto dx=.12;
                    const auto dy=std::abs(before.y)<1e-8?0.:-.08;
                    check(edited.move_point(point_id,before.x+dx,before.y+dy),"Native shared arc point cannot be dragged");verify(edited);
                    const auto* moved=edited.find_point(point_id);check(std::hypot(moved->x-before.x,moved->y-before.y)>1e-5,"Shared arc point drag did not move");
                }
            }
            for(const auto kind:{DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Distance})
            for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;auto edited=s;auto dimension=edited.create_point_dimension(contact,"sketch_origin",kind);
                std::cout<<"FORM-KOULE position reverse="<<reverse<<" kind="<<int(kind)<<" driving="<<driving<<" locked="<<locked<<std::endl;
                dimension.driving=driving;dimension.locked=locked;
                if(!driving&&locked) {
                    const auto unchanged=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==unchanged,"Locked reference position was not rejected transactionally");continue;
                }
                edited.apply_dimension(dimension);
                for(const double scale:{1.03,.97,1.01}) {
                    auto changed=dimension;changed.value=dimension.value*scale;edited.apply_dimension(changed);verify(edited);
                    const auto measured=edited.create_point_dimension(contact,"sketch_origin",kind).value;
                    check(std::abs(measured-edited.dimensions.front().value)<1e-6,"Shared contact position dimension failed");
                }
            }
            for(unsigned arc_index=0;arc_index<2;++arc_index)for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;auto edited=s;const auto& arc=edited.arcs[arc_index];
                std::cout<<"FORM-KOULE angle reverse="<<reverse<<" arc="<<arc_index<<" driving="<<driving<<" locked="<<locked<<std::endl;
                auto dimension=edited.create_three_point_angle_dimension(arc.start_point_id,arc.center_point_id,arc.end_point_id);
                dimension.driving=driving;dimension.locked=locked;
                if(!driving&&locked) {
                    const auto unchanged=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==unchanged,"Locked reference angle was not rejected transactionally");continue;
                }
                edited.apply_dimension(dimension);
                for(const double scale:{1.01,.99,1.02}) {
                    auto changed=dimension;changed.value=dimension.value*scale;edited.apply_dimension(changed);verify(edited);
                    const auto& a=edited.arcs[arc_index];
                    const auto measured=edited.create_three_point_angle_dimension(a.start_point_id,a.center_point_id,a.end_point_id).value;
                    check(std::abs(measured-edited.dimensions.front().value)<1e-6,"Shared arc angular dimension failed");
                }
            }
            ++cases;auto wrapped=s;const auto& measured_arc=wrapped.arcs[0];
            auto wrapped_angle=wrapped.create_three_point_angle_dimension(measured_arc.start_point_id,measured_arc.center_point_id,measured_arc.end_point_id);
            wrapped_angle.locked=true;wrapped.apply_dimension(wrapped_angle);
            for(const double value:{-1.01*wrapped_angle.value,360.-.99*wrapped_angle.value}) {
                auto changed=wrapped_angle;changed.value=value;wrapped.apply_dimension(changed);verify(wrapped);
                const auto& arc=wrapped.arcs[0];const auto measured=wrapped.create_three_point_angle_dimension(arc.start_point_id,arc.center_point_id,arc.end_point_id).value;
                check(std::abs(measured-(value<0?-value:360.-value))<1e-6,"Wrapped native arc angle changed its geometric magnitude");
            }
            auto fixed=s;for(auto& point:fixed.points)point.fixed=true;
            const auto frozen=fixed.serialized();
            for(const auto& point_id:point_ids) {
                const auto* point=fixed.find_point(point_id);
                check(!fixed.move_point(point_id,point->x+.1,point->y+.1)&&fixed.serialized()==frozen,"Fully fixed shared arcs escaped constraints while dragging");
            }
            ++cases;auto dimensioned=s;dimensioned.find_point(dimensioned.arcs[1].center_point_id)->fixed=true;
            const auto arc_ids=std::array{dimensioned.arcs[0].id,dimensioned.arcs[1].id};
            for(const auto& curve:arc_ids){auto radius=dimensioned.create_arc_radius_dimension(curve);radius.locked=true;dimensioned.apply_dimension(radius);}
            const auto& last=dimensioned.arcs[1];auto angle=dimensioned.create_three_point_angle_dimension(last.start_point_id,last.center_point_id,last.end_point_id);
            angle.locked=true;dimensioned.apply_dimension(angle);const auto closed=dimensioned.solve();
            std::cout<<"Dimensioned shared arcs remaining DOF="<<closed.remaining_degrees_of_freedom<<std::endl;
            check(closed.status==SolveStatus::Solved,"Dimensioned native arc pair is not fully constrained");
            const auto dimensioned_snapshot=dimensioned.serialized();
            for(const auto& point_id:point_ids) {
                const auto* point=dimensioned.find_point(point_id);
                check(!dimensioned.move_point(point_id,point->x+.1,point->y+.1)&&dimensioned.serialized()==dimensioned_snapshot,"Fully dimensioned shared arcs escaped locked dimensions");
            }
            auto reopened=Sketch::from_serialized(s.serialized());verify(reopened);
            reopened.remove_constraint(id);check(reopened.points.size()==s.points.size(),"Removing T destroyed the native shared point");
            auto document=zima::document::PartDocument::create_default();const auto feature=zima::document::PartDocument::create_sketch_container();
            auto persisted=s;persisted.owner_container_id=feature.id;document.history={feature};document.sketches={persisted};
            zima::document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Shared arc regression"));graph.insert({zima::document::PartHistoryKind::Feature,feature.id});
            document.set_body_history(graph);document.resolve_constructions();
            zima::document::DocumentSession session(document);auto changed=document;changed.sketches.front().remove_constraint(id);session.commit(changed);
            check(session.undo()&&session.document().sketches.front().constraints==persisted.constraints,"Native shared arc T Undo lost identities");
            check(session.redo()&&session.document().sketches.front().constraints==changed.sketches.front().constraints,"Native shared arc T Redo changed remaining constraints");
            check(session.undo(),"Native shared arc T restoration failed");
            const auto saved_file=std::filesystem::temp_directory_path()/("zima-native-contact-"+feature.id+".prtz");session.document().save(saved_file);
            auto loaded=zima::document::PartDocument::load(saved_file).sketches.front();verify(loaded);std::filesystem::remove(saved_file);
            std::cout<<"Read-only FORM-KOULE shared endpoint tangency passed, reverse="<<reverse<<'\n';
        }catch(const std::exception& e){++failures;std::cout<<"FORM-KOULE reverse="<<reverse<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Native contact initial/edit matrix cases="<<cases<<" failures="<<failures<<std::endl;
    return failures?1:0;
}
