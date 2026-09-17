// Included inside occt_kernel.cpp's anonymous namespace after the Boolean
// topology propagation helpers. A cut owns this calculated material domain.
// No sampled depth, OCCT index or traversal position becomes topology identity.

template<class Algorithm>
void set_sheet_cut_boolean_inputs(Algorithm& algorithm,const TopoDS_Shape& argument,
        const TopoDS_Shape& tool) {
    const auto unwrap=[](TopoDS_Shape shape) {
        while(shape.ShapeType()==TopAbs_COMPOUND||shape.ShapeType()==TopAbs_COMPSOLID) {
            TopoDS_Iterator child(shape);if(!child.More())break;
            const auto only=child.Value();child.Next();if(child.More())break;
            shape=only;
        }
        return shape;
    };
    set_boolean_inputs(algorithm,unwrap(argument),unwrap(tool));
    // Clearance repeatedly inspects the same source and authored prism. A
    // proof must not change the tolerances or pcurves of its next input.
    algorithm.SetNonDestructive(true);
}

PrimitiveData sheet_cut_normal_volume(const TopoDS_Face& face,
        const FaceReference& source,const std::vector<OwnedEdge>& source_edges,
        double tolerance) {
    PrimitiveData result;
    const BRepAdaptor_Surface surface(face);
    const bool indirect_cone=surface.GetType()==GeomAbs_Cone&&
        !surface.Cone().Position().Direct();
    if(!BRepCheck_Analyzer(face).IsValid())
        throw std::runtime_error("Sheet Cut has an invalid material-domain face before thickening.");
    const double depth=source.sheet_thickness+(surface.GetType()==GeomAbs_Cone?10*tolerance:0);
    BRepOffset_MakeSimpleOffset offset(face,indirect_cone?depth:-depth);
    offset.SetBuildSolidFlag(true);offset.SetTolerance(tolerance);offset.Perform();
    if(!offset.IsDone()||offset.GetResultShape().IsNull())
        throw std::runtime_error("Sheet Cut cannot construct the full material thickness.");
    if(!BRepCheck_Analyzer(offset.GetResultShape()).IsValid())
        throw std::runtime_error("Sheet Cut generated an invalid normal thickness volume.");
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    bool found=false;
    for(TopExp_Explorer solids(offset.GetResultShape(),TopAbs_SOLID);solids.More();solids.Next()) {
        auto solid=TopoDS::Solid(solids.Current());
        if(!BRepLib::OrientClosedSolid(solid))
            throw std::runtime_error("Sheet Cut cannot orient the full material thickness.");
        builder.Add(compound,solid);found=true;
    }
    if(!found)throw std::runtime_error("Sheet Cut thickness did not produce a solid.");
    result.shape=compound;result.faces.push_back({face,source});
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    const auto opposite=offset.Generated(face);
    if(!opposite.IsNull()&&opposite.ShapeType()==TopAbs_FACE) {
        auto ref=source;ref.semantic_key="sheetcut-zone:opposite:from:"+token(source.semantic_key);
        ref.sheet_role=SheetFaceRole::SideB;ref.surface.reset();
        result.faces.push_back({opposite,std::move(ref)});
    }
    for(const auto& edge:source_edges) {
        bool belongs=false;
        for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next())
            if(edges.Current().IsSame(edge.shape)){belongs=true;break;}
        if(!belongs)continue;
        result.edges.push_back(edge);
        const auto moved=offset.Generated(edge.shape);
        if(!moved.IsNull()&&moved.ShapeType()==TopAbs_EDGE)
            result.edges.push_back({moved,{source.owner_id,
                "sheetcut-zone:opposite-edge:from:"+token(edge.reference.owner_id)+":"+token(edge.reference.semantic_key)}});
        for(TopExp_Explorer walls(offset.GetResultShape(),TopAbs_FACE);walls.More();walls.Next()) {
            if(walls.Current().IsSame(face)||(!opposite.IsNull()&&walls.Current().IsSame(opposite)))continue;
            for(TopExp_Explorer rims(walls.Current(),TopAbs_EDGE);rims.More();rims.Next())
                if(rims.Current().IsSame(edge.shape)) {
                    FaceReference ref{source.owner_id,"sheetcut-zone:wall:from:"+
                        token(edge.reference.owner_id)+":"+token(edge.reference.semantic_key)};
                    ref.sheet_role=SheetFaceRole::ThicknessFace;ref.sheet_thickness=source.sheet_thickness;
                    result.faces.push_back({walls.Current(),std::move(ref)});break;
                }
        }
    }
    return result;
}

#include "sheet_cut_circle_clearance.inc"
#include "sheet_cut_conic_profile_clearance.inc"
#include "sheet_cut_polygon_clearance.inc"
#include "sheet_cut_boundary_runs.inc"

// Enlarge a sampled material domain by a bounded physical distance, including
// shrinking its inner loops. The flat metric dominates the analytic surface
// metric, so this cannot move a material boundary farther than the allowance.
// The caller still verifies the complete 3D passage before accepting it.
PrimitiveData sheet_cut_domain_margin(const PrimitiveData& domain,
        const TopoDS_Face& source,const FaceReference& reference,
        const std::vector<OwnedEdge>& source_edges,double allowance,double movement_budget,
        double tolerance) {
    const BRepAdaptor_Surface support(source);double u_scale=1;
    if(support.GetType()==GeomAbs_Cylinder)u_scale=support.Cylinder().Radius();
    else if(support.GetType()==GeomAbs_Cone) {
        double v0=std::numeric_limits<double>::infinity(),v1=-v0;
        for(TopExp_Explorer faces(domain.shape,TopAbs_FACE);faces.More();faces.Next()) {
            double first_u=0,last_u=0,first_v=0,last_v=0;
            BRepTools::UVBounds(TopoDS::Face(faces.Current()),first_u,last_u,first_v,last_v);
            v0=std::min(v0,first_v-allowance);v1=std::max(v1,last_v+allowance);
        }
        if(!std::isfinite(v0)||!std::isfinite(v1))return {};
        const auto cone=support.Cone();
        u_scale=std::max(std::abs(cone.RefRadius()+v0*std::sin(cone.SemiAngle())),
            std::abs(cone.RefRadius()+v1*std::sin(cone.SemiAngle())));
    } else if(support.GetType()!=GeomAbs_Plane)return {};
    if(u_scale<=tolerance||allowance<=0)return {};
    const Handle(Geom_Surface) metric_surface=new Geom_Plane(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
    const auto surface=BRep_Tool::Surface(source);
    const auto area=[](const TopoDS_Shape& shape) {
        GProp_GProps value;BRepGProp::SurfaceProperties(shape,value);return std::abs(value.Mass());
    };
    const auto map=[&](const TopoDS_Face& face,const Handle(Geom_Surface)& target_surface,
                       const std::vector<OwnedEdge>& ancestry,double factor) {
        PrimitiveData mapped;TopoDS_Face target;BRep_Builder builder;
        builder.MakeFace(target,target_surface,tolerance);
        struct ExpectedEdge {TopoDS_Shape shape;Handle(Geom2d_Curve) curve;EdgeReference parent;};
        std::vector<ExpectedEdge> expected_edges;
        const auto forward=TopoDS::Face(face.Oriented(TopAbs_FORWARD));
        for(TopExp_Explorer wires(forward,TopAbs_WIRE);wires.More();wires.Next()) {
            BRepBuilderAPI_MakeWire wire;
            for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),forward);edges.More();edges.Next()) {
                double first=0,last=0;const auto edge=edges.Current();
                const auto curve=BRep_Tool::CurveOnSurface(edge,forward,first,last);
                if(curve.IsNull())return PrimitiveData{};
                auto spline=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve,first,last));
                spline=Handle(Geom2d_BSplineCurve)::DownCast(spline->Copy());
                for(int pole=1;pole<=spline->NbPoles();++pole) {
                    const auto point=spline->Pole(pole);spline->SetPole(pole,gp_Pnt2d(point.X()*factor,point.Y()));
                }
                BRepBuilderAPI_MakeEdge make(spline,target_surface);
                if(!make.IsDone())return PrimitiveData{};
                auto replacement=make.Edge();replacement.Orientation(edge.Orientation());
                double edge_tolerance=std::max(tolerance,BRep_Tool::Tolerance(edge));
                for(TopExp_Explorer vertices(edge,TopAbs_VERTEX);vertices.More();vertices.Next())
                    edge_tolerance=std::max(edge_tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(vertices.Current())));
                builder.UpdateEdge(replacement,edge_tolerance);
                for(TopExp_Explorer vertices(replacement,TopAbs_VERTEX);vertices.More();vertices.Next())
                    builder.UpdateVertex(TopoDS::Vertex(vertices.Current()),edge_tolerance);
                double stored_first=0,stored_last=0;
                const auto stored_curve=BRep_Tool::CurveOnSurface(replacement,target,stored_first,stored_last);
                wire.Add(replacement);
                if(!wire.IsDone())return PrimitiveData{};
                for(const auto& parent:ancestry)if(parent.shape.IsSame(edge))
                    expected_edges.push_back({wire.Edge(),stored_curve,parent.reference});
            }
            if(!wire.IsDone())return PrimitiveData{};builder.Add(target,wire.Wire());
        }
        // MakeWire may replace an earlier edge when it joins vertices. Recover
        // its predeclared parent using the exact pcurve construction handle,
        // after all edges have been connected, before parameter correction.
        for(TopExp_Explorer edges(target,TopAbs_EDGE);edges.More();edges.Next()) {
            double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(edges.Current()),target,first,last);
            for(const auto& expected:expected_edges)
                if(edges.Current().IsSame(expected.shape)||(!curve.IsNull()&&curve==expected.curve))
                    if(std::ranges::none_of(mapped.edges,[&](const auto& value){
                        return value.shape.IsSame(edges.Current())&&value.reference==expected.parent;}))
                        mapped.edges.push_back({edges.Current(),expected.parent});
        }
        for(const auto& expected:expected_edges)
            if(std::ranges::none_of(mapped.edges,[&](const auto& value){return value.reference==expected.parent;}))
                return PrimitiveData{};
        if(!BRepLib::BuildCurves3d(target,std::min(1.e-8,tolerance*.01)))return PrimitiveData{};
        BRepLib::SameParameter(target,std::min(1.e-8,tolerance*.01),true);
        if(!BRepCheck_Analyzer(target).IsValid())return PrimitiveData{};
        mapped.shape=target;return mapped;
    };
    PrimitiveData result;
    for(TopExp_Explorer faces(domain.shape,TopAbs_FACE);faces.More();faces.Next()) {
        auto metric=map(TopoDS::Face(faces.Current()),metric_surface,domain.edges,u_scale);
        if(metric.shape.IsNull())return {};
        PrimitiveData enlarged;
        for(const double sign:{1.,-1.}) {
            BRepOffsetAPI_MakeOffset offset(TopoDS::Face(metric.shape),GeomAbs_Arc,false);
            offset.Perform(sign*allowance);if(!offset.IsDone())continue;
            TopoDS_Face face;BRep_Builder builder;builder.MakeFace(face,metric_surface,tolerance);
            for(TopExp_Explorer wires(offset.Shape(),TopAbs_WIRE);wires.More();wires.Next())builder.Add(face,wires.Current());
            if(!BRepCheck_Analyzer(face).IsValid()||area(face)<=area(metric.shape))continue;
            enlarged.shape=face;
            std::vector<TopoDS_Edge> unmatched;
            for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next()) {
                std::set<std::pair<std::string,std::string>> parents;
                const auto generated_from=[&](const TopoDS_Shape& input) {
                    for(TopTools_ListIteratorOfListOfShape item(offset.Generated(input));item.More();item.Next()) {
                        if(item.Value().IsSame(edges.Current()))return true;
                        for(TopExp_Explorer children(item.Value(),TopAbs_EDGE);children.More();children.Next())
                            if(children.Current().IsSame(edges.Current()))return true;
                    }
                    return false;
                };
                for(const auto& parent:metric.edges) {
                    bool generated=generated_from(parent.shape);
                    for(TopExp_Explorer vertices(parent.shape,TopAbs_VERTEX);!generated&&vertices.More();vertices.Next())
                        generated=generated_from(vertices.Current());
                    if(generated)parents.emplace(parent.reference.owner_id,parent.reference.semantic_key);
                }
                if(parents.empty()){unmatched.push_back(TopoDS::Edge(edges.Current()));continue;}
                if(parents.size()==1)enlarged.edges.push_back({edges.Current(),{parents.begin()->first,parents.begin()->second}});
                else {
                    std::string key="sheetcut-envelope:join:from:";
                    for(const auto& [owner,parent]:parents)key+=std::to_string(owner.size())+":"+owner+":"+
                        std::to_string(parent.size())+":"+parent+":";
                    enlarged.edges.push_back({edges.Current(),{reference.owner_id,std::move(key)}});
                }
            }
            // OCCT's OffsetWire::FixHoles constructs straight connectors
            // without recording Generated history. Their exact shared end
            // vertices identify their already-owned neighbours. No geometric
            // proximity or enumeration position participates in this ancestry.
            for(const auto& bridge:unmatched) {
                if(BRepAdaptor_Curve(bridge).GetType()!=GeomAbs_Line)return {};
                GProp_GProps length;BRepGProp::LinearProperties(bridge,length);
                // The metric dominates the material surface metric. Every
                // bridge point is at most half its length from an endpoint on
                // the offset curve; reserve numerical error in that bound.
                if(allowance+std::abs(length.Mass())*.5+10*tolerance>movement_budget)return {};
                TopoDS_Vertex first,last;TopExp::Vertices(bridge,first,last);
                if(first.IsNull()||last.IsNull())return {};
                std::set<std::pair<std::string,std::string>> parents;
                for(const auto& endpoint:{first,last}) {
                    bool covered=false;
                    for(const auto& neighbour:enlarged.edges)
                        for(TopExp_Explorer vertices(neighbour.shape,TopAbs_VERTEX);vertices.More();vertices.Next())
                            if(vertices.Current().IsSame(endpoint)) {
                                parents.emplace(neighbour.reference.owner_id,neighbour.reference.semantic_key);
                                covered=true;break;
                            }
                    if(!covered)return {};
                }
                std::string key="sheetcut-envelope:repair-join:from:";
                for(const auto& [owner,parent]:parents)key+=std::to_string(owner.size())+":"+owner+":"+
                    std::to_string(parent.size())+":"+parent+":";
                enlarged.edges.push_back({bridge,{reference.owner_id,std::move(key)}});
            }
            break;
        }
        if(enlarged.shape.IsNull())return {};
        auto mapped=map(TopoDS::Face(enlarged.shape),surface,enlarged.edges,1./u_scale);
        if(mapped.shape.IsNull())return {};
        mapped.shape.Orientation(source.Orientation());
        if(result.shape.IsNull()){result=std::move(mapped);continue;}
        BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,result.shape,mapped.shape);
        unite.SetFuzzyValue(tolerance);unite.SetToFillHistory(true);unite.Build();
        if(!unite.IsDone())return {};
        result.edges=propagate_topology(unite,result.edges,mapped.edges);result.shape=unite.Shape();
    }
    BRepAlgoAPI_Common limit;set_sheet_cut_boolean_inputs(limit,result.shape,source);
    limit.SetFuzzyValue(tolerance);limit.SetToFillHistory(true);limit.Build();if(!limit.IsDone())return {};
    result.edges=propagate_topology(limit,result.edges,source_edges);result.shape=limit.Shape();
    ShapeUpgrade_UnifySameDomain merge(result.shape,true,true,false);merge.SetLinearTolerance(tolerance);merge.Build();
    result.edges=propagate_topology(merge.History(),result.edges);result.shape=merge.Shape();
    if(!BRepCheck_Analyzer(result.shape).IsValid())return {};
    return result;
}

// A polyhedral volume's orthographic shadow is exactly the union of the
// shadows of its boundary faces. This avoids thickness sampling for planar
// sheets with polygonal cutters, including narrow slots and retained islands.
PrimitiveData sheet_cut_planar_domain(const TopoDS_Face& source,
        const FaceReference& reference,const std::vector<OwnedEdge>& source_edges,
        const PrimitiveData& projection,double tolerance) {
    if(BRepAdaptor_Surface(source).GetType()!=GeomAbs_Plane)return {};
    for(TopExp_Explorer faces(projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        if(BRepAdaptor_Surface(TopoDS::Face(faces.Current())).GetType()!=GeomAbs_Plane)return {};
        for(TopExp_Explorer edges(faces.Current(),TopAbs_EDGE);edges.More();edges.Next())
            if(BRepAdaptor_Curve(TopoDS::Edge(edges.Current())).GetType()!=GeomAbs_Line)return {};
    }
    const auto plane=BRepAdaptor_Surface(source).Plane();const auto frame=plane.Position();
    const gp_Vec normal(frame.Direction()),axis_x(frame.XDirection()),axis_y(frame.YDirection());
    const auto project=[&](const gp_Pnt& point) {
        return point.Translated(normal*(-gp_Vec(frame.Location(),point).Dot(normal)));
    };
    const auto token=[](const std::string& text){return std::to_string(text.size())+":"+text;};
    PrimitiveData result;
    for(TopExp_Explorer faces(projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current().Oriented(TopAbs_FORWARD));
        const auto outer=BRepTools::OuterWire(face);TopoDS_Wire outer_shadow;
        std::vector<TopoDS_Wire> holes;std::vector<OwnedEdge> shadow_edges;
        for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next()) {
            BRepBuilderAPI_MakeWire projected;double twice_area=0;int edge_count=0;
            for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),face);edges.More();edges.Next()) {
                const auto edge=edges.Current();BRepAdaptor_Curve curve(edge);
                auto first=project(curve.Value(curve.FirstParameter()));
                auto last=project(curve.Value(curve.LastParameter()));
                if(edge.Orientation()==TopAbs_REVERSED)std::swap(first,last);
                if(first.Distance(last)<=std::max(1.e-10,tolerance*.01))continue;
                const gp_Vec a(frame.Location(),first),b(frame.Location(),last);
                twice_area+=a.Dot(axis_x)*b.Dot(axis_y)-a.Dot(axis_y)*b.Dot(axis_x);
                BRepBuilderAPI_MakeEdge make(first,last);
                if(!make.IsDone())return {};
                projected.Add(make.Edge());if(!projected.IsDone())return {};
                ++edge_count;
                std::set<std::pair<std::string,std::string>> parents;
                for(const auto& parent:projection.edges)if(parent.shape.IsSame(edge))
                    parents.emplace(parent.reference.owner_id,parent.reference.semantic_key);
                // Boolean intersections may create a new edge shared by two
                // already attributed faces. Their identities define its parents.
                for(const auto& parent:projection.faces)
                    for(TopExp_Explorer rims(parent.shape,TopAbs_EDGE);rims.More();rims.Next())
                        if(rims.Current().IsSame(edge)) {
                            parents.emplace(parent.reference.owner_id,parent.reference.semantic_key);break;
                        }
                if(parents.empty())return {};
                std::string key="sheetcut-shadow:source:"+token(reference.owner_id)+":"+token(reference.semantic_key)+":from:";
                for(const auto& [owner,parent]:parents)key+=token(owner)+":"+token(parent)+":";
                shadow_edges.push_back({projected.Edge(),{reference.owner_id,std::move(key)}});
            }
            if(edge_count<3||std::abs(twice_area)<=tolerance*tolerance)continue;
            auto wire=projected.Wire();const bool is_outer=wires.Current().IsSame(outer);
            if((twice_area>0)!=is_outer)wire.Reverse();
            if(is_outer)outer_shadow=wire;else holes.push_back(wire);
        }
        if(outer_shadow.IsNull())continue;
        BRepBuilderAPI_MakeFace make(plane,outer_shadow,true);
        for(const auto& hole:holes)make.Add(hole);
        if(!make.IsDone())return {};
        auto shadow=make.Face();shadow.Orientation(source.Orientation());
        if(!BRepCheck_Analyzer(shadow).IsValid())return {};
        if(result.shape.IsNull()) {result.shape=shadow;result.edges=std::move(shadow_edges);continue;}
        BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,result.shape,shadow);
        unite.SetToFillHistory(true);unite.SetFuzzyValue(tolerance);unite.Build();
        if(!unite.IsDone())return {};
        result.edges=propagate_topology(unite,result.edges,shadow_edges);result.shape=unite.Shape();
    }
    if(result.shape.IsNull())return {};
    BRepAlgoAPI_Common limit;set_sheet_cut_boolean_inputs(limit,result.shape,source);
    limit.SetToFillHistory(true);limit.SetFuzzyValue(tolerance);limit.Build();
    if(!limit.IsDone())return {};
    result.edges=propagate_topology(limit,result.edges,source_edges);result.shape=limit.Shape();
    ShapeUpgrade_UnifySameDomain merge(result.shape,true,true,false);
    merge.SetLinearTolerance(tolerance);merge.Build();
    result.edges=propagate_topology(merge.History(),result.edges);result.shape=merge.Shape();
    TopTools_IndexedMapOfShape actual_edges;TopExp::MapShapes(result.shape,TopAbs_EDGE,actual_edges);
    std::erase_if(result.edges,[&](const auto& edge){return !actual_edges.Contains(edge.shape);});
    return result;
}

PrimitiveData sheet_cut_clearance_domain(const TopoDS_Face& source,
        const FaceReference& reference,const std::vector<OwnedEdge>& source_edges,
        const PrimitiveData& projection,const PrimitiveData& selected,double model_tolerance,
        double clearance_accuracy) {
    // Keep construction and normal-thickness precision independent of the
    // operation's permitted deviation. Analytic containment may retry a
    // near-coincident Boolean within that budget; its geometry stays unchanged
    // and an invalid residual never counts as verified clearance.
    const double tolerance=std::min(model_tolerance,1.e-7);
    if(!std::isfinite(clearance_accuracy)||clearance_accuracy<1.e-6||clearance_accuracy>1.)
        throw std::runtime_error("Sheet Cut maximum deviation must be between 0.000001 and 1 mm.");
    const double thickness=reference.sheet_thickness;
    if(!(thickness>0))throw std::runtime_error("Sheet Cut source has no thickness.");
    const BRepAdaptor_Surface original(source);
    gp_Ax3 frame;double radius=0,angle=0;
    switch(original.GetType()) {
    case GeomAbs_Plane:frame=original.Plane().Position();break;
    case GeomAbs_Cylinder:frame=original.Cylinder().Position();radius=original.Cylinder().Radius();break;
    case GeomAbs_Cone:frame=original.Cone().Position();radius=original.Cone().RefRadius();angle=original.Cone().SemiAngle();break;
    default:throw std::runtime_error("Sheet Cut clearance requires an analytic sheet region.");
    }
    const double outward=(source.Orientation()==TopAbs_REVERSED?-1.:1.)*(frame.Direct()?1.: -1.);
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
        return std::abs(properties.Mass());
    };
    const double volume_tolerance=std::max(1.e-12,tolerance*tolerance*tolerance);
    const auto stock=sheet_cut_normal_volume(source,reference,source_edges,tolerance);
    BRepAlgoAPI_Common required;set_sheet_cut_boolean_inputs(required,stock.shape,selected.shape);
    required.SetFuzzyValue(tolerance);required.Build();
    if(!required.IsDone()||!BRepCheck_Analyzer(required.Shape()).IsValid())
        throw std::runtime_error("Sheet Cut cannot identify valid material reached by its profile.");
    const auto required_shape=required.Shape();
    if(volume(required_shape)<=volume_tolerance)return {};

    auto analytic=sheet_cut_planar_domain(source,reference,source_edges,selected,tolerance);
    if(analytic.shape.IsNull())analytic=sheet_cut_cylindrical_circle_domain(source,reference,
        source_edges,projection,selected,tolerance);
    if(analytic.shape.IsNull())analytic=sheet_cut_conical_profile_domain(source,reference,
        source_edges,projection,selected,tolerance);
    if(analytic.shape.IsNull())analytic=sheet_cut_convex_prism_domain(source,reference,
        source_edges,projection,selected,tolerance);
    if(!analytic.shape.IsNull()) {
        BRep_Builder builder;TopoDS_Compound tools;builder.MakeCompound(tools);
        for(TopExp_Explorer faces(analytic.shape,TopAbs_FACE);faces.More();faces.Next()) {
            auto face=TopoDS::Face(faces.Current());face.Orientation(source.Orientation());
            builder.Add(tools,sheet_cut_normal_volume(face,reference,{},tolerance).shape);
        }
        BRepAlgoAPI_Cut remaining;set_sheet_cut_boolean_inputs(remaining,required_shape,tools);
        remaining.SetFuzzyValue(tolerance);remaining.Build();
        if(remaining.IsDone()&&BRepCheck_Analyzer(remaining.Shape()).IsValid()&&
           volume(remaining.Shape())<=volume_tolerance)return analytic;
        // A valid analytic domain does not need geometric enlargement when a
        // near-coincident Boolean boundary is the only obstacle. Retry that
        // inspection within the operation's separate accuracy budget; never
        // feed this distance into normal-thickness construction.
        for(const double fuzzy:{clearance_accuracy*.1,clearance_accuracy*.2}) {
            if(fuzzy<=tolerance)continue;
            BRepAlgoAPI_Cut retried;set_sheet_cut_boolean_inputs(retried,required_shape,tools);
            retried.SetFuzzyValue(fuzzy);retried.Build();
            if(retried.IsDone()&&BRepCheck_Analyzer(retried.Shape()).IsValid()&&volume(retried.Shape())<=volume_tolerance)
                return analytic;
        }
    }

    // Each analytic skin uses the ORIGINAL UV coordinates. In particular,
    // normal cone offsets move both the reference radius and the origin;
    // merely changing radius would shift V and corrupt the material domain.
    const auto skin_surface=[&](double depth)->Handle(Geom_Surface) {
        if(depth==0)return BRep_Tool::Surface(source);
        const double distance=-depth*outward;auto position=frame;
        if(original.GetType()==GeomAbs_Plane) {
            position.SetLocation(frame.Location().Translated(gp_Vec(frame.Direction())*distance));
            return new Geom_Plane(gp_Pln(position));
        }
        if(original.GetType()==GeomAbs_Cylinder) {
            if(radius+distance<=tolerance)throw std::runtime_error("Sheet Cut reaches the cylinder axis.");
            return new Geom_CylindricalSurface(gp_Cylinder(position,radius+distance));
        }
        position.SetLocation(frame.Location().Translated(gp_Vec(frame.Direction())*(-distance*std::sin(angle))));
        if(radius+distance*std::cos(angle)<=tolerance)
            throw std::runtime_error("Sheet Cut reaches the cone apex.");
        return new Geom_ConicalSurface(gp_Cone(position,angle,radius+distance*std::cos(angle)));
    };
    const auto base_surface=skin_surface(0);
    double uv_scale=1;
    if(original.GetType()==GeomAbs_Cylinder)uv_scale=std::max(1.,radius);
    else if(original.GetType()==GeomAbs_Cone) {
        double u0=0,u1=0,v0=0,v1=0;BRepTools::UVBounds(source,u0,u1,v0,v1);
        uv_scale=std::max({1.,std::abs(radius+v0*std::sin(angle)),std::abs(radius+v1*std::sin(angle))});
    }
    const Handle(Geom_Surface) uv_surface=new Geom_Plane(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)));

    // Copy exact pcurves, including every inner loop, between skins sharing UV.
    const auto transfer=[&](const TopoDS_Face& face,const Handle(Geom_Surface)& surface,
                            const std::vector<OwnedEdge>& ancestry,const std::string& context,double u_factor=1.) {
        PrimitiveData mapped;BRep_Builder builder;TopoDS_Face target;
        builder.MakeFace(target,surface,tolerance);
        struct ExpectedEdge {TopoDS_Shape shape;Handle(Geom2d_Curve) curve;EdgeReference parent;};
        std::vector<ExpectedEdge> expected_edges;
        // Copy intrinsic wire orientation, then apply the face orientation
        // once. Exploring a reversed face directly would compose it twice.
        const auto forward=TopoDS::Face(face.Oriented(TopAbs_FORWARD));
        for(TopExp_Explorer wires(forward,TopAbs_WIRE);wires.More();wires.Next()) {
            BRepBuilderAPI_MakeWire wire;
            for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),forward);edges.More();edges.Next()) {
                const auto edge=edges.Current();double first=0,last=0;
                const auto original_curve=BRep_Tool::CurveOnSurface(edge,forward,first,last);
                if(original_curve.IsNull())throw std::runtime_error("Sheet Cut is missing a material boundary curve.");
                auto curve=Handle(Geom2d_Curve)::DownCast(original_curve->Copy());
                if(u_factor!=1.) {
                    auto spline=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve,first,last));
                    spline=Handle(Geom2d_BSplineCurve)::DownCast(spline->Copy());
                    for(int pole=1;pole<=spline->NbPoles();++pole) {
                        const auto point=spline->Pole(pole);spline->SetPole(pole,gp_Pnt2d(point.X()*u_factor,point.Y()));
                    }
                    curve=spline;first=curve->FirstParameter();last=curve->LastParameter();
                }
                BRepBuilderAPI_MakeEdge make(curve,surface,first,last);
                if(!make.IsDone())throw std::runtime_error("Sheet Cut cannot transfer a material boundary.");
                auto replacement=make.Edge();replacement.Orientation(edge.Orientation());
                // Keep the source's existing geometric resolution. Replacing
                // its connected vertices by tighter defaults can disconnect
                // otherwise valid revolved/imported boundary curves.
                double edge_tolerance=std::max(tolerance,BRep_Tool::Tolerance(edge));
                for(TopExp_Explorer vertices(edge,TopAbs_VERTEX);vertices.More();vertices.Next())
                    edge_tolerance=std::max(edge_tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(vertices.Current())));
                builder.UpdateEdge(replacement,edge_tolerance);
                for(TopExp_Explorer vertices(replacement,TopAbs_VERTEX);vertices.More();vertices.Next())
                    builder.UpdateVertex(TopoDS::Vertex(vertices.Current()),edge_tolerance);
                // MakeEdge may unwrap a trimmed curve. Record the actual
                // stored pcurve rather than the supplied wrapper handle.
                double stored_first=0,stored_last=0;
                const auto stored_curve=BRep_Tool::CurveOnSurface(replacement,target,stored_first,stored_last);
                wire.Add(replacement);
                if(!wire.IsDone())throw std::runtime_error("Sheet Cut cannot connect its material boundary ("+context+").");
                for(const auto& parent:ancestry)if(parent.shape.IsSame(edge))
                    expected_edges.push_back({wire.Edge(),stored_curve,parent.reference});
            }
            if(!wire.IsDone())throw std::runtime_error("Sheet Cut has an incomplete material loop.");
            builder.Add(target,wire.Wire());
        }
        target.Orientation(face.Orientation());
        for(TopExp_Explorer edges(target,TopAbs_EDGE);edges.More();edges.Next()) {
            double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(edges.Current()),target,first,last);
            for(const auto& expected:expected_edges)
                if(edges.Current().IsSame(expected.shape)||(!curve.IsNull()&&curve==expected.curve))
                    if(std::ranges::none_of(mapped.edges,[&](const auto& value){
                        return value.shape.IsSame(edges.Current())&&value.reference==expected.parent;}))
                        mapped.edges.push_back({edges.Current(),expected.parent});
        }
        for(const auto& expected:expected_edges)
            if(std::ranges::none_of(mapped.edges,[&](const auto& value){return value.reference==expected.parent;}))
                throw std::runtime_error("Sheet Cut lost a material boundary's authored ancestry during transfer.");
        // The edge builder starts with exact pcurves and the kernel's small
        // edge tolerance. A model-tolerance 3D approximation would violate
        // that edge/pcurve contract on circular cylindrical intersections.
        const double curve_tolerance=std::min(1.e-7,tolerance*.01);
        if(!BRepLib::BuildCurves3d(target,curve_tolerance))
            throw std::runtime_error("Sheet Cut cannot evaluate its transferred material boundary.");
        BRepLib::SameParameter(target,curve_tolerance,true);
        BRepCheck_Analyzer checked(target);
        if(!checked.IsValid()) {
            std::ostringstream diagnostic;
            diagnostic<<"Sheet Cut could not preserve its exact surface boundary during transfer ("<<context;
            const auto append=[&](const TopoDS_Shape& shape) {
                const auto& result=checked.Result(shape);
                if(result.IsNull())return;
                for(const auto status:result->Status())if(status!=BRepCheck_NoError)
                    diagnostic<<"; shape "<<static_cast<int>(shape.ShapeType())<<" status "<<static_cast<int>(status);
                for(result->InitContextIterator();result->MoreShapeInContext();result->NextShapeInContext())
                    for(const auto status:result->StatusOnShape())if(status!=BRepCheck_NoError)
                        diagnostic<<"; shape "<<static_cast<int>(shape.ShapeType())<<" in "
                            <<static_cast<int>(result->ContextualShape().ShapeType())<<" status "<<static_cast<int>(status);
            };
            append(target);
            for(const auto kind:{TopAbs_WIRE,TopAbs_EDGE,TopAbs_VERTEX})
                for(TopExp_Explorer children(target,kind);children.More();children.Next())append(children.Current());
            diagnostic<<").";
            throw std::runtime_error(diagnostic.str());
        }
        mapped.shape=target;return mapped;
    };
    PrimitiveData domain;
    std::vector<double> sampled;
    const auto add_depth=[&](double requested) {
        const double depth=std::clamp(requested,0.,thickness);
        if(std::ranges::any_of(sampled,[&](double previous){return std::abs(previous-depth)<std::max(1.e-10,tolerance*.05);}))return false;
        sampled.push_back(depth);
        auto skin=transfer(source,skin_surface(depth),source_edges,"skin depth "+std::to_string(depth));
        BRepAlgoAPI_Common clip;set_sheet_cut_boolean_inputs(clip,skin.shape,projection.shape);
        clip.SetToFillHistory(true);clip.SetFuzzyValue(tolerance);clip.Build();
        if(!clip.IsDone())throw std::runtime_error("Sheet Cut cannot project onto a material thickness section.");
        bool changed=false;
        for(TopExp_Explorer faces(clip.Shape(),TopAbs_FACE);faces.More();faces.Next()) {
            auto patch=TopoDS::Face(faces.Current());patch.Orientation(source.Orientation());
            GProp_GProps area;BRepGProp::SurfaceProperties(patch,area);
            if(area.Mass()<=tolerance*tolerance)continue;
            std::vector<OwnedEdge> ancestry;
            for(TopExp_Explorer edges(patch,TopAbs_EDGE);edges.More();edges.Next()) {
                const auto edge=edges.Current();std::set<std::pair<std::string,std::string>> parents;
                const auto descends=[&](const auto& input) {
                    if(input.shape.IsSame(edge))return true;
                    for(const auto* list:{&clip.Generated(input.shape),&clip.Modified(input.shape)})
                        for(TopTools_ListIteratorOfListOfShape item(*list);item.More();item.Next())
                            if(item.Value().IsSame(edge))return true;
                    return false;
                };
                for(const auto& input:projection.faces)if(descends(input))parents.emplace(input.reference.owner_id,input.reference.semantic_key);
                for(const auto& input:projection.edges)if(descends(input))parents.emplace(input.reference.owner_id,input.reference.semantic_key);
                for(const auto& input:skin.edges)if(descends(input))parents.emplace(input.reference.owner_id,input.reference.semantic_key);
                if(parents.empty())throw std::runtime_error("Sheet Cut clearance boundary has no authored ancestry.");
                const std::string role=depth<=tolerance?"side-a":depth>=thickness-tolerance?"side-b":"interior";
                // Boolean splits remain fragments of the same authored
                // semantic boundary; a sampled depth is never an identity.
                std::string key="sheetcut-envelope:"+role+":source:"+token(reference.owner_id)+":"+token(reference.semantic_key)+":from:";
                for(const auto& [owner,parent]:parents)key+=token(owner)+":"+token(parent)+":";
                ancestry.push_back({edge,{reference.owner_id,std::move(key)}});
            }
            // Skin clipping uses the authored prism, whose end caps cannot
            // coincide with the stock skins. Keep only the wall components
            // reached by the user's finite projection range. This intersection
            // is only an ownership test; its coincident-cap boundary geometry
            // is discarded, and each section need not be thickened separately.
            BRepAlgoAPI_Common reached;set_sheet_cut_boolean_inputs(reached,patch,selected.shape);
            reached.SetFuzzyValue(tolerance);reached.Build();
            if(!reached.IsDone())throw std::runtime_error("Sheet Cut cannot identify a reached material-domain component.");
            GProp_GProps reached_area;BRepGProp::SurfaceProperties(reached.Shape(),reached_area);
            if(std::abs(reached_area.Mass())<=tolerance*tolerance)continue;
            // Boolean unions operate in a metric material UV plane (angle is
            // multiplied by a radius bound, so tolerances remain lengths).
            // Repeated
            // intersections of coincident curved surfaces are both slower and
            // less stable; only the complete candidate is embedded in 3D.
            auto mapped=transfer(patch,uv_surface,ancestry,"UV patch depth "+std::to_string(depth),uv_scale);
            if(domain.shape.IsNull()){domain=std::move(mapped);changed=true;continue;}
            BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,domain.shape,mapped.shape);
            unite.SetToFillHistory(true);unite.SetFuzzyValue(tolerance);unite.Build();
            if(!unite.IsDone())throw std::runtime_error("Sheet Cut cannot unite its material domains.");
            domain.edges=propagate_topology(unite,domain.edges,mapped.edges);domain.shape=unite.Shape();
            ShapeUpgrade_UnifySameDomain merge(domain.shape,true,true,false);
            merge.SetLinearTolerance(tolerance);merge.Build();
            domain.edges=propagate_topology(merge.History(),domain.edges);domain.shape=merge.Shape();
            changed=true;
        }
        return changed;
    };
    const auto material_depth=[&](const gp_Pnt& point) {
        const gp_Vec vector(frame.Location(),point);
        const double axial=vector.Dot(gp_Vec(frame.Direction()));
        if(original.GetType()==GeomAbs_Plane)return -outward*axial;
        const double radial=std::sqrt(std::max(0.,vector.SquareMagnitude()-axial*axial));
        return -outward*(radial-radius-axial*std::tan(angle))*std::cos(angle);
    };
    add_depth(0);add_depth(thickness);add_depth(thickness*.5);
    double last_residual=0;
    constexpr std::size_t maximum_samples=33;
    std::size_t margin_attempted_at=0;
    const auto embed=[&](const PrimitiveData& candidate) {
        PrimitiveData embedded;if(candidate.shape.IsNull())return embedded;
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        for(TopExp_Explorer faces(candidate.shape,TopAbs_FACE);faces.More();faces.Next()) {
            auto mapped=transfer(TopoDS::Face(faces.Current()),base_surface,candidate.edges,"merged UV domain",1./uv_scale);
            mapped.shape.Orientation(source.Orientation());builder.Add(compound,mapped.shape);
            embedded.edges.insert(embedded.edges.end(),mapped.edges.begin(),mapped.edges.end());
        }
        embedded.shape=compound;return embedded;
    };
    const auto remainder=[&](const PrimitiveData& candidate) {
        if(candidate.shape.IsNull())return TopoDS_Shape(required_shape);
        BRep_Builder builder;TopoDS_Compound tools;builder.MakeCompound(tools);
        for(TopExp_Explorer faces(candidate.shape,TopAbs_FACE);faces.More();faces.Next()) {
            auto face=TopoDS::Face(faces.Current());face.Orientation(source.Orientation());
            builder.Add(tools,sheet_cut_normal_volume(face,reference,{},tolerance).shape);
        }
        BRepAlgoAPI_Cut remaining;set_sheet_cut_boolean_inputs(remaining,required_shape,tools);
        remaining.SetFuzzyValue(tolerance);remaining.Build();
        if(!remaining.IsDone())throw std::runtime_error("Sheet Cut cannot verify profile clearance.");
        // A nearly closed sampled envelope may produce a degenerate residual
        // sliver. Reject that proof and continue refinement; an invalid Boolean
        // result must never count as a verified zero-volume remainder.
        if(!BRepCheck_Analyzer(remaining.Shape()).IsValid())return TopoDS_Shape{};
        return remaining.Shape();
    };
    const auto retain_final_edges=[](PrimitiveData& candidate) {
        TopTools_IndexedMapOfShape final_edges;TopExp::MapShapes(candidate.shape,TopAbs_EDGE,final_edges);
        std::erase_if(candidate.edges,[&](const auto& edge){return !final_edges.Contains(edge.shape);});
    };
    for(int iteration=0;iteration<10;++iteration) {
        const auto sampled_domain=embed(domain);
        auto curved=sheet_cut_join_boundary_runs(sampled_domain,reference,clearance_accuracy,tolerance);
        auto residual=remainder(curved);
        const bool valid_residual=!residual.IsNull();
        if(!valid_residual)residual=required_shape;
        last_residual=volume(residual);
        if(valid_residual&&last_residual<=volume_tolerance) {
            retain_final_edges(curved);
            return curved;
        }
        if(!domain.shape.IsNull()&&sampled.size()>=9&&sampled.size()>margin_attempted_at) {
            margin_attempted_at=sampled.size();
            for(const double fraction:{.1,.4,.8}) {
                auto enlarged=sheet_cut_domain_margin(sampled_domain,source,reference,source_edges,
                    clearance_accuracy*fraction,clearance_accuracy*.98,tolerance);
                if(enlarged.shape.IsNull())continue;
                enlarged=sheet_cut_join_boundary_runs(enlarged,reference,clearance_accuracy,tolerance);
                const auto enlarged_residual=remainder(enlarged);
                if(!enlarged_residual.IsNull()&&volume(enlarged_residual)<=volume_tolerance) {
                    retain_final_edges(enlarged);return enlarged;
                }
            }
        }
        if(sampled.size()>=maximum_samples)break;
        std::vector<double> candidates;
        for(TopExp_Explorer solids(residual,TopAbs_SOLID);solids.More();solids.Next()) {
            GProp_GProps properties;BRepGProp::VolumeProperties(solids.Current(),properties);
            if(std::abs(properties.Mass())<=volume_tolerance)continue;
            const auto center=properties.CentreOfMass();
            BRepClass3d_SolidClassifier classifier(solids.Current(),center,tolerance);
            if(classifier.State()==TopAbs_IN)candidates.push_back(material_depth(center));
            double minimum=thickness,maximum=0;
            for(TopExp_Explorer vertices(solids.Current(),TopAbs_VERTEX);vertices.More();vertices.Next()) {
                const double depth=std::clamp(material_depth(BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()))),0.,thickness);
                minimum=std::min(minimum,depth);maximum=std::max(maximum,depth);
            }
            if(maximum>=minimum) {
                candidates.push_back((minimum+maximum)*.5);
                candidates.push_back(minimum*.75+maximum*.25);
                candidates.push_back(minimum*.25+maximum*.75);
            }
        }
        bool advanced=false;
        // Balanced sections bound the largest remaining thickness gap. A
        // residual centre alone clusters samples at the same symmetric depth.
        const int sections=sampled.size()<9?8:sampled.size()<17?16:32;
        for(int section=1;section<sections&&sampled.size()<maximum_samples;++section)
            advanced=add_depth(thickness*section/sections)||advanced;
        if(domain.shape.IsNull())for(double depth:candidates) {
            if(sampled.size()>=maximum_samples)break;
            advanced=add_depth(depth)||advanced;
        }
        if(!advanced) {
            // Symmetric residuals often repeat the same centre depth. Split
            // the largest unsampled gaps instead of mistaking symmetry for
            // convergence; four additions bound each refinement step.
            auto ordered=sampled;std::sort(ordered.begin(),ordered.end());
            std::vector<std::pair<double,double>> gaps;
            for(std::size_t i=1;i<ordered.size();++i)
                gaps.emplace_back(ordered[i]-ordered[i-1],(ordered[i]+ordered[i-1])*.5);
            std::sort(gaps.rbegin(),gaps.rend());
            for(std::size_t i=0;i<std::min<std::size_t>(4,gaps.size())&&sampled.size()<maximum_samples;++i)
                advanced=add_depth(gaps[i].second)||advanced;
        }
        if(!advanced)break;
    }
    throw std::runtime_error("Sheet Cut cannot resolve full profile clearance within the permitted cut deviation.");
}
