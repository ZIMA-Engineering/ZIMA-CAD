// Explicit calculation only. Native curve packets are converted without sampling.
occ::handle<Geom_BSplineCurve> loft_network_curve(const TopoDS_Wire& wire,double tolerance) {
    GeomConvert_CompCurveToBSplineCurve joined;
    for(BRepTools_WireExplorer it(wire);it.More();it.Next()) {
        double first,last;auto curve=BRep_Tool::Curve(it.Current(),first,last);
        if(curve.IsNull())throw std::runtime_error("Loft contains a missing native curve.");
        occ::handle<Geom_TrimmedCurve> segment=new Geom_TrimmedCurve(curve,first,last);
        if(it.Current().Orientation()==TopAbs_REVERSED)segment->Reverse();
        if(!joined.Add(segment,tolerance,true,true))
            throw std::runtime_error("Loft requires one connected contour in each Sketch.");
    }
    const auto result=joined.BSplineCurve();
    if(result.IsNull())throw std::runtime_error("Loft requires nonempty Sketches.");
    return result;
}
// A closed guide can cross each station more than once. Keep every branch
// between opposite end profiles; Gordon then validates the intermediate network.
std::vector<occ::handle<Geom_Curve>> loft_guide_branches(
    const TopoDS_Wire& guide,const TopoDS_Wire& first,const TopoDS_Wire& last,double tolerance) {
    const auto curve=loft_network_curve(guide,tolerance);
    if(!guide.Closed()&&!curve->IsClosed())return {curve};
    struct Contact {double parameter;bool last;};std::vector<Contact> contacts;
    for(bool end:{false,true}) {
        BRepAlgoAPI_Section section(guide,end?last:first,false);section.Build();
        if(!section.IsDone())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
        for(TopExp_Explorer it(section.Shape(),TopAbs_VERTEX);it.More();it.Next()) {
            GeomAPI_ProjectPointOnCurve projection(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())),curve);
            if(projection.NbPoints()==0||projection.LowerDistance()>tolerance)continue;
            double parameter=projection.LowerDistanceParameter();
            if(std::abs(parameter-curve->LastParameter())<1e-10)parameter=curve->FirstParameter();
            if(std::ranges::none_of(contacts,[&](const auto& old){return std::abs(old.parameter-parameter)<1e-10;}))
                contacts.push_back({parameter,end});
        }
    }
    std::ranges::sort(contacts,{},&Contact::parameter);
    std::vector<occ::handle<Geom_Curve>> branches;
    for(std::size_t i=0;i<contacts.size();++i) {
        const auto& a=contacts[i];const auto& b=contacts[(i+1)%contacts.size()];
        if(a.last==b.last)continue;
        occ::handle<Geom_Curve> branch;
        if(i+1<contacts.size())branch=new Geom_TrimmedCurve(curve,a.parameter,b.parameter);
        else {
            GeomConvert_CompCurveToBSplineCurve joined;
            for(const auto interval:{std::pair{a.parameter,curve->LastParameter()},std::pair{curve->FirstParameter(),b.parameter}})
                if(interval.second-interval.first>1e-10&&!joined.Add(new Geom_TrimmedCurve(curve,interval.first,interval.second),tolerance,true,true))
                    throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
            branch=joined.BSplineCurve();
        }
        if(a.last)branch->Reverse();
        branches.push_back(std::move(branch));
    }
    if(branches.empty())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
    return branches;
}
std::pair<TopoDS_Face,double> loft_network_face(
        const NCollection_Array1<occ::handle<Geom_Curve>>& profiles,
        const NCollection_Array1<occ::handle<Geom_Curve>>& rails,double tolerance,bool numbered_patch=false) {
        // Exact native contacts should not inherit a coarse parameterization
        // tolerance: it can make a valid rational skin unsuitable for offsets.
        // Retry at the authored contact budget only when the fine network fails.
        double construction_tolerance=std::min(tolerance*.01,1e-7);
        GeomFill_Gordon build;build.Init(profiles,rails,construction_tolerance);
        build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);
        build.Perform();
        if(!build.IsDone()&&construction_tolerance<tolerance) {
            construction_tolerance=tolerance;build.Init(profiles,rails,tolerance);
            build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);build.Perform();
        }
        if(!build.IsDone()&&build.Status()==GeomFill_Gordon::ResultStatus::CurveCompatibilityFailed&&
           !(numbered_patch&&profiles.Size()==2&&rails.Size()==2)) {
            // OCCT's exact network path requires rational families to already
            // share a basis. Degree elevation and knot insertion preserve the
            // complete curves; unit weights retain polynomial members exactly.
            const auto common_basis=[](const auto& curves) {
                GeomFill_Profiler basis;
                for(const auto& curve:curves)basis.AddCurve(occ::down_cast<Geom_Curve>(curve->Copy()));
                basis.Perform(Precision::PConfusion());
                NCollection_Array1<occ::handle<Geom_Curve>> prepared(curves.Lower(),curves.Upper());
                for(int i=curves.Lower();i<=curves.Upper();++i) {
                    const auto curve=occ::down_cast<Geom_BSplineCurve>(basis.Curve(i-curves.Lower()+1));
                    NCollection_Array1<double> weights(1,curve->NbPoles());curve->Weights(weights);
                    prepared(i)=new Geom_BSplineCurve(curve->Poles(),weights,curve->Knots(),
                        curve->Multiplicities(),curve->Degree(),curve->IsPeriodic(),false);
                }
                return prepared;
            };
            const auto prepared_profiles=common_basis(profiles),prepared_rails=common_basis(rails);
            construction_tolerance=std::min(tolerance*.01,1e-7);
            build.Init(prepared_profiles,prepared_rails,construction_tolerance);
            build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);build.Perform();
            if(!build.IsDone()&&construction_tolerance<tolerance) {
                construction_tolerance=tolerance;build.Init(prepared_profiles,prepared_rails,tolerance);
                build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);build.Perform();
            }
        }
        if(!build.IsDone()&&numbered_patch&&profiles.Size()==2&&rails.Size()==2&&
           build.Status()==GeomFill_Gordon::ResultStatus::CurveCompatibilityFailed) {
            // A two-station numbered span has four exact boundaries. The
            // native stretch patch supports mixed rational/polynomial curves
            // which Gordon 8.0.1 cannot combine. No sampled surface is used.
            const auto copy=[](const auto& curve) {
                return GeomConvert::CurveToBSplineCurve(occ::down_cast<Geom_Curve>(curve->Copy()));
            };
            GeomFill_BSplineCurves patch(copy(profiles(1)),copy(rails(2)),
                copy(profiles(2)),copy(rails(1)),GeomFill_StretchStyle);
            const auto surface=patch.Surface();
            if(surface.IsNull())throw std::runtime_error("Loft could not construct the profile transition.");
            double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);
            // Certify all four boundaries using common B-spline coefficients.
            // Equal positive weights and bounded pole displacement bound the
            // complete curves, including between display/test samples.
            const auto same_boundary=[&](const auto& authored,const auto& actual) {
                auto expected=copy(authored);auto edge=copy(actual);
                if(expected->StartPoint().Distance(edge->StartPoint())>
                   expected->EndPoint().Distance(edge->StartPoint()))expected->Reverse();
                GeomFill_Profiler basis;basis.AddCurve(expected);basis.AddCurve(edge);
                basis.Perform(Precision::PConfusion());
                expected=occ::down_cast<Geom_BSplineCurve>(basis.Curve(1));
                edge=occ::down_cast<Geom_BSplineCurve>(basis.Curve(2));
                if(expected->NbPoles()!=edge->NbPoles())return false;
                for(int i=1;i<=expected->NbPoles();++i)
                    if(expected->Pole(i).Distance(edge->Pole(i))>tolerance||
                       std::abs(expected->Weight(i)/expected->Weight(1)-edge->Weight(i)/edge->Weight(1))>1e-10)return false;
                return true;
            };
            if(!same_boundary(profiles(1),surface->VIso(v0))||!same_boundary(profiles(2),surface->VIso(v1))||
               !same_boundary(rails(1),surface->UIso(u0))||!same_boundary(rails(2),surface->UIso(u1)))
                throw std::runtime_error("Loft exceeds the document geometry tolerance.");
            const double precision=std::min(tolerance*.01,1e-7);
            return {BRepBuilderAPI_MakeFace(surface,precision).Face(),precision};
        }
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,
            "Loft network: profiles=%zu rails=%zu done=%d status=%d\n",profiles.Size(),rails.Size(),int(build.IsDone()),int(build.Status()));
        if(!build.IsDone()||build.IsApproximate())
            throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
        const auto& report=build.Report();
        if(std::max({report.MaxContactGap,report.MaxReparametrizationDeviation,
                     report.MaxProfileDeviation,report.MaxGuideDeviation})>tolerance)
            throw std::runtime_error("Loft exceeds the document geometry tolerance.");
        const auto skin=BRepBuilderAPI_MakeFace(build.Surface(),construction_tolerance).Face();
        return {skin,construction_tolerance};
}
PrimitiveData make_loft_data(const LoftRequest& request,const std::string& owner) {
    const double tolerance=request.tolerance;
    if(request.profiles.size()<2||!std::isfinite(tolerance)||tolerance<=0||
       request.profile_owners.size()!=request.profiles.size()||request.guide_owners.size()!=request.guides.size())
        throw std::runtime_error("Loft requires at least two different profile Sketches.");
    if(request.guides.size()==1&&!request.match_profile_vertices)
        throw std::runtime_error("A single Loft guide requires matching numbered profile points.");
    if(request.thin) {
        if(!request.make_solid)throw std::runtime_error("Thin Loft requires a solid result.");
        auto skin_request=request;skin_request.thin=false;
        const bool closed=std::ranges::all_of(request.profiles,[](const auto& p){return p.open_profile_end_id.empty();});
        skin_request.make_solid=closed;
        auto skin=make_loft_data(skin_request,owner);
        if(closed) {
            // Orient the closed carrier once, then discard its end caps.
            // Inward/outward thickness uses actual material sides, independent
            // of Gordon's internal curve sorting or surface parameterization.
            std::erase_if(skin.faces,[](const auto& face){return face.reference.semantic_key.starts_with("loft:cap:");});
            TopoDS_Shell shell;BRep_Builder builder;builder.MakeShell(shell);
            for(const auto& face:skin.faces)builder.Add(shell,face.shape);
            skin.shape=shell;
        }
        if(skin.faces.empty())throw std::runtime_error("Thin Loft has no surface to thicken.");
        for(auto& face:skin.faces)face.reference.surface_result=true;
        // Offset construction needs a finer working tolerance than network
        // contact validation. Keep the requested input budget unchanged.
        SurfaceThickenRequest thickness{skin.faces.front().reference,request.thickness,request.thickness_side,std::min(tolerance,1e-7)};
        return make_thickened_surface_data(thickness,owner,skin.shape,skin.faces,skin.edges,skin.vertices).operand;
    }
    std::vector<TopoDS_Wire> wires,guides;
    std::vector<std::vector<TopoDS_Edge>> source_edges;
    const auto prepare=[&](const auto& family,auto& output,bool profile) {
        for(const auto& input:family) {
            if(!input.inner_profiles.empty()||!input.additional_profile_regions.empty())
                throw std::runtime_error("Loft requires one contour without holes in each Sketch.");
            if(profile&&request.make_solid&&!input.open_profile_end_id.empty())
                throw std::runtime_error("A solid Loft requires closed profile Sketches.");
            std::vector<TopoDS_Edge> edges;
            const auto radial=profile&&output.size()<request.profile_radial_directions.size()?request.profile_radial_directions[output.size()]:std::nullopt;
            output.push_back(make_profile_wire(input.outer_profile,input.direction,radial,&edges));
            if(profile)source_edges.push_back(std::move(edges));
        }
    };
    prepare(request.profiles,wires,true);prepare(request.guides,guides,false);
    PrimitiveData result;
    bool bounded_network=false;
    std::vector<std::pair<TopoDS_Shape,std::string>> authored_faces;
    std::vector<std::pair<TopoDS_Shape,std::string>> authored_longitudinals;
    bool split_profiles=std::ranges::any_of(source_edges,[&](const auto& edges){return edges.size()!=source_edges.front().size();});
    if(guides.empty()) {
        BRepOffsetAPI_ThruSections build(request.make_solid,false,tolerance);
        for(const auto& wire:wires)build.AddWire(wire);
        build.CheckCompatibility(!request.match_profile_vertices);
        build.Build();
        if(!build.IsDone())throw std::runtime_error("Loft could not construct the profile transition.");
        result.shape=build.Shape();
        for(std::size_t edge=0;edge<source_edges.front().size();++edge) {
            const auto& profile=request.profiles.front();
            const auto key="loft:skin:from:"+request.profile_owners.front()+":"+
                (edge<profile.outer_edge_source_ids.size()?profile.outer_edge_source_ids[edge]:profile.outer_boundary_id);
            const auto& generated=build.Generated(source_edges.front()[edge]);
            std::vector<TopoDS_Shape> faces;
            for(NCollection_List<TopoDS_Shape>::Iterator it(generated);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE)faces.push_back(it.Value());
            for(const auto& face:faces) {
                auto identity=key;
                if(faces.size()>1) {
                    split_profiles=true;
                    // Compatibility can divide one smooth source curve into
                    // several spans. Identify each span by its authored curve
                    // partners, never by OCCT traversal or a generated index.
                    for(std::size_t station=1;station<source_edges.size();++station)
                        for(std::size_t other=0;other<source_edges[station].size();++other) {
                            const auto& partners=build.Generated(source_edges[station][other]);
                            for(NCollection_List<TopoDS_Shape>::Iterator it(partners);it.More();it.Next())
                                if(face.IsSame(it.Value())) {
                                    const auto& p=request.profiles[station];
                                    identity+=":with:"+request.profile_owners[station]+":"+
                                        (other<p.outer_edge_source_ids.size()?p.outer_edge_source_ids[other]:p.outer_boundary_id);
                                    break;
                                }
                        }
                }
                authored_faces.emplace_back(face,std::move(identity));
            }
        }
        for(std::size_t station=0;station<source_edges.size();++station) {
            const auto& p=request.profiles[station];
            for(std::size_t point=0;point<source_edges[station].size();++point) {
                const auto vertex=TopExp::FirstVertex(source_edges[station][point],true);
                const auto parent=request.profile_owners[station]+":"+
                    (point<p.outer_vertex_source_ids.size()?p.outer_vertex_source_ids[point]:p.outer_boundary_id);
                const auto& generated=build.Generated(vertex);
                for(NCollection_List<TopoDS_Shape>::Iterator it(generated);it.More();it.Next())
                    if(it.Value().ShapeType()==TopAbs_EDGE)authored_longitudinals.emplace_back(it.Value(),parent);
            }
        }
        std::map<std::string,std::size_t> span_counts;
        for(const auto& [face,key]:authored_faces)++span_counts[key];
        for(auto& [face,key]:authored_faces)if(span_counts.at(key)>1) {
            // A periodic source seam can also split the opposite polygon
            // edge. The two children share curve parents but have different
            // authored point parents on their longitudinal boundaries.
            for(const auto& [span,parent]:authored_longitudinals)
                for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())if(it.Current().IsSame(span)) {
                    key+=":bounded-by:"+parent;break;
                }
        }
        if(request.make_solid) {
            authored_faces.emplace_back(build.FirstShape(),"loft:cap:from:"+request.profile_owners.front());
            authored_faces.emplace_back(build.LastShape(),"loft:cap:from:"+request.profile_owners.back());
        }
    } else if(request.match_profile_vertices) {
        const auto spans=source_edges.front().size();
        const bool closed=request.profiles.front().open_profile_end_id.empty();
        const auto count=spans+(closed?0:1);
        for(std::size_t station=0;station<wires.size();++station)
            if(source_edges[station].size()!=spans||request.profiles[station].open_profile_end_id.empty()!=closed)
                throw std::runtime_error("Loft correspondence requires the same number of points on every profile.");
        const auto vertex=[&](std::size_t station,std::size_t point) {
            return point==spans?TopExp::LastVertex(source_edges[station].back(),true):
                TopExp::FirstVertex(source_edges[station][point],true);
        };
        // The matched profile-only Loft supplies the ordinary interpolating
        // longitudinal curves. Explicit side guides replace their own spans;
        // no sampled or invented polyline approximation is introduced.
        BRepOffsetAPI_ThruSections baseline(false,false,tolerance);
        for(const auto& wire:wires)baseline.AddWire(wire);
        baseline.CheckCompatibility(false);baseline.Build();
        if(!baseline.IsDone())throw std::runtime_error("Loft could not construct the profile transition.");
        std::vector<occ::handle<Geom_Curve>> rails(count);
        for(std::size_t point=0;point<count;++point) {
            const auto& generated=baseline.Generated(vertex(0,point));
            for(NCollection_List<TopoDS_Shape>::Iterator it(generated);it.More();it.Next())if(it.Value().ShapeType()==TopAbs_EDGE) {
                BRepBuilderAPI_MakeWire wire(TopoDS::Edge(it.Value()));
                if(!rails[point].IsNull())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
                rails[point]=loft_network_curve(wire.Wire(),tolerance);
            }
            if(rails[point].IsNull())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
        }
        std::set<std::size_t> assigned;
        for(const auto& guide:guides)for(const auto& branch:loft_guide_branches(guide,wires.front(),wires.back(),tolerance)) {
            std::optional<std::size_t> match;
            for(std::size_t point=0;point<count;++point) {
                bool touches=true;std::vector<double> parameters;
                for(std::size_t station=0;station<wires.size();++station) {
                    GeomAPI_ProjectPointOnCurve projection(BRep_Tool::Pnt(vertex(station,point)),branch);
                    if(projection.NbPoints()==0||projection.LowerDistance()>tolerance){touches=false;break;}
                    parameters.push_back(projection.LowerDistanceParameter());
                }
                if(touches) {
                    const bool forward=parameters.back()>parameters.front();
                    for(std::size_t i=1;i<parameters.size();++i)
                        if(forward?parameters[i]<=parameters[i-1]:parameters[i]>=parameters[i-1])touches=false;
                }
                if(touches){if(match)throw std::runtime_error("Loft guide must pass through the same numbered point on every profile.");match=point;}
            }
            if(!match||!assigned.insert(*match).second)
                throw std::runtime_error("Loft guide must pass through the same numbered point on every profile.");
            rails[*match]=branch;
        }
        double construction_tolerance=std::min(tolerance*.01,1e-7);
        for(std::size_t span=0;span<spans;++span) {
            NCollection_Array1<occ::handle<Geom_Curve>> profiles(1,static_cast<int>(wires.size()));
            for(std::size_t station=0;station<wires.size();++station) {
                BRepBuilderAPI_MakeWire wire(source_edges[station][span]);
                profiles(static_cast<int>(station+1))=loft_network_curve(wire.Wire(),tolerance);
            }
            NCollection_Array1<occ::handle<Geom_Curve>> sides(1,2);
            sides(1)=rails[span];sides(2)=rails[(span+1)%count];
            const auto identity="loft:skin:from:"+request.profile_owners.front()+":"+request.profiles.front().outer_edge_source_ids.at(span);
            try {
                const auto [face,precision]=loft_network_face(profiles,sides,tolerance,true);
                construction_tolerance=std::max(construction_tolerance,precision);
                authored_faces.emplace_back(face,identity);
            } catch(const std::runtime_error&) {
                if(wires.size()<=2)throw;
                bounded_network=true;
                // Mixed rational stations can have incompatible longitudinal
                // parameterizations for a single Gordon patch. Preserve every
                // authored station and complete guide by bounding patches at
                // their exact contacts, using the existing four-boundary path.
                std::vector<std::array<double,2>> contacts(wires.size());
                for(std::size_t station=0;station<wires.size();++station)
                    for(unsigned side=0;side<2;++side) {
                        GeomAPI_ProjectPointOnCurve projection(BRep_Tool::Pnt(vertex(station,(span+side)%count)),sides(side+1));
                        if(projection.NbPoints()==0||projection.LowerDistance()>tolerance)throw;
                        contacts[station][side]=projection.LowerDistanceParameter();
                    }
                for(std::size_t station=0;station+1<wires.size();++station) {
                    NCollection_Array1<occ::handle<Geom_Curve>> bounded_profiles(1,2),bounded_sides(1,2);
                    bounded_profiles(1)=profiles(int(station+1));bounded_profiles(2)=profiles(int(station+2));
                    for(unsigned side=0;side<2;++side) {
                        const double a=contacts[station][side],b=contacts[station+1][side];
                        if(std::abs(a-b)<=Precision::PConfusion())throw;
                        occ::handle<Geom_TrimmedCurve> bounded=new Geom_TrimmedCurve(sides(side+1),std::min(a,b),std::max(a,b));
                        if(b<a)bounded->Reverse();bounded_sides(side+1)=bounded;
                    }
                    const auto [face,precision]=loft_network_face(bounded_profiles,bounded_sides,tolerance,true);
                    construction_tolerance=std::max(construction_tolerance,precision);
                    authored_faces.emplace_back(face,identity+":between:"+request.profile_owners[station]+":"+request.profile_owners[station+1]);
                }
            }
        }
        if(request.make_solid)for(const auto endpoint:{std::size_t{0},wires.size()-1}) {
            BRepBuilderAPI_MakeFace cap(wires[endpoint],true);
            if(!cap.IsDone())throw std::runtime_error("Loft requires planar closed end profiles.");
            authored_faces.emplace_back(cap.Face(),"loft:cap:from:"+request.profile_owners[endpoint]);
        }
        BRepBuilderAPI_Sewing sewing(construction_tolerance);
        for(const auto& [face,key]:authored_faces)sewing.Add(face);
        sewing.Perform();
        for(auto& [face,key]:authored_faces)if(sewing.IsModifiedSubShape(face))face=sewing.ModifiedSubShape(face);
        result.shape=sewing.SewedShape();
        if(request.make_solid) {
            if(result.shape.ShapeType()!=TopAbs_SHELL)throw std::runtime_error("Loft did not form one closed shell.");
            BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(result.shape));auto shape=solid.Solid();
            if(!BRepLib::OrientClosedSolid(shape))throw std::runtime_error("Loft did not form one closed solid.");
            result.shape=shape;
        }
    } else {
        NCollection_Array1<occ::handle<Geom_Curve>> profiles(1,static_cast<int>(wires.size()));
        std::vector<occ::handle<Geom_Curve>> branches;
        for(const auto& guide:guides) {
            auto parts=loft_guide_branches(guide,wires.front(),wires.back(),tolerance);
            branches.insert(branches.end(),parts.begin(),parts.end());
        }
        NCollection_Array1<occ::handle<Geom_Curve>> rails(1,static_cast<int>(branches.size()));
        for(std::size_t i=0;i<wires.size();++i)profiles(static_cast<int>(i+1))=loft_network_curve(wires[i],tolerance);
        for(std::size_t i=0;i<branches.size();++i)rails(static_cast<int>(i+1))=branches[i];
        const auto [skin,construction_tolerance]=loft_network_face(profiles,rails,tolerance);
        authored_faces.emplace_back(skin,"loft:skin:from:"+request.region_id);
        result.shape=skin;
        if(request.make_solid) {
            BRepBuilderAPI_Sewing sewing(construction_tolerance);sewing.Add(skin);
            for(const auto endpoint:{std::size_t{0},wires.size()-1}) {
                BRepBuilderAPI_MakeFace cap(wires[endpoint],true);
                if(!cap.IsDone())throw std::runtime_error("Loft requires planar closed end profiles.");
                sewing.Add(cap.Face());
                authored_faces.emplace_back(cap.Face(),"loft:cap:from:"+request.profile_owners[endpoint]);
            }
            sewing.Perform();
            for(auto& [face,key]:authored_faces)if(sewing.IsModifiedSubShape(face))face=sewing.ModifiedSubShape(face);
            auto shell=sewing.SewedShape();
            if(shell.ShapeType()!=TopAbs_SHELL)throw std::runtime_error("Loft did not form one closed shell.");
            BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(shell));
            auto shape=solid.Solid();
            if(!BRepLib::OrientClosedSolid(shape))throw std::runtime_error("Loft did not form one closed solid.");
            result.shape=shape;
        }
    }
    if(result.shape.IsNull()||!BRepCheck_Analyzer(result.shape).IsValid())
        throw std::runtime_error("Loft produced invalid native geometry.");
    std::set<std::string> face_keys;
    for(TopExp_Explorer it(result.shape,TopAbs_FACE);it.More();it.Next()) {
        bool found=false;
        for(const auto& [source,key]:authored_faces)if(!source.IsNull()&&it.Current().IsSame(source)) {
            FaceReference reference{owner,key,{}};reference.surface_result=!request.make_solid;
            if(!face_keys.insert(key).second)throw std::runtime_error("Loft face ancestry could not be resolved.");
            result.faces.push_back({it.Current(),std::move(reference)});found=true;break;
        }
        if(!found)throw std::runtime_error("Loft face ancestry could not be resolved.");
    }
    std::vector<TopoDS_Vertex> seen_vertices;std::set<std::string> vertex_keys;
    std::vector<std::size_t> boundary_stations{0,wires.size()-1};
    if(bounded_network)for(std::size_t station=1;station+1<wires.size();++station)boundary_stations.push_back(station);
    for(TopExp_Explorer it(result.shape,TopAbs_VERTEX);it.More();it.Next()) {
        const auto vertex=TopoDS::Vertex(it.Current());
        if(std::ranges::any_of(seen_vertices,[&](const auto& old){return old.IsSame(vertex);}))continue;
        seen_vertices.push_back(vertex);const auto position=BRep_Tool::Pnt(vertex);std::string key;
        for(const auto endpoint:boundary_stations) {
            const auto& p=request.profiles[endpoint];
            for(std::size_t i=0;i<source_edges[endpoint].size();++i) {
                if(position.Distance(BRep_Tool::Pnt(TopExp::FirstVertex(source_edges[endpoint][i],true)))>tolerance)continue;
                key="loft:point:from:"+request.profile_owners[endpoint]+":"+
                    (i<p.outer_vertex_source_ids.size()?p.outer_vertex_source_ids[i]:p.outer_boundary_id);break;
            }
            if(key.empty()&&!p.open_profile_end_id.empty()&&
               position.Distance(BRep_Tool::Pnt(TopExp::LastVertex(source_edges[endpoint].back(),true)))<=tolerance)
                key="loft:point:from:"+request.profile_owners[endpoint]+":"+p.open_profile_end_id;
            if(key.empty()&&p.open_profile_end_id.empty()) {
                BRepExtrema_DistShapeShape distance(vertex,wires[endpoint]);
                if(distance.IsDone()&&distance.Value()<=tolerance) {
                    if(split_profiles)for(const auto& [span,parent]:authored_longitudinals)
                        if(vertex.IsSame(TopExp::FirstVertex(TopoDS::Edge(span),true))||
                           vertex.IsSame(TopExp::LastVertex(TopoDS::Edge(span),true))) {
                            key="loft:point:from:"+request.profile_owners[endpoint]+":corresponding:"+parent;break;
                        }
                    if(key.empty())key="loft:point:seam:from:"+request.profile_owners[endpoint];
                }
            }
            if(!key.empty())break;
        }
        if(key.empty()||!vertex_keys.insert(key).second)throw std::runtime_error("Loft point ancestry could not be resolved.");
        result.vertices.push_back({vertex,{owner,std::move(key),{}}});
    }
    const auto lies_on=[&](const TopoDS_Edge& candidate,const TopoDS_Edge& source) {
        BRepAdaptor_Curve curve(candidate);
        for(double f:{0.,.125,.25,.5,.75,.875,1.}) {
            const auto point=curve.Value(curve.FirstParameter()+f*(curve.LastParameter()-curve.FirstParameter()));
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),source);
            if(!distance.IsDone()||distance.Value()>tolerance)return false;
        }
        return true;
    };
    std::vector<TopoDS_Edge> seen_edges;std::set<std::string> edge_keys;
    for(TopExp_Explorer it(result.shape,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());
        if(BRep_Tool::Degenerated(edge)||std::ranges::any_of(seen_edges,[&](const auto& old){return old.IsSame(edge);}))continue;
        seen_edges.push_back(edge);std::string key;
        for(const auto endpoint:boundary_stations) {
            const auto& p=request.profiles[endpoint];
            for(std::size_t i=0;i<source_edges[endpoint].size();++i)if(lies_on(edge,source_edges[endpoint][i])) {
                key="loft:edge:from:"+request.profile_owners[endpoint]+":"+
                    (i<p.outer_edge_source_ids.size()?p.outer_edge_source_ids[i]:p.outer_boundary_id);break;
            }
            if(!key.empty())break;
        }
        if(key.empty())for(const auto& vertex:result.vertices) {
            const bool starts_span=std::ranges::any_of(boundary_stations,[&](const auto station){
                return station+1<wires.size()&&
                    (vertex.reference.semantic_key.starts_with("loft:point:from:"+request.profile_owners[station]+":")||
                     vertex.reference.semantic_key=="loft:point:seam:from:"+request.profile_owners[station]);
            });
            if(!starts_span)continue;
            if(vertex.shape.IsSame(TopExp::FirstVertex(edge,true))||vertex.shape.IsSame(TopExp::LastVertex(edge,true))) {
                key="loft:longitudinal:from:"+vertex.reference.semantic_key;break;
            }
        }
        if(key.empty())throw std::runtime_error("Loft edge ancestry could not be resolved.");
        if(bounded_network&&key.starts_with("loft:longitudinal:from:")) {
            std::vector<std::string> ends;
            for(const auto& point:result.vertices)
                if(point.shape.IsSame(TopExp::FirstVertex(edge,true))||point.shape.IsSame(TopExp::LastVertex(edge,true)))
                    ends.push_back(point.reference.semantic_key);
            std::ranges::sort(ends);
            if(ends.size()!=2)throw std::runtime_error("Loft edge ancestry could not be resolved.");
            key="loft:longitudinal:from:"+ends.front()+":to:"+ends.back();
        }
        result.edges.push_back({edge,{owner,std::move(key),{}}});
    }
    // Splitting a smooth end curve leaves several rim edges with the same
    // curve parent. Their incident authored skin spans distinguish the children.
    std::map<std::string,std::size_t> rim_counts;
    for(const auto& edge:result.edges)++rim_counts[edge.reference.semantic_key];
    for(auto& edge:result.edges) {
        auto& key=edge.reference.semantic_key;
        if(rim_counts.at(key)>1&&key.starts_with("loft:edge:from:")) {
            for(const auto& face:result.faces) {
                if(!face.reference.semantic_key.starts_with("loft:skin:from:"))continue;
                for(TopExp_Explorer it(face.shape,TopAbs_EDGE);it.More();it.Next())if(it.Current().IsSame(edge.shape)) {
                    key+=":span:"+face.reference.semantic_key;break;
                }
            }
        }
        if(!edge_keys.insert(key).second)throw std::runtime_error("Loft edge ancestry could not be resolved.");
    }
    return result;
}
