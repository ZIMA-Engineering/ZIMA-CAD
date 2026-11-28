// Operation-local authored boundary preparation, before material evaluation.
// These finite planar sections are derived from native authored equations;
// no sampled curve or calculated solid defines their support.
template<class Part> auto* skeleton_sketch(Part& part,const std::string& id) {
    const auto found=std::ranges::find(part.sketches,id,&sketcher::Sketch::owner_container_id);
    if(found==part.sketches.end())throw std::runtime_error("Authored boundary preparation lost a native profile");
    return &*found;
}
kernel::ViewerReferenceGeometry skeleton_geometry(const document::PartDocument& part,
        const std::vector<std::string>& created) {
    kernel::ViewerReferenceGeometry result;
    const auto defaults=document::sheet_metal_defaults(part);
    const auto section=[&](const std::string& owner,const std::string& key,
            V a,V b,V inner_a,V inner_b,V outward,V skin_normal,
            const std::string& first,const std::string& last,double thickness,
            std::shared_ptr<const kernel::SurfaceGeometry> native_skin={}) {
        Face cap{owner,"generated:skeleton-cap:"+key,{}},skin{owner,"generated:skeleton-skin:"+key,{}};
        auto plane=std::make_shared<kernel::SurfaceGeometry>();plane->origin=a;plane->axis=unit(outward);
        cap.surface=plane;cap.sheet_role=kernel::SheetFaceRole::ThicknessFace;
        auto skin_plane=std::make_shared<kernel::SurfaceGeometry>();skin_plane->origin=a;skin_plane->axis=unit(skin_normal);
        skin.surface=native_skin?native_skin:skin_plane;skin.sheet_role=kernel::SheetFaceRole::SideA;
        for(auto* face:{&cap,&skin}){face->sheet_owner=owner;face->sheet_thickness=thickness;}
        const auto offset=static_cast<unsigned>(result.vertices.size());
        result.vertices.insert(result.vertices.end(),{a,b,inner_b,inner_a});
        if(dot(cross(sub(b,a),sub(inner_b,a)),outward)>0)
            result.triangles.insert(result.triangles.end(),{offset,offset+1,offset+2,offset,offset+2,offset+3});
        else result.triangles.insert(result.triangles.end(),{offset,offset+2,offset+1,offset,offset+3,offset+2});
        result.triangle_references.insert(result.triangle_references.end(),{cap,cap});
        kernel::ViewerEdge edge;edge.reference={owner,"generated:skeleton-edge:"+key,{}};
        edge.points={a,b};edge.edge_treatment_side_references={skin,cap};
        edge.edge_treatment_endpoint_references={{owner,"generated:skeleton-point:"+first,{}},{owner,"generated:skeleton-point:"+last,{}}};
        result.points.push_back({a,edge.edge_treatment_endpoint_references[0]});
        result.points.push_back({b,edge.edge_treatment_endpoint_references[1]});
        auto opposite=edge;opposite.reference.semantic_key+=":opposite";
        opposite.points={inner_a,inner_b};
        auto opposite_skin=skin;opposite_skin.semantic_key+=":opposite";
        opposite_skin.sheet_role=kernel::SheetFaceRole::SideB;
        auto inner_surface=std::make_shared<kernel::SurfaceGeometry>(*skin.surface);
        if(inner_surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
            inner_surface->radius-=thickness;inner_surface->reversed=true;
        } else {inner_surface->origin=inner_a;inner_surface->axis=mul(skin_normal,-1.);}
        opposite_skin.surface=inner_surface;opposite.edge_treatment_side_references[0]=opposite_skin;
        for(auto& ref:opposite.edge_treatment_endpoint_references)ref.semantic_key+=":opposite";
        result.points.push_back({inner_a,opposite.edge_treatment_endpoint_references[0]});
        result.points.push_back({inner_b,opposite.edge_treatment_endpoint_references[1]});
        result.edges.push_back(std::move(edge));
        result.edges.push_back(std::move(opposite));
    };
    for(const auto& id:created) {
        const auto* feature=part.find_container(id);const auto* sketch=skeleton_sketch(part,id);
        if(!feature||!sketch)throw std::runtime_error("Authored boundary preparation lost a native profile");
        if(feature->feature_kind==document::FeatureKind::Flat) {
            const auto request=document::flat_request(*feature,*sketch,defaults);
            std::vector<V> points;
            if(const auto* polygon=std::get_if<kernel::ExtrusionRequest::PolygonProfile>(&request.outer_profile))points=polygon->vertices;
            else if(const auto* curves=std::get_if<kernel::ExtrusionRequest::CurvedProfile>(&request.outer_profile)) {
                for(const auto& curve:curves->curves) {
                    const auto* line=std::get_if<kernel::ExtrusionRequest::LineCurve>(&curve);
                    if(!line)throw std::runtime_error("Authored boundary preparation currently requires straight Flat perimeter segments");
                    points.push_back(line->start);
                }
            } else throw std::runtime_error("Authored boundary preparation currently requires a polygonal Flat");
            if(points.size()!=request.outer_edge_source_ids.size()||points.size()!=request.outer_vertex_source_ids.size())
                throw std::runtime_error("Authored boundary preparation requires native perimeter identities");
            const auto n=unit(mul(request.direction,-1.));V area{};
            // Translation must not amplify cancellation in the winding test.
            for(std::size_t i=0;i<points.size();++i)area=add(area,cross(sub(points[i],points.front()),sub(points[(i+1)%points.size()],points.front())));
            const double winding=dot(area,n)>0?1.:-1.;
            for(std::size_t i=0;i<points.size();++i) {
                const auto j=(i+1)%points.size();const auto outward=mul(unit(cross(sub(points[j],points[i]),n)),winding);
                section(id,request.outer_edge_source_ids[i],points[i],points[j],add(points[i],request.direction),add(points[j],request.direction),outward,n,
                    request.outer_vertex_source_ids[i],request.outer_vertex_source_ids[j],document::flat_thickness(*feature,defaults));
            }
        } else if(feature->feature_kind==document::FeatureKind::Bend) {
            const auto material=document::bend_material_definition(*feature,*sketch,defaults);
            const double length=material.angle*material.neutral_radius;
            const auto basis=kernel::sheet_material::basis(material,{0,length,0,false});
            // Consume the exact authored terminal section, including corner
            // correction. Interior sweep samples never define this boundary.
            const auto authored=document::bend_request(*feature,*sketch,defaults);
            if(authored.children.size()!=1)throw std::runtime_error("Skeleton Bend has no authored material section");
            const auto* sweep=std::get_if<kernel::Sweep3DRequest>(&authored.children.front());
            if(!sweep||sweep->sections.empty())throw std::runtime_error("Skeleton Bend has no terminal section");
            const auto* profile=std::get_if<kernel::ExtrusionRequest::PolygonProfile>(&sweep->sections.back().profile.outer_profile);
            if(!profile||profile->vertices.size()!=4)throw std::runtime_error("Skeleton Bend terminal section is not a thickness rectangle");
            const auto& ends=profile->vertices;
            const auto segment=std::ranges::find_if(sketch->segments,[](const auto& s){return !s.construction;});
            auto cylinder=std::make_shared<kernel::SurfaceGeometry>();
            cylinder->kind=kernel::SurfaceGeometry::Kind::Cylinder;
            cylinder->origin=sub(material.origin,mul(material.radial,material.radius));
            cylinder->axis=material.along;cylinder->radial=material.radial;cylinder->radius=material.radius;
            section(id,feature->feature_id+":end",ends[0],ends[1],
                ends[3],ends[2],basis[1],basis[2],
                segment->first_point_id+":end",segment->second_point_id+":end",material.thickness,cylinder);
        }
    }
    return result;
}

kernel::ViewerReferenceGeometry skeleton_reference_geometry(const document::PartDocument& part,
        const std::vector<std::string>& created,const kernel::ViewerReferenceGeometry& source) {
    auto result=source;
    document::history_reference_detail::append(result,skeleton_geometry(part,created));
    return result;
}

void settle_skeleton(document::PartDocument& part,const std::vector<std::string>& created,
        const kernel::ViewerReferenceGeometry& source) {
    // A preparation-only dependency walk. No document transaction or body work.
    for(std::size_t pass=0;pass<=created.size();++pass) {
        const auto before=skeleton_geometry(part,created);
        part.resolve_constructions(skeleton_reference_geometry(part,created,source));
        const auto after=skeleton_geometry(part,created);
        if(document::serialize_viewer_reference_geometry(before)==document::serialize_viewer_reference_geometry(after))return;
    }
    throw std::runtime_error("Authored boundary preparation did not converge");
}

void freeze_skeleton(SheetBodyConversion& result) {
    auto& part=result.document;auto graph=skeleton_geometry(part,result.created);
    auto* conversion=part.find_container(result.container_id);
    const auto rename=[&](auto& ref) {
        ref.semantic_key="generated:sheet-boundary:"+ref.owner_id+":"+ref.semantic_key;
        ref.owner_id=result.container_id;
    };
    for(auto& binding:conversion->sheet_from_body.child_bindings)if(!binding.root&&!binding.detached) {
        auto* feature=part.find_container(binding.feature_id);
        binding.ordinary_attachment=feature->placement.references; // Private until exact native reconciliation below.
        binding.parent_feature_id=feature->placement.references.front().owner_id;
        for(auto& ref:feature->placement.references)rename(ref);
        for(auto& ref:skeleton_sketch(part,feature->id)->external_references) {
            if(std::ranges::find(result.created,ref.source_owner_id)==result.created.end())continue;
            ref.source_semantic_key="generated:sheet-boundary:"+ref.source_owner_id+":"+ref.source_semantic_key;
            ref.source_owner_id=result.container_id;
        }
    }
    for(auto& face:graph.triangle_references)rename(face);
    for(auto& edge:graph.edges) {
        rename(edge.reference);for(auto& point:edge.edge_treatment_endpoint_references)rename(point);
        for(auto& face:edge.edge_treatment_side_references)rename(face);
    }
    for(auto& point:graph.points)rename(point.reference);
    conversion->sheet_from_body.boundary_graph=document::serialize_viewer_reference_geometry(graph).dump();
    conversion->sheet_from_body.boundary_source_inputs=sheet_boundary_source_inputs(part,*conversion);
}

void reconcile_skeleton(SheetBodyConversion& result) {
    auto* conversion=result.document.find_container(result.container_id);
    const auto graph=document::load_viewer_reference_geometry(nlohmann::json::parse(conversion->sheet_from_body.boundary_graph));
    for(auto& binding:conversion->sheet_from_body.child_bindings)if(!binding.root&&!binding.detached) {
        const auto* feature=result.document.find_container(binding.feature_id);
        const auto* expected=document::current_sheet_attachment_edge(feature->placement.references,graph);
        const auto* input=calculated_operation_input(result.document,result.calculated,feature->id);
        if(!expected||!input)throw std::runtime_error("Authored boundary preparation lacks an exact calculated comparison boundary");
        auto source=document::sheet_attachment_reference_geometry(input->mesh);
        document::history_reference_detail::append(source,input->mesh.original_references);
        const kernel::ViewerEdge* best=nullptr;double error=INFINITY;
        for(const auto& edge:source.edges) {
            if(edge.reference.owner_id!=binding.parent_feature_id||edge.points.size()<2)continue;
            try {if(binding.bend)static_cast<void>(document::bend_sheet_references(edge));else static_cast<void>(conversion_flat_references(edge));}catch(const std::exception&){continue;}
            const auto distance=[](V a,V b){const auto d=sub(a,b);return std::sqrt(dot(d,d));};
            const double mismatch=std::min(std::max(distance(edge.points.front(),expected->points.front()),distance(edge.points.back(),expected->points.back())),
                std::max(distance(edge.points.back(),expected->points.front()),distance(edge.points.front(),expected->points.back())));
            if(mismatch<error){error=mismatch;best=&edge;}
        }
        if(!best||error>1e-7) {
            std::map<std::string,std::size_t> owners;std::vector<std::string> keys;
            for(const auto& edge:source.edges){++owners[edge.reference.owner_id];if(edge.reference.owner_id==binding.parent_feature_id)keys.push_back(edge.reference.semantic_key+" sides="+std::to_string(edge.edge_treatment_side_references.size())+" points="+std::to_string(edge.edge_treatment_endpoint_references.size()));}
            throw std::runtime_error("Authored boundary preparation failed exact native attachment reconciliation: "+feature->name+" error="+std::to_string(error)+
                " parent="+binding.parent_feature_id+" owners="+nlohmann::json(owners).dump()+" edges="+nlohmann::json(keys).dump());
        }
        binding.ordinary_attachment=binding.bend?document::bend_sheet_references(*best):conversion_flat_references(*best);
    }
}
