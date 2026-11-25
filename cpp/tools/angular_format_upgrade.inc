// Explicit offline factory conversion. The application has one current reader.
namespace {
std::size_t upgrade_angle_reference_json(nlohmann::json& value) {
    std::size_t changed{};
    if (value.is_string()) {
        const auto text=value.get<std::string>();
        if (!text.empty() && (text.front()=='{' || text.front()=='[')) {
            auto nested=nlohmann::json::parse(text,nullptr,false);
            if (!nested.is_discarded()) {
                changed=upgrade_angle_reference_json(nested);
                if (changed) value=nested.dump();
            }
        }
        return changed;
    }
    if (value.is_array()) {
        for (auto& item:value) changed+=upgrade_angle_reference_json(item);
        return changed;
    }
    if (!value.is_object()) return 0;
    if(value.contains("name")&&value.contains("rotation")&&value.contains("zoom")&&
       value.contains("pan_x")&&value.contains("pan_y")&&value.contains("reference_scale")&&
       !value.contains("center_offset")) {value["center_offset"]={0,0,0};++changed;}
    if (value.value("format",std::string{})=="zima-shared-cache-v1") {
        auto unpacked=zima::document::unpack_cache_storage(std::move(value));
        changed=upgrade_angle_reference_json(unpacked);
        value=zima::document::pack_cache_storage(std::move(unpacked));
        return changed;
    }
    if (value.contains("owner_id") && value.contains("semantic_key") &&
        value.contains("supports_offset") && value.contains("orientation_only") &&
        value.contains("orientation_drives_rotation") && value.contains("offset")) {
        if (!value.contains("use_angle")) {value["use_angle"]=false;++changed;}
    }
    if(value.contains("correspondence_start_point_id")&&value.contains("sketch_serialized")&&
       !value.contains("correspondence_reversed")) {value["correspondence_reversed"]=false;++changed;}
    if(value.contains("correspondence_starts")&&value.contains("profiles")&&value.contains("guides")&&
       !value.contains("correspondence_reversed")) {value["correspondence_reversed"]=nlohmann::json::array();++changed;}
    for (auto& item:value) changed+=upgrade_angle_reference_json(item);
    if (value.value("format",std::string{})=="zima-cad-cpp" && value.value("type",std::string{})=="part") {
        // Factory and approved protected-project Drawings retain older embedded,
        // Sketch-only frames and title blocks. Convert those snapshots offline;
        // this does not authorize arbitrary old modeling-history recovery.
        const bool embedded_drawing_library=(value.at("format_version")==70||value.at("format_version")==71)&&
            std::ranges::all_of(value.at("history"),[](const auto& item) {
                return item.value("type",std::string{})=="sketch";
            });
        if (!embedded_drawing_library&&value.at("format_version")!=77&&value.at("format_version")!=78&&value.at("format_version")!=79&&value.at("format_version")!=80)
            throw std::runtime_error("Unexpected embedded conversion-source Part format or history");
        value["format_version"]=80;
        auto authored=value;authored["calculated_boundaries"]=nlohmann::json::array();
        value["calculation_definition_fingerprint"]=zima::document::PartDocument::from_serialized(authored)
            .serialized().at("calculation_definition_fingerprint");
        ++changed;
    }
    if (value.value("format",std::string{})=="zima-cad-cpp" && value.value("type",std::string{})=="assembly") {
        if (value.at("format_version")!=49&&value.at("format_version")!=50&&value.at("format_version")!=51)
            throw std::runtime_error("Expected an embedded conversion-source Assembly format 49, 50 or 51");
        value["format_version"]=51;++changed;
    }
    return changed;
}

std::vector<zima::kernel::BodyResult> analyze_retained_native_baseline(
        const std::vector<zima::kernel::BodyResult>& baseline,zima::kernel::OcctKernel& engine) {
    using namespace zima;
    // A user document may defer scalar analysis. Compare properties of its
    // retained exact B-Rep with the new calculation, never pending/zero fields
    // with calculated values. This does not regenerate the comparison source.
    auto analyzed_baseline=baseline;
    std::map<std::string,const kernel::BodyResult*> native_sources;
    std::set<const kernel::BodyResult*> visited;
    const auto retain_native=[&](auto&& self,const kernel::BodyResult& body)->void {
        if(!visited.insert(&body).second)return;
        if(!body.source_fingerprint.empty()&&!body.kernel_shape.empty())
            native_sources.try_emplace(body.source_fingerprint,&body);
        for(const auto* snapshots:{&body.body_inputs,&body.body_outputs})
            for(const auto& [id,value]:*snapshots)self(self,value.get());
        for(const auto& [id,values]:body.body_boundaries)for(const auto& value:values.get())self(self,value);
        if(body.incremental_history)for(const auto& frame:body.incremental_history->frames.get())self(self,frame.native_body.get());
    };
    for(const auto& body:baseline)retain_native(retain_native,body);
    for(std::size_t i=0;i<analyzed_baseline.size();++i) {
        auto& body=analyzed_baseline[i];if(body.physical_properties_current)continue;
        if(body.kernel_shape.empty())if(const auto found=native_sources.find(body.source_fingerprint);found!=native_sources.end()) {
            // Final-first publication can keep a boundary's exact B-Rep in
            // the native history archive rather than its display packet.
            auto native=*found->second;engine.ensure_body_properties(native);
            body.volume=native.volume;body.surface_area=native.surface_area;
            body.physical_properties_current=true;
        }
        if(!body.physical_properties_current)try {engine.ensure_body_properties(body);}
        catch(const std::exception& e){throw std::runtime_error("Cannot analyze retained baseline boundary "+std::to_string(i)+": "+e.what());}
    }
    return analyzed_baseline;
}

std::vector<zima::kernel::BodyResult> recalculate_angle_format_part(
        zima::document::PartDocument& authored,const std::vector<zima::kernel::BodyResult>& baseline,
        zima::kernel::OcctKernel& engine,std::size_t& embedded) {
    using namespace zima;
    for (auto& feature:authored.history) if (feature.feature_kind==document::FeatureKind::SheetForm) {
        std::vector<kernel::BodyResult> previous;
        auto definition=document::PartDocument::from_serialized(nlohmann::json::parse(*feature.sheet_form.definition),&previous);
        const auto calculated=recalculate_angle_format_part(definition,previous,engine,embedded);
        feature.sheet_form.definition=std::make_shared<const std::string>(definition.serialized(calculated).dump());
        ++embedded;
    }
    const auto original=authored;
    workspace::Workspace live;const auto id=authored.document_id;
    live.add_part(authored,baseline);
    workspace::PartCalculationPolicy policy;policy.reject_errors=true;
    policy.refresh_physical_properties=true;policy.publish_complete_history=true;
    static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
    const auto* current=live.open_part(id);
    if (!current) throw std::runtime_error("Angular format conversion lost its Part");
    auto updated=current->session.document();
    std::vector<kernel::BodyResult> calculated;
    {
        kernel::AggregatePropertyScope all(kernel::AggregatePropertyDemand::All);
        calculated=engine.evaluate_history(updated.kernel_operations());
    }
    for (const auto& body:calculated) if (!body.calculation_errors.empty())
        throw std::runtime_error("Angular format factory calculation contains errors");
    const auto analyzed_baseline=analyze_retained_native_baseline(baseline,engine);
    const auto error=format_migration_calculated_contract_error(analyzed_baseline,calculated);
    if (!error.empty()) throw std::runtime_error("Angular format conversion changed the prior geometry: "+error);
    if (comparable_history(original.history)!=comparable_history(updated.history) ||
        original.body_history!=updated.body_history || original.sketches.size()!=updated.sketches.size())
        throw std::runtime_error("Angular format conversion changed authored history or Body roles");
    for (std::size_t i=0;i<original.sketches.size();++i)
        if (!same_authored_sketch_definition(original.sketches[i],updated.sketches[i]) ||
            !approximately_same_contract(nlohmann::json::parse(original.sketches[i].serialized()),
                                         nlohmann::json::parse(updated.sketches[i].serialized())))
            throw std::runtime_error("Angular format conversion changed authored Sketches");
    authored=std::move(updated);
    return calculated;
}

void upgrade_angular_native_asset(const std::filesystem::path& source,const std::filesystem::path& output,
        const std::filesystem::path& report) {
    using namespace zima;
    if (std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
        throw std::runtime_error("Verification output must be separate from its source");
    std::ifstream input(source,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read angular format source");
    std::string section,line,converted,cache;bool version_seen=false,is_assembly=false;
    std::size_t references{};
    while (std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.starts_with('[') && line.ends_with(']')) section=line.substr(1,line.size()-2);
        if (section=="CachedBodies") {cache+=line+'\n';continue;}
        if (section=="Document" && line.starts_with("format_version=")) {
            if (version_seen) throw std::runtime_error("Duplicate native format declaration");
            version_seen=true;
            if (line=="format_version=54"||line=="format_version=55"||line=="format_version=56"||line=="format_version=57")line="format_version=57";
            else if (line=="format_version=37"||line=="format_version=38"||line=="format_version=39") {line="format_version=39";is_assembly=true;}
            else throw std::runtime_error("Offline conversion requires Part 54/55/56 or Assembly 37/38");
        }
        const auto equals=line.find('=');
        if (equals!=std::string::npos && equals+1<line.size() && (line[equals+1]=='{' || line[equals+1]=='[')) {
            auto value=nlohmann::json::parse(line.substr(equals+1),nullptr,false);
            if (!value.is_discarded()) {
                const auto changed=upgrade_angle_reference_json(value);references+=changed;
                if (changed) line=line.substr(0,equals+1)+value.dump();
            }
        }
        converted+=line+'\n';
    }
    if (!input.eof() || !version_seen) throw std::runtime_error("Invalid angular format source");
    std::filesystem::create_directories(output.parent_path());
    const auto write=[&](const std::string& data) {
        std::ofstream stream(output,std::ios::binary|std::ios::trunc);stream<<data;
        if (!stream)throw std::runtime_error("Cannot write angular format candidate");
    };
    write(converted);
    nlohmann::json evidence{{"source",document::path_to_utf8(source)},
        {"reference_fields_added",references},{"native_readback_exact",false}};
    if (is_assembly) {
        auto assembly=assembly::AssemblyDocument::load(output);
        if (!assembly.components.empty() || !assembly.cuts.empty())
            throw std::runtime_error("Assembly conversion requires an empty factory start template");
        const auto definition=assembly.serialized();assembly.save(output);
        if (assembly::AssemblyDocument::load(output).serialized()!=definition)
            throw std::runtime_error("Angular Assembly template changed on reopen");
        evidence["assembly_start_template"]=true;
    } else {
        auto authored=document::PartDocument::load(output);
        // Authored conversion adds inactive angle/reversed modes and versions.
        // Attach the untouched old cache solely as an offline comparison oracle.
        // Runtime output below is always freshly calculated and saved.
        if (!cache.empty()) {
            const auto marker=cache.find("definition_fingerprint=");
            if (marker==std::string::npos) throw std::runtime_error("Prior native cache lacks its definition fingerprint");
            const auto begin=marker+std::string("definition_fingerprint=").size(),end=cache.find('\n',begin);
            cache.replace(begin,end-begin,authored.serialized().at("calculation_definition_fingerprint").get<std::string>());
            write(converted+cache);
        }
        std::vector<kernel::BodyResult> previous;
        authored=document::PartDocument::load(output,&previous);
        kernel::OcctKernel engine;std::size_t embedded{};
        const auto calculated=recalculate_angle_format_part(authored,previous,engine,embedded);
        authored.save(output,calculated);
        std::vector<kernel::BodyResult> restored;
        const auto reopened=document::PartDocument::load(output,&restored);
        if (authored.serialized()!=reopened.serialized() || restored.size()!=calculated.size())
            throw std::runtime_error("Angular native readback changed definition or boundaries");
        for (std::size_t i=0;i<calculated.size();++i)
            if (document::serialize_body_result(calculated[i])!=document::serialize_body_result(restored[i]))
                throw std::runtime_error("Angular native readback changed geometry or side identities");
        evidence["calculated_boundaries"]=calculated.size();
        evidence["prior_cached_geometry_compared"]=!previous.empty();
        evidence["authored_history_unchanged"]=true;
        evidence["embedded_forms_recalculated"]=embedded;
    }
    evidence["native_readback_exact"]=true;
    std::filesystem::create_directories(report.parent_path());
    std::ofstream stream(report);stream<<evidence.dump(2)<<'\n';
    if (!stream) throw std::runtime_error("Cannot write angular format evidence");
}
}
