kernel::ViewerMesh PartDocument::wrapping_preview_mesh(const HistoryContainer& feature,
        const kernel::ViewerMesh& input,const PartDocument* owners,bool images) const {
    using namespace kernel;using namespace kernel::wrap_detail;
    const auto sketch=std::ranges::find(sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    const auto& p=feature.feature.wrapping;
    if(sketch==sketches.end()||!p.target)return {};
    const auto* body=owners?owners->body_owner_for_object(feature.id):nullptr;
    if(!body&&owners)body=owners->body_history.find(owners->body_history.active_body_id());
    const auto rotation=placement_rotation_matrix_from_euler_degrees(body?body->scope.rotation_degrees():Vec3{});
    const auto local=[&](Vec3 point){return body&&!p.target->external_source?placement_inverse_transform_point(rotation,body->scope.translation(),point):point;};
    const Vec3 origin{feature.placement.x,feature.placement.y,feature.placement.z};
    const auto x=sketch->resolved_x_axis,y=sketch->resolved_y_axis;
    const double tolerance=precision_value(document_precision,"linear_tolerance",.001);
    std::vector<FaceReference> selected{p.target->reference};for(const auto& face:p.continuations)selected.push_back(face.reference);
    std::vector<WrapChart> charts;ViewerMesh external;
    const ViewerMesh* domain=&input;
    if(p.target->external_source) {
        auto geometry=p.target->external_source->source_face->mesh.original_references;
        if(owners)geometry=owners->construction_reference_geometry_for(body?body->scope.id:feature.id,std::move(geometry));
        std::optional<SurfaceGeometry> support;
        for(const auto& face:geometry.triangle_references)if(wrapped_image_face_matches(p.target->reference,face)) {
            const auto data=face.surface?face.surface:face.contour_surface;if(data){support=*data;break;}
        }
        if(!support)throw std::runtime_error("Wrap target is unavailable.");
        charts.push_back({*support,p.target->reference,{p.target->reference},SurfaceWrap(*support,origin,x,y,p.reverse_u,p.reverse_v),{}});
        for(const auto point:p.target->fallback_triangles){external.triangles.push_back(static_cast<std::uint32_t>(external.vertices.size()));external.vertices.push_back(point);}
        external.triangle_references.assign(external.triangles.size()/3,p.target->reference);domain=&external;
    }else charts=viewer_wrap_charts(input,selected,origin,x,y,p.reverse_u,p.reverse_v,tolerance,local,[&](const auto& face) {
        if(!owners||!body)return true;const auto* owner=owners->body_owner_for_object(face.owner_id);
        return owner&&owner->scope.id==body->scope.id&&face.instance_path.empty();
    });
    ViewerMesh result;const auto source=sketch->evaluated_profile_sketch().viewer_mesh(false);
    using UV=std::pair<double,double>;
    const auto uv=[&](Vec3 point)->UV {const auto delta=sub(point,origin);return {dot(delta,x),dot(delta,y)};};
    const auto sub2=[](UV a,UV b)->UV{return {a.first-b.first,a.second-b.second};};
    for(const auto& chart:charts) {
        const auto& map=chart.map;
        const double depth=p.output==WrappingOutput::Relief?(feature.combine_mode==CombineMode::Subtract?-1.:1.)*(chart.surface.reversed?-1.:1.)*p.height:0;
        const auto mapped=[&](Vec3 point,double offset){const auto [u,v]=uv(point);return map.point(u,v,offset);};
        for(const auto& edge:source.edges)if(!edge.construction&&edge.points.size()>1)for(std::size_t segment=1;segment<edge.points.size();++segment) {
            const auto a=edge.points[segment-1],b=edge.points[segment];const auto pa=uv(a),pb=uv(b),direction=sub2(pb,pa);
            // Analytic support continues past trims, just like native wrapping.
            // Only explicit transitions between different charts limit a span.
            double lo=0,hi=1;
            for(const auto& limit:chart.limits) {
                const double start=limit.value(pa.first,pa.second),slope=limit.u*direction.first+limit.v*direction.second;
                if(std::abs(slope)<1e-14){if(start< -1e-9)hi=-1;continue;}
                const double root=-start/slope;if(slope>0)lo=std::max(lo,root);else hi=std::min(hi,root);
            }
            {
                if(hi-lo<1e-12)continue;
                const auto from=add(a,scale(sub(b,a),lo)),to=add(a,scale(sub(b,a),hi));
                for(double offset:depth==0?std::vector<double>{0}:std::vector<double>{0,depth}) {
                    auto wrapped=edge;wrapped.points.clear();wrapped.exact_spline.reset();wrapped.annotation.reset();
                    wrapped.reference.owner_id=feature.id;wrapped.display_owner_id=feature.id;wrapped.overlay=true;
                    wrapped.reference.semantic_key="wrap:preview:"+edge.reference.semantic_key;
                    if(p.output==WrappingOutput::Relief)wrapped.color="#00D1FF";
                    const auto append=[&](auto&& self,Vec3 a,Vec3 b,Vec3 ma,Vec3 mb,unsigned level)->void {
                        const auto middle=scale(add(a,b),.5),mapped_middle=mapped(middle,offset),chord=scale(add(ma,mb),.5);
                        if(length(sub(mapped_middle,chord))>.02) {
                            if(level==16)throw std::runtime_error("Wrap curve could not meet the model tolerance.");
                            self(self,a,middle,ma,mapped_middle,level+1);self(self,middle,b,mapped_middle,mb,level+1);
                        }else wrapped.points.push_back(mb);
                    };
                    const auto first=mapped(from,offset),last=mapped(to,offset);wrapped.points.push_back(first);append(append,from,to,first,last,0);
                    result.edges.push_back(std::move(wrapped));
                }
                if(depth!=0)for(const auto point:{from,to})if(point==edge.points.front()||point==edge.points.back()||lo>0||hi<1) {
                    ViewerEdge wall;wall.reference={feature.id,"wrap:preview:wall",{}};wall.points={mapped(point,0),mapped(point,depth)};wall.overlay=true;wall.display_owner_id=feature.id;wall.color="#00D1FF";result.edges.push_back(std::move(wall));
                }
            }
        }
    }
    if(images&&!source.images.empty()) {
        ViewerMesh target;
        for(std::size_t i=0;i<domain->triangle_references.size();++i)
            if(std::ranges::any_of(charts,[&](const auto& chart){return std::ranges::find(chart.coverage,domain->triangle_references[i])!=chart.coverage.end();})) {
                target.triangle_references.push_back(domain->triangle_references[i]);
                for(std::size_t corner=0;corner<3;++corner){target.triangles.push_back(static_cast<std::uint32_t>(target.vertices.size()));target.vertices.push_back(local(domain->vertices.at(domain->triangles.at(i*3+corner))));}
            }
        WrappingRequest request;request.origin=origin;request.x_axis=x;request.y_axis=y;
        for(const auto& image:source.images) {
            auto mapped=wrapped_chart_image(image,request,charts,target);mapped.reference.owner_id=feature.id;result.images.push_back(std::move(mapped));
        }
    }
    return result;
}
