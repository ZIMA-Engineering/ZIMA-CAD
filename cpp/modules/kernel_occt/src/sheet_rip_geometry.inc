// Explicit transient preprocessing for Sheet-from-Body. The result is a
// material-equivalent partition of the source solid; no material is removed.
namespace sheet_rip_geometry {
PrimitiveData apply(const PrimitiveData& input,const SheetRipRequest& request,
        const std::string& owner) {
    if(input.shape.IsNull()||request.curves.empty())
        throw std::invalid_argument("Sheet RIP requires source material and at least one curve.");
    if(!std::isfinite(request.tolerance)||request.tolerance<=0||
            request.tolerance>1)
        throw std::invalid_argument("Invalid Sheet RIP tolerance.");
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(input.shape,bounds,false,true);
    if(bounds.IsVoid())throw std::runtime_error(
        "Sheet RIP source bounds are unavailable.");
    double x0,y0,z0,x1,y1,z1;
    bounds.Get(x0,y0,z0,x1,y1,z1);
    const double diagonal=std::hypot(std::hypot(x1-x0,y1-y0),z1-z0);
    const double extent=std::max(10.,diagonal*2+10.);
    TopTools_ListOfShape arguments;
    arguments.Append(input.shape);
    TopTools_ListOfShape tools;
    std::vector<OwnedFace> tool_faces;
    std::size_t tool_index{};
    for(const auto& curve:request.curves) {
        const double magnitude=std::hypot(std::hypot(curve.normal.x,
            curve.normal.y),curve.normal.z);
        if(!std::isfinite(magnitude)||magnitude<=1e-12)
            throw std::invalid_argument("Sheet RIP Sketch has an invalid plane normal.");
        const Vec3 normal{curve.normal.x/magnitude,curve.normal.y/magnitude,
            curve.normal.z/magnitude};
        auto wire=make_profile_wire(curve.profile,normal);
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(-normal.x*extent,-normal.y*extent,
            -normal.z*extent));
        BRepBuilderAPI_Transform moved(wire,shift,true);
        moved.Build();
        if(!moved.IsDone())throw std::runtime_error(
            "Sheet RIP could not position a Sketch curve.");
        BRepPrimAPI_MakePrism prism(moved.Shape(),gp_Vec(
            normal.x*extent*2,normal.y*extent*2,normal.z*extent*2),false,true);
        prism.Build();
        if(!prism.IsDone()||prism.Shape().IsNull())
            throw std::runtime_error("Sheet RIP could not sweep a Sketch curve.");
        BRepAlgoAPI_Section section(input.shape,prism.Shape(),false);
        section.SetFuzzyValue(request.tolerance);
        section.Build();
        bool intersects{};
        if(section.IsDone())for(TopExp_Explorer edge(section.Shape(),TopAbs_EDGE);
                edge.More();edge.Next()) {intersects=true;break;}
        if(!intersects)throw std::invalid_argument(
            "A selected RIP Sketch does not intersect the source Body.");
        tools.Append(prism.Shape());
        std::size_t face_index{};
        for(TopExp_Explorer face(prism.Shape(),TopAbs_FACE);face.More();
                face.Next())
            tool_faces.push_back({TopoDS::Face(face.Current()),FaceReference{
                owner,"rip:tool:"+curve.sketch_id+":"+curve.source_id+":"+
                    std::to_string(tool_index)+":"+
                    std::to_string(face_index++),{}}});
        ++tool_index;
    }
    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(arguments);
    splitter.SetTools(tools);
    splitter.SetFuzzyValue(request.tolerance);
    splitter.Build();
    if(!splitter.IsDone()||splitter.Shape().IsNull()||
            !BRepCheck_Analyzer(splitter.Shape()).IsValid())
        throw std::runtime_error("Sheet RIP partition failed.");
    auto faces=propagate_topology(splitter,input.faces,
        std::vector<OwnedFace>{});
    auto introduced=propagate_topology(splitter,std::vector<OwnedFace>{},
        tool_faces);
    faces.insert(faces.end(),std::make_move_iterator(introduced.begin()),
        std::make_move_iterator(introduced.end()));
    auto edges=propagate_topology(splitter,input.edges,
        std::vector<OwnedEdge>{});
    auto vertices=propagate_topology(splitter,input.vertices,
        std::vector<OwnedVertex>{});
    faces=complete_boolean_faces(splitter.Shape(),faces,edges,vertices,
        owner,"sheet-rip",{});
    edges=complete_boolean_edges(splitter.Shape(),faces,edges,
        owner,"sheet-rip");
    vertices=complete_boolean_vertices(splitter.Shape(),faces,edges,
        vertices,owner,"sheet-rip");
    GProp_GProps before,after;
    BRepGProp::VolumeProperties(input.shape,before);
    BRepGProp::VolumeProperties(splitter.Shape(),after);
    const double scale=std::max(1.,std::abs(before.Mass()));
    if(std::abs(before.Mass()-after.Mass())>
            std::max(1e-7,request.tolerance*request.tolerance)*scale)
        throw std::runtime_error("Sheet RIP changed source material volume.");
    return {splitter.Shape(),std::move(faces),std::move(edges),
        std::move(vertices)};
}
} // namespace sheet_rip_geometry
