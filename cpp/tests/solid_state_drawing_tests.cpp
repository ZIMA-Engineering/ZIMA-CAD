#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing/view_breaks.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/command_host/host.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <cstdlib>

using namespace zima;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(double actual,double expected) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>1e-5)
        throw std::runtime_error("Drawing state value: expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
double height(const drawing::DrawingView& view) {
    double low=1e100,high=-1e100;
    for(const auto& triangle:view.projected_triangles)for(const auto& p:triangle.points) {
        low=std::min(low,p.y);high=std::max(high,p.y);
    }
    return high-low;
}
double dimension_value(const drawing::DrawingDocument& document,const std::string& view) {
    const auto& dimension=document.sheets.front().dimensions.front();
    const auto result=drawing::evaluate_drawing_dimension(*document.find_view(view),dimension);
    require(result.state!=drawing::MeasurementState::Unresolved&&result.direction_resolved&&
        std::ranges::all_of(result.resolved_attachments,[](bool resolved){return resolved;})&&
        !result.presentations.empty(),"State dimension lost its live reference resolution");
    return result.presentations.front().value;
}
}
int main(int argc,char** argv){try {
    const bool curved_chain=argc==2&&std::string(argv[1])=="--curved-chain";
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto part=document::PartDocument::create_default();
    if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=root/("zima-state-drawing-"+part.document_id);std::filesystem::create_directory(directory);
    const auto part_path=directory/"source.prtz",drawing_path=directory/"views.drwz";
    const auto id=part.document_id;live.add_part(part,{},part_path);live.activate(id);
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(100,0,120,20));
    const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
    auto source=document::PartDocument::create_revolution_container(sketch.id);
    source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
    workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
    std::string dimension_source=source.id;
    if(curved_chain) {
        const auto plan=kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(
            live.open_part(id)->session.document().kernel_operations().front().primitive),1.);
        const auto& geometry=live.open_part(id)->session.calculated_boundaries().back().mesh.original_references;
        const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref) {
            return ref.owner_id==source.id&&ref.semantic_key.starts_with("end:from:");
        });
        require(face!=geometry.triangle_references.end(),"Drawing chain lacks its joining cap");
        auto contour=sketcher::Sketch::create_default();static_cast<void>(contour.add_rectangle(-1,-1.5,1,1.5));
        const auto axis=contour.add_segment(-20,-50,-20,50,true);contour.set_segment_centerline(axis,true);
        auto next=document::PartDocument::create_revolution_container(contour.id);
        next.revolution.axis_segment_id=axis;next.revolution.angle_degrees=next.revolution.angle_reverse=90;
        next.placement.x=plan.source_end_centroid.x;next.placement.y=plan.source_end_centroid.y;next.placement.z=plan.source_end_centroid.z;
        document::ConstructionReference position;position.owner_id=source.id;position.semantic_key=face->semantic_key;
        position.supports_offset=true;position.offset=-0.;
        auto top=position;top.orientation_only=true;top.orientation_drives_rotation=true;top.orientation_role="top";top.supports_offset=false;
        next.placement.references={position,top};
        workspace::commit_profile(live,kernel,id,next,workspace::ProfileEditMode::Create,contour);
        const auto child_plan=kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(
            live.open_part(id)->session.document().kernel_operations().back().primitive),1.);
        const auto& f=plan.end_transform;const auto t=plan.start_tangent;
        const kernel::Vec3 end_tangent{f[1].x*t.x+f[2].x*t.y+f[3].x*t.z,
            f[1].y*t.x+f[2].y*t.y+f[3].y*t.z,f[1].z*t.x+f[2].z*t.y+f[3].z*t.z};
        if(child_plan.start_tangent.x*end_tangent.x+child_plan.start_tangent.y*end_tangent.y+child_plan.start_tangent.z*end_tangent.z<0) {
            next=*live.open_part(id)->session.document().find_container(next.id);next.revolution.direction=document::ExtrusionDirection::Reverse;
            workspace::commit_profile(live,kernel,id,next,workspace::ProfileEditMode::Replace,contour);
        }
        dimension_source=next.id;
    }
    double formed_low=1e100,formed_high=-1e100;
    for(const auto& p:live.open_part(id)->session.calculated_boundaries().back().mesh.vertices) {
        formed_low=std::min(formed_low,p.z);formed_high=std::max(formed_high,p.z);
    }
    const double formed_height=formed_high-formed_low;
    if(!curved_chain)near(formed_height,120.);
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    require(workspace::commit_solid_state(live,kernel,id,straight),"Drawing fixture Straighten failed");
    auto* state=live.open_part(id);state->session.document().save(part_path,state->session.calculated_boundaries());
    const double length=(curved_chain?130.:110.)*std::numbers::pi/2;
    const double measured_length=(curved_chain?20.:110.)*std::numbers::pi/2;
    auto drawing_document=drawing::DrawingDocument::create_default();const auto drawing_id=drawing_document.document_id,sheet=drawing_document.sheets.front().id;
    live.add_drawing(drawing_document,drawing_path);live.activate(drawing_id);live.display_top_level(drawing_id);
    auto working_directory=directory;command_host::Host host(live,kernel,working_directory);
    const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
        const auto result=host.execute({{"command",command},{"arguments",std::move(arguments)}});
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        return result;
    };
    live.activate(id);live.display_top_level(id);
    const auto section_result=run("section.create",{{"document",id},{"plane","XY"},
        {"path_mm",{{0,10},{200,10}}},{"name","State section"},{"show_cut",false}});
    require(!section_result.data.at("body_calculated").get<bool>(),"Section creation recalculated its source solid");
    const auto section_id=section_result.data.at("object").get<std::string>();
    live.activate(drawing_id);live.display_top_level(drawing_id);
    state=live.open_part(id);state->session.document().save(part_path,state->session.calculated_boundaries());
    const auto generation=live.open_part(id)->session.data_generation();
    const auto parent=run("drawing.view.create",{{"sheet",sheet},{"source",id},{"scale",1}}).data.at("view").get<std::string>();
    const auto child=run("drawing.view.create",{{"sheet",sheet},{"parent_view",parent},{"projection_direction","right"},{"distance_mm",30}}).data.at("view").get<std::string>();
    const auto section_view=run("drawing.view.create",{{"sheet",sheet},{"source",id},{"scale",1},{"section",section_id}}).data.at("view").get<std::string>();
    const auto verify_section=[&](const drawing::DrawingDocument& document,double expected) {
        const auto* view=document.find_view(section_view);
        require(view&&view->source_document_id==id&&view->section_id==section_id&&
            view->section_snapshot&&view->section_snapshot->id==section_id,
            "Section view lost its source or persisted section identity");
        near(height(*view),expected);
        auto output=*view;drawing::prepare_output_view(output);
        require(std::ranges::any_of(output.projected_edges,[](const auto& edge){return edge.hatch&&!edge.points.empty();}),
            "State section output contains no cut-face hatching");
    };
    auto* drawing_state=live.open_drawing(drawing_id);near(height(*drawing_state->document().find_view(parent)),length*.9);
    verify_section(drawing_state->document(),length*.9);
    require(live.open_part(id)->session.data_generation()==generation,"Drawing creation changed the calculated Part");
    auto dimensioned=drawing_state->document();const auto& view=*dimensioned.find_view(parent);
    require(view.measurement_geometry!=nullptr,"State view lacks measuring geometry");
    const auto curve=std::ranges::find_if(view.measurement_geometry->curves,[&](const auto& c) {
        const auto parent=kernel::solid_state_parent(c.source.semantic_key);
        return c.source.owner_id==straight.id&&parent&&parent->first==dimension_source&&
            c.points.size()>1&&std::abs(std::abs(c.points.back().z-c.points.front().z)-measured_length*.9)<1e-5;
    });
    require(curve!=view.measurement_geometry->curves.end(),"State view lacks a persisted longitudinal measuring curve");
    auto dimension=drawing::make_drawing_dimension(parent);dimension.direction=drawing::DimensionDirection::Vertical;
    dimension.attachments={{drawing::DimensionAttachmentKind::CurvePoint,curve->source,{},0},
        {drawing::DimensionAttachmentKind::CurvePoint,curve->source,{},1}};
    drawing::refresh_drawing_dimension(view,dimension);dimensioned.sheets.front().dimensions={dimension};drawing_state->commit(dimensioned);
    near(dimension_value(drawing_state->document(),parent),measured_length*.9);
    // A detail must follow the regenerated parent, retaining its local crop
    // and paper-scale break gap without changing model-space measurements.
    auto detailed=drawing_state->document();
    auto broken=*detailed.find_view(parent);
    double bottom=1e100,top=-1e100;
    for(const auto& triangle:broken.projected_triangles)for(const auto& point:triangle.points) {
        bottom=std::min(bottom,point.y);top=std::max(top,point.y);
    }
    broken.breaks={{"state-break",true,(bottom+top)/2-10,20,3,drawing::BreakMark::Zigzag}};
    workspace::DrawingProjection projection(&live,drawing_path);
    workspace::edit_drawing_view(detailed,sheet,broken,false,projection);
    auto detail=drawing::DrawingDocument::create_view(id,part_path,live.open_part(id)->session.calculated_boundaries().back().mesh,drawing::ViewOrientation::Front);
    detail.name="State detail";detail.detail_view=true;detail.parent_view_id=parent;detail.use_sheet_scale=false;detail.scale=2;
    double left=1e100,right=-1e100;
    for(const auto& triangle:broken.projected_triangles)for(const auto& point:triangle.points) {
        left=std::min(left,point.x);right=std::max(right,point.x);
    }
    detail.crop=drawing::ViewCrop{drawing::ViewCropShape::Circle,drawing::break_map(broken,{(left+right)/2,top-5}),{{8,8}}};
    const auto detail_id=detail.id;const auto crop=*detail.crop;
    workspace::edit_drawing_view(detailed,sheet,detail,true,projection);drawing_state->commit(detailed);
    const auto verify_detail=[&](const drawing::DrawingDocument& document,double expected) {
        const auto* current=document.find_view(detail_id);const auto* source_view=document.find_view(parent);
        require(current&&source_view&&current->detail_view&&current->parent_view_id==parent,
            "Regeneration lost the detail hierarchy");
        require(current->crop&&current->crop->shape==crop.shape&&current->crop->anchor==crop.anchor&&
            current->crop->points==crop.points,"Regeneration changed the detail crop");
        require(current->breaks.size()==1&&current->breaks.front().id=="state-break",
            "Regeneration lost the inherited break");
        near(current->breaks.front().gap,6);near(height(*current),expected);
        auto output=*current;drawing::prepare_output_view(output);
        require(!output.projected_edges.empty()&&output.measurement_geometry,
            "Detail output lost its actual geometry or measurement references");
        const auto edges=drawing::broken_edges(output);double low=1e100,high=-1e100;
        for(const auto& edge:edges)for(const auto& point:edge.points) {
            low=std::min(low,point.y);high=std::max(high,point.y);
        }
        near(high-low,expected-17);
        near(dimension_value(document,parent),expected*measured_length/length);
    };
    verify_detail(drawing_state->document(),length*.9);
    auto changed=*live.open_part(id)->session.document().find_container(straight.id);changed.solid_state.coefficient=1.1;
    require(workspace::commit_solid_state(live,kernel,id,changed),"Drawing source coefficient edit failed");
    const auto edited_generation=live.open_part(id)->session.data_generation();
    near(height(*drawing_state->document().find_view(parent)),length*.9);
    run("regenerate");near(height(*drawing_state->document().find_view(parent)),length*1.1);
    near(dimension_value(drawing_state->document(),parent),measured_length*1.1);
    verify_detail(drawing_state->document(),length*1.1);
    verify_section(drawing_state->document(),length*1.1);
    require(drawing_state->document().sheets.front().dimensions.front().attachments==dimension.attachments,
        "Drawing regeneration replaced the state dimension identity");
    require(live.open_part(id)->session.data_generation()==edited_generation,"Drawing regeneration recalculated its Part");
    require(drawing_state->document().find_view(child)->parent_view_id==parent,"Projected state view lost its parent");
    run("undo");near(dimension_value(drawing_state->document(),parent),measured_length*.9);
    run("redo");near(dimension_value(drawing_state->document(),parent),measured_length*1.1);
    run("save");auto reopened=drawing::DrawingDocument::load(drawing_path);
    near(dimension_value(reopened,parent),measured_length*1.1);
    state=live.open_part(id);state->session.document().save(part_path,state->session.calculated_boundaries());
    require(workspace::regenerate_drawing_views(reopened,nullptr,drawing_path)==4,"Disk-only drawing regeneration lost a state view");
    verify_section(reopened,length*1.1);
    verify_detail(reopened,length*1.1);
    near(height(*reopened.find_view(parent)),length*1.1);near(dimension_value(reopened,parent),measured_length*1.1);
    if(const char* destination=std::getenv("ZIMA_SOLID_STATE_DRAWING_FIXTURE")) {
        const std::filesystem::path output=destination;std::filesystem::create_directories(output);
        const auto source_path=output/"source.prtz";
        state->session.document().save(source_path,state->session.calculated_boundaries());
        auto fixture=reopened;fixture.source_path=source_path;
        for(auto& sheet:fixture.sheets)for(std::size_t index=0;index<sheet.views.size();++index) {
            auto& view=sheet.views[index];view.source_path=source_path;
            double x0=1e100,x1=-1e100,y0=1e100,y1=-1e100;
            for(const auto& triangle:view.projected_triangles)for(auto point:triangle.points) {
                point=drawing::break_map(view,point);
                x0=std::min(x0,point.x);x1=std::max(x1,point.x);
                y0=std::min(y0,point.y);y1=std::max(y1,point.y);
            }
            const auto center=view.crop?view.crop->anchor:drawing::Point2{(x0+x1)/2,(y0+y1)/2};
            view.x=sheet.width_mm()-(30+50*index)+center.x*view.scale;
            view.y=sheet.height_mm()/2-center.y*view.scale;
        }
        fixture.save(output/"views.drwz");
    }
    auto restore=document::PartDocument::create_solid_state_container(true);
    require(workspace::commit_solid_state(live,kernel,id,restore),"Drawing source Restore failed");
    run("regenerate");near(height(*drawing_state->document().find_view(parent)),formed_height);
    verify_section(drawing_state->document(),formed_height);
    state=live.open_part(id);state->session.document().save(part_path,state->session.calculated_boundaries());
    run("save");reopened=drawing::DrawingDocument::load(drawing_path);
    static_cast<void>(workspace::regenerate_drawing_views(reopened,nullptr,drawing_path));near(height(*reopened.find_view(parent)),formed_height);
    document::FamilyTable family;family.columns={"Restored"};family.bindings["Restored"]={"feature",restore.id,{}};
    family.instances={{"Formed",{{"Restored","yes"}}},{"Developed",{{"Restored","no"}}}};
    static_cast<void>(workspace::set_family_table(live,id,family));
    std::vector<std::pair<std::string,double>> family_views;
    for(const auto& name:{"Formed","Developed"}) {
        const auto member=workspace::open_family_instance(live,kernel,id,name,false);
        const auto variant_generation=live.open_part(member)->session.data_generation();
        const auto family_view=run("drawing.view.create",{{"sheet",sheet},{"source",member},{"scale",1}}).data.at("view").get<std::string>();
        const double expected=std::string(name)=="Formed"?formed_height:length*1.1;
        near(height(*live.open_drawing(drawing_id)->document().find_view(family_view)),expected);
        require(live.open_part(member)->session.data_generation()==variant_generation,"Family drawing calculated its source variant");
        family_views.emplace_back(family_view,expected);
    }
    state=live.open_part(id);state->session.document().save(part_path,state->session.calculated_boundaries());
    run("save");reopened=drawing::DrawingDocument::load(drawing_path);
    require(workspace::regenerate_drawing_views(reopened,nullptr,drawing_path)==6,"Native Family drawing lost its sources");
    verify_section(reopened,formed_height);
    for(const auto& [view_id,expected]:family_views)near(height(*reopened.find_view(view_id)),expected);
    std::filesystem::remove(part_path);std::filesystem::remove(drawing_path);
    require(directory.parent_path()==root&&directory.filename()=="zima-state-drawing-"+id,"Unsafe fixture directory");std::filesystem::remove_all(directory);
    std::cout<<"Solid state drawing: current geometry, persistent dimension, regeneration, Undo/Redo and native sources passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
