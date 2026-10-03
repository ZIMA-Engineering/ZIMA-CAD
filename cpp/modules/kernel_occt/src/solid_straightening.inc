namespace solid_straightening {

Vec3 vector(const gp_XYZ& p) { return {p.X(),p.Y(),p.Z()}; }
gp_Pnt point(Vec3 p) { return {p.x,p.y,p.z}; }
std::array<Vec3,4> frame_values(const gp_Trsf& transform) {
    return {vector(gp_Pnt{}.Transformed(transform).XYZ()),
        vector(gp_Vec(1,0,0).Transformed(transform).XYZ()),
        vector(gp_Vec(0,1,0).Transformed(transform).XYZ()),
        vector(gp_Vec(0,0,1).Transformed(transform).XYZ())};
}

void transform_loop(ExtrusionRequest::ProfileLoop& loop,const gp_Trsf& transform) {
    const auto position=[&](Vec3& p){p=vector(point(p).Transformed(transform).XYZ());};
    const auto direction=[&](Vec3& p){p=vector(gp_Vec(p.x,p.y,p.z).Transformed(transform).XYZ());};
    std::visit([&](auto& profile) {
        if constexpr(requires{profile.vertices;})for(auto& p:profile.vertices)position(p);
        else if constexpr(requires{profile.center;}) {
            position(profile.center);
            if constexpr(requires{profile.major_axis_direction;})direction(profile.major_axis_direction);
        } else for(auto& curve:profile.curves)std::visit([&](auto& c) {
            position(c.start);position(c.end);
            if constexpr(requires{c.middle;})position(c.middle);
            if constexpr(requires{c.center;})position(c.center);
            if constexpr(requires{c.major_axis_direction;})direction(c.major_axis_direction);
            if constexpr(requires{c.control_points;})for(auto& p:c.control_points)position(p);
        },curve);
    },loop);
}

template<class From,class To> void copy_profile(const From& from,To& to) {
    to.profile_region_id=from.profile_region_id;
    to.outer_boundary_id=from.outer_boundary_id;to.inner_boundary_ids=from.inner_boundary_ids;
    to.outer_edge_source_ids=from.outer_edge_source_ids;to.inner_edge_source_ids=from.inner_edge_source_ids;
    to.outer_vertex_source_ids=from.outer_vertex_source_ids;to.inner_vertex_source_ids=from.inner_vertex_source_ids;
    to.outer_profile=from.outer_profile;to.inner_profiles=from.inner_profiles;
    to.additional_profile_regions=from.additional_profile_regions;to.wall=from.wall;
    to.first_cap_is_start=from.first_cap_is_start;to.centerlines=from.centerlines;
}

SolidStraighteningPlan revolution(const RevolutionRequest& source,double coefficient) {
    if(!std::isfinite(coefficient)||coefficient<=0)
        throw std::invalid_argument("Straightening coefficient must be positive and finite.");
    if(source.surface_result)throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    validate_revolution(source);
    // Measure the exact filled section, including holes and profile offsets.
    // A bounding box or a sampled display polygon is not a section centroid.
    const auto measure=[](const RevolutionRequest& input) {
        const auto profiles=make_body_profiles(input,input.profile_normal);
        std::vector<TopoDS_Wire> wires;for(const auto& profile:profiles)wires.push_back(profile.wire);
        GProp_GProps properties;BRepGProp::SurfaceProperties(profile_base(wires,false),properties);
        return properties;
    };
    auto properties=measure(source);
    for(const auto& region:source.additional_profile_regions) {
        auto child=source;child.additional_profile_regions.clear();
        child.profile_region_id=region.region_id;child.outer_boundary_id=region.outer_boundary_id;
        child.inner_boundary_ids=region.inner_boundary_ids;child.outer_profile=region.outer_profile;
        child.inner_profiles=region.inner_profiles;child.outer_edge_source_ids=region.outer_edge_source_ids;
        child.inner_edge_source_ids=region.inner_edge_source_ids;child.outer_vertex_source_ids=region.outer_vertex_source_ids;
        child.inner_vertex_source_ids=region.inner_vertex_source_ids;
        properties.Add(measure(child));
    }
    if(!std::isfinite(properties.Mass())||properties.Mass()<=0)
        throw std::invalid_argument("Straightening requires a valid cross-section area.");
    const gp_Ax1 axis(point(source.axis_point),gp_Dir(source.axis_direction.x,source.axis_direction.y,source.axis_direction.z));
    gp_Trsf start_rotation;start_rotation.SetRotation(axis,source.start_angle_degrees*std::numbers::pi/180.);
    const auto center=properties.CentreOfMass().Transformed(start_rotation);
    const auto radial=gp_Vec(axis.Location(),center);
    const auto tangent=gp_Vec(axis.Direction()).Crossed(radial);
    const double length=tangent.Magnitude()*source.angle_degrees*std::numbers::pi/180.;
    if(!std::isfinite(length*coefficient)||length<=Precision::Confusion())
        throw std::invalid_argument("Straightening requires a nonzero centroid trajectory.");
    const auto normal=gp_Vec(source.profile_normal.x,source.profile_normal.y,source.profile_normal.z).Transformed(start_rotation);
    if(normal.Magnitude()<Precision::Confusion()||std::abs(normal.Normalized().Dot(tangent.Normalized()))<1.-1e-9)
        throw std::invalid_argument("Straightening requires a section normal to its centroid trajectory.");
    ExtrusionRequest target;copy_profile(source,target);
    transform_loop(target.outer_profile,start_rotation);
    for(auto& loop:target.inner_profiles)transform_loop(loop,start_rotation);
    for(auto& region:target.additional_profile_regions) {
        transform_loop(region.outer_profile,start_rotation);
        for(auto& loop:region.inner_profiles)transform_loop(loop,start_rotation);
    }
    target.centerlines.origin=vector(point(target.centerlines.origin).Transformed(start_rotation).XYZ());
    const auto center_normal=target.centerlines.normal;
    target.centerlines.normal=vector(gp_Vec(center_normal.x,center_normal.y,center_normal.z).Transformed(start_rotation).XYZ());
    const auto displacement=tangent.Normalized()*(length*coefficient);
    target.direction=vector(displacement.XYZ());
    gp_Trsf end_rotation;end_rotation.SetRotation(axis,source.angle_degrees*std::numbers::pi/180.);
    SolidStraighteningPlan result;result.primitive=std::move(target);
    result.section_area=properties.Mass();result.centroid_path_length=length;result.coefficient=coefficient;
    result.source_start_centroid=vector(center.XYZ());
    result.source_end_centroid=vector(center.Transformed(end_rotation).XYZ());
    result.straight_end_centroid=vector(center.Translated(displacement).XYZ());
    result.end_transform=frame_values(end_rotation);result.start_tangent=vector(tangent.Normalized().XYZ());
    return result;
}

// Use the same discrete transport as H-Sweep. Simulate evaluates section
// locations without constructing a body; its wires retain the exact section.
// Refine the centroid trajectory independently of the display mesh deviation.
SolidStraighteningPlan transported_sweep(const Sweep3DRequest& source,double coefficient) {
    if(!std::isfinite(coefficient)||coefficient<=0)
        throw std::invalid_argument("Straightening coefficient must be positive and finite.");
    if(!source.make_solid||!source.transported||source.sections.size()!=1)
        throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    validate_sweep3d(source);
    const auto& section=source.sections.front();
    GeomConvert_CompCurveToBSplineCurve curve;
    std::vector<double> source_lengths;double spine_length=0;
    for(const auto& segment:source.path_segments) {
        TColgp_Array1OfPnt poles(1,4);
        for(int i=0;i<4;++i)poles.SetValue(i+1,point(segment.bezier_control_points.at(i)));
        Handle(Geom_BezierCurve) part=new Geom_BezierCurve(poles);
        GeomAdaptor_Curve adaptor(part);
        const double length=GCPnts_AbscissaPoint::Length(adaptor,source.linear_tolerance*.01/source.path_segments.size());
        spine_length+=length;source_lengths.push_back(spine_length);
        if(!curve.Add(part,1e-7,true,true,1))throw std::runtime_error("Straightening could not join the source trajectory.");
    }
    auto spine=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(curve.BSplineCurve()).Edge()).Wire();
    gp_Pnt first;gp_Vec tangent;curve.BSplineCurve()->D1(curve.BSplineCurve()->FirstParameter(),first,tangent);
    tangent.Normalize();
    const gp_Vec normal(section.profile_normal.x,section.profile_normal.y,section.profile_normal.z);
    if(normal.Magnitude()<Precision::Confusion()||std::abs(normal.Normalized().Dot(tangent))<1.-1e-7)
        throw std::invalid_argument("Straightening requires a section normal to its centroid trajectory.");
    RevolutionRequest profile;
    profile.outer_profile=section.profile.outer_profile;profile.inner_profiles=section.profile.inner_profiles;
    profile.outer_edge_source_ids=section.profile.outer_edge_source_ids;profile.outer_vertex_source_ids=section.profile.outer_vertex_source_ids;
    profile.inner_edge_source_ids=section.profile.inner_edge_source_ids;profile.inner_vertex_source_ids=section.profile.inner_vertex_source_ids;
    if(source.thin)profile.wall=ProfileWall{source.thin_first,source.thin_second,section.thin_end_point_id};
    auto profiles=make_body_profiles(profile,section.profile_normal,section.circle_radial_direction);
    std::vector<TopoDS_Wire> wires;for(const auto& item:profiles)wires.push_back(item.wire);
    GProp_GProps properties;BRepGProp::SurfaceProperties(profile_base(wires,false),properties);
    const double area=properties.Mass();
    if(!std::isfinite(area)||area<=0)throw std::invalid_argument("Straightening requires a valid cross-section area.");
    // A single profile is rigidly transported. If its filled-area centroid is
    // on the guide, that centroid follows the guide exactly, independently of
    // the transport frame. Reuse the tolerance-controlled curve integrals;
    // only offset centroids need section sampling.
    const bool centered=properties.CentreOfMass().Distance(first)<=1e-12;
    std::vector<gp_Pnt> centers{first,curve.BSplineCurve()->Value(curve.BSplineCurve()->LastParameter())};
    std::vector<double> cumulative{0,spine_length};
    std::size_t intervals=1;
    if(!centered) {
        std::vector<std::unique_ptr<BRepOffsetAPI_MakePipeShell>> pipes;
        for(const auto& wire:wires) {
            auto pipe=std::make_unique<BRepOffsetAPI_MakePipeShell>(spine);
            pipe->SetDiscreteMode();pipe->SetTolerance(source.linear_tolerance/2,source.linear_tolerance/2,1e-6);
            pipe->SetMaxSegments(2000);pipe->Add(wire,false,false);pipes.push_back(std::move(pipe));
        }
        double previous=-1;unsigned stable=0;
        intervals=std::max<std::size_t>(32,source.path_segments.size()*2);
        for(;;intervals*=2) {
            if(intervals>65536)throw std::runtime_error("Straightening centroid trajectory did not converge to the feature precision.");
            centers.assign(intervals+2,gp_Pnt{});
            std::vector<double> areas(intervals+2,0);
            std::size_t section_count=0;
            for(std::size_t loop=0;loop<pipes.size();++loop) {
                TopTools_ListOfShape sections;pipes[loop]->Simulate(static_cast<int>(intervals+1),sections);
                if(loop==0)section_count=sections.Size();
                if((section_count!=intervals+1&&section_count!=intervals+2)||sections.Size()!=section_count)
                    throw std::runtime_error("Straightening section evaluation is incomplete.");
                std::size_t index=0;
                for(TopTools_ListIteratorOfListOfShape it(sections);it.More();it.Next(),++index) {
                    BRepBuilderAPI_MakeFace face(TopoDS::Wire(it.Value()),true);
                    if(!face.IsDone())throw std::runtime_error("Straightening section evaluation is incomplete.");
                    GProp_GProps sample;BRepGProp::SurfaceProperties(face.Face(),sample);
                    const double weight=std::abs(sample.Mass())*(loop==0?1.:-1.);
                    centers[index].SetXYZ(centers[index].XYZ()+sample.CentreOfMass().XYZ()*weight);
                    areas[index]+=weight;
                }
            }
            centers.resize(section_count);areas.resize(section_count);
            cumulative.assign(section_count,0);
            for(std::size_t i=0;i<centers.size();++i) {
                if(!std::isfinite(areas[i])||std::abs(areas[i]-area)>std::max(area*1e-8,1e-9))
                    throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
                centers[i].SetXYZ(centers[i].XYZ()/areas[i]);
                if(i)cumulative[i]=cumulative[i-1]+centers[i-1].Distance(centers[i]);
            }
            // OCCT's additive parameter stepping can emit its final endpoint
            // twice. Accept only a coincident extra endpoint, never missing data.
            if(section_count==intervals+2) {
                if(centers.back().Distance(centers[intervals])>1e-8)
                    throw std::runtime_error("Straightening section evaluation is incomplete.");
                centers.pop_back();cumulative.pop_back();
            }
            const double length=cumulative.back();
            if(previous>=0&&std::abs(length-previous)<=source.linear_tolerance*.125)++stable;else stable=0;
            if(stable>=2)break;
            previous=length;
        }
    }
    const double length=cumulative.back();
    if(!std::isfinite(length*coefficient)||length<=Precision::Confusion())
        throw std::invalid_argument("Straightening requires a nonzero centroid trajectory.");
    auto target=source;
    // Keep authored path/section identifiers. Approximation interval indices
    // never become new source identities when flattening the path.
    const auto start=point(source.path_points.front());
    target.path_points[0]=vector(start.XYZ());double prior=0;
    for(std::size_t i=0;i<target.path_segments.size();++i) {
        const double station=std::clamp(source_lengths[i]/spine_length,0.,1.)*intervals;
        const auto lower=std::min(static_cast<std::size_t>(station),intervals-1);
        const double distance=(cumulative[lower]+(cumulative[lower+1]-cumulative[lower])*(station-lower))*coefficient;
        auto& segment=target.path_segments[i];
        segment.start=vector(start.Translated(tangent*prior).XYZ());segment.end=vector(start.Translated(tangent*distance).XYZ());
        segment.bezier_control_points={segment.start,vector(start.Translated(tangent*(prior+(distance-prior)/3)).XYZ()),
            vector(start.Translated(tangent*(prior+2*(distance-prior)/3)).XYZ()),segment.end};
        target.path_points[i+1]=segment.end;prior=distance;
    }
    SolidStraighteningPlan result;result.primitive=std::move(target);result.section_area=area;
    result.centroid_path_length=length;result.coefficient=coefficient;
    result.source_start_centroid=vector(centers.front().XYZ());result.source_end_centroid=vector(centers.back().XYZ());
    result.straight_end_centroid=vector(centers.front().Translated(tangent*(length*coefficient)).XYZ());
    // The same discrete trihedron as MakePipeShell; evaluate two frames only,
    // without constructing another swept body or sampling display geometry.
    GeomFill_DiscreteTrihedron law;law.SetCurve(new GeomAdaptor_Curve(curve.BSplineCurve()));
    const auto frame=[&](double parameter) {
        gp_Vec t,n,b;if(!law.D0(parameter,t,n,b))
            throw std::runtime_error("Straightening section evaluation is incomplete.");
        return gp_Ax3(curve.BSplineCurve()->Value(parameter),gp_Dir(t),gp_Dir(n));
    };
    gp_Trsf transport;transport.SetDisplacement(frame(curve.BSplineCurve()->FirstParameter()),frame(curve.BSplineCurve()->LastParameter()));
    result.end_transform=frame_values(transport);result.start_tangent=vector(tangent.XYZ());
    return result;
}

bool same_loop(const ExtrusionRequest::ProfileLoop& a,const ExtrusionRequest::ProfileLoop& b) {
    const auto scalar=[](double x,double y){return std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-8;};
    const auto position=[&](Vec3 x,Vec3 y){return scalar(x.x,y.x)&&scalar(x.y,y.y)&&scalar(x.z,y.z);};
    const auto positions=[&](const auto& x,const auto& y){
        return x.size()==y.size()&&std::equal(x.begin(),x.end(),y.begin(),position);
    };
    return std::visit([&](const auto& x) {
        using T=std::decay_t<decltype(x)>;const auto* y=std::get_if<T>(&b);if(!y)return false;
        if constexpr(requires{x.vertices;})return positions(x.vertices,y->vertices);
        else if constexpr(requires{x.radius;})return position(x.center,y->center)&&scalar(x.radius,y->radius);
        else if constexpr(requires{x.major_radius;})return position(x.center,y->center)&&
            position(x.major_axis_direction,y->major_axis_direction)&&scalar(x.major_radius,y->major_radius)&&scalar(x.minor_radius,y->minor_radius);
        else {
            if(x.curves.size()!=y->curves.size())return false;
            for(std::size_t i=0;i<x.curves.size();++i) {
                const bool same=std::visit([&](const auto& c) {
                    using C=std::decay_t<decltype(c)>;const auto* d=std::get_if<C>(&y->curves[i]);if(!d)return false;
                    if(!position(c.start,d->start)||!position(c.end,d->end))return false;
                    if constexpr(requires{c.middle;})if(!position(c.middle,d->middle))return false;
                    if constexpr(requires{c.major_radius;}) {
                        if(!position(c.center,d->center)||!position(c.major_axis_direction,d->major_axis_direction)||
                            !scalar(c.major_radius,d->major_radius)||!scalar(c.minor_radius,d->minor_radius)||
                            !scalar(c.start_parameter,d->start_parameter)||!scalar(c.end_parameter,d->end_parameter)||c.reversed!=d->reversed)return false;
                    }
                    if constexpr(requires{c.control_points;}) {
                        if(!positions(c.control_points,d->control_points)||c.degree!=d->degree||c.interpolating!=d->interpolating||
                            c.periodic!=d->periodic||c.knots!=d->knots||c.weights!=d->weights)return false;
                    }
                    return true;
                },x.curves[i]);
                if(!same)return false;
            }
            return true;
        }
    },a);
}

// Piecewise rigid development. The existing ordinary Sweep uses explicit
// station frames; accept only a complete section that agrees with the rigid
// path frame at every station. Equal area alone is not proof of a constant
// section. Keep the Sweep topology roles and authored station/profile IDs.
struct StraightPortion {
    gp_Trsf from_formed;
    gp_Pnt origin;
    gp_Vec tangent;
    double length{}, developed_start{};
    ExtrusionRequest region;
};
SolidStraighteningPlan ordinary_sweep(const Sweep3DRequest& source,double coefficient,
        std::vector<StraightPortion>* straight_portions=nullptr) {
    if(!std::isfinite(coefficient)||coefficient<=0)
        throw std::invalid_argument("Straightening coefficient must be positive and finite.");
    validate_sweep3d(source);
    if(!source.make_solid||source.smooth_loft||source.fixed_section_frames||source.sections.front().point_index!=0)
        throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    for(std::size_t i=1;i<source.sections.size();++i)if(source.sections[i].point_index<=source.sections[i-1].point_index)
        throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    const auto unsupported=[](){throw std::invalid_argument("Straightening requires connected, tangent-continuous Sweep segments.");};
    const auto first_station=[&](std::size_t i){return source.separate_segments?2*i:i;};
    std::vector<TopoDS_Edge> edges;std::vector<gp_Vec> starts,ends;
    std::vector<Handle(GeomFill_CorrectedFrenet)> laws;
    std::vector<Handle(Geom_Curve)> curves;
    for(const auto& segment:source.path_segments) {
        const auto curve=sweep_spine_curve(segment);
        auto edge=curve.IsNull()?BRepBuilderAPI_MakeEdge(point(segment.start),point(segment.end)).Edge():BRepBuilderAPI_MakeEdge(curve).Edge();
        BRepAdaptor_Curve adaptor(edge);
        Handle(GeomFill_CorrectedFrenet) law;
        if(adaptor.GetType()!=GeomAbs_Line&&adaptor.GetType()!=GeomAbs_Circle) {
            law=new GeomFill_CorrectedFrenet;
            law->SetCurve(new GeomAdaptor_Curve(curve));
        }
        laws.push_back(law);curves.push_back(curve);
        gp_Pnt a,b;gp_Vec da,db;adaptor.D1(adaptor.FirstParameter(),a,da);adaptor.D1(adaptor.LastParameter(),b,db);
        const auto first=first_station(edges.size());
        if(a.Distance(point(source.path_points[first]))>1e-7||b.Distance(point(source.path_points[first+1]))>1e-7)unsupported();
        if(!edges.empty()&&a.Distance(point(source.path_points[first_station(edges.size()-1)+1]))>1e-7)unsupported();
        da.Normalize();db.Normalize();
        if(!ends.empty()&&ends.back().Dot(da)<1-1e-9)unsupported();
        starts.push_back(da);ends.push_back(db);edges.push_back(edge);
    }
    const auto origin=point(source.path_points.front());const auto tangent=starts.front();
    // Match the corrected-Frenet transport used by ordinary MakePipeShell.
    // These frames calculate geometry only; sampling never creates identities.
    const auto spline_frame=[&](std::size_t i,double parameter) {
        gp_Vec t,n,b;
        if(!laws[i]->D0(parameter,t,n,b))
            throw std::runtime_error("Straightening section evaluation is incomplete.");
        return gp_Ax3(curves[i]->Value(parameter),gp_Dir(t),gp_Dir(n));
    };
    std::vector<gp_Trsf> frames(source.path_points.size());gp_Trsf rotation;
    for(std::size_t i=0;i<edges.size();++i) {
        const auto first=first_station(i);
        auto start_frame=rotation;start_frame.SetTranslationPart(gp_Vec(origin.Transformed(start_frame),point(source.path_points[first])));
        frames[first]=start_frame;
        BRepAdaptor_Curve curve(edges[i]);
        if(curve.GetType()==GeomAbs_Circle) {
            gp_Trsf turn;turn.SetRotation(gp_Ax1(gp_Pnt{},curve.Circle().Axis().Direction()),curve.LastParameter()-curve.FirstParameter());
            rotation=turn*rotation;
        }
        else if(!laws[i].IsNull()) {
            gp_Trsf turn;turn.SetDisplacement(spline_frame(i,curve.FirstParameter()),spline_frame(i,curve.LastParameter()));
            turn.SetTranslationPart(gp_Vec{});rotation=turn*rotation;
        }
        auto frame=rotation;
        frame.SetTranslationPart(gp_Vec(origin.Transformed(frame),point(source.path_points[first+1])));
        frames[first+1]=frame;
    }
    const auto inherited_frames=inherited_spline_frames(source,edges);
    const auto normalize=[&](const Sweep3DRequest::Section& section,std::size_t station) {
        if(inherited_frames) {
            const auto n=section.profile_normal;
            gp_Trsf initial;initial.SetRotation(gp_Quaternion(gp_Vec(n.x,n.y,n.z),starts.front()));
            initial.SetTranslationPart(gp_Vec(origin.Transformed(initial),origin));
            return frames[station].Inverted()*inherited_frames->at(station)*initial;
        }
        const auto n=section.profile_normal;
        const auto direction=source.separate_segments?(station%2?ends[station/2]:starts[station/2]):
            (station<starts.size()?starts[station]:ends.back());
        gp_Trsf movement;movement.SetRotation(gp_Quaternion(gp_Vec(n.x,n.y,n.z),direction));
        movement.SetTranslationPart(gp_Vec(point(source.path_points[section.point_index]).Transformed(movement),point(source.path_points[station])));
        return frames[station].Inverted()*movement;
    };
    const auto transformed=[](auto section,const gp_Trsf& transform) {
        transform_loop(section.profile.outer_profile,transform);
        for(auto& hole:section.profile.inner_profiles)transform_loop(hole,transform);
        const auto n=section.profile_normal;section.profile_normal=vector(gp_Vec(n.x,n.y,n.z).Transformed(transform).XYZ());
        if(section.circle_radial_direction) {const auto d=*section.circle_radial_direction;
            section.circle_radial_direction=vector(gp_Vec(d.x,d.y,d.z).Transformed(transform).XYZ());}
        return section;
    };
    const auto initial=transformed(source.sections.front(),normalize(source.sections.front(),0));
    for(std::size_t station=0;station<frames.size();++station) {
        const auto* section=&source.sections.front();
        for(const auto& next:source.sections){if(next.point_index>station)break;section=&next;}
        const auto current=transformed(*section,normalize(*section,station));
        bool same=same_loop(initial.profile.outer_profile,current.profile.outer_profile)&&initial.profile.inner_profiles.size()==current.profile.inner_profiles.size();
        if(same)for(std::size_t i=0;i<initial.profile.inner_profiles.size();++i)
            same=same&&same_loop(initial.profile.inner_profiles[i],current.profile.inner_profiles[i]);
        if(!same)throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    }
    RevolutionRequest profile;profile.outer_profile=initial.profile.outer_profile;profile.inner_profiles=initial.profile.inner_profiles;
    profile.outer_edge_source_ids=initial.profile.outer_edge_source_ids;profile.outer_vertex_source_ids=initial.profile.outer_vertex_source_ids;
    profile.inner_edge_source_ids=initial.profile.inner_edge_source_ids;profile.inner_vertex_source_ids=initial.profile.inner_vertex_source_ids;
    if(source.thin)profile.wall=ProfileWall{source.thin_first,source.thin_second,initial.thin_end_point_id};
    std::vector<TopoDS_Wire> wires;for(const auto& item:make_body_profiles(profile,initial.profile_normal,initial.circle_radial_direction))wires.push_back(item.wire);
    GProp_GProps properties;BRepGProp::SurfaceProperties(profile_base(wires,false),properties);
    if(!std::isfinite(properties.Mass())||properties.Mass()<=0)throw std::invalid_argument("Straightening requires a valid cross-section area.");
    const auto center=properties.CentreOfMass();std::vector<double> lengths(frames.size());double total=0;
    for(std::size_t i=0;i<edges.size();++i) {
        const auto first=first_station(i);lengths[first]=total;
        BRepAdaptor_Curve curve(edges[i]);double length;
        if(curve.GetType()==GeomAbs_Line) {
            length=point(source.path_points[first]).Distance(point(source.path_points[first+1]));
            if(straight_portions) {
                ExtrusionRequest region;copy_profile(profile,region);
                region.profile_region_id=initial.profile.region_id;
                region.outer_boundary_id=initial.profile.outer_boundary_id;
                region.inner_boundary_ids=initial.profile.inner_boundary_ids;
                region.direction=vector((tangent*length).XYZ());
                straight_portions->push_back({frames[first].Inverted(),origin,tangent,length,total,std::move(region)});
            }
        }
        else if(curve.GetType()==GeomAbs_Circle) {
            const auto axis=curve.Circle().Axis();const auto radial=gp_Vec(axis.Location(),center.Transformed(frames[first]));
            length=gp_Vec(axis.Direction()).Crossed(radial).Magnitude()*(curve.LastParameter()-curve.FirstParameter());
        }
        else {
            const auto start_frame=spline_frame(i,curve.FirstParameter());
            const auto start_center=center.Transformed(frames[first]);
            double previous=-1;unsigned stable=0;
            const double tolerance=source.linear_tolerance*.125/edges.size();
            for(std::size_t intervals=16;;intervals*=2) {
                if(intervals>65536)throw std::runtime_error("Straightening section evaluation is incomplete.");
                auto prior=start_center;length=0;
                for(std::size_t sample=1;sample<=intervals;++sample) {
                    const double parameter=curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*sample/intervals;
                    gp_Trsf movement;movement.SetDisplacement(start_frame,spline_frame(i,parameter));
                    const auto current=start_center.Transformed(movement);
                    length+=prior.Distance(current);prior=current;
                }
                if(previous>=0&&std::abs(length-previous)<=tolerance)++stable;else stable=0;
                if(stable>=2)break;
                previous=length;
            }
        }
        if(!std::isfinite(length*coefficient)||length<=Precision::Confusion())
            throw std::invalid_argument("Straightening requires a nonzero centroid trajectory.");
        total+=length;lengths[first+1]=total;
    }
    if(!std::isfinite(lengths.back()*coefficient))
        throw std::invalid_argument("Straightening requires a nonzero centroid trajectory.");
    auto target=source;
    for(std::size_t i=0;i<target.path_points.size();++i)target.path_points[i]=vector(origin.Translated(tangent*(lengths[i]*coefficient)).XYZ());
    for(std::size_t i=0;i<target.path_segments.size();++i) {
        const auto first=first_station(i);
        auto& segment=target.path_segments[i];segment.start=target.path_points[first];segment.end=target.path_points[first+1];
        segment.arc_midpoint.reset();segment.bezier_control_points.clear();segment.bezier_spans.clear();
    }
    for(auto& section:target.sections) {
        gp_Trsf shift;shift.SetTranslation(tangent*(lengths[section.point_index]*coefficient));
        section=transformed(section,shift*normalize(section,section.point_index));
    }
    SolidStraighteningPlan result;result.primitive=std::move(target);result.section_area=properties.Mass();
    result.centroid_path_length=lengths.back();result.coefficient=coefficient;
    result.source_start_centroid=vector(center.XYZ());result.source_end_centroid=vector(center.Transformed(frames.back()).XYZ());
    result.straight_end_centroid=vector(center.Translated(tangent*(lengths.back()*coefficient)).XYZ());
    result.end_transform=frame_values(frames.back());result.start_tangent=vector(tangent.XYZ());return result;
}

} // namespace solid_straightening
