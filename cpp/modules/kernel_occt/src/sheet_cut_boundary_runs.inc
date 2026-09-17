// A sampled envelope may split one authored boundary into many adjacent arcs.
// Consolidate those arcs before they become persisted edges and normal walls.
// Distinct runs are identified by their ordered semantic neighbours, never by
// a sampled depth, traversal index or geometric coordinate.
PrimitiveData sheet_cut_join_boundary_runs(const PrimitiveData& input,
        const FaceReference& source,double accuracy,double tolerance) {
    PrimitiveData result;if(input.shape.IsNull())return result;
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    std::set<std::string> identities;
    TopTools_IndexedMapOfShape all_edges;TopExp::MapShapes(input.shape,TopAbs_EDGE,all_edges);
    // Each join consumes at most this share of one percent of the physical
    // deviation budget. Joining before and after margin leaves two percent
    // reserved for these operations; the margin receives the remaining 98%.
    const double join_tolerance=std::min(tolerance,accuracy/(100.*std::max(1,all_edges.Extent())));
    for(TopExp_Explorer faces(input.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());
        const auto forward=TopoDS::Face(face.Oriented(TopAbs_FORWARD));
        const auto outer=BRepTools::OuterWire(forward);
        const auto surface=BRep_Tool::Surface(face);const BRepAdaptor_Surface support(face);
        double u_scale=1;
        if(support.GetType()==GeomAbs_Cylinder)
            u_scale=std::max(1.,support.Cylinder().Radius()+source.sheet_thickness);
        else if(support.GetType()==GeomAbs_Cone) {
            double u0=0,u1=0,v0=0,v1=0;BRepTools::UVBounds(face,u0,u1,v0,v1);
            const auto cone=support.Cone();u_scale=std::max({1.,
                std::abs(cone.RefRadius()+v0*std::sin(cone.SemiAngle()))+source.sheet_thickness,
                std::abs(cone.RefRadius()+v1*std::sin(cone.SemiAngle()))+source.sheet_thickness});
        }
        TopoDS_Face target;builder.MakeFace(target,surface,tolerance);
        struct Expected {Handle(Geom2d_Curve) curve;EdgeReference reference;};
        std::vector<Expected> expected;
        struct Endpoint {TopoDS_Vertex source;TopoDS_Vertex replacement;};
        std::vector<Endpoint> endpoints;
        const auto endpoint=[&](const TopoDS_Vertex& original,const gp_Pnt2d& uv) {
            if(original.IsNull())throw std::runtime_error("Sheet Cut is missing a material-boundary endpoint.");
            const auto point=BRep_Tool::Pnt(original);
            const double gap=point.Distance(surface->Value(uv.X(),uv.Y()));
            const double inherited=BRep_Tool::Tolerance(original);
            const double endpoint_tolerance=std::max(inherited,gap+join_tolerance);
            if(gap>inherited+join_tolerance||endpoint_tolerance>accuracy)
                throw std::runtime_error("Sheet Cut cannot preserve a material-boundary endpoint within its permitted deviation.");
            const auto found=std::ranges::find_if(endpoints,[&](const auto& item){return item.source.IsSame(original);});
            if(found!=endpoints.end()) {
                builder.UpdateVertex(found->replacement,endpoint_tolerance);
                return found->replacement;
            }
            TopoDS_Vertex vertex;builder.MakeVertex(vertex,point,endpoint_tolerance);
            endpoints.push_back({original,vertex});return vertex;
        };
        struct Piece {TopoDS_Edge edge;std::string parents;};
        struct Run {std::string parents;std::vector<TopoDS_Edge> edges;};
        for(TopExp_Explorer wires(forward,TopAbs_WIRE);wires.More();wires.Next()) {
            std::vector<Piece> pieces;
            for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),forward);edges.More();edges.Next()) {
                std::set<std::pair<std::string,std::string>> parents;
                for(const auto& edge:input.edges)if(edge.shape.IsSame(edges.Current()))
                    parents.emplace(edge.reference.owner_id,edge.reference.semantic_key);
                if(parents.empty())throw std::runtime_error("Sheet Cut cannot consolidate an unowned material boundary.");
                std::string key;
                for(const auto& [owner,parent]:parents)key+=token(owner)+":"+token(parent)+":";
                pieces.push_back({edges.Current(),std::move(key)});
            }
            if(pieces.empty())throw std::runtime_error("Sheet Cut has an empty material boundary.");
            std::size_t start=0;
            for(std::size_t i=0;i<pieces.size();++i)
                if(pieces[i].parents!=pieces[(i+pieces.size()-1)%pieces.size()].parents){start=i;break;}
            std::vector<Run> runs;
            for(std::size_t step=0;step<pieces.size();++step) {
                const auto& piece=pieces[(start+step)%pieces.size()];
                if(runs.empty()||runs.back().parents!=piece.parents)runs.push_back({piece.parents,{}});
                runs.back().edges.push_back(piece.edge);
            }
            BRepBuilderAPI_MakeWire wire;
            for(std::size_t i=0;i<runs.size();++i) {
                const auto& run=runs[i];
                std::string key="sheetcut-material-run:source:"+token(source.owner_id)+":"+
                    token(source.semantic_key)+":loop:"+(wires.Current().IsSame(outer)?"outer:":"hole:")+
                    "parent:"+token(run.parents)+":";
                if(runs.size()==1)key+="closed";
                else key+="from:"+token(runs[(i+runs.size()-1)%runs.size()].parents)+":to:"+
                    token(runs[(i+1)%runs.size()].parents);
                if(!identities.insert(key).second)
                    throw std::runtime_error("Sheet Cut has ambiguous material-boundary ancestry.");
                Geom2dConvert_CompCurveToBSplineCurve joined;
                for(const auto& edge:run.edges) {
                    double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(edge,forward,first,last);
                    if(curve.IsNull())throw std::runtime_error("Sheet Cut is missing a material boundary curve.");
                    auto part=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve,first,last));
                    part=Handle(Geom2d_BSplineCurve)::DownCast(part->Copy());
                    if(edge.Orientation()==TopAbs_REVERSED)part->Reverse();
                    for(int pole=1;pole<=part->NbPoles();++pole) {
                        const auto point=part->Pole(pole);part->SetPole(pole,gp_Pnt2d(point.X()*u_scale,point.Y()));
                    }
                    if(!joined.Add(part,join_tolerance,true))
                        throw std::runtime_error("Sheet Cut cannot join a material boundary within its permitted deviation.");
                }
                auto curve=joined.BSplineCurve();
                for(int pole=1;pole<=curve->NbPoles();++pole) {
                    const auto point=curve->Pole(pole);curve->SetPole(pole,gp_Pnt2d(point.X()/u_scale,point.Y()));
                }
                // Preserve the original wire's shared endpoint topology and
                // tolerances. Independent fresh vertices can disconnect a
                // valid Boolean boundary whose pcurves meet within tolerance.
                const auto first_vertex=endpoint(TopExp::FirstVertex(run.edges.front(),true),curve->StartPoint());
                const auto last_vertex=endpoint(TopExp::LastVertex(run.edges.back(),true),curve->EndPoint());
                BRepBuilderAPI_MakeEdge make(curve,surface,first_vertex,last_vertex,
                    curve->FirstParameter(),curve->LastParameter());
                if(!make.IsDone())throw std::runtime_error("Sheet Cut cannot construct its consolidated boundary.");
                double first=0,last=0;
                const auto stored=BRep_Tool::CurveOnSurface(make.Edge(),target,first,last);
                wire.Add(make.Edge());
                if(!wire.IsDone())throw std::runtime_error("Sheet Cut cannot connect its consolidated boundary.");
                expected.push_back({stored,{source.owner_id,std::move(key)}});
            }
            builder.Add(target,wire.Wire());
        }
        target.Orientation(face.Orientation());
        TopTools_IndexedMapOfShape final_edges;TopExp::MapShapes(target,TopAbs_EDGE,final_edges);
        for(int i=1;i<=final_edges.Extent();++i) {
            double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(final_edges(i)),target,first,last);
            const auto owner=std::ranges::find_if(expected,[&](const auto& item){return item.curve==curve;});
            if(owner==expected.end())throw std::runtime_error("Sheet Cut lost consolidated-boundary ancestry.");
            result.edges.push_back({final_edges(i),owner->reference});
        }
        const double curve_tolerance=std::min(1.e-9,join_tolerance*.01);
        if(!BRepLib::BuildCurves3d(target,curve_tolerance,GeomAbs_C0))
            throw std::runtime_error("Sheet Cut cannot evaluate its consolidated boundary.");
        BRepLib::SameParameter(target,curve_tolerance,true);
        if(!BRepCheck_Analyzer(target).IsValid())
            throw std::runtime_error("Sheet Cut generated an invalid consolidated material domain.");
        builder.Add(compound,target);
    }
    result.shape=compound;return result;
}
