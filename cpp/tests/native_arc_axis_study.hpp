#pragma once

static int run_native_arc_axis_study() {
    using namespace zima::sketcher;
    const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
    std::ifstream file(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/form-koule-shared-arcs.json");
    check(bool(file),"FORM-KOULE axis fixture missing");
    const auto source=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(file),{}));
    const auto verify=[&](const Sketch& s) {
        for(const auto& a:s.arcs) {
            const auto* c=s.find_point(a.center_point_id);check(c&&a.radius>1e-5,"Arc centre/radius invalid");
            for(const auto& id:{a.start_point_id,a.end_point_id}) {
                const auto* p=s.find_point(id);check(p&&std::abs(std::hypot(p->x-c->x,p->y-c->y)-a.radius)<1e-6,"Arc incidence failed");
            }
        }
        for(const auto& c:s.constraints) {
            if(c.suppressed)continue;
            const auto* p=s.find_point(c.first_point_id);check(p,"Axis contact point lost");
            if(c.kind==ConstraintKind::PointOnLine) {
                if(c.geometry_id.starts_with("sketch_axis:"))check(std::abs(c.geometry_id=="sketch_axis:x"?p->y:p->x)<1e-6,"Axis C equation failed");
                else {
                    const auto ref=std::ranges::find(s.external_references,c.geometry_id,&SketchExternalReference::id);
                    check(ref!=s.external_references.end()&&ref->cached_points.size()==2,"External axis support missing");
                    const auto a=ref->cached_points[0],b=ref->cached_points[1];
                    check(std::abs((p->x-a[0])*(b[1]-a[1])-(p->y-a[1])*(b[0]-a[0]))/std::hypot(b[0]-a[0],b[1]-a[1])<1e-6,"External line C equation failed");
                }
            }else if(c.kind==ConstraintKind::PointReference) {
                auto target=std::array{0.,0.};
                if(c.second_point_id!="sketch_origin") {
                    const auto ref=std::ranges::find(s.external_references,c.second_point_id,&SketchExternalReference::id);
                    check(ref!=s.external_references.end()&&ref->cached_points.size()==1,"External Origin support missing");target=ref->cached_points.front();
                }
                check(std::hypot(p->x-target[0],p->y-target[1])<1e-6,"Origin reference equation failed");
            }
            else if(c.kind==ConstraintKind::Tangent) {
                const auto a=std::ranges::find(s.arcs,c.second_geometry_id,&SketchArc::id);check(a!=s.arcs.end(),"T arc missing");
                const auto* ac=s.find_point(a->center_point_id);
                if(c.geometry_id.starts_with("sketch_axis:"))check(std::abs(c.geometry_id=="sketch_axis:x"?p->x-ac->x:p->y-ac->y)<1e-6,"Arc/axis T equation failed");
                else {
                    const auto b=std::ranges::find(s.arcs,c.geometry_id,&SketchArc::id);check(b!=s.arcs.end(),"Shared T arc missing");
                    const auto* bc=s.find_point(b->center_point_id);
                    const double residual=std::abs((p->x-ac->x)*(p->y-bc->y)-(p->y-ac->y)*(p->x-bc->x))/(a->radius*b->radius);
                    if(residual>=1e-6)std::cout<<"Shared T residual="<<residual<<" radii="<<a->radius<<","<<b->radius<<std::endl;
                    check(residual<1e-6,"Shared T equation failed");
                }
            }
        }
        for(const auto& d:s.dimensions)if(!d.suppressed) {
            const auto a=std::ranges::find(s.arcs,d.geometry_id,&SketchArc::id);
            if(a!=s.arcs.end()&&(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter))
                check(std::abs((d.kind==DimensionKind::Radius?a->radius:2*a->radius)-d.value)<1e-6,"Arc R/D equation failed");
            else if(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY||d.kind==DimensionKind::Distance) {
                const auto* p=s.find_point(d.first_point_id);const auto* q=s.find_point(d.second_point_id);
                check(p&&(q||d.second_point_id=="sketch_origin"),"Position dimension point missing");
                const double dx=(q?q->x:0)-p->x,dy=(q?q->y:0)-p->y;
                check(std::abs((d.kind==DimensionKind::DistanceX?dx:d.kind==DimensionKind::DistanceY?dy:std::hypot(dx,dy))-d.value)<1e-6,"Arc contact position equation failed");
            }else if(d.kind==DimensionKind::AngleThreePoint) {
                const auto* p=s.find_point(d.first_point_id);const auto* c=s.find_point(d.second_point_id);const auto* q=s.find_point(d.geometry_id);
                check(p&&c&&q,"Angle dimension point missing");
                const double ax=p->x-c->x,ay=p->y-c->y,bx=q->x-c->x,by=q->y-c->y;
                const double measured=std::atan2(std::abs(ax*by-ay*bx),ax*bx+ay*by)*180./std::numbers::pi;
                check(std::abs(measured-std::acos(std::clamp(std::cos(d.value*std::numbers::pi/180.),-1.,1.))*180./std::numbers::pi)<1e-6,"Arc contact angle equation failed");
            }
        }
    };
    unsigned cases=0,failures=0;
    for(unsigned tangent=0;tangent<3;++tangent)for(const bool rotated:{false,true})for(const bool mirrored:{false,true})
    for(unsigned action=0;action<10;++action) {
        ++cases;std::string stage="create";
        try {
            auto s=source;
            for(auto& p:s.points) {
                if(mirrored)p.x=-p.x;
                if(rotated){const double x=p.x;p.x=-p.y;p.y=x;}
            }
            for(auto& a:s.arcs) {
                if(mirrored)std::swap(a.start_point_id,a.end_point_id);
                const auto* c=s.find_point(a.center_point_id);const auto* p=s.find_point(a.start_point_id);const auto* q=s.find_point(a.end_point_id);
                a.start_angle=std::atan2(p->y-c->y,p->x-c->x);a.end_angle=std::atan2(q->y-c->y,q->x-c->x);
                while(a.end_angle<=a.start_angle)a.end_angle+=2*std::numbers::pi;
            }
            if(rotated)for(auto& c:s.constraints)if(c.geometry_id=="sketch_axis:x")c.geometry_id="sketch_axis:y";
            if(tangent)static_cast<void>(s.add_tangent_constraint(s.arcs[tangent==2?1:0].id,s.arcs[tangent==2?0:1].id));
            const unsigned index=action<6?action/3:(action-6)/2;
            const auto point=action<6?s.arcs[index].center_point_id:source.arcs[index].start_point_id;
            const unsigned support=action<6?action%3:(action-6)%2;
            const std::string axis=support==2?"sketch_origin":(bool(support)^rotated)?"sketch_axis:y":"sketch_axis:x";
            if(action>=6)std::erase_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnLine&&c.first_point_id==point;});
            const auto before=s.serialized();const auto constraints=s.constraints;const auto radii=std::array{s.arcs[0].radius,s.arcs[1].radius};
            const bool expected=action==1||action==4||action==5||action>=6;
            bool accepted=false;
            try {
                if(support==2)static_cast<void>(s.add_point_reference_constraint(point,axis));
                else static_cast<void>(s.add_point_on_line_constraint(point,axis));
                accepted=true;
            }catch(const std::exception&){check(!expected,"Feasible arc point axis/Origin constraint rejected");}
            check(accepted==expected,"Impossible/redundant arc contact accepted");
            if(!accepted){check(s.serialized()==before,"Rejected axis contact changed document");continue;}
            verify(s);check(s.points.size()==source.points.size()&&s.constraints.size()==constraints.size()+1,"Contact changed native point ownership");
            for(const auto& c:constraints)check(std::ranges::find(s.constraints,c.id,&SketchConstraint::id)!=s.constraints.end(),"Existing constraint identity changed");
            if(action!=8)for(unsigned n=0;n<2;++n)check(std::abs(s.arcs[n].radius-radii[n])<1e-6,"Arc contact unnecessarily changed radius");
            auto reopened=Sketch::from_serialized(s.serialized());check(reopened.serialized()==s.serialized(),"Axis contact save/reopen changed identity");verify(reopened);
            for(unsigned arc=0;arc<2;++arc)for(const bool diameter:{false,true})for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;stage="R/D arc="+std::to_string(arc)+" diameter="+std::to_string(diameter)+" driving="+std::to_string(driving)+" locked="+std::to_string(locked);
                auto edited=s;auto dimension=diameter?edited.create_arc_diameter_dimension(edited.arcs[arc].id):edited.create_arc_radius_dimension(edited.arcs[arc].id);
                dimension.driving=driving;dimension.locked=locked;
                if(!driving&&locked) {
                    const auto unchanged=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==unchanged,"Locked reference dimension was not rejected atomically");continue;
                }
                edited.apply_dimension(dimension);verify(edited);
                for(const double factor:{1.01,.99,1.02}) {
                    auto changed=dimension;changed.value=dimension.value*factor;edited.apply_dimension(changed);verify(edited);
                }
                // Exercise the remaining along-axis freedom of the selected
                // centre/end. Origin anchoring and an axis intersection reject
                // this request; rejection must preserve every persisted byte.
                for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto p=*edited.find_point(point);const auto unchanged=edited.serialized();
                    const double dx=axis=="sketch_axis:y"?0:.025,dy=axis=="sketch_axis:y"?.025:0;
                    const bool moved=edited.move_point(point,p.x+dx,p.y+dy);
                    if(moved) {verify(edited);const auto* q=edited.find_point(point);check(std::hypot(q->x-p.x,q->y-p.y)>1e-6,"Permitted drag did not move");}
                    else check(edited.serialized()==unchanged,"Rejected drag changed the document");
                }
                verify(Sketch::from_serialized(edited.serialized()));
            }
            for(unsigned kind=0;kind<5;++kind)for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;stage="position/angle kind="+std::to_string(kind)+" driving="+std::to_string(driving)+" locked="+std::to_string(locked);
                auto edited=s;
                auto dimension=kind<3?edited.create_point_dimension(source.arcs[0].end_point_id,"sketch_origin",
                    kind==0?DimensionKind::DistanceX:kind==1?DimensionKind::DistanceY:DimensionKind::Distance):
                    edited.create_three_point_angle_dimension(edited.arcs[kind-3].start_point_id,edited.arcs[kind-3].center_point_id,edited.arcs[kind-3].end_point_id);
                dimension.driving=driving;dimension.locked=locked;
                if(!driving&&locked) {
                    const auto unchanged=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==unchanged,"Locked reference position/angle not rejected atomically");continue;
                }
                edited.apply_dimension(dimension);verify(edited);
                for(const double factor:{1.005,.995,1.01}) {
                    auto changed=dimension;changed.value=dimension.value*factor;edited.apply_dimension(changed);verify(edited);
                }
                if(kind>=3&&driving)for(const double value:{-1.005*dimension.value,360.-.995*dimension.value}) {
                    auto changed=dimension;changed.value=value;edited.apply_dimension(changed);verify(edited);
                }
                for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto id=source.arcs[0].end_point_id;const auto p=*edited.find_point(id);const auto unchanged=edited.serialized();
                    if(edited.move_point(id,p.x+.005,p.y+.005)) {
                        verify(edited);const auto* q=edited.find_point(id);check(std::hypot(q->x-p.x,q->y-p.y)>1e-6,"Free dimensioned contact did not move");
                    }else check(edited.serialized()==unchanged,"Rejected dimensioned contact drag changed document");
                }
                verify(Sketch::from_serialized(edited.serialized()));
            }
            stage="fixed drag";auto fixed=s;for(auto& p:fixed.points)p.fixed=true;
            for(const auto& p:fixed.points){const auto unchanged=fixed.serialized();check(!fixed.move_point(p.id,p.x+.02,p.y+.02)&&fixed.serialized()==unchanged,"Fully fixed arc escaped constraints");}
        }catch(const std::exception& e){++failures;std::cout<<"Arc axis T="<<tangent<<" rotated="<<rotated<<" mirrored="<<mirrored<<" action="<<action<<" stage="<<stage<<": "<<e.what()<<std::endl;}
    }
    for(unsigned tangent=0;tangent<3;++tangent)for(unsigned action=0;action<3;++action)
    for(const bool diameter:{false,true})for(const bool driving:{false,true})for(const bool locked:{false,true}) {
        ++cases;
        try {
            auto s=source;if(tangent)static_cast<void>(s.add_tangent_constraint(s.arcs[tangent==2?1:0].id,s.arcs[tangent==2?0:1].id));
            auto ref=Sketch::create_external_reference(action==1?ExternalReferenceKind::Point:ExternalReferenceKind::Axis);
            ref.source_document_id="immutable-source";ref.source_owner_id="source-Origin";ref.source_semantic_key=action==1?"origin":"axis:y";
            ref.cached_points=action==1?std::vector<std::array<double,2>>{{0.,0.}}:std::vector<std::array<double,2>>{{0.,-40.},{0.,40.}};
            static_cast<void>(s.add_external_reference(ref));
            const auto point=action==0?s.arcs[0].center_point_id:action==1?s.arcs[1].center_point_id:s.arcs[1].start_point_id;
            if(action==1)static_cast<void>(s.add_point_reference_constraint(point,ref.id));
            else static_cast<void>(s.add_point_on_line_constraint(point,ref.id));
            auto d=diameter?s.create_arc_diameter_dimension(s.arcs[1].id):s.create_arc_radius_dimension(s.arcs[1].id);d.locked=locked;d.driving=driving;
            if(!driving&&locked) {
                const auto unchanged=s.serialized();bool rejected=false;
                try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}
                check(rejected&&s.serialized()==unchanged,"External locked reference dimension not rejected atomically");continue;
            }
            s.apply_dimension(d);verify(s);
            for(const double factor:{1.01,.99,1.02}){auto changed=d;changed.value=d.value*factor;s.apply_dimension(changed);verify(s);}
            for(unsigned repeat=0;repeat<3;++repeat) {
                const auto p=*s.find_point(point);const auto unchanged=s.serialized();
                if(s.move_point(point,p.x,p.y+.02))verify(s);else check(s.serialized()==unchanged,"Rejected external arc contact drag changed document");
                const auto actual=std::ranges::find(s.external_references,ref.id,&SketchExternalReference::id);
                check(actual!=s.external_references.end()&&actual->cached_points==ref.cached_points&&actual->source_document_id==ref.source_document_id&&actual->source_semantic_key==ref.source_semantic_key,"External axis/Origin source changed");
            }
            verify(Sketch::from_serialized(s.serialized()));
        }catch(const std::exception& e){++failures;std::cout<<"External arc contact T="<<tangent<<" action="<<action<<" diameter="<<diameter<<" locked="<<locked<<": "<<e.what()<<std::endl;}
    }
    for(unsigned tangent=0;tangent<3;++tangent)for(unsigned action=0;action<3;++action) {
        ++cases;
        try {
            auto s=source;if(tangent)static_cast<void>(s.add_tangent_constraint(s.arcs[tangent==2?1:0].id,s.arcs[tangent==2?0:1].id));
            const auto point=action==0?s.arcs[0].center_point_id:action==1?s.arcs[1].center_point_id:s.arcs[1].start_point_id;
            const auto contact=action==1?s.add_point_reference_constraint(point,"sketch_origin"):s.add_point_on_line_constraint(point,"sketch_axis:y");
            const auto arc=s.arcs[0];const auto center=*s.find_point(arc.center_point_id);const double angle=(arc.start_angle+arc.end_angle)/2;
            static_cast<void>(s.add_segment(center.x-20*std::cos(angle),center.y-20*std::sin(angle),center.x+20*std::cos(angle),center.y+20*std::sin(angle),1e-6,true));
            const auto before=s;auto pieces=sketch_trim_topology(s,false);
            std::erase_if(pieces,[&](const auto& piece){return piece.geometry_id!=arc.id;});
            const double removed_angle=(arc.start_angle+angle)/2;
            const auto removed=nearest_sketch_trim_piece(pieces,{center.x+arc.radius*std::cos(removed_angle),center.y+arc.radius*std::sin(removed_angle)},.01);
            check(removed.has_value(),"Arc trim piece missing");static_cast<void>(apply_sketch_trim(s,{*removed}));
            check(std::ranges::find(s.constraints,contact,&SketchConstraint::id)!=s.constraints.end()&&s.find_point(point),"Trim discarded surviving axis/Origin reference identity");verify(s);
            auto document=zima::document::PartDocument::create_default();const auto feature=zima::document::PartDocument::create_sketch_container();
            auto persisted=before;persisted.owner_container_id=feature.id;document.history={feature};document.sketches={persisted};
            zima::document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Arc axis regression"));graph.insert({zima::document::PartHistoryKind::Feature,feature.id});
            document.set_body_history(graph);document.resolve_constructions();zima::document::DocumentSession session(document);
            auto changed=document;changed.sketches.front()=s;changed.sketches.front().owner_container_id=feature.id;session.commit(changed);
            check(session.undo()&&session.document().sketches.front().serialized()==persisted.serialized(),"Arc trim Undo changed axis identities");
            check(session.redo()&&session.document().sketches.front().serialized()==changed.sketches.front().serialized(),"Arc trim Redo changed axis identities");
            const auto path=std::filesystem::temp_directory_path()/("zima-arc-axis-"+feature.id+".prtz");session.document().save(path);
            verify(zima::document::PartDocument::load(path).sketches.front());std::filesystem::remove(path);
        }catch(const std::exception& e){++failures;std::cout<<"Arc axis trim T="<<tangent<<" action="<<action<<": "<<e.what()<<std::endl;}
    }
    std::ifstream current_file(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/form-koule-axis-ends.json");
    check(bool(current_file),"Current FORM-KOULE axis-end fixture missing");
    const auto current=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(current_file),{}));
    for(unsigned action=0;action<7;++action) {
        ++cases;
        try {
            auto s=current;const unsigned index=action<6?action/3:1,support=action<6?action%3:1;
            const auto point=action<6?s.arcs[index].center_point_id:s.arcs[1].start_point_id;
            if(action==6)std::erase_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnLine&&c.first_point_id==point;});
            const auto before=s.serialized();bool accepted=false;
            try {
                if(support==2)static_cast<void>(s.add_point_reference_constraint(point,"sketch_origin"));
                else static_cast<void>(s.add_point_on_line_constraint(point,support?"sketch_axis:y":"sketch_axis:x"));
                accepted=true;
            }catch(const std::exception&){check(action!=4&&action!=5&&action!=6,"Current FORM-KOULE feasible contact rejected");}
            check(accepted==(action==4||action==5||action==6),"Current FORM-KOULE incompatible contact accepted");
            if(!accepted){check(s.serialized()==before,"Current FORM-KOULE rejected contact changed geometry");continue;}
            verify(s);
            for(unsigned arc=0;arc<2;++arc)for(const bool diameter:{false,true})for(const bool driving:{false,true})for(const bool locked:{false,true}) {
                ++cases;auto edited=s;auto d=diameter?edited.create_arc_diameter_dimension(edited.arcs[arc].id):edited.create_arc_radius_dimension(edited.arcs[arc].id);
                d.driving=driving;d.locked=locked;
                if(!driving&&locked){const auto unchanged=edited.serialized();bool rejected=false;try{edited.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&edited.serialized()==unchanged,"Current fixture locked reference accepted");continue;}
                edited.apply_dimension(d);
                for(const double factor:{1.01,.99,1.02}){auto changed=d;changed.value=d.value*factor;edited.apply_dimension(changed);verify(edited);}
                for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto id=edited.arcs[arc].center_point_id;const auto p=*edited.find_point(id);const auto unchanged=edited.serialized();
                    if(edited.move_point(id,p.x+.01,p.y+.01))verify(edited);else check(edited.serialized()==unchanged,"Current fixture rejected drag changed document");
                }
                verify(Sketch::from_serialized(edited.serialized()));
            }
        }catch(const std::exception& e){++failures;std::cout<<"Current FORM-KOULE action="<<action<<": "<<e.what()<<std::endl;}
    }
    std::cout<<"Native arc centre/endpoint axis/Origin matrix cases="<<cases<<" failures="<<failures<<std::endl;
    return failures?1:0;
}
