// Disposable preview from the current viewer packet. It never supplies native
// references or partition geometry and is reused by ordinary feature inspection.
kernel::ViewerMesh PartDocument::projection_preview_mesh(
        const HistoryContainer& feature,const kernel::ViewerMesh& input,const PartDocument* owners) const {
    using namespace kernel;
    const auto sketch=std::ranges::find(sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    if(sketch==sketches.end())return {};
    const auto& p=feature.feature.projection;
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
    struct Triangle {Vec3 a,b,c;std::size_t target;};std::vector<Triangle> triangles;
    for(std::size_t i=0;i<input.triangle_references.size();++i) {
        const auto target=std::ranges::find_if(p.targets,[&](const auto& value){return value.reference==input.triangle_references[i];});
        if(target==p.targets.end()||i*3+2>=input.triangles.size())continue;
        const auto a=input.triangles[i*3],b=input.triangles[i*3+1],c=input.triangles[i*3+2];
        if(a>=input.vertices.size()||b>=input.vertices.size()||c>=input.vertices.size())continue;
        triangles.push_back({local(input.vertices[a]),local(input.vertices[b]),local(input.vertices[c]),static_cast<std::size_t>(target-p.targets.begin())});
    }
    // External packets are already expressed in the destination Body frame.
    for(std::size_t i=0;i<p.targets.size();++i)if(p.targets[i].external_source) {
        const auto& points=p.targets[i].fallback_triangles;
        for(std::size_t j=0;j+2<points.size();j+=3)triangles.push_back({points[j],points[j+1],points[j+2],i});
    }
    const auto cast=[&](Vec3 point)->std::optional<Vec3> {
        double closest=std::numeric_limits<double>::infinity();std::size_t owner=0;bool ambiguous=false,on_boundary=false;
        for(const auto& triangle:triangles) {
            const auto e1=placement_vec_sub(triangle.b,triangle.a),e2=placement_vec_sub(triangle.c,triangle.a);
            const auto h=placement_vec_cross(direction,e2);const double determinant=placement_vec_dot(e1,h);
            if(std::abs(determinant)<1e-14)continue;
            const auto s=placement_vec_sub(point,triangle.a);const double u=placement_vec_dot(s,h)/determinant;
            const auto q=placement_vec_cross(s,e1);const double v=placement_vec_dot(direction,q)/determinant;
            const double distance=placement_vec_dot(e2,q)/determinant;
            if(u< -1e-9||v< -1e-9||u+v>1+1e-9||distance< -1e-7)continue;
            const bool boundary=std::min({u,v,1-u-v})<1e-8;
            if(distance<closest-1e-7) {closest=std::max(0.,distance);owner=triangle.target;ambiguous=false;on_boundary=boundary;}
            else if(std::abs(distance-closest)<1e-7&&owner!=triangle.target&&!(boundary&&on_boundary))ambiguous=true;
        }
        if(!std::isfinite(closest)||ambiguous)return {};
        return placement_vec_add(point,placement_vec_scale(direction,closest));
    };
    ViewerMesh result;
    auto source=sketch->evaluated_profile_sketch().viewer_mesh(false);
    for(auto edge:source.edges)if(!edge.construction&&!edge.points.empty()) {
        edge.reference.owner_id=feature.id;edge.reference.semantic_key="projection:preview:source:"+edge.reference.semantic_key;
        edge.color="#FFFFFF";edge.exact_spline.reset();result.edges.push_back(edge);
        for(std::size_t i=1;i<edge.points.size();++i) {
            const auto a=edge.points[i-1],delta=placement_vec_sub(edge.points[i],a);
            // Existing curved display packets already contain adaptive samples;
            // straight spans need intermediate rays to show target curvature.
            const int count=edge.points.size()==2?64:2;
            auto previous=a;auto projected=cast(a);
            for(int sample=1;sample<=count;++sample) {
                const auto next=placement_vec_add(a,placement_vec_scale(delta,double(sample)/count));const auto hit=cast(next);
                ViewerEdge span;span.reference={feature.id,projected&&hit?"projection:preview:result":"projection:preview:missing",{}};
                if(projected&&hit){span.points={*projected,*hit};span.color="#FFFFFF";}
                else {span.points={previous,next};span.color="#FF5E52";span.preview_terminal_dashed=true;}
                result.edges.push_back(std::move(span));previous=next;projected=hit;
            }
        }
    }
    for(auto point:source.points)if(!point.construction&&point.always_visible&&point.reference.semantic_key.starts_with("point:")) {
        const auto key=point.reference.semantic_key;
        point.reference={feature.id,"projection:preview:source:"+key,{}};point.label.clear();
        result.points.push_back(point);
        if(const auto hit=cast(point.position)) {
            point.position=*hit;point.reference.semantic_key="projection:preview:result:"+key;
            result.points.push_back(std::move(point));
        }
    }
    return result;
}
