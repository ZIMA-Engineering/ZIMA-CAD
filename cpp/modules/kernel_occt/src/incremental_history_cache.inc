// Included inside evaluate_flat_history: operation-local capture/restore owns no
// document mutations and is used only during an explicit calculation.
bool native_history_enabled=!context.preview_wire&&!has_forms&&std::ranges::none_of(operations,[](const auto& operation) {
    return std::holds_alternative<ThreadSurfaceRequest>(operation.primitive)||
        std::holds_alternative<SheetStateRequest>(operation.primitive);
});
std::vector<KernelHistoryFrame> native_frames;
if(native_history_enabled&&!previous_boundaries.empty()) {
    const auto& saved=previous_boundaries.back().incremental_history;
    // The suffix hash records the exact producer build for diagnostics.  The
    // persisted continuation representation itself is versioned by the
    // history-vN family.  Ordinary kernel implementation changes must not
    // discard a valid saved prefix: every frame still has to match the exact
    // authored-operation fingerprint and pass the native binding checks below.
    // Bump the family when that representation or its identity semantics
    // become incompatible (for example together with an OCCT/schema update).
    constexpr std::string_view compatible_history_family{"history-v1/"};
    const bool compatible=saved&&saved->algorithm.starts_with(compatible_history_family);
    if(compatible)native_frames=saved->frames.get();
    if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
        std::fprintf(stderr,"[native history loaded] enabled=%d saved=%d algorithm=%d frames=%zu previous=%zu\n",
            int(native_history_enabled),int(bool(saved)),int(compatible),
            native_frames.size(),previous_boundaries.size());
}
if(native_history_enabled) {
    native_frames.resize(operations.size());
    // Prefer an exact frame retained by this kernel. The persisted packet owns
    // only the current branch, while the bounded live cache can also retain a
    // previously visited value for fast and byte-stable Undo/Redo.
    for(std::size_t index=0;index<operations.size();++index) {
        const auto key=fingerprint(operations,index+1);
        const auto found=live_cache_->history_frames.find(key);
        if(found!=live_cache_->history_frames.end()&&
                found->second.original_edges_retained==retain_original_edges)
            native_frames[index]=found->second;
    }
}
std::shared_ptr<const OriginalFaces> native_original_before;
std::optional<HistoryReferenceCounts> native_reference_before;
const auto capture_native_frame=[&](const std::string& key,const TopoDS_Shape& shape,
        const LiveCache::Topology& topology) {
    if(!native_history_enabled||!native_reference_before)return;
    if(const auto retained=live_cache_->history_frames.find(key);
            retained!=live_cache_->history_frames.end()&&
            retained->second.original_edges_retained==retain_original_edges) {
        std::erase(live_cache_->history_frame_order,key);
        live_cache_->history_frame_order.push_back(key);
        native_frames[current_operation]=retained->second;
        return;
    }
    auto delta=native_history_reference_delta(original_references,*native_reference_before);
    if(!delta){native_history_enabled=false;return;}
    KernelHistoryFrame frame;frame.fingerprint=key;frame.original_edges_retained=retain_original_edges;
    frame.reference_delta=std::make_shared<const ViewerReferenceGeometry>(std::move(*delta));
    // Ordinary boundary packets already own this exact native archive and its
    // bindings. Reference them instead of serializing/storing the body again.
    const auto* existing=!boundaries.empty()&&boundaries.back().source_fingerprint==key&&
        !boundaries.back().kernel_shape.empty()&&boundaries.back().kernel_shape!=kernel_body_compound&&
        boundaries.back().kernel_placements.empty()?&boundaries.back():nullptr;
    bool use_existing=existing!=nullptr;
    if(existing)for(const auto& hidden:topology.hidden_display_edges) {
        const auto owned=std::ranges::find_if(topology.edges,[&](const auto& edge){return edge.shape.IsSame(hidden);});
        if(owned==topology.edges.end()){use_existing=false;break;}
        const auto count=std::ranges::count_if(existing->kernel_edges,[&](const auto& edge){return edge.reference==owned->reference;});
        if(count!=1){use_existing=false;break;}
        const auto binding=std::ranges::find_if(existing->kernel_edges,[&](const auto& edge){return edge.reference==owned->reference;});
        frame.hidden_edges.push_back(binding->locator);
    }
    if(!use_existing) {
        PrimitiveData body;body.shape=shape;body.faces=topology.faces;body.edges=topology.edges;body.vertices=topology.vertices;
        auto native=snapshot_native_history_data(body,&topology.hidden_display_edges,&frame.hidden_edges);
        native.source_fingerprint=key;frame.native_body=std::move(native);
    }
    for(auto layer=context.original_faces;layer!=native_original_before;layer=layer->previous) {
        if(!layer){native_history_enabled=false;return;}
        frame.original_layers.emplace_back(snapshot_native_history_sources(layer->faces,layer->edges,layer->vertices));
    }
    std::ranges::reverse(frame.original_layers);
    const auto owner=operations[current_operation].owner_id;
    const auto source=context.source_solids.find(owner);
    if(source!=context.source_solids.end()) {
        const auto cached=live_cache_->copy_sources.find(source->second.source_fingerprint);
        if(cached!=live_cache_->copy_sources.end()) {
            if(!cached->second.forms.empty()){native_history_enabled=false;return;}
            const auto& operand=cached->second.operand;
            frame.copy_source=KernelHistoryCopy{snapshot_native_history_data(operand),
                snapshot_native_history_sources(operand.source_caps),cached->second.solid,
                operand.empty_material_operand,operand.reference_only};
        }
    }
    auto [retained,inserted]=live_cache_->history_frames.insert_or_assign(key,frame);
    static_cast<void>(retained);
    if(!inserted)std::erase(live_cache_->history_frame_order,key);
    live_cache_->history_frame_order.push_back(key);
    while(live_cache_->history_frame_order.size()>kLiveShapeCacheLimit) {
        live_cache_->history_frames.erase(live_cache_->history_frame_order.front());
        live_cache_->history_frame_order.pop_front();
    }
    native_frames[current_operation]=std::move(frame);
};
const auto restore_native_prefix=[&](std::size_t count) {
    const auto fail=[&](const char* reason) {
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
            std::fprintf(stderr,"[native history restore] prefix=%zu failed=%s frames=%zu previous=%zu\n",
                count,reason,native_frames.size(),previous_boundaries.size());
        return false;
    };
    if(!native_history_enabled)return fail("disabled");
    if(!count)return fail("empty-prefix");
    if(count>native_frames.size())return fail("missing-frames");
    if(count>previous_boundaries.size())return fail("missing-boundaries");
    const auto& prefix_key=previous_boundaries[count-1].source_fingerprint;
    const bool references_available=(context.requested_solids.empty()&&!has_subtractions)||
        live_cache_->copy_references.contains(prefix_key);
    const bool operands_available=std::ranges::all_of(operations.begin(),operations.begin()+count,[&](const auto& operation) {
        if(!context.requested_solids.contains(operation.owner_id))return true;
        const auto index=static_cast<std::size_t>(&operation-operations.data());
        return live_cache_->copy_sources.contains(previous_boundaries[index].source_fingerprint+":solid:"+operation.owner_id);
    });
    // Native shapes and reference packets have independent bounded lifetimes.
    // A long suffix can evict its input packet while its native shape remains.
    // Restore the missing capabilities instead of claiming complete reuse.
    if(live_cache_->boundaries.contains(prefix_key)&&references_available&&operands_available)return true;
    // Validate all archive addresses and fingerprints before publishing any live
    // cache entry. A miss retains the ordinary full calculation path.
    try {
        std::shared_ptr<const OriginalFaces> originals;
        ViewerReferenceGeometry references;
        std::vector<std::pair<std::string,LiveCache::CopySource>> copies;
        for(std::size_t index=0;index<count;++index) {
            const auto& frame=native_frames[index];const auto& boundary=previous_boundaries[index];
            if(frame.fingerprint!=boundary.source_fingerprint)return fail("fingerprint");
            if(!frame.reference_delta)return fail("reference-delta");
            if(!boundary.calculation_errors.empty())return fail("calculation-error");
            if(!frame.native_body->kernel_shape.empty()) {
                if(frame.native_body->source_fingerprint!=frame.fingerprint)return fail("native-fingerprint");
                if(!boundary.kernel_shape.empty()&&boundary.kernel_shape.get()!=frame.native_body->kernel_shape.get())return fail("native-archive");
            }
            for(const auto& layer:frame.original_layers) {
                auto data=restore_native_history_data(layer);
                originals=std::make_shared<OriginalFaces>(OriginalFaces{std::move(data.faces),originals,
                    std::move(data.edges),std::move(data.vertices)});
            }
            append_reference_geometry(references,*frame.reference_delta);
            if(frame.copy_source) {
                const auto& source=*frame.copy_source;
                auto operand=restore_native_history_data(source.operand);
                operand.source_caps=restore_native_history_data(source.source_caps).faces;
                operand.empty_material_operand=source.empty_material_operand;operand.reference_only=source.reference_only;
                auto solid=source.solid.get();const auto owner=operations[index].owner_id;
                const auto key=frame.fingerprint+":solid:"+owner;solid.source_fingerprint=key;
                copies.emplace_back(key,LiveCache::CopySource{std::move(operand),std::move(solid),{}});
            }
        }
        const auto& frame=native_frames[count-1];std::vector<TopoDS_Shape> hidden;
        const auto& snapshot=frame.native_body->kernel_shape.empty()?previous_boundaries[count-1]:frame.native_body.get();
        auto body=restore_native_history_data(snapshot,&frame.hidden_edges,&hidden);
        if(body.shape.IsNull())return fail("empty-shape");
        auto topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
            std::move(body.faces),std::move(body.edges),std::move(body.vertices),std::move(hidden)});
        for(auto& [key,copy]:copies) {
            const auto [it,inserted]=live_cache_->copy_sources.insert_or_assign(key,std::move(copy));
            if(inserted)live_cache_->copy_source_order.push_back(key);
        }
        const auto key=frame.fingerprint;
        live_cache_->copy_references.insert_or_assign(key,std::move(references));
        std::erase(live_cache_->copy_reference_order,key);live_cache_->copy_reference_order.push_back(key);
        live_cache_->boundaries.insert_or_assign(key,LiveCache::Boundary{body.shape,std::move(topology),
            std::move(originals),nullptr,frame.original_edges_retained});
        std::erase(live_cache_->insertion_order,key);live_cache_->insertion_order.push_back(key);
        while(live_cache_->copy_source_order.size()>kLiveShapeCacheLimit) {
            live_cache_->copy_sources.erase(live_cache_->copy_source_order.front());live_cache_->copy_source_order.pop_front();
        }
        while(live_cache_->copy_reference_order.size()>kLiveShapeCacheLimit) {
            live_cache_->copy_references.erase(live_cache_->copy_reference_order.front());live_cache_->copy_reference_order.pop_front();
        }
        while(live_cache_->insertion_order.size()>kLiveShapeCacheLimit) {
            live_cache_->boundaries.erase(live_cache_->insertion_order.front());live_cache_->insertion_order.pop_front();
        }
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[native history restore] operations="<<operations.size()<<" prefix="<<count<<" modeling=0\n";
        return true;
    }catch(const Standard_Failure&) {
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[native history restore] invalid archive; calculate normally\n";
    }catch(const std::runtime_error&) {
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[native history restore] invalid bindings; calculate normally\n";
    }
    return false;
};

const auto attach_native_history=[&](std::vector<BodyResult>& result) {
    if(!native_history_enabled||result.empty()||native_frames.size()!=result.size())return;
    for(std::size_t index=0;index<result.size();++index)
        if(!native_frames[index].reference_delta||native_frames[index].fingerprint!=result[index].source_fingerprint)return;
    auto cache=std::make_shared<KernelHistoryCache>();cache->algorithm=std::string(native_history_algorithm);
    cache->frames=std::move(native_frames);result.back().incremental_history=std::move(cache);
};
