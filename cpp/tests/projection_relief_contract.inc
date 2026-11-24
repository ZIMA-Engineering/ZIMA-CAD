// Exercise the normal material side through the real document transaction,
// including the indirect native cylinder produced by an ordinary Sketch.
void verify_projection_relief() {
    // Interactive document commits defer physical properties; verify the actual
    // native solid instead of treating an unrequested cached total as a volume.
    const auto native_volume=[](const kernel::BodyResult& result) {
        GProp_GProps properties;BRepGProp::VolumeProperties(kernel::read_calculated_body_shape(result),properties);
        return properties.Mass();
    };
    for(bool sphere:{false,true})for(bool polar:{false,true}) {
        if(polar&&!sphere)continue;
        const auto file=std::filesystem::temp_directory_path()/(sphere?"zima-projection-relief-sphere.step":"zima-projection-relief-cone.step");
        const TopoDS_Shape shape=sphere?BRepPrimAPI_MakeSphere(30).Shape():BRepPrimAPI_MakeCone(30,20,40).Shape();
        STEPControl_Writer writer;require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone,"Relief fixture transfer failed");
        require(writer.Write(file.string().c_str())==IFSelect_RetDone,"Relief fixture write failed");
        kernel::OcctKernel engine;kernel::StepRequest imported;imported.source_path=file.string();
        const kernel::HistoryOperation base{"relief-base",imported};
        const auto initial=engine.evaluate_history({base}).back();kernel::FaceReference target;
        for(const auto& face:initial.mesh.triangle_references) {
            const auto data=face.surface?face.surface:face.contour_surface;
            if(data&&data->kind==(sphere?kernel::SurfaceGeometry::Kind::Sphere:kernel::SurfaceGeometry::Kind::Cone))target=face;
        }
        require(target.valid(),"Relief analytic target missing");
        for(bool inward:{false,true}) {
            kernel::ProjectionRequest request;request.sketch_id="relief-profile";
            request.output=kernel::ProjectionResult::Relief;request.height=1.;request.inward=inward;
            request.targets={target};request.both_directions=true;
            request.sketch_origin=polar?kernel::Vec3{0,0,35}:kernel::Vec3{35,0,sphere?0.:20.};
            request.sketch_normal=request.direction=polar?kernel::Vec3{0,0,1}:kernel::Vec3{1,0,0};
            request.regions={{target,polar?kernel::Vec3{.5,.5,std::sqrt(899.5)}:kernel::Vec3{std::sqrt((sphere?900.:625.)-.25),.5,sphere?0.:20.},{}}};
            auto& profile=request.profiles.emplace_back();profile.outer_profile=kernel::ExtrusionRequest::CircleProfile{request.sketch_origin,3};
            profile.direction=request.direction;profile.profile_region_id="disc";profile.outer_boundary_id="circle";
            profile.outer_edge_source_ids={"circle"};
            kernel::HistoryOperation relief{"relief",request};relief.operation=inward?kernel::BooleanOperation::Subtract:kernel::BooleanOperation::Add;
            const auto result=engine.evaluate_history({base,relief}).back();
            for(const auto& [owner,error]:result.calculation_errors)std::cerr<<owner<<": "<<error<<'\n';
            require(result.calculation_errors.empty(),"Analytic relief calculation failed");
            require(BRepCheck_Analyzer(kernel::read_calculated_body_shape(result)).IsValid(),"Analytic relief BRep invalid");
            require(inward?result.volume<initial.volume:result.volume>initial.volume,"Analytic relief material direction is wrong");
            double expected_delta{};const double signed_height=inward?-1.:1.;
            if(sphere) {
                const double area=2*std::numbers::pi*30*(30-std::sqrt(891.));
                expected_delta=area*30/3*(std::pow((30+signed_height)/30,3)-1);
            } else {
                // Independent normal-layer integral over the projected disc
                // y^2+(z-20)^2 <= 9; cone r(z)=30-z/4, Gaussian curvature=0.
                constexpr int radial=100,angular=400;
                for(int i=0;i<radial;++i)for(int j=0;j<angular;++j) {
                    const double rho=3*(i+.5)/radial,angle=2*std::numbers::pi*(j+.5)/angular;
                    const double y=rho*std::cos(angle),z=20+rho*std::sin(angle),r=30-z/4;
                    expected_delta+=(signed_height*std::sqrt(1.0625)*r+.5)/std::sqrt(r*r-y*y)*
                        rho*(3./radial)*(2*std::numbers::pi/angular);
                }
            }
            require(std::abs(result.volume-initial.volume-expected_delta)<.002,
                "Analytic relief disagrees with the independent full-region volume equation");
            // Selecting either side of a parameter seam must retain the same
            // complete physical region, never one arbitrary half of it.
            request.regions.front().seed.y=-request.regions.front().seed.y;
            relief.primitive=request;
            kernel::OcctKernel cold;const auto fresh=cold.evaluate_history({base,relief}).back();
            require(fresh.calculation_errors.empty()&&std::abs(fresh.volume-result.volume)<1e-6,"Analytic relief cold regeneration changed volume");
            std::cout<<"PASS Projection relief sphere="<<sphere<<" polar="<<polar<<" inward="<<inward<<" native validity, side and cold regeneration\n";
        }
        std::filesystem::remove(file);
    }
    for(bool curved:{false,true})for(bool bore:{false,true})for(bool inward:{false,true})for(bool hole:{false,true}) {
        if(curved&&hole)continue;
        const double profile_area=std::numbers::pi*(hole?6.75:9.);
        if(bore&&!curved)continue;
        std::cout<<"Projection relief document curved="<<curved<<" bore="<<bore<<" inward="<<inward<<" hole="<<hole<<'\n';
        kernel::OcctKernel engine;workspace::Workspace live;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        live.add_part(part);live.activate(id);
        auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
        if(curved) {
            static_cast<void>(base_sketch.add_circle(0,0,30));
            if(bore)static_cast<void>(base_sketch.add_circle(0,0,20));
        } else static_cast<void>(base_sketch.add_rectangle(-30,-30,30,30));
        auto base=document::PartDocument::create_feature_container(base_sketch.id);base.feature.sides[0].length=40;
        workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,base_sketch);
        auto* state=live.open_part(id);const auto original=state->session.calculated_boundaries().back();
        const double original_volume=native_volume(original);
        kernel::FaceReference target;
        for(const auto& face:original.mesh.triangle_references) {
            if(curved) {
                const auto s=face.surface?face.surface:face.contour_surface;
                if(s&&s->kind==kernel::SurfaceGeometry::Kind::Cylinder&&std::abs(s->radius-(bore?20:30))<1e-7)target=face;
            } else if(face.owner_id==base.id&&face.semantic_key.find("end:from:")!=std::string::npos)target=face;
        }
        require(target.valid(),"Relief test target missing");
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_circle(0,0,3));
        if(hole)static_cast<void>(sketch.add_circle(0,0,1.5));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.feature.type=document::FeatureType::Projection;
        feature.feature.projection.output=document::ProjectionOutput::Relief;
        feature.feature.projection.height=1.25;
        feature.combine_mode=inward?document::CombineMode::Subtract:document::CombineMode::Add;
        feature.placement.z=curved?20:45;
        if(curved) {feature.placement.x=bore?18:35;feature.placement.rotation_y=90;feature.placement.absolute_rotation_y=90;}
        document::ExtrusionParameters::EndTarget selected;selected.reference=target;
        feature.feature.projection.targets={selected};
        feature.feature.projection.regions={{target,curved?kernel::Vec3{std::sqrt((bore?400.:900.)-.25),.5,20}:kernel::Vec3{hole?2.:0.,0,40},{}}};
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,sketch);
        feature=*state->session.document().find_container(feature.id);
        sketch=*std::ranges::find(state->session.document().sketches,sketch.id,&sketcher::Sketch::id);
        const auto keys=[&](const auto& result) {
            std::set<std::pair<std::string,std::string>> value;
            for(const auto& face:result.mesh.triangle_references)value.emplace(face.owner_id,face.semantic_key);
            for(const auto& edge:result.mesh.edges)value.emplace(edge.reference.owner_id,edge.reference.semantic_key);
            return value;
        };
        for(double height:{1.25,.75,1.5}) {
            feature.feature.projection.height=height;
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,sketch);
            const auto& result=state->session.calculated_boundaries().back();
            require(result.calculation_errors.empty(),"Relief calculation failed");
            require(BRepCheck_Analyzer(kernel::read_calculated_body_shape(result)).IsValid(),"Relief is not valid native geometry");
            const double volume=native_volume(result);
            require(inward?volume<original_volume:volume>original_volume,"Relief changed the wrong material side");
            if(!curved)require(std::abs(volume-original_volume-(inward?-1.:1.)*profile_area*height)<1e-5,
                "Planar relief disagrees with its independent volume equation");
            const auto revision=state->session.document().serialized();const auto identity=keys(result);
            const auto generation=state->session.data_generation(),calculations=kernel::body_calculation_count.load();
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,sketch);
            require(state->session.data_generation()==generation&&state->session.document().serialized()==revision&&
                kernel::body_calculation_count.load()==calculations,"Unchanged relief OK changed the document or calculated geometry");
            require(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),"Relief Undo/Redo failed");
            require(keys(state->session.calculated_boundaries().back())==identity,"Relief Redo changed reference identities");
        }
        if(!curved) {
            base=*state->session.document().find_container(base.id);
            base_sketch=*std::ranges::find(state->session.document().sketches,base_sketch.id,&sketcher::Sketch::id);
            for(double length:{42.,38.,40.}) {
                base.feature.sides[0].length=length;
                workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Replace,base_sketch);
                const auto& result=state->session.calculated_boundaries().back();
                require(result.calculation_errors.empty()&&std::abs(native_volume(result)-3600*length-
                    (inward?-1.:1.)*profile_area*feature.feature.projection.height)<1e-5,"Relief did not follow its changed target");
                bool region_found=false;
                const auto& refs=result.mesh.original_references;
                for(std::size_t i=0;i<refs.triangle_references.size();++i) {
                    const auto& face=refs.triangle_references[i];
                    if(face.owner_id!=feature.id||face.semantic_key!=result.resolved_projection_regions.front().boundary_key)continue;
                    region_found=true;
                    for(unsigned j=0;j<3;++j)require(std::abs(refs.vertices.at(refs.triangles.at(i*3+j)).z-length)<1e-7,
                        "Relief reused stale target reference geometry");
                }
                require(region_found,"Changed relief target lost its retained region");
            }
        }
        const auto file=std::filesystem::temp_directory_path()/("zima-projection-relief-"+id+".prtz");
        const auto identity=keys(state->session.calculated_boundaries().back());
        state->session.document().save(file,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cached;auto reopened=document::PartDocument::load(file,&cached);
        kernel::OcctKernel cold;const auto fresh=workspace::calculate_part_with_resolved_references(cold,reopened);
        require(fresh.back().calculation_errors.empty()&&keys(fresh.back())==identity,"Relief cold reopen changed geometry or references");
        require(reopened.find_container(feature.id)->feature.projection==feature.feature.projection,"Relief persistence changed its definition");
        std::filesystem::remove(file);
        std::cout<<"PASS Projection relief curved="<<curved<<" bore="<<bore<<" inward="<<inward<<" hole="<<hole<<" edits, volume, no-op, Undo/Redo and cold reopen\n";
    }
}
