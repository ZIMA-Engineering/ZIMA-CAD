// Optional diagnostic mode for an explicitly supplied COPY of a native Part.
// Each pin is an ordinary solid extrusion calculated without Sheet Cut logic.
// The document and its source geometry are never saved or rewritten here.
void verify_actual_sheet_cut_copy(const std::filesystem::path& path) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;
        settings.templates={std::filesystem::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"};
        return settings;};
    auto directory=std::filesystem::absolute(path).parent_path();
    command_host::Host host(live,kernel,directory,options);
    run(host,"open",{{"path",std::filesystem::absolute(path).string()}});
    const auto start=std::chrono::steady_clock::now();run(host,"regenerate");
    const auto elapsed=[](auto beginning) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now()-beginning).count();
    };
    std::cout<<Json{{"check","actual-sheet-cut-regeneration"},{"seconds",elapsed(start)}}.dump()<<'\n';
    const auto* state=live.open_part(live.active_document_id());
    check(state!=nullptr,"Actual-file verification requires a Part copy");
    const auto operations=state->session.document().kernel_operations(false,true);
    std::vector<kernel::HistoryOperation> prefix;std::vector<kernel::BodyResult> boundaries;
    std::size_t cuts=0;
    for(const auto& operation:operations) {
        prefix.push_back(operation);
        const auto calculation_start=std::chrono::steady_clock::now();
        auto calculated=kernel.evaluate_history_incremental(prefix,boundaries);
        const double seconds=elapsed(calculation_start);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),
            "Actual-file incremental calculation failed");
        const auto* request=std::get_if<kernel::ExtrusionRequest>(&operation.primitive);
        if(request&&request->sheet_cut) {
            check(!boundaries.empty(),"Actual Sheet Cut has no preceding calculated body");
            const auto& before=boundaries.back();const auto& after=calculated.back();
            check(after.volume>0&&after.volume<before.volume,"Actual Sheet Cut did not remove material");
            for(const auto& face:after.mesh.triangle_references)
                check(face.valid(),"Actual Sheet Cut produced anonymous result topology");
            check(!after.sheet_cuts.empty(),"Actual Sheet Cut lost its persisted material regions");
            auto pin_operation=operation;pin_operation.owner_id="verification-pin:"+operation.owner_id;
            pin_operation.operation=kernel::BooleanOperation::Add;pin_operation.body={};
            pin_operation.sheet_operation=kernel::SheetOperation::None;pin_operation.sheet_thickness=0;
            pin_operation.boolean_tolerance=1.e-7;
            auto& pin_request=std::get<kernel::ExtrusionRequest>(pin_operation.primitive);
            pin_request.sheet_cut=false;pin_request.sheet_cut_clearance=false;
            pin_request.wall.reset();pin_request.surface_result=false;
            pin_request.extent=kernel::ExtrusionRequest::Extent::Blind;
            pin_request.through_all_forward=false;pin_request.through_all_reverse=false;
            pin_request.reverse_limit.reset();pin_request.symmetric_limit=false;
            pin_request.target_face={};pin_request.target_is_datum=false;pin_request.target_surface_triangles.clear();
            const auto direction=pin_request.direction;
            const double length=std::hypot(direction.x,direction.y,direction.z);
            check(length>0,"Actual Sheet Cut has no extrusion direction");
            // The copied case is less than 300 mm across. This independent
            // two-metre prism reaches the complete selected sheet thickness.
            pin_request.direction={2000*direction.x/length,2000*direction.y/length,2000*direction.z/length};
            pin_request.start_offset=-1000;
            const auto pins=kernel.evaluate_history({pin_operation});
            check(!pins.empty()&&pins.back().volume>0,"Ordinary verification prism is empty");
            const auto& pin=pins.back();
            const auto cut_source=kernel.subtract_bodies(before,pin,{},{},1.e-7);
            const auto cut_result=kernel.subtract_bodies(after,pin,{},{},1.e-7);
            const double source_intersection=before.volume-cut_source.volume;
            const double result_intersection=after.volume-cut_result.volume;
            std::cout<<Json{{"check","actual-sheet-cut-pin"},{"owner",operation.owner_id},
                {"calculation_seconds",seconds},{"before_volume_mm3",before.volume},
                {"after_volume_mm3",after.volume},{"pin_volume_mm3",pin.volume},
                {"source_pin_intersection_mm3",source_intersection},
                {"result_pin_intersection_mm3",result_intersection},
                {"boolean_tolerance_mm",1.e-7},{"volume_tolerance_mm3",1.e-5}}.dump()<<'\n';
            check(source_intersection>1.e-5,"Ordinary pin did not intersect the original source body");
            near(cut_result.volume,after.volume);
            ++cuts;
        } else std::cout<<Json{{"check","actual-source-operation"},{"owner",operation.owner_id},
            {"calculation_seconds",seconds},{"volume_mm3",calculated.back().volume}}.dump()<<'\n';
        boundaries=std::move(calculated);
    }
    check(cuts>0,"Actual-file verification found no Sheet Cut features");
    std::cout<<Json{{"check","actual-sheet-cut-pins-passed"},{"count",cuts}}.dump()<<'\n';
}
