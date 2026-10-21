#include <zima/document/component_source.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <QTemporaryDir>

static void verify_sketch_axis_visibility(QApplication& application,QWidget& parent) {
    using namespace zima;
    auto source=sketcher::Sketch::create_default();
    const auto axis=source.add_segment(-20,12,20,12);source.set_segment_centerline(axis,true);
    const auto profile=source.add_segment(-25,-18,25,-18);
    const auto axis_start=source.segments.front().first_point_id;
    const auto axis_end=source.segments.front().second_point_id;
    const auto serialized=source.serialized();
    viewer::MeshView view(&parent);view.setGeometry(0,0,600,500);view.show();view.raise();
    const auto original_palette=view.palette();
    auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;
    view.set_camera_state(camera);
    view.set_reference_visibility(viewer::ReferenceVisibility::Origins,false);
    const auto project=[&](double x,double y){const double unit=view.world_tolerance_for_pixels(1);
        return QPointF(view.width()/2.+x/unit,view.height()/2.-y/unit);};
    unsigned cases=0;
    for(bool dark:{false,true})for(bool reopened:{false,true}) {
        auto palette=original_palette;palette.setColor(QPalette::Window,dark?QColor("#202020"):QColor("#FFFFFF"));
        palette.setColor(QPalette::WindowText,dark?QColor("#EEEEEE"):QColor("#202020"));view.setPalette(palette);
        const auto sketch=reopened?sketcher::Sketch::from_serialized(serialized):source;
        for(bool editing:{false,true}) {
            auto packet=sketch.viewer_mesh();if(!editing)sketch.filter_hidden_3d_geometry(packet);
            packet.axes.clear();packet.vertices={{-50,-50,0},{50,50,0}};
            view.set_active_sketch_owner(editing?sketch.id:"");view.set_mesh(packet);view.clear_selection();
            view.set_selection_contract({});
            for(const auto mode:{viewer::DisplayMode::Wire,viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Shaded})
            for(bool sketches:{true,false})for(bool axes:{true,false})for(bool points:{true,false}) {
                view.set_display_mode(mode);view.set_reference_visibility(viewer::ReferenceVisibility::Sketches,sketches);
                view.set_reference_visibility(viewer::ReferenceVisibility::Axes,axes);
                view.set_reference_visibility(viewer::ReferenceVisibility::Points,points);application.processEvents();
                const auto frame=view.grabFramebuffer();const bool line_shown=editing?sketches:axes;
                const auto wire_color=editing?QColor("#4DD811"):QColor(173,110,46);
                require(framebuffer_contains_color_near(frame,view.size(),project(0,12),wire_color,8)==line_shown,
                    "Sketch rotation axis uses the wrong visibility category");
                for(double x:{-20.,20.}) {
                    const auto color=editing?viewer::view_theme(palette).foreground:QColor(173,110,46);
                    require(framebuffer_contains_color_near(frame,view.size(),project(x,12),color,2)==(editing||axes),
                        "Sketch rotation axis endpoint has the wrong visibility or idle color");
                }
                // A real profile and its native points keep their own role.
                require(framebuffer_contains_color_near(frame,view.size(),project(0,-18),viewer::view_theme(palette).foreground,2)==sketches,
                    "Axis visibility changed the ordinary Sketch profile");
                ++cases;
            }
            if(!editing) {
                view.set_reference_visibility(viewer::ReferenceVisibility::Sketches,false);
                for(bool axes:{true,false,true}) {
                    view.set_reference_visibility(viewer::ReferenceVisibility::Axes,axes);
                    view.set_selection_contract({viewer::CandidateKind::Container,viewer::CandidateKind::SketchSegment});
                    const auto hits=view.selection_candidates_at(project(0,12));
                    require(std::ranges::any_of(hits,[&](const auto& hit){return hit.owner_id==sketch.id;})==axes,
                        "Hidden Sketch axis still offers its edge or owning Container");
                }
                view.set_selection_contract({});view.set_reference_visibility(viewer::ReferenceVisibility::Axes,false);
                view.confirm_reference(sketch.id,"point:"+axis_start,{},viewer::CandidateKind::SketchPoint);application.processEvents();
                require(!framebuffer_contains_color_near(view.grabFramebuffer(),view.size(),project(-20,12),QColor("#00D1FF"),3),
                    "A previously confirmed endpoint is repainted after its Sketch axis is hidden");
                view.clear_selection();
                require(view.grabFramebuffer().save(dark?"build/sketch-axis-hidden-dark.png":"build/sketch-axis-hidden-light.png"),
                    "Cannot save hidden-axis GUI evidence");
                view.set_reference_visibility(viewer::ReferenceVisibility::Axes,true);
                require(view.grabFramebuffer().save(dark?"build/sketch-axis-shown-dark.png":"build/sketch-axis-shown-light.png"),
                    "Cannot save shown-axis GUI evidence");
            }
        }
        require(sketch.serialized()==serialized,"Axis display toggles changed native Sketch data");
    }
    // An inserted standalone Sketch must retain the real axis endpoints too.
    auto part=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();
    source.owner_container_id=owner.id;part.history={owner};part.sketches={source};
    const auto component=document::component_source(part,{});
    for(const auto& point:{axis_start,axis_end}) {
        require(std::ranges::any_of(component.mesh.points,[&](const auto& value){return value.reference.owner_id==source.id&&
            value.reference.semantic_key=="point:"+point&&value.construction;}),"Component source dropped a visible Sketch axis endpoint");
    }
    // BodyResult's solid-vertex packet intentionally omits Sketch presentation.
    // Exercise the production native source path: Part/Assembly loading rebuilds
    // the displayed Sketch from its persisted native incidence, without OCCT.
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Axis source"));
    graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
    QTemporaryDir files;require(files.isValid(),"Cannot create native axis fixture directory");
    const auto part_path=std::filesystem::path(files.filePath("axis-source.prtz").toStdWString());
    part.save(part_path);const auto reopened_part=document::PartDocument::load(part_path);
    const auto reopened_source=document::component_source(reopened_part,{});
    auto assembly=assembly::AssemblyDocument::create_default();
    for(int index=0;index<2;++index) {
        auto occurrence=assembly::AssemblyDocument::create_part_occurrence("Axis source",part.document_id,part_path,reopened_source);
        assembly.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,occurrence.occurrence_id);
        assembly.components.push_back(std::move(occurrence));
    }
    const auto assembly_path=std::filesystem::path(files.filePath("axes.asmz").toStdWString());
    assembly.save(assembly_path);const auto reopened_assembly=assembly::AssemblyDocument::load(assembly_path);
    for(const auto& occurrence:reopened_assembly.components) {
        require(!occurrence.source_missing,"Reopened Assembly failed to hydrate its native axis source");
        for(const auto& point:{axis_start,axis_end})
            require(std::ranges::any_of(occurrence.calculated_source->mesh.points,[&](const auto& value){
                return value.reference.owner_id==source.id&&value.reference.semantic_key=="point:"+point&&value.construction;}),
                "Native Part/Assembly source hydration lost a Sketch axis endpoint role");
    }
    // A shared profile/axis junction remains an ordinary profile point.
    auto shared=sketcher::Sketch::from_serialized(serialized);
    const auto shared_axis=shared.add_segment(-25,-18,-25,5);shared.set_segment_centerline(shared_axis,true);
    auto shared_packet=shared.viewer_mesh();shared.filter_hidden_3d_geometry(shared_packet);
    shared_packet.axes.clear();shared_packet.vertices={{-50,-50,0},{50,50,0}};
    view.set_active_sketch_owner({});view.set_mesh(shared_packet);view.clear_selection();view.set_selection_contract({});
    view.set_reference_visibility(viewer::ReferenceVisibility::Sketches,true);
    view.set_reference_visibility(viewer::ReferenceVisibility::Axes,false);application.processEvents();
    require(framebuffer_contains_color_near(view.grabFramebuffer(),view.size(),project(-25,-18),viewer::view_theme(view.palette()).foreground,2),
        "Hiding a Sketch axis removed its shared ordinary profile point");
    // The shared centerline category also includes the existing curved kinds.
    for(int kind=0;kind<5;++kind) {
        auto curve=sketcher::Sketch::create_default();std::string id;
        if(kind==0)id=curve.add_circle(0,0,10);
        if(kind==1)id=curve.add_arc(0,0,10,0,0,10);
        if(kind==2)id=curve.add_ellipse(0,0,20,0,0,10);
        if(kind==3)id=curve.add_elliptical_arc(0,0,20,0,0,10,20,0,0,10);
        if(kind==4)id=curve.add_bspline({{{-20,-10}},{{-10,15}},{{10,-15}},{{20,10}}});
        curve.set_geometry_centerline(id,true);
        auto packet=curve.viewer_mesh();curve.filter_hidden_3d_geometry(packet);packet.axes.clear();
        packet.vertices={{-50,-50,0},{50,50,0}};view.set_mesh(packet);view.clear_selection();
        view.set_reference_visibility(viewer::ReferenceVisibility::Sketches,false);
        for(bool axes:{true,false,true}) {
            view.set_reference_visibility(viewer::ReferenceVisibility::Axes,axes);application.processEvents();
            const auto frame=view.grabFramebuffer();
            for(const auto& point:packet.points)if(point.reference.semantic_key.starts_with("point:")&&point.construction)
                require(framebuffer_contains_color_near(frame,view.size(),project(point.position.x,point.position.y),QColor(173,110,46),2)==axes,
                    "A curved Sketch centerline point does not follow its Axes category");
        }
    }
    view.set_active_sketch_owner({});view.setPalette(original_palette);
    std::cout<<"Sketch rotation axis: "<<cases<<" native round-trip/theme/display/editing/visibility cases, hidden picking and component endpoints passed\n";
}
