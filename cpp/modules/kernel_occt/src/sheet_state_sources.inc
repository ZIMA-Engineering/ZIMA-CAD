// Source material is immutable. State changes rebuild from these authored
// contributions, never from a previously fitted state result. This cache is
// disposable: an explicit cold calculation reconstructs it from native history.
namespace sheet_state_sources {
struct Contribution {
    PrimitiveData material;
    std::vector<SheetMaterialDefinition> regions;
    BooleanOperation operation{BooleanOperation::Add};
    std::shared_ptr<const Contribution> previous;
    // Newly created sheet material belongs to that creator even where its
    // Boolean joining cap inherits a face from the preceding sheet region.
    std::string created_region;
};
using Sources=std::shared_ptr<const Contribution>;

PrimitiveData difference(const PrimitiveData& a,const PrimitiveData& b,
        const std::string& owner) {
    if(a.shape.IsNull()||b.shape.IsNull())return a;
    BRepAlgoAPI_Cut cut;set_boolean_inputs(cut,a.shape,b.shape);
    cut.SetToFillHistory(true);cut.SetFuzzyValue(1e-7);cut.Build();
    if(!cut.IsDone())throw std::runtime_error("Cannot retain the authored sheet material change.");
    PrimitiveData result;result.shape=cut.Shape();
    result.faces=propagate_topology(cut,a.faces,b.faces);
    result.edges=complete_boolean_edges(result.shape,result.faces,
        propagate_topology(cut,a.edges,b.edges),owner,"sheet-source");
    result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,
        propagate_topology(cut,a.vertices,b.vertices),owner,"sheet-source");
    return result;
}
std::vector<sheet_material::Transition> transitions_to(const Contribution& source,
        const std::vector<SheetMaterialDefinition>& target) {
    std::vector<sheet_material::Transition> transitions;
    for(const auto& before:source.regions) {
        const auto after=std::ranges::find(target,before.owner_id,&SheetMaterialDefinition::owner_id);
        if(after!=target.end()&&*after!=before)transitions.push_back({before,*after});
    }
    return transitions;
}
Sources capture(Sources sources,const PrimitiveData& before,const PrimitiveData& after,
        const std::vector<SheetMaterialDefinition>& regions,const HistoryOperation& operation,double tolerance) {
    if(operation.suppressed||std::holds_alternative<SheetStateRequest>(operation.primitive)||regions.empty())return sources;
    if(!sources&&!before.shape.IsNull())sources=std::make_shared<Contribution>(Contribution{before,{},BooleanOperation::Add,{}});
    const auto domains=sources;
    const auto retain=[&](PrimitiveData material,BooleanOperation mode,std::string region) {
        if(material.shape.IsNull()||!TopExp_Explorer(material.shape,TopAbs_SOLID).More())return;
        if(!BRepCheck_Analyzer(material.shape).IsValid())throw std::runtime_error("Authored sheet material change is invalid.");
        if(region.empty()) {
            std::set<std::string> owners;
            for(const auto& face:material.faces)if(!face.reference.sheet_owner.empty()&&
                (face.reference.sheet_role==SheetFaceRole::SideA||face.reference.sheet_role==SheetFaceRole::SideB))owners.insert(face.reference.sheet_owner);
            if(owners.size()==1)region=*owners.begin();
        }
        if(!region.empty())for(auto& face:material.faces) {
            if(!face.reference.sheet_owner.empty()&&face.reference.sheet_owner!=region)
                face.reference.sheet_role=SheetFaceRole::ThicknessFace;
            face.reference.sheet_owner=region;
        }
        sources=std::make_shared<Contribution>(Contribution{std::move(material),regions,mode,sources,std::move(region)});
    };
    const bool treatment=std::holds_alternative<FilletRequest>(operation.primitive)||
        std::holds_alternative<ChamferRequest>(operation.primitive)||std::holds_alternative<ShellRequest>(operation.primitive);
    if(operation.operation==BooleanOperation::Subtract||treatment) {
        auto remaining=difference(before,after,operation.owner_id);
        // Removed material may be completely enclosed by the thickness and
        // have no original skin left on its boundary. Intersect with authored
        // positive material domains; face adjacency alone cannot identify it.
        for(auto domain=domains;domain&&TopExp_Explorer(remaining.shape,TopAbs_SOLID).More();domain=domain->previous) {
            if(domain->operation!=BooleanOperation::Add||domain->created_region.empty())continue;
            auto material=sheet_state_geometry::transform(domain->material,transitions_to(*domain,regions),
                tolerance,operation.owner_id,domain->created_region);
            Bnd_Box a,b;BRepBndLib::Add(remaining.shape,a);BRepBndLib::Add(material.shape,b);if(a.IsOut(b))continue;
            BRepAlgoAPI_Common common;set_boolean_inputs(common,remaining.shape,material.shape);
            common.SetToFillHistory(true);common.SetFuzzyValue(1e-7);common.Build();
            if(!common.IsDone())throw std::runtime_error("Cannot attribute the removed sheet material.");
            PrimitiveData piece;piece.shape=common.Shape();if(!TopExp_Explorer(piece.shape,TopAbs_SOLID).More())continue;
            piece.faces=propagate_topology(common,remaining.faces,material.faces);
            piece.edges=complete_boolean_edges(piece.shape,piece.faces,propagate_topology(common,remaining.edges,material.edges),operation.owner_id,"sheet-domain");
            piece.vertices=complete_boolean_vertices(piece.shape,piece.faces,piece.edges,propagate_topology(common,remaining.vertices,material.vertices),operation.owner_id,"sheet-domain");
            remaining=difference(remaining,piece,operation.owner_id);
            retain(std::move(piece),BooleanOperation::Subtract,domain->created_region);
        }
        retain(std::move(remaining),BooleanOperation::Subtract,{});
    }
    if(operation.operation==BooleanOperation::Add||treatment)
        retain(difference(after,before,operation.owner_id),BooleanOperation::Add,
            operation.sheet_material?operation.sheet_material->owner_id:std::string{});
    return sources;
}

PrimitiveData calculate(Sources sources,const std::vector<SheetMaterialDefinition>& target,
        double tolerance,const std::string& owner) {
    std::vector<Sources> ordered;for(auto source=sources;source;source=source->previous)ordered.push_back(source);
    PrimitiveData result;
    for(auto it=ordered.rbegin();it!=ordered.rend();++it) {
        const auto& source=**it;const auto transitions=transitions_to(source,target);
        auto material=sheet_state_geometry::transform(source.material,transitions,tolerance,owner,source.created_region);
        if(result.shape.IsNull()){result=std::move(material);continue;}
        const auto combine=[&](auto& algorithm) {
            set_boolean_inputs(algorithm,result.shape,material.shape);algorithm.SetToFillHistory(true);
            algorithm.SetFuzzyValue(tolerance/8);algorithm.Build();
            if(!algorithm.IsDone())throw std::runtime_error("Cannot reconstruct sheet material in the requested state.");
            result.faces=propagate_topology(algorithm,result.faces,material.faces);
            result.edges=propagate_topology(algorithm,result.edges,material.edges);
            result.vertices=propagate_topology(algorithm,result.vertices,material.vertices);result.shape=algorithm.Shape();
        };
        if(source.operation==BooleanOperation::Subtract){BRepAlgoAPI_Cut cut;combine(cut);}
        else {BRepAlgoAPI_Fuse fuse;combine(fuse);}
    }
    if(result.shape.IsNull()||!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("Reconstructed sheet state is not a valid solid.");
    result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,owner,"sheet-state");
    result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,result.vertices,owner,"sheet-state");
    sheet_state_geometry::identify(result,owner);
    return result;
}
} // namespace sheet_state_sources
