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
