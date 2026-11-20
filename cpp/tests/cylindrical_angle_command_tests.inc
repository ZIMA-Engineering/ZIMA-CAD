void cylindrical_angle_commands(const kernel::OcctKernel& kernel,const fs::path& directory) {
    for(const std::string kind:{"point","body","section"}) {
        auto part=document::PartDocument::create_default();
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,10));
        auto stock=document::PartDocument::create_feature_container(sketch.id);stock.feature.sides[0].length=25;
        sketch.owner_container_id=stock.id;part.history={stock};part.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Cylinder");
        graph.insert({document::PartHistoryKind::Feature,stock.id});
        std::string object;
        if(kind=="point") {
            auto point=sketcher::Sketch::create_default();
            auto target=document::PartDocument::create_feature_container(point.id);target.feature.type=document::FeatureType::Point;
            for(auto& side:target.feature.sides)side.operation=document::FeatureSideOperation::None;
            target.placement.x=10;target.placement.z=8;object=target.id;point.owner_container_id=target.id;
            part.sketches.push_back(point);part.history.push_back(target);graph.insert({document::PartHistoryKind::Feature,object});
        } else if(kind=="body") {
            object=graph.create_body("Placed Body");auto value=*graph.find(object);
            value.scope.placement.x=10;value.scope.placement.z=8;graph.update_body(value);
        }
        part.set_body_history(graph);part.resolve_constructions();
        const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        std::optional<kernel::FaceReference> face;
        for(const auto& boundary:calculated)for(const auto& candidate:boundary.mesh.original_references.triangle_references)
            if(candidate.surface&&candidate.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder)face=candidate;
        require(face.has_value(),"Angle command fixture lacks a cylinder");
        const auto file=directory/("angular-"+kind+".prtz");part.save(file,calculated);
        workspace::Workspace live;auto active_directory=directory;command_host::Host host(live,kernel,active_directory,{});
        run(host,"open",{{"path",file.generic_string()}});const auto id=live.active_document_id();
        if(kind=="section")object=run(host,"section.create",{{"path_mm",Json::array({Json::array({-5,0}),Json::array({5,0})})},
            {"placement",{{"x",10},{"z",8}}}}).data.at("object").get<std::string>();
        const auto set_reference=[&](std::size_t index,Json reference,Json extra=Json::object()) {
            extra["object"]=object;extra["index"]=index;extra["reference"]=std::move(reference);
            return run(host,kind=="section"?"section.reference.set":"placement.reference.set",std::move(extra));
        };
        const auto get=[&] {
            return run(host,kind=="section"?"section.get":"placement.get",{{"object",object}}).data;
        };
        const Json cylinder={{"owner",face->owner_id},{"key",face->semantic_key}},
            axial={{"owner",stock.container_origin.id},{"key","origin:plane:xy"}},
            zero={{"owner",stock.container_origin.id},{"key","origin:plane:yz"}};
        set_reference(0,cylinder);set_reference(1,axial,{{"offset_mm",8}});set_reference(2,zero,{{"angle_degrees",30}});
        const auto position=[&](double degrees) {
            const auto p=get().at("placement").get<document::Placement>();
            // Positive angles follow the persisted analytic cylinder axis;
            // this profile's outward cylinder uses the negative Z direction.
            near(p.x,10*std::cos(degrees*std::acos(-1.)/180.));
            near(p.y,face->surface->axis.z*10*std::sin(degrees*std::acos(-1.)/180.));near(p.z,8);
            require(p.references[2].use_angle&&!p.references[2].orientation_drives_rotation,"Command lost angular mode/role");
        };
        position(30);
        const auto edit=[&](double angle) {
            if(kind=="section")run(host,"section.set",{{"object",object},{"placement",{{"reference_offset:2",angle}}}});
            else run(host,"placement.set",{{"object",object},{"values",{{"reference_offset:2",angle}}}});
        };
        for(double angle:{90.,-90.,450.,60.}){edit(angle);position(angle);}
        const auto accepted=get();edit(60);require(get().at("revision")==accepted.at("revision"),"Unchanged angle committed a transaction");
        edit(120);position(120);run(host,"undo");position(60);run(host,"redo");position(120);
        // Replacing the cylinder must evaluate the complete pending pair, even
        // though removing it temporarily leaves Angle without an axis.
        set_reference(0,cylinder);position(120);
        const auto before=get();
        Json invalid={{"object",object},{"index",2},{"reference",axial},{"angle_degrees",20}};
        const auto rejected=host.execute({{"command",kind=="section"?"section.reference.set":"placement.reference.set"},{"arguments",invalid}});
        require(!rejected.ok&&get()==before,"Parallel zero plane changed the document despite rejection");
        invalid["reference"]=zero;invalid["offset_mm"]=2;
        require(!host.execute({{"command",kind=="section"?"section.reference.set":"placement.reference.set"},{"arguments",invalid}}).ok&&get()==before,
            "Mixed length/angle arguments were accepted or partly committed");
        run(host,"save");run(host,"close");run(host,"open",{{"path",file.generic_string()}});position(120);
        require(get().at("placement")==before.at("placement"),"Native angular reopen changed side, frame or mode");
        std::cout<<"Cylindrical angle command "<<kind<<": equations, edits, rejection, cylinder repair, unchanged OK, Undo/Redo and native reopen passed\n";
    }
}
