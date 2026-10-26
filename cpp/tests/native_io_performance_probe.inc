int compare_native_properties(const std::filesystem::path& before,const std::filesystem::path& after) {
    using namespace zima;std::vector<kernel::BodyResult> left,right;
    const auto a=document::PartDocument::load(before,&left),b=document::PartDocument::load(after,&right);
    require(a.body_history.bodies().size()==b.body_history.bodies().size()&&left.size()==right.size(),"Native property oracle has different Body/history counts");
    std::map<std::string,std::string> ids;
    for(std::size_t i=0;i<a.body_history.bodies().size();++i)ids[a.body_history.bodies()[i].scope.id]=b.body_history.bodies()[i].scope.id;
    const auto vec=[](const kernel::Vec3& p){return Json::array({p.x,p.y,p.z});};
    const auto properties=[&](const kernel::BodyResult& body){return Json{{"volume",body.volume},{"area",body.surface_area},{"current",body.physical_properties_current},{"solid",body.solid_present},
        {"surface_centroid",body.surface_centroid?vec(*body.surface_centroid):Json(nullptr)},
        {"integrals",body.volume_integrals?Json{{"centroid",vec(body.volume_integrals->centroid)},{"inertia",body.volume_integrals->inertia}}:Json(nullptr)}}.dump();};
    std::size_t snapshots=0;
    std::function<void(const kernel::BodyResult&,const kernel::BodyResult&)> compare;
    compare=[&](const auto& x,const auto& y){
        require(properties(x)==properties(y),"Native properties changed between serial and parallel imports");++snapshots;
        require(x.body_boundaries.size()==y.body_boundaries.size()&&x.body_inputs.size()==y.body_inputs.size()&&x.body_outputs.size()==y.body_outputs.size(),"Native property snapshot hierarchy changed");
        for(const auto& [id,boundaries]:x.body_boundaries){const auto& other=y.body_boundaries.at(ids.at(id));require(boundaries.size()==other.size(),"Native Body boundary count changed");for(std::size_t i=0;i<boundaries.size();++i)compare(boundaries[i],other[i]);}
        for(const auto& [id,input]:x.body_inputs)compare(input.get(),y.body_inputs.at(ids.at(id)).get());
        for(const auto& [id,output]:x.body_outputs)compare(output.get(),y.body_outputs.at(ids.at(id)).get());
    };
    for(std::size_t i=0;i<left.size();++i)compare(left[i],right[i]);
    std::cout<<"Serial/parallel native properties match exactly; snapshots="<<snapshots<<std::endl;return 0;
}

int refresh_native_factory(const fs::path& input,const fs::path& output) {
    kernel::OcctKernel kernel;std::size_t count=0;
    for(const auto& entry:fs::recursive_directory_iterator(input))if(entry.is_regular_file()) {
        const auto relative=fs::relative(entry.path(),input),destination=output/relative;
        fs::create_directories(destination.parent_path());
        if(entry.path().extension()==".asmz") {
            auto model=assembly::AssemblyDocument::load(entry.path(),{},false);const auto definition=model.serialized();
            model.save(destination);require(assembly::AssemblyDocument::load(destination,{},false).serialized()==definition,"Factory Assembly definition changed");
        } else {
            std::vector<kernel::BodyResult> previous;auto model=document::PartDocument::load(entry.path(),&previous);
            const auto authored=[](const auto& document){auto definition=document.serialized();definition.erase("calculation_definition_fingerprint");return definition;};
            const auto definition=authored(model);
            kernel::AggregatePropertyScope analysis(kernel::AggregatePropertyDemand::All);
            kernel::FaceMeasurementScope demand(false);
            const auto calculated=kernel.evaluate_history(model.kernel_operations());
            model.resolve_constructions(workspace::construction_reference_source_geometry(calculated));
            require(std::ranges::all_of(calculated,[](const auto& body){return body.calculation_errors.empty();}),"Factory geometry failed");
            if(authored(model)!=definition)std::cerr<<relative.generic_string()<<" definition diff "<<Json::diff(definition,authored(model)).dump()<<std::endl;
            require(authored(model)==definition,"Factory authored history or references changed");
            model.save(destination,calculated);std::vector<kernel::BodyResult> reopened;
            require(authored(document::PartDocument::load(destination,&reopened))==definition&&reopened.size()==calculated.size(),"Factory native reopen failed");
            for(std::size_t i=0;i<reopened.size();++i)require(document::serialize_body_result(reopened[i])==document::serialize_body_result(calculated[i]),"Factory exact calculated data did not roundtrip");
        }
        ++count;std::cout<<"Factory refreshed "<<relative.generic_string()<<std::endl;
    }
    std::cout<<"Factory native assets calculated/saved/reopened="<<count<<std::endl;return 0;
}
int save_native_assembly_definition(const fs::path& input,const fs::path& output) {
    std::ifstream stream(input);Json definition;stream>>definition;
    auto model=assembly::AssemblyDocument::from_serialized(definition);
    model.save(output);const auto reopened=assembly::AssemblyDocument::load(output,{},false);
    require(reopened.serialized()==definition,"Factory Assembly authored definition did not roundtrip");
    std::cout<<"Factory Assembly calculated/written/reopened "<<output.generic_string()<<std::endl;return 0;
}

int benchmark_native_io(const std::filesystem::path& source) {
    using namespace zima;namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-native-io-"+document::PartDocument::create_default().document_id);fs::create_directory(directory);
    const auto input=directory/"input.prtz",output=directory/"roundtrip.prtz";fs::copy_file(source,input);
    const auto digest=[](const auto& json){const auto text=json.dump();return QCryptographicHash::hash(QByteArrayView(text.data(),text.size()),QCryptographicHash::Sha256);};
    const auto packet_digest=[&](const auto& boundaries){QCryptographicHash hash(QCryptographicHash::Sha256);for(const auto& body:boundaries)hash.addData(digest(document::serialize_body_result(body)));return hash.result();};
    auto started=std::chrono::steady_clock::now();std::vector<kernel::BodyResult> calculated;auto model=document::PartDocument::load(input,&calculated);const auto load_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto definition=digest(model.serialized()),packet=packet_digest(calculated);
    std::cout<<"Native IO baseline load_ms="<<load_ms<<" boundaries="<<calculated.size()<<" definition="<<definition.toHex().constData()<<" packet="<<packet.toHex().constData()<<std::endl;
    started=std::chrono::steady_clock::now();model.save(output,calculated);const auto save_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    started=std::chrono::steady_clock::now();std::vector<kernel::BodyResult> reopened;const auto restored=document::PartDocument::load(output,&reopened);const auto reopen_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    require(digest(restored.serialized())==definition&&packet_digest(reopened)==packet,"Native roundtrip changed exact definitions, geometry, properties, references or sides");
    std::cout<<"Native IO verified input_bytes="<<fs::file_size(input)<<" saved_bytes="<<fs::file_size(output)<<" load_ms="<<load_ms<<" save_ms="<<save_ms<<" reopen_ms="<<reopen_ms<<std::endl;
    require(fs::canonical(directory).parent_path()==parent,"Unsafe native IO probe cleanup");fs::remove_all(directory);return 0;
}

int resave_native_part(const std::filesystem::path& source,
        const std::filesystem::path& destination) {
    using namespace zima;namespace fs=std::filesystem;
    require(!fs::exists(destination),"Native conversion destination already exists");
    std::vector<kernel::BodyResult> calculated;
    const auto model=document::PartDocument::load(source,&calculated);
    const auto definition=model.serialized();
    model.save(destination,calculated);
    std::vector<kernel::BodyResult> reopened;
    const auto restored=document::PartDocument::load(destination,&reopened);
    require(restored.serialized()==definition,
        "Native conversion changed the authored document definition");
    require(reopened.size()==calculated.size(),
        "Native conversion changed the calculated history count");
    for(std::size_t index=0;index<calculated.size();++index)
        require(document::serialize_body_result(reopened[index])==
                document::serialize_body_result(calculated[index]),
            "Native conversion changed exact geometry, properties, references or sides");
    std::cout<<"Native Part converted and exactly verified input="
        <<source.generic_string()<<" output="<<destination.generic_string()
        <<" boundaries="<<calculated.size()<<" input_bytes="<<fs::file_size(source)
        <<" output_bytes="<<fs::file_size(destination)<<std::endl;
    return 0;
}

std::string edit_native_last_operation(zima::document::PartDocument& changed) {
    using namespace zima;
    auto& feature=changed.history.back();
    if(feature.feature.type==document::FeatureType::Modeling&&!feature.feature.sketch_id.empty()) {
        const auto sketch=std::ranges::find(changed.sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
        if(sketch!=changed.sketches.end()&&!sketch->corner_radii.empty()) {
            const auto corner=sketch->corner_radii.front();
            static_cast<void>(sketch->add_corner_fillet(
                corner.first_segment_id,corner.second_segment_id,corner.radius+1.0));
            return "CornerRadius";
        }
    }
    for(auto& side:feature.feature.sides)if(side.operation==document::FeatureSideOperation::Extrusion&&
            side.extrusion_extent==document::EndCondition::Length) {
        side.length+=1.0;
        return "ExtrusionLength";
    }
    for(auto& side:feature.feature.sides)if(side.operation==document::FeatureSideOperation::Extrusion) {
        side.draft_angle_degrees+=.25;
        return "DraftAngle";
    }
    throw std::runtime_error("Last operation needs an editable extrusion parameter");
}

int benchmark_native_last_operation(const std::filesystem::path& source, bool add=false,bool cold_only=false) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    auto model=document::PartDocument::load(source,&previous);
    require(previous.size()>1,"Last-operation probe needs calculated preceding history");
    const auto geometry_json=[](const kernel::BodyResult& body) {
        auto json=document::serialize_body_result(body);
        std::function<void(Json&)> remove_analysis=[&](Json& value) {
            if(value.is_object()){value.erase("measured_area");value.erase("kernel_bindings");value.erase("measurement_sources");value.erase("kernel_shape");}
            if(value.is_structured())for(auto& child:value)remove_analysis(child);
        };
        remove_analysis(json);return json;
    };
    const auto geometry_digest=[](const Json& json) {
        const auto bytes=json.dump();
        return QCryptographicHash::hash(QByteArrayView(bytes.data(),bytes.size()),QCryptographicHash::Sha256);
    };
    const auto& expected_body=previous.back().body_boundaries.at(model.body_history.active_body_id()).back();
    const auto reference_signatures=[](const kernel::BodyResult& body) {
        std::multiset<std::string> values;
        for(const auto& face:body.mesh.original_references.triangle_references)
            values.insert("face|"+face.owner_id+'|'+face.semantic_key+'|'+face.instance_path+'|'+
                std::to_string(static_cast<int>(face.sheet_role))+'|'+face.sheet_owner+'|'+
                (face.surface?std::to_string(static_cast<int>(face.surface->kind))+'|'+
                    std::to_string(face.surface->reversed):"none"));
        for(const auto& edge:body.mesh.original_references.edges)
            values.insert("edge|"+edge.reference.owner_id+'|'+edge.reference.semantic_key+'|'+edge.reference.instance_path);
        for(const auto& point:body.mesh.original_references.points)
            values.insert("point|"+point.reference.owner_id+'|'+point.reference.semantic_key+'|'+point.reference.instance_path);
        for(const auto& axis:body.mesh.original_references.axes)
            values.insert("axis|"+axis.reference.owner_id+'|'+axis.reference.semantic_key+'|'+axis.reference.instance_path);
        return values;
    };
    const auto expected_references=reference_signatures(expected_body);
    const auto mesh_bounds=[](const kernel::BodyResult& body) {
        kernel::ModelEnvelope bounds;
        for(const auto& vertex:body.mesh.vertices)bounds.include(vertex);
        return bounds;
    };
    const auto expected_bounds=mesh_bounds(expected_body);
    const auto expected_json=geometry_json(expected_body);
    const auto expected_geometry=geometry_digest(expected_json);
    const auto expected_volume=expected_body.volume;
    const auto expected_surface_area=expected_body.surface_area;
    const auto expected_vertex_count=expected_body.mesh.vertices.size();
    const auto expected_triangle_count=expected_body.mesh.triangles.size()/3;
    const auto expected_edge_count=expected_body.mesh.edges.size();
    if(add) {
        auto& feature=model.history.back();
        require(feature.feature_kind==document::FeatureKind::Feature&&feature.feature.type==document::FeatureType::Modeling,
            "Last-operation Add probe needs a modeling Feature");
        feature.combine_mode=document::CombineMode::Add;
        for(auto& side:feature.feature.sides)if(side.operation==document::FeatureSideOperation::Extrusion)
            side.extrusion_extent=document::EndCondition::Length;
    }
    const auto definition=model.serialized().dump();
    previous.pop_back();kernel::OcctKernel kernel;
    const auto start=std::chrono::steady_clock::now();
    auto calculated=workspace::calculate_part_with_resolved_references(kernel,model,&previous);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Last operation failed");
    if(model.serialized().dump()!=definition) {
        const auto difference=Json::diff(Json::parse(definition),model.serialized());
        std::cerr<<"Last-operation definition differences="<<difference.size()<<std::endl;
        for(std::size_t index=0;index<std::min<std::size_t>(difference.size(),12);++index)
            std::cerr<<difference[index].dump().substr(0,1000)<<std::endl;
    }
    require(model.serialized().dump()==definition,"Last-operation calculation changed persisted definition or side choices");
    const auto& branch=calculated.back().body_boundaries.at(model.body_history.active_body_id()).back();
    const auto current_json=geometry_json(branch);const auto geometry=geometry_digest(current_json);
    const bool references_equal=expected_references==reference_signatures(branch);
    const auto current_bounds=mesh_bounds(branch);
    const auto same_bounds=[](const kernel::ModelEnvelope& left,const kernel::ModelEnvelope& right) {
        if(left.valid!=right.valid)return false;
        if(!left.valid)return true;
        const auto close=[](double a,double b){return std::abs(a-b)<=1.e-8;};
        return close(left.minimum.x,right.minimum.x)&&
            close(left.minimum.y,right.minimum.y)&&
            close(left.minimum.z,right.minimum.z)&&
            close(left.maximum.x,right.maximum.x)&&
            close(left.maximum.y,right.maximum.y)&&
            close(left.maximum.z,right.maximum.z);
    };
    const bool bounds_equal=same_bounds(expected_bounds,current_bounds);
    if(!add&&geometry!=expected_geometry) {
        std::cerr<<std::setprecision(17)<<"Last-operation semantic before/after volume="<<expected_volume<<'/'<<branch.volume
            <<" area="<<expected_surface_area<<'/'<<branch.surface_area
            <<" vertices="<<expected_vertex_count<<'/'<<branch.mesh.vertices.size()
            <<" triangles="<<expected_triangle_count<<'/'<<branch.mesh.triangles.size()/3
            <<" edges="<<expected_edge_count<<'/'<<branch.mesh.edges.size()
            <<" references_equal="<<references_equal<<" bounds_equal="<<bounds_equal<<std::endl;
        const auto difference=Json::diff(expected_json,current_json);
        std::cerr<<"Last-operation geometry differences="<<difference.size()<<std::endl;
        for(std::size_t index=0;index<std::min<std::size_t>(difference.size(),12);++index)
            std::cerr<<difference[index].dump().substr(0,1000)<<std::endl;
    }
    const bool allow_stale_snapshot=std::getenv("ZIMA_CPP_ALLOW_STALE_CALCULATED_SNAPSHOT")&&
        references_equal&&bounds_equal;
    if(!add)require(geometry==expected_geometry||allow_stale_snapshot,
        "Last operation changed geometry, properties, references or sides");
    const auto packet=document::serialize_body_result(branch).dump();
    const auto hash=QCryptographicHash::hash(QByteArrayView(packet.data(),packet.size()),QCryptographicHash::Sha256);
    std::cout<<"Native last operation ms="<<ms<<" packet="<<hash.toHex().constData()
        <<" geometry="<<geometry.toHex().constData()<<std::endl;
    if(cold_only)return 0;
    // Ordinary editing passes the complete aggregate, whose branch histories
    // retain the live prefix. Popping its last row discards that context and
    // measures another cold rebuild rather than a normal changed operation.
    auto changed=model;const auto edit_kind=edit_native_last_operation(changed);
    const auto warm_start=std::chrono::steady_clock::now();
    auto warm=workspace::calculate_part_with_resolved_references(kernel,changed,&calculated);
    const auto warm_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warm_start).count();
    require(!warm.empty()&&warm.back().calculation_errors.empty()&&model.serialized().dump()==definition,
        "Warm operation failed or changed the definition");
    const auto restore_start=std::chrono::steady_clock::now();
    const auto restored=workspace::calculate_part_with_resolved_references(kernel,model,&warm);
    const auto restore_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-restore_start).count();
    require(restored.back().calculation_errors.empty(),"Warm restored operation failed");
    const auto warm_packet=document::serialize_body_result(restored.back().body_boundaries.at(model.body_history.active_body_id()).back()).dump();
    if(warm_packet!=packet) {
        const auto& restored_body=restored.back().body_boundaries.at(model.body_history.active_body_id()).back();
        std::cerr<<"Warm semantic before/after vertices="<<branch.mesh.vertices.size()<<'/'<<restored_body.mesh.vertices.size()
            <<" triangles="<<branch.mesh.triangles.size()/3<<'/'<<restored_body.mesh.triangles.size()/3
            <<" edges="<<branch.mesh.edges.size()<<'/'<<restored_body.mesh.edges.size()
            <<" references_equal="<<(reference_signatures(branch)==reference_signatures(restored_body))
            <<" bounds_equal="<<(same_bounds(mesh_bounds(branch),mesh_bounds(restored_body)))
            <<" kernel_bytes="<<branch.kernel_shape.size()<<'/'<<restored_body.kernel_shape.size()
            <<" edit_ms="<<warm_ms<<" restore_ms="<<restore_ms<<std::endl;
        const auto old=Json::parse(packet),current=Json::parse(warm_packet);
        for(const auto& [key,value]:old.items())if(!current.contains(key)||current.at(key)!=value)
            std::cerr<<"Warm packet changed field="<<key<<" before_count="<<value.size()<<" after_count="<<(current.contains(key)?current.at(key).size():0)<<std::endl;
        unsigned differences=0;
        std::function<void(const Json&,const Json&,const std::string&)> detail;
        detail=[&](const Json& a,const Json& b,const std::string& path) {
            if(a==b||differences>=12)return;
            if(a.is_object()&&b.is_object())for(const auto& [key,value]:a.items()) {
                if(b.contains(key))detail(value,b.at(key),path+"/"+key);
                else {std::cerr<<"Warm missing "<<path<<"/"<<key<<std::endl;++differences;}
            } else if(a.is_array()&&b.is_array()&&a.size()==b.size()) {
                for(std::size_t i=0;i<a.size()&&differences<12;++i)detail(a[i],b[i],path+"/"+std::to_string(i));
            } else {
                std::cerr<<"Warm difference "<<path<<" before="<<a.dump().substr(0,200)<<" after="<<b.dump().substr(0,200)<<std::endl;++differences;
            }
        };
        detail(old,current,"");
        std::cerr<<"Warm restored geometry="<<geometry_digest(geometry_json(restored.back().body_boundaries.at(model.body_history.active_body_id()).back())).toHex().constData()<<std::endl;
    }
    const auto& restored_body=restored.back().body_boundaries.at(model.body_history.active_body_id()).back();
    require(geometry_digest(geometry_json(restored_body))==geometry&&
        reference_signatures(restored_body)==reference_signatures(branch)&&
        same_bounds(mesh_bounds(restored_body),mesh_bounds(branch)),
        "Warm operation changed geometry, properties or reference identities");
    require(warm_packet==packet,
        "Warm operation changed the cleaned kernel snapshot or measurement sources");
    require(restored_body.kernel_shape.size()<=branch.kernel_shape.size()*5/4,
        "Warm operation inflated the opaque kernel archive unexpectedly");

    auto changed_again=model;require(edit_native_last_operation(changed_again)==edit_kind,
        "Repeated warm operation changed parameter kind");
    const auto second_start=std::chrono::steady_clock::now();
    const auto second_warm=workspace::calculate_part_with_resolved_references(kernel,changed_again,&restored);
    const auto second_edit_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-second_start).count();
    const auto second_restore_start=std::chrono::steady_clock::now();
    const auto restored_again=workspace::calculate_part_with_resolved_references(kernel,model,&second_warm);
    const auto second_restore_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-second_restore_start).count();
    require(!second_warm.empty()&&!restored_again.empty()&&second_warm.back().calculation_errors.empty()&&restored_again.back().calculation_errors.empty(),
        "Second warm operation failed");
    const auto& restored_again_body=restored_again.back().body_boundaries.at(model.body_history.active_body_id()).back();
    require(geometry_digest(geometry_json(restored_again_body))==geometry&&
        reference_signatures(restored_again_body)==reference_signatures(branch)&&
        same_bounds(mesh_bounds(restored_again_body),mesh_bounds(branch)),
        "Second warm operation changed geometry, properties or reference identities");
    require(document::serialize_body_result(restored_again_body).dump()==packet,
        "Second warm operation changed the cleaned kernel snapshot or measurement sources");
    require(restored_again_body.kernel_shape.size()<=branch.kernel_shape.size()*5/4,
        "Repeated warm operation inflated the opaque kernel archive unexpectedly");
    std::cout<<"Native last operation warm_edit_ms="<<warm_ms<<" warm_restore_ms="<<restore_ms
        <<" second_edit_ms="<<second_edit_ms<<" second_restore_ms="<<second_restore_ms
        <<" kernel_bytes="<<branch.kernel_shape.size()<<'/'<<restored_body.kernel_shape.size()<<'/'<<restored_again_body.kernel_shape.size()
        <<" parameter="<<edit_kind<<" mode="<<(add?"Add":"Subtract")<<std::endl;
    if(std::getenv("ZIMA_CPP_SAVE_CALCULATED_PROBE")) {
        const auto parent=fs::canonical(fs::temp_directory_path());
        const auto directory=parent/("zima-demand-io-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);const auto output=directory/"calculated.prtz";
        const auto save_start=std::chrono::steady_clock::now();model.save(output,restored_again);
        const auto save_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-save_start).count();
        std::vector<kernel::BodyResult> reopened;const auto opened=document::PartDocument::load(output,&reopened);
        require(opened.serialized().dump()==definition&&reopened.size()==restored_again.size(),"Calculated native save changed definitions or history count");
        for(std::size_t i=0;i<restored_again.size();++i)require(document::serialize_body_result(reopened[i])==document::serialize_body_result(restored_again[i]),
            "Calculated native save changed geometry, pending analysis or references");
        std::cout<<"Native calculated IO verified save_ms="<<save_ms<<" bytes="<<fs::file_size(output)<<std::endl;
        require(fs::canonical(directory).parent_path()==parent,"Unsafe calculated probe cleanup");fs::remove_all(directory);
    }
    return 0;
}

int describe_native_last_operation(const std::filesystem::path& source) {
    using namespace zima;
    const auto model=document::PartDocument::load(source);
    const auto root=model.serialized();
    require(root.at("history").is_array()&&!root.at("history").empty(),
        "Last-operation description needs document history");
    const auto& feature=root.at("history").back();
    std::cout<<"Last feature\n"<<feature.dump(2)<<std::endl;
    const auto sketch_id=feature.value("feature",Json::object()).value("sketch_id",std::string{});
    if(sketch_id.empty())return 0;
    const auto sketch=std::ranges::find_if(root.at("sketches"),[&](const Json& value) {
        return value.value("id",std::string{})==sketch_id;
    });
    require(sketch!=root.at("sketches").end(),"Last-operation Sketch is missing");
    std::cout<<"Last Sketch\n"<<sketch->dump(2)<<std::endl;
    return 0;
}

int benchmark_native_loaded_last_operation(const std::filesystem::path& source) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    auto model=document::PartDocument::load(source,&previous);
    require(!previous.empty()&&!model.history.empty(),
        "Loaded last-operation probe needs calculated history");
    auto& feature=model.history.back();
    const auto parameter=edit_native_last_operation(model);
    kernel::OcctKernel kernel;
    workspace::PartCalculationPolicy policy;
    policy.edited_document_id=model.document_id;
    policy.edited_history_limit=model.history.size()-1;
    policy.reuse_unchanged_local_sketch_references=true;
    policy.reuse_unchanged_reference_resolution=true;
    policy.unchanged_last_operation_owner=feature.id;
    const auto start=std::chrono::steady_clock::now();
    const auto calculated=workspace::calculate_part_with_resolved_references(kernel,model,&previous,policy);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    require(!calculated.empty()&&calculated.back().calculation_errors.empty(),
        "Loaded last-operation calculation failed");
    require(calculated.size()==previous.size(),
        "Loaded last-operation calculation changed the history boundary count");
    for(std::size_t index=0;index+1<calculated.size();++index)
        require(document::serialize_body_result(calculated[index])==
                document::serialize_body_result(previous[index]),
            "Loaded last-operation calculation changed an unchanged saved prefix boundary");
    const auto& body=calculated.back().body_boundaries.at(model.body_history.active_body_id()).back();
    const auto packet=document::serialize_body_result(body).dump();
    const auto digest=QCryptographicHash::hash(QByteArrayView(packet.data(),packet.size()),QCryptographicHash::Sha256);
    std::cout<<"Native loaded last operation ms="<<ms
        <<" previous="<<previous.size()<<" calculated="<<calculated.size()
        <<" parameter="<<parameter<<" packet="<<digest.toHex().constData()<<std::endl;
    return 0;
}
