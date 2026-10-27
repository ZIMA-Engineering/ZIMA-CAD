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
    const auto input=directory/"input.prtz",output=directory/"roundtrip.prtz",stripped_output=directory/"without-native-history.prtz";fs::copy_file(source,input);
    const auto digest=[](const auto& json){const auto text=json.dump();return QCryptographicHash::hash(QByteArrayView(text.data(),text.size()),QCryptographicHash::Sha256);};
    const auto packet_digest=[&](const auto& boundaries){QCryptographicHash hash(QCryptographicHash::Sha256);for(const auto& body:boundaries)hash.addData(digest(document::serialize_body_result(body)));return hash.result();};
    auto started=std::chrono::steady_clock::now();std::vector<kernel::BodyResult> calculated;auto model=document::PartDocument::load(input,&calculated);const auto load_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto definition=digest(model.serialized()),packet=packet_digest(calculated);
    std::cout<<"Native IO baseline load_ms="<<load_ms<<" boundaries="<<calculated.size()<<" definition="<<definition.toHex().constData()<<" packet="<<packet.toHex().constData()<<std::endl;
    started=std::chrono::steady_clock::now();model.save(output,calculated);const auto save_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    started=std::chrono::steady_clock::now();std::vector<kernel::BodyResult> reopened;const auto restored=document::PartDocument::load(output,&reopened);const auto reopen_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    require(digest(restored.serialized())==definition&&packet_digest(reopened)==packet,"Native roundtrip changed exact definitions, geometry, properties, references or sides");
    std::cout<<"Native IO verified input_bytes="<<fs::file_size(input)<<" saved_bytes="<<fs::file_size(output)<<" load_ms="<<load_ms<<" save_ms="<<save_ms<<" reopen_ms="<<reopen_ms<<std::endl;
    // The continuation cache is explicitly disposable. Measure its real file
    // and latency cost without changing authored data or any calculated viewer,
    // reference, property or native final-body packet.
    std::function<void(kernel::BodyResult&)> strip_history;
    strip_history=[&](kernel::BodyResult& body) {
        body.incremental_history.reset();
        for(auto& [id,rows]:body.body_boundaries) {
            auto copy=rows.get();for(auto& row:copy)strip_history(row);rows=std::move(copy);
        }
        for(auto* snapshots:{&body.body_inputs,&body.body_outputs})for(auto& [id,snapshot]:*snapshots) {
            auto copy=snapshot.get();strip_history(copy);snapshot=std::move(copy);
        }
        if(!body.measurement_sources.empty()) {
            std::vector<kernel::BodySnapshot> copies;copies.reserve(body.measurement_sources.size());
            for(const auto& snapshot:body.measurement_sources) {
                auto copy=snapshot.get();strip_history(copy);copies.emplace_back(std::move(copy));
            }
            body.measurement_sources=std::move(copies);
        }
    };
    auto without_history=calculated;for(auto& body:without_history)strip_history(body);
    const auto stripped_packet=packet_digest(without_history);
    started=std::chrono::steady_clock::now();model.save(stripped_output,without_history);
    const auto stripped_save_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    started=std::chrono::steady_clock::now();std::vector<kernel::BodyResult> stripped_reopened;
    const auto stripped_model=document::PartDocument::load(stripped_output,&stripped_reopened);
    const auto stripped_reopen_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    require(digest(stripped_model.serialized())==definition&&packet_digest(stripped_reopened)==stripped_packet,
        "Removing disposable native history changed authored data or the retained calculated packet");
    std::cout<<"Native IO without continuation bytes="<<fs::file_size(stripped_output)
        <<" save_ms="<<stripped_save_ms<<" reopen_ms="<<stripped_reopen_ms<<std::endl;
    require(fs::canonical(directory).parent_path()==parent,"Unsafe native IO probe cleanup");fs::remove_all(directory);return 0;
}

int benchmark_native_close(const std::filesystem::path& source) {
    using namespace zima;
    std::vector<kernel::BodyResult> calculated;
    auto model=document::PartDocument::load(source,&calculated);
    const auto id=model.document_id;
    workspace::Workspace live;
    live.add_part(std::move(model),std::move(calculated),source);
    std::future<void> disposal;
    const auto start=std::chrono::steady_clock::now();
    require(live.remove(id,[&](workspace::DocumentState&& state) {
        disposal=std::async(std::launch::async,
            [retired=std::make_shared<workspace::DocumentState>(std::move(state))] {});
    }),"Native close probe could not remove the loaded Part");
    const auto detach_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    require(live.size()==0,"Native close probe retained the removed Part");
    require(disposal.valid(),"Native close probe did not retire the removed Part");
    disposal.get();
    const auto total_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    std::cout<<"Native close detach_ms="<<detach_ms
        <<" disposal_ms="<<(total_ms-detach_ms)<<std::endl;
    return 0;
}

int describe_native_history(const std::filesystem::path& source) {
    using namespace zima;
    const auto packet_digest=[](const auto& value) {
        const auto text=value.dump();
        return QCryptographicHash::hash(
            QByteArrayView(text.data(),text.size()),QCryptographicHash::Sha256);
    };
    std::vector<kernel::BodyResult> calculated;
    const auto model=document::PartDocument::load(source,&calculated);
    std::size_t index{};
    for(const auto& container:model.history) {
        std::cout<<index++<<'\t'<<container.name<<'\t'<<container.id<<'\t'
            <<static_cast<int>(container.feature_kind)<<'\t'
            <<static_cast<int>(container.feature.type)<<'\t'
            <<container.feature.sketch_id<<'\n';
    }
    if(!calculated.empty()) {
        std::map<std::string,std::size_t> field_bytes;
        const auto& active=calculated.back().body_boundaries.at(
            model.body_history.active_body_id());
        std::vector<const kernel::BodySnapshot*> unique_measurement_sources;
        std::map<std::string,std::size_t> measurement_source_fingerprints;
        std::map<std::string,std::set<QByteArray>> measurement_source_digests;
        std::optional<nlohmann::json> previous_packet;
        const auto prefix=[](const auto& before,const auto& after) {
            return before.size()<=after.size()&&std::equal(before.begin(),before.end(),after.begin());
        };
        std::size_t boundary_index{};
        for(const auto& boundary:active) {
            const auto packet=document::serialize_body_result(boundary,false,false);
            for(const auto& [name,value]:packet.items())field_bytes[name]+=value.dump().size();
            std::size_t new_measurement_sources{};
            for(const auto& source:boundary.measurement_sources) {
                ++measurement_source_fingerprints[source->source_fingerprint];
                measurement_source_digests[source->source_fingerprint].insert(
                    packet_digest(document::serialize_body_result(source,false,false)));
                if(std::ranges::none_of(unique_measurement_sources,[&](const auto* known) {
                        return source.shares_with(*known);
                    })) {
                    unique_measurement_sources.push_back(&source);
                    ++new_measurement_sources;
                }
            }
            std::cout<<"Native history boundary="<<boundary_index
                <<" packet_bytes="<<packet.dump().size()
                <<" vertices="<<boundary.mesh.vertices.size()
                <<" triangles="<<boundary.mesh.triangles.size()/3
                <<" edges="<<boundary.mesh.edges.size()
                <<" points="<<boundary.mesh.points.size()
                <<" measurement_sources="<<boundary.measurement_sources.size()
                <<" new_shared_sources="<<new_measurement_sources
                <<" history_frames="<<(boundary.incremental_history
                    ?boundary.incremental_history->frames.size():0);
            if(previous_packet)std::cout
                <<" prefix_vertices="<<prefix(active[boundary_index-1].mesh.vertices,boundary.mesh.vertices)
                <<" prefix_triangles="<<prefix(active[boundary_index-1].mesh.triangles,boundary.mesh.triangles)
                <<" prefix_faces="<<prefix(previous_packet->at("triangle_references"),packet.at("triangle_references"))
                <<" prefix_edges="<<prefix(previous_packet->at("edges"),packet.at("edges"))
                <<" prefix_points="<<prefix(previous_packet->at("points"),packet.at("points"))
                <<" prefix_measurements="<<prefix(previous_packet->at("measurement_sources"),packet.at("measurement_sources"));
            std::cout<<'\n';
            previous_packet=packet;
            ++boundary_index;
        }
        const auto& final=calculated.back();
        std::size_t shared_input_outputs{};
        for(const auto& [id,input]:final.body_inputs) {
            const auto output=final.body_outputs.find(id);
            if(output!=final.body_outputs.end()&&input.shares_with(output->second))
                ++shared_input_outputs;
        }
        std::cout<<"Native history unique_shared_measurement_sources="
            <<unique_measurement_sources.size()<<" final_inputs="<<final.body_inputs.size()
            <<" final_outputs="<<final.body_outputs.size()
            <<" shared_input_outputs="<<shared_input_outputs<<'\n';
        const auto conflicting_fingerprints=std::ranges::count_if(
            measurement_source_digests,[](const auto& item) { return item.second.size()>1; });
        std::size_t measurement_source_occurrences{};
        for(const auto& [fingerprint,count]:measurement_source_fingerprints)
            measurement_source_occurrences+=count;
        std::cout<<"Native history measurement_source_occurrences="
            <<measurement_source_occurrences<<" unique_fingerprints="
            <<measurement_source_fingerprints.size()<<" conflicting_fingerprints="
            <<conflicting_fingerprints<<'\n';
        std::vector<std::pair<std::size_t,std::string>> ordered;
        for(const auto& [name,bytes]:field_bytes)ordered.emplace_back(bytes,name);
        std::ranges::sort(ordered,std::greater{});
        std::cout<<"Native history local_boundaries="<<active.size()<<" field_bytes:\n";
        for(const auto& [bytes,name]:ordered)std::cout<<bytes<<'\t'<<name<<'\n';
    }
    return 0;
}

std::set<std::string> native_operation_reference_owners(
        const zima::kernel::HistoryOperation& operation) {
    using namespace zima;
    std::set<std::string> references;
    const auto face=[&](const auto& reference) {
        if(!reference.owner_id.empty())references.insert(reference.owner_id);
    };
    const auto extrusion=[&](const kernel::ExtrusionRequest& request) {
        face(request.target_face);
        if(request.reverse_limit)face(request.reverse_limit->reference);
        if(request.ordered_end_limit)for(const auto& patch:request.ordered_end_limit->patches) {
            face(patch.source_reference);face(patch.target.reference);
        }
    };
    std::visit([&](const auto& request) {
        using Request=std::decay_t<decltype(request)>;
        if constexpr(std::is_same_v<Request,kernel::ExtrusionRequest>)extrusion(request);
        else if constexpr(std::is_same_v<Request,kernel::FeatureGroupRequest>) {
            for(const auto& child:request.children)std::visit([&](const auto& value) {
                using Child=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Child,kernel::ExtrusionRequest>)extrusion(value);
            },child);
        } else if constexpr(std::is_same_v<Request,kernel::FilletRequest>||
                std::is_same_v<Request,kernel::ChamferRequest>)
            for(const auto& edge:request.edges)
                if(!edge.owner_id.empty())references.insert(edge.owner_id);
        else if constexpr(std::is_same_v<Request,kernel::ShellRequest>)
            for(const auto& removed:request.removed_faces)face(removed);
    },operation.primitive);
    if(operation.feature_copy&&!operation.feature_copy->source_feature_id.empty())
        references.insert(operation.feature_copy->source_feature_id);
    return references;
}

int describe_native_kernel_plan(const std::filesystem::path& source) {
    using namespace zima;
    const auto model=document::PartDocument::load(source);
    const auto operations=model.kernel_operations(false,true);
    for(std::size_t index=0;index<operations.size();++index) {
        const auto& operation=operations[index];
        const auto references=native_operation_reference_owners(operation);
        std::cout<<index<<'\t'<<operation.owner_id<<" primitive="<<operation.primitive.index()
            <<" boolean="<<static_cast<int>(operation.operation)
            <<" body="<<static_cast<int>(operation.body.combination)
            <<" source="<<operation.body.source_id
            <<" feature="<<operation.body.source_feature_id<<" refs=";
        bool first=true;for(const auto& reference:references) {
            if(!first)std::cout<<',';std::cout<<reference;first=false;
        }
        std::cout<<'\n';
    }
    return 0;
}

int describe_native_dependency_envelopes(const std::filesystem::path& source,
        const std::string& changed_owner) {
    using namespace zima;
    std::vector<kernel::BodyResult> calculated;
    const auto model=document::PartDocument::load(source,&calculated);
    require(!calculated.empty(),"Native dependency probe has no calculated result");
    const auto operations=model.kernel_operations(false,true);
    std::set<std::string> affected{changed_owner};
    bool grew=true;
    while(grew) {
        grew=false;
        for(const auto& operation:operations)if(!affected.contains(operation.owner_id)) {
            const auto references=native_operation_reference_owners(operation);
            if(std::ranges::any_of(references,[&](const auto& owner){return affected.contains(owner);})) {
                affected.insert(operation.owner_id);grew=true;
            }
        }
    }
    std::map<std::string,kernel::ModelEnvelope> bounds;
    const auto& geometry=calculated.back().mesh.original_references;
    for(std::size_t triangle=0;triangle<geometry.triangle_references.size();++triangle) {
        const auto& owner=geometry.triangle_references[triangle].owner_id;
        if(owner.empty())continue;
        for(std::size_t corner=0;corner<3;++corner) {
            const auto index=geometry.triangles.at(3*triangle+corner);
            bounds[owner].include(geometry.vertices.at(index));
        }
    }
    for(const auto& edge:geometry.edges)if(!edge.reference.owner_id.empty())
        for(const auto& point:edge.points)bounds[edge.reference.owner_id].include(point);
    for(const auto& point:geometry.points)if(!point.reference.owner_id.empty())
        bounds[point.reference.owner_id].include(point.position);
    kernel::ModelEnvelope changed;
    for(const auto& owner:affected)if(const auto found=bounds.find(owner);found!=bounds.end()&&found->second.valid) {
        changed.include(found->second.minimum);changed.include(found->second.maximum);
    }
    require(changed.valid,"Native dependency probe has no changed-owner envelope");
    const auto overlaps=[&](const kernel::ModelEnvelope& other) {
        constexpr double tolerance=.001;
        return other.valid&&!(other.maximum.x+tolerance<changed.minimum.x||
            changed.maximum.x+tolerance<other.minimum.x||
            other.maximum.y+tolerance<changed.minimum.y||
            changed.maximum.y+tolerance<other.minimum.y||
            other.maximum.z+tolerance<changed.minimum.z||
            changed.maximum.z+tolerance<other.minimum.z);
    };
    std::set<std::string> spatial;
    for(const auto& [owner,envelope]:bounds)
        if(!affected.contains(owner)&&overlaps(envelope))spatial.insert(owner);
    std::cout<<"Native dependency changed="<<changed_owner<<" logical=";
    bool first=true;for(const auto& owner:affected){if(!first)std::cout<<',';std::cout<<owner;first=false;}
    std::cout<<" spatial_candidates=";
    first=true;for(const auto& owner:spatial){if(!first)std::cout<<',';std::cout<<owner;first=false;}
    std::cout<<" logical_count="<<affected.size()<<" spatial_count="<<spatial.size()<<'\n';
    return 0;
}

int benchmark_native_local_delta(const std::filesystem::path& source,
        const std::string& changed_owner,const std::string& dependent_owner) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    auto original=document::PartDocument::load(source,&previous);
    const auto changed_index=original.history_index(changed_owner);
    const auto dependent_index=original.history_index(dependent_owner);
    require(changed_index&&dependent_index&&*changed_index<*dependent_index&&
        *dependent_index<previous.size(),"Native delta owners are outside calculated history");
    auto changed=original;
    auto* definition=changed.find_container(changed_owner);
    require(definition&&definition->feature.result_type==document::ProfileResultType::Thin,
        "Native delta source is not a Thin Feature");
    definition->feature.thin_thickness+=.1;
    const auto old_operations=original.kernel_operations(false,true);
    const auto new_operations=changed.kernel_operations(false,true);
    require(old_operations.size()==new_operations.size()&&
        std::holds_alternative<kernel::FeatureGroupRequest>(old_operations[*changed_index].primitive)&&
        std::holds_alternative<kernel::FeatureGroupRequest>(old_operations[*dependent_index].primitive),
        "Native delta probe needs ordinary Feature operations");
    const auto solid=[](const TopoDS_Shape& shape) {
        return !shape.IsNull()&&TopExp_Explorer(shape,TopAbs_SOLID).More();
    };
    const auto run_boolean=[&](auto& algorithm,const TopoDS_Shape& argument,
            const TopoDS_Shape& tool,const char* failure) {
        TopTools_ListOfShape arguments,tools;arguments.Append(argument);tools.Append(tool);
        algorithm.SetArguments(arguments);algorithm.SetTools(tools);
        algorithm.SetFuzzyValue(.001);algorithm.SetToFillHistory(false);algorithm.Build();
        require(algorithm.IsDone()&&!algorithm.Shape().IsNull()&&
            BRepCheck_Analyzer(algorithm.Shape()).IsValid(),failure);
        return algorithm.Shape();
    };
    kernel::AggregatePropertyScope properties(kernel::AggregatePropertyDemand::Final);
    kernel::FaceMeasurementScope measurements(false);
    kernel::OcctKernel local_kernel;
    const auto restore_started=std::chrono::steady_clock::now();
    const auto restored=local_kernel.evaluate_history_incremental(old_operations,previous);
    const auto restore_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-restore_started).count();
    require(restored.size()==previous.size()&&restored.back().calculation_errors.empty(),
        "Native delta could not restore the old contact history");
    const auto started=std::chrono::steady_clock::now();
    auto phase_started=started;
    const auto phase=[&](const char* name) {
        const auto now=std::chrono::steady_clock::now();
        std::cerr<<"Native delta phase "<<name<<"="
            <<std::chrono::duration<double,std::milli>(now-phase_started).count()<<" ms\n";
        phase_started=now;
    };
    const auto empty=kernel::BodyResult{};
    const auto& changed_input=*changed_index?previous[*changed_index-1]:empty;
    const auto& dependent_input=previous[*dependent_index-1];
    std::cerr<<"Native delta: old changed tool\n";
    const auto old_changed_tool=local_kernel.calculate_feature_tool(
        old_operations[*changed_index],changed_input,{},changed_input,{});
    std::cerr<<"Native delta: new changed tool\n";
    const auto new_changed_tool=local_kernel.calculate_feature_tool(
        new_operations[*changed_index],changed_input,{},changed_input,{});
    std::cerr<<"Native delta: old dependent tool\n";
    const auto old_dependent_tool=local_kernel.calculate_feature_tool(
        old_operations[*dependent_index],old_changed_tool,{},dependent_input,{});
    std::cerr<<"Native delta: new dependent tool\n";
    const auto new_dependent_tool=local_kernel.calculate_feature_tool(
        new_operations[*dependent_index],new_changed_tool,{},dependent_input,{});
    phase("feature-tools");
    auto old_branch=kernel::read_calculated_body_shape(old_changed_tool);
    auto new_branch=kernel::read_calculated_body_shape(new_changed_tool);
    {
        BRepAlgoAPI_Fuse fuse;
        old_branch=run_boolean(fuse,old_branch,
            kernel::read_calculated_body_shape(old_dependent_tool),
            "Native delta old branch fuse failed");
    }
    {
        BRepAlgoAPI_Fuse fuse;
        new_branch=run_boolean(fuse,new_branch,
            kernel::read_calculated_body_shape(new_dependent_tool),
            "Native delta new branch fuse failed");
    }
    phase("branch-fuses");
    const auto old_final=kernel::read_calculated_body_shape(previous.back());
    const auto cells_started=std::chrono::steady_clock::now();
    TopoDS_Shape cell_patched;
    {
        NCollection_List<TopoDS_Shape> arguments;
        arguments.Append(old_final);arguments.Append(old_branch);arguments.Append(new_branch);
        BOPAlgo_PaveFiller filler;filler.SetArguments(arguments);filler.SetFuzzyValue(.001);filler.Perform();
        require(!filler.HasErrors(),"Native delta cell intersection failed");
        BOPAlgo_CellsBuilder cells;cells.SetArguments(arguments);cells.SetFuzzyValue(.001);
        cells.SetToFillHistory(true);cells.PerformWithFiller(filler);
        require(!cells.HasErrors(),"Native delta cell split failed");
        NCollection_List<TopoDS_Shape> take,avoid;
        take.Append(old_final);avoid.Append(old_branch);
        cells.AddToResult(take,avoid,1,false);
        take.Clear();avoid.Clear();take.Append(new_branch);
        cells.AddToResult(take,avoid,1,false);
        cells.RemoveInternalBoundaries();
        require(!cells.HasErrors()&&!cells.Shape().IsNull()&&
            BRepCheck_Analyzer(cells.Shape()).IsValid(),"Native delta cell result failed");
        cell_patched=cells.Shape();
    }
    const auto cells_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-cells_started).count();
    phase("contact-cells");
    TopoDS_Shape removed,added;
    {
        NCollection_List<TopoDS_Shape> both;both.Append(old_branch);both.Append(new_branch);
        BOPAlgo_PaveFiller filler;filler.SetArguments(both);filler.SetFuzzyValue(.001);filler.Perform();
        require(!filler.HasErrors(),"Native delta shared intersection failed");
        const auto cut=[&](const TopoDS_Shape& argument,const TopoDS_Shape& tool,const char* failure) {
            NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(argument);tools.Append(tool);
            BOPAlgo_BOP algorithm;algorithm.SetArguments(arguments);algorithm.SetTools(tools);
            algorithm.SetOperation(BOPAlgo_CUT);algorithm.PerformWithFiller(filler);
            require(!algorithm.HasErrors()&&!algorithm.Shape().IsNull()&&
                BRepCheck_Analyzer(algorithm.Shape()).IsValid(),failure);
            return algorithm.Shape();
        };
        removed=cut(old_branch,new_branch,"Native delta removal failed");
        added=cut(new_branch,old_branch,"Native delta addition failed");
    }
    phase("shared-differences");
    auto patched=old_final;
    if(solid(removed)) {
        BRepAlgoAPI_Cut cut;patched=run_boolean(cut,patched,removed,
            "Native delta final removal failed");
    }
    if(solid(added)) {
        BRepAlgoAPI_Fuse fuse;patched=run_boolean(fuse,patched,added,
            "Native delta final addition failed");
    }
    phase("final-patch");
    const auto local_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
    std::vector<kernel::BodyResult> warmed_old;
    double warm_ms{};
    const auto body_id=old_operations[*changed_index].body.id;
    require(body_id==old_operations[*dependent_index].body.id,
        "Native delta owners do not share one Body");
    const auto contact_body=[&](const std::vector<kernel::BodyResult>& boundaries)
            ->const kernel::BodyResult& {
        if(body_id.empty())return boundaries.back();
        const auto local=boundaries.back().body_boundaries.find(body_id);
        if(local!=boundaries.back().body_boundaries.end()&&!local->second.empty())
            return local->second.back();
        const auto found=boundaries.back().body_outputs.find(body_id);
        require(found!=boundaries.back().body_outputs.end(),
            "Native delta Body output is unavailable");
        return found->second.get();
    };
    const auto contact_packet_for=[&](const kernel::BodyResult& current) {
        return local_kernel.replace_additive_contact_region(current,
            {old_changed_tool,old_dependent_tool},{new_changed_tool,new_dependent_tool},
            new_operations,{changed_owner,dependent_owner},.001,
            new_operations.back().mesh_deflection);
    };
    const auto packet_started=std::chrono::steady_clock::now();
    std::optional<kernel::BodyResult> contact_packet;
    try {contact_packet=contact_packet_for(contact_body(previous));}
    catch(const std::runtime_error& error) {
        if(std::string_view(error.what())!="Contact-region ancestry is unavailable")throw;
        const auto warm_started=std::chrono::steady_clock::now();
        warmed_old=local_kernel.evaluate_history(old_operations);
        warm_ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-warm_started).count();
        require(warmed_old.size()==previous.size()&&warmed_old.back().calculation_errors.empty(),
            "Native delta could not warm the old contact history");
        contact_packet=contact_packet_for(contact_body(warmed_old));
    }
    const auto packet_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-packet_started).count()-warm_ms;
    phase("contact-packet");
    kernel::OcctKernel baseline_kernel;
    const auto baseline_started=std::chrono::steady_clock::now();
    const auto correct=baseline_kernel.evaluate_history_incremental(new_operations,previous);
    const auto baseline_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-baseline_started).count();
    require(correct.size()==previous.size()&&correct.back().calculation_errors.empty(),
        "Native delta baseline failed");
    const auto& expected_packet=contact_body(correct);
    const auto reference_key=[](const auto& reference) {
        return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+reference.instance_path;
    };
    const auto report_reference_delta=[&](std::string_view kind,const auto& before,const auto& after) {
        const auto reference_of=[](const auto& value)->const auto& {
            if constexpr(requires{value.reference;})return value.reference;
            else return value;
        };
        std::set<std::string> old_keys,new_keys;
        std::map<std::string,std::size_t> old_owners,new_owners;
        for(const auto& value:before) {
            const auto& reference=reference_of(value);
            if(!reference.valid())continue;
            if(old_keys.insert(reference_key(reference)).second)
                ++old_owners[reference.owner_id];
        }
        for(const auto& value:after) {
            const auto& reference=reference_of(value);
            if(!reference.valid())continue;
            if(new_keys.insert(reference_key(reference)).second)
                ++new_owners[reference.owner_id];
        }
        std::vector<std::string> removed,added;
        std::ranges::set_difference(old_keys,new_keys,std::back_inserter(removed));
        std::ranges::set_difference(new_keys,old_keys,std::back_inserter(added));
        std::cout<<"Native reference delta kind="<<kind
            <<" old="<<old_keys.size()<<" new="<<new_keys.size()
            <<" retained="<<(old_keys.size()-removed.size())
            <<" removed="<<removed.size()<<" added="<<added.size()<<" owners=";
        std::set<std::string> owners;
        for(const auto& [owner,count]:old_owners)owners.insert(owner);
        for(const auto& [owner,count]:new_owners)owners.insert(owner);
        bool first=true;
        for(const auto& owner:owners) {
            const auto old_count=old_owners[owner],new_count=new_owners[owner];
            if(old_count==new_count)continue;
            if(!first)std::cout<<',';
            std::cout<<owner<<':'<<old_count<<"->"<<new_count;
            first=false;
        }
        std::cout<<'\n';
    };
    report_reference_delta("face",previous.back().mesh.triangle_references,
        correct.back().mesh.triangle_references);
    report_reference_delta("edge",previous.back().mesh.edges,correct.back().mesh.edges);
    report_reference_delta("point",previous.back().mesh.points,correct.back().mesh.points);
    report_reference_delta("source-face",previous.back().mesh.original_references.triangle_references,
        correct.back().mesh.original_references.triangle_references);
    report_reference_delta("source-edge",previous.back().mesh.original_references.edges,
        correct.back().mesh.original_references.edges);
    report_reference_delta("source-point",previous.back().mesh.original_references.points,
        correct.back().mesh.original_references.points);
    report_reference_delta("source-axis",previous.back().mesh.original_references.axes,
        correct.back().mesh.original_references.axes);
    report_reference_delta("patched-face",contact_packet->mesh.triangle_references,
        expected_packet.mesh.triangle_references);
    report_reference_delta("patched-edge",contact_packet->mesh.edges,expected_packet.mesh.edges);
    report_reference_delta("patched-point",contact_packet->mesh.points,expected_packet.mesh.points);
    report_reference_delta("patched-source-face",contact_packet->mesh.original_references.triangle_references,
        expected_packet.mesh.original_references.triangle_references);
    report_reference_delta("patched-source-edge",contact_packet->mesh.original_references.edges,
        expected_packet.mesh.original_references.edges);
    report_reference_delta("patched-source-point",contact_packet->mesh.original_references.points,
        expected_packet.mesh.original_references.points);
    report_reference_delta("patched-source-axis",contact_packet->mesh.original_references.axes,
        expected_packet.mesh.original_references.axes);
    const auto expected=kernel::read_calculated_body_shape(expected_packet);
    const auto difference_volume=[&](const TopoDS_Shape& left,const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut;const auto difference=run_boolean(cut,left,right,
            "Native delta equivalence cut failed");
        GProp_GProps volume;BRepGProp::VolumeProperties(difference,volume);
        return std::abs(volume.Mass());
    };
    const auto missing=difference_volume(expected,patched);
    const auto excess=difference_volume(patched,expected);
    const auto cell_missing=difference_volume(expected,cell_patched);
    const auto cell_excess=difference_volume(cell_patched,expected);
    const auto contact_shape=kernel::read_calculated_body_shape(*contact_packet);
    const auto packet_missing=difference_volume(expected,contact_shape);
    const auto packet_excess=difference_volume(contact_shape,expected);
    GProp_GProps patched_volume,expected_volume;
    BRepGProp::VolumeProperties(patched,patched_volume);
    BRepGProp::VolumeProperties(expected,expected_volume);
    std::cout<<"Native local delta_ms="<<local_ms<<" restore_ms="<<restore_ms
        <<" warm_ms="<<warm_ms<<" packet_ms="<<packet_ms<<" baseline_ms="<<baseline_ms
        <<" missing_volume="<<missing<<" excess_volume="<<excess
        <<" cells_ms="<<cells_ms<<" cell_missing="<<cell_missing
        <<" cell_excess="<<cell_excess
        <<" packet_missing="<<packet_missing<<" packet_excess="<<packet_excess
        <<" volume_difference="<<std::abs(patched_volume.Mass()-expected_volume.Mass())
        <<" patched_volume="<<patched_volume.Mass()<<" expected_volume="<<expected_volume.Mass()
        <<std::endl;
    return 0;
}

int describe_native_operation(const std::filesystem::path& source,
        const std::string& name) {
    using namespace zima;
    const auto model=document::PartDocument::load(source);
    const auto feature=std::ranges::find_if(model.history,[&](const auto& value) {
        return value.name==name||value.id==name;
    });
    require(feature!=model.history.end(),"Named native history operation is missing");
    const auto root=model.serialized();
    const auto definition=std::ranges::find_if(root.at("history"),[&](const Json& value) {
        return value.value("id",std::string{})==feature->id;
    });
    require(definition!=root.at("history").end(),"Named native operation definition is missing");
    std::cout<<"Feature\n"<<definition->dump(2)<<std::endl;
    if(feature->feature.sketch_id.empty())return 0;
    const auto sketch=std::ranges::find(model.sketches,feature->feature.sketch_id,&sketcher::Sketch::id);
    require(sketch!=model.sketches.end(),"Named native operation Sketch is missing");
    std::cout<<"Sketch\n"<<Json::parse(sketch->serialized()).dump(2)<<std::endl;
    return 0;
}

int benchmark_native_operation_edit(const std::filesystem::path& source,
        const std::string& id,const std::string& edit_kind) {
    using namespace zima;
    std::vector<kernel::BodyResult> calculated;
    auto model=document::PartDocument::load(source,&calculated);
    const auto* stored=model.find_container(id);
    require(stored&&stored->feature_kind==document::FeatureKind::Feature,
        "Named native edit operation is not a Feature");
    const auto history_index=model.history_index(id);
    require(history_index.has_value(),"Named native edit operation has no history index");
    const auto original_feature=stored->feature;
    const auto document_id=model.document_id;
    workspace::Workspace live;
    live.add_part(std::move(model),std::move(calculated),source);
    auto value=*live.open_part(document_id)->session.document().find_container(id);
    std::optional<sketcher::Sketch> changed_sketch;
    if(edit_kind=="thickness") {
        require(value.feature.result_type==document::ProfileResultType::Thin,
            "Native thickness edit needs a thin Feature");
        value.feature.thin_thickness+=.1;
    } else if(edit_kind=="sketch-dimension") {
        const auto& document=live.open_part(document_id)->session.document();
        const auto sketch=std::ranges::find(document.sketches,value.feature.sketch_id,&sketcher::Sketch::id);
        require(sketch!=document.sketches.end(),"Native Sketch dimension edit has no owned Sketch");
        changed_sketch=*sketch;
        const auto dimension=std::ranges::find_if(changed_sketch->dimensions,[](const auto& candidate) {
            return candidate.driving&&!candidate.locked&&!candidate.suppressed;
        });
        require(dimension!=changed_sketch->dimensions.end(),"Native Sketch dimension edit has no editable dimension");
        require(changed_sketch->set_dimension_value(dimension->id,dimension->value+1.0),
            "Native Sketch dimension edit was rejected by the solver");
    } else throw std::runtime_error("Unknown native operation edit kind");
    const auto& before=live.open_part(document_id)->session.calculated_boundaries();
    std::vector<std::string> prefix;
    for(std::size_t index=0;index<std::min(*history_index,before.size());++index)
        prefix.push_back(document::serialize_body_result(before[index]).dump());
    kernel::OcctKernel kernel;
    const auto start=std::chrono::steady_clock::now();
    workspace::commit_profile(live,kernel,document_id,std::move(value),
        workspace::ProfileEditMode::Replace,changed_sketch,
        workspace::ProfileFailurePolicy::KeepDefinition);
    const auto milliseconds=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    auto* edited=live.open_part(document_id);
    require(edited,"Native operation edit closed its Part");
    const auto& after=edited->session.calculated_boundaries();
    require(after.size()==before.size(),"Native operation edit changed the boundary count");
    for(std::size_t index=0;index<prefix.size();++index)
        require(document::serialize_body_result(after[index]).dump()==prefix[index],
            "Native operation edit changed a preceding history boundary");
    const auto errors=after.empty()?0u:after.back().calculation_errors.size();
    if(std::getenv("ZIMA_CPP_TEST_PRINT_CALCULATION_ERRORS")&&!after.empty())
        for(const auto& [owner,message]:after.back().calculation_errors)
            std::cerr<<"Native operation error owner="<<owner<<" message="<<message<<'\n';
    require(edited->session.undo(),"Native operation edit did not create an Undo transaction");
    require(edited->session.document().find_container(id)->feature==original_feature,
        "Undo did not restore the native Feature definition");
    std::cout<<"Native operation edit_ms="<<milliseconds
        <<" history_index="<<*history_index<<" boundaries="<<after.size()
        <<" errors="<<errors<<" kind="<<edit_kind<<std::endl;
    return 0;
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
