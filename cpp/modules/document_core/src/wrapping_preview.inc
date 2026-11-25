kernel::ViewerMesh PartDocument::wrapping_preview_mesh(const HistoryContainer& feature,
        const kernel::ViewerMesh& input,const PartDocument* owners,bool images) const {
    using namespace kernel;
    const auto sketch=std::ranges::find(sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    const auto& p=feature.feature.wrapping;
    if(sketch==sketches.end()||!p.target)return {};
    std::optional<SurfaceGeometry> support;
    const auto find=[&](const auto& faces) {
        for(const auto& face:faces)if(wrapped_image_face_matches(p.target->reference,face)) {
            const auto data=face.surface?face.surface:face.contour_surface;
            if(data){support=*data;return;}
        }
    };
    const auto* body=owners?owners->body_owner_for_object(feature.id):nullptr;
    if(!body&&owners)body=owners->body_history.find(owners->body_history.active_body_id());
    if(p.target->external_source) {
        auto geometry=p.target->external_source->source_face->mesh.original_references;
        if(owners)geometry=owners->construction_reference_geometry_for(body?body->scope.id:feature.id,std::move(geometry));
        find(geometry.triangle_references);
    } else {
        find(input.triangle_references);if(!support)find(input.original_references.triangle_references);
    }
    if(support&&body&&!p.target->external_source) {
        const auto rotation=placement_rotation_matrix_from_euler_degrees(body->scope.rotation_degrees());
        support->origin=placement_inverse_transform_point(rotation,body->scope.translation(),support->origin);
        support->axis=placement_inverse_transform_direction(rotation,support->axis);
        support->radial=placement_inverse_transform_direction(rotation,support->radial);
    }
    if(!support)throw std::runtime_error("Wrap target is unavailable.");
    const Vec3 origin{feature.placement.x,feature.placement.y,feature.placement.z};
    const auto x=sketch->resolved_x_axis,y=sketch->resolved_y_axis;
    const SurfaceWrap map(*support,origin,x,y,p.reverse_u,p.reverse_v);
    const auto mapped=[&](Vec3 point,double offset) {
        const auto delta=wrap_detail::sub(point,origin);
        return map.point(wrap_detail::dot(delta,x),wrap_detail::dot(delta,y),offset);
    };
    ViewerMesh result;const auto source=sketch->evaluated_profile_sketch().viewer_mesh(false);
    const double depth=p.output==WrappingOutput::Relief?(feature.combine_mode==CombineMode::Subtract?-1.:1.)*
        (support->reversed?-1.:1.)*p.height:0;
    for(const auto& edge:source.edges)if(!edge.construction&&edge.points.size()>1) {
        for(double offset:depth==0?std::vector<double>{0}:std::vector<double>{0,depth}) {
            auto wrapped=edge;wrapped.points.clear();wrapped.exact_spline.reset();wrapped.annotation.reset();
            wrapped.reference.owner_id=feature.id;wrapped.display_owner_id=feature.id;wrapped.overlay=true;
            wrapped.reference.semantic_key="wrap:preview:"+edge.reference.semantic_key;
            if(p.output==WrappingOutput::Relief)wrapped.color="#00D1FF";
            const auto append=[&](auto&& self,Vec3 a,Vec3 b,Vec3 ma,Vec3 mb,unsigned level)->void {
                const auto middle=wrap_detail::scale(wrap_detail::add(a,b),.5),mapped_middle=mapped(middle,offset);
                const auto chord=wrap_detail::scale(wrap_detail::add(ma,mb),.5);
                if(wrap_detail::length(wrap_detail::sub(mapped_middle,chord))>.02) {
                    if(level==16)throw std::runtime_error("Wrap curve could not meet the model tolerance.");
                    self(self,a,middle,ma,mapped_middle,level+1);self(self,middle,b,mapped_middle,mb,level+1);
                } else wrapped.points.push_back(mb);
            };
            wrapped.points.push_back(mapped(edge.points.front(),offset));
            for(std::size_t i=1;i<edge.points.size();++i)append(append,edge.points[i-1],edge.points[i],mapped(edge.points[i-1],offset),mapped(edge.points[i],offset),0);
            result.edges.push_back(std::move(wrapped));
        }
        if(depth!=0)for(const auto point:{edge.points.front(),edge.points.back()}) {
            ViewerEdge wall;wall.points={mapped(point,0),mapped(point,depth)};wall.overlay=true;wall.display_owner_id=feature.id;wall.color="#00D1FF";result.edges.push_back(std::move(wall));
        }
    }
    if(images&&!source.images.empty()) {
        ViewerMesh target;
        const auto local=[&](Vec3 point) {
            if(!body||p.target->external_source)return point;
            return placement_inverse_transform_point(placement_rotation_matrix_from_euler_degrees(body->scope.rotation_degrees()),body->scope.translation(),point);
        };
        for(std::size_t i=0;!p.target->external_source&&i<input.triangle_references.size();++i)if(input.triangle_references[i]==p.target->reference)
            for(std::size_t corner=0;corner<3;++corner) {
                target.triangles.push_back(static_cast<std::uint32_t>(target.vertices.size()));
                target.vertices.push_back(local(input.vertices.at(input.triangles.at(i*3+corner))));
            }
        if(p.target->external_source)for(const auto point:p.target->fallback_triangles) {
            target.triangles.push_back(static_cast<std::uint32_t>(target.vertices.size()));target.vertices.push_back(point);
        }
        WrappingRequest request;request.origin=origin;request.x_axis=x;request.y_axis=y;
        for(const auto& image:source.images) {
            auto mapped=wrapped_image(image,request,map,target);mapped.reference.owner_id=feature.id;
            result.images.push_back(std::move(mapped));
        }
    }
    return result;
}
