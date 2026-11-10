// Optional diagnostic mode for an explicitly supplied COPY of a native Part.
// Each pin is an ordinary solid extrusion calculated without Sheet Cut logic.
// The document and its source geometry are never saved or rewritten here.
void verify_actual_sheet_cut_copy(const std::filesystem::path& path) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;
        settings.templates={std::filesystem::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};
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
            const auto before_target=before.body_outputs.find(operation.body.id);
            const auto after_target=after.body_outputs.find(operation.body.id);
            check(before_target!=before.body_outputs.end()&&
                    after_target!=after.body_outputs.end(),
                "Actual Sheet Cut lost its target Body output");
            check(after_target->second->volume>0&&
                    after_target->second->volume<before_target->second->volume,
                "Actual Sheet Cut did not remove target-Body material");
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
            // A document can show several independent Bodies.  Compare the
            // ordinary solid pin with the exact Body owned by this Sheet Cut;
            // subtracting the pin from the aggregate would deliberately cut
            // unrelated visible Bodies too and would not test feature scope.
            const auto& source_body=before_target->second.get();
            const auto& result_body=after_target->second.get();
            const auto source_pin_start=std::chrono::steady_clock::now();
            const auto cut_source=kernel.subtract_bodies(
                source_body,pin,{},{},1.e-7);
            const double source_pin_seconds=elapsed(source_pin_start);
            const auto result_pin_start=std::chrono::steady_clock::now();
            const auto cut_result=kernel.subtract_bodies(
                result_body,pin,{},{},1.e-7);
            const double result_pin_seconds=elapsed(result_pin_start);
            const double source_intersection=
                source_body.volume-cut_source.volume;
            const double result_intersection=
                result_body.volume-cut_result.volume;
            std::cout<<Json{{"check","actual-sheet-cut-pin"},{"owner",operation.owner_id},
                {"target_body",operation.body.id},
                {"calculation_seconds",seconds},
                {"ordinary_source_pin_seconds",source_pin_seconds},
                {"ordinary_result_pin_seconds",result_pin_seconds},
                {"before_volume_mm3",source_body.volume},
                {"after_volume_mm3",result_body.volume},
                {"pin_volume_mm3",pin.volume},
                {"source_pin_intersection_mm3",source_intersection},
                {"result_pin_intersection_mm3",result_intersection},
                {"boolean_tolerance_mm",1.e-7},{"volume_tolerance_mm3",1.e-5}}.dump()<<'\n';
            check(source_intersection>1.e-5,"Ordinary pin did not intersect the original source body");
            // The verification pin intentionally spans farther than a bounded
            // or Up-To Sheet Cut.  Removing the remainder from the calculated
            // result must therefore equal removing that complete pin from the
            // input; requiring zero remainder would incorrectly turn the
            // feature into Through All (and cut other folded walls).
            near(cut_result.volume,cut_source.volume);
            near(source_intersection-result_intersection,
                source_body.volume-result_body.volume);
            ++cuts;
        } else std::cout<<Json{{"check","actual-source-operation"},{"owner",operation.owner_id},
            {"calculation_seconds",seconds},{"volume_mm3",calculated.back().volume}}.dump()<<'\n';
        boundaries=std::move(calculated);
    }
    check(cuts>0,"Actual-file verification found no Sheet Cut features");
    std::cout<<Json{{"check","actual-sheet-cut-pins-passed"},{"count",cuts}}.dump()<<'\n';
}
