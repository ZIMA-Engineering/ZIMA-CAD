// Explicit, offline preparation of the approved protected-project copies.
// This is not a product reader, a published asset or a verified conversion.
// The caller must run native recalculation/comparison before promotion.
namespace {
nlohmann::json restore_saved_loft_correspondence(zima::document::PartDocument& authored,
        const std::vector<zima::kernel::BodyResult>& baseline,const std::set<std::string>& owners) {
    using namespace zima;auto changes=nlohmann::json::array();
    if(owners.empty())return changes;
    if(baseline.empty())throw std::runtime_error("Old Loft correspondence has no saved geometry oracle");
    const auto& geometry=baseline.back().mesh.original_references;
    const auto near=[](const auto& a,const auto& b) {
        return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<=1e-7;
    };
    for(auto& feature:authored.history)if(owners.contains(feature.id)) {
        auto& loft=feature.loft;
        if(loft.profiles.size()!=2||!loft.guides.empty())
            throw std::runtime_error("Offline Loft correspondence requires an explicit two-profile saved oracle");
        const auto sketch_for=[&](const std::string& id) {
            const auto found=std::ranges::find_if(loft.sketches,[&](const auto& source) {
                return source.sketch_feature&&source.sketch_feature->id==id;
            });
            if(found==loft.sketches.end())throw std::runtime_error("Saved Loft profile is absent");
            return sketcher::Sketch::from_serialized(found->sketch_serialized);
        };
        const bool open=loft.result_type!=document::ProfileResultType::Solid;
        const auto first=sketch_for(loft.profiles.front()),last=sketch_for(loft.profiles.back());
        const auto from=document::sweep3d_profile_correspondence(first,{},open);
        const auto to=document::sweep3d_profile_correspondence(last,{},open);
        if(from.point_ids.empty()||from.point_ids.size()!=to.point_ids.size())
            throw std::runtime_error("Saved Loft point correspondence is unavailable");
        const auto point_key=[](const std::string& profile,const std::string& point) {
            return "loft:point:from:"+profile+":"+point;
        };
        const auto saved_point=[&](const std::string& key) {
            std::optional<kernel::Vec3> result;
            for(const auto& point:geometry.points)if(point.reference.owner_id==feature.id&&point.reference.semantic_key==key) {
                if(result&&!near(*result,point.position))throw std::runtime_error("Ambiguous saved Loft point");
                result=point.position;
            }
            if(!result)throw std::runtime_error("Missing saved Loft point");return *result;
        };
        std::vector<std::string> correspondence;
        for(const auto& id:from.point_ids) {
            const auto key=point_key(loft.profiles.front(),id);const auto source=saved_point(key);
            std::set<std::string> matches;
            for(const auto& edge:geometry.edges)if(edge.reference.owner_id==feature.id&&
                    edge.reference.semantic_key=="loft:longitudinal:from:"+key&&edge.points.size()>=2) {
                const auto a=edge.points.front(),b=edge.points.back();
                if(!near(a,source)&&!near(b,source))throw std::runtime_error("Saved Loft rail lost its source point");
                const auto target=near(a,source)?b:a;
                for(const auto& end:to.point_ids)if(near(target,saved_point(point_key(loft.profiles.back(),end))))matches.insert(end);
            }
            if(matches.size()!=1)throw std::runtime_error("Saved Loft rail does not identify one target point");
            correspondence.push_back(*matches.begin());
        }
        bool found=false;
        for(bool reversed:{false,true})if(document::sweep3d_profile_correspondence(
                last,correspondence.front(),open,reversed).point_ids==correspondence) {
            loft.correspondence_starts[loft.profiles.front()]=from.point_ids.front();
            loft.correspondence_starts[loft.profiles.back()]=correspondence.front();
            if(reversed)loft.correspondence_reversed.insert(loft.profiles.back());
            changes.push_back({{"owner",feature.id},{"starts",loft.correspondence_starts},
                {"reversed",loft.correspondence_reversed},{"source_points",from.point_ids},{"target_points",correspondence}});
            found=true;break;
        }
        if(!found)throw std::runtime_error("Saved Loft correspondence is not representable by the current explicit order");
    }
    return changes;
}
void prepare_exact_surface_verification_part(const std::filesystem::path& source,
        const std::filesystem::path& output,const std::filesystem::path& report) {
    using namespace zima;
    if(std::filesystem::absolute(source).lexically_normal()==
            std::filesystem::absolute(output).lexically_normal()||std::filesystem::exists(output))
        throw std::runtime_error("Offline preparation requires a separate unused target");
    std::ifstream input(document::native_file_io_path(source),std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read offline preparation source");
    std::string section,line,definition,cache;
    int version{};std::size_t fields{};std::set<std::string> missing_loft_correspondence;
    // Native INI sections are not ordered: Containers can precede Document.
    // Determine the source contract before converting any authored fields.
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.starts_with('[')&&line.ends_with(']'))section=line.substr(1,line.size()-2);
        if(section=="Document"&&line.starts_with("format_version=")) {
            if(version)throw std::runtime_error("Duplicate native format declaration");
            if(line=="format_version=50")version=50;
            else if(line=="format_version=56")version=56;
            else if(line=="format_version=57")version=57;
            else throw std::runtime_error("Expected the approved Part 50, 56 or current Part 57 source");
        }
    }
    if(!input.eof()||!version)throw std::runtime_error("Invalid offline preparation source");
    input.clear();input.seekg(0);section.clear();
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.starts_with('[')&&line.ends_with(']'))section=line.substr(1,line.size()-2);
        if(section=="CachedBodies"){cache+=line+'\n';continue;}
        if(section=="Document"&&line.starts_with("format_version=")) {
            line="format_version=57";
        }
        const auto equal=line.find('=');
        if(equal!=std::string::npos&&equal+1<line.size()&&
                (line[equal+1]=='{'||line[equal+1]=='[')) {
            auto value=nlohmann::json::parse(line.substr(equal+1),nullptr,false);
            if(!value.is_discarded()) {
                std::size_t changed{};
                if(version==50) {
                    if(value.is_object()&&value.value("type",std::string{})=="loft") {
                        auto& loft=value.at("loft");
                        if(!loft.contains("correspondence_starts")) {
                            // Read the current schema first, then reconstruct the
                            // old automatic correspondence from saved native rails.
                            missing_loft_correspondence.insert(value.at("id").get<std::string>());
                            loft["correspondence_starts"]=nlohmann::json::object();++changed;
                        }
                    }
                    if(value.is_object()&&value.value("type",std::string{})=="sheet_from_body") {
                        auto& sheet=value.at("sheet_from_body");
                        if(sheet.contains("child_bindings"))throw std::runtime_error("Unexpected format-50 Sheet associations");
                        sheet["child_bindings"]=nlohmann::json::array();++changed;
                    }
                    changed+=add_edge_treatment_route_end_modes(value);
                    changed+=make_shared_fillet_nodes_explicit(value);
                }
                changed+=upgrade_angle_reference_json(value);
                if(changed)line=line.substr(0,equal+1)+value.dump();
                fields+=changed;
            }
        }
        definition+=line+'\n';
    }
    if(!input.eof()||!version)throw std::runtime_error("Invalid offline preparation source");
    std::filesystem::create_directories(output.parent_path());
    const auto write=[&](bool with_cache) {
        std::ofstream stream(document::native_file_io_path(output),std::ios::binary|std::ios::trunc);
        stream<<definition;if(with_cache)stream<<cache;
        if(!stream)throw std::runtime_error("Cannot write offline preparation target");
    };
    write(false);
    auto authored=document::PartDocument::load(output);
    // Publish the native reader's canonical authored definition before pairing
    // it with the saved comparison oracle. Derived profile serialization may
    // differ from the original INI even when its authored model is unchanged.
    authored.save(output);
    {
        std::ifstream normalized(document::native_file_io_path(output),std::ios::binary);
        definition.clear();section.clear();
        while(std::getline(normalized,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.starts_with('[')&&line.ends_with(']'))section=line.substr(1,line.size()-2);
            if(section!="CachedBodies")definition+=line+'\n';
        }
        if(!normalized.eof())throw std::runtime_error("Cannot read canonical offline definition");
    }
    if(!cache.empty()) {
        constexpr std::string_view key="definition_fingerprint=";
        const auto position=cache.find(key);
        if(position==std::string::npos||cache.find(key,position+key.size())!=std::string::npos)
            throw std::runtime_error("Prior calculation has no unique definition proof");
        const auto begin=position+key.size(),end=cache.find('\n',begin);
        cache.replace(begin,end-begin,authored.serialized().at("calculation_definition_fingerprint").get<std::string>());
        // Retain the saved native BRep only as a comparison oracle. It must
        // never be treated as proof that the new model has been calculated.
        write(true);
    }
    std::vector<kernel::BodyResult> baseline;
    auto reopened=document::PartDocument::load(output,&baseline);
    if(reopened.serialized()!=authored.serialized())throw std::runtime_error("Offline preparation changed its authored readback");
    const auto correspondence=restore_saved_loft_correspondence(authored,baseline,missing_loft_correspondence);
    if(!correspondence.empty()) {
        // Adding an explicit correspondence changes calculation fingerprints.
        // Do not attach a saved calculation to different parameters. The
        // unchanged source remains the independent native geometry oracle.
        authored.save(output);
        reopened=document::PartDocument::load(output);
        if(reopened.serialized()!=authored.serialized())throw std::runtime_error("Explicit Loft correspondence changed on readback");
    }
    std::filesystem::create_directories(report.parent_path());
    std::ofstream evidence(document::native_file_io_path(report));
    evidence<<nlohmann::json{{"source_format",version},{"prepared_fields",fields},
        {"baseline_boundaries",baseline.size()},{"native_readback_exact",true},
        {"restored_loft_correspondence",correspondence},
        {"baseline_retained_in_prepared_copy",correspondence.empty()},
        {"calculation_verified",false},{"publication_allowed",false}}.dump(2)<<'\n';
    if(!evidence)throw std::runtime_error("Cannot write offline preparation evidence");
}

void prepare_exact_surface_verification_document(const std::filesystem::path& source,
        const std::filesystem::path& output,const std::filesystem::path& report) {
    using namespace zima;
    const bool drawing=source.extension()==".drwz",assembly=source.extension()==".asmz";
    if(!drawing&&!assembly) {prepare_exact_surface_verification_part(source,output,report);return;}
    if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal()||
            std::filesystem::exists(output))throw std::runtime_error("Offline preparation requires a separate unused target");
    std::ifstream input(document::native_file_io_path(source),std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read offline preparation source");
    std::string section,line,prepared;bool version_seen=false;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.starts_with('[')&&line.ends_with(']'))section=line.substr(1,line.size()-2);
        if(section=="Document"&&line.starts_with("format_version=")) {
            if(version_seen)throw std::runtime_error("Duplicate native format declaration");
            version_seen=true;
            if(drawing&&(line=="format_version=21"||line=="format_version=22"))line="format_version=22";
            else if(assembly&&(line=="format_version=37"||line=="format_version=38"||line=="format_version=39"))line="format_version=39";
            else throw std::runtime_error("Unexpected offline document schema");
        }
        const auto equal=line.find('=');
        if(equal!=std::string::npos&&equal+1<line.size()&&(line[equal+1]=='{'||line[equal+1]=='[')) {
            auto data=nlohmann::json::parse(line.substr(equal+1));
            bool changed=upgrade_angle_reference_json(data)!=0;
            if(drawing&&data.is_object()&&data.value("format",std::string{})=="zima-cad-drawing") {
                if(data.at("version")!=13&&data.at("version")!=14)throw std::runtime_error("Unexpected Drawing payload schema");
                data["version"]=14;changed=true;
            }
            if(changed)line=line.substr(0,equal+1)+data.dump();
        }
        prepared+=line+'\n';
    }
    if(!input.eof()||!version_seen)throw std::runtime_error("Invalid offline preparation source");
    std::filesystem::create_directories(output.parent_path());
    {std::ofstream stream(document::native_file_io_path(output),std::ios::binary);stream<<prepared;
        if(!stream)throw std::runtime_error("Cannot write offline document preparation");}
    const auto id=drawing?drawing::DrawingDocument::load(output).document_id:assembly::AssemblyDocument::load(output).document_id;
    std::filesystem::create_directories(report.parent_path());
    std::ofstream evidence(document::native_file_io_path(report));
    evidence<<nlohmann::json{{"document_id",id},{"native_readback",true},{"calculation_verified",false},
        {"publication_allowed",false}}.dump(2)<<'\n';
    if(!evidence)throw std::runtime_error("Cannot save offline preparation report");
}
}
