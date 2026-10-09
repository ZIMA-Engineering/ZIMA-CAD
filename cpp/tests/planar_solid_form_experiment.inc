// Explicit, test-only feasibility probes. Do not alter product FORM behavior.
class ExperimentalArcThickener:public BRepOffsetAPI_MakeThickSolid {
public:
    void calculate(const TopoDS_Shape& source,double thickness) {
        NotDone();myLastUsedAlgo=OffsetAlgo_JOIN;
        myOffsetShape.Initialize(source,thickness,.001,BRepOffset_Skin,false,false,GeomAbs_Arc,true,false);
        myOffsetShape.MakeOffsetShape();
        if(!myOffsetShape.IsDone())return;
        myShape=myOffsetShape.Shape();Done();
    }
};
int corner_surface_form_experiment(const std::filesystem::path& path) {
    const auto definition=document::read_sheet_form_definition(path);
    check(definition.solid_opening_faces.size()==2,"Expected the current solid corner definition");
    auto part=definition.part;const auto shape_id=definition.bodies[1];
    kernel::OcctKernel engine;std::vector<kernel::HistoryOperation> operations;
    for(auto operation:part.kernel_operations())if(operation.body.id==shape_id) {
        operation.body={};operations.push_back(std::move(operation));
    }
    const auto closed=engine.evaluate_history(operations);valid(closed.back());
    auto surface_operations=operations;
    surface_operations.push_back({"corner-surface",kernel::ShellRequest{definition.solid_opening_faces,0.}});
    const auto converted=engine.evaluate_history_incremental(surface_operations,closed);
    check(!converted.empty()&&converted.back().calculation_errors.empty()&&converted.back().volume==0.,"Corner surface conversion failed");
    const auto anchor=converted.back().mesh.triangle_references.front();
    // Preserve the independent native source and show its zero-thickness Shell
    // as an ordinary editable feature in a separate inspection document.
    auto graph=part.body_history;graph.activate(shape_id);part.set_body_history(std::move(graph));
    auto shell=document::PartDocument::create_shell_container(definition.solid_opening_faces);shell.shell.thickness=0.;
    part.insert_history_entry(document::PartHistoryKind::Feature,shell.id);part.history.push_back(shell);
    const auto native=workspace::calculate_part_with_resolved_references(engine,part);
    check(!native.empty()&&native.back().calculation_errors.empty(),"Native corner surface preparation failed");
    const auto output=std::filesystem::path("Projects/test/CornerGusset90-surface-experiment.prtz");
    std::filesystem::create_directories(output.parent_path());part.save(output,native);
    std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(output,&saved);
    check(reopened.history.size()==part.history.size()&&!saved.empty(),"Corner source lost history");
    for(const auto& entry:part.history) {
        const auto found=std::ranges::find(reopened.history,entry.id,&document::HistoryContainer::id);
        check(found!=reopened.history.end()&&*found==entry,"Corner surface native roundtrip changed a history entry");
    }
    near(saved.back().body_outputs.at(shape_id)->surface_area,native.back().body_outputs.at(shape_id)->surface_area);
    nlohmann::json report=nlohmann::json::array();
    for(double thickness:{.5,1.,2.,3.,4.})for(const auto side:{kernel::SurfaceThicknessSide::First,kernel::SurfaceThicknessSide::Second}) {
        nlohmann::json row{{"stock_thickness",thickness},{"wall_thickness",.5*thickness},{"side",side==kernel::SurfaceThicknessSide::First?"first":"second"}};
        try {
            auto shell_operations=operations;
            shell_operations.push_back({"corner-solid-shell",kernel::ShellRequest{definition.solid_opening_faces,.5*thickness}});
            const auto solid_start=std::chrono::steady_clock::now();
            const auto direct=engine.evaluate_history_incremental(shell_operations,closed);
            row["solid_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-solid_start).count();valid(direct.back());
            auto thick_operations=surface_operations;
            thick_operations.push_back({"corner-surface-thicken",kernel::SurfaceThickenRequest{anchor,.5*thickness,side,.001}});
            const auto surface_start=std::chrono::steady_clock::now();
            const auto thick=engine.evaluate_history_incremental(thick_operations,converted);
            row["surface_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-surface_start).count();valid(thick.back());
            row["solid_volume"]=direct.back().volume;row["surface_volume"]=thick.back().volume;
            near(thick.back().volume,direct.back().volume);near(thick.back().surface_area,direct.back().surface_area);
            TopoDS_Shape a,b;BRep_Builder builder;std::istringstream a_data(direct.back().kernel_shape),b_data(thick.back().kernel_shape);
            BRepTools::Read(a,a_data,builder);BRepTools::Read(b,b_data,builder);
            BRepAlgoAPI_Cut difference(a,b);difference.Build();check(difference.IsDone(),"Corner comparison cut failed");
            GProp_GProps outside;BRepGProp::VolumePropertiesGK(difference.Shape(),outside,1e-12,false,true);
            near(outside.Mass(),0.);
            BRepAlgoAPI_Cut reverse(b,a);reverse.Build();check(reverse.IsDone(),"Reverse corner comparison failed");
            GProp_GProps missing;BRepGProp::VolumePropertiesGK(reverse.Shape(),missing,1e-12,false,true);near(missing.Mass(),0.);
            row["equivalent_wall"]=true;row["volume"]=thick.back().volume;
        }catch(const std::exception& error){row["error"]=error.what();}
        std::cout<<"Corner surface probe "<<row.dump()<<std::endl;report.push_back(row);
        std::ofstream report_file("build/form-diagnostic/corner-surface-report.json");report_file<<report.dump(2);
    }
    // Isolate join policy without changing the shared surface operation. This
    // comparison has no native ancestry and cannot qualify a product feature.
    for(double thickness:{.5,1.,2.,3.,4.}) {
        nlohmann::json row{{"stock_thickness",thickness},{"route","raw-arc-join"}};
        try {
            auto direct_operations=operations;
            direct_operations.push_back({"corner-direct",kernel::ShellRequest{definition.solid_opening_faces,.5*thickness}});
            const auto direct=engine.evaluate_history_incremental(direct_operations,closed);valid(direct.back());
            TopoDS_Shape converted_shape;BRep_Builder builder;std::istringstream data(converted.back().kernel_shape);
            BRepTools::Read(converted_shape,data,builder);TopExp_Explorer shells(converted_shape,TopAbs_SHELL);
            check(shells.More(),"Converted corner has no shell");
            const auto start=std::chrono::steady_clock::now();ExperimentalArcThickener thick;
            thick.calculate(shells.Current(),-.5*thickness);
            row["surface_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            check(thick.IsDone()&&BRepCheck_Analyzer(thick.Shape()).IsValid(),"Arc-joined surface is invalid");
            TopTools_IndexedMapOfShape solids;TopExp::MapShapes(thick.Shape(),TopAbs_SOLID,solids);
            check(solids.Extent()==1,"Arc surface did not produce one solid");
            GProp_GProps volume;BRepGProp::VolumePropertiesGK(thick.Shape(),volume,1e-12,false,true);
            row["solid_volume"]=direct.back().volume;row["surface_volume"]=volume.Mass();
            TopoDS_Shape original,direct_shape;std::istringstream original_data(closed.back().kernel_shape),direct_data(direct.back().kernel_shape);
            BRepTools::Read(original,original_data,builder);BRepTools::Read(direct_shape,direct_data,builder);
            const auto trim_start=std::chrono::steady_clock::now();BRepAlgoAPI_Common trimmed(thick.Shape(),original);
            trimmed.Build();row["trim_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-trim_start).count();
            check(trimmed.IsDone()&&BRepCheck_Analyzer(trimmed.Shape()).IsValid(),"Surface trim failed exact validity");
            TopTools_IndexedMapOfShape trimmed_solids;TopExp::MapShapes(trimmed.Shape(),TopAbs_SOLID,trimmed_solids);
            check(trimmed_solids.Extent()==1,"Trimmed surface did not produce one solid");
            GProp_GProps trimmed_volume,trimmed_area;
            BRepGProp::VolumePropertiesGK(trimmed.Shape(),trimmed_volume,1e-12,false,true);
            BRepGProp::SurfaceProperties(trimmed.Shape(),trimmed_area);
            row["trimmed_volume"]=trimmed_volume.Mass();near(trimmed_volume.Mass(),direct.back().volume);
            near(trimmed_area.Mass(),direct.back().surface_area);
            for(const bool reverse:{false,true}) {
                BRepAlgoAPI_Cut difference(reverse?direct_shape:trimmed.Shape(),reverse?trimmed.Shape():direct_shape);difference.Build();
                check(difference.IsDone(),"Trimmed surface comparison failed");GProp_GProps residual;
                BRepGProp::VolumePropertiesGK(difference.Shape(),residual,1e-12,false,true);near(residual.Mass(),0.);
            }
            row["equivalent_trimmed_geometry"]=true;
        }catch(const std::exception& error){row["error"]=error.what();}
        std::cout<<"Corner arc probe "<<row.dump()<<std::endl;report.push_back(row);
    }
    std::ofstream report_file("build/form-diagnostic/corner-surface-report.json");report_file<<report.dump(2);
    return 0;
}

int planar_solid_form_experiment(const std::filesystem::path& path) {
    auto part=document::PartDocument::load(path);
    kernel::OcctKernel engine;
    std::vector<kernel::HistoryOperation> operations;
    for(auto operation:part.kernel_operations()) {operation.body={};operations.push_back(std::move(operation));}
    const auto calculated=engine.evaluate_history(operations);
    check(!calculated.empty(),"Missing solid tool");valid(calculated.back());
    auto definition=part;
    auto graph=definition.body_history;
    const auto cut_body=document::create_origin_bound_body(graph,definition.document_id,"FORM_CUT");
    static_cast<void>(document::create_origin_bound_body(graph,definition.document_id,"FORM_FLAT"));
    const auto symbol_body=document::create_origin_bound_body(graph,definition.document_id,"FORM_SYMBOL");
    definition.set_body_history(std::move(graph));
    // The authored line selects a cutting wall by its actual support plane.
    // It never acts as a zero-width Boolean tool.
    for(const auto& body:{cut_body,symbol_body}) {
        auto active=definition.body_history;active.activate(body);definition.set_body_history(std::move(active));
        auto container=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=container.id;
        static_cast<void>(sketch.add_segment(-50,0,50,0));
        definition.insert_history_entry(document::PartHistoryKind::Feature,container.id);
        definition.history.push_back(container);definition.sketches.push_back(sketch);
    }
    auto active=definition.body_history;active.activate(cut_body);definition.set_body_history(std::move(active));
    definition.name="20-FORM solid planar experiment";
    const auto native=workspace::calculate_part_with_resolved_references(engine,definition);
    check(!native.empty()&&native.back().calculation_errors.empty(),"Experimental native definition failed");
    const auto asset=std::filesystem::path("Projects/test/20-FORM-solid-planar-experiment.prtz");
    std::filesystem::create_directories(asset.parent_path());definition.save(asset,native);
    std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(asset,&saved);
    check(reopened.history==definition.history&&reopened.sketches.size()==definition.sketches.size(),"Experimental native source roundtrip changed definitions");
    const auto cut_sketch=std::ranges::find_if(reopened.sketches,[&](const auto& sketch){
        const auto* owner=reopened.body_history.owner(sketch.owner_container_id);return owner&&owner->scope.id==cut_body;
    });
    check(cut_sketch!=reopened.sketches.end()&&cut_sketch->segments.size()==1,"Missing persisted cutting line");
    const auto* first=cut_sketch->find_point(cut_sketch->segments.front().first_point_id);
    const auto* last=cut_sketch->find_point(cut_sketch->segments.front().second_point_id);
    check(first&&last,"Missing cutting line endpoints");near(first->x,-50);near(last->x,50);near(first->y,0);near(last->y,0);
    std::map<std::pair<std::string,std::string>,kernel::FaceReference> planes;
    for(const auto& face:calculated.back().mesh.triangle_references)
        if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane)
            planes.emplace(std::pair{face.owner_id,face.semantic_key},face);
    for(const auto& [key,face]:planes) {
        const auto& p=*face.surface;
        std::cout<<"Tool plane origin="<<p.origin.x<<','<<p.origin.y<<','<<p.origin.z
            <<" normal="<<p.axis.x<<','<<p.axis.y<<','<<p.axis.z<<" reversed="<<p.reversed<<std::endl;
    }
    nlohmann::json report=nlohmann::json::array();
    for(int mode:{0,1,2,3})for(bool cut:{false,true})for(double thickness:{.5,1.,2.}) {
        auto input_operations=operations;
        if(mode>=2)std::erase_if(input_operations,[](const auto& operation){return std::holds_alternative<kernel::FilletRequest>(operation.primitive);});
        const auto input=mode>=2?engine.evaluate_history(input_operations):calculated;
        std::map<std::pair<std::string,std::string>,kernel::FaceReference> current_planes;
        for(const auto& face:input.back().mesh.triangle_references)
            if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane)
                current_planes.emplace(std::pair{face.owner_id,face.semantic_key},face);
        kernel::ShellRequest shell;shell.thickness=thickness;
        for(const auto& [key,face]:current_planes)if(std::abs(face.surface->axis.z)>1.-1e-10&&
            std::abs(face.surface->origin.z)<1e-8)shell.removed_faces.push_back(face);
        check(shell.removed_faces.size()==1,"Expected one planar base closing face");
        if(cut) {
            const auto& base=shell.removed_faces.front();
            for(const auto& [key,face]:current_planes) {
                if(face==base)continue;
                const auto distance=[&](double x,double y) {
                    return kernel::sheet_material::dot(kernel::sheet_material::sub(kernel::Vec3{x,y,0},face.surface->origin),face.surface->axis);
                };
                if(std::abs(distance(first->x,first->y))<1e-8&&std::abs(distance(last->x,last->y))<1e-8)
                    shell.removed_faces.push_back(face);
            }
            check(shell.removed_faces.size()==2,"Cutting line needs exactly one supported wall");
        }
        auto probe=input_operations;
        if(mode==1)shell.thickness=0.;
        probe.push_back({"planar-solid-shell",shell});
        const auto start=std::chrono::steady_clock::now();
        nlohmann::json row{{"mode",mode},{"cut",cut},{"thickness",thickness}};
        try {
        auto result=engine.evaluate_history_incremental(probe,input);
        if(mode==1) {
            check(result.back().calculation_errors.empty(),"Surface conversion failed");
            const auto anchor=result.back().mesh.triangle_references.front();
            probe.push_back({"planar-solid-thicken",kernel::SurfaceThickenRequest{anchor,thickness,kernel::SurfaceThicknessSide::Second,.001}});
            result=engine.evaluate_history_incremental(probe,result);
        }
        if(mode==3) {
            // Separate discovery direction: preserve authored R5 requests,
            // but execute them after hollowing. This is not assumed to retain
            // uniform thickness or equivalent inner transitions.
            for(const auto& operation:operations)if(std::holds_alternative<kernel::FilletRequest>(operation.primitive))probe.push_back(operation);
            result=engine.evaluate_history_incremental(probe,result);
        }
        row["seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"Planar solid mode="<<mode<<" cut="<<cut<<" t="<<thickness<<" seconds="
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        valid(result.back());
        row["valid"]=true;row["volume"]=result.back().volume;
        TopoDS_Shape tool,wall;BRep_Builder builder;
        std::istringstream tool_data(input.back().kernel_shape),wall_data(result.back().kernel_shape);
        BRepTools::Read(tool,tool_data,builder);BRepTools::Read(wall,wall_data,builder);
        // Outer-side attachment: the tool base outward normal (-Z) maps to
        // the selected sheet outward normal (+Z). The stock occupies -t..0.
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),std::numbers::pi);
        tool=BRepBuilderAPI_Transform(tool,transform,false).Shape();
        wall=BRepBuilderAPI_Transform(wall,transform,false).Shape();
        auto sheet=document::PartDocument::create_default();
        static_cast<void>(document::create_origin_bound_body(sheet.body_history,sheet.document_id,"Sheet"));
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        flat.flat.direction=document::ExtrusionDirection::Reverse;flat.flat.thickness_override=true;flat.flat.thickness=thickness;
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=flat.id;
        static_cast<void>(outline.add_rectangle(-100,-100,100,100));flat.flat.sketch_id=outline.id;
        sheet.insert_history_entry(document::PartHistoryKind::Feature,flat.id);sheet.history.push_back(flat);sheet.sketches.push_back(outline);
        kernel::OcctKernel stock_engine;const auto stock=stock_engine.evaluate_history(sheet.kernel_operations());
        TopoDS_Shape stock_shape;std::istringstream stock_data(stock.back().kernel_shape);BRepTools::Read(stock_shape,stock_data,builder);
        const auto boolean_start=std::chrono::steady_clock::now();
        BRepAlgoAPI_Cut subtract;subtract.SetArguments(TopTools_ListOfShape{stock_shape});subtract.SetTools(TopTools_ListOfShape{tool});
        subtract.SetNonDestructive(true);subtract.SetFuzzyValue(.001);subtract.Build();check(subtract.IsDone(),"Experimental sheet cut failed");
        BRepAlgoAPI_Fuse fuse;fuse.SetArguments(TopTools_ListOfShape{subtract.Shape()});fuse.SetTools(TopTools_ListOfShape{wall});
        fuse.SetNonDestructive(true);fuse.SetFuzzyValue(.001);fuse.Build();check(fuse.IsDone(),"Experimental sheet fuse failed");
        row["boolean_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-boolean_start).count();
        check(BRepCheck_Analyzer(fuse.Shape(),true,false,true).IsValid(),"Experimental insertion has invalid exact BRep");
        TopTools_IndexedMapOfShape solids;TopExp::MapShapes(fuse.Shape(),TopAbs_SOLID,solids);
        check(solids.Extent()==1,"Experimental insertion is disconnected");
        GProp_GProps mass;const auto error=BRepGProp::VolumePropertiesGK(fuse.Shape(),mass,1e-12,false,true);
        check(std::isfinite(error)&&error>=0.&&mass.Mass()>0.,"Experimental insertion independent volume failed");
        row["inserted"]=true;row["inserted_volume"]=mass.Mass();
        const auto output=std::filesystem::path("build/form-diagnostic")/("planar-solid-mode-"+std::to_string(mode)+"-cut-"+std::to_string(cut)+"-t-"+std::to_string(thickness)+".brep");
        BRepTools::Write(fuse.Shape(),output.string().c_str());
        std::cout<<"Inserted exact connected sheet volume="<<mass.Mass()<<" Boolean seconds="<<row["boolean_seconds"]<<std::endl;
        }catch(const std::exception& error) {
            row["error"]=error.what();row["elapsed_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"Planar solid mode="<<mode<<" cut="<<cut<<" t="<<thickness<<" rejected="<<error.what()
                <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        }
        report.push_back(std::move(row));
        std::ofstream output("build/form-diagnostic/planar-solid-report.json");output<<report.dump(2);
    }
    return 0;
}
