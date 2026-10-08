#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <nlohmann/json.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <iostream>
#include <chrono>
#include <set>
#include <numbers>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6) {
    if(std::abs(a-b)>tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));
}
void valid(const kernel::BodyResult& result) {
    for(const auto& [owner,error]:result.calculation_errors)std::cerr<<owner<<": "<<error<<std::endl;
    check(result.calculation_errors.empty(),"FORM calculation failed");
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape,true,false,true).IsValid(),"FORM produced an invalid BRep");
    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    check(solids==1,"FORM is not connected to the sheet");
    GProp_GProps properties;const auto error=BRepGProp::VolumePropertiesGK(shape,properties,1e-12,false,true);
    check(std::isfinite(error)&&error>=0.,"FORM volume integration failed");near(result.volume,properties.Mass());
}
}
int main(int argc,char** argv){try {
    if(argc==3&&std::string_view(argv[1])=="--prepare-definition-xy") {
        const auto path=std::filesystem::path(argv[2]);
        const auto before=document::read_sheet_form_definition(path);auto part=before.part;
        unsigned changed=0,rotated_manual=0;
        for(auto& sketch:part.sketches) {
            auto* owner=part.find_container(sketch.owner_container_id);
            if(owner&&sketch.plane_auto&&sketch.plane==sketcher::SketchPlane::XZ&&
               document::placement_references_use_whole_origin(owner->placement.references)) {
                sketch.plane=sketcher::SketchPlane::XY;++changed;
            } else if(owner&&!sketch.plane_auto&&sketch.plane==sketcher::SketchPlane::YZ&&
                      document::placement_references_use_whole_origin(owner->placement.references)) {
                near(owner->placement.rotation_x,0.);near(owner->placement.rotation_y,0.);near(owner->placement.rotation_z,0.);
                owner->placement.rotation_offset_x=90.;owner->placement.absolute_rotation_x=90.;
                owner->placement.rotation_x=90.;++rotated_manual;
            }
        }
        check(changed==3,"FORM XY preparation expected three whole-Origin Sketches");
        check(rotated_manual==1,"FORM XY preparation expected one manual cutting profile");
        kernel::OcctKernel kernel;
        const auto boundaries=workspace::calculate_part_with_resolved_references(kernel,part,before.calculated.get());
        if(!boundaries.empty())for(const auto& [owner,error]:boundaries.back().calculation_errors)
            std::cerr<<owner<<": "<<error<<'\n';
        check(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"FORM XY source calculation failed");
        const auto after=document::sheet_form_definition(part,boundaries);
        check(before.bodies==after.bodies&&before.cut_sketch==after.cut_sketch&&before.surface==after.surface,
            "FORM XY preparation changed source identities");
        const auto old=document::sheet_form_request(before,{}, {},{0,1,0},{1,0,0},1.);
        const auto next=document::sheet_form_request(after,{}, {},{0,1,0},{1,0,0},1.);
        check(old.surface_snapshot&&next.surface_snapshot,"FORM XY preparation lost the source shell cache");
        near(next.source_normal.z,1.);near(next.source_normal.x,0.);near(next.source_normal.y,0.);
        near(old.surface_snapshot->surface_area,next.surface_snapshot->surface_area,1e-5);
        const auto shape=[](const kernel::BodyResult& body) {
            TopoDS_Shape result;BRep_Builder builder;std::istringstream input(body.kernel_shape);
            BRepTools::Read(result,input,builder);check(!result.IsNull(),"FORM comparison lost its native shell");return result;
        };
        gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),std::numbers::pi/2.);
        const auto rotated=BRepBuilderAPI_Transform(shape(*old.surface_snapshot),rotation,true).Shape();
        const auto rebuilt=shape(*next.surface_snapshot);
        const auto on_shell=[](kernel::Vec3 point,const TopoDS_Shape& shell) {
            const auto vertex=BRepBuilderAPI_MakeVertex(gp_Pnt(point.x,point.y,point.z)).Shape();
            BRepExtrema_DistShapeShape distance(vertex,shell);
            check(distance.IsDone()&&distance.Value()<1e-5,"FORM XY preparation changed the authored native shell");
        };
        for(const auto point:old.surface_snapshot->mesh.vertices)on_shell({point.x,-point.z,point.y},rebuilt);
        for(const auto point:next.surface_snapshot->mesh.vertices)on_shell(point,rotated);
        part.save(path,boundaries);
        const auto reopened=document::read_sheet_form_definition(path);
        const auto cut=std::ranges::find(reopened.part.sketches,reopened.cut_sketch,&sketcher::Sketch::id);
        check(cut!=reopened.part.sketches.end()&&cut->plane==sketcher::SketchPlane::XY&&cut->corner_radii.size()==2,
            "FORM XY native reload lost the cutting plane or R15 corners");
        std::cout<<"FORM XY: three Origin-bound Sketches, retained face-bound profile, unchanged IDs, independently compared rotated shell, save/reopen PASS\n";
        return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--prepare-definition") {
        const auto path=std::filesystem::path("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
        auto part=document::PartDocument::load(path);kernel::OcctKernel kernel;
        const auto boundaries=kernel.evaluate_history(part.kernel_operations());
        check(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"FORM source preparation failed");
        part.save(path,boundaries);
        const auto source=document::read_sheet_form_definition(path);
        const auto request=document::sheet_form_request(source,{}, {},{0,0,1},{1,0,0},1);
        check(static_cast<bool>(request.surface_snapshot),"Prepared FORM did not retain its native surface bindings");
        std::cout<<"Native FORM definition prepared"<<std::endl;return 0;
    }
    const auto begin=std::chrono::steady_clock::now();
    const auto elapsed=[](const auto start){return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    const auto source=document::read_sheet_form_definition("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
    auto cut_after_shape=source.part;auto role_graph=cut_after_shape.body_history;
    role_graph.move_body(source.bodies[1],0);
    cut_after_shape.set_body_history(std::move(role_graph));
    const auto reordered_source=document::sheet_form_definition(cut_after_shape,*source.calculated);
    const auto authored=source.part.serialized();
    const auto cut=std::ranges::find(source.part.sketches,source.cut_sketch,&sketcher::Sketch::id);
    check(cut!=source.part.sketches.end()&&cut->corner_radii.size()==2,
        "Ventilation Window cutting Sketch must contain two rounded corners");
    for(const auto& corner:cut->corner_radii) {
        near(corner.radius,15.);
        const auto vertex=std::ranges::find(cut->points,corner.vertex_id,&sketcher::SketchPoint::id);
        check(vertex!=cut->points.end(),"Window corner lost its source vertex");
        near(vertex->y,-20.);near(std::abs(vertex->x),50.);
    }
    auto part=document::PartDocument::create_default();
    static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Sheet"));
    auto stock=document::PartDocument::create_sketch_container();stock.feature_kind=document::FeatureKind::Flat;
    stock.flat.direction=document::ExtrusionDirection::Reverse;stock.flat.thickness_override=true;stock.flat.thickness=1;
    auto outline=sketcher::Sketch::create_default();outline.owner_container_id=stock.id;
    static_cast<void>(outline.add_rectangle(-100,-100,100,100));stock.flat.sketch_id=outline.id;
    part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);part.history.push_back(stock);part.sketches.push_back(outline);
    const auto original=part.kernel_operations();kernel::OcctKernel kernel;
    const auto baseline=kernel.evaluate_history(original);near(baseline.back().volume,40000.);
    const auto face=std::ranges::find_if(baseline.back().mesh.triangle_references,[](const auto& face){return
        (face.sheet_role==kernel::SheetFaceRole::SideA||face.sheet_role==kernel::SheetFaceRole::SideB)&&face.surface&&
        face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(face.surface->origin.z)<1e-9;});
    check(face!=baseline.back().mesh.triangle_references.end(),"Flat has no native outer face");
    std::cout<<"Support: "<<face->semantic_key<<" origin="<<face->surface->origin.x<<","<<face->surface->origin.y<<","<<face->surface->origin.z
        <<" axis="<<face->surface->axis.x<<","<<face->surface->axis.y<<","<<face->surface->axis.z<<" reversed="<<face->surface->reversed<<std::endl;
    const auto normal=kernel::sheet_material::mul(face->surface->axis,face->surface->reversed?-1.:1.);
    const auto position=kernel::sheet_material::mul(normal,kernel::sheet_material::dot(face->surface->origin,normal));
    const auto reference_geometry=part.construction_reference_geometry_for(stock.id,baseline.back().mesh.original_references);
    const auto* stock_body=part.body_owner_for_object(stock.id);
    check(stock_body!=nullptr,"FORM stock has no owning Body");
    const auto origin=stock_body->origin().id;
    std::set<std::string> tested_sides;
    for(const auto& side:baseline.back().mesh.triangle_references) {
        if(!side.surface||side.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||
           (side.sheet_role!=kernel::SheetFaceRole::SideA&&side.sheet_role!=kernel::SheetFaceRole::SideB)||
           !tested_sides.insert(side.semantic_key).second)continue;
        const auto outward=kernel::sheet_material::mul(side.surface->axis,side.surface->reversed?-1.:1.);
        const auto seed_point=kernel::sheet_material::add(kernel::Vec3{4,7,0},
            kernel::sheet_material::mul(outward,kernel::sheet_material::dot(side.surface->origin,outward)));
        const auto zero=document::sheet_form_attachment(side,origin,reference_geometry,{},seed_point);
        const auto zero_tangent=document::construction_direction_from_local_axis("x",
            {zero.rotation_x,zero.rotation_y,zero.rotation_z});
        const auto zero_z=document::construction_direction_from_local_axis("z",
            {zero.rotation_x,zero.rotation_y,zero.rotation_z});
        auto positioning_geometry=reference_geometry;
        kernel::ViewerPoint point;point.position={12,17,9};point.reference={"native-position-point","position"};
        positioning_geometry.points.push_back(point);
        kernel::ViewerEdge line;line.reference={"native-position-line","segment"};
        const auto anchor=kernel::sheet_material::add(side.surface->origin,
            kernel::sheet_material::add(kernel::sheet_material::mul(zero_tangent,21),kernel::sheet_material::mul(zero_z,13)));
        line.points={anchor,kernel::sheet_material::add(anchor,kernel::sheet_material::mul(zero_z,20))};
        positioning_geometry.edges.push_back(line);
        auto positioning_face=std::make_shared<kernel::SurfaceGeometry>();
        positioning_face->kind=kernel::SurfaceGeometry::Kind::Plane;
        positioning_face->origin=anchor;positioning_face->axis=zero_tangent;
        positioning_geometry.triangle_references.push_back({"native-position-face","face",{},positioning_face});
        check(document::sheet_form_position_reference_available({{},point.reference.owner_id,point.reference.semantic_key},positioning_geometry,outward)&&
            document::sheet_form_position_reference_available({{},line.reference.owner_id,line.reference.semantic_key},positioning_geometry,outward)&&
            document::sheet_form_position_reference_available({{},"native-position-face","face"},positioning_geometry,outward),
            "Form positioning reference kinds unavailable");
        auto curved=line;curved.reference.owner_id="curved-position-line";
        curved.points.insert(curved.points.begin()+1,kernel::sheet_material::add(anchor,zero_tangent));
        positioning_geometry.edges.push_back(curved);
        check(!document::sheet_form_position_reference_available({{},curved.reference.owner_id,curved.reference.semantic_key},positioning_geometry,outward),
            "Form offered a curved positioning segment");
        for(double offset:{0.,3.,-4.,-0.}) {
            auto posed=zero;
            posed.references[1]={"",point.reference.owner_id,point.reference.semantic_key,offset,true};
            posed.references[2]={"",point.reference.owner_id,point.reference.semantic_key,-2*offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM two point coordinates failed");
            const kernel::Vec3 result{posed.x,posed.y,posed.z};
            near(kernel::sheet_material::dot(result,zero_tangent),kernel::sheet_material::dot(point.position,zero_tangent)+offset);
            near(kernel::sheet_material::dot(result,zero_z),kernel::sheet_material::dot(point.position,zero_z)-2*offset);
            near(kernel::sheet_material::dot(kernel::sheet_material::sub(result,side.surface->origin),outward),0);
            auto serialized=document::create_sheet_form();serialized.placement=posed;
            serialized.sheet_form=document::copy_sheet_form_definition(source);serialized.sheet_form.support=side;
            auto point_part=part;point_part.insert_history_entry(document::PartHistoryKind::Feature,serialized.id);
            point_part.history.push_back(serialized);
            const auto reopened=document::PartDocument::from_serialized(point_part.serialized());
            check(std::signbit(reopened.history.back().placement.references[1].offset)==std::signbit(offset)&&
                std::signbit(reopened.history.back().placement.references[2].offset)==std::signbit(-2*offset),
                "Form persistence lost the authored signed zero offset");
            posed.references[1]={"","native-position-face","face",offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"Form planar face and point positioning failed");
            near(kernel::sheet_material::dot(kernel::sheet_material::sub({posed.x,posed.y,posed.z},anchor),zero_tangent),offset);
            posed.references[1]={"",line.reference.owner_id,line.reference.semantic_key,offset,true};
            check(document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM line and point coordinates failed");
            const auto distance_normal=kernel::sheet_material::cross(outward,zero_z);
            near(kernel::sheet_material::dot(kernel::sheet_material::sub({posed.x,posed.y,posed.z},anchor),distance_normal),offset);
            const auto before=posed;posed.references[2]=posed.references[1];
            check(!document::resolve_sheet_form_placement(posed,positioning_geometry),"FORM accepted dependent positioning lines");
            near(posed.x,before.x);near(posed.y,before.y);near(posed.z,before.z);
        }
        for(double angle:{0.,30.,-70.}) {
            document::Placement seed;seed.absolute_rotation_y=angle;
            auto attached=document::sheet_form_attachment(side,origin,reference_geometry,seed,seed_point);
            check(document::resolve_sheet_form_placement(attached,reference_geometry),"FORM native plane positioning failed");
            near(attached.x,seed_point.x);near(attached.y,seed_point.y);near(attached.z,seed_point.z);
            check(attached.references.size()==4,"FORM did not retain three position references and FRONT");
            check(document::point_constraint_remaining_dof(attached.references,reference_geometry,seed_point)==0,
                "FORM insertion point is not fixed by native references");
            const auto direction=document::construction_direction_from_local_axis("y",
                {attached.rotation_x,attached.rotation_y,attached.rotation_z});
            near(kernel::sheet_material::dot(direction,outward),1.);
            const auto tangent=document::construction_direction_from_local_axis("x",
                {attached.rotation_x,attached.rotation_y,attached.rotation_z});
            const auto radians=angle*std::numbers::pi/180.;
            const auto expected=kernel::sheet_material::add(kernel::sheet_material::mul(zero_tangent,std::cos(radians)),
                kernel::sheet_material::mul(kernel::sheet_material::cross(outward,zero_tangent),std::sin(radians)));
            near(kernel::sheet_material::dot(tangent,expected),1.);
            auto feature=document::create_sheet_form();feature.placement=attached;
            auto persisted=part;persisted.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
            feature.sheet_form=document::copy_sheet_form_definition(source);feature.sheet_form.support=side;
            persisted.history.push_back(feature);
            const auto reopened=document::PartDocument::from_serialized(persisted.serialized());
            check(reopened.history.back().placement==attached,"FORM attachment lost its native side or position references");
        }
    }
    check(tested_sides.size()==2,"FORM attachment test did not cover both sheet sides");
    {
        TopoDS_Shape solid;BRep_Builder builder;std::istringstream data(baseline.back().kernel_shape);BRepTools::Read(solid,data,builder);
        for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next()) {
            const auto actual=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(actual);
            if(surface.GetType()!=GeomAbs_Plane)continue;
            const auto plane=surface.Plane();if(plane.Distance(gp_Pnt(position.x,position.y,position.z))>1e-7)continue;
            auto outward=plane.Axis().Direction();if(!plane.Position().Direct())outward.Reverse();if(actual.Orientation()==TopAbs_REVERSED)outward.Reverse();
            BRepClass_FaceClassifier classifier(actual,gp_Pnt(position.x,position.y,position.z),1e-7);
            std::cout<<"Native support n="<<outward.X()<<","<<outward.Y()<<","<<outward.Z()<<" state="<<classifier.State()<<std::endl;
        }
    }
    auto start=std::chrono::steady_clock::now();
    auto request=document::sheet_form_request(source,*face,position,normal,face->surface->radial,1);
    check(static_cast<bool>(request.surface_snapshot),"FORM did not consume its calculated surface");
    auto changed_source=source;
    auto* source_shell=changed_source.part.find_container(
        changed_source.part.body_history.find(changed_source.bodies[1])->entries.back().id);
    check(source_shell&&source_shell->feature_kind==document::FeatureKind::Shell,"FORM fixture has no source shell");
    source_shell->shell.thickness=.25;
    check(!document::sheet_form_request(changed_source,*face,position,normal,face->surface->radial,1).surface_snapshot,
        "Changing FORM geometry reused a stale calculated shell");
    std::cout<<"FORM definition preparation="<<elapsed(start)<<" s"<<std::endl;
    kernel::HistoryOperation form{"inserted-form",request};form.body=original.front().body;
    kernel::SheetMaterialDefinition material;material.kind=kernel::SheetMaterialDefinition::Kind::Form;
    material.owner_id=form.owner_id;material.parent_owner_id=face->sheet_owner;material.thickness=1;
    material.origin=request.position;material.along=request.x_direction;material.radial=request.normal;
    material.tangent=kernel::sheet_material::cross(material.radial,material.along);form.sheet_material=material;
    auto history=original;history.push_back(form);
    if(argc==3&&std::string_view(argv[1])=="--angle") {
        const auto degrees=std::stod(argv[2]);const auto radians=degrees*std::numbers::pi/180.;
        request.x_direction=kernel::sheet_material::add(kernel::sheet_material::mul(request.x_direction,std::cos(radians)),
            kernel::sheet_material::mul(kernel::sheet_material::cross(normal,request.x_direction),std::sin(radians)));
        history.back().primitive=request;start=std::chrono::steady_clock::now();
        const auto rotated=kernel.evaluate_history_incremental(history,baseline);
        std::cout<<"FORM angle="<<degrees<<" insertion="<<elapsed(start)<<" s"<<std::endl;
        valid(rotated.back());return 0;
    }
    start=std::chrono::steady_clock::now();const auto result=kernel.evaluate_history(history);
    std::cout<<"FORM cold insertion="<<elapsed(start)<<" s"<<std::endl;valid(result.back());
    check(result.back().volume!=baseline.back().volume,"FORM left the spatial sheet unchanged");
    {
        auto reordered_history=original;auto reordered_form=form;
        reordered_form.primitive=document::sheet_form_request(reordered_source,*face,position,normal,face->surface->radial,1.);
        reordered_history.push_back(reordered_form);
        const auto reordered_result=kernel.evaluate_history_incremental(reordered_history,baseline);
        valid(reordered_result.back());near(reordered_result.back().volume,result.back().volume);
        auto before=document::serialize_viewer_reference_geometry(result.back().mesh.original_references);
        auto after=document::serialize_viewer_reference_geometry(reordered_result.back().mesh.original_references);
        // Restoring the input BRep may reorder its display triangulation.
        // Compare actual sample positions independently of vertex indexing,
        // and keep every identity, analytical surface and exact edge check.
        const auto positions=[](const auto& geometry) {
            std::set<std::array<double,3>> values;
            for(const auto& p:geometry.vertices)values.insert({p.x,p.y,p.z});
            return values;
        };
        check(positions(reordered_result.back().mesh.original_references)==positions(result.back().mesh.original_references),
            "FORM_CUT after FORM changed original reference sample geometry");
        for(const auto* key:{"vertices_binary","triangles_binary"}) {before.erase(key);after.erase(key);}
        check(after==before,
            "FORM_CUT after FORM changed native result reference geometry");
        std::cout<<"FORM_CUT after FORM produced equivalent valid spatial geometry\n";
    }
    check(std::ranges::any_of(result.back().mesh.triangle_references,[&](const auto& face){return face.owner_id==form.owner_id&&face.sheet_role==kernel::SheetFaceRole::SideA;}),"FORM lost its outside skin identity");
    std::cout<<"Spatial FORM volume="<<result.back().volume<<std::endl;
    kernel::HistoryOperation flat{"unbend-form",kernel::SheetStateRequest{true,false,{form.owner_id}}};flat.body=form.body;
    history.push_back(flat);start=std::chrono::steady_clock::now();const auto unfolded=kernel.evaluate_history_incremental(history,result);
    std::cout<<"FORM flat calculation="<<elapsed(start)<<" s"<<std::endl;valid(unfolded.back());
    near(unfolded.back().volume,baseline.back().volume);
    check(std::ranges::none_of(unfolded.back().mesh.triangle_references,[&](const auto& face){return face.owner_id==form.owner_id;}),"Empty FORM_FLAT left a spatial forming face");
    kernel::HistoryOperation restore{"restore-form",kernel::SheetStateRequest{false,false,{form.owner_id}}};restore.body=form.body;
    history.push_back(restore);start=std::chrono::steady_clock::now();const auto restored=kernel.evaluate_history_incremental(history,unfolded);
    std::cout<<"FORM shape restoration="<<elapsed(start)<<" s"<<std::endl;valid(restored.back());
    near(restored.back().volume,result.back().volume);
    auto cut_history=original;cut_history.push_back(form);
    auto cut_sketch=sketcher::Sketch::create_default();
    auto opening=document::PartDocument::create_extrusion_container(cut_sketch.id);opening.extrusion.sheet_cut=true;
    cut_sketch.owner_container_id=opening.id;
    static_cast<void>(cut_sketch.add_rectangle(-5,-5,5,5));opening.extrusion.sketch_id=cut_sketch.id;
    opening.extrusion.height=10;opening.extrusion.length_forward=10;
    opening.extrusion.extent_mode=document::ProfileExtentMode::OneSide;opening.combine_mode=document::CombineMode::Subtract;
    auto cut_part=part;cut_part.insert_history_entry(document::PartHistoryKind::Feature,opening.id);
    cut_part.history.push_back(opening);cut_part.sketches.push_back(cut_sketch);
    auto cut_operation=cut_part.kernel_operations().back();cut_history.push_back(cut_operation);
    const auto cut_result=kernel.evaluate_history_incremental(cut_history,result);valid(cut_result.back());
    check(std::ranges::none_of(cut_result.back().sheet_cuts,[&](const auto& region){
        return region.source.owner_id==form.owner_id;}),"Sheet Cut processed a symbolic FORM skin");
    check(source.part.serialized()==authored,"FORM insertion changed the library source");
    auto feature=document::create_sheet_form();feature.sheet_form=document::copy_sheet_form_definition(source);
    feature.sheet_form.support=*face;feature.placement.rotation_x=90;feature.placement.absolute_rotation_x=90;
    check(normal==kernel::Vec3{0,0,1},"Native persistence fixture must retain its outward normal");
    check(document::stored_sheet_form_definition(feature.sheet_form).part.document_id!=source.part.document_id,
        "FORM persisted the library document namespace");
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
    const auto persisted=part.serialized();auto reopened=document::PartDocument::from_serialized(persisted);
    check(reopened.history.back()==feature,"FORM JSON round trip lost its independent definition or placement");
    const auto file=std::filesystem::absolute("build/form-diagnostic/InsertedVentilationWindow.prtz");part.save(file);
    reopened=document::PartDocument::load(file);
    check(reopened.history.back()==feature,"FORM native save/reopen lost its independent definition or placement");
    auto copied=document::stored_sheet_form_definition(feature.sheet_form);
    auto renamed=copied.part;for(const auto& id:copied.bodies) {
        auto body=*renamed.body_history.find(id);body.name="Renamed "+id;renamed.body_history.update_body(body);
    }
    auto parameters=feature.sheet_form;
    parameters.definition=std::make_shared<const std::string>(renamed.serialized(*copied.calculated).dump());
    check(document::stored_sheet_form_definition(parameters).bodies==copied.bodies,"FORM roles depended on Body names after insertion");
    start=std::chrono::steady_clock::now();const auto native=kernel.evaluate_history(reopened.kernel_operations());
    std::cout<<"FORM reopened native calculation="<<elapsed(start)<<" s"<<std::endl;valid(native.back());
    near(native.back().volume,result.back().volume);
    std::cout<<"FORM entire regression="<<elapsed(begin)<<" s"<<std::endl;
    std::cout<<"Native spatial/flat FORM geometry passed"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
