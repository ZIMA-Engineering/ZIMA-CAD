// The joint between two authored cylinder faces must not become a material
// boundary for a relief. Check both sides of that joint in the actual solid.
void verify_relief_cylinder_junction() {
    for(bool sequential:{false,true})for(bool wrapping:{false,true})
    for(bool bore:{false,true})for(bool inward:{false,true})for(bool directional:{false,true}) {
        if(wrapping&&directional)continue;
        std::cout<<"Cylinder junction sequential="<<sequential<<" wrap="<<wrapping
            <<" bore="<<bore<<" inward="<<inward<<" directional="<<directional<<std::endl;
        kernel::OcctKernel engine;workspace::Workspace live;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        part.document_precision["linear_tolerance"]="0.000001";
        live.add_part(part);live.activate(id);
        auto profile=sketcher::Sketch::create_default();profile.plane_auto=false;
        static_cast<void>(profile.add_circle(0,0,30));
        if(bore)static_cast<void>(profile.add_circle(0,0,24));
        auto base=document::PartDocument::create_feature_container(profile.id);
        base.feature.sides[0].length=20;
        if(sequential)base.placement.z=-20;
        else {base.feature.sides[1].operation=document::FeatureSideOperation::Extrusion;base.feature.sides[1].length=20;}
        workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,profile);
        if(sequential) {
            auto next=sketcher::Sketch::create_default();next.plane_auto=false;
            static_cast<void>(next.add_circle(0,0,30));
            if(bore)static_cast<void>(next.add_circle(0,0,24));
            auto extension=document::PartDocument::create_feature_container(next.id);
            extension.feature.sides[0].length=20;
            workspace::commit_profile(live,engine,id,extension,workspace::ProfileEditMode::Create,next);
        }
        auto* state=live.open_part(id);const auto input=state->session.calculated_boundaries().back();
        std::vector<kernel::FaceReference> targets;const double radius=bore?24.:30.;
        for(const auto& face:input.mesh.triangle_references) {
            const auto s=face.surface?face.surface:face.contour_surface;
            if(s&&s->kind==kernel::SurfaceGeometry::Kind::Cylinder&&std::abs(s->radius-radius)<1e-7&&
                    std::ranges::none_of(targets,[&](const auto& f){return f.owner_id==face.owner_id&&f.semantic_key==face.semantic_key;}))targets.push_back(face);
        }
        require(targets.size()==2,"Cylinder junction fixture must retain two bounded cylindrical faces");
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_rectangle(-6,-4,6,4));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.placement.x=radius;feature.placement.rotation_y=90;feature.placement.absolute_rotation_y=90;
        feature.combine_mode=inward?document::CombineMode::Subtract:document::CombineMode::Add;
        feature.feature.type=wrapping?document::FeatureType::Wrapping:document::FeatureType::Projection;
        if(wrapping) {
            auto& p=feature.feature.wrapping;p.output=document::WrappingOutput::Relief;p.height=1.25;
            p.target.emplace();p.target->reference=targets.front();
        } else {
            auto& p=feature.feature.projection;p.output=document::ProjectionOutput::Relief;p.height=1.25;
            p.relief_direction=directional?document::ProjectionReliefDirection::Projection:document::ProjectionReliefDirection::SurfaceNormal;
            document::ExtrusionParameters::EndTarget t;t.reference=targets.front();p.targets.push_back(t);
        }
        const auto start=std::chrono::steady_clock::now();
        std::cout<<"Junction fixture ready; calculating relief"<<std::endl;
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,sketch);
        const auto verify=[&](const kernel::BodyResult& result) {
            require(result.calculation_errors.empty(),"Cylinder junction relief remained unresolved");
            const auto shape=kernel::read_calculated_body_shape(result);
            require(BRepCheck_Analyzer(shape).IsValid(),"Cylinder junction relief is invalid");
            require(!TopExp_Explorer(shape,TopAbs_FACE,TopAbs_SOLID).More(),"Relief left a loose face inside the Sketch");
            GProp_GProps before,after;
            BRepGProp::VolumeProperties(kernel::read_calculated_body_shape(input),before,1e-10);
            BRepGProp::VolumeProperties(shape,after,1e-10);
            const double radial_sign=(bore?-1.:1.)*(inward?-1.:1.),h=1.25;
            const double angle=wrapping?8/radius:2*std::asin(4/radius);
            const double expected=(inward?-1.:1.)*(directional?96*h:12*angle*(radius*h+radial_sign*h*h/2));
            if(std::abs(after.Mass()-before.Mass()-expected)>=.002)
                std::cerr<<"Junction volume delta="<<after.Mass()-before.Mass()<<" expected="<<expected<<'\n';
            require(std::abs(after.Mass()-before.Mass()-expected)<.002,"Cylinder junction relief omitted or added material outside the complete Sketch");
            for(double z:{-5.,-1.,0.,1.,5.})for(double offset:{.3,.9})for(double y:{-2.,0.,2.}) {
                const double r=radius+radial_sign*offset;
                const double x=std::sqrt(radius*radius-y*y);
                const gp_Pnt sample=directional?gp_Pnt(x+radial_sign*offset,y,z):gp_Pnt(x*r/radius,y*r/radius,z);
                BRepClass3d_SolidClassifier material(shape,sample,1e-7);
                if(material.State()!=(inward?TopAbs_OUT:TopAbs_IN)) {
                    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                    std::cerr<<"Unexpected material state="<<int(material.State())<<" r="<<r<<" z="<<z<<" solids="<<solids<<'\n';
                }
                require(material.State()==(inward?TopAbs_OUT:TopAbs_IN),"A material patch remains or is missing across the cylinder junction");
            }
            for(double z:{-8.,8.}) {
                BRepClass3d_SolidClassifier material(shape,gp_Pnt(radius+(bore?1.:-1.)*.5,0,z),1e-7);
                require(material.State()==TopAbs_IN,"Relief changed material outside the Sketch");
            }
            if(!wrapping) {
                const auto calls=kernel::body_calculation_count.load();
                const auto& doc=state->session.document();
                bool incomplete=false;
                const auto preview=doc.projection_preview_mesh(*doc.find_container(feature.id),input.mesh,&doc,&incomplete);
                if(incomplete) {
                    for(const auto& target:targets)std::cerr<<"Target "<<target.owner_id<<" "<<target.semantic_key<<'\n';
                    for(const auto& edge:input.mesh.edges)if(edge.edge_treatment_side_references.size()==2) {
                        std::cerr<<"Adjacency";
                        for(const auto& side:edge.edge_treatment_side_references)std::cerr<<" ["<<side.owner_id<<" "<<side.semantic_key<<"]";
                        std::cerr<<'\n';
                    }
                }
                require(!incomplete,"Single-face continuation preview omitted the adjoining cylinder face");
                unsigned walls=0,source_edges=0;
                for(const auto& edge:preview.edges) {
                    const bool source=edge.reference.semantic_key.starts_with("projection:preview:source:");
                    require(edge.color==(source?"#FFFFFF":"#00D1FF"),"Relief preview lost white source or azure material");
                    if(source)++source_edges;
                    if(edge.reference.semantic_key!="projection:preview:wall")continue;
                    ++walls;const auto a=edge.points.front(),b=edge.points.back();
                    require(std::abs(std::hypot(std::hypot(b.x-a.x,b.y-a.y),b.z-a.z)-h)<1e-8,"Relief preview depth is incorrect");
                    require(std::abs(b.z-a.z)<1e-8&&(directional?std::abs(b.y-a.y)<1e-8:std::abs(b.y-a.y)>.01),
                        "Relief preview does not follow the selected direction");
                }
                require(walls&&source_edges&&kernel::body_calculation_count.load()==calls,"Relief preview missing or calculated geometry");
            }
        };
        verify(state->session.calculated_boundaries().back());
        std::cout<<"Junction changed OK ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        feature=*state->session.document().find_container(feature.id);
        if(!wrapping) {
            document::ExtrusionParameters::EndTarget duplicate;duplicate.reference=targets.back();
            feature.feature.projection.targets.push_back(duplicate);
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace);
            require(state->session.document().find_container(feature.id)->feature.projection.targets.size()==2,
                "Redundant same-surface selection was removed from the authored definition");
            verify(state->session.calculated_boundaries().back());
            std::ranges::reverse(feature.feature.projection.targets);
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace);
            verify(state->session.calculated_boundaries().back());
        }
        const auto revision=state->session.document().serialized();const auto calls=kernel::body_calculation_count.load();
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace);
        require(state->session.document().serialized()==revision&&kernel::body_calculation_count.load()==calls,"Unchanged junction relief calculated or changed the document");
        if(!wrapping)for(int switch_back=0;switch_back<2;++switch_back) {
            directional=!directional;
            feature.feature.projection.relief_direction=directional?document::ProjectionReliefDirection::Projection:document::ProjectionReliefDirection::SurfaceNormal;
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace);
            verify(state->session.calculated_boundaries().back());
        }
        require(state->session.undo()&&state->session.redo(),"Junction relief Undo/Redo failed");verify(state->session.calculated_boundaries().back());
        const auto file=std::filesystem::temp_directory_path()/("zima-relief-junction-"+id+".prtz");
        state->session.document().save(file,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cached;auto reopened=document::PartDocument::load(file,&cached);verify(cached.back());
        {
            // Compact files share reference metadata at the final boundary.
            // Exercise the same rollback input assembly as Properties.
            document::DocumentSession restored(reopened,cached);
            const auto boundary=restored.rollback_boundary(feature.id);
            require(boundary&&boundary->input_body,"Reopened relief input is unavailable");
            auto selected=*reopened.find_container(feature.id);
            if(!wrapping)selected.feature.projection.targets.resize(1);
            bool incomplete=false;const auto calls=kernel::body_calculation_count.load();
            const auto preview=wrapping?reopened.wrapping_preview_mesh(selected,boundary->input_body->mesh,&reopened):
                reopened.projection_preview_mesh(selected,boundary->input_body->mesh,&reopened,&incomplete);
            require(!incomplete&&!preview.edges.empty()&&kernel::body_calculation_count.load()==calls,
                "Native reload lost hidden support continuation in the calculation-free preview");
            double lower=0,upper=0;
            for(const auto& edge:preview.edges)if(!edge.reference.semantic_key.starts_with("projection:preview:source:"))
                for(const auto& point:edge.points){lower=std::min(lower,point.z);upper=std::max(upper,point.z);}
            require(lower< -5.99&&upper>5.99,"Reopened relief preview omitted one side of the cylinder junction");
        }
        kernel::OcctKernel cold;const auto fresh=workspace::calculate_part_with_resolved_references(cold,reopened);verify(fresh.back());
        std::filesystem::remove(file);
    }
    // Equal radius alone does not authorize crossing a real gap. A connected
    // cylindrical step is also not a continuation of the selected support.
    for(bool gap:{false,true}) {
        kernel::OcctKernel engine;workspace::Workspace live;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        live.add_part(part);live.activate(id);
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_circle(0,0,30));
        auto base=document::PartDocument::create_feature_container(sketch.id);
        base.placement.z=-20;base.feature.sides[0].length=20;
        workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,sketch);
        auto* state=live.open_part(id);kernel::FaceReference target;
        for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references)
            if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder)target=face;
        require(target.valid(),"Wrap coverage limit fixture has no cylinder");
        auto next=sketcher::Sketch::create_default();next.plane_auto=false;
        static_cast<void>(next.add_circle(0,0,gap?30.:29.));
        auto extension=document::PartDocument::create_feature_container(next.id);
        extension.placement.z=gap?2.:0.;extension.feature.sides[0].length=20;
        workspace::commit_profile(live,engine,id,extension,workspace::ProfileEditMode::Create,next);
        auto source=sketcher::Sketch::create_default();source.plane_auto=false;
        static_cast<void>(source.add_rectangle(-6,-4,6,4));
        auto wrap=document::PartDocument::create_feature_container(source.id);
        wrap.placement.x=30;wrap.placement.rotation_y=90;wrap.placement.absolute_rotation_y=90;
        wrap.feature.type=document::FeatureType::Wrapping;
        wrap.feature.wrapping.output=document::WrappingOutput::Relief;
        wrap.feature.wrapping.target.emplace();wrap.feature.wrapping.target->reference=target;
        const auto before=state->session.document().serialized();const auto generation=state->session.data_generation();
        bool rejected=false;
        try {workspace::commit_profile(live,engine,id,wrap,workspace::ProfileEditMode::Create,source);}
        catch(const std::exception&){rejected=true;}
        require(rejected&&state->session.document().serialized()==before&&state->session.data_generation()==generation,
            "Wrap crossed a physical gap or another cylindrical radius, or changed the rejected document");
        std::cout<<"Wrap coverage limit gap="<<gap<<" passed\n";
        for(bool projection:{false,true}) {
            const auto pending_before=state->session.document().serialized();
            if(projection) {
                wrap.feature.type=document::FeatureType::Projection;
                wrap.feature.projection.output=document::ProjectionOutput::Relief;
                document::ExtrusionParameters::EndTarget selected;selected.reference=target;
                wrap.feature.projection.targets={selected};
            }
            workspace::ModelingConfirmation confirmation;
            for(bool first:{true,false}) {
                const auto calls=kernel::body_calculation_count.load();bool failed=false;
                workspace::ModelingConfirmationAttempt attempt(confirmation,live,id);
                try {workspace::commit_profile(live,engine,id,wrap,workspace::ProfileEditMode::Create,source,workspace::ProfileFailurePolicy::KeepDefinition);}
                catch(const std::exception&){failed=true;}
                require(attempt.failed()==first&&failed==first,"Relief geometric failure did not use two-step confirmation");
                if(first)require(state->session.document().serialized()==pending_before,"First geometric failure changed the document");
                else require(kernel::body_calculation_count.load()==calls&&state->session.document().find_container(wrap.id)&&
                    state->session.calculated_boundaries().back().calculation_errors.contains(wrap.id),
                    "Second OK discarded or recalculated the failed relief");
            }
            const auto file=std::filesystem::temp_directory_path()/("zima-relief-failed-"+id+".prtz");
            state->session.document().save(file,state->session.calculated_boundaries());
            std::vector<kernel::BodyResult> cached;const auto reopened=document::PartDocument::load(file,&cached);
            require(reopened.find_container(wrap.id)&&cached.back().calculation_errors.contains(wrap.id),"Native reopen lost the failed relief");
            require(state->session.undo()&&state->session.redo()&&state->session.document().find_container(wrap.id)&&
                state->session.calculated_boundaries().back().calculation_errors.contains(wrap.id),"Undo/Redo lost the failed relief");
            require(state->session.undo(),"Cannot restore input after the failed relief");std::filesystem::remove(file);
            std::cout<<"Geometric failure retained after second OK projection="<<projection<<" passed\n";
        }
    }
}
