// Explicit feasibility experiment, never a production FORM implementation.
#include <zima/document/surface_thicken.hpp>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>

namespace {
TopoDS_Shape experiment_shape(const kernel::BodyResult& result) {
    return kernel::read_calculated_body_shape(result);
}

double experiment_volume(const TopoDS_Shape&);
unsigned experiment_solids(const TopoDS_Shape&);
void experiment_packet(const TopoDS_Shape&,const std::filesystem::path&);
document::PartDocument experiment_corner_stock(kernel::OcctKernel&,double,double);
int prepare_corner_solid_cut(const std::filesystem::path& input,const std::filesystem::path& output) {
    const auto definition=document::read_sheet_form_definition(input);
    check(definition.solid_opening_faces.size()==2,"Expected original closed corner definition");
    auto part=definition.part;part.form_outward=true;auto graph=part.body_history;
    auto shape=*graph.find(definition.bodies[1]);auto cut=*graph.find(definition.bodies[0]);
    check(cut.entries.empty(),"Original closed corner cutter must be empty");
    cut.entries=shape.entries;cut.cursor=cut.entries.size();cut.scope.placement=shape.scope.placement;
    shape.entries.clear();shape.cursor=0;
    graph.update_body(shape);graph.update_body(cut);graph.move_body(cut.scope.id,0);graph.move_body(shape.scope.id,1);
    graph.activate(shape.scope.id);part.set_body_history(graph);
    const auto old_origin=document::create_container_origin(shape.scope.id).id;
    const auto new_origin=document::create_container_origin(cut.scope.id).id;
    const auto reference=[&](auto& ref) {
        if(ref.owner_id==old_origin)ref.owner_id=new_origin;
        else if(ref.owner_id==shape.scope.id)ref.owner_id=cut.scope.id;
    };
    for(auto& feature:part.history) {
        if(part.body_history.owner(feature.id)->scope.id==cut.scope.id) {
            feature.feature.centroid_centerline=true;feature.feature.origin_centerline=false;
            feature.extrusion.centroid_centerline=true;feature.extrusion.origin_centerline=false;
        }
        for(auto& ref:feature.placement.references)reference(ref);
        for(auto& side:feature.feature.sides)for(auto& target:side.targets)reference(target.reference);
    }
    for(auto& sketch:part.sketches)if(sketch.plane_reference_owner_id==old_origin)sketch.plane_reference_owner_id=new_origin;
    for(auto& construction:part.constructions)for(auto& ref:construction.references)reference(ref);
    auto extraction=document::PartDocument::create_shell_container(definition.solid_opening_faces);
    extraction.name="FORM outside surface";extraction.shell.thickness=0.;extraction.shell.source_body=cut.scope.id;
    part.insert_history_entry(document::PartHistoryKind::Feature,extraction.id);part.history.push_back(extraction);
    part.name="Corner outward surface with solid cutter";
    kernel::OcctKernel engine;auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
    check(calculated.back().calculation_errors.empty(),"Linked corner definition failed");
    check(calculated.back().body_outputs.at(cut.scope.id)->volume>0.&&calculated.back().body_outputs.at(shape.scope.id)->volume==0.,"Source solid was consumed by extraction");
    part.save(output,calculated);
    const auto reopened=document::read_sheet_form_definition(output);
    check(reopened.solid_cut_supports.size()==2&&reopened.solid_opening_faces.empty()&&reopened.cut_sketches.empty(),"Native solid cutter and linked surface were not recognized");
    const auto request=document::sheet_form_request(reopened,{}, {},{0,1,0},{1,0,0},1.);
    check(request.surface_snapshot&&request.solid_cut_snapshot,"Independent definition lost calculated inputs");
    std::cout<<"Native FORM_CUT solid precedes linked FORM surface; source and surface roundtrip passed\n";
    return 0;
}
int compare_outward_user_cutters(const std::filesystem::path& path) {
    using namespace kernel::sheet_material;
    std::vector<kernel::BodyResult> cache;auto part=document::PartDocument::load(path,&cache);
    const auto feature=std::ranges::find_if(part.history,[](const auto& value){return value.feature_kind==document::FeatureKind::SheetForm;});
    check(feature!=part.history.end(),"Missing user FORM");
    const auto source=document::stored_sheet_form_definition(feature->sheet_form);
    const auto operation=document::sheet_form_operation(part,*feature);
    const auto& request=std::get<kernel::SheetFormRequest>(operation.primitive);
    check(request.outward&&source.corner(),"Expected outward corner candidate");
    const auto history=part.kernel_operations();
    const auto entry=std::ranges::find(history,feature->id,&kernel::HistoryOperation::owner_id);
    check(entry!=history.end()&&entry!=history.begin(),"Missing FORM input boundary");
    const auto boundary=static_cast<std::size_t>(entry-history.begin());
    check(cache.size()>boundary&&cache[boundary-1].source_fingerprint==kernel::history_fingerprint(history,boundary),"User input cache is not current");
    const auto regions=regions_before(history,boundary);
    const auto bend=std::ranges::find_if(regions.regions,[&](const auto& region){
        return region.kind==kernel::SheetMaterialDefinition::Kind::Cylinder&&!region.unfolded&&
            std::abs(dot(sub(request.position,region.origin),region.radial))<1e-7&&
            std::abs(dot(sub(request.position,region.origin),region.tangent)-region.radius)<1e-7;
    });check(bend!=regions.regions.end(),"Missing actual Bend frame");
    std::vector<kernel::HistoryOperation> closed_history,cut_history,surface_history;
    for(auto local:*request.definition) {
        if(local.body.id==request.shape_body) {
            local.body={};surface_history.push_back(local);
            if(!std::holds_alternative<kernel::ShellRequest>(local.primitive))closed_history.push_back(local);
        }else if(local.body.id==request.cut_body) {
            const auto adjust=[&](auto& value) {
                if(auto* extrusion=std::get_if<kernel::ExtrusionRequest>(&value))extrusion->direction=mul(unit(extrusion->direction),bend->radius);
            };
            adjust(local.primitive);
            if(auto* group=std::get_if<kernel::FeatureGroupRequest>(&local.primitive))for(auto& child:group->children)adjust(child);
            local.body={};cut_history.push_back(local);
        }
    }
    check(surface_history.size()==closed_history.size()+1&&std::holds_alternative<kernel::ShellRequest>(surface_history.back().primitive),"Expected one native surface extraction");
    kernel::OcctKernel engine;
    const auto closed=engine.evaluate_history(closed_history);std::cout<<"Closed source volume="<<closed.back().volume<<" BRep bytes="<<closed.back().kernel_shape.size()<<std::endl;valid(closed.back());
    const auto surface=engine.evaluate_history_incremental(surface_history,closed);
    check(surface.back().calculation_errors.empty()&&surface.back().volume==0.,"Invalid source skin");
    surface_history.push_back({"diagnostic-outward-wall",kernel::SurfaceThickenRequest{request.surface,request.thickness,kernel::SurfaceThicknessSide::First,.001}});
    const auto wall=engine.evaluate_history_incremental(surface_history,surface);std::cout<<"Wall volume="<<wall.back().volume<<" BRep bytes="<<wall.back().kernel_shape.size()<<std::endl;valid(wall.back());
    const auto cuts=engine.evaluate_history(cut_history);std::cout<<"Cut volume="<<cuts.back().volume<<" BRep bytes="<<cuts.back().kernel_shape.size()<<std::endl;valid(cuts.back());
    const auto transform=[](kernel::Vec3 origin,kernel::Vec3 normal,kernel::Vec3 x) {
        return gp_Ax3(gp_Pnt(origin.x,origin.y,origin.z),gp_Dir(normal.x,normal.y,normal.z),gp_Dir(x.x,x.y,x.z));
    };
    gp_Trsf placement;placement.SetDisplacement(transform(request.source_origin,request.source_normal,request.source_x),transform(request.position,request.normal,request.x_direction));
    const auto placed=[&](const TopoDS_Shape& shape,const std::string& body) {
        const auto& scope=source.part.body_history.find(body)->scope;
        const auto frame=kernel::annotation_frame(scope.translation(),scope.rotation_degrees());
        const auto origin=frame.world({});
        gp_Trsf local;local.SetDisplacement(gp_Ax3(),transform(origin,sub(frame.world({0,0,1}),origin),sub(frame.world({1,0,0}),origin)));
        auto combined=placement;combined.Multiply(local);
        return BRepBuilderAPI_Transform(shape,combined,false).Shape();
    };
    auto input_part=part;std::vector<std::string> omitted;
    for(auto next=feature;next!=part.history.end();++next)omitted.push_back(next->id);
    for(auto next=omitted.rbegin();next!=omitted.rend();++next)input_part.erase_history_object(*next);
    const auto input_results=workspace::calculate_part_with_resolved_references(engine,input_part);
    valid(input_results.back());const auto stock=experiment_shape(input_results.back());
    const auto solid_tool=placed(experiment_shape(closed.back()),request.shape_body);
    const auto sketch_tools=placed(experiment_shape(cuts.back()),request.cut_body);
    const auto formed=placed(experiment_shape(wall.back()),request.shape_body);
    const std::filesystem::path directory="build/form-diagnostic/user-01-cutter-comparison";
    std::filesystem::create_directories(directory);
    const auto difference=[](const TopoDS_Shape& a,const TopoDS_Shape& b) {
        BRepAlgoAPI_Cut operation(a,b);operation.SetNonDestructive(true);operation.Build();
        check(operation.IsDone(),"Diagnostic difference failed");return operation.Shape();
    };
    const auto common=[](const TopoDS_Shape& a,const TopoDS_Shape& b) {
        BRepAlgoAPI_Common operation(a,b);operation.SetNonDestructive(true);operation.Build();
        check(operation.IsDone(),"Diagnostic intersection failed");return operation.Shape();
    };
    const auto excess=common(stock,difference(sketch_tools,solid_tool));
    const auto unfilled=difference(excess,formed);
    nlohmann::json report{{"stock_thickness",request.thickness},{"outer_bend_radius",bend->radius},
        {"extra_sketch_cut_volume",experiment_volume(excess)},{"extra_sketch_cut_not_filled_by_wall",experiment_volume(unfilled)}};
    for(const bool solid:{false,true}) {
        const auto start=std::chrono::steady_clock::now();
        const auto removed=difference(stock,solid?solid_tool:sketch_tools);
        BRepAlgoAPI_Fuse joined(removed,formed);joined.SetNonDestructive(true);joined.Build();
        check(joined.IsDone()&&BRepCheck_Analyzer(joined.Shape(),true,false,true).IsValid(),"Diagnostic Cut/Fuse is invalid");
        check(experiment_solids(joined.Shape())==1,"Diagnostic FORM disconnected");
        const auto name=solid?"solid":"sketches";
        report[name]={{"cut_fuse_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()},
            {"volume",experiment_volume(joined.Shape())},{"valid",true},{"solid_count",1}};
        experiment_packet(joined.Shape(),directory/(std::string(name)+".json"));
        BRepTools::Write(joined.Shape(),(directory/(std::string(name)+".brep")).string().c_str());
    }
    experiment_packet(unfilled,directory/"extra-unfilled-cut.json");
    std::ofstream file(directory/"report.json");file<<report.dump(2);
    std::cout<<"Read-only cutter comparison "<<report.dump()<<std::endl;
    return 0;
}
int corner_outward_rounding_experiment() {
    const std::filesystem::path directory="build/form-diagnostic";
    TopoDS_Shape original;BRep_Builder builder;
    check(BRepTools::Read(original,(directory/"native-outward-unrounded.brep").string().c_str(),builder),"Missing native unrounded BRep");
    BRepLib::SameParameter(original,1e-7,true);
    BRepLib::EncodeRegularity(original,1e-10);
    {
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;TopExp::MapShapesAndAncestors(original,TopAbs_EDGE,TopAbs_FACE,adjacent);
        unsigned changed=0;
        for(int i=1;i<=adjacent.Extent();++i) {
            std::vector<TopoDS_Face> faces;
            for(TopTools_ListIteratorOfListOfShape it(adjacent.FindFromIndex(i));it.More();it.Next())
                if(std::ranges::none_of(faces,[&](const auto& f){return f.IsSame(it.Value());}))faces.push_back(TopoDS::Face(it.Value()));
            if(faces.size()!=2)continue;
            const auto edge=TopoDS::Edge(adjacent.FindKey(i));
            const auto actual=BRepLib::ContinuityOfFaces(edge,faces[0],faces[1],1e-10);
            if(actual>BRep_Tool::Continuity(edge,faces[0],faces[1])) {builder.Continuity(edge,faces[0],faces[1],actual);++changed;}
        }
        std::cout<<"Recomputed geometric regularity changed "<<changed<<" edges"<<std::endl;
    }
    std::array<std::vector<TopoDS_Edge>,2> routes;
    for(unsigned side:{0u,1u}) {
        TopoDS_Shape serialized;
        check(BRepTools::Read(serialized,(directory/("native-outward-route-"+std::to_string(side)+".brep")).string().c_str(),builder),"Missing native route BRep");
        for(TopExp_Explorer e(serialized,TopAbs_EDGE);e.More();e.Next()) {
            BRepAdaptor_Curve requested(TopoDS::Edge(e.Current()));
            const auto p=requested.Value(requested.FirstParameter()),q=requested.Value(requested.LastParameter());
            const auto m=requested.Value((requested.FirstParameter()+requested.LastParameter())*.5);
            std::vector<TopoDS_Edge> matches;
            for(TopExp_Explorer i(original,TopAbs_EDGE);i.More();i.Next()) {
                const auto edge=TopoDS::Edge(i.Current());BRepAdaptor_Curve curve(edge);
                const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
                if(((a.Distance(p)<1e-7&&b.Distance(q)<1e-7)||(a.Distance(q)<1e-7&&b.Distance(p)<1e-7))&&
                   curve.Value((curve.FirstParameter()+curve.LastParameter())*.5).Distance(m)<1e-7&&
                   std::ranges::none_of(matches,[&](const auto& other){return other.IsSame(edge);}))matches.push_back(edge);
            }
            check(matches.size()==1,"Diagnostic route is not geometrically unique");
            if(std::ranges::none_of(routes[side],[&](const auto& other){return other.IsSame(matches.front());}))routes[side].push_back(matches.front());
        }
    }
    {
        nlohmann::json geometry=nlohmann::json::array();
        TopTools_IndexedMapOfShape vertices;TopExp::MapShapes(original,TopAbs_VERTEX,vertices);
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;TopExp::MapShapesAndAncestors(original,TopAbs_EDGE,TopAbs_FACE,adjacent);
        for(unsigned side:{0u,1u})for(const auto& edge:routes[side]) {
            BRepAdaptor_Curve curve(edge);nlohmann::json points=nlohmann::json::array(),faces=nlohmann::json::array();
            for(const double f:{0.,.25,.5,.75,1.}) {
                const auto p=curve.Value(curve.FirstParameter()+f*(curve.LastParameter()-curve.FirstParameter()));points.push_back(std::array{p.X(),p.Y(),p.Z()});
            }
            for(TopTools_ListIteratorOfListOfShape it(adjacent.FindFromKey(edge));it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Value());BRepAdaptor_Surface surface(face);
                double first,last;const auto uv=BRep_Tool::CurveOnSurface(edge,face,first,last);
                const auto p=uv->Value((first+last)*.5);gp_Pnt q;gp_Vec du,dv;surface.D1(p.X(),p.Y(),q,du,dv);
                auto n=du.Crossed(dv);n.Normalize();if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
                faces.push_back({{"type",int(surface.GetType())},{"normal",std::array{n.X(),n.Y(),n.Z()}}});
            }
            gp_Pnt p;gp_Vec a,b;curve.D1(curve.FirstParameter(),p,a);curve.D1(curve.LastParameter(),p,b);a.Normalize();b.Normalize();
            geometry.push_back({{"side",side},{"type",int(curve.GetType())},{"points",points},{"faces",faces},
                {"start_vertex",vertices.FindIndex(TopExp::FirstVertex(edge))},{"end_vertex",vertices.FindIndex(TopExp::LastVertex(edge))},
                {"start_tangent",std::array{a.X(),a.Y(),a.Z()}},{"end_tangent",std::array{b.X(),b.Y(),b.Z()}}});
        }
        std::ofstream file(directory/"native-route-geometry.json");file<<geometry.dump(2);
    }
    nlohmann::json report=nlohmann::json::array();
    for(const auto type:{ChFi3d_Rational,ChFi3d_QuasiAngular})
    for(const double angular:{.01})for(const double sag:{.001})
    for(const bool swapped:{false})for(const double radius:{1.})for(const unsigned order:{0u,1u,2u}) {
        nlohmann::json row{{"parameterization",int(type)},{"angular",angular},{"sag",sag},{"swapped",swapped},{"cavity_radius",radius},{"opposite_radius",.25},{"order",order}};
        auto shape=original;
        try {
            for(unsigned stage=0;stage<(order==2?1u:2u);++stage) {
                BRepFilletAPI_MakeFillet fillet(shape);fillet.SetFilletShape(type);
                fillet.SetParams(angular,1e-7,1e-5,1e-7,1e-5,sag);
                for(unsigned side:{1u,0u}) {
                    if(order<2&&side!=(stage==0?order:1-order))continue;
                    for(const auto& edge:routes[side])if(fillet.Contour(edge)==0)fillet.Add((side==1)!=swapped?radius:.25,edge);
                }
                fillet.Build();
                row["done"]=bool(fillet.IsDone());row["strict_valid"]=fillet.IsDone()&&!fillet.Shape().IsNull()&&BRepCheck_Analyzer(fillet.Shape(),true,false,true).IsValid();row["default_valid"]=fillet.IsDone()&&!fillet.Shape().IsNull()&&BRepCheck_Analyzer(fillet.Shape()).IsValid();
                row["stage"]=stage;row["contours"]=fillet.NbContours();row["faulty_contours"]=fillet.NbFaultyContours();row["faulty_vertices"]=fillet.NbFaultyVertices();
                check(fillet.IsDone()&&!fillet.Shape().IsNull()&&BRepCheck_Analyzer(fillet.Shape(),true,false,true).IsValid(),"Diagnostic rounding failed");
                shape=fillet.Shape();
            }
            row["valid"]=true;row["solid_count"]=experiment_solids(shape);row["volume"]=experiment_volume(shape);
            if(radius==1.)BRepTools::Write(shape,(directory/("diagnostic-rounding-type-"+std::to_string(int(type))+"-order-"+std::to_string(order)+".brep")).string().c_str());
        }catch(const Standard_Failure& e){row["error"]=e.GetMessageString();}
        catch(const std::exception& e){row["error"]=e.what();}
        report.push_back(row);std::cout<<"Native route diagnostic "<<row.dump()<<std::endl;
        std::ofstream file(directory/"native-rounding-parameterization.json");file<<report.dump(2);
    }
    return 0;
}

double experiment_volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    const auto error=BRepGProp::VolumePropertiesGK(shape,properties,1e-12,false,true);
    check(std::isfinite(error)&&error>=0.,"Independent volume integration failed");
    return properties.Mass();
}
unsigned experiment_solids(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
    return static_cast<unsigned>(solids.Extent());
}
void experiment_packet(const TopoDS_Shape& shape,const std::filesystem::path& output) {
    // Display-only diagnostics deliberately have no selectable persistent IDs.
    kernel::BodyResult result;result.volume=experiment_volume(shape);
    BRepMesh_IncrementalMesh mesher(shape,.04,false,.15,false);
    for(TopExp_Explorer i(shape,TopAbs_FACE);i.More();i.Next()) {
        const auto face=TopoDS::Face(i.Current());TopLoc_Location location;
        const auto triangles=BRep_Tool::Triangulation(face,location);if(triangles.IsNull())continue;
        const auto base=static_cast<std::uint32_t>(result.mesh.vertices.size());
        for(int node=1;node<=triangles->NbNodes();++node) {
            const auto p=triangles->Node(node).Transformed(location.Transformation());
            result.mesh.vertices.push_back({p.X(),p.Y(),p.Z()});
        }
        for(int triangle=1;triangle<=triangles->NbTriangles();++triangle) {
            int a,b,c;triangles->Triangle(triangle).Get(a,b,c);
            if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
            result.mesh.triangles.insert(result.mesh.triangles.end(),{base+a-1,base+b-1,base+c-1});
            result.mesh.triangle_references.push_back({});
        }
    }
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    for(int i=1;i<=edges.Extent();++i) {
        BRepAdaptor_Curve curve(TopoDS::Edge(edges(i)));kernel::ViewerEdge edge;
        const int samples=curve.GetType()==GeomAbs_Line?1:80;
        for(int j=0;j<=samples;++j) {
            const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*j/samples);
            edge.points.push_back({p.X(),p.Y(),p.Z()});
        }
        result.mesh.edges.push_back(std::move(edge));
    }
    std::ofstream file(output);file<<document::serialize_body_result(result).dump();
    check(bool(file),"Cannot save experimental viewer packet");
}
nlohmann::json experiment_section(const TopoDS_Shape& shape) {
    BRepAlgoAPI_Section section(shape,gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),false);
    section.Build();check(section.IsDone(),"Independent centre section failed");
    nlohmann::json curves=nlohmann::json::array();
    for(TopExp_Explorer i(section.Shape(),TopAbs_EDGE);i.More();i.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(i.Current()));nlohmann::json points=nlohmann::json::array();
        const int samples=curve.GetType()==GeomAbs_Line?1:160;
        for(int j=0;j<=samples;++j) {
            const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*j/samples);
            points.push_back(std::array{p.Y(),p.Z()});
        }
        curves.push_back(std::move(points));
    }
    return curves;
}
sketcher::Sketch experiment_contact_profile(const sketcher::Sketch& original,bool xz) {
    auto sketch=sketcher::Sketch::create_default();
    sketch.plane_auto=false;sketch.plane=xz?sketcher::SketchPlane::XZ:sketcher::SketchPlane::XY;
    sketch.name=xz?"FORM_CUT XZ contact":"FORM_CUT XY contact";
    // This source is a 45-degree native extrusion to XY and XZ. Projection
    // along its authored extrusion direction maps (u,v) to (u,sqrt(2)*v).
    // A circular source arc therefore becomes an exact ellipse, not a polyline.
    const auto xy=[&](const std::string& id) {
        const auto* point=original.find_point(id);check(point,"Contact source point missing");
        // Native XZ has +Y normal and -Z vertical axis; its local ordinate
        // must be reflected to describe the same negative-Z contact.
        return std::array{point->x,(xz?-1.:1.)*std::sqrt(2.)*point->y};
    };
    for(const auto& segment:original.segments)if(!segment.construction&&!segment.centerline) {
        const auto a=xy(segment.first_point_id),b=xy(segment.second_point_id);
        static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1]));
    }
    for(const auto& arc:original.arcs)if(!arc.construction&&!arc.centerline) {
        const auto center=xy(arc.center_point_id),a=xy(arc.start_point_id),b=xy(arc.end_point_id);
        static_cast<void>(sketch.add_elliptical_arc(center[0],center[1],
            center[0],center[1]+(xz?-1.:1.)*std::sqrt(2.)*arc.radius,
            center[0]-arc.radius,center[1],a[0],a[1],b[0],b[1],xz));
    }
    sketch.validate();return sketch;
}
document::PartDocument experiment_corner_stock(kernel::OcctKernel& engine,double t,double ri) {
    auto part=document::PartDocument::create_default();
    static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Corner sheet"));
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto line=sketcher::Sketch::create_default();line.owner_container_id=bend.id;
    static_cast<void>(line.add_segment(-30,0,30,0));
    bend.bend.sketch_id=line.id;bend.bend.angle_degrees=90.;
    bend.bend.radius_follows_thickness=false;bend.bend.radius=ri;
    bend.bend.thickness_override=true;bend.bend.thickness=t;
    part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);
    part.history.push_back(bend);part.sketches.push_back(line);
    const auto initial=workspace::calculate_part_with_resolved_references(engine,part);valid(initial.back());
    for(bool second:{false,true}) {
        auto plate=document::PartDocument::create_sketch_container();plate.feature_kind=document::FeatureKind::Flat;
        plate.flat.thickness_override=true;plate.flat.thickness=t;
        plate.flat.sheet_attachment=true;plate.flat.direction=document::ExtrusionDirection::Reverse;
        for(const auto& edge:initial.back().mesh.original_references.edges)try {
            auto references=document::flat_sheet_references(edge);
            if(references[1].semantic_key.starts_with(second?"sweep:cap:end:from:":"sweep:cap:start:from:")) {
                plate.placement.references=std::move(references);break;
            }
        }catch(const std::exception&){}
        check(!plate.placement.references.empty(),"Native Bend attachment missing");
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=plate.id;
        plate.flat.sketch_id=sketch.id;
        part.insert_history_entry(document::PartHistoryKind::Feature,plate.id);
        part.history.push_back(plate);part.sketches.push_back(sketch);
        part.resolve_constructions(initial.back().mesh.original_references);
        const auto& ends=part.sketches.back().external_references;
        check(ends.size()==2,"Native Flat endpoints missing");
        const auto a=ends[0].cached_points.front(),b=ends[1].cached_points.front();
        static_cast<void>(part.sketches.back().add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),30));
    }
    return part;
}
}

int corner_outward_form_experiment(const std::filesystem::path& path) {
    using namespace kernel::sheet_material;
    const auto definition=document::read_sheet_form_definition(path);
    check(definition.solid_opening_faces.size()==2,"Expected current compact corner source");
    kernel::OcctKernel engine;std::vector<kernel::HistoryOperation> closed_operations;
    for(auto op:definition.part.kernel_operations())if(op.body.id==definition.bodies[1]) {
        op.body={};closed_operations.push_back(std::move(op));
    }
    const auto closed=engine.evaluate_history(closed_operations);valid(closed.back());
    auto surface_operations=closed_operations;
    surface_operations.push_back({"experiment-surface",kernel::ShellRequest{definition.solid_opening_faces,0.}});
    const auto surface=engine.evaluate_history_incremental(surface_operations,closed);
    check(surface.back().calculation_errors.empty()&&surface.back().volume==0.,"Surface conversion failed");
    const auto anchor=surface.back().mesh.triangle_references.front();
    const auto source_sketch=std::ranges::find_if(definition.part.sketches,[&](const auto& sketch) {
        const auto* body=definition.part.body_history.owner(sketch.owner_container_id);
        return body&&body->scope.id==definition.bodies[1];
    });check(source_sketch!=definition.part.sketches.end(),"Source profile missing");
    const std::filesystem::path directory="build/form-diagnostic/outward-corner";
    std::filesystem::create_directories(directory);
    // An independently editable authoring copy: actual native role Sketches
    // and zero-thickness Shell, with the existing source identities preserved.
    auto authored=definition.part;authored.form_outward=true;auto graph=authored.body_history;
    graph.activate(definition.bodies[1]);authored.set_body_history(graph);
    auto conversion=document::PartDocument::create_shell_container(definition.solid_opening_faces);
    conversion.shell.thickness=0.;conversion.name="FORM outside surface";
    authored.insert_history_entry(document::PartHistoryKind::Feature,conversion.id);authored.history.push_back(conversion);
    graph=authored.body_history;
    auto cutting_body=*graph.find(definition.bodies[0]);
    cutting_body.scope.placement=graph.find(definition.bodies[1])->scope.placement;
    graph.update_body(std::move(cutting_body));
    graph.activate(definition.bodies[0]);authored.set_body_history(graph);
    std::array<sketcher::Sketch,2> contacts;
    for(unsigned i=0;i<2;++i) {
        contacts[i]=experiment_contact_profile(*source_sketch,i==1);
        auto feature=document::PartDocument::create_sketch_container();feature.name=contacts[i].name;
        contacts[i].owner_container_id=feature.id;
        authored.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
        authored.history.push_back(feature);authored.sketches.push_back(contacts[i]);
    }
    authored.name="Corner outward surface experiment";
    for(auto& feature:authored.history)if(authored.body_history.owner(feature.id)->scope.id==definition.bodies[1])
        feature.feature.centroid_centerline=true;
    const auto native=workspace::calculate_part_with_resolved_references(engine,authored);
    check(!native.empty()&&native.back().calculation_errors.empty(),"Native authoring calculation failed");
    const auto output=std::getenv("ZIMA_CPP_OUTWARD_SOURCE_OUTPUT");
    const auto asset=std::filesystem::path(output?output:"Projects/test/CornerGusset90-outward-surface-experiment.prtz");
    authored.save(asset,native);
    std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(asset,&saved);
    check(reopened.sketches.size()==authored.sketches.size()&&!saved.empty(),"Source roundtrip failed");
    for(const auto& feature:authored.history) {
        const auto found=std::ranges::find(reopened.history,feature.id,&document::HistoryContainer::id);
        check(found!=reopened.history.end()&&*found==feature,"Source history changed on reopening");
    }
    const auto revised=document::read_sheet_form_definition(asset);
    check(revised.cut_sketches.size()==2&&revised.solid_opening_faces.empty(),"Native surface roles were not recognized");
    std::cout<<"Native outward authoring source roundtrip passed"<<std::endl;
    if(std::getenv("ZIMA_CPP_OUTWARD_PREPARE_ONLY"))return 0;
    nlohmann::json report=nlohmann::json::array();
    for(const auto [t,ri]:std::array<std::pair<double,double>,9>{{{.5,.5},{1,1},{2,2},{3,3},{4,4},{.5,2},{1,2},{2,4},{3,6}}}) {
        kernel::OcctKernel stock_engine;auto part=experiment_corner_stock(stock_engine,t,ri);
        const auto baseline=workspace::calculate_part_with_resolved_references(stock_engine,part);valid(baseline.back());
        std::cout<<"Stock t="<<t<<" Ri="<<ri<<" valid"<<std::endl;
        const auto stock=experiment_shape(baseline.back());
        const auto frame=document::bend_material_definition(part.history.front(),part.sketches.front(),document::sheet_metal_defaults(part));
        const auto position=add(frame.origin,mul(frame.tangent,frame.radius));
        const auto support=std::ranges::find_if(baseline.back().mesh.triangle_references,[&](const auto& ref) {
            return ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                dot(mul(ref.surface->axis,ref.surface->reversed?-1.:1.),frame.radial)>1.-1e-10&&
                std::abs(dot(sub(ref.surface->origin,position),frame.radial))<1e-8;
        });check(support!=baseline.back().mesh.triangle_references.end(),"Outer support missing");
        // Derive the frame from both outer normals, rather than assuming a
        // Bend parameter frame's cross-product handedness.
        const auto along=unit(cross(frame.tangent,frame.radial));
        near(dot(cross(frame.radial,along),frame.tangent),1.,1e-10);
        const auto request=document::sheet_form_request(definition,*support,position,frame.radial,along,t);
        gp_Trsf placement;placement.SetDisplacement(
            gp_Ax3(gp_Pnt(0,0,0),gp_Dir(request.source_normal.x,request.source_normal.y,request.source_normal.z),gp_Dir(request.source_x.x,request.source_x.y,request.source_x.z)),
            gp_Ax3(gp_Pnt(position.x,position.y,position.z),gp_Dir(frame.radial.x,frame.radial.y,frame.radial.z),gp_Dir(along.x,along.y,along.z)));
        // The library FORM Body is rotated 180 degrees about X. Ordinary FORM
        // applies this authored Body placement before insertion; the independent
        // experiment must do the same for the surface and both contact tools.
        const auto& scope=definition.part.body_history.find(definition.bodies[1])->scope;
        const auto source_frame=kernel::annotation_frame(scope.translation(),scope.rotation_degrees());
        const auto origin=source_frame.world({0,0,0});
        const auto source_z=sub(source_frame.world({0,0,1}),origin),source_x=sub(source_frame.world({1,0,0}),origin);
        gp_Trsf body_pose;body_pose.SetDisplacement(gp_Ax3(),
            gp_Ax3(gp_Pnt(origin.x,origin.y,origin.z),gp_Dir(source_z.x,source_z.y,source_z.z),gp_Dir(source_x.x,source_x.y,source_x.z)));
        placement.Multiply(body_pose);
        // Two ordinary native Extrusions, each only as deep as the Bend's
        // outer radius, from exact contact Sketches on the outer sheet planes.
        auto cutter=document::PartDocument::create_default();
        for(unsigned i=0;i<2;++i) {
            auto sketch=contacts[i];auto cut=document::PartDocument::create_extrusion_container(sketch.id);
            sketch.owner_container_id=cut.id;cut.extrusion.length_forward=frame.radius;
            cut.extrusion.direction=document::ExtrusionDirection::Reverse;
            cutter.history.push_back(cut);cutter.sketches.push_back(sketch);
        }
        cutter.resolve_constructions();const auto cuts=engine.evaluate_history(cutter.kernel_operations());
        check(!cuts.empty()&&cuts.back().calculation_errors.empty(),"Contact cut-tool calculation failed");
        check(BRepCheck_Analyzer(experiment_shape(cuts.back()),true,false,true).IsValid(),"Contact cut-tool BRep invalid");
        std::cout<<"Contact cut-tool solids="<<experiment_solids(experiment_shape(cuts.back()))<<std::endl;
        unsigned verified_profiles=0;
        for(TopExp_Explorer i(experiment_shape(closed.back()),TopAbs_FACE);i.More();i.Next()) {
            const auto f=TopoDS::Face(i.Current());BRepAdaptor_Surface s(f);
            if(s.GetType()!=GeomAbs_Plane)continue;
            const auto p=s.Plane();const auto n=p.Axis().Direction();
            if((std::abs(n.Y())<1.-1e-8&&std::abs(n.Z())<1.-1e-8)||p.Distance(gp_Pnt(0,0,0))>1e-8)continue;
            const bool xy=std::abs(n.Z())>1.-1e-8;
            const auto prism=BRepPrimAPI_MakePrism(f,xy?gp_Vec(0,0,-frame.radius):gp_Vec(0,-frame.radius,0)).Shape();
            const auto op=cutter.kernel_operations().at(xy?0:1);
            const auto profile_result=engine.evaluate_history({op});valid(profile_result.back());
            const auto authored_tool=experiment_shape(profile_result.back());
            BRepAlgoAPI_Cut a(prism,authored_tool),b(authored_tool,prism);a.Build();b.Build();
            check(a.IsDone()&&b.IsDone(),"Exact contact-profile comparison failed");
            std::cout<<"Contact comparison XY="<<xy<<" missing="<<experiment_volume(a.Shape())<<" extra="<<experiment_volume(b.Shape())<<std::endl;
            near(experiment_volume(a.Shape()),0.);near(experiment_volume(b.Shape()),0.);++verified_profiles;
        }
        check(verified_profiles==2,"Both exact source contacts must be checked");
        if(t==1.&&ri==2.) {
            experiment_packet(experiment_shape(cuts.back()),directory/"local-cut-tools.json");
            experiment_packet(experiment_shape(closed.back()),directory/"local-closed-source.json");
        }
        const auto cut_shape=BRepBuilderAPI_Transform(experiment_shape(cuts.back()),placement,false).Shape();
        const auto cut_start=std::chrono::steady_clock::now();
        BRepAlgoAPI_Cut removed(stock,cut_shape);removed.SetNonDestructive(true);removed.Build();
        check(removed.IsDone()&&BRepCheck_Analyzer(removed.Shape(),true,false,true).IsValid(),"Two-profile Cut invalid");
        const auto cut_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-cut_start).count();
        for(const auto side:{kernel::SurfaceThicknessSide::First,kernel::SurfaceThicknessSide::Second,kernel::SurfaceThicknessSide::Symmetric}) {
        if(side!=kernel::SurfaceThicknessSide::First&&(t!=1.||ri!=2.))continue;
        for(double wall_factor:{.5,1.}) {
            const std::string side_name=side==kernel::SurfaceThicknessSide::First?"outward":
                side==kernel::SurfaceThicknessSide::Second?"inward-control":"symmetric-control";
            nlohmann::json row{{"stock_thickness",t},{"inner_radius",ri},{"side",side_name},{"cut_depth",frame.radius},{"wall_factor",wall_factor},{"cut_seconds",cut_seconds}};
            try {
                auto operations=surface_operations;
                operations.push_back({"experiment-outward-thicken",kernel::SurfaceThickenRequest{anchor,t*wall_factor,side,.001}});
                const auto begin=std::chrono::steady_clock::now();
                const auto thick=engine.evaluate_history_incremental(operations,surface);valid(thick.back());
                row["thicken_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
                const auto formed=BRepBuilderAPI_Transform(experiment_shape(thick.back()),placement,false).Shape();
                BRepAlgoAPI_Common contact(removed.Shape(),formed);contact.SetNonDestructive(true);contact.Build();
                check(contact.IsDone(),"Contact intersection failed");row["overlap_volume"]=experiment_volume(contact.Shape());
                GProp_GProps contact_area;BRepGProp::SurfaceProperties(contact.Shape(),contact_area);
                row["contact_area"]=contact_area.Mass();
                BRepAlgoAPI_Common original_overlap(experiment_shape(closed.back()),experiment_shape(thick.back()));
                original_overlap.Build();check(original_overlap.IsDone(),"Offset direction verification failed");
                row["inside_original_tool_volume"]=experiment_volume(original_overlap.Shape());
                // Sample both outlines through the complete stock thickness.
                // Connectivity alone does not prove full-thickness contact.
                nlohmann::json gaps=nlohmann::json::array();
                for(TopExp_Explorer f(experiment_shape(closed.back()),TopAbs_FACE);f.More();f.Next()) {
                    const auto face=TopoDS::Face(f.Current());BRepAdaptor_Surface s(face);
                    if(s.GetType()!=GeomAbs_Plane)continue;const auto plane=s.Plane();const auto n=plane.Axis().Direction();
                    if(plane.Distance(gp_Pnt(0,0,0))>1e-8||
                        (std::abs(n.Y())<1.-1e-8&&std::abs(n.Z())<1.-1e-8))continue;
                    const bool xy=std::abs(n.Z())>1.-1e-8;
                    for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
                        BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
                        for(unsigned boundary_sample=0;boundary_sample<=10;++boundary_sample) {
                        const auto midpoint=curve.Value(curve.FirstParameter()+
                            (curve.LastParameter()-curve.FirstParameter())*boundary_sample/10.);
                        if((xy?-midpoint.Y():-midpoint.Z())<=frame.radius+1e-6)continue;
                        for(const double fraction:{.1,.5,.9,1.}) {
                        auto p=midpoint;p.Translate(xy?gp_Vec(0,0,-t*fraction):gp_Vec(0,-t*fraction,0));
                        p.Transform(placement);
                        const auto vertex=BRepBuilderAPI_MakeVertex(p).Shape();
                        BRepExtrema_DistShapeShape to_sheet(vertex,removed.Shape()),to_wall(vertex,formed);
                        to_sheet.Perform();to_wall.Perform();
                        check(to_sheet.IsDone()&&to_wall.IsDone(),"Cut-wall sample distance failed");
                        near(to_sheet.Value(),0.,1e-7);
                        gaps.push_back({{"plane",xy?"XY":"XZ"},{"point",std::array{p.X(),p.Y(),p.Z()}},
                            {"distance_to_cut_stock",to_sheet.Value()},{"on_cut_stock",to_sheet.Value()<1e-7},
                            {"depth_fraction",fraction},{"boundary_fraction",boundary_sample/10.},
                            {"gap_to_formed_wall",to_wall.Value()}});
                        }
                        }
                    }
                }
                row["cut_wall_samples"]=gaps;
                BRepExtrema_DistShapeShape distance(removed.Shape(),formed);distance.Perform();
                check(distance.IsDone(),"Contact distance failed");row["minimum_distance"]=distance.Value();
                const auto fuse_start=std::chrono::steady_clock::now();
                BRepAlgoAPI_Fuse joined(removed.Shape(),formed);joined.SetNonDestructive(true);joined.Build();
                check(joined.IsDone(),"Unrounded Fuse failed");
                row["fuse_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-fuse_start).count();
                row["valid"]=BRepCheck_Analyzer(joined.Shape(),true,false,true).IsValid();
                row["solid_count"]=experiment_solids(joined.Shape());row["volume"]=experiment_volume(joined.Shape());
                const auto suffix="t-"+std::to_string(t)+"-ri-"+std::to_string(ri)+"-wall-"+std::to_string(wall_factor)+"-"+side_name;
                BRepTools::Write(joined.Shape(),(directory/(suffix+".brep")).string().c_str());
                if(t==1.&&ri==2.) {
                    experiment_packet(joined.Shape(),directory/(suffix+".json"));
                    experiment_packet(removed.Shape(),directory/"cut-stock.json");
                    experiment_packet(stock,directory/"stock.json");
                    experiment_packet(formed,directory/(suffix+"-wall.json"));
                    part.save(directory/"stock.prtz",baseline);
                    row["placement"]=std::array{position.x,position.y,position.z};
                    std::ofstream sections(directory/(suffix+"-section.json"));
                    sections<<nlohmann::json{{"cut_stock",experiment_section(removed.Shape())},
                        {"formed_wall",experiment_section(formed)},{"fused",experiment_section(joined.Shape())}}.dump(2);
                }
            }catch(const std::exception& error){row["error"]=error.what();}
            report.push_back(row);std::cout<<"Outward corner "<<row.dump()<<std::endl;
            std::ofstream file(directory/"report.json");file<<report.dump(2);
        }
        }
    }
    unsigned outward_count=0;
    for(const auto& row:report) {
        check(!row.contains("error"),"An outward experiment or control failed calculation");
        if(row.at("side")!="outward")continue;
        ++outward_count;
        check(row.at("valid").get<bool>()&&row.at("solid_count").get<unsigned>()==1&&
            row.at("overlap_volume").get<double>()>0.,"Outward wall must have positive material connection");
        near(row.at("inside_original_tool_volume").get<double>(),0.,1e-6);
        std::set<std::string> planes;
        for(const auto& sample:row.at("cut_wall_samples")) {
            check(sample.at("on_cut_stock").get<bool>(),"Gap sample missed the actual stock wall");
            planes.insert(sample.at("plane").get<std::string>());
            if(row.at("wall_factor").get<double>()==1.)near(sample.at("gap_to_formed_wall").get<double>(),0.,1e-7);
        }
        check(planes.size()==2,"Both sheet contacts must be sampled");
    }
    check(outward_count==18,"Outward experiment matrix incomplete");
    std::cout<<"Outward experiment accepted 18 connected BReps; full stock-thickness walls passed all contact-depth samples"<<std::endl;
    return 0;
}

int native_outward_form_probe(const std::filesystem::path& path,double thickness=1.,double inner_radius=2.) {
    auto source=document::read_sheet_form_definition(path);
    if(!std::getenv("ZIMA_CPP_FORM_BASELINE"))source.part.form_outward=true;
    kernel::OcctKernel engine;
    document::PartDocument stock;
    kernel::Vec3 position,normal,along;
    if(source.corner()) {
        stock=experiment_corner_stock(engine,thickness,inner_radius);
        const auto frame=document::bend_material_definition(stock.history.front(),stock.sketches.front(),document::sheet_metal_defaults(stock));
        position=kernel::sheet_material::add(frame.origin,kernel::sheet_material::mul(frame.tangent,frame.radius));
        normal=frame.radial;along=kernel::sheet_material::unit(kernel::sheet_material::cross(frame.tangent,frame.radial));
    }else {
        stock=document::PartDocument::create_default();
        static_cast<void>(document::create_origin_bound_body(stock.body_history,stock.document_id,"Outward test sheet"));
        auto plate=document::PartDocument::create_sketch_container();plate.feature_kind=document::FeatureKind::Flat;
        plate.flat.thickness_override=true;plate.flat.thickness=thickness;plate.flat.direction=document::ExtrusionDirection::Reverse;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=plate.id;
        static_cast<void>(sketch.add_rectangle(-100,-100,100,100));plate.flat.sketch_id=sketch.id;
        stock.insert_history_entry(document::PartHistoryKind::Feature,plate.id);stock.history.push_back(plate);stock.sketches.push_back(sketch);
        normal={0,0,1};along={1,0,0};
        if(std::getenv("ZIMA_CPP_FORM_OPPOSITE_SUPPORT")){normal={0,0,-1};position={0,0,-thickness};}
    }
    auto baseline=workspace::calculate_part_with_resolved_references(engine,stock);valid(baseline.back());
    const auto support=std::ranges::find_if(baseline.back().mesh.triangle_references,[&](const auto& face) {
        if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)return false;
        const auto n=kernel::sheet_material::mul(face.surface->axis,face.surface->reversed?-1.:1.);
        return kernel::sheet_material::dot(n,normal)>1.-1e-10&&
            std::abs(kernel::sheet_material::dot(kernel::sheet_material::sub(face.surface->origin,position),normal))<1e-7;
    });check(support!=baseline.back().mesh.triangle_references.end(),"Outward support missing");
    auto feature=document::create_sheet_form();
    if(!source.corner()) {
        feature.sheet_form=document::copy_sheet_form_definition(source);
        feature.sheet_form.support=*support;feature.sheet_form.thickness=thickness;
        const auto* body=stock.body_owner_for_object(support->owner_id);check(body,"Native stock ownership missing");
        auto geometry=stock.construction_reference_geometry_for(stock.history.back().id,baseline.back().mesh.original_references);
        document::Placement seed;
        if(const auto value=std::getenv("ZIMA_CPP_FORM_ANGLE"))seed.absolute_rotation_y=std::stod(value);
        feature.placement=document::sheet_form_attachment(*support,body->origin().id,geometry,seed,position);
        check(document::resolve_sheet_form_feature_placement(feature.sheet_form,feature.placement,geometry),"Outward native placement unresolved");
        const kernel::Vec3 rotation{feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z};
        along=document::construction_direction_from_local_axis("x",rotation);
        near(kernel::sheet_material::dot(normal,document::construction_direction_from_local_axis("y",rotation)),1.,1e-10);
    }
    auto request=document::sheet_form_request(source,*support,position,normal,along,thickness);
    std::cout<<"FORM source normal=("<<request.source_normal.x<<','<<request.source_normal.y<<','<<request.source_normal.z
        <<") source origin=("<<request.source_origin.x<<','<<request.source_origin.y<<','<<request.source_origin.z<<")"<<std::endl;
    auto operations=stock.kernel_operations();kernel::HistoryOperation form{kernel::make_stable_id(),request};form.body=operations.front().body;
    kernel::SheetMaterialDefinition material;material.kind=kernel::SheetMaterialDefinition::Kind::Form;
    material.owner_id=form.owner_id;material.parent_owner_id=support->sheet_owner;material.thickness=thickness;material.origin=position;
    material.along=along;material.radial=normal;material.tangent=kernel::sheet_material::cross(normal,along);form.sheet_material=material;
    operations.push_back(form);
    const auto start=std::chrono::steady_clock::now();
    const auto results=engine.evaluate_history_incremental(operations,baseline);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    valid(results.back());if(source.corner()&&(!source.part.form_outward||source.part.form_corner_finish))verify_corner_transition_radii(results.back(),form.owner_id,thickness);
    const auto verify_window_side=[&](const kernel::BodyResult& result) {
        if(source.corner()||!source.part.form_outward)return;
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(TopExp_Explorer vertex(experiment_shape(result),TopAbs_VERTEX);vertex.More();vertex.Next()) {
            const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
            const auto distance=kernel::sheet_material::dot(kernel::sheet_material::sub({p.X(),p.Y(),p.Z()},position),normal);
            low=std::min(low,distance);high=std::max(high,distance);
        }
        check(low>=-thickness-1e-7&&high>2.*thickness,"Window must grow outside its selected face without protruding through the opposite stock face");
        const auto preview=document::sheet_form_preview_edges(feature.sheet_form);
        check(std::ranges::any_of(preview,[](const auto& edge){return std::ranges::any_of(edge.points,[](const auto& p){return p.y>0.;});}),"Window preview has no outward geometry");
        std::cout<<"Window signed extent from selected outer face=["<<low<<','<<high<<"]"<<std::endl;
    };
    verify_window_side(results.back());
    if(!source.solid_cut_supports.empty()) {
        // Independently execute the user's ordinary sequence, including the
        // complete Bend domain: native solid Cut, outward thickness and Fuse.
        auto authored=*request.definition;
        const auto last=std::ranges::find_if(authored.rbegin(),authored.rend(),[&](const auto& value){return value.body.id==request.shape_body;});
        check(last!=authored.rend(),"Linked surface history missing");
        kernel::HistoryOperation wall{"independent-form-wall",kernel::SurfaceThickenRequest{request.surface,thickness,kernel::SurfaceThicknessSide::First,.001}};
        wall.body=last->body;authored.insert(last.base(),wall);
        const auto ordinary=engine.evaluate_history(authored);check(ordinary.back().calculation_errors.empty(),"Independent outward thickness failed");
        gp_Trsf placement;placement.SetDisplacement(
            gp_Ax3(gp_Pnt(request.source_origin.x,request.source_origin.y,request.source_origin.z),gp_Dir(request.source_normal.x,request.source_normal.y,request.source_normal.z),gp_Dir(request.source_x.x,request.source_x.y,request.source_x.z)),
            gp_Ax3(gp_Pnt(position.x,position.y,position.z),gp_Dir(normal.x,normal.y,normal.z),gp_Dir(along.x,along.y,along.z)));
        const auto cutter=BRepBuilderAPI_Transform(experiment_shape(ordinary.back().body_outputs.at(request.cut_body).get()),placement,false).Shape();
        auto formed=BRepBuilderAPI_Transform(experiment_shape(ordinary.back().body_outputs.at(request.shape_body).get()),placement,false).Shape();
        TopoDS_Compound walls;BRep_Builder builder;builder.MakeCompound(walls);
        for(TopExp_Explorer solid(formed,TopAbs_SOLID);solid.More();solid.Next())builder.Add(walls,solid.Current());
        const auto bend=document::bend_material_definition(stock.history.front(),stock.sketches.front(),document::sheet_metal_defaults(stock));
        using namespace kernel::sheet_material;
        const auto center=sub(bend.origin,mul(bend.radial,bend.radius));
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(TopExp_Explorer vertex(walls,TopAbs_VERTEX);vertex.More();vertex.Next()) {
            const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
            const auto x=dot(sub({p.X(),p.Y(),p.Z()},bend.origin),bend.along);
            low=std::min(low,x);high=std::max(high,x);
        }
        const auto point=[&](double r,double t,double x){const auto p=add(center,add(mul(bend.radial,r),add(mul(bend.tangent,t),mul(bend.along,x))));return gp_Pnt(p.x,p.y,p.z);};
        BRepBuilderAPI_MakePolygon polygon;
        for(const auto p:{point(0,0,low),point(bend.radius,0,low),point(bend.radius,bend.radius,low),point(0,bend.radius,low)})polygon.Add(p);
        polygon.Close();const auto quarter=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(bend.along.x,bend.along.y,bend.along.z)*(high-low)).Shape();
        const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(point(0,0,low),gp_Dir(bend.along.x,bend.along.y,bend.along.z)),bend.radius,high-low).Shape();
        BRepAlgoAPI_Cut exterior(quarter,cylinder);check(exterior.IsDone(),"Independent Bend trim tool failed");
        BRepAlgoAPI_Cut trimmed(walls,exterior.Shape());check(trimmed.IsDone(),"Independent outer Bend radius trim failed");
        BRepAlgoAPI_Cut cut(experiment_shape(baseline.back()),cutter);cut.SetNonDestructive(true);cut.Build();check(cut.IsDone(),"Independent solid cut failed");
        BRepAlgoAPI_Fuse joined(cut.Shape(),trimmed.Shape());joined.SetNonDestructive(true);joined.Build();check(joined.IsDone(),"Independent wall Fuse failed");
        check(BRepCheck_Analyzer(joined.Shape(),true,false,true).IsValid()&&experiment_solids(joined.Shape())==1,"Independent FORM sequence is invalid/disconnected");
        auto unrounded_operations=operations;
        std::get<kernel::SheetFormRequest>(unrounded_operations.back().primitive).finish_corner=false;
        const auto unrounded=engine.evaluate_history_incremental(unrounded_operations,baseline);
        valid(unrounded.back());const auto actual=experiment_shape(unrounded.back());
        for(const bool reverse:{false,true}) {
            BRepAlgoAPI_Cut difference(reverse?actual:joined.Shape(),reverse?joined.Shape():actual);
            difference.SetNonDestructive(true);difference.Build();check(difference.IsDone(),"Complete Bend equivalence check failed");
            check(std::abs(experiment_volume(difference.Shape()))<1e-7,"FORM differs from ordinary Cut/thickness/Fuse in complete Bend domain");
        }
        std::cout<<"Independent solid Cut/outward thickness/Bend trim/Fuse equals unrounded FORM\n";
    }
    const auto result_path=std::filesystem::path("build/form-diagnostic")/(source.corner()?"native-outward-corner-result.json":"native-outward-window-result.json");
    std::ofstream file(result_path);file<<document::serialize_body_result(results.back()).dump();
    std::cout<<"Native outward FORM complete calculation seconds="<<seconds<<" volume="<<results.back().volume<<std::endl;
    if(!source.corner()&&std::getenv("ZIMA_CPP_FORM_PERSIST")) {
        const auto authored=source.part.serialized(*source.calculated);
        workspace::Workspace live;live.add_part(stock,baseline);
        check(workspace::commit_sheet_form(live,engine,stock.document_id,feature),"Outward FORM insertion did not commit");
        auto* state=live.open_part(stock.document_id);check(state,"Outward native session missing");
        valid(state->session.calculated_boundaries().back());near(state->session.calculated_boundaries().back().volume,results.back().volume);
        verify_window_side(state->session.calculated_boundaries().back());
        const auto inserted=*state->session.document().find_container(feature.id);
        const auto revision=state->session.revision();const auto fingerprint=state->session.calculated_boundaries().back().source_fingerprint;
        check(!workspace::commit_sheet_form(live,engine,stock.document_id,inserted)&&state->session.revision()==revision&&
            state->session.calculated_boundaries().back().source_fingerprint==fingerprint,"Unchanged outward FORM created a transaction or calculation");
        check(state->session.undo()&&!state->session.document().find_container(feature.id),"Outward insertion Undo failed");
        near(state->session.calculated_boundaries().back().volume,baseline.back().volume);
        check(state->session.redo()&&*state->session.document().find_container(feature.id)==inserted,"Outward insertion Redo lost definition/reference sides");
        auto moved=inserted;
        for(auto& reference:moved.placement.references)if(!reference.orientation_only&&reference.supports_offset&&reference.semantic_key.starts_with("origin:plane:")) {reference.offset+=2.;break;}
        check(workspace::commit_sheet_form(live,engine,stock.document_id,moved),"Outward FORM position edit did not commit");
        valid(state->session.calculated_boundaries().back());
        check(state->session.undo()&&*state->session.document().find_container(feature.id)==inserted,"Outward position Undo lost reference intent");
        auto unfold=document::PartDocument::create_sketch_container();unfold.feature_kind=document::FeatureKind::Unbend;unfold.sheet_state.all=true;
        check(workspace::commit_sheet_state(live,engine,stock.document_id,unfold),"Outward FORM Unbend did not commit");
        valid(state->session.calculated_boundaries().back());
        check(state->session.undo(),"Outward Unbend Undo failed");
        const auto path=std::filesystem::absolute("build/form-diagnostic/VentilationWindow-outward-placed.prtz");
        std::filesystem::create_directories(path.parent_path());
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened_results;auto reopened=document::PartDocument::load(path,&reopened_results);
        check(*reopened.find_container(feature.id)==inserted,"Outward native save/reopen changed references or embedded definition");
        valid(reopened_results.back());
        verify_window_side(reopened_results.back());
        const auto regenerated=workspace::calculate_part_with_resolved_references(engine,reopened,&reopened_results);
        valid(regenerated.back());near(regenerated.back().volume,results.back().volume);
        verify_window_side(regenerated.back());
        check(source.part.serialized(*source.calculated)==authored,"Outward placement modified source authoring document");
        std::cout<<"Outward FORM native placement, unchanged OK, position edit, Undo/Redo, Unbend and save/reopen passed"<<std::endl;
    }
    return 0;
}
