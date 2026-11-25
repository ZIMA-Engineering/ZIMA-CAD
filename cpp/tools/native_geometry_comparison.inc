// Independent offline audit of retained user BReps. Never edits either source.
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <zima/kernel/occt_surface_data.hpp>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

namespace {
// Packet table indices are storage locations, not persistent identities.
// An exact trim adds a table row when two trims share an authored face key;
// compare the referenced entities independently of that allocation.
nlohmann::json semantic_reference_contract(
        const zima::kernel::ViewerReferenceGeometry& source) {
    const auto geometry=canonical_reference_geometry(source);
    auto result=calculated_reference_contract(geometry);
    zima::kernel::ViewerReferenceGeometry faces;
    faces.triangle_references=geometry.triangle_references;
    result["faces"]=calculated_reference_contract(faces).at("references");
    result.erase("references");
    result.erase("edge_references_binary");
    result.erase("point_references_binary");
    const auto identity=[](const auto& reference) {
        nlohmann::json value={{"owner",reference.owner_id},
            {"key",reference.semantic_key},{"instance_path",reference.instance_path}};
        if constexpr(requires {reference.display_owner_id;})
            value["display_owner"]=reference.display_owner_id;
        return value;
    };
    result["edge_identities"]=nlohmann::json::array();
    for(const auto& edge:geometry.edges)result["edge_identities"].push_back(identity(edge.reference));
    result["point_identities"]=nlohmann::json::array();
    for(const auto& point:geometry.points)result["point_identities"].push_back(identity(point.reference));
    for(std::size_t i=0;i<geometry.axes.size();++i)
        result["axes"][i]["reference"]=identity(geometry.axes[i].reference);
    return result;
}
nlohmann::json compare_semantic_reference_contracts(
        const zima::kernel::ViewerReferenceGeometry& before,
        const zima::kernel::ViewerReferenceGeometry& after) {
    auto old_contract=semantic_reference_contract(before);
    auto new_contract=semantic_reference_contract(after);
    const auto old_faces=old_contract.at("faces"),new_faces=new_contract.at("faces");
    old_contract.erase("faces");new_contract.erase("faces");
    // Match every old and new face in both directions. Additional exact trim
    // data may distinguish rows previously sharing one identity, but cannot
    // change or remove an existing support, orientation, side or ownership.
    const auto compatible=[](const nlohmann::json& old_face,nlohmann::json new_face) {
        for(const auto* field:{"exact_surface","surface","contour_surface","continuation_group"})
            if(!old_face.contains(field))new_face.erase(field);
        return approximately_same_contract(old_face,new_face);
    };
    const auto identity=[](const nlohmann::json& face) {
        return std::tuple{face.at("owner").get<std::string>(),
            face.at("key").get<std::string>(),face.at("instance_path").get<std::string>()};
    };
    std::map<std::tuple<std::string,std::string,std::string>,
        std::vector<const nlohmann::json*>> old_by_identity,new_by_identity;
    for(const auto& face:old_faces)old_by_identity[identity(face)].push_back(&face);
    for(const auto& face:new_faces)new_by_identity[identity(face)].push_back(&face);
    auto missing_old=nlohmann::json::array(),unmatched_new=nlohmann::json::array();
    for(const auto& old_face:old_faces) {
        const auto found=new_by_identity.find(identity(old_face));
        if(found==new_by_identity.end()||std::ranges::none_of(found->second,
                [&](const auto* value){return compatible(old_face,*value);}))
            missing_old.push_back(old_face);
    }
    for(const auto& new_face:new_faces) {
        const auto found=old_by_identity.find(identity(new_face));
        if(found==old_by_identity.end()||std::ranges::none_of(found->second,
                [&](const auto* value){return compatible(*value,new_face);}))
            unmatched_new.push_back(new_face);
    }
    return {{"old_face_rows",old_faces.size()},{"new_face_rows",new_faces.size()},
        {"face_identity_sets_equal",old_by_identity.size()==new_by_identity.size()&&
            std::equal(old_by_identity.begin(),old_by_identity.end(),new_by_identity.begin(),
                [](const auto& a,const auto& b){return a.first==b.first;})},
        {"missing_old_faces",missing_old},{"unmatched_new_faces",unmatched_new},
        {"other_entities_equal",approximately_same_contract(old_contract,new_contract)},
        {"other_entity_differences",annotated_contract_diff(old_contract,new_contract)},
        {"equal_except_declared_added_surface_data",missing_old.empty()&&unmatched_new.empty()&&
            approximately_same_contract(old_contract,new_contract)}};
}
void verify_semantic_reference_comparison() {
    using namespace zima::kernel;
    ViewerReferenceGeometry old_geometry;
    FaceReference face{"source","face"};
    auto plane=std::make_shared<SurfaceGeometry>();plane->kind=SurfaceGeometry::Kind::Plane;
    plane->axis={0,0,1};face.surface=plane;
    old_geometry.triangle_references={face,face};
    ViewerEdge edge;edge.reference={"source","edge"};edge.points={{0,0,0},{1,0,0}};
    old_geometry.edges={edge};
    ViewerPoint point;point.reference={"source","vertex"};old_geometry.points={point};
    ViewerAxis axis;axis.reference={"source","face"};axis.direction={0,0,1};axis.display_length=10;
    old_geometry.axes={axis};
    auto upgraded=old_geometry;
    for(std::size_t i=0;i<upgraded.triangle_references.size();++i) {
        auto patch=std::make_shared<ExactSurfaceGeometry>();patch->support=std::make_shared<ExactSurface>();
        patch->u_min=static_cast<double>(i);patch->u_max=static_cast<double>(i+1);
        upgraded.triangle_references[i].exact_surface=patch;
    }
    const auto equal=[&](const auto& a,const auto& b) {
        return compare_semantic_reference_contracts(a,b).at("equal_except_declared_added_surface_data").template get<bool>();
    };
    if(!equal(old_geometry,upgraded))throw std::runtime_error("Semantic audit rejected declared trim metadata additions");
    auto changed=upgraded;changed.edges.front().reference.semantic_key="other-edge";
    if(equal(old_geometry,changed))throw std::runtime_error("Semantic audit missed changed edge identity");
    changed=upgraded;changed.points.front().reference.semantic_key="other-point";
    if(equal(old_geometry,changed))throw std::runtime_error("Semantic audit missed changed point identity");
    changed=upgraded;changed.axes.front().direction={0,1,0};
    if(equal(old_geometry,changed))throw std::runtime_error("Semantic audit missed changed axis geometry");
    changed=upgraded;auto reversed=std::make_shared<SurfaceGeometry>(*plane);reversed->reversed=true;
    changed.triangle_references.front().surface=reversed;
    if(equal(old_geometry,changed))throw std::runtime_error("Semantic audit missed changed face side");
    changed=upgraded;changed.triangle_references.clear();
    if(equal(old_geometry,changed))throw std::runtime_error("Semantic audit missed removed face");
    changed=upgraded;auto patch=std::make_shared<ExactSurfaceGeometry>(*changed.triangle_references.front().exact_surface);
    patch->u_min=-2;changed.triangle_references.front().exact_surface=patch;
    if(equal(upgraded,changed))throw std::runtime_error("Semantic audit missed changed existing exact trim");
}
void audit_native_physical_inputs(const std::filesystem::path& source,
        const std::filesystem::path& report,bool recalculate=false) {
    using namespace zima;
    std::vector<kernel::BodyResult> saved;
    auto part=document::PartDocument::load(source,&saved);
    if(recalculate) {
        kernel::OcctKernel engine;workspace::PartCalculationPolicy policy;
        policy.history_reference_baseline=&saved;
        auto calculated=workspace::calculate_part_with_resolved_references(engine,part,nullptr,policy);
        saved=std::move(calculated);
    }
    auto values=nlohmann::json::array();
    std::vector<std::pair<std::string,const kernel::BodyResult*>> inputs;
    const auto record=[&](const kernel::BodyResult& body,const std::string& label) {
        auto identical=nlohmann::json::array();
        for(const auto& [name,prior]:inputs)
            if(body.kernel_shape!=kernel::kernel_body_compound&&!body.kernel_shape.empty()&&
                    body.kernel_shape==prior->kernel_shape&&body.kernel_placements==prior->kernel_placements)
                identical.push_back(name);
        values.push_back({{"label",label},{"fingerprint",body.source_fingerprint},
            {"shape_bytes",body.kernel_shape.size()},
            {"placement_count",body.kernel_placements.size()},
            {"surface_mode",std::ranges::any_of(body.mesh.triangle_references,[](const auto& face){return face.surface_result;})||
                std::ranges::any_of(body.kernel_faces,[](const auto& face){return face.reference.surface_result;})},
            {"imported_step_mode",std::ranges::any_of(body.mesh.triangle_references,[](const auto& face){return face.semantic_key.starts_with("step:face:#");})},
            {"diagnostic_shape_hash",std::hash<std::string_view>{}(body.kernel_shape.get())},
            {"exact_geometry_and_placement_equal_to",identical},
            {"current",body.physical_properties_current}});
        inputs.emplace_back(label,&body);
    };
    const auto visit=[&](auto&& self,const kernel::BodyResult& body,const std::string& label)->void {
        record(body,label);
        for(const auto& [id,value]:body.body_outputs)self(self,value.get(),label+"/output/"+id);
        for(const auto& [id,history]:body.body_boundaries)
            for(std::size_t i=0;i<history.size();++i)record(history[i],label+"/history/"+id+"/"+std::to_string(i));
    };
    if(!saved.empty())visit(visit,saved.back(),"final");
    for(const auto& row:part.body_properties)
        for(const auto& input:document::body_properties_inputs(part,saved,row))record(*input.body,"measurement/"+row.id);
    std::ofstream output(document::native_file_io_path(report));output<<values.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot save native physical-input audit");
}
void compare_native_contracts(const std::filesystem::path& source,
        const std::filesystem::path& candidate,const std::filesystem::path& report) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous,current;
    const auto authored=document::PartDocument::load(source,&previous);
    const auto updated=document::PartDocument::load(candidate,&current);
    if(previous.empty()||current.empty())throw std::runtime_error("Native comparison needs both calculated bodies");
    const auto properties=[](const kernel::BodyResult& body) {
        return nlohmann::json{{"current",body.physical_properties_current},
            {"volume",body.volume},{"area",body.surface_area},
            {"calculation_errors",body.calculation_errors}};
    };
    auto evidence=nlohmann::json{
        {"current_history_contract_error",format_migration_calculated_contract_error(previous,current)},
        {"contract_error_allowing_declared_exact_data_additions",format_migration_calculated_contract_error(previous,current,true)},
        {"authored_history_equal",comparable_history(authored.history)==comparable_history(updated.history)},
        {"body_ownership_equal",authored.body_history==updated.body_history},
        {"original_properties",properties(previous.back())},
        {"candidate_properties",properties(current.back())}};
    evidence["final_reference_curves_equal"]=same_reference_curves(
        previous.back().mesh.original_references,current.back().mesh.original_references);
    evidence["semantic_reference_comparison"]=compare_semantic_reference_contracts(
        previous.back().mesh.original_references,current.back().mesh.original_references);
    auto details=nlohmann::json::array();std::map<std::string,std::size_t> fields;
    for(auto change:annotated_contract_diff(calculated_reference_contract(previous.back().mesh.original_references),
            calculated_reference_contract(current.back().mesh.original_references))) {
        if(change.contains("previous")&&change.contains("value")&&
                approximately_same_contract(change["previous"],change["value"]))continue;
        const auto path=change.at("path").get<std::string>();
        const auto field=path.substr(path.find_last_of('/')+1);++fields[field];
        if(field=="exact_surface"&&change.at("op")=="add")continue;
        if(details.size()<30) {
            for(const auto* name:{"previous","value"})if(change.contains(name)&&change[name].dump().size()>600)
                change[name]=change[name].dump().substr(0,600)+" [truncated diagnostic]";
            details.push_back(std::move(change));
        }
    }
    evidence["final_reference_changed_fields"]=fields;
    evidence["final_reference_change_examples"]=details;
    std::filesystem::create_directories(report.parent_path());
    std::ofstream output(document::native_file_io_path(report));output<<evidence.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot save native contract comparison");
}
void audit_native_prefix(const std::filesystem::path& source,std::size_t count,const std::filesystem::path& report) {
    using namespace zima;
    std::vector<kernel::BodyResult> saved;
    const auto authored=document::PartDocument::load(source,&saved);
    auto operations=authored.kernel_operations();
    if(count==0||count>operations.size()||count>saved.size())throw std::runtime_error("Invalid native prefix audit boundary");
    operations.resize(count);kernel::OcctKernel engine;
    std::cerr<<"Native prefix audit: loaded, calculating "<<count<<" operations\n";
    // This diagnostic isolates reference geometry. It must not integrate every
    // intermediate spline face solely to identify the first differing prefix.
    kernel::AggregatePropertyScope deferred(kernel::AggregatePropertyDemand::Deferred);
    const auto calculated=engine.evaluate_history(operations);
    std::cerr<<"Native prefix audit: calculated, comparing references\n";
    const auto& previous=saved[count-1];const auto& current=calculated.back();
    auto evidence=nlohmann::json{{"boundary",count},{"owner",operations.back().owner_id},
        {"original_references",calculated_reference_contract(previous.mesh.original_references)},
        {"original_final_references",calculated_reference_contract(saved.back().mesh.original_references)},
        {"current_references",calculated_reference_contract(current.mesh.original_references)},
        {"curves_equal",same_reference_curves(previous.mesh.original_references,current.mesh.original_references)},
        {"current_properties_current",current.physical_properties_current},
        {"original_properties_current",previous.physical_properties_current},
        {"original_volume",previous.volume},{"current_volume",current.volume},
        {"original_area",previous.surface_area},{"current_area",current.surface_area}};
    std::filesystem::create_directories(report.parent_path());
    std::ofstream output(document::native_file_io_path(report));output<<evidence.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot write native prefix comparison");
}
void audit_exact_surface_capture(const std::filesystem::path& source,const std::filesystem::path& report) {
    using namespace zima;
    std::vector<kernel::BodyResult> saved;
    static_cast<void>(document::PartDocument::load(source,&saved));
    if(saved.empty())throw std::runtime_error("Capture audit requires a calculated native body");
    const auto shape=kernel::read_calculated_body_shape(saved.back());
    std::size_t count{},samples{};auto failures=nlohmann::json::array();double maximum_point_error{},maximum_normal_error{};
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next(),++count) {
        const auto face=TopoDS::Face(it.Current());
        try {
            const auto exact=kernel::capture_exact_surface_geometry(face);
            BRepAdaptor_Surface native(face);
            for(double fu:{.13,.51,.89})for(double fv:{.17,.53,.87}) {
                const double u=std::lerp(exact->u_min,exact->u_max,fu),v=std::lerp(exact->v_min,exact->v_max,fv);
                gp_Pnt point;gp_Vec du,dv;native.D1(u,v,point,du,dv);
                const auto calculated=kernel::exact_surface_point(*exact,u,v);
                if(!calculated)throw std::runtime_error("Captured point cannot be evaluated");
                const double error=gp_Pnt(calculated->x,calculated->y,calculated->z).Distance(point);
                maximum_point_error=std::max(maximum_point_error,error);++samples;
                if(error>1e-7)throw std::runtime_error("Captured point differs from native support");
                auto normal=du.Crossed(dv);
                if(normal.SquareMagnitude()>1e-20) {
                    normal.Normalize();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
                    const auto evaluated=kernel::evaluate_exact_surface(*exact,u,v);
                    const double normal_error=(normal-gp_Vec(evaluated.normal.x,evaluated.normal.y,evaluated.normal.z)).Magnitude();
                    maximum_normal_error=std::max(maximum_normal_error,normal_error);
                    if(!evaluated.regular||normal_error>1e-7)throw std::runtime_error("Captured normal differs from native support");
                }
            }
        }
        catch(const Standard_Failure& error) {
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            auto row=nlohmann::json{{"ordinal_for_diagnostics_only",count},{"error",error.GetMessageString()},
                {"face_domain",{u0,u1,v0,v1}}};
            if(const auto spline=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(face))) {
                spline->Bounds(u0,u1,v0,v1);row["spline_domain"]={u0,u1,v0,v1};
                row["u_periodic"]=spline->IsUPeriodic();row["v_periodic"]=spline->IsVPeriodic();
                row["degrees"]={spline->UDegree(),spline->VDegree()};
            }
            failures.push_back(std::move(row));
        }
        catch(const std::exception& error) {failures.push_back({{"ordinal_for_diagnostics_only",count},{"error",error.what()}});}
    }
    std::ofstream output(document::native_file_io_path(report));
    output<<nlohmann::json{{"faces",count},{"samples",samples},{"maximum_point_error",maximum_point_error},
        {"maximum_normal_error",maximum_normal_error},{"failures",failures}}.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot save exact-surface audit");
    if(!failures.empty())throw std::runtime_error("Exact-surface capture audit found failures");
}
void compare_retained_native_geometry(const std::filesystem::path& source,
        const std::filesystem::path& candidate,const std::filesystem::path& report) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous,current;
    const auto authored=document::PartDocument::load(source,&previous);
    const auto updated=document::PartDocument::load(candidate,&current);
    if(previous.empty()||current.empty())throw std::runtime_error("Native comparison needs both calculated bodies");
    const auto first=kernel::read_calculated_body_shape(previous.back());
    const auto second=kernel::read_calculated_body_shape(current.back());
    const auto properties=[](const TopoDS_Shape& shape) {
        GProp_GProps volume,area;
        BRepGProp::VolumeProperties(shape,volume);BRepGProp::SurfaceProperties(shape,area);
        auto faces=nlohmann::json::array();
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());GProp_GProps measure;
            BRepGProp::SurfaceProperties(face,measure);const auto center=measure.CentreOfMass();
            Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            faces.push_back({{"area",measure.Mass()},{"center",{center.X(),center.Y(),center.Z()}},
                {"bounds",{x0,y0,z0,x1,y1,z1}},
                {"support_type",static_cast<int>(BRepAdaptor_Surface(face).GetType())},
                {"orientation",static_cast<int>(face.Orientation())}});
        }
        return nlohmann::json{{"volume",volume.Mass()},{"area",area.Mass()},
            {"valid",BRepCheck_Analyzer(shape).IsValid()},{"faces",std::move(faces)}};
    };
    const auto difference=[&](const TopoDS_Shape& a,const TopoDS_Shape& b) {
        BRepAlgoAPI_Cut cut(a,b);
        if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Independent native difference failed");
        return properties(cut.Shape());
    };
    const auto old_properties=properties(first),new_properties=properties(second);
    auto evidence=nlohmann::json{{"original",old_properties},{"candidate",new_properties},
        {"original_minus_candidate",difference(first,second)},
        {"candidate_minus_original",difference(second,first)},
        {"current_history_contract_error",format_migration_calculated_contract_error(previous,current)},
        {"authored_history_equal",comparable_history(authored.history)==comparable_history(updated.history)},
        {"body_ownership_equal",authored.body_history==updated.body_history},
        {"unavailable_original_boundary_properties",nlohmann::json::array()}};
    for(std::size_t i=0;i<previous.size();++i)if(!previous[i].physical_properties_current&&previous[i].kernel_shape.empty())
        evidence["unavailable_original_boundary_properties"].push_back(i);
    std::filesystem::create_directories(report.parent_path());
    std::ofstream output(document::native_file_io_path(report));output<<evidence.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot save independent native comparison");
}
}
