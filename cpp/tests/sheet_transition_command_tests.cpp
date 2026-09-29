#include <zima/workspace/sheet_transition_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <transition_sketches.hpp>
#include <transition_sheet.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <chrono>
#include <iostream>
using namespace zima;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void check_solid(const kernel::BodyResult& body) {
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(body.kernel_shape);BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Transition has invalid B-Rep");
    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    if(solids!=1)throw std::runtime_error("Transition solid count: "+std::to_string(solids));
}
void check_material_preview(document::HistoryContainer feature) {
    using namespace kernel::sheet_material;
    document::reframe_sheet_transition(feature);
    const auto& p=feature.sheet_transition;
    const auto material=[&] {
        const auto a=sketcher::Sketch::from_serialized(p.sketches[0]),b=sketcher::Sketch::from_serialized(p.sketches[1]);
        if(document::rectangular_sheet_transition(feature))
            return research::transition::manufacture(research::transition::read_rectangular_sketches(b,a).model,{p.thickness,p.inside_radius,p.k_factor});
        auto input=research::transition::read_sketches(a,b);input.model.corner_facets={p.facets[0],p.facets[1]};
        return research::transition::manufacture(input.model,{p.thickness,p.inside_radius,p.k_factor});
    }();
    const auto started=std::chrono::steady_clock::now();
    const auto preview=document::sheet_transition_preview(feature);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    check(std::ranges::none_of(preview.edges,[](const auto& e){return e.construction;}),"Half transition exposed construction closure in cyan wire");
    const auto has=[&](kernel::Vec3 expected) {
        return std::ranges::any_of(preview.edges,[&](const auto& edge) {
            return std::ranges::any_of(edge.points,[&](auto actual){const auto d=sub(actual,expected);return std::hypot(d.x,d.y,d.z)<1e-7;});
        });
    };
    for(const auto& panel:material.panels)for(auto outer:panel.outer) {
        check(has(outer),"Preview omitted a finite-radius panel boundary");
        check(has(add(outer,mul(panel.inward,material.thickness))),"Preview omitted inner skin / thickness");
    }
    for(const auto& bend:material.bends)for(const auto* section:{&bend.sections.front(),&bend.sections.back()})
        for(auto point:*section)check(has(point),"Tilted preview omitted an outer/inner bend junction");
    std::cout<<"Material preview "<<p.end_rotation.x<<","<<p.end_rotation.y<<","<<p.end_rotation.z<<": "<<ms<<" ms, "<<preview.edges.size()<<" edges\n";
}
void check_assembly_datums(const kernel::BodyResult& body,const std::string& owner) {
    auto assembly=assembly::AssemblyDocument::create_default();
    assembly::PartOccurrence fixed;fixed.occurrence_id="fixed";fixed.source_document_id="transition";fixed.grounded=true;
    fixed.calculated_source=kernel::BodySnapshot(body);fixed.placement.x=30;
    auto moving=fixed;moving.occurrence_id="moving";moving.grounded=false;moving.placement.x=-25;
    assembly.components={fixed,moving};
    const auto reference=[&](const char* occurrence,assembly::MateReferenceKind kind,const char* key) {
        return assembly::MateReference{kind,assembly::InstancePath{}.child(occurrence),owner,key};
    };
    for(const auto* key:{"axis:start","axis:end"})
        check(assembly.resolve_point(reference("fixed",assembly::MateReferenceKind::Point,key)).status==assembly::MateStatus::Valid,"Assembly cannot resolve transition endpoint");
    check(assembly.resolve_plane(reference("fixed",assembly::MateReferenceKind::Face,"plane:end")).status==assembly::MateStatus::Valid,"Assembly cannot resolve transition end plane");
    assembly.components[1].placement_references={
        {assembly::MateKind::PointCoincident,reference("moving",assembly::MateReferenceKind::Point,"axis:end"),reference("fixed",assembly::MateReferenceKind::Point,"axis:end")},
        {assembly::MateKind::PlaneCoincident,reference("moving",assembly::MateReferenceKind::Face,"plane:end"),reference("fixed",assembly::MateReferenceKind::Face,"plane:end")}};
    assembly.calculate_placement_references();
    const auto moving_point=assembly.resolve_point(reference("moving",assembly::MateReferenceKind::Point,"axis:end")).point;
    const auto fixed_point=assembly.resolve_point(reference("fixed",assembly::MateReferenceKind::Point,"axis:end")).point;
    const auto delta=kernel::sheet_material::sub(moving_point,fixed_point);
    check(std::hypot(delta.x,delta.y,delta.z)<1e-6,"Assembly did not mate transition end datums");
    assembly.components[1].placement_references.back().flip=true;
    assembly.calculate_placement_references();
    const auto a=assembly.resolve_plane(reference("moving",assembly::MateReferenceKind::Face,"plane:end")).plane;
    const auto b=assembly.resolve_plane(reference("fixed",assembly::MateReferenceKind::Face,"plane:end")).plane;
    check(kernel::sheet_material::dot(a.normal,b.normal)<-1+1e-7,"End plane lost opposite-side choice at zero offset");
}
}
int main()try {
    if(const auto* path=std::getenv("ZIMA_VERIFY_TRANSITION_SOURCE")) {
        std::vector<kernel::BodyResult> cached;auto part=document::PartDocument::load(path,&cached);
        const auto found=std::ranges::find(part.history,document::FeatureKind::SheetTransition,&document::HistoryContainer::feature_kind);
        check(found!=part.history.end(),"Saved transition missing");
        auto feature=*found;document::reframe_sheet_transition(feature);
        check_material_preview(feature);
        const auto input=research::transition::read_sketches(sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0]),sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]));
        std::cout<<"Saved transition: radius="<<input.model.radius<<" width="<<input.model.width<<" depth="<<input.model.depth<<" corner="<<input.model.corner_radius<<" thickness="<<feature.sheet_transition.thickness<<std::endl;
        const auto preview=document::sheet_transition_preview(feature);
        std::cout<<"Preview edges="<<preview.edges.size()<<std::endl;
        kernel::OcctKernel kernel;const auto result=kernel.evaluate_history({document::sheet_transition_operation(part,feature)}).back();
        check_solid(result);std::cout<<"Saved transition BRep valid, one solid, volume="<<result.volume<<std::endl;
        if(std::getenv("ZIMA_VERIFY_ENDPOINT_PLANES")) {
            auto model=input.model;
            model.corner_facets={feature.sheet_transition.facets[0],feature.sheet_transition.facets[1]};
            const auto surface=research::transition::calculate(model);
            check(surface.valid() && surface.faces.size()==model.corner_facets[0]+model.corner_facets[1]+3,"Endpoint candidate added diagonal panels");
            const auto& p=feature.sheet_transition;
            for(const double factor:{p.k_factor,.5}) {
            const auto material=research::transition::manufacture(model,{p.thickness,p.inside_radius,factor});
            std::cout<<"No-diagonal candidate: panels="<<material.panels.size()<<" bends="<<material.bends.size()<<std::endl;
            const auto operation=research::transition::sheet_operation(material,"endpoint-candidate");
            const kernel::HistoryOperation flat{"flat",kernel::SheetStateRequest{true,true,{}}};
            const kernel::HistoryOperation restore{"restore",kernel::SheetStateRequest{false,true,{}}};
            const auto states=kernel.evaluate_history({operation,flat,restore});
            for(const auto& body:states) {
                for(const auto& [owner,error]:body.calculation_errors)std::cerr<<owner<<": "<<error<<std::endl;
                check(body.calculation_errors.empty(),"Endpoint candidate calculation failed");
            }
            check_solid(states.back());
            check_solid(kernel.evaluate_history({operation}).back());
            check_solid(kernel.evaluate_history({operation,flat}).back());
            std::cout<<"No-diagonal K="<<factor<<" formed/flat/restored: "<<states[0].volume<<", "<<states[1].volume<<", "<<states[2].volume<<std::endl;
            // Constant-thickness folded/flat volumes agree for the mid-thickness
            // neutral layer. Other K factors intentionally change bend allowance.
            if(factor==.5)check(std::abs(states[0].volume-states[1].volume)<states[0].volume*2e-4,"Endpoint candidate lost material in unfolding");
            check(std::abs(states[0].volume-states[2].volume)<1e-5,"Endpoint candidate did not restore material");
            }
        }
        return 0;
    }
    for(auto angle:std::array<kernel::Vec3,4>{{{0,0,0},{5,0,0},{-5,0,0},{8,-10,0}}}) {
        auto feature=document::create_sheet_transition();feature.sheet_transition.end_rotation=angle;
        feature.sheet_transition.thickness=3;check_material_preview(feature);
    }
    {
        auto part=document::PartDocument::create_default();auto feature=document::create_sheet_transition();
        feature.sheet_transition.end_rotation={8,-10,0};kernel::OcctKernel kernel;
        std::cout<<"Native two-axis half-transition"<<std::endl;
        check_solid(kernel.evaluate_history({document::sheet_transition_operation(part,feature)}).back());
    }
    for(unsigned sides:{2u,3u}) {
        auto rectangular=document::create_sheet_transition(true);
        document::set_rectangular_transition_sides(rectangular,sides);
        const auto preview=document::sheet_transition_preview(rectangular);
        check(std::ranges::none_of(preview.edges,[](const auto& edge){return edge.preview_terminal_dashed||edge.construction;}),
            "Material preview retained the dashed rectangular envelope");
        const auto parsed=research::transition::read_rectangular_sketches(sketcher::Sketch::from_serialized(rectangular.sheet_transition.sketches[1]),sketcher::Sketch::from_serialized(rectangular.sheet_transition.sketches[0]));
        check(parsed.model.sides==sides&&parsed.model.width[0]==200&&parsed.model.width[1]==140,"Rectangular profile interpretation changed its sides or dimensions");
        auto doc=document::PartDocument::create_default();static_cast<void>(doc.body_history.create_body("Rectangular transition"));
        auto alternative=rectangular;document::set_rectangular_transition_sides(alternative,sides==2?3:2);
        const auto original_operation=document::sheet_transition_operation(doc,rectangular);
        const auto changed_operation=document::sheet_transition_operation(doc,alternative);
        const auto& original_group=std::get<kernel::FeatureGroupRequest>(original_operation.primitive);
        const auto& changed_group=std::get<kernel::FeatureGroupRequest>(changed_operation.primitive);
        const auto& original_panel=std::get<kernel::ExtrusionRequest>(original_group.children.front());
        const auto& changed_panel=std::get<kernel::ExtrusionRequest>(changed_group.children.front());
        check(original_panel.profile_region_id==changed_panel.profile_region_id&&original_panel.outer_edge_source_ids==changed_panel.outer_edge_source_ids,"L/U renamed an unchanged wall's source ancestry");
        rectangular.sheet_transition.end_rotation={8,-10,12};
        rectangular.sheet_transition.thickness=3;check_material_preview(rectangular);
        workspace::Workspace test;kernel::OcctKernel geometry;const auto document_id=doc.document_id;
        std::cout<<"Rotated rectangular transition, sides "<<sides<<std::endl;
        check_solid(geometry.evaluate_history({document::sheet_transition_operation(doc,rectangular)}).back());
        std::cout<<"Committing rotated rectangular transition"<<std::endl;
        test.add_part(doc,workspace::calculate_part_with_resolved_references(geometry,doc));test.activate(document_id);
        check(workspace::commit_sheet_transition(test,geometry,document_id,rectangular),"Rectangular transition failed to commit");
        auto* state=test.open_part(document_id);check_solid(state->session.calculated_boundaries().back());
        check_assembly_datums(state->session.calculated_boundaries().back(),rectangular.id);
        const auto committed=state->session.document().serialized();
        auto loaded=document::PartDocument::from_serialized(committed);
        check(document::rectangular_sheet_transition(*loaded.find_container(rectangular.id)),"Rectangular transition lost its profile mode");
        check(loaded.find_container(rectangular.id)->sheet_transition.end_rotation==rectangular.sheet_transition.end_rotation,"Rectangular transition lost its three-axis rotation");
        check_solid(workspace::calculate_part_with_resolved_references(geometry,loaded).back());
        check(workspace::step_part_document_history(test,document_id,false),"Rectangular Undo failed");
        check(workspace::step_part_document_history(test,document_id,true),"Rectangular Redo failed");
        check(state->session.document().serialized()==committed,"Rectangular Redo changed authored data");
        const double volume=state->session.calculated_boundaries().back().volume;
        for(bool unfold:{true,false}) {
            auto change=document::PartDocument::create_sketch_container();
            change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            change.sheet_state.all=true;
            check(workspace::commit_sheet_state(test,geometry,document_id,change),"Rectangular state change failed");
            const auto& result=state->session.calculated_boundaries().back();check_solid(result);
            check(std::abs(result.volume-volume)<(unfold?volume*2e-4:1e-5),"Rectangular state changed material volume");
        }
        const auto file=std::filesystem::path("build/transition-model")/("native-rectangular-"+std::to_string(sides)+".prtz");
        std::filesystem::create_directories(file.parent_path());
        state->session.document().save(file,state->session.calculated_boundaries());
        auto reopened=document::PartDocument::load(file);kernel::OcctKernel cold;
        const auto recalculated=workspace::calculate_part_with_resolved_references(cold,reopened);
        check_solid(recalculated.back());
        check(std::abs(recalculated.back().volume-volume)<1e-5,"Cold rectangular state restoration changed volume");
    }
    auto part=document::PartDocument::create_default();
    static_cast<void>(part.body_history.create_body("Transition test"));
    kernel::OcctKernel kernel;workspace::Workspace live;const auto id=part.document_id;
    live.add_part(part,workspace::calculate_part_with_resolved_references(kernel,part));live.activate(id);
    auto feature=document::create_sheet_transition();feature.name="Transition";
    {
        auto rectangle=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]);
        const auto height=std::ranges::find_if(rectangle.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::DistanceY;});
        check(height!=rectangle.dimensions.end()&&height->value==80,"Rectangle height uses trimmed leg instead of overall envelope");
        const auto preview=rectangle.viewer_mesh();
        check(std::ranges::any_of(preview.dimensions,[&](const auto& d){return d.reference.semantic_key=="dimension:"+height->id&&std::abs(d.value-80)<1e-8;}),"Overall height annotation disappeared from trimmed profile");
        check(rectangle.set_dimension_value(height->id,90),"Overall rectangle height is not editable");
        const auto radius=std::ranges::find_if(rectangle.corner_radii,[](const auto& c){return c.dimension_visible;});
        check(radius!=rectangle.corner_radii.end(),"Corner radius annotation is missing");
        static_cast<void>(rectangle.add_corner_fillet(radius->first_segment_id,radius->second_segment_id,18));
        check(std::ranges::all_of(rectangle.corner_radii,[](const auto& c){return std::abs(c.radius-18)<1e-8;}),"Corner radii did not stay equal");
        const auto parsed=research::transition::read_sketches(sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0]),rectangle);
        check(std::abs(parsed.model.depth-180)<1e-8&&std::abs(parsed.model.corner_radius-18)<1e-8,"Envelope edit did not reach transition input");
    }
    const auto start=std::chrono::steady_clock::now();
    check(workspace::commit_sheet_transition(live,kernel,id,feature),"Creation did not commit");
    auto* state=live.open_part(id);const auto created=state->session.document().serialized();
    for(bool rectangular:{false,true}) {
        bool rejected=false;
        try{static_cast<void>(workspace::commit_sheet_transition(live,kernel,id,document::create_sheet_transition(rectangular)));}
        catch(const std::invalid_argument& e){rejected=std::string_view(e.what())=="A Part can contain only one sheet transition.";}
        check(rejected&&state->session.document().serialized()==created,"Second transition was not rejected atomically");
    }
    const auto volume=state->session.calculated_boundaries().back().volume;check(volume>1000,"Missing transition solid");
    {
        auto multiple_bodies=state->session.document();
        static_cast<void>(multiple_bodies.body_history.create_body("Second Body"));
        multiple_bodies.find_container(feature.id)->suppressed=true;
        workspace::Workspace other;other.add_part(multiple_bodies);other.activate(id);
        bool blocked=false;
        try{static_cast<void>(workspace::commit_sheet_transition(other,kernel,id,document::create_sheet_transition(true)));}
        catch(const std::invalid_argument& e){blocked=std::string_view(e.what())=="A Part can contain only one sheet transition.";}
        check(blocked&&other.open_part(id)->session.document().serialized()==multiple_bodies.serialized(),
            "Another Body or suppression bypassed the Part transition limit");
    }
    check(!workspace::commit_sheet_transition(live,kernel,id,feature),"Unchanged OK created an Undo transaction");
    check(workspace::step_part_document_history(live,id,false),"Undo failed");
    check(!state->session.document().find_container(feature.id),"Undo retained feature");
    check(workspace::step_part_document_history(live,id,true),"Redo failed");
    check(state->session.document().serialized()==created,"Redo changed feature");
    auto invalid=feature;invalid.sheet_transition.inside_radius=10000;
    bool rejected=false;try{static_cast<void>(workspace::commit_sheet_transition(live,kernel,id,invalid));}catch(const std::exception&){rejected=true;}
    check(rejected&&state->session.document().serialized()==created,"Invalid edit mutated document");
    auto edited=feature;edited.sheet_transition.facets={6,3};edited.sheet_transition.thickness=1.2;
    edited.sheet_transition.inside_radius=1.4;edited.sheet_transition.k_factor=.4;
    check(workspace::commit_sheet_transition(live,kernel,id,edited),"Parameter edit did not commit");
    check(state->session.document().find_container(feature.id)->sheet_transition==edited.sheet_transition,"Parameter edit lost values");
    check(state->session.calculated_boundaries().back().volume>volume,"Thickness edit did not change solid");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Edit Undo failed");
    auto moved=feature;moved.sheet_transition.end_position.z=200;
    check(workspace::commit_sheet_transition(live,kernel,id,moved),"Second Origin movement did not commit");
    check(state->session.calculated_boundaries().back().calculation_errors.empty()&&state->session.calculated_boundaries().back().volume>volume,"Second Origin movement did not regenerate transition");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Origin edit Undo failed");
    auto placed=feature;placed.placement.x=23;placed.placement.y=-11;placed.placement.z=8;
    placed.placement.rotation_x=17;placed.placement.rotation_y=-13;placed.placement.rotation_z=27;
    placed.placement.absolute_rotation_x=17;placed.placement.absolute_rotation_y=-13;placed.placement.absolute_rotation_z=27;
    check(workspace::commit_sheet_transition(live,kernel,id,placed),"Rigid placement did not commit");
    check(std::abs(state->session.calculated_boundaries().back().volume-volume)<1e-4,"Rigid placement changed volume");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Rigid placement Undo failed");
    for(const auto rotation:std::array<kernel::Vec3,3>{{{0,-15,0},{0,15,0},{8,-10,0}}}) {
        auto rotated=feature;rotated.sheet_transition.end_rotation=rotation;
        std::cout<<"Manufacturing relative tilt "<<rotation.x<<','<<rotation.y<<','<<rotation.z<<std::endl;
        check(workspace::commit_sheet_transition(live,kernel,id,rotated),"Relative rotated solid failed");
        check_solid(state->session.calculated_boundaries().back());
        const auto rotated_volume=state->session.calculated_boundaries().back().volume;
        for(bool unfold:{true,false}) {
            auto change=document::PartDocument::create_sketch_container();change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            change.sheet_state.all=true;
            check(workspace::commit_sheet_state(live,kernel,id,change),"Rotated transition state change failed");
            const auto& result=state->session.calculated_boundaries().back();check_solid(result);
            // Tilted finite-radius lofts and flat reconstruction are approximate.
            // Bound the flat volume error to 0.02%; Bend Back restores the
            // original material and must agree to numerical roundoff.
            if(std::abs(result.volume-rotated_volume)>=(unfold?rotated_volume*2e-4:1e-6))
                throw std::runtime_error(std::string(unfold?"Unbend":"Bend Back")+" changed rotated transition volume from "+std::to_string(rotated_volume)+" to "+std::to_string(result.volume));
        }
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,false),"Rotated state Undo failed");
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Rotated solid has errors");
        check(workspace::step_part_document_history(live,id,false),"Rotated solid Undo failed");
    }
    const auto directory=std::filesystem::absolute("build/transition-model");std::filesystem::create_directories(directory);
    const auto path=directory/"native-transition.prtz";
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(path,&cache);
    check(loaded.find_container(feature.id)->sheet_transition==feature.sheet_transition,"Native file lost parameters");
    kernel::OcctKernel cold;const auto recalculated=workspace::calculate_part_with_resolved_references(cold,loaded);
    check(!recalculated.empty()&&recalculated.back().calculation_errors.empty(),"Cold regeneration failed");
    check(std::abs(recalculated.back().volume-volume)<1e-5,"Cold regeneration changed volume");
    for(const kernel::BodyResult* result:{static_cast<const kernel::BodyResult*>(&cache.back()),&recalculated.back()}) {
        check_assembly_datums(*result,feature.id);
        for(const auto& expected:document::sheet_transition_axis_points(*loaded.find_container(feature.id))) {
            const auto& points=result->mesh.original_references.points;
            const auto found=std::ranges::find(points,expected.reference,&kernel::ViewerPoint::reference);
            check(found!=points.end(),"Native calculation lost an axis endpoint");
            const auto delta=kernel::sheet_material::sub(found->position,expected.position);
            check(std::hypot(delta.x,delta.y,delta.z)<1e-8,"Native endpoint moved away from its profile centre");
        }
        const auto& axes=result->mesh.original_references.axes;
        const auto axis=std::ranges::find_if(axes,[&](const auto& a){return a.reference.owner_id==feature.id&&a.reference.semantic_key=="axis:primary";});
        check(axis!=axes.end(),"Transition centre axis was not persisted or regenerated");
        check(std::abs(axis->point.z-75)<1e-8&&std::abs(axis->direction.z-1)<1e-8,
            "Transition axis does not connect the two profile centres");
        check(std::ranges::any_of(result->mesh.axes,[&](const auto& a){return a.reference==axis->reference;}),
            "Transition centre axis is missing from ordinary display");
    }
    {
        auto shown=loaded;shown.find_container(feature.id)->origin_point_visible=true;
        const auto count=[&](const auto& mesh){return std::ranges::count_if(mesh.points,[&](const auto& point){return point.reference.owner_id==feature.id&&(point.reference.semantic_key=="axis:start"||point.reference.semantic_key=="axis:end")&&point.always_visible;});};
        check(count(shown.construction_viewer_mesh())==2,"Point checkbox did not show both endpoints");
        shown.find_container(feature.id)->origin_point_visible=false;
        check(count(shown.construction_viewer_mesh())==0,"Point checkbox did not hide both endpoints");
    }
    {
        auto shifted=placed;shifted.sheet_transition.end_position={0,0,180};
        shifted.sheet_transition.end_rotation={0,15,0};
        const auto operation=document::sheet_transition_operation(loaded,shifted);
        const auto& axes=std::get<kernel::FeatureGroupRequest>(operation.primitive).axes;
        const auto axis=std::ranges::find_if(axes,[](const auto& a){return a.reference.semantic_key=="axis:primary";});
        check(axis!=axes.end()&&std::abs(axis->display_length-182)<1e-8,
            "Offset/rotated transition centre axis has the wrong span");
    }
    {
        const auto origins=loaded.history_origin_reference_geometry_before({});
        for(const auto& owner:{feature.container_origin.id,feature.sheet_transition.end_origin_id}) {
            check(std::ranges::count_if(origins.axes,[&](const auto& a){return a.reference.owner_id==owner;})==3,"Transition Origin is missing its three reference axes");
            document::Placement attachment;
            attachment.references={{{},owner,"origin:point"},{{},owner,"origin:plane:xy",0,false,"front",true},{{},owner,"origin:plane:yz",0,false,"top",true}};
            check(document::resolve_placement(attachment,origins),"Whole transition Origin cannot resolve a downstream placement");
            check(std::abs(attachment.z-(owner==feature.container_origin.id?0.:150.))<1e-8,"Transition Origin resolved to the wrong profile");
        }
    }
    // A real downstream Bend consumes the same original edge/face/point packet
    // as the interactive sheet commands; display-only edges are insufficient.
    {
        const auto& geometry=recalculated.back().mesh.original_references;
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& candidate){
            if(candidate.reference.owner_id!=feature.id||candidate.reference.semantic_key.find("rectangle-rim")==std::string::npos)return false;
            try {return document::bend_sheet_references(candidate).size()==3;}catch(const std::exception&){return false;}
        });
        check(edge!=geometry.edges.end(),"Transition has no attachable original rectangle rim");
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        bend.bend.sheet_attachment=true;bend.bend.radius=3;bend.bend.angle_degrees=30;
        bend.placement.references=document::bend_sheet_references(*edge);
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=bend.id;bend.bend.sketch_id=outline.id;
        document::initialize_bend_start_profile(outline,*edge->measured_length);
        check(workspace::commit_bend(live,kernel,id,bend,outline),"Bend attachment to transition did not commit");
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Attached Bend failed calculation");
        auto changed=feature;changed.sheet_transition.end_position.z=175;
        check(workspace::commit_sheet_transition(live,kernel,id,changed),"Transition edit with attached Bend failed");
        check(state->session.document().find_container(bend.id)->placement.reference_valid,"Transition edit invalidated Bend references");
        const auto attached_path=directory/"native-transition-attached.prtz";
        state->session.document().save(attached_path,state->session.calculated_boundaries());
        auto attached=document::PartDocument::load(attached_path);
        kernel::OcctKernel attachment_kernel;
        const auto rebuilt=workspace::calculate_part_with_resolved_references(attachment_kernel,attached);
        check(rebuilt.back().calculation_errors.empty()&&attached.find_container(bend.id)->placement.reference_valid,"Attached Bend failed native save/reopen/regeneration");
        check(attached.find_container(bend.id)->placement.references==bend.placement.references,"Native file changed original attachment identity");
        const auto attached_volume=state->session.calculated_boundaries().back().volume;
        for(bool unfold:{true,false}) {
            auto change=document::PartDocument::create_sketch_container();change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;change.sheet_state.all=true;
            check(workspace::commit_sheet_state(live,kernel,id,change),"Attached transition state change failed");
            const auto& result=state->session.calculated_boundaries().back();check_solid(result);
            check(std::abs(result.volume-attached_volume)<(unfold?attached_volume*2e-4:1e-6),"Attached transition state lost material");
        }
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,false),"Attached state Undo failed");
        std::cout<<"Original transition edge/face/point attachment: create, edit, save/reopen and cold regeneration passed\n";
        check(workspace::step_part_document_history(live,id,false),"Attached source edit Undo failed");
        check(workspace::step_part_document_history(live,id,false)&&!state->session.document().find_container(bend.id),"Bend attachment Undo failed");
    }
    const auto dxf=workspace::prepare_sheet_dxf(state->session.document(),state->session.calculated_boundaries(),kernel);
    check(!dxf.contour.segments.empty()||!dxf.contour.bsplines.empty(),"DXF has no flat contour");
    for(bool unfold:{true,false}) {
        auto operation=document::PartDocument::create_sketch_container();
        operation.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
        operation.sheet_state.all=false;operation.sheet_state.owners={feature.id};
        check(workspace::commit_sheet_state(live,kernel,id,operation),"State command did not commit");
        check(std::abs(state->session.calculated_boundaries().back().volume-volume)<volume*1e-4,"State command changed volume");
        std::size_t axes=0;
        for(const auto& source:workspace::drawing_annotation_sources(&live,id,{}))for(const auto& axis:source.axes)
            if(kernel::sheet_material::is_bend_line(axis.reference))++axes;
        check(unfold?axes>=6:axes==0,"Drawing offers wrong bend axes for material state");
    }
    std::cout<<"Native transition create/edit/save/reopen/Undo/Redo/state: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
