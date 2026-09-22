#include <zima/command_host/host.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/drawing_render/pdf_export.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <zima/document/feature_parameter_dimensions.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <QGuiApplication>
#include <QImage>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double thread_length(const std::vector<drawing::ProjectedEdge>& edges,bool hidden=false){
    double result=0;for(const auto& e:edges)if(e.thread&&e.hidden==hidden)for(std::size_t i=1;i<e.points.size();++i)
        result+=std::hypot(e.points[i].x-e.points[i-1].x,e.points[i].y-e.points[i-1].y);return result;
}
void verify(){
    const auto folder=std::filesystem::absolute("Projects/test/thread-drawing");std::filesystem::create_directories(folder);
    kernel::OcctKernel kernel;
    auto part=document::PartDocument::create_default();
    auto box=document::PartDocument::create_box_container();box.box={40,40,40};part.history.push_back(box);
    auto thread=document::PartDocument::create_thread_container();thread.placement.z=-20;
    thread.thread.chamfer_enabled=false;thread.hole.drill_point_enabled=false;
    thread.thread.nominal_diameter=10;thread.thread.pitch=1.5;thread.thread.length_forward=25;thread.thread.bore_length=32;
    part.history.push_back(thread);
    const auto boundaries=kernel.evaluate_history(part.kernel_operations());const auto mesh=boundaries.back().mesh;
    const auto camera=drawing::standard_camera(drawing::ViewOrientation::Bottom);
    const auto edges=drawing::project_edges(mesh,camera);
    const double expected=5*280*std::numbers::pi/180;
    require(std::abs(thread_length(edges)-expected)<.02,"Axial internal thread is not one visible 280-degree arc");
    require(std::ranges::any_of(edges,[](const auto& e){return !e.thread&&!e.hidden;}),"Real hole/body outlines disappeared");
    const auto back=drawing::project_edges(mesh,drawing::ViewOrientation::Top);
    require(thread_length(back)==0&&thread_length(back,true)>20,"Blind thread shows through the back of the solid");
    const auto oblique=drawing::project_edges(mesh,drawing::ViewOrientation::Isometric);
    require(std::ranges::any_of(oblique,[](const auto& e){return e.thread&&e.silhouette;}),"Oblique thread lost its thin surface silhouettes");
    auto repeated=mesh;
    for(auto& edge:repeated.edges)edge.reference.instance_path="first";
    for(auto& ref:repeated.triangle_references)ref.instance_path="first";
    // A second occurrence uses the same source IDs, at a separate position.
    const auto base=static_cast<std::uint32_t>(repeated.vertices.size());
    for(auto p:mesh.vertices){p.x+=50;repeated.vertices.push_back(p);}
    for(auto index:mesh.triangles)repeated.triangles.push_back(base+index);
    for(auto ref:mesh.triangle_references){ref.instance_path="second";repeated.triangle_references.push_back(ref);}
    for(auto edge:mesh.edges){for(auto& p:edge.points)p.x+=50;edge.reference.instance_path="second";repeated.edges.push_back(edge);}
    require(std::abs(thread_length(drawing::project_edges(repeated,camera))-2*expected)<.04,"Repeated thread occurrences were merged");
    part.history.back().thread.chamfer_enabled=true;
    const auto chamfer=kernel.evaluate_history(part.kernel_operations());
    require(std::abs(thread_length(drawing::project_edges(chamfer.back().mesh,camera))-expected)<.02,"Entrance chamfer hides or duplicates the thread arc");
    {
        auto specimen=part;specimen.history.back().hole.drill_point_enabled=true;
        const auto calculated=kernel.evaluate_history(specimen.kernel_operations());
        auto axial=drawing::DrawingDocument::create_view(specimen.document_id,{},calculated.back().mesh,drawing::ViewOrientation::Bottom);
        const auto leadins=std::ranges::count_if(axial.projected_edges,[](const auto& edge){return edge.thread_leadin;});
        require(leadins>0,"Thread lead-in circle was not recognized");
        specimen.save(folder/"chamfer-source.prtz",calculated);
        {
            auto assembly=assembly::AssemblyDocument::create_default();
            for(double x:{-70.,70.}) {
                auto occurrence=assembly::AssemblyDocument::create_part_occurrence("Thread",specimen.document_id,folder/"chamfer-source.prtz",calculated.back());
                occurrence.placement.x=x;occurrence.placement.y=23;occurrence.placement.z=11;
                occurrence.placement.rotation_z=90;
                assembly.components.push_back(std::move(occurrence));
            }
            const auto ordinary=assembly.build_scene();
            const auto packet=assembly.build_drawing_scene();
            const auto projected=drawing::project_edges(packet,camera);
            require(std::ranges::count_if(projected,[](const auto& edge){return edge.thread_leadin;})==2*leadins,"Translated/rotated Assembly thread lead-ins were not recognized");
            require(std::abs(thread_length(projected)-2*expected)<.04,"Translated Assembly lost thread arcs");
            const auto unchanged=assembly.build_scene();
            for(std::size_t i=0;i<ordinary.triangle_references.size();++i)if(ordinary.triangle_references[i].surface)
                require(*ordinary.triangle_references[i].surface==*unchanged.triangle_references[i].surface,"Drawing changed persisted Assembly surface frames or side identity");
            auto parent=assembly::AssemblyDocument::create_default();
            auto child=assembly::AssemblyDocument::create_assembly_occurrence("Nested",assembly.document_id,folder/"nested.asmz",assembly);
            child.placement.x=31;child.placement.y=47;child.placement.rotation_z=90;
            parent.components.push_back(std::move(child));
            const auto nested=drawing::project_edges(parent.build_drawing_scene(),camera);
            require(std::ranges::count_if(nested,[](const auto& edge){return edge.thread_leadin;})==2*leadins,"Nested Assembly lost thread lead-in surface frames");
        }
        std::vector<kernel::BodyResult> reopened_boundaries;
        const auto reopened=document::PartDocument::load(folder/"chamfer-source.prtz",&reopened_boundaries);
        require(!reopened_boundaries.empty(),"Saved thread source lost its calculated geometry");
        const auto reopened_axial=drawing::DrawingDocument::create_view(reopened.document_id,{},reopened_boundaries.back().mesh,drawing::ViewOrientation::Bottom);
        require(std::ranges::count_if(reopened_axial.projected_edges,[](const auto& edge){return edge.thread_leadin;})==leadins,"Reopened Part lost thread lead-in recognition");
        require(std::abs(thread_length(reopened_axial.projected_edges)-thread_length(axial.projected_edges))<.02,"Reopened Part chamfer occludes the conventional thread arc");
        for(const auto& edge:reopened_axial.projected_edges)if(edge.silhouette&&!edge.hidden&&!edge.thread)for(auto p:edge.points)
            require(std::hypot(p.x,p.y)>1e-5,"Reopened Part draws a cone generator from the thread centre");
        for(const auto& edge:axial.projected_edges)if(edge.thread_leadin){require(!drawing::drawing_edge_visible(axial,edge),"Leadin visible by default");axial.show_thread_leadins=true;require(drawing::drawing_edge_visible(axial,edge)==!edge.hidden,"Leadin toggle does not restore circle");axial.show_thread_leadins=false;}
        for(const auto& edge:axial.projected_edges)if(edge.silhouette&&!edge.hidden&&!edge.thread)for(auto p:edge.points)
            require(std::hypot(p.x,p.y)>1e-5,"A cone generator is drawn from the thread centre");
        const auto annotations=document::primitive_parameter_dimensions(specimen.history.back());
        drawing::ModelAnnotationSource source;source.document_id=specimen.document_id;source.dimensions=annotations.dimensions;
        for(auto orientation:{drawing::ViewOrientation::Bottom,drawing::ViewOrientation::Front}) {
            auto view=drawing::DrawingDocument::create_view(specimen.document_id,{},calculated.back().mesh,orientation);
            const std::array sources{source};view.display_style=drawing::DisplayStyle::HiddenEdges;drawing::refresh_model_annotations(view,sources);
            bool checked=false;
            for(const auto& edge:view.projected_edges)if(edge.thread&&edge.source.valid()&&edge.points.size()>1) {
                const auto a=edge.points.front(),b=edge.points[1];drawing::Point2 target{(a.x+b.x)/2,(a.y+b.y)/2};
                drawing::MeasurementPickRequest request;request.circles_only=true;
                for(const auto& candidate:drawing::measurement_candidates(view,target,.2,request))if(candidate.attachment.reference.semantic_key.starts_with("thread:boundary:")) {
                    auto dimension=drawing::make_drawing_dimension(view.id,drawing::DrawingDimensionKind::Diameter);dimension.attachments[0]=candidate.attachment;
                    const auto evaluation=drawing::evaluate_drawing_dimension(view,dimension);
                    require(evaluation.state==drawing::MeasurementState::Resolved,"Thread diameter did not resolve in hidden projection");
                    require(drawing::drawing_dimension_text(dimension,evaluation.presentations.front())==specimen.history.back().thread.designation,"Thread dimension lost its designation");checked=true;break;
                }
                if(checked)break;
            }
            require(checked,"No measurable thread candidate in axial or side projection");
        }
    }
    auto shaft_doc=document::PartDocument::create_default();auto shaft=document::PartDocument::create_cylinder_container();
    shaft.cylinder.radius=5;shaft.cylinder.height=30;shaft_doc.history.push_back(shaft);
    const auto shaft_base=kernel.evaluate_history(shaft_doc.kernel_operations());
    const auto& refs=shaft_base.back().mesh.original_references;
    const auto face=[&](const char* key){for(const auto& ref:refs.triangle_references)if(ref.owner_id==shaft.id&&ref.semantic_key==key&&ref.surface)return ref;throw std::runtime_error("Missing shaft reference");};
    auto external=document::PartDocument::create_shaft_thread_container();external.shaft_thread.cylinder=face("side");external.shaft_thread.start=face("z_min");
    external.shaft_thread.root_diameter=8.16;external.shaft_thread.length=15;shaft_doc.history.push_back(external);
    const auto shaft_result=kernel.evaluate_history(shaft_doc.kernel_operations());
    require(std::abs(thread_length(drawing::project_edges(shaft_result.back().mesh,camera))-expected*8.16/10)<.02,"External thread did not use the root-diameter arc");
    {
        workspace::Workspace live;
        auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-50,0,50,0));
        part.sections.push_back(section);live.add_part(part,chamfer);
        live.add_part(shaft_doc,shaft_result);
        workspace::DrawingProjection projection(&live,{});
        for(const auto* model:{&part,&shaft_doc})for(bool cut:{false,true}) {
            if(cut&&model==&shaft_doc)continue;
            auto measured=drawing::DrawingDocument::create_view(model->document_id,{}, {},drawing::ViewOrientation::Front);
            measured.display_style=drawing::DisplayStyle::HiddenEdges;
            if(cut)measured.section_id=section.id;
            projection.project(measured,{});
            bool picked=false;
            for(const auto& edge:measured.projected_edges)if(edge.thread&&edge.source.valid()&&edge.points.size()>1) {
                const auto a=edge.points[0],b=edge.points[1];drawing::MeasurementPickRequest request;request.circles_only=true;
                for(const auto& candidate:drawing::measurement_candidates(measured,{(a.x+b.x)/2,(a.y+b.y)/2},.2,request)) {
                    if(!candidate.attachment.reference.semantic_key.starts_with("thread:boundary:"))continue;
                    auto dimension=drawing::make_drawing_dimension(measured.id,drawing::DrawingDimensionKind::Diameter);
                    dimension.attachments[0]=candidate.attachment;
                    const auto evaluation=drawing::evaluate_drawing_dimension(measured,dimension);
                    require(evaluation.state==drawing::MeasurementState::Resolved,"Section/external thread cannot be dimensioned");
                    const auto expected=model==&part?part.history.back().thread.designation:external.shaft_thread.designation;
                    require(drawing::drawing_dimension_text(dimension,evaluation.presentations.front())==expected,"Section/external thread designation missing");
                    auto saved=drawing::DrawingDocument::create_default();saved.sheets.front().views={measured};saved.save(folder/"measured.drwz");
                    const auto loaded=drawing::DrawingDocument::load(folder/"measured.drwz");
                    const auto after=drawing::evaluate_drawing_dimension(loaded.sheets.front().views.front(),dimension);
                    require(after.state==drawing::MeasurementState::Resolved&&drawing::drawing_dimension_text(dimension,after.presentations.front())==expected,"Thread measurement metadata failed to persist");
                    picked=true;break;
                }
                if(picked)break;
            }
            require(picked,"Section/external thread has no measurable candidate");
        }
    }
    auto doc=drawing::DrawingDocument::create_default();auto& sheet=doc.sheets.front();
    auto view=drawing::DrawingDocument::create_view(part.document_id,folder/"source.prtz",{},drawing::ViewOrientation::Bottom);
    view.camera=camera;view.x=105;view.y=200;view.scale=2;view.projected_edges=edges;sheet.views={view};
    doc.save(folder/"thread.drwz");const auto loaded=drawing::DrawingDocument::load(folder/"thread.drwz");
    require(std::abs(thread_length(loaded.sheets.front().views.front().projected_edges)-expected)<.02,"Saved drawing lost thread line identity");
    part.history.back().thread.chamfer_enabled=false;part.save(folder/"source.prtz",boundaries);
    drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&sheet);
    QImage image(840,1188,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#303B47"));
    {QPainter painter(&image);renderer.paint_sheet(painter,4,{},false);}require(image.save(QString::fromStdString((folder/"thread-view.png").string())),"Cannot save thread preview");
    require(drawing_render::export_pdf(doc,folder/"thread.pdf",folder/"thread.drwz",nullptr,true)>0,"Thread PDF export failed");
    // The shared painter must retain thick ordinary contours and thin thread
    // lines on paper, with gray reserved for the interactive thread preview.
    drawing::ProjectedEdge ordinary;ordinary.points={{-10,5},{10,5}};
    auto thin=ordinary;thin.thread=true;for(auto& p:thin.points)p.y=-5;
    sheet.views.front().projected_edges={ordinary,thin};sheet.views.front().scale=1;sheet.views.front().x=105;sheet.views.front().y=148;
    renderer.set_render_sheet(&sheet);QImage paper(2100,2970,QImage::Format_RGB32);paper.fill(Qt::white);
    {QPainter painter(&paper);renderer.paint_sheet(painter,10,{},true);}
    const auto ink=[&](int center){int n=0;for(int y=center-8;y<=center+8;++y)if(qGray(paper.pixel(1050,y))<128)++n;return n;};
    require(ink(1440)>ink(1540)&&ink(1540)>=2,"Thread and ordinary contours print with the same width");
    for(const auto format:{drawing::SheetFormat::A0,drawing::SheetFormat::A1,drawing::SheetFormat::A2,drawing::SheetFormat::A3,drawing::SheetFormat::A4}) {
        auto marked=drawing::DrawingDocument::create_default();auto& frame=marked.sheets.front();frame.format=format;
        const auto name=std::string("ZE-A")+std::to_string(int(drawing::SheetFormat::A0)-int(format));
        drawing::load_frame_template(frame,std::filesystem::path("config/formats")/(name+".frmz"));
        require(frame.frame_trimming_marks,"Factory frame lacks ISO trimming marks");
        marked.save(folder/(name+".drwz"));
        require(drawing::DrawingDocument::load(folder/(name+".drwz")).sheets.front().frame_trimming_marks,"Trimming marks lost on Drawing reopen");
        renderer.set_render_sheet(&frame);
        QImage marks(int(frame.width_mm()*2),int(frame.height_mm()*2),QImage::Format_RGB32);marks.fill(Qt::white);
        {QPainter painter(&marks);renderer.paint_sheet(painter,2,{},true);}
        for(bool right:{false,true})for(bool bottom:{false,true}) {
            const auto sample=[&](int x,int y){return qGray(marks.pixel(right?marks.width()-1-x*2:x*2,bottom?marks.height()-1-y*2:y*2));};
            require(sample(2,7)<32&&sample(7,2)<32&&sample(7,7)>220,"Trimming mark is not a filled 10 x 10 mm L with 5 mm arms");
        }
        require(marks.save(QString::fromStdString((folder/(name+"-trimming.png")).string())),"Cannot save trimming mark preview");
    }
    std::cout<<"Native thread axial/oblique/occluded/repeated projection, chamfer, persistence, PDF and line weights passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
