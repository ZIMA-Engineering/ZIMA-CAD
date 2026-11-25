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
    struct Triangle {Vec3 a,b,c;std::size_t target;std::shared_ptr<const SurfaceGeometry> surface;};std::vector<Triangle> triangles;
    for(std::size_t i=0;i<input.triangle_references.size();++i) {
        const auto target=std::ranges::find_if(p.targets,[&](const auto& value){return value.reference==input.triangle_references[i];});
        if(target==p.targets.end()||i*3+2>=input.triangles.size())continue;
        const auto a=input.triangles[i*3],b=input.triangles[i*3+1],c=input.triangles[i*3+2];
        if(a>=input.vertices.size()||b>=input.vertices.size()||c>=input.vertices.size())continue;
        const auto& face=input.triangle_references[i];
        triangles.push_back({local(input.vertices[a]),local(input.vertices[b]),local(input.vertices[c]),
            static_cast<std::size_t>(target-p.targets.begin()),face.surface?face.surface:face.contour_surface});
    }
    // External packets are already expressed in the destination Body frame.
    for(std::size_t i=0;i<p.targets.size();++i)if(p.targets[i].external_source) {
        const auto& points=p.targets[i].fallback_triangles;
        for(std::size_t j=0;j+2<points.size();j+=3)triangles.push_back({points[j],points[j+1],points[j+2],i});
    }
    struct Hit {Vec3 point,normal;};
    const auto cast=[&](Vec3 point)->std::optional<Hit> {
        double closest=std::numeric_limits<double>::infinity(),signed_depth=0;std::size_t owner=0;bool ambiguous=false,on_boundary=false;
        const Triangle* nearest=nullptr;
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
        if(!std::isfinite(closest)||ambiguous)return {};
        const auto hit=placement_vec_add(point,placement_vec_scale(direction,signed_depth));
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
    const auto raised=[&](const Hit& hit){return placement_vec_add(hit.point,placement_vec_scale(hit.normal,depth));};
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
