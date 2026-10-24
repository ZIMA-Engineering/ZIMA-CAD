void verify_face_projection_matrix() {
    using namespace zima::sketcher;
    using V=zima::kernel::Vec3;
    for(bool contour_only:{false,true})for(bool individual:{false,true})for(auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ})
    for(bool cone:{false,true})for(double side:{-1.,1.}) {
        auto frame=Sketch::create_default();frame.plane=plane;frame.refresh_default_frame();
        const auto origin=frame.world_point(0,0),x=frame.world_point(1,0),y=frame.world_point(0,1);
        const V axis{y.x-origin.x,y.y-origin.y,y.z-origin.z};
        const V radial{x.x-origin.x,x.y-origin.y,x.z-origin.z};
        const auto normal=frame.resolved_normal;
        const auto source=[&](double radius) {
            zima::kernel::ViewerReferenceGeometry g;
            auto analytic=std::make_shared<zima::kernel::SurfaceGeometry>();
            analytic->kind=cone?zima::kernel::SurfaceGeometry::Kind::Cone:zima::kernel::SurfaceGeometry::Kind::Cylinder;
            analytic->origin=origin;analytic->axis=axis;analytic->radial=radial;
            analytic->radius=radius;analytic->semi_angle=cone?std::atan(.1):0.;analytic->axial_min=0;analytic->axial_max=12;
            constexpr unsigned count=32;
            for(unsigned i=0;i<=count;++i)for(double z:{0.,12.}) {
                const double a=(i==count?0:i)*2*std::numbers::pi/count+.073;
                const double r=radius+(cone?.1*z:0.);
                const auto p=frame.world_point(r*std::cos(a),z);
                g.vertices.push_back({p.x+normal.x*r*std::sin(a),p.y+normal.y*r*std::sin(a),p.z+normal.z*r*std::sin(a)});
            }
            for(unsigned i=0;i<count;++i) {
                g.triangles.insert(g.triangles.end(),{2*i,2*i+2,2*i+3,2*i,2*i+3,2*i+1});
                for(int j=0;j<2;++j){zima::kernel::FaceReference ref{"source","side",{}};if(contour_only)ref.contour_surface=analytic;else ref.surface=analytic;g.triangle_references.push_back(ref);}
            }
            return g;
        };
        auto g=source(5);const auto unchanged=g.vertices;
        if(individual) {
            auto partial=g;
            partial.triangles.resize(partial.triangles.size()/2);
            partial.triangle_references.resize(partial.triangle_references.size()/2);
            const auto curves=external_face_curve_edges(partial,frame.world_point(0,0),frame.resolved_x_axis,frame.resolved_y_axis);
            require(curves.size()==1,"A half cylinder/cone must offer only its supported exact generator");
            require(frame.local_point(curves.front().points.front())[0]<0,"A half-band offered an unsupported opposite generator");
            auto shifted=frame;shifted.plane_offset=100;shifted.refresh_default_frame();
            const auto shifted_curves=external_face_curve_edges(partial,shifted.world_point(0,0),shifted.resolved_x_axis,shifted.resolved_y_axis);
            require(shifted_curves.size()==1&&shifted_curves.front().points==curves.front().points,
                "The offered source generator moved onto the offset Sketch plane");
            const auto paths=shifted.external_face_reference_paths(partial,{"source",curves.front().reference.semantic_key,{}});
            require(paths&&paths->size()==1&&paths->front().size()==2,"Partial analytic band cannot supply an individual reference");
            for(unsigned i=0;i<2;++i) {
                const auto expected=frame.local_point(curves.front().points[i]);
                require(std::hypot((*paths)[0][i][0]-expected[0],(*paths)[0][i][1]-expected[1])<1e-8,
                    "An offset partial-band cache changed its exact projected line");
            }
        }
        const auto baseline=frame.external_face_reference_paths(g,{"source","side",{}});
        require(baseline&&baseline->size()==1&&baseline->front().size()==5,"Analytic face outline was faceted or incomplete");
        for(double offset:{-100.,-10.,0.,10.,100.}) {
            auto s=frame;s.plane_offset=offset;s.refresh_default_frame();
            const auto projected=s.external_face_reference_paths(g,{"source","side",{}});
            require(projected==baseline,"Normal displacement changed the projected face boundary");
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Face);
            ref.source_document_id="source-document";ref.source_owner_id="source";ref.source_semantic_key="side";
            if(individual) {
                const auto edges=external_face_curve_edges(g,s.world_point(0,0),s.resolved_x_axis,s.resolved_y_axis);
                require(edges.size()==2,"Both cylinder/cone generators must be independently selectable");
                const auto chosen=std::ranges::find_if(edges,[&](const auto& edge){return s.local_point(edge.points.front())[0]*side>0;});
                require(chosen!=edges.end(),"Requested outline side is missing");
                ref.source_semantic_key=chosen->reference.semantic_key;
                const auto path=s.external_face_reference_paths(g,{"source",ref.source_semantic_key,{}});
                require(path&&path->size()==1&&path->front().size()==2,"A selected generator contains another boundary");
                ref.cached_paths=*path;
            } else ref.cached_paths=*projected;
            const auto id=ref.id;s.add_external_reference(ref);
            const auto point=s.add_point(side*(5+(cone?.6:0)),6);
            static_cast<void>(s.add_point_on_line_constraint(point,id));
            auto d=s.create_point_dimension("sketch_origin",point,DimensionKind::DistanceY);
            d.locked=true;s.apply_dimension(d);
            for(double radius:{6.,4.,5.}) {
                const auto updated=source(radius);static_cast<void>(s.refresh_external_references("source-document",updated));
                const auto* p=s.find_point(point);
                require(std::abs(p->y-6)<1e-7&&std::abs(p->x-side*(radius+(cone?.6:0)))<1e-7,
                    "Projected source update lost point contact or its dimension");
                for(double height:{7.,8.,6.}) {
                    const double origin_coordinate=plane==SketchPlane::XZ?-height:height;
                    require(s.set_dimension_value(d.id,origin_coordinate),"Projected face contact blocked successive Feature Origin coordinate edits");
                    const auto* q=s.find_point(point);
                    require(std::abs(q->y-height)<1e-7&&std::abs(q->x-side*(radius+(cone?.1*height:0)))<1e-7,
                        "Projected contact did not follow its exact straight contour");
                }
            }
            const auto fixed=s.serialized();
            require(!s.move_point(point,side*99,99)&&s.serialized()==fixed,
                "Fully dimensioned projected contact escaped during dragging");
            s.dimensions.front().locked=false;
            for(double height:{7.,8.,6.}) {
                require(s.move_point(point,side*(5+(cone?.1*height:0)),height),
                    "Unlocked projected contact could not be dragged");
                const auto* q=s.find_point(point);
                require(std::abs(q->y-height)<1e-7&&std::abs(q->x-side*(5+(cone?.1*height:0)))<1e-7,
                    "Dragging left the projected source contour");
            }
            const auto saved=Sketch::from_serialized(s.serialized());
            require(saved.external_references.front().id==id&&!saved.external_references.front().broken,
                "Projected face reference identity did not persist");
            require(g.vertices==unchanged,"Face projection modified its read-only source");
        }
    }
    for(bool reversed:{false,true})for(double side:{-1.,1.}) {
        auto s=Sketch::create_default();s.plane_offset=100;s.refresh_default_frame();
        zima::kernel::ViewerReferenceGeometry g;
        g.vertices={{2,-3,-2},{2,3,-2},{2,3,2},{2,-3,2}};
        g.triangles=reversed?std::vector<std::uint32_t>{0,2,1,0,3,2}:std::vector<std::uint32_t>{0,1,2,0,2,3};
        g.triangle_references.assign(2,{"plane","side",{}});
        const auto paths=s.external_face_reference_paths(g,{"plane","side",{}});
        require(paths&&paths->size()==1,"Edge-on cap did not project to one line");
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Face);
        ref.source_document_id="source-document";ref.source_owner_id="plane";ref.source_semantic_key="side";
        ref.cached_paths=*paths;const auto id=ref.id;s.add_external_reference(ref);
        const auto point=s.add_point(2+side*2,0);auto d=s.create_point_line_dimension(point,id);s.apply_dimension(d);
        const auto branch=s.dimensions.front().solution_side;
        for(double x:{3.,4.,2.}) {
            for(auto& v:g.vertices)v.x=x;
            static_cast<void>(s.refresh_external_references("source-document",g));
            require(std::abs(s.find_point(point)->x-x-side*2)<1e-7 && s.dimensions.front().solution_side==branch,
                "Projected line refresh reversed the persisted distance side");
            require(s.set_dimension_value(d.id,0)&&s.set_dimension_value(d.id,2),
                "Projected line rejected zero/nonzero dimension edits");
            require(std::abs(s.find_point(point)->x-x-side*2)<1e-7&&s.dimensions.front().solution_side==branch,
                "Projected line lost its persisted side at zero distance");
        }
    }
    std::cout<<"Projected cylinder/cone boundaries: 240 surface/contour whole-face/individual-curve plane/offset/side matrices, source edits and dependent dimensions passed\n";
}
