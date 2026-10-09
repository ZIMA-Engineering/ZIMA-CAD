#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/feature_reference_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/solid_state_reference_views.hpp>
#include "sweep_test_support.hpp"
#include <filesystem>
#include <iostream>
#include <numbers>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pln.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <sstream>
#include <iomanip>

using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){
    if(!std::isfinite(actual)||std::abs(actual-expected)>=1e-5)
        throw std::runtime_error("Native state value: expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void native_fillets() {
    for(bool after_straight:{false,true})for(bool linear:{false,true}) {
        kernel::OcctKernel kernel;workspace::Workspace live;
        auto part=document::PartDocument::create_default();
        if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
        const auto id=part.document_id;live.add_part(part,{});live.activate(id);
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(100,0,120,20));
        const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
        auto source=document::PartDocument::create_revolution_container(sketch.id);
        source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
        workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
        auto* state=live.open_part(id);const auto authored_source=*state->session.document().find_container(source.id);
        const auto original_operations=state->session.document().kernel_operations();
        auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
        if(after_straight)require(workspace::commit_solid_state(live,kernel,id,straight),"Pre-Fillet Straighten failed");
        const auto& mesh=state->session.calculated_boundaries().back().mesh;
        const auto edge=std::ranges::find_if(mesh.edges,[&](const auto& e) {
            if(e.reference.owner_id!=(after_straight?straight.id:source.id))return false;
            const auto parent=kernel::solid_state_parent(e.reference.semantic_key);
            if(after_straight&&!parent)return false;
            const auto key=after_straight?parent->second:e.reference.semantic_key;
            return key.starts_with("generated:")&&e.edge_treatment_endpoint_references.size()==2;
        });
        require(edge!=mesh.edges.end(),"Native Fillet fixture has no longitudinal input edge");
        auto fillet=document::PartDocument::create_fillet_container({edge->reference});
        fillet.edge_treatment.primary_size=1;fillet.edge_treatment.secondary_size=2;
        if(linear) {
            fillet.edge_treatment.fillet_mode=document::EdgeTreatmentParameters::FilletMode::Linear;
            fillet.edge_treatment.reverse=true;
            fillet.edge_treatment.route_start_vertices={edge->edge_treatment_endpoint_references.front()};
        }
        require(workspace::commit_edge_treatment(live,kernel,id,fillet,workspace::EdgeTreatmentEditMode::Create),"Native state Fillet failed");
        const auto authored_fillet=*state->session.document().find_container(fillet.id);
        auto reference_request=std::get<kernel::FilletRequest>(state->session.document().kernel_operations().back().primitive);
        const auto original_reference=[&](auto& reference) {
            if(reference.owner_id==straight.id) {
                const auto parent=kernel::solid_state_parent(reference.semantic_key);
                require(parent.has_value(),"State Fillet reference has no source ancestry");
                reference.owner_id=parent->first;reference.semantic_key=parent->second;
            }
        };
        for(auto& ref:reference_request.edges)original_reference(ref);
        for(auto& ref:reference_request.contour_start_vertices)original_reference(ref);
        auto expected_operations=original_operations;
        auto expected_fillet=state->session.document().kernel_operations().back();expected_fillet.primitive=reference_request;
        expected_operations.push_back(std::move(expected_fillet));
        const auto expected=kernel.evaluate_history(expected_operations).back().volume;
        require(expected<400*110*std::numbers::pi/2,"Independent Fillet did not remove material");
        if(!after_straight)require(workspace::commit_solid_state(live,kernel,id,straight),"Post-Fillet Straighten failed");
        const auto flat=state->session.calculated_boundaries().back().volume;
        require(flat<400*110*std::numbers::pi/2*.9,"Straighten discarded Fillet material removal");
        auto restore=document::PartDocument::create_solid_state_container(true);
        require(workspace::commit_solid_state(live,kernel,id,restore),"Native treated Restore failed");
        near(state->session.calculated_boundaries().back().volume,expected);
        require(*state->session.document().find_container(fillet.id)==authored_fillet&&
            *state->session.document().find_container(source.id)==authored_source,"State replay rewrote Fillet radius, direction or source identity");
        require(workspace::step_part_document_history(live,id,false),"Treated state Undo failed");near(state->session.calculated_boundaries().back().volume,flat);
        require(workspace::step_part_document_history(live,id,true),"Treated state Redo failed");near(state->session.calculated_boundaries().back().volume,expected);
        const auto path=std::filesystem::temp_directory_path()/("zima-state-fillet-"+id+".prtz");
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);
        require(*reopened.find_container(fillet.id)==authored_fillet,"Native reload lost Fillet intent");near(saved.back().volume,expected);
        kernel::OcctKernel cold;const auto regenerated=workspace::calculate_part_with_resolved_references(cold,reopened);
        require(regenerated.back().calculation_errors.empty(),"Native treated cold regeneration failed");near(regenerated.back().volume,expected);
        std::filesystem::remove(path);
    }
}
void invalid_fillet_state() {
    for(bool editing:{false,true}) {
        kernel::OcctKernel kernel;workspace::Workspace live;
        auto part=document::PartDocument::create_default();
        if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
        const auto id=part.document_id;live.add_part(part,{});live.activate(id);
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(100,0,120,20));
        const auto rim_curve=sketch.segments.front().id;
        const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
        auto source=document::PartDocument::create_revolution_container(sketch.id);
        source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
        workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
        auto* state=live.open_part(id);
        std::vector<kernel::EdgeReference> rims;
        for(const auto& edge:state->session.calculated_boundaries().back().mesh.edges)
            if(edge.reference.owner_id==source.id&&
                (edge.reference.semantic_key=="start:"+rim_curve||edge.reference.semantic_key=="end:"+rim_curve))
                rims.push_back(edge.reference);
        require(rims.size()==2,"Invalid Fillet fixture lacks its two authored rim edges");
        auto fillet=document::PartDocument::create_fillet_container(rims);
        fillet.edge_treatment.primary_size=2;
        require(workspace::commit_edge_treatment(live,kernel,id,fillet,workspace::EdgeTreatmentEditMode::Create),
            "Initial end-rim Fillet failed");
        auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
        if(editing)require(workspace::commit_solid_state(live,kernel,id,straight),"Initial end-rim Straighten failed");
        const auto before=state->session.document().serialized();
        const auto revision=state->session.revision(),generation=state->session.data_generation();
        const auto* calculated=state->session.calculated_boundaries().data();
        const auto can_undo=state->session.can_undo(),can_redo=state->session.can_redo();
        const auto* input=workspace::calculated_operation_input(state->session,editing?straight.id:std::string{});
        require(input,"Invalid Fillet state lacks calculated input");const auto input_volume=input->volume;
        // Developed stock becomes 0.173 mm long: opposing R2 end fillets cannot fit.
        // A valid definition with a failed calculation is retained with its
        // native error under the general feature-definition contract.
        straight.solid_state.coefficient=.001;
        if(editing) {
            // An existing successfully calculated feature keeps the established
            // atomic rejection policy when a proposed edit would fail.
            std::string failure;
            try {static_cast<void>(workspace::commit_solid_state(live,kernel,id,straight));}
            catch(const std::exception& error){failure=error.what();}
            require(!failure.empty(),"Impossible edit of a valid state was silently accepted");
            require(state->session.document().serialized()==before&&state->session.revision()==revision&&
                state->session.data_generation()==generation&&state->session.calculated_boundaries().data()==calculated&&
                state->session.can_undo()==can_undo&&state->session.can_redo()==can_redo,
                "Rejected state edit changed the document, geometry or Undo state");
            require(workspace::step_part_document_history(live,id,false)&&!state->session.document().find_container(straight.id),
                "Rejected state edit inserted an extra Undo step");
            require(workspace::step_part_document_history(live,id,true)&&state->session.document().serialized()==before,
                "Rejected state edit damaged the previous Redo operation");
            continue;
        }
        require(workspace::commit_solid_state(live,kernel,id,straight),"Failed state definition was not retained");
        const auto& failed=state->session.calculated_boundaries().back();
        require(failed.calculation_errors.contains(straight.id)&&!failed.calculation_errors.at(straight.id).empty(),
            "Impossible end-rim Fillet lacks its native calculation error");
        require(*state->session.document().find_container(straight.id)==straight&&
            state->session.revision()==revision+1&&state->session.data_generation()>generation,
            "Failed state lost its authored definition or transaction");
        near(failed.volume,input_volume);
        const auto retained=state->session.document().serialized();
        const auto retained_error=failed.calculation_errors.at(straight.id);
        require(workspace::step_part_document_history(live,id,false),"Failed state Undo was unavailable");
        auto undone=state->session.document().serialized();
        // DocumentSession intentionally retains assigned dimension numbers
        // across Undo. Check that registry independently from authored content.
        const auto& registry=undone.at("dimension_identifiers");
        for(const auto& entry:before.at("dimension_identifiers").at("entries"))
            require(std::find(registry.at("entries").begin(),registry.at("entries").end(),entry)!=registry.at("entries").end(),
                "Failed state Undo changed an existing dimension identity");
        for(const auto& entry:registry.at("entries"))if(std::find(before.at("dimension_identifiers").at("entries").begin(),
                before.at("dimension_identifiers").at("entries").end(),entry)==before.at("dimension_identifiers").at("entries").end())
            require(entry.at("owner")==straight.id,"Failed state Undo allocated unrelated dimension identities");
        require(registry.at("next")>=before.at("dimension_identifiers").at("next"),"Undo reused retired dimension numbers");
        undone["dimension_identifiers"]=before.at("dimension_identifiers");
        require(undone==before,
            "Failed state Undo did not restore the complete previous document");
        require(workspace::step_part_document_history(live,id,true)&&state->session.document().serialized()==retained&&
            state->session.calculated_boundaries().back().calculation_errors.at(straight.id)==retained_error,
            "Failed state Redo lost the definition or error");
        const auto path=std::filesystem::temp_directory_path()/("zima-failed-state-"+id+".prtz");
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(path,&saved);
        require(*reopened.find_container(straight.id)==straight&&saved.back().calculation_errors.at(straight.id)==retained_error,
            "Failed state save/reopen lost its parameters or native error");
        near(saved.back().volume,input_volume);std::filesystem::remove(path);
    }
}
void native_holes() {
    for(bool after_straight:{false,true}) {
        kernel::OcctKernel kernel;workspace::Workspace live;
        auto part=document::PartDocument::create_default();
        if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
        const auto id=part.document_id;live.add_part(part,{});live.activate(id);
        auto source=test_support::sweep_fixture(document::FeatureKind::Sweep3D);
        source.sweep_precision.custom_tolerance=1e-4;
        auto endpoint=document::PartDocument::create_construction(document::ConstructionKind::Point);
        endpoint.parent_construction_id=source.sweep3d.path.id;endpoint.origin={20,0,20};source.sweep3d.path.curve_points.push_back(endpoint);
        source.sweep3d.path.curve_rounding_enabled=true;source.sweep3d.path.curve_points[1].curve_radius=5;
        auto section=sketcher::Sketch::from_serialized(source.sweep3d.profiles.front().sketch_serialized);
        section.circles.clear();section.points.clear();static_cast<void>(section.add_rectangle(-2,-2,2,2));
        source.sweep3d.profiles.front().sketch_serialized=section.serialized();
        workspace::commit_sweep(live,kernel,id,source,workspace::SweepEditMode::Create);
        auto* state=live.open_part(id);const auto authored_source=*state->session.document().find_container(source.id);
        const double ideal=16*(30+5*std::numbers::pi/2),removed=std::numbers::pi;
        const double formed=state->session.calculated_boundaries().back().volume;
        require(std::abs(formed-ideal)<.002,"Native curved Sweep exceeds the fixture approximation budget");
        auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
        if(after_straight)require(workspace::commit_solid_state(live,kernel,id,straight),"Pre-hole Straighten failed");
        auto hole_sketch=sketcher::Sketch::create_default();static_cast<void>(hole_sketch.add_circle(0,0,.5));
        auto hole=document::PartDocument::create_extrusion_container(hole_sketch.id);
        hole.combine_mode=document::CombineMode::Subtract;hole.extrusion.length_forward=6;hole.extrusion.height=6;
        hole.placement.y=-3;hole.placement.z=7.5*(after_straight?.9:1.);hole.placement.rotation_x=-90;
        hole.placement.absolute_rotation_x=-90;
        workspace::commit_profile(live,kernel,id,hole,workspace::ProfileEditMode::Create,hole_sketch);
        const auto authored_hole=*state->session.document().find_container(hole.id);
        const auto check_bore=[&](const kernel::BodyResult& result,const std::string& state_id,double z) {
            const auto packet=result.solid_state_reference_views.find(state_id);
            require(packet!=result.solid_state_reference_views.end(),"Native hole state lost reference correspondence");
            bool found=false;
            for(const auto& face:packet->second->triangle_references)
                if(face.owner_id==hole.id&&face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
                    found=true;near(face.surface->radius,.5);near(face.surface->origin.x,0);near(face.surface->origin.z,z);
                    near(std::abs(face.surface->axis.y),1);
                }
            require(found,"Native hole state lost its cylindrical surface");
        };
        near(state->session.calculated_boundaries().back().volume,(after_straight?ideal*.9:formed)-removed);
        if(!after_straight)require(workspace::commit_solid_state(live,kernel,id,straight),"Post-hole Straighten failed");
        near(state->session.calculated_boundaries().back().volume,ideal*.9-removed);
        // A hole created after Straighten belongs to the normal authored prefix;
        // the earlier state's packet intentionally excludes that later feature.
        if(!after_straight)check_bore(state->session.calculated_boundaries().back(),straight.id,7.5*.9);
        auto restore=document::PartDocument::create_solid_state_container(true);
        require(workspace::commit_solid_state(live,kernel,id,restore),"Native hole Restore failed");
        near(state->session.calculated_boundaries().back().volume,formed-removed);
        check_bore(state->session.calculated_boundaries().back(),restore.id,7.5);
        require(*state->session.document().find_container(source.id)==authored_source&&
            *state->session.document().find_container(hole.id)==authored_hole,"State replay rewrote authored hole geometry or placement");
        require(workspace::step_part_document_history(live,id,false),"Hole state Undo failed");near(state->session.calculated_boundaries().back().volume,ideal*.9-removed);
        require(workspace::step_part_document_history(live,id,true),"Hole state Redo failed");near(state->session.calculated_boundaries().back().volume,formed-removed);
        const auto path=std::filesystem::temp_directory_path()/("zima-state-hole-"+id+".prtz");
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);
        require(*reopened.find_container(hole.id)==authored_hole,"Native reload changed hole parameters");near(saved.back().volume,formed-removed);
        kernel::OcctKernel cold;const auto regenerated=workspace::calculate_part_with_resolved_references(cold,reopened);
        require(regenerated.back().calculation_errors.empty(),"Native hole cold regeneration failed");near(regenerated.back().volume,formed-removed);
        check_bore(regenerated.back(),restore.id,7.5);
        // A complete removed volume crossing the straight/curved transition is
        // unsupported even when its center is still inside the straight leg.
        auto crossing_sketch=sketcher::Sketch::create_default();static_cast<void>(crossing_sketch.add_circle(0,0,.5));
        auto crossing=document::PartDocument::create_extrusion_container(crossing_sketch.id);
        crossing.combine_mode=document::CombineMode::Subtract;crossing.extrusion.length_forward=6;crossing.extrusion.height=6;
        crossing.placement.y=-3;crossing.placement.z=14.9;
        crossing.placement.rotation_x=-90;crossing.placement.absolute_rotation_x=-90;
        workspace::commit_profile(live,kernel,id,crossing,workspace::ProfileEditMode::Create,crossing_sketch);
        const auto before=state->session.document().serialized();
        const auto revision=state->session.revision(),generation=state->session.data_generation();
        const auto* calculated=state->session.calculated_boundaries().data();
        bool rejected=false;try {
            static_cast<void>(workspace::commit_solid_state(live,kernel,id,document::PartDocument::create_solid_state_container()));
        }catch(const std::invalid_argument&){rejected=true;}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&state->session.document().serialized()==before&&state->session.revision()==revision&&
            state->session.data_generation()==generation&&state->session.calculated_boundaries().data()==calculated,
            "Crossing-hole state was accepted or partially committed");
        std::filesystem::remove(path);
    }
}
void native_sweep_lifecycle(document::HistoryContainer source,double expected,double tolerance,bool probe_rectangle=false) {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    workspace::commit_sweep(live,kernel,id,source,workspace::SweepEditMode::Create);
    auto* state=live.open_part(id);const auto authored=*state->session.document().find_container(source.id);
    const auto formed=state->session.calculated_boundaries().back().volume;
    if(std::abs(formed-expected)>=tolerance)throw std::runtime_error(
        "Native sweep source volume differs from independent length: expected "+std::to_string(expected)+", got "+std::to_string(formed));
    if(probe_rectangle) {
        TopoDS_Shape shape;BRep_Builder builder;
        std::istringstream stored(state->session.calculated_boundaries().back().kernel_shape);
        BRepTools::Read(shape,stored,builder);require(!shape.IsNull(),"Spatial rectangle has no exact body");
        const auto operations=state->session.document().kernel_operations();
        const auto& sweep=std::get<kernel::Sweep3DRequest>(operations.front().primitive);
        double largest_perimeter_error=0,largest_side_error=0,largest_angle_cosine=0,largest_edge_bow=0;std::size_t probes=0;
        for(const auto& segment:sweep.path_segments) {
        std::vector<std::vector<kernel::Vec3>> spans;
        if(segment.bezier_spans.empty())spans.push_back(segment.bezier_control_points);
        else for(const auto& span:segment.bezier_spans)spans.emplace_back(span.begin(),span.end());
        for(const auto& poles:spans) {
        require(poles.size()==4,"Spatial section probe requires a cubic curve");
        for(double t:{.25,.5,.75}) {
            const auto value=[&](auto member,bool derivative) {
                const double a=poles[0].*member,b=poles[1].*member,c=poles[2].*member,d=poles[3].*member;
                return derivative?3*((1-t)*(1-t)*(b-a)+2*t*(1-t)*(c-b)+t*t*(d-c)):
                    (1-t)*(1-t)*(1-t)*a+3*t*(1-t)*(1-t)*b+3*t*t*(1-t)*c+t*t*t*d;
            };
            BRepAlgoAPI_Section cut(shape,gp_Pln(gp_Pnt(value(&kernel::Vec3::x,false),value(&kernel::Vec3::y,false),value(&kernel::Vec3::z,false)),
                gp_Dir(value(&kernel::Vec3::x,true),value(&kernel::Vec3::y,true),value(&kernel::Vec3::z,true))),true);
            require(cut.IsDone(),"Spatial rectangle section probe failed");
            GProp_GProps perimeter;BRepGProp::LinearProperties(cut.Shape(),perimeter);
            largest_perimeter_error=std::max(largest_perimeter_error,std::abs(perimeter.Mass()-6));++probes;
            std::vector<std::pair<gp_Pnt,gp_Pnt>> chords;
            for(TopExp_Explorer edges(cut.Shape(),TopAbs_EDGE);edges.More();edges.Next()) {
                BRepAdaptor_Curve edge(TopoDS::Edge(edges.Current()));
                const auto a=edge.Value(edge.FirstParameter()),b=edge.Value(edge.LastParameter());
                const double length=a.Distance(b);largest_side_error=std::max(largest_side_error,std::min(std::abs(length-1),std::abs(length-2)));
                const auto middle=edge.Value((edge.FirstParameter()+edge.LastParameter())/2);
                largest_edge_bow=std::max(largest_edge_bow,gp_Vec(a,middle).Crossed(gp_Vec(a,b)).Magnitude()/length);
                chords.emplace_back(a,b);
            }
            require(chords.size()==4,"Spatial rectangle section does not have four sides");
            for(std::size_t i=0;i<chords.size();++i)for(std::size_t j=i+1;j<chords.size();++j) {
                const auto& a=chords[i];const auto& b=chords[j];
                if(std::min({a.first.Distance(b.first),a.first.Distance(b.second),a.second.Distance(b.first),a.second.Distance(b.second)})<1e-5)
                    largest_angle_cosine=std::max(largest_angle_cosine,std::abs(gp_Vec(a.first,a.second).Normalized().Dot(gp_Vec(b.first,b.second).Normalized())));
            }
        }
        }
        }
        std::cerr<<std::setprecision(16)<<"Spatial rectangle sections="<<probes<<" maximum perimeter error="<<largest_perimeter_error<<" mm\n";
        std::cerr<<"Spatial rectangle maximum side error="<<largest_side_error<<" mm, right-angle cosine="<<largest_angle_cosine<<", edge bow="<<largest_edge_bow<<" mm\n";
        const double linear_tolerance=source.sweep_precision.custom_tolerance.value();
        require(probes==9,"Spatial rectangle did not exercise all section probes");
        require(largest_perimeter_error<=4*linear_tolerance&&largest_side_error<=linear_tolerance&&
            2*largest_angle_cosine<=linear_tolerance&&largest_edge_bow<=linear_tolerance,
            "Spatial rectangle section exceeds the requested linear tolerance");
    }
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    require(workspace::commit_solid_state(live,kernel,id,straight),"Native sweep Straighten failed");
    require(std::abs(state->session.calculated_boundaries().back().volume-expected*.9)<tolerance,
        "Native sweep developed volume differs from independent length");
    const auto revision=state->session.revision(),generation=state->session.data_generation();
    require(!workspace::commit_solid_state(live,kernel,id,*state->session.document().find_container(straight.id))&&
        state->session.revision()==revision&&state->session.data_generation()==generation,"Native sweep unchanged state recalculated");
    require(*state->session.document().find_container(source.id)==authored,"Native sweep state changed authored source");
    const auto flat=state->session.calculated_boundaries().back().volume;
    const auto path=std::filesystem::temp_directory_path()/("zima-native-sweep-state-"+id+".prtz");
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cached;auto reopened=document::PartDocument::load(path,&cached);
    require(*reopened.find_container(source.id)==authored,"Native sweep reload changed source");near(cached.back().volume,flat);
    kernel::OcctKernel cold;
    auto regenerated=workspace::calculate_part_with_resolved_references(cold,reopened);
    require(regenerated.back().calculation_errors.empty(),"Native sweep cold regeneration failed");near(regenerated.back().volume,flat);
    auto restore=document::PartDocument::create_solid_state_container(true);
    require(workspace::commit_solid_state(live,kernel,id,restore),"Native sweep Restore failed");
    near(state->session.calculated_boundaries().back().volume,formed);
    require(workspace::step_part_document_history(live,id,false),"Native sweep state Undo failed");near(state->session.calculated_boundaries().back().volume,flat);
    require(workspace::step_part_document_history(live,id,true),"Native sweep state Redo failed");near(state->session.calculated_boundaries().back().volume,formed);
    state->session.document().save(path,state->session.calculated_boundaries());cached.clear();reopened=document::PartDocument::load(path,&cached);
    regenerated=workspace::calculate_part_with_resolved_references(cold,reopened);
    require(regenerated.back().calculation_errors.empty()&&*reopened.find_container(source.id)==authored,
        "Restored native sweep lost source definition");near(regenerated.back().volume,formed);
    std::filesystem::remove(path);
}
kernel::Vec3 spatial_attachment_chain(document::HistoryContainer source,bool flip) {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    workspace::commit_sweep(live,kernel,id,source,workspace::SweepEditMode::Create);
    auto* state=live.open_part(id);
    const auto& original=state->session.calculated_boundaries().back().mesh.original_references;
    const auto vertex=std::ranges::find_if(original.points,[&](const auto& p) {
        return p.reference.owner_id==source.id&&p.reference.semantic_key.starts_with("sweep:vertex:end:")&&p.position.z>73;
    });
    require(vertex!=original.points.end(),"Spatial source has no final semantic vertex");
    const auto formed_vertex=*vertex;
    const auto cap=std::ranges::find_if(original.triangle_references,[&](const auto& f) {
        return f.owner_id==source.id&&f.semantic_key.starts_with("sweep:cap:end:")&&f.surface&&f.surface->origin.z>73;
    });
    require(cap!=original.triangle_references.end(),"Spatial source has no final cap plane");
    const auto cap_key=cap->semantic_key;
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    require(workspace::commit_solid_state(live,kernel,id,straight),"Spatial attachment Straighten failed");
    auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(0,0,.2,.2));
    auto child=document::PartDocument::create_extrusion_container(profile.id);child.extrusion.length_forward=2;
    document::ConstructionReference point;point.owner_id=source.id;point.semantic_key=formed_vertex.reference.semantic_key;
    auto front=point;front.semantic_key=cap_key;front.orientation_role="front";
    front.orientation_drives_rotation=true;front.orientation_only=true;front.flip=flip;
    child.placement.references={point,front};
    workspace::commit_profile(live,kernel,id,child,workspace::ProfileEditMode::Create,profile);
    auto second_profile=sketcher::Sketch::create_default();static_cast<void>(second_profile.add_rectangle(0,0,.1,.1));
    auto descendant=document::PartDocument::create_extrusion_container(second_profile.id);descendant.extrusion.length_forward=3;
    point.owner_id=child.id+":origin";point.semantic_key="origin:point";descendant.placement.references={point};
    workspace::commit_profile(live,kernel,id,descendant,workspace::ProfileEditMode::Create,second_profile);
    auto changed=*state->session.document().find_container(straight.id);changed.solid_state.coefficient=1.1;
    require(workspace::commit_solid_state(live,kernel,id,changed),"Spatial coefficient edit failed with attached chain");
    const auto& flat=*state->session.calculated_boundaries().back().solid_state_reference_views.at(straight.id);
    const auto endpoint=std::ranges::find_if(flat.points,[&](const auto& p){return p.reference==formed_vertex.reference;});
    require(endpoint!=flat.points.end(),"Spatial coefficient edit lost its source endpoint");
    std::map<std::string,std::string> anchors;
    const auto& geometry=state->session.calculated_boundaries().back().mesh.original_references;
    for(const auto& owner:{child.id,descendant.id}) {
        const auto& placement=state->session.document().find_container(owner)->placement;
        near(placement.x,endpoint->position.x);near(placement.y,endpoint->position.y);near(placement.z,endpoint->position.z);
        const auto anchor=std::ranges::find_if(geometry.points,[&](const auto& p) {
            return p.reference.owner_id==owner&&std::hypot(p.position.x-placement.x,p.position.y-placement.y,p.position.z-placement.z)<1e-5;
        });
        require(anchor!=geometry.points.end(),"Spatial child lost its anchored semantic point");
        anchors.emplace(owner,anchor->reference.semantic_key);
    }
    const auto authored_child=*state->session.document().find_container(child.id);
    const auto authored_descendant=*state->session.document().find_container(descendant.id);
    auto restore=document::PartDocument::create_solid_state_container(true);
    require(workspace::commit_solid_state(live,kernel,id,restore),"Spatial attached Restore failed");
    kernel::Vec3 restored_axis;
    const auto verify=[&](const kernel::BodyResult& body) {
        require(body.calculation_errors.empty(),"Spatial attached calculation reported errors");
        const auto& packet=*body.solid_state_reference_views.at(restore.id);
        const auto face=std::ranges::find_if(packet.triangle_references,[&](const auto& f) {
            return f.owner_id==child.id&&f.semantic_key.starts_with("end:from:")&&f.surface;
        });
        require(face!=packet.triangle_references.end(),"Spatial Restore lost the attached end plane");
        const auto axis=face->surface->axis;
        if(std::hypot(restored_axis.x,restored_axis.y,restored_axis.z)>0) {
            near(axis.x,restored_axis.x);near(axis.y,restored_axis.y);near(axis.z,restored_axis.z);
        } else restored_axis=axis;
        for(const auto& [owner,key]:anchors) {
            const auto anchor=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
            require(anchor!=packet.points.end(),"Spatial Restore lost descendant anchor identity");
            near(anchor->position.x,formed_vertex.position.x);near(anchor->position.y,formed_vertex.position.y);near(anchor->position.z,formed_vertex.position.z);
        }
    };
    verify(state->session.calculated_boundaries().back());
    require(*state->session.document().find_container(child.id)==authored_child&&
        *state->session.document().find_container(descendant.id)==authored_descendant,"Spatial Restore rewrote authored references");
    require(workspace::step_part_document_history(live,id,false),"Spatial attached Undo failed");
    require(workspace::step_part_document_history(live,id,true),"Spatial attached Redo failed");verify(state->session.calculated_boundaries().back());
    const auto path=std::filesystem::temp_directory_path()/("zima-spatial-attachment-"+id+".prtz");
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);verify(saved.back());
    kernel::OcctKernel cold;verify(workspace::calculate_part_with_resolved_references(cold,reopened).back());
    require(*reopened.find_container(child.id)==authored_child&&*reopened.find_container(descendant.id)==authored_descendant,
        "Spatial attached cold regeneration rewrote authored placement");
    std::filesystem::remove(path);
    return restored_axis;
}
void native_spatial_sweeps() {
    auto source=test_support::sweep_fixture(document::FeatureKind::Sweep3D);
    source.sweep_precision.custom_tolerance=1e-4;
    source.sweep3d.path.curve_points.clear();source.sweep3d.path.curve_type=document::Curve3DType::InterpolatingSpline;
    for(const auto origin:std::vector<kernel::Vec3>{{0,0,0},{5,1,25},{15,5,50},{30,15,75}}) {
        auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
        point.parent_construction_id=source.sweep3d.path.id;point.origin=origin;source.sweep3d.path.curve_points.push_back(point);
    }
    source.sweep3d.profiles.front().point_id=source.sweep3d.path.curve_points.front().id;
    document::PartDocument request_document=document::PartDocument::create_default();request_document.history={source};
    const auto operations=request_document.kernel_operations();const auto& request=std::get<kernel::Sweep3DRequest>(operations.front().primitive);
    // Integrate cubic derivatives, independently of the development frame/chord algorithm.
    const auto cubic_length=[](const auto& poles) {
        require(poles.size()==4,"Native spatial spline test requires cubic spans");
        const auto speed=[&](double t) {
            const auto coordinate=[&](auto member) {
                return 3*((1-t)*(1-t)*(poles[1].*member-poles[0].*member)+
                    2*t*(1-t)*(poles[2].*member-poles[1].*member)+t*t*(poles[3].*member-poles[2].*member));
            };
            return std::hypot(coordinate(&kernel::Vec3::x),coordinate(&kernel::Vec3::y),coordinate(&kernel::Vec3::z));
        };
        constexpr int count=4096;double result=speed(0)+speed(1);
        for(int i=1;i<count;++i)result+=(i%2?4:2)*speed(double(i)/count);
        return result/(3*count);
    };
    double length=0;
    for(const auto& segment:request.path_segments) {
        if(!segment.bezier_spans.empty())for(const auto& span:segment.bezier_spans)length+=cubic_length(span);
        else length+=cubic_length(segment.bezier_control_points);
    }
    native_sweep_lifecycle(source,4*std::numbers::pi*length,.005);
    auto rectangular=source;
    auto rectangle=sketcher::Sketch::from_serialized(rectangular.sweep3d.profiles.front().sketch_serialized);
    rectangle.circles.clear();rectangle.points.clear();
    static_cast<void>(rectangle.add_rectangle(-1,-.5,1,.5));
    rectangular.sweep3d.profiles.front().sketch_serialized=rectangle.serialized();
    native_sweep_lifecycle(rectangular,2*length,.005,true);
    const auto normal=spatial_attachment_chain(rectangular,false);
    const auto flipped=spatial_attachment_chain(rectangular,true);
    near(normal.x,-flipped.x);near(normal.y,-flipped.y);near(normal.z,-flipped.z);
    for(bool rectangle:{false,true}) {
        auto helix=test_support::sweep_fixture(document::FeatureKind::HelicalSweep);
        helix.helical.pitch=5;helix.sweep_precision.custom_tolerance=1e-4;
        if(rectangle) {
            auto section=sketcher::Sketch::from_serialized(helix.helical.sketches[2]);section.circles.clear();section.points.clear();
            static_cast<void>(section.add_rectangle(-.5,-.3,.5,.3));helix.helical.sketches[2]=section.serialized();
        }
        const double area=rectangle?.6:.25*std::numbers::pi;
        native_sweep_lifecycle(helix,area*std::hypot(40*std::numbers::pi,10.),.002);
    }
}
void native_spline() {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    auto source=document::PartDocument::create_sweep2d_container();source.sweep_precision.custom_tolerance=1e-4;
    auto guide=sketcher::Sketch::from_serialized(source.sweep2d.path_sketch);
    static_cast<void>(guide.add_bspline({{0,0},{0,100./3},{20./3,200./3},{20,100}}));
    source.sweep2d.path_sketch=guide.serialized();document::PartDocument::reframe_sweep2d_sketches(source);
    const auto station=document::PartDocument::sweep2d_route(source).stations.front();
    const auto index=document::PartDocument::ensure_sweep2d_profile(source,station.point_id,station.incoming);
    auto section=sketcher::Sketch::from_serialized(source.sweep2d.profiles[index].sketch_serialized);
    auto rectangular=source;
    auto rectangle=section;static_cast<void>(rectangle.add_rectangle(-1,-.5,1,.5));
    rectangular.sweep2d.profiles[index].sketch_serialized=rectangle.serialized();
    native_sweep_lifecycle(rectangular,2*(.5*std::sqrt(11600.)+125*std::asinh(.4)),.002);
    std::vector<std::string> sides;for(const auto& segment:rectangle.segments)sides.push_back(segment.id);
    require(sides.size()==4,"Rounded spline fixture requires four profile sides");
    for(std::size_t corner=0;corner<4;++corner)
        static_cast<void>(rectangle.add_corner_fillet(sides[corner],sides[(corner+1)%4],.2));
    for(const bool hollow:{false,true}) {
        auto rounded=rectangle;
        if(hollow)static_cast<void>(rounded.add_circle(0,0,.1));
        rectangular.sweep2d.profiles[index].sketch_serialized=rounded.serialized();
        const double area=2-4*.2*.2+std::numbers::pi*.2*.2-(hollow?std::numbers::pi*.1*.1:0);
        native_sweep_lifecycle(rectangular,area*(.5*std::sqrt(11600.)+125*std::asinh(.4)),.002);
    }
    static_cast<void>(section.add_circle(0,0,.5));source.sweep2d.profiles[index].sketch_serialized=section.serialized();
    workspace::commit_sweep(live,kernel,id,source,workspace::SweepEditMode::Create);
    auto* state=live.open_part(id);
    const auto authored=*state->session.document().find_container(source.id);
    const double formed=state->session.calculated_boundaries().back().volume;
    const double expected=.25*std::numbers::pi*(.5*std::sqrt(11600.)+125*std::asinh(.4));
    require(std::abs(formed-expected)<.001,"Native spline adapter changed the analytic source volume");
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    straight.solid_state.all=false;straight.solid_state.owners={source.id};
    require(workspace::commit_solid_state(live,kernel,id,straight),"Native spline Straighten failed");
    near(state->session.calculated_boundaries().back().volume,expected*.9);
    require(*state->session.document().find_container(source.id)==authored,"Native spline state changed authored Sketches");
    const auto path=std::filesystem::temp_directory_path()/("zima-solid-spline-"+id+".prtz");
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cached;auto reopened=document::PartDocument::load(path,&cached);
    require(*reopened.find_container(source.id)==authored,"Native spline reload changed source identities");
    near(cached.back().volume,expected*.9);
    kernel::OcctKernel cold;
    const auto regenerated=workspace::calculate_part_with_resolved_references(cold,reopened);
    require(regenerated.back().calculation_errors.empty(),"Native spline cold regeneration failed");
    near(regenerated.back().volume,expected*.9);
    auto restore=document::PartDocument::create_solid_state_container(true);
    require(workspace::commit_solid_state(live,kernel,id,restore),"Native spline Restore failed");
    near(state->session.calculated_boundaries().back().volume,formed);
    require(workspace::step_part_document_history(live,id,false),"Native spline Restore Undo failed");
    near(state->session.calculated_boundaries().back().volume,expected*.9);
    require(workspace::step_part_document_history(live,id,true),"Native spline Restore Redo failed");
    near(state->session.calculated_boundaries().back().volume,formed);
    state->session.document().save(path,state->session.calculated_boundaries());cached.clear();
    reopened=document::PartDocument::load(path,&cached);
    require(*reopened.find_container(source.id)==authored&&reopened.find_container(restore.id),"Native spline restored history changed on reload");
    near(workspace::calculate_part_with_resolved_references(cold,reopened).back().volume,formed);
    auto changed=*state->session.document().find_container(source.id);
    guide=sketcher::Sketch::from_serialized(changed.sweep2d.path_sketch);
    const auto spline_id=guide.bsplines.front().id;
    guide.edit_bspline_properties(spline_id,3,false,{{0,0},{0,100./3},{10,200./3},{30,100}});
    changed.sweep2d.path_sketch=guide.serialized();
    workspace::commit_sweep(live,kernel,id,changed,workspace::SweepEditMode::Replace);
    const double revised=.25*std::numbers::pi*(.5*std::sqrt(13600.)+(10000./120)*std::asinh(.6));
    require(std::abs(state->session.calculated_boundaries().back().volume-revised)<.001,
        "Edited spline reused the previous restored geometry");
    require(sketcher::Sketch::from_serialized(state->session.document().find_container(source.id)->sweep2d.path_sketch).bsplines.front().id==spline_id,
        "Editing spline source changed its curve identity");
    auto next=document::PartDocument::create_solid_state_container();next.solid_state.coefficient=1.1;
    require(workspace::commit_solid_state(live,kernel,id,next),"Edited spline could not be straightened again");
    near(state->session.calculated_boundaries().back().volume,revised*1.1);
    std::filesystem::remove(path);
}
}
int main(int argc,char** argv){try {
    if(argc>1&&std::string(argv[1])=="--holes") {native_holes();std::cout<<"Native hole state lifecycle passed\n";return 0;}
    if(argc>1&&std::string(argv[1])=="--fillets") {native_fillets();invalid_fillet_state();std::cout<<"Native Fillet state lifecycle passed\n";return 0;}
    if(argc>1&&std::string(argv[1])=="--spatial-sweeps") {native_spatial_sweeps();std::cout<<"Native 3D and H Sweep state lifecycle passed\n";return 0;}
    if(argc>1&&std::string(argv[1])=="--spline") {native_spline();std::cout<<"Native spline state lifecycle passed\n";return 0;}
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(100,0,120,20));
    const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
    auto source=document::PartDocument::create_revolution_container(sketch.id);
    source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
    workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
    auto* state=live.open_part(id);
    const auto authored=*state->session.document().find_container(source.id);
    const double formed=400*110*std::numbers::pi/2;
    near(state->session.calculated_boundaries().back().volume,formed);
    require(workspace::solid_state_sources(state->session.document(),false)==std::vector<std::string>{source.id},
        "Source picker did not offer the preceding solid");
    require(workspace::solid_state_sources(state->session.document(),true).empty(),"Restore offered an unstraightened source");
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    straight.solid_state.all=false;straight.solid_state.owners={source.id};
    require(workspace::commit_solid_state(live,kernel,id,straight),"Straighten did not commit");
    near(state->session.calculated_boundaries().back().volume,formed*.9);
    const auto revision=state->session.revision(),generation=state->session.data_generation();
    require(!workspace::commit_solid_state(live,kernel,id,*state->session.document().find_container(straight.id)),
        "Unchanged properties created a transaction");
    require(state->session.revision()==revision&&state->session.data_generation()==generation,
        "Unchanged properties published calculation data");
    require(workspace::solid_state_sources(state->session.document(),true)==std::vector<std::string>{source.id},
        "Restore did not offer the straightened source");
    const auto path=std::filesystem::temp_directory_path()/("zima-solid-state-"+id+".prtz");
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> stored;auto loaded=document::PartDocument::load(path,&stored);
    require(*loaded.find_container(straight.id)==*state->session.document().find_container(straight.id),
        "Native save/reopen lost state parameters or identity");
    require(loaded.body_history.active_body_id()==state->session.document().body_history.active_body_id(),
        "Native reopen lost active Body context");
    near(stored.back().volume,formed*.9);
    kernel::OcctKernel cold;const auto regenerated=workspace::calculate_part_with_resolved_references(cold,loaded);
    require(regenerated.back().calculation_errors.empty(),"Cold state regeneration failed");
    near(regenerated.back().volume,formed*.9);
    bool ancestry=false;for(const auto& ref:stored.back().mesh.original_references.triangle_references)
        if(ref.owner_id==straight.id) {ancestry=true;require(kernel::solid_state_parent(ref.semantic_key).has_value(),"Native save lost state ancestry");}
    require(ancestry,"Native save lost state-owned reference geometry");
    require(stored.back().solid_state_reference_views.contains(straight.id),"Native save lost state evaluation packet");
    {
        auto missing=stored.back();missing.solid_state_reference_views.clear();
        bool rejected=false;try {
            static_cast<void>(document::solid_state_reference_views(loaded,missing,workspace::construction_reference_source_geometry(stored)));
        }catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"A successful state silently fell back to authored geometry when its evaluation packet was missing");
    }
    {
        workspace::Workspace attached;attached.add_part(loaded,stored);attached.activate(id);
        const auto& alias=*stored.back().solid_state_reference_views.at(straight.id);
        const auto end=std::ranges::find_if(alias.points,[&](const auto& point){return point.reference.owner_id==source.id&&point.reference.semantic_key.starts_with("end:");});
        require(end!=alias.points.end(),"State packet lost original end-point correspondence");
        auto child_sketch=sketcher::Sketch::create_default();static_cast<void>(child_sketch.add_rectangle(0,0,1,1));
        auto child=document::PartDocument::create_extrusion_container(child_sketch.id);child.extrusion.length_forward=2;
        document::ConstructionReference reference;reference.owner_id=source.id;reference.semantic_key=end->reference.semantic_key;
        const auto cap=std::ranges::find_if(alias.triangle_references,[&](const auto& face){return face.owner_id==source.id&&face.semantic_key.starts_with("end:from:");});
        require(cap!=alias.triangle_references.end(),"State packet lost the source end-cap plane");
        auto front=reference;front.semantic_key=cap->semantic_key;front.orientation_role="front";
        front.orientation_drives_rotation=true;front.orientation_only=true;front.flip=true;
        if(argc>1&&std::string(argv[1])=="--state-owned-reference") {
            // GUI picking offers the persisted topology of the Straighten row,
            // whose ancestry must remain usable through later state changes.
            reference.semantic_key=kernel::solid_state_child_key(reference.owner_id,reference.semantic_key);
            front.semantic_key=kernel::solid_state_child_key(front.owner_id,front.semantic_key);
            reference.owner_id=front.owner_id=straight.id;
        }
        child.placement.references={reference,front};
        workspace::commit_profile(attached,cold,id,child,workspace::ProfileEditMode::Create,child_sketch);
        const auto* attachment=attached.open_part(id)->session.document().find_container(child.id);
        require(attachment->placement.reference_valid&&attachment->placement.references==child.placement.references,
            "Attached child changed reference identity");
        auto expected=child.placement;
        for(auto& ref:expected.references)if(ref.owner_id==straight.id) {
            const auto parent=kernel::solid_state_parent(ref.semantic_key);
            require(parent.has_value(),"GUI-owned reference lost its persisted ancestry");
            ref.owner_id=parent->first;ref.semantic_key=parent->second;
        }
        require(document::resolve_placement(expected,alias),"Cannot resolve the independently calculated state endpoint");
        near(attachment->placement.rotation_x,expected.rotation_x);near(attachment->placement.rotation_y,expected.rotation_y);near(attachment->placement.rotation_z,expected.rotation_z);
        near(attachment->placement.x,end->position.x);near(attachment->placement.y,end->position.y);near(attachment->placement.z,end->position.z);
        auto descendant_sketch=sketcher::Sketch::create_default();static_cast<void>(descendant_sketch.add_rectangle(0,0,1,1));
        auto descendant=document::PartDocument::create_extrusion_container(descendant_sketch.id);descendant.extrusion.length_forward=3;
        reference.owner_id=child.id+":origin";reference.semantic_key="origin:point";descendant.placement.references={reference};
        workspace::commit_profile(attached,cold,id,descendant,workspace::ProfileEditMode::Create,descendant_sketch);
        const auto* settled=attached.open_part(id);const auto& next=settled->session.document().find_container(descendant.id)->placement;
        near(next.x,end->position.x);near(next.y,end->position.y);near(next.z,end->position.z);
        const auto attached_path=std::filesystem::temp_directory_path()/("zima-solid-attachment-"+id+".prtz");
        settled->session.document().save(attached_path,settled->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cache;auto reopened=document::PartDocument::load(attached_path,&cache);
        auto geometry=workspace::construction_reference_source_geometry(cache);
        const auto views=document::solid_state_reference_views(reopened,cache.back(),geometry);
        reopened.resolve_constructions(geometry,views); // Cached native data only; no OCCT.
        near(reopened.find_container(child.id)->placement.z,end->position.z);
        const auto regenerated_attachment=workspace::calculate_part_with_resolved_references(cold,reopened);
        require(regenerated_attachment.back().calculation_errors.empty(),"Cold attached state regeneration failed");
        near(reopened.find_container(descendant.id)->placement.z,end->position.z);
        auto changed=*attached.open_part(id)->session.document().find_container(straight.id);
        changed.solid_state.coefficient=1.1;
        require(workspace::commit_solid_state(attached,cold,id,changed),"Earlier state coefficient did not update attached children");
        const auto* updated=attached.open_part(id);
        const auto& updated_alias=*updated->session.calculated_boundaries().back().solid_state_reference_views.at(straight.id);
        const auto updated_end=std::ranges::find_if(updated_alias.points,[&](const auto& point){return point.reference==end->reference;});
        require(updated_end!=updated_alias.points.end(),"Changed coefficient lost endpoint identity");
        require(std::abs(updated_end->position.z-end->position.z)>1,"Coefficient did not move the actual endpoint");
        for(const auto& owner:{child.id,descendant.id}) {
            const auto& placement=updated->session.document().find_container(owner)->placement;
            near(placement.x,updated_end->position.x);near(placement.y,updated_end->position.y);near(placement.z,updated_end->position.z);
        }
        if(argc>1&&(std::string(argv[1])=="--restore-attached-prefix"||std::string(argv[1])=="--state-owned-reference")) {
            // Earlier authored frames describe their own boundary, not the later restore.
            const auto authored_child=*updated->session.document().find_container(child.id);
            const auto authored_descendant=*updated->session.document().find_container(descendant.id);
            const auto straight_endpoint=updated_end->position;
            std::map<std::string,std::string> anchors;
            const auto& before=updated->session.calculated_boundaries().back().mesh.original_references;
            for(const auto& owner:{child.id,descendant.id}) {
                const auto point=std::ranges::find_if(before.points,[&](const auto& p) {
                    return p.reference.owner_id==owner&&
                        std::hypot(p.position.x-updated_end->position.x,p.position.y-updated_end->position.y,
                            p.position.z-updated_end->position.z)<1e-5;
                });
                require(point!=before.points.end(),"Attached-prefix fixture lacks its exact anchored vertex");
                anchors.emplace(owner,point->reference.semantic_key);
            }
            auto restore_attached=document::PartDocument::create_solid_state_container(true);
            restore_attached.solid_state.all=false;restore_attached.solid_state.owners={source.id};
            if(argc>1&&std::string(argv[1])=="--state-owned-reference") {
                // This exploratory fixture is corner-anchored, not centroid/
                // tangent-continuous, and has a separate Origin descendant.
                // The refined material-transfer scope must reject it atomically.
                // Accepted end-face joins have their own document/GUI suite.
                const auto saved=updated->session.document().serialized();
                const auto revision=updated->session.revision();
                const auto fingerprint=updated->session.calculated_boundaries().back().source_fingerprint;
                bool rejected=false;
                try {static_cast<void>(workspace::commit_solid_state(attached,cold,id,restore_attached));}
                catch(const std::invalid_argument& error) {
                    rejected=std::string(error.what()).find("Solid state replay cannot yet preserve this operation.")!=std::string::npos;
                }
                require(rejected,"Unsupported state-owned descendant chain was silently carried");
                const auto* unchanged=attached.open_part(id);
                require(unchanged->session.document().serialized()==saved&&unchanged->session.revision()==revision&&
                    unchanged->session.calculated_boundaries().back().source_fingerprint==fingerprint,
                    "Rejected state-owned chain changed the document or calculated geometry");
                std::cout<<"Unsupported corner/Origin chain rejected without changing native state\n";
                return 0;
            }
            require(workspace::commit_solid_state(attached,cold,id,restore_attached),
                "Restore of attached prefix did not commit");
            const auto* restored=attached.open_part(id);
            require(*restored->session.document().find_container(child.id)==authored_child&&
                *restored->session.document().find_container(descendant.id)==authored_descendant,
                "Restore rewrote earlier authored attachment frames");
            const auto& target=*restored->session.calculated_boundaries().back().solid_state_reference_views.at(restore_attached.id);
            const auto target_end=std::ranges::find_if(target.points,[&](const auto& p){return p.reference==end->reference;});
            require(target_end!=target.points.end(),"Restore lost the parent's semantic endpoint");
            const auto& original_points=stored.back().mesh.original_references.points;
            const auto original_end=std::ranges::find_if(original_points,[&](const auto& p){return p.reference==end->reference;});
            require(original_end!=original_points.end(),"Restore fixture lost the authored formed endpoint");
            require(std::hypot(target_end->position.x-original_end->position.x,target_end->position.y-original_end->position.y,
                target_end->position.z-original_end->position.z)<1e-5,"Restore did not recover the authored formed endpoint");
            require(std::hypot(target_end->position.x-straight_endpoint.x,target_end->position.y-straight_endpoint.y,
                target_end->position.z-straight_endpoint.z)>1,"Restore fixture did not move the source endpoint");
            for(const auto& [owner,key]:anchors) {
                const auto actual=std::ranges::find_if(target.points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                require(actual!=target.points.end(),"Restore lost an attached child's semantic anchor");
                if(std::hypot(actual->position.x-target_end->position.x,actual->position.y-target_end->position.y,
                    actual->position.z-target_end->position.z)>1e-5)
                    throw std::runtime_error("Restore left an earlier attached feature in the straight frame: "+owner);
            }
            const auto verify_restored=[&](const kernel::BodyResult& result) {
                require(result.calculation_errors.empty(),"Attached restore regeneration reported an error");
                const auto& packet=*result.solid_state_reference_views.at(restore_attached.id);
                for(const auto& [owner,key]:anchors) {
                    const auto actual=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                    require(actual!=packet.points.end(),"Reopened restore lost attached topology identity");
                    require(std::hypot(actual->position.x-original_end->position.x,actual->position.y-original_end->position.y,
                        actual->position.z-original_end->position.z)<1e-5,"Reopened restore moved an attached anchor");
                }
            };
            {
                // Properties of the earlier child must use its straight input,
                // even while the final body has already returned to formed shape.
                const auto revision=restored->session.revision(),generation=restored->session.data_generation();
                const auto input=workspace::placement_edit_geometry(attached,id,child.id);
                const auto point=std::ranges::find_if(input.points,[&](const auto& p){return p.reference==end->reference;});
                require(point!=input.points.end(),"Properties input lost the source endpoint");
                require(std::hypot(point->position.x-straight_endpoint.x,point->position.y-straight_endpoint.y,
                    point->position.z-straight_endpoint.z)<1e-5,"Properties used the final or authored frame instead of the child's boundary");
                const auto draft=workspace::prepare_part_feature_reference(attached,id,child.id,0,authored_child.placement.references.front(),false);
                near(draft.placement.x,straight_endpoint.x);near(draft.placement.y,straight_endpoint.y);near(draft.placement.z,straight_endpoint.z);
                require(restored->session.revision()==revision&&restored->session.data_generation()==generation,
                    "Read-only reference preparation published a calculation or transaction");
                auto rollback=restored->session.calculated_boundaries().back();
                rollback.solid_state_reference_views.erase(restore_attached.id);
                auto damaged=restored->session.document();damaged.find_container(child.id)->placement.references.front().semantic_key="missing-for-repair";
                const auto repair_input=document::solid_state_editor_reference_geometry(damaged,rollback,child.id,rollback.mesh.original_references);
                const auto repair_point=std::ranges::find_if(repair_input.points,[&](const auto& p){return p.reference==end->reference;});
                require(repair_point!=repair_input.points.end(),"Editor rejected repair of a missing stored reference");
                near(repair_point->position.z,straight_endpoint.z);
                rollback.solid_state_reference_views.clear();
                bool missing_packet_rejected=false;
                try {static_cast<void>(document::solid_state_editor_reference_geometry(damaged,rollback,child.id,rollback.mesh.original_references));}
                catch(const std::invalid_argument&){missing_packet_rejected=true;}
                require(missing_packet_rejected,"Editor silently used authored geometry without its required state packet");
            }
            require(workspace::step_part_document_history(attached,id,false),"Attached restore Undo failed");
            require(!attached.open_part(id)->session.document().find_container(restore_attached.id),"Attached restore Undo retained state");
            require(workspace::step_part_document_history(attached,id,true),"Attached restore Redo failed");
            verify_restored(attached.open_part(id)->session.calculated_boundaries().back());
            attached.open_part(id)->session.document().save(attached_path,attached.open_part(id)->session.calculated_boundaries());
            std::vector<kernel::BodyResult> saved_restore;
            auto reopened_restore=document::PartDocument::load(attached_path,&saved_restore);
            verify_restored(saved_restore.back());
            kernel::OcctKernel reopened_kernel;
            const auto recalculated=workspace::calculate_part_with_resolved_references(reopened_kernel,reopened_restore);
            verify_restored(recalculated.back());
            require(*reopened_restore.find_container(child.id)==authored_child&&
                *reopened_restore.find_container(descendant.id)==authored_descendant,
                "Cold restore regeneration rewrote authored attachment frames");
            static_cast<void>(workspace::regenerate_part(attached,reopened_kernel,id));
            verify_restored(attached.open_part(id)->session.calculated_boundaries().back());
            require(*attached.open_part(id)->session.document().find_container(child.id)==authored_child&&
                *attached.open_part(id)->session.document().find_container(descendant.id)==authored_descendant,
                "Explicit workspace Regenerate rewrote earlier authored attachment frames");
            workspace::Workspace attached_family;
            attached_family.add_part(reopened_restore,recalculated);attached_family.activate(id);
            document::FamilyTable variants;variants.columns={"Restored"};
            variants.bindings["Restored"]={"feature",restore_attached.id,{}};
            variants.instances={{"Formed",{{"Restored","yes"}}},{"Straight",{{"Restored","no"}}}};
            static_cast<void>(workspace::set_family_table(attached_family,id,variants));
            for(const auto& name:{"Formed","Straight"}) {
                const auto member=workspace::open_family_instance(attached_family,reopened_kernel,id,name,false);
                const auto* variant=attached_family.open_part(member);
                require(variant&&!variant->session.calculated_boundaries().empty(),"Attached Family variant is missing");
                const auto& body=variant->session.calculated_boundaries().back();
                if(std::string(name)=="Formed")verify_restored(body);
                else for(const auto& [owner,key]:anchors) {
                    const auto& points=body.mesh.original_references.points;
                    const auto actual=std::ranges::find_if(points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                    require(actual!=points.end(),"Straight Family variant lost attached topology");
                    require(std::hypot(actual->position.x-straight_endpoint.x,actual->position.y-straight_endpoint.y,
                        actual->position.z-straight_endpoint.z)<1e-5,"Family state suppression left an attached feature in the formed frame");
                }
            }
            auto revised_source=*attached.open_part(id)->session.document().find_container(source.id);
            revised_source.revolution.angle_degrees=120;
            workspace::commit_profile(attached,reopened_kernel,id,revised_source,workspace::ProfileEditMode::Replace);
            const auto& revised_body=attached.open_part(id)->session.calculated_boundaries().back();
            require(revised_body.calculation_errors.empty(),"Source edit broke the restored attachment chain");
            const auto& revised_packet=*revised_body.solid_state_reference_views.at(restore_attached.id);
            const auto revised_end=std::ranges::find_if(revised_packet.points,[&](const auto& p){return p.reference==end->reference;});
            require(revised_end!=revised_packet.points.end(),"Source edit lost endpoint identity");
            require(std::hypot(revised_end->position.x-original_end->position.x,revised_end->position.y-original_end->position.y,
                revised_end->position.z-original_end->position.z)>1,"Source edit reused its old calculated endpoint");
            for(const auto& [owner,key]:anchors) {
                const auto actual=std::ranges::find_if(revised_packet.points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                require(actual!=revised_packet.points.end(),"Source edit lost attached topology identity");
                require(std::hypot(actual->position.x-revised_end->position.x,actual->position.y-revised_end->position.y,
                    actual->position.z-revised_end->position.z)<1e-5,"Source edit reused stale restored attachment geometry");
            }
            const auto formed_endpoint=revised_end->position;
            const auto before_cycles_child=*attached.open_part(id)->session.document().find_container(child.id);
            const auto before_cycles_descendant=*attached.open_part(id)->session.document().find_container(descendant.id);
            std::string last_state;
            for(double coefficient:{.85,1.2,1.0})for(bool restore_cycle:{false,true}) {
                auto cycle=document::PartDocument::create_solid_state_container(restore_cycle);
                cycle.solid_state.all=false;cycle.solid_state.owners={source.id};
                cycle.solid_state.coefficient=restore_cycle?1.:coefficient;
                require(workspace::commit_solid_state(attached,reopened_kernel,id,cycle),"Repeated attached state did not commit");
                last_state=cycle.id;
                const auto& packet=*attached.open_part(id)->session.calculated_boundaries().back().solid_state_reference_views.at(cycle.id);
                const auto endpoint=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference==end->reference;});
                require(endpoint!=packet.points.end(),"Repeated state lost source endpoint identity");
                if(restore_cycle) {
                    require(std::hypot(endpoint->position.x-formed_endpoint.x,endpoint->position.y-formed_endpoint.y,
                        endpoint->position.z-formed_endpoint.z)<1e-5,"Repeated Restore accumulated endpoint drift");
                } else {
                    const auto start_key=std::string("start:")+end->reference.semantic_key.substr(4);
                    const auto start=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==source.id&&p.reference.semantic_key==start_key;});
                    require(start!=packet.points.end(),"Repeated Straighten lost source start identity");
                    // Independent area-centroid radius 110 mm and authored angle 120 degrees.
                    near(std::hypot(endpoint->position.x-start->position.x,endpoint->position.y-start->position.y,
                        endpoint->position.z-start->position.z),110*2*std::numbers::pi/3*coefficient);
                }
                for(const auto& [owner,key]:anchors) {
                    const auto actual=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                    require(actual!=packet.points.end(),"Repeated state lost descendant anchor identity");
                    require(std::hypot(actual->position.x-endpoint->position.x,actual->position.y-endpoint->position.y,
                        actual->position.z-endpoint->position.z)<1e-5,"Repeated state detached a descendant");
                }
                require(*attached.open_part(id)->session.document().find_container(child.id)==before_cycles_child&&
                    *attached.open_part(id)->session.document().find_container(descendant.id)==before_cycles_descendant,
                    "Repeated states rewrote earlier authored frames");
            }
            attached.open_part(id)->session.document().save(attached_path,attached.open_part(id)->session.calculated_boundaries());
            std::vector<kernel::BodyResult> cycle_cache;
            auto cycle_document=document::PartDocument::load(attached_path,&cycle_cache);
            kernel::OcctKernel cycle_kernel;
            const auto cycle_result=workspace::calculate_part_with_resolved_references(cycle_kernel,cycle_document);
            require(cycle_result.back().calculation_errors.empty(),"Cold repeated-state regeneration failed");
            const auto& cycle_points=cycle_result.back().solid_state_reference_views.at(last_state)->points;
            for(const auto& [owner,key]:anchors) {
                const auto actual=std::ranges::find_if(cycle_points,[&](const auto& p){return p.reference.owner_id==owner&&p.reference.semantic_key==key;});
                require(actual!=cycle_points.end(),"Cold repeated states lost descendant identity");
                require(std::hypot(actual->position.x-formed_endpoint.x,actual->position.y-formed_endpoint.y,
                    actual->position.z-formed_endpoint.z)<1e-5,"Cold repeated states changed descendant geometry");
            }
        }
        std::filesystem::remove(attached_path);
    }
    // Family presence operates on the state operation, preserving the source
    // and the state identity in every independently calculated variant.
    workspace::Workspace family;
    family.add_part(loaded,stored);family.activate(id);
    document::FamilyTable table;table.columns={"Developed"};
    table.bindings["Developed"]={"feature",straight.id,{}};
    table.instances={{"Formed",{{"Developed","no"}}},{"Straight",{{"Developed","yes"}}},{"Inherited",{}}};
    static_cast<void>(workspace::set_family_table(family,id,table));
    for(const auto& name:{"Formed","Straight","Inherited"}) {
        const auto member=workspace::open_family_instance(family,cold,id,name,false);
        const auto* variant=family.open_part(member);
        require(variant&&variant->session.document().find_container(source.id)&&
            variant->session.document().find_container(straight.id),"Family lost source or state identity");
        near(variant->session.calculated_boundaries().back().volume,formed*(std::string(name)=="Formed"?1:.9));
    }
    const auto family_path=std::filesystem::temp_directory_path()/("zima-solid-family-"+id+".prtz");
    const auto* generic=family.open_part(id);
    generic->session.document().save(family_path,generic->session.calculated_boundaries());
    std::vector<kernel::BodyResult> family_cache;
    const auto reopened_family=document::PartDocument::load(family_path,&family_cache);
    const auto formed_id=workspace::open_family_instance(family,cold,id,"Formed",false);
    auto formed_variant=workspace::family_part_source(reopened_family,family_cache,formed_id);
    require(formed_variant.find_container(straight.id)->suppressed,"Saved Family lost state suppression");
    near(workspace::calculate_part_with_resolved_references(cold,formed_variant).back().volume,formed);
    std::filesystem::remove(family_path);
    auto restore=document::PartDocument::create_solid_state_container(true);
    require(workspace::commit_solid_state(live,kernel,id,restore),"Restore did not commit");
    near(state->session.calculated_boundaries().back().volume,formed);
    require(workspace::step_part_document_history(live,id,false),"Undo failed");
    near(state->session.calculated_boundaries().back().volume,formed*.9);
    require(!state->session.document().find_container(restore.id),"Undo retained the new state container");
    require(workspace::step_part_document_history(live,id,true),"Redo failed");
    near(state->session.calculated_boundaries().back().volume,formed);
    require(*state->session.document().find_container(source.id)==authored,"State rewrote the authored source");
    auto edit=*state->session.document().find_container(straight.id);edit.solid_state.coefficient=1.1;
    require(workspace::commit_solid_state(live,kernel,id,edit),"Earlier state edit failed");
    near(state->session.calculated_boundaries().back().volume,formed);
    const auto before=state->session.document().serialized();const auto before_generation=state->session.data_generation();
    edit.solid_state.coefficient=0;
    bool rejected=false;try{static_cast<void>(workspace::commit_solid_state(live,kernel,id,edit));}
    catch(const std::exception&){rejected=true;}
    require(rejected&&state->session.document().serialized()==before&&state->session.data_generation()==before_generation,
        "Failed state edit partially committed the document or calculation");
    state->session.document().save(path,state->session.calculated_boundaries());
    stored.clear();loaded=document::PartDocument::load(path,&stored);
    near(workspace::calculate_part_with_resolved_references(cold,loaded).back().volume,formed);
    auto directory=std::filesystem::temp_directory_path();command_host::Host host(live,kernel,directory);
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        return result;
    };
    const auto command_generation=state->session.data_generation();
    const auto properties=run("straighten.get",{{"container",straight.id}});
    require(properties.data.at("coefficient")==1.1&&properties.data.at("owners")==std::vector<std::string>{source.id},
        "Command read lost native state parameters");
    require(!run("straighten.set",{{"container",straight.id},{"coefficient",1.1}}).data.at("changed").get<bool>() &&
        state->session.data_generation()==command_generation,"Command no-op calculated or committed");
    const auto command_state=run("straighten.create",{{"coefficient",1.05}}).data.at("container").get<std::string>();
    near(state->session.calculated_boundaries().back().volume,formed*1.05);
    require(run("straighten.set",{{"container",command_state},{"coefficient",1.15}}).data.at("changed").get<bool>(),
        "Command coefficient edit did not report its transaction");
    near(state->session.calculated_boundaries().back().volume,formed*1.15);
    const auto restored_command=run("restore_shape.create").data.at("container").get<std::string>();
    near(state->session.calculated_boundaries().back().volume,formed);
    const auto restored_generation=state->session.data_generation(),restored_revision=state->session.revision();
    const auto restored_properties=run("restore_shape.get",{{"container",restored_command}});
    require(restored_properties.data.at("all")==true&&restored_properties.data.at("coefficient")==1.&&
        restored_properties.data.at("container")==restored_command,"Restore command query lost its definition");
    require(!run("restore_shape.set",{{"container",restored_command}}).data.at("changed").get<bool>()&&
        state->session.data_generation()==restored_generation&&state->session.revision()==restored_revision,
        "Restore command query or unchanged confirmation calculated or committed");
    const auto wrong=host.execute({{"command","restore_shape.set"},{"arguments",{{"container",straight.id}}}});
    require(!wrong.ok,"Command changed the kind of an existing state container");
    std::filesystem::remove(path);
    std::cout<<"Native solid states: persistence, cold regeneration, Family, no-op, Undo/Redo, earlier edit and atomic failure passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
