// Included after native profile-end ancestry helpers. No UI or persistence work
// belongs here: inputs are resolved Feature targets at the calculation boundary.
detail::OrderedLimitResult prepare_ordered_up_to(const OrderedUpToLimit& definition,
        const std::vector<std::optional<TopoDS_Face>>& exact,const TopoDS_Shape& profile,
        double tolerance,const std::optional<gp_Ax1>& axis=std::nullopt) {
    using namespace detail;
    if(definition.patches.empty()||definition.patches.size()!=exact.size()||
            !std::isfinite(definition.path_correction))
        throw OrderedLimitError(OrderedLimitFailure::InvalidInput,0,0);
    Bnd_Box envelope;BRepBndLib::AddOptimal(profile,envelope,false,false);
    if(axis&&!envelope.IsVoid()) {
        double x0,y0,z0,x1,y1,z1;envelope.Get(x0,y0,z0,x1,y1,z1);
        for(unsigned mask=0;mask<8;++mask) {
            const gp_Pnt p(mask&1?x1:x0,mask&2?y1:y0,mask&4?z1:z0);
            const gp_Vec n(axis->Direction()),delta(axis->Location(),p);
            const auto center=axis->Location().Translated(n*delta.Dot(n));
            const double radius=center.Distance(p);
            envelope.Add(gp_Pnt(center.X()-radius,center.Y()-radius,center.Z()-radius));
            envelope.Add(gp_Pnt(center.X()+radius,center.Y()+radius,center.Z()+radius));
        }
    }
    std::vector<OrderedLimitInput> sources;
    for(std::size_t row=0;row<definition.patches.size();++row) {
        const auto& patch=definition.patches[row];const auto& target=patch.target;
        if(!patch.source_reference.valid()||!target.reference.valid()||
                !target.reference.instance_path.empty())
            throw OrderedLimitError(OrderedLimitFailure::InvalidInput,row,row);
        TopoDS_Face authored;
        if(exact[row])authored=*exact[row];
        else if(target.planar&&target.datum) {
            // A datum has no authored finite patch from which to infer a side.
            if(definition.patches.size()>1&&((row&&!patch.keep_previous_positive)||
                (row+1<definition.patches.size()&&!patch.keep_next_positive)))
                throw OrderedLimitError(OrderedLimitFailure::AmbiguousRetainedPatch,row,row);
            const gp_Pln plane(gp_Pnt(target.origin.x,target.origin.y,target.origin.z),
                gp_Dir(target.normal.x,target.normal.y,target.normal.z));
            authored=BRepBuilderAPI_MakeFace(plane,-1.,1.,-1.,1.).Face();
        } else throw OrderedLimitError(OrderedLimitFailure::InvalidInput,row,row);
        BRepBndLib::AddOptimal(authored,envelope,false,false);
        sources.push_back({authored,patch.keep_previous_positive,patch.keep_next_positive});
    }
    double x0,y0,z0,x1,y1,z1;envelope.Get(x0,y0,z0,x1,y1,z1);
    const double diagonal=std::hypot(std::hypot(x1-x0,y1-y0),z1-z0);
    envelope.Enlarge(std::max(1.,2*diagonal+(axis?0.:std::abs(definition.path_correction))));
    return build_ordered_up_to(sources,envelope,std::max(tolerance,Precision::Confusion()));
}

FaceReference ordered_up_to_cap(const FaceReference& cap,const FaceReference& source) {
    auto result=cap;result.semantic_key+=":limit";
    for(const auto& text:{source.owner_id,source.semantic_key,source.instance_path})
        result.semantic_key+=":"+std::to_string(text.size())+":"+text;
    return result;
}

void record_ordered_up_to_resolution(std::vector<ResolvedOrderedUpToLimit>* output,
        const OrderedUpToLimit& definition,const detail::OrderedLimitResult& result,
        const std::string& owner,const std::string& region) {
    if(!output)return;
    bool seeded=definition.patches.size()>1&&!definition.primary_cap_source;
    for(std::size_t row=0;row<definition.patches.size();++row) {
        seeded|=(row&&result.patches[row].keep_previous_positive&&!definition.patches[row].keep_previous_positive)||
            (row+1<definition.patches.size()&&result.patches[row].keep_next_positive&&!definition.patches[row].keep_next_positive);
    }
    if(!seeded)return;
    ResolvedOrderedUpToLimit resolved{owner,region,{}};
    if(definition.patches.size()>1)resolved.primary_cap_source=definition.primary_cap_source?
        definition.primary_cap_source:std::optional<FaceReference>{definition.patches.front().source_reference};
    for(std::size_t row=0;row<definition.patches.size();++row) {
        if(row&&result.patches[row].keep_previous_positive)resolved.retained_patches.push_back({definition.patches[row].source_reference,
            definition.patches[row-1].source_reference,*result.patches[row].keep_previous_positive});
        if(row+1<definition.patches.size()&&result.patches[row].keep_next_positive)resolved.retained_patches.push_back({definition.patches[row].source_reference,
            definition.patches[row+1].source_reference,*result.patches[row].keep_next_positive});
    }
    output->push_back(std::move(resolved));
}

double ordered_up_to_linear_span(const detail::OrderedLimitResult& limit,
        const TopoDS_Shape& profile,const gp_Pnt& origin,const gp_Dir& direction) {
    gp_Trsf local;local.SetTransformation(gp_Ax3(origin,direction));
    Bnd_Box footprint;BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(profile,local,false).Shape(),footprint,false,false);
    double x0,y0,z0,x1,y1,z1;footprint.Get(x0,y0,z0,x1,y1,z1);
    double maximum=-std::numeric_limits<double>::infinity();
    for(const auto& patch:limit.patches) {
        BRepAdaptor_Surface support(patch.face,false);
        if(support.GetType()==GeomAbs_Plane) {
            const auto plane=support.Plane().Transformed(local);double a,b,c,d;plane.Coefficients(a,b,c,d);
            if(std::abs(c)>1e-12) {
                // An affine height reaches its maximum at one of the exact
                // footprint-box corners. Extended remote corners of the plane
                // must not determine the auxiliary prism's draft height.
                for(double x:{x0,x1})for(double y:{y0,y1})maximum=std::max(maximum,-(a*x+b*y+d)/c);
                continue;
            }
        }
        Bnd_Box bounds;BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(patch.face,local,false).Shape(),bounds,false,false);
        double px0,py0,pz0,px1,py1,pz1;bounds.Get(px0,py0,pz0,px1,py1,pz1);maximum=std::max(maximum,pz1);
    }
    if(!std::isfinite(maximum)||maximum<=Precision::Confusion())
        throw detail::OrderedLimitError(detail::OrderedLimitFailure::EmptyPatch,0,limit.patches.size()-1);
    return maximum+std::max(1.,maximum*.01);
}

void clip_ordered_up_to_profile(PrimitiveData& result,const detail::OrderedLimitResult& limit,
        const OrderedUpToLimit& definition,const TopoDS_Shape& first,const TopoDS_Shape& last,
        const gp_Pnt& keep,const FaceReference& cap,const std::string& role,bool surface,double tolerance,
        const TopoDS_Shape& terminal_window={}) {
    BRepPrimAPI_MakeHalfSpace half(limit.shell,keep);
    std::vector<OwnedFace> named;
    for(std::size_t row=0;row<limit.patches.size();++row)
        named.push_back({limit.patches[row].face,definition.patches.size()==1||definition.patches[row].source_reference==
            definition.primary_cap_source.value_or(definition.patches.front().source_reference)?cap:
            ordered_up_to_cap(cap,definition.patches[row].source_reference)});
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> operation;
    if(terminal_window.IsNull()) {
        operation=std::make_unique<BRepAlgoAPI_Common>();
        set_boolean_inputs(*operation,result.shape,half.Solid());
    } else {
        // Beyond half a turn the limiting half-space also crosses an earlier
        // orbit segment. Remove only the excess within the terminal angular
        // window; the preceding sweep must remain intact.
        BRepAlgoAPI_Cut excess;set_boolean_inputs(excess,terminal_window,half.Solid());
        excess.SetFuzzyValue(tolerance);excess.SetToFillHistory(true);excess.SetNonDestructive(true);excess.Build();
        if(!excess.IsDone()||excess.Shape().IsNull()||!BRepCheck_Analyzer(excess.Shape()).IsValid())
            throw detail::OrderedLimitError(detail::OrderedLimitFailure::DisconnectedChain,0,limit.patches.size()-1);
        named=propagate_topology(excess,std::vector<OwnedFace>{},named);
        operation=std::make_unique<BRepAlgoAPI_Cut>();
        set_boolean_inputs(*operation,result.shape,excess.Shape());
    }
    auto& clip=*operation;
    clip.SetFuzzyValue(tolerance);clip.SetToFillHistory(true);clip.SetNonDestructive(true);clip.Build();
    if(!clip.IsDone()||clip.Shape().IsNull()||!BRepCheck_Analyzer(clip.Shape()).IsValid())
        throw detail::OrderedLimitError(detail::OrderedLimitFailure::DisconnectedChain,0,limit.patches.size()-1);
    TopTools_IndexedMapOfShape actual;TopExp::MapShapes(clip.Shape(),actual);
    // The complete start must survive and no overrun cap may remain. This is
    // required for finite domains too; a partly covered profile is not success.
    for(TopExp_Explorer it(first,surface?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(!actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw detail::OrderedLimitError(detail::OrderedLimitFailure::DisconnectedChain,0,limit.patches.size()-1);
    for(TopExp_Explorer it(last,surface?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw detail::OrderedLimitError(detail::OrderedLimitFailure::DisconnectedChain,0,limit.patches.size()-1);
    locate_clipped_profile_end(clip,result,cap.owner_id,role);
    result.faces=propagate_topology(clip,result.faces,named);
    result.edges=propagate_topology(clip,result.edges,{});result.vertices=propagate_topology(clip,result.vertices,{});
    std::erase_if(result.faces,[&](const auto& value){return !actual.Contains(value.shape);});
    std::erase_if(result.edges,[&](const auto& value){return !actual.Contains(value.shape);});
    std::erase_if(result.vertices,[&](const auto& value){return !actual.Contains(value.shape);});
    result.shape=clip.Shape();
    // Newly cut rims have no propagated edge parent. Complete their identity
    // from the retained adjacent faces using the ordinary Boolean contract,
    // before publishing either the native operand or its transient preview.
    result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,cap.owner_id,role,named,result.vertices);
    result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,result.vertices,cap.owner_id,role);
}
