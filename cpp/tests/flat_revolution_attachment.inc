void verify_revolution_attachment(const std::filesystem::path& directory) {
    kernel::OcctKernel kernel;
    const auto plus=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x+b.x,a.y+b.y,a.z+b.z};};
    const auto scale=[](kernel::Vec3 p,double k){return kernel::Vec3{p.x*k,p.y*k,p.z*k};};
    const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto cross=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto unit=[&](kernel::Vec3 p){return scale(p,1/std::sqrt(dot(p,p)));};
    const auto close=[&](kernel::Vec3 a,kernel::Vec3 b){const auto delta=plus(a,scale(b,-1));return dot(delta,delta)<1.e-12;};
    for(bool conical:{false,true})for(bool reverse_axis:{false,true})for(bool reverse_angle:{false,true})
        for(bool at_end:{false,true})for(auto thickness_mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}) {
        auto source=document::PartDocument::create_default();
        auto generator=sketcher::Sketch::create_default();
        auto revolved=document::PartDocument::create_revolution_container(generator.id);
        document::initialize_sheet_revolution(revolved,generator,{});
        revolved.revolution.thickness_override=true;revolved.revolution.thin_thickness=2;
        revolved.revolution.thin_mode=thickness_mode;
        revolved.revolution.direction=reverse_angle?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        revolved.placement.absolute_rotation_x=17;revolved.placement.absolute_rotation_z=23;
        revolved.placement.x=13;revolved.placement.y=-7;revolved.placement.z=11;
        const auto axis=std::ranges::find(generator.segments,revolved.revolution.axis_segment_id,&sketcher::SketchSegment::id);
        check(axis!=generator.segments.end(),"Sheet Revolution attachment test lost its axis");
        auto* first=generator.find_point(axis->first_point_id);auto* last=generator.find_point(axis->second_point_id);
        if(conical)last->y=20;
        if(reverse_axis){std::swap(first->x,last->x);std::swap(first->y,last->y);}
        source.history={revolved};source.sketches={generator};
        auto target=document::PartDocument::create_default();
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=flat.id;flat.flat.sketch_id=outline.id;
        flat.flat.sheet_attachment=true;flat.flat.direction=document::ExtrusionDirection::Reverse;
        target.history={flat};target.sketches={outline};
        std::string edge_key;std::vector<sketcher::SketchExternalReference> previous;
        for(double angle:{35.,180.}) {
            source.history.front().revolution.angle_degrees=angle;source.resolve_constructions();
            const auto result=kernel.evaluate_history(source.kernel_operations()).back();
            auto geometry=result.mesh.original_references;
            const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& value) {
                if(!edge_key.empty())return value.reference.semantic_key==edge_key;
                const bool first_skin=std::ranges::any_of(value.edge_treatment_side_references,[](const auto& face) {
                    return face.sheet_role==kernel::SheetFaceRole::SideA;
                });
                const bool end_cap=std::ranges::any_of(value.edge_treatment_side_references,[&](const auto& face) {
                    return face.sheet_role==kernel::SheetFaceRole::ThicknessFace&&
                        face.semantic_key.starts_with(at_end?"end:from:":"start:from:");
                });
                return first_skin&&end_cap;
            });
            check(edge!=geometry.edges.end(),"Sheet Revolution lost its native Side A End edge");
            edge_key=edge->reference.semantic_key;
            if(target.history.front().placement.references.empty())
                target.history.front().placement.references=document::flat_sheet_references(*edge);
            const auto reference=target.history.front().placement.references[1];
            kernel::Vec3 cap_normal{},cap_centroid{};double area=0;
            for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
                const auto& face=geometry.triangle_references[i];
                if(face.owner_id!=reference.owner_id||face.semantic_key!=reference.semantic_key)continue;
                const auto a=geometry.vertices.at(geometry.triangles.at(3*i));
                const auto b=geometry.vertices.at(geometry.triangles.at(3*i+1));
                const auto c=geometry.vertices.at(geometry.triangles.at(3*i+2));
                const auto normal=cross(plus(b,scale(a,-1)),plus(c,scale(a,-1)));
                const double weight=std::sqrt(dot(normal,normal));
                if(weight<=1.e-14)continue;
                cap_normal=unit(normal);cap_centroid=plus(cap_centroid,scale(plus(plus(a,b),c),weight/3));area+=weight;
            }
            check(area>0,"Sheet Revolution end cap has no persisted triangles");cap_centroid=scale(cap_centroid,1/area);
            const auto origin=edge->points.front(),along=unit(plus(edge->points.back(),scale(origin,-1)));
            const auto delta=plus(cap_centroid,scale(origin,-1));
            const auto into_material=unit(plus(delta,scale(along,-dot(delta,along))));
            for(bool reverse_samples:{false,true}) {
                if(reverse_samples){
                    std::ranges::reverse(edge->points);std::ranges::reverse(edge->edge_treatment_endpoint_references);
                    if(edge->exact_spline){
                        std::ranges::reverse(edge->exact_spline->poles);
                        kernel::reverse_bspline_parameters(edge->exact_spline->knots,edge->exact_spline->weights);
                    }
                }
                target.resolve_constructions(geometry);
                const auto& attached=target.history.front();const auto& profile=target.sketches.front();
                check(attached.placement.reference_valid,"Flat rejected a Sheet Revolution End edge");
                near(attached.flat.thickness,2);
                check(close(profile.resolved_y_axis,cap_normal),"Flat does not leave the Sheet Revolution end tangentially");
                if(!close(scale(profile.resolved_normal,-1),into_material))
                    throw std::runtime_error("Flat thickness points outside the source Sheet Revolution: cone="+std::to_string(conical)+
                        " axis="+std::to_string(reverse_axis)+" reverse="+std::to_string(reverse_angle)+" end="+std::to_string(at_end)+
                        " mode="+std::to_string(static_cast<int>(thickness_mode))+" angle="+std::to_string(angle)+" samples="+std::to_string(reverse_samples));
                check(profile.external_references.size()==2,"Flat lost its two Sheet Revolution endpoint links");
                for(std::size_t i=0;i<2;++i) {
                    const auto& ref=profile.external_references[i];
                    check(!ref.broken&&ref.cached_points.size()==1,"Flat has a broken Sheet Revolution endpoint");
                    const auto point=profile.world_point(ref.cached_points.front()[0],ref.cached_points.front()[1]);
                    check(close(point,edge->points.front())||close(point,edge->points.back()),"Flat endpoint left its source Revolution edge");
                    if(!previous.empty())check(ref.id==previous[i].id&&ref.source_semantic_key==previous[i].source_semantic_key,
                        "Revolution angle or parameter reversal replaced the Flat endpoint identity");
                }
                previous=profile.external_references;
                auto rectangle=profile;rectangle.points.clear();rectangle.segments.clear();rectangle.constraints.clear();rectangle.dimensions.clear();
                const auto a=rectangle.local_point(edge->points.front()),b=rectangle.local_point(edge->points.back());
                static_cast<void>(rectangle.add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),12));
                auto calculated=target;calculated.sketches.front()=rectangle;
                near(kernel.evaluate_history(calculated.kernel_operations()).back().volume,40*12*2);
                target.sketches.front()=std::move(rectangle);
            }
            for(const auto& candidate:geometry.edges) {
                const bool inner=std::ranges::any_of(candidate.edge_treatment_side_references,[](const auto& face) {
                    return face.sheet_role==kernel::SheetFaceRole::SideB;
                });
                if(!inner&&kernel::sheet_edge_role(candidate)!=kernel::SheetEdgeRole::Thickness)continue;
                bool rejected=false;try{static_cast<void>(document::flat_sheet_references(candidate));}catch(const std::exception&){rejected=true;}
                check(rejected,"Flat accepted a Sheet Revolution inner or thickness edge");
            }
            target.save(directory/"revolution-attached-flat.prtz");
            target=document::PartDocument::load(directory/"revolution-attached-flat.prtz");
        }
        source.history.front().revolution.angle_degrees=360;source.resolve_constructions();
        const auto full=kernel.evaluate_history(source.kernel_operations()).back();
        for(const auto& edge:full.mesh.original_references.edges) {
            bool rejected=false;try{static_cast<void>(document::flat_sheet_references(edge));}catch(const std::exception&){rejected=true;}
            check(rejected,"Full Sheet Revolution exposed a nonexistent planar end attachment");
        }
    }
    std::cout<<"Flat/Revolution attachment: cylinder, cone, axis reversal, sweep reversal, end frame, endpoints and persistence passed\n";
}
