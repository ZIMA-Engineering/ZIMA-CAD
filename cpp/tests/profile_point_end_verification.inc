void point_end_targets(const kernel::OcctKernel& kernel,fs::path directory) {
    unsigned count=0;
    for(const std::string result:{"solid","thin","surface"})for(bool reverse:{false,true})for(const std::string extent:{"one_side","symmetric","two_sides"}) {
        Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name","point-end-"+std::to_string(count++)}});
        const auto source=f.run("sketch.create",{{"name","Stop points"},{"plane","XZ"}}).at("sketch").get<std::string>();
        const auto point=f.run("sketch.point.create",{{"sketch",source},{"position",{3.,reverse?7.:-7.}}}).at("point").get<std::string>();
        const auto other=f.run("sketch.point.create",{{"sketch",source},{"position",{4.,reverse?-3.:3.}}}).at("point").get<std::string>();
        const auto profile=f.sketch();f.run("sketch.circle.create",{{"sketch",profile},{"center",{0,0}},{"radius_mm",2}});
        const Json target={{"kind","point"},{"owner",source},{"key","point:"+point}};
        Json args={{"sketch",profile},{"direction",reverse?"reverse":"forward"},{"result_type",result},{"thin_thickness_mm",.5},{"extent",extent},{"end_forward","up_to"},{"targets_forward",Json::array({target})}};
        if(extent=="two_sides"){args["end_reverse"]="up_to";args["targets_reverse"]=Json::array({{{"kind","point"},{"owner",source},{"key","point:"+other}}});}
        const auto id=f.run("extrusion.create",args).at("container").get<std::string>();
        const auto verify=[&](double length) {
            const double back=extent=="symmetric"?length:extent=="two_sides"?3.:0.;
            const auto& body=f.part().session.calculated_boundaries().back();require(body.calculation_errors.empty()&&!body.mesh.vertices.empty(),"Point end target left failed or empty geometry");
            double low=body.mesh.vertices.front().z,high=low;for(const auto& p:body.mesh.vertices){low=std::min(low,p.z);high=std::max(high,p.z);}near(low,reverse?-length:-back);near(high,reverse?back:length);
            if(result=="solid")near(body.volume,4*std::numbers::pi*(length+back));
            if(result=="surface"){near(body.volume,0);near(body.surface_area,4*std::numbers::pi*(length+back));}
            const auto& current=f.doc().find_container(id)->extrusion.end_targets_forward.front();require(current.kind==document::EndTargetKind::Point&&current.reference.owner_id==source&&current.reference.semantic_key=="point:"+point,"Point end identity changed");
        };
        verify(7.);
        for(double value:{9.,6.,8.}) {
            f.run("sketch.point.move",{{"sketch",source},{"point",point},{"position",{3.,reverse?value:-value}}});f.run("regenerate");verify(value);
            f.run("undo");f.run("redo");verify(value);
        }
        f.reject("extrusion.set",{{"container",id},{"targets_forward",Json::array({{{"kind","point"},{"owner",profile},{"key","point:missing"}}})}},"invalid_reference_source");
        f.run("save");std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(f.directory/(f.doc().name+".prtz"),&cache);
        require(reopened.find_container(id)->extrusion.end_targets_forward.front().kind==document::EndTargetKind::Point,"Point end kind lost on native reopen");
        kernel::OcctKernel cold;auto native=reopened;const auto recalculated=workspace::calculate_part_with_resolved_references(cold,native);require(recalculated.back().calculation_errors.empty(),"Point end target failed cold regeneration");
    }
    for(bool symmetric:{false,true}) {
        Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name",symmetric?"point-feature-symmetric":"point-feature-one-side"}});
        const auto source=f.run("sketch.create",{{"name","Stop point"},{"plane","XZ"}}).at("sketch").get<std::string>();
        const auto point=f.run("sketch.point.create",{{"sketch",source},{"position",{3.,-7.}}}).at("point").get<std::string>();
        const auto profile=f.sketch();f.run("sketch.circle.create",{{"sketch",profile},{"center",{0,0}},{"radius_mm",2}});
        auto value=workspace::profile_from_sketch(f.doc(),profile,document::FeatureKind::Feature);
        value.feature.symmetric=symmetric;value.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
        document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Point;target.reference={source,"point:"+point,{}};
        value.feature.sides[0].targets={target};const auto id=value.id;
        workspace::commit_profile(f.live,kernel,f.doc().document_id,value,workspace::ProfileEditMode::TransformSketch);
        for(double length:{7.,9.,6.,8.}) {
            if(length!=7.){f.run("sketch.point.move",{{"sketch",source},{"point",point},{"position",{3.,-length}}});f.run("regenerate");}
            near(f.volume(),4*std::numbers::pi*length*(symmetric?2:1));
            require(f.doc().find_container(id)->feature.sides[0].targets.front().kind==document::EndTargetKind::Point,"Unified Feature lost point target");
            f.run("undo");f.run("redo");near(f.volume(),4*std::numbers::pi*length*(symmetric?2:1));
        }
        f.run("save");auto reopened=document::PartDocument::load(f.directory/(f.doc().name+".prtz"));kernel::OcctKernel cold;
        const auto bodies=workspace::calculate_part_with_resolved_references(cold,reopened);require(bodies.back().calculation_errors.empty(),"Unified Feature point target failed reopen");near(bodies.back().volume,4*std::numbers::pi*8*(symmetric?2:1));
    }
    std::cout<<"Point end targets: "<<count<<" result/direction/extent combinations, repeated source motion, Undo/Redo and cold reopen passed\n";
}
