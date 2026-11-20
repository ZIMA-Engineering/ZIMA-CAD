// Included after the conversion association helpers. Creation and Properties
// share the same authoring path; source-region identities retain native child
// identity and authored Bend output corrections across an explicit edit.
void stage_unresolved_sheet_from_body(document::PartDocument& document,
        document::HistoryContainer marker,const Face& selected,double thickness,
        bool thickness_override,const std::vector<Face>& included,
        const std::vector<std::string>& rips) {
    const auto* target=document.body_history.find(document.body_history.active_body_id());
    const auto* source=document.body_owner_for_object(selected.owner_id);
    if(!target||target->derived_copy||!source||!selected.surface||
            selected.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)
        throw std::invalid_argument("Select a calculated planar source face.");
    auto* existing=document.find_container(marker.id);
    if(existing)marker=*existing;
    auto& definition=marker.sheet_from_body;
    definition.source_body_id=source->scope.id;
    definition.replaces_body=source==target;
    const auto target_id=target->scope.id;
    const auto source_id=source->scope.id;
    const bool replaces_body=definition.replaces_body;
    definition.thickness=thickness_override?thickness:document::sheet_metal_defaults(document).thickness_mm.value_or(1.);
    definition.thickness_override=thickness_override;
    const auto binding=[](const Face& face) {
        return document::SheetFromBodyFaceBinding{persistent_face_reference(face),
            document::SheetFromBodyFaceRole::Skin,face.surface&&face.surface->reversed?-1:1,surface_signature(face)};
    };
    definition.source_face=binding(selected);definition.included_faces.clear();
    for(const auto& face:included)definition.included_faces.push_back(binding(face));
    definition.rip_sketches=rips;
    for(auto& child:definition.child_bindings)if(!child.detached)child.references_valid=false;
    if(existing)*existing=std::move(marker);
    else {
        document.insert_history_entry(document::PartHistoryKind::Feature,marker.id);
        document.history.push_back(std::move(marker));
    }
    document.validate_sheet_from_body();
    if(!replaces_body) {
        auto target_body=*document.body_history.find(target_id);
        if(std::ranges::find(target_body.dependencies,source_id)==target_body.dependencies.end())
            target_body.dependencies.push_back(source_id);
        document.body_history.update_body(std::move(target_body));
    }
}

bool stage_sheet_from_body_rips(document::PartDocument& document,
        const std::string& conversion_id,const std::vector<std::string>& selected,
        const std::vector<document::GeneralSurfaceBoundary>& drafts) {
    const auto* active=document.body_history.find(document.body_history.active_body_id());
    if(!active||active->derived_copy)
        throw std::invalid_argument("Sheet from Body requires an editable active Body.");
    const auto body_id=active->scope.id;
    const auto original_cursor=active->cursor;
    const std::string frontier=original_cursor<active->entries.size()?active->entries[original_cursor].id:std::string{};
    bool changed{};
    for(const auto& draft:drafts) {
        if(!draft.sketch_feature||draft.curve)
            throw std::invalid_argument("Invalid Sheet from Body RIP Sketch reference");
        const auto& feature=*draft.sketch_feature;
        auto sketch=sketcher::Sketch::from_serialized(draft.sketch_serialized);
        if(feature.feature_kind!=document::FeatureKind::Feature||feature.feature.type!=document::FeatureType::Sketch||
                feature.feature.sketch_id!=sketch.id||sketch.owner_container_id!=feature.id||
                std::ranges::find(selected,sketch.id)==selected.end())
            throw std::invalid_argument("Invalid Sheet from Body RIP Sketch reference");
        if(auto* existing=document.find_container(feature.id)) {
            const auto* owner=document.body_owner_for_object(feature.id);
            auto* old=owned_sketch(document,feature.id);
            if(!owner||owner->scope.id!=body_id||!old||old->id!=sketch.id)
                throw std::invalid_argument("Invalid Sheet from Body RIP Sketch reference");
            changed|=*existing!=feature||old->serialized()!=sketch.serialized();
            *existing=feature;*old=std::move(sketch);
        } else {
            if(!conversion_id.empty())document.body_history.set_history_cursor(body_id,
                document.body_history.rollback_before(conversion_id).entry_count);
            document.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
            document.history.push_back(feature);document.sketches.push_back(std::move(sketch));changed=true;
        }
    }
    if(auto* parent=document.find_container(conversion_id)) {
        const auto previous=parent->sheet_from_body.rip_sketches;
        parent->sheet_from_body.rip_sketches=selected;
        for(const auto& id:previous)if(std::ranges::find(selected,id)==selected.end()) {
            const auto sketch=std::ranges::find(document.sketches,id,&sketcher::Sketch::id);
            if(sketch==document.sketches.end())continue;
            const auto feature_id=sketch->owner_container_id;
            const auto* feature=document.find_container(feature_id);
            const auto* owner=document.body_owner_for_object(feature_id);
            // An explicitly referenced source profile is not owned material.
            // Only ordinary RIP Sketch features in the conversion Body are removed.
            if(!feature||!owner||owner->scope.id!=body_id||
                    !(feature->feature_kind==document::FeatureKind::Sketch||
                      (feature->feature_kind==document::FeatureKind::Feature&&feature->feature.type==document::FeatureType::Sketch)))continue;
            const auto plan=plan_history_deletion(document,feature_id);
            detach_deleted_history_references(document,plan);
            document.erase_history_object(feature_id);changed=true;
        }
    }
    const auto& entries=document.body_history.find(body_id)->entries;
    const auto cursor=frontier.empty()?entries.end():std::ranges::find(entries,frontier,&document::PartHistoryEntry::id);
    document.body_history.set_history_cursor(body_id,static_cast<std::size_t>(cursor-entries.begin()));
    return changed;
}

SheetBodyConversion prepare_edited_sheet_from_body(
        const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,
        const std::string& conversion_id,const std::optional<Face>& selected,
        double thickness,const kernel::OcctKernel& kernel,
        bool thickness_override,std::vector<Face> included_faces,
        std::vector<std::string> rip_sketches) {
    const auto* existing=source.find_container(conversion_id);
    const auto* owner=source.body_owner_for_object(conversion_id);
    if(!existing||existing->feature_kind!=document::FeatureKind::SheetFromBody)
        throw std::invalid_argument("Sheet from Body does not exist.");
    if(!owner||owner->derived_copy||owner->scope.id!=source.body_history.active_body_id())
        throw std::invalid_argument("Sheet from Body requires an editable active Body.");
    if(!selected&&(!included_faces.empty()||!rip_sketches.empty()))
        throw std::invalid_argument("Select a calculated planar source face.");
    if(!thickness_override)
        thickness=document::sheet_metal_defaults(source).thickness_mm.value_or(1.);
    if(!std::isfinite(thickness)||thickness<.001||thickness>1000000)
        throw std::invalid_argument("Sheet thickness must be between 0.001 and 1000000 mm.");
    const auto old=*existing;
    const auto old_body=*owner;
    const auto old_definition=old.sheet_from_body;
    SheetConversionAuthoringScope authoring_scope;
    SheetBodyConversion rebuilt;
    auto prefix=source;
    {
        const auto boundary=std::ranges::find(old_body.entries,conversion_id,
            &document::PartHistoryEntry::id);
        if(boundary==old_body.entries.end())
            throw std::invalid_argument("Sheet from Body is missing from Body history");
        for(auto entry=old_body.entries.rbegin();
                entry!=std::make_reverse_iterator(boundary);++entry)
            prefix.erase_history_object(entry->id);
        prefix.body_history.activate(old_body.scope.id);
        prefix.body_history.set_history_cursor(old_body.scope.id,
            prefix.body_history.find(old_body.scope.id)->entries.size());
        const auto& order=prefix.body_history.order();
        prefix.body_history.set_insertion_cursor(static_cast<std::size_t>(
            std::ranges::find(order,old_body.scope.id)-order.begin())+1);
    }
    if(selected) {
        PartCalculationPolicy policy;policy.reject_errors=true;
        const auto input=calculate_part_with_resolved_references(kernel,prefix,&cache,policy);
        rebuilt=prepare_sheet_from_body(prefix,input,*selected,thickness,kernel,
            thickness_override,std::move(included_faces),std::move(rip_sketches));
        if(rebuilt.container_id.empty())
            throw std::invalid_argument("Sheet from Body does not exist.");
    } else {
        rebuilt.document=std::move(prefix);
        auto marker=document::PartDocument::create_sheet_from_body_container();
        marker.sheet_from_body.source_body_id=old_body.scope.id;
        marker.sheet_from_body.replaces_body=true;
        marker.sheet_from_body.thickness=thickness;
        marker.sheet_from_body.thickness_override=thickness_override;
        rebuilt.container_id=marker.id;
        rebuilt.document.insert_history_entry(document::PartHistoryKind::Feature,marker.id);
        rebuilt.document.history.push_back(std::move(marker));
    }

    std::map<std::string,const document::SheetFromBodyChildBinding*> old_regions;
    for(const auto& binding:old_definition.child_bindings)
        old_regions.emplace(binding.region_id,&binding);
    std::vector<std::pair<std::string,std::string>> replacements{
        {rebuilt.container_id,conversion_id}};
    const auto* generated_parent=rebuilt.document.find_container(rebuilt.container_id);
    replacements.emplace_back(generated_parent->feature_id,old.feature_id);
    for(const auto& binding:generated_parent->sheet_from_body.child_bindings) {
        const auto previous=old_regions.find(binding.region_id);
        if(previous==old_regions.end()||previous->second->bend!=binding.bend)continue;
        const auto* generated=rebuilt.document.find_container(binding.feature_id);
        const auto* retained=source.find_container(previous->second->feature_id);
        if(!generated||!retained)continue;
        replacements.emplace_back(generated->id,retained->id);
        replacements.emplace_back(generated->feature_id,retained->feature_id);
        const auto* generated_sketch=owned_sketch(rebuilt.document,generated->id);
        const auto* retained_sketch=owned_sketch(source,retained->id);
        if(generated_sketch&&retained_sketch)
            replacements.emplace_back(generated_sketch->id,retained_sketch->id);
    }
    auto encoded_document=rebuilt.document.serialized();
    // Temporary generated owners must not collide with removed owners' old
    // dimension reservations during remapping. The final document below keeps
    // the source's original ledger and allocates only genuinely new parameters.
    encoded_document["dimension_identifiers"]=nlohmann::json::parse(
        document::DimensionIdentifiers{}.serialized());
    auto encoded=encoded_document.dump();
    replace_generated_identity(encoded,replacements);
    rebuilt.document=document::PartDocument::from_serialized(nlohmann::json::parse(encoded));
    const auto definition=rebuilt.document.find_container(conversion_id)->sheet_from_body;
    SheetBodyConversion result;result.document=source;result.container_id=conversion_id;
    result.skipped=rebuilt.skipped;
    // Temporarily detach the old children for the established lost-reference
    // lifecycle when an edited region disappears. Surviving children keep IDs.
    auto* parent=result.document.find_container(conversion_id);
    for(auto& binding:parent->sheet_from_body.child_bindings)binding.detached=true;
    for(const auto& binding:old_definition.child_bindings) {
        if(binding.detached||std::ranges::find(definition.generated_features,
                binding.feature_id)!=definition.generated_features.end())continue;
        const auto deletion=plan_history_deletion(result.document,binding.feature_id);
        detach_deleted_history_references(result.document,deletion);
        result.document.erase_history_object(binding.feature_id);
    }
    for(const auto& binding:definition.child_bindings) {
        const auto prior=old_regions.find(binding.region_id);
        if(prior!=old_regions.end()&&prior->second->detached)continue;
        const auto* generated=rebuilt.document.find_container(binding.feature_id);
        const auto* generated_sketch=owned_sketch(rebuilt.document,binding.feature_id);
        if(!generated||!generated_sketch)
            throw std::runtime_error("Sheet from Body generated an incomplete region.");
        auto* retained=result.document.find_container(binding.feature_id);
        if(retained) {
            auto* retained_sketch=owned_sketch(result.document,binding.feature_id);
            if(!retained_sketch)
                throw std::runtime_error("Sheet from Body generated an incomplete region.");
            auto next=*retained;next.placement=generated->placement;
            if(binding.bend) {
                next.bend=generated->bend;
                next.bend.auxiliary_sketches[1]=retained->bend.auxiliary_sketches[1];
                next.bend.corner=retained->bend.corner;
                next.bend.corner_gap=retained->bend.corner_gap;
            } else next.flat=generated->flat;
            auto sketch=*generated_sketch;
            if(binding.bend)document::prepare_bend_sketches(next,sketch,
                document::sheet_metal_defaults(result.document));
            *retained=std::move(next);
            *retained_sketch=std::move(sketch);
        } else {
            result.document.history.push_back(*generated);
            result.document.sketches.push_back(*generated_sketch);
            result.created.push_back(binding.feature_id);
        }
    }
    parent=result.document.find_container(conversion_id);
    parent->sheet_from_body=definition;
    for(auto& binding:parent->sheet_from_body.child_bindings) {
        const auto prior=old_regions.find(binding.region_id);
        if(prior!=old_regions.end()&&prior->second->detached)binding=*prior->second;
    }
    // Unmatched detached features remain independent ordinary history entries.
    // Clearing the root releases the boundary's definition, not those features.
    auto body=*result.document.body_history.find(old_body.scope.id);
    std::set<std::string> bound;
    for(const auto& binding:old_definition.child_bindings)
        if(!binding.detached)bound.insert(binding.feature_id);
    std::erase_if(body.entries,[&](const auto& entry){return bound.contains(entry.id);});
    auto insertion=std::ranges::find(body.entries,conversion_id,&document::PartHistoryEntry::id);
    std::size_t position=static_cast<std::size_t>(insertion-body.entries.begin())+1;
    for(const auto& binding:parent->sheet_from_body.child_bindings)if(!binding.detached)
        body.entries.insert(body.entries.begin()+static_cast<std::ptrdiff_t>(position++),
            {document::PartHistoryKind::Feature,binding.feature_id});
    if(old_body.cursor==old_body.entries.size())body.cursor=body.entries.size();
    else {
        const auto frontier=std::ranges::find(body.entries,
            old_body.entries[old_body.cursor].id,&document::PartHistoryEntry::id);
        body.cursor=frontier==body.entries.end()?position:
            static_cast<std::size_t>(frontier-body.entries.begin());
    }
    auto graph=result.document.body_history;graph.update_body(std::move(body));
    result.document.set_body_history(std::move(graph));
    result.document.validate_sheet_from_body();
    auto policy=feature_definition_calculation_policy(source,cache,conversion_id);
    policy.edited_document_id=source.document_id;
    policy.edited_history_limit=source.history_index(conversion_id);
    result.calculated=calculate_part_with_resolved_references(
        kernel,result.document,&cache,policy);
    return result;
}
