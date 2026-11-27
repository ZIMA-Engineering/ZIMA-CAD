// Disposable preview from the current viewer packet. It never supplies native
// references or partition geometry and is reused by ordinary feature inspection.
kernel::ViewerMesh PartDocument::projection_preview_mesh(
        const HistoryContainer& feature,const kernel::ViewerMesh& input,const PartDocument* owners,bool* incomplete) const {
    using namespace kernel;
    if(incomplete)*incomplete=false;
    const auto sketch=std::ranges::find(sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    if(sketch==sketches.end())return {};
    const auto& p=feature.feature.projection;
    const bool both_directions=p.direction==ProjectionDirection::SketchNormal;
    auto direction=sketch->normal();
    const auto* body=owners?owners->body_owner_for_object(feature.id):nullptr;
    if(!body&&owners)body=owners->body_history.find(owners->body_history.active_body_id());
    const auto local=[&](Vec3 point){return body?placement_inverse_transform_point(
        placement_rotation_matrix_from_euler_degrees(body->scope.rotation_degrees()),body->scope.translation(),point):point;};
    if(p.direction==ProjectionDirection::Reference) {
        auto references=input.original_references;
        references.edges.insert(references.edges.end(),input.edges.begin(),input.edges.end());
        if(owners) {
            append_reference_geometry(references,owners->origin_viewer_mesh().original_references);
            append_reference_geometry(references,owners->history_origin_reference_geometry_before(feature.id));
            append_reference_geometry(references,owners->construction_viewer_mesh().original_references);
            references=owners->construction_reference_geometry_for(body?body->scope.id:feature.id,std::move(references));
        }
        ConstructionReference ref;ref.owner_id=p.direction_reference.owner_id;ref.semantic_key=p.direction_reference.semantic_key;ref.instance_path=p.direction_reference.instance_path;
        const auto axis=placement_reference_axis(ref,references,{},true);
        if(!axis)throw std::runtime_error("Projection direction reference is missing or is not straight.");
        direction=axis->direction;
    }
    direction=placement_vec_normalized(direction);if(p.reverse)direction=placement_vec_scale(direction,-1);
    std::vector<FaceReference> selected_faces;
    for(const auto& target:p.targets)selected_faces.push_back(target.reference);
    if(p.output==ProjectionOutput::Relief)
        selected_faces=continued_viewer_surfaces(input,selected_faces,precision_value(document_precision,"linear_tolerance",0.001),[&](const auto& face) {
            if(!owners||!body)return true;
            const auto* owner=owners->body_owner_for_object(face.owner_id);
            return owner&&owner->scope.id==body->scope.id&&face.instance_path.empty();
        });
    struct Triangle {Vec3 a,b,c;std::size_t target;std::shared_ptr<const SurfaceGeometry> surface;};std::vector<Triangle> triangles;
    std::map<std::tuple<std::string,std::string,std::string>,std::shared_ptr<const SurfaceGeometry>> supports;
    for(const auto& face:viewer_surface_references(input))
        supports.emplace(std::tuple{face.owner_id,face.semantic_key,face.instance_path},face.surface?face.surface:face.contour_surface);
    struct Analytic {SurfaceGeometry surface;std::size_t target;};std::vector<Analytic> analytic;
    const auto add_analytic=[&](const SurfaceGeometry& source,std::size_t target,bool external) {
        if(p.output!=ProjectionOutput::Curves||source.kind==SurfaceGeometry::Kind::Torus)return;
        auto surface=source;
        if(!external) {
            surface.origin=local(source.origin);
            surface.axis=placement_vec_normalized(placement_vec_sub(local(placement_vec_add(source.origin,source.axis)),surface.origin));
        }
        analytic.push_back({surface,target});
    };
    for(std::size_t i=0;i<selected_faces.size();++i) {
        const auto& face=selected_faces[i];const auto found=supports.find({face.owner_id,face.semantic_key,face.instance_path});
        if(found!=supports.end()&&found->second)add_analytic(*found->second,i,false);
    }
    for(std::size_t i=0;i<input.triangle_references.size();++i) {
        const auto target=std::ranges::find(selected_faces,input.triangle_references[i]);
        if(target==selected_faces.end()||i*3+2>=input.triangles.size())continue;
        if(std::ranges::any_of(analytic,[&](const auto& item){return item.target==static_cast<std::size_t>(target-selected_faces.begin());}))continue;
        const auto a=input.triangles[i*3],b=input.triangles[i*3+1],c=input.triangles[i*3+2];
        if(a>=input.vertices.size()||b>=input.vertices.size()||c>=input.vertices.size())continue;
        const auto& face=input.triangle_references[i];
        triangles.push_back({local(input.vertices[a]),local(input.vertices[b]),local(input.vertices[c]),
            static_cast<std::size_t>(target-selected_faces.begin()),supports.at({face.owner_id,face.semantic_key,face.instance_path})});
    }
    // External packets are already expressed in the destination Body frame.
    for(std::size_t i=0;i<p.targets.size();++i)if(p.targets[i].external_source) {
        auto references=p.targets[i].external_source->source_face->mesh.original_references;
        if(owners)references=owners->construction_reference_geometry_for(body?body->scope.id:feature.id,std::move(references));
        const auto& selected=p.targets[i].reference;
        for(const auto& face:references.triangle_references)if(face.owner_id==selected.owner_id&&face.semantic_key==selected.semantic_key) {
            const auto support=face.surface?face.surface:face.contour_surface;
            if(support){add_analytic(*support,i,true);break;}
        }
        if(std::ranges::any_of(analytic,[&](const auto& item){return item.target==i;}))continue;
        const auto& points=p.targets[i].fallback_triangles;
        for(std::size_t j=0;j+2<points.size();j+=3)triangles.push_back({points[j],points[j+1],points[j+2],i});
    }
    struct Hit {Vec3 point,normal;};
    const auto cast=[&](Vec3 point)->std::optional<Hit> {
        double closest=std::numeric_limits<double>::infinity(),signed_depth=0;std::size_t owner=0;bool ambiguous=false,on_boundary=false;
        const Triangle* nearest=nullptr;
        const Analytic* nearest_analytic=nullptr;
        for(const auto& triangle:triangles) {
            const auto e1=placement_vec_sub(triangle.b,triangle.a),e2=placement_vec_sub(triangle.c,triangle.a);
            const auto h=placement_vec_cross(direction,e2);const double determinant=placement_vec_dot(e1,h);
            if(std::abs(determinant)<1e-14)continue;
            const auto s=placement_vec_sub(point,triangle.a);const double u=placement_vec_dot(s,h)/determinant;
            const auto q=placement_vec_cross(s,e1);const double v=placement_vec_dot(direction,q)/determinant;
            const double depth=placement_vec_dot(e2,q)/determinant;
            if(u< -1e-9||v< -1e-9||u+v>1+1e-9||(!both_directions&&depth< -1e-7))continue;
            const double distance=std::abs(depth);
            const bool boundary=std::min({u,v,1-u-v})<1e-8;
            if(distance<closest-1e-7) {closest=distance;signed_depth=depth;owner=triangle.target;ambiguous=false;on_boundary=boundary;nearest=&triangle;}
            else if(std::abs(distance-closest)<1e-7&&
                (std::abs(depth-signed_depth)>1e-7||(owner!=triangle.target&&!(boundary&&on_boundary))))ambiguous=true;
        }
        // Curves use the complete analytic support; current mesh trims must not
        // remove a mathematically defined preview or choose a faceted hit.
        for(const auto& item:analytic) {
            const auto& surface=item.surface;const auto axis=placement_vec_normalized(surface.axis);
            const auto delta=placement_vec_sub(point,surface.origin);
            const double axial=placement_vec_dot(delta,axis),axial_direction=placement_vec_dot(direction,axis);
            const auto radial=placement_vec_sub(delta,placement_vec_scale(axis,axial));
            const auto radial_direction=placement_vec_sub(direction,placement_vec_scale(axis,axial_direction));
            std::vector<double> roots;
            const auto quadratic=[&](double a,double b,double c) {
                if(std::abs(a)<1e-14){if(std::abs(b)>1e-14)roots.push_back(-c/b);return;}
                const double discriminant=b*b-4*a*c;
                const double roundoff=32*std::numeric_limits<double>::epsilon()*(b*b+std::abs(4*a*c));
                if(discriminant< -roundoff)return;
                const double q=-.5*(b+std::copysign(std::sqrt(std::max(0.,discriminant)),b));
                if(q==0)roots.push_back(-b/(2*a));else {roots.push_back(q/a);roots.push_back(c/q);}
            };
            if(surface.kind==SurfaceGeometry::Kind::Plane) {
                if(std::abs(axial_direction)>1e-14)roots.push_back(-axial/axial_direction);
            }else if(surface.kind==SurfaceGeometry::Kind::Sphere) {
                quadratic(1,2*placement_vec_dot(delta,direction),placement_vec_dot(delta,delta)-surface.radius*surface.radius);
            }else {
                const double slope=surface.kind==SurfaceGeometry::Kind::Cone?std::tan(surface.semi_angle):0;
                const double radius=surface.radius+axial*slope,rate=axial_direction*slope;
                quadratic(placement_vec_dot(radial_direction,radial_direction)-rate*rate,
                    2*(placement_vec_dot(radial,radial_direction)-radius*rate),placement_vec_dot(radial,radial)-radius*radius);
            }
            for(const double depth:roots) {
                if(!std::isfinite(depth)||(!both_directions&&depth< -1e-7))continue;
                if(surface.kind==SurfaceGeometry::Kind::Cone&&surface.radius+(axial+depth*axial_direction)*std::tan(surface.semi_angle)<=1e-7)continue;
                const double distance=std::abs(depth);
                if(distance<closest-1e-7) {
                    closest=distance;signed_depth=depth;owner=item.target;ambiguous=false;nearest_analytic=&item;
                }else if(std::abs(distance-closest)<1e-7&&
                    (std::abs(depth-signed_depth)>1e-7||(owner!=item.target&&
                        !(nearest_analytic&&same_surface_support(nearest_analytic->surface,surface,1e-7)))))ambiguous=true;
            }
        }
        if(!std::isfinite(closest)||ambiguous)return {};
        const auto hit=placement_vec_add(point,placement_vec_scale(direction,signed_depth));
        if(nearest_analytic) {
            const auto& surface=nearest_analytic->surface;const auto axis=placement_vec_normalized(surface.axis);
            const auto delta=placement_vec_sub(hit,surface.origin);
            const auto radial=placement_vec_sub(delta,placement_vec_scale(axis,placement_vec_dot(delta,axis)));
            auto normal=axis;
            if(surface.kind==SurfaceGeometry::Kind::Sphere)normal=placement_vec_normalized(delta);
            else if(surface.kind==SurfaceGeometry::Kind::Cylinder)normal=placement_vec_normalized(radial);
            else if(surface.kind==SurfaceGeometry::Kind::Cone)normal=placement_vec_sub(
                placement_vec_scale(placement_vec_normalized(radial),std::cos(surface.semi_angle)),placement_vec_scale(axis,std::sin(surface.semi_angle)));
            return Hit{hit,placement_vec_scale(normal,surface.reversed?-1.:1.)};
        }
        auto normal=placement_vec_normalized(placement_vec_cross(placement_vec_sub(nearest->b,nearest->a),placement_vec_sub(nearest->c,nearest->a)));
        // Smooth analytic normals come from the already calculated face packet.
        // Triangle orientation remains authoritative for the material side.
        if(const auto& surface=nearest->surface) {
            const auto axis=placement_vec_normalized(placement_vec_sub(local(placement_vec_add(surface->origin,surface->axis)),local(surface->origin)));
            const auto delta=placement_vec_sub(hit,local(surface->origin));
            const auto radial=placement_vec_sub(delta,placement_vec_scale(axis,placement_vec_dot(delta,axis)));
            Vec3 analytic=normal;
            switch(surface->kind) {
                case SurfaceGeometry::Kind::Plane:analytic=axis;break;
                case SurfaceGeometry::Kind::Sphere:analytic=placement_vec_normalized(delta);break;
                case SurfaceGeometry::Kind::Cylinder:analytic=placement_vec_normalized(radial);break;
                case SurfaceGeometry::Kind::Cone:analytic=placement_vec_sub(
                    placement_vec_scale(placement_vec_normalized(radial),std::cos(surface->semi_angle)),
                    placement_vec_scale(axis,std::sin(surface->semi_angle)));break;
                case SurfaceGeometry::Kind::Torus:analytic=placement_vec_normalized(placement_vec_sub(delta,
                    placement_vec_scale(placement_vec_normalized(radial),surface->radius)));break;
            }
            normal=placement_vec_scale(analytic,placement_vec_dot(analytic,normal)<0?-1.:1.);
        }
        return Hit{hit,normal};
    };
    ViewerMesh result;
    const double depth=p.output==ProjectionOutput::Relief?
        (feature.combine_mode==CombineMode::Subtract?-1.:1.)*p.height:0;
    const auto raised=[&](const Hit& hit) {
        const auto normal=p.relief_direction==ProjectionReliefDirection::Projection?
            placement_vec_scale(direction,placement_vec_dot(direction,hit.normal)<0?-1.:1.):hit.normal;
        return placement_vec_add(hit.point,placement_vec_scale(normal,depth));
    };
    const auto span=[&](Vec3 a,Vec3 b,const char* role) {
        ViewerEdge edge;edge.reference={feature.id,role,{}};edge.points={a,b};
        edge.color=p.output==ProjectionOutput::Relief?"#00D1FF":"#FFFFFF";
        result.edges.push_back(std::move(edge));
    };
    auto source=sketch->evaluated_profile_sketch().viewer_mesh(false);
    for(auto edge:source.edges)if(!edge.construction&&!edge.points.empty()) {
        edge.reference.owner_id=feature.id;edge.reference.semantic_key="projection:preview:source:"+edge.reference.semantic_key;
        edge.color="#FFFFFF";edge.exact_spline.reset();result.edges.push_back(edge);
        for(std::size_t i=1;i<edge.points.size();++i) {
            const auto a=edge.points[i-1],delta=placement_vec_sub(edge.points[i],a);
            // Existing curved display packets already contain adaptive samples;
            // straight spans need intermediate rays to show target curvature.
            const int count=edge.points.size()==2?64:2;
            auto projected=cast(a);
            for(int sample=1;sample<=count;++sample) {
                const auto next=placement_vec_add(a,placement_vec_scale(delta,double(sample)/count));const auto hit=cast(next);
                if(projected&&hit) {
                    span(projected->point,hit->point,"projection:preview:result");
                    if(depth!=0)span(raised(*projected),raised(*hit),"projection:preview:relief");
                } else if(incomplete)*incomplete=true;
                projected=hit;
            }
        }
        if(depth!=0)for(const auto point:{edge.points.front(),edge.points.back()})
            if(const auto hit=cast(point))span(hit->point,raised(*hit),"projection:preview:wall");
    }
    for(auto point:source.points)if(!point.construction&&point.always_visible&&point.reference.semantic_key.starts_with("point:")) {
        const auto key=point.reference.semantic_key;
        point.reference={feature.id,"projection:preview:source:"+key,{}};point.label.clear();
        result.points.push_back(point);
        if(const auto hit=cast(point.position)) {
            point.position=hit->point;point.reference.semantic_key="projection:preview:result:"+key;
            result.points.push_back(std::move(point));
        }
    }
    return result;
}
