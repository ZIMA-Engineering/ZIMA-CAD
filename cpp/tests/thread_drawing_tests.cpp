#include <zima/command_host/host.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/drawing_render/pdf_export.hpp>
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
    auto shaft_doc=document::PartDocument::create_default();auto shaft=document::PartDocument::create_cylinder_container();
    shaft.cylinder.radius=5;shaft.cylinder.height=30;shaft_doc.history.push_back(shaft);
    const auto shaft_base=kernel.evaluate_history(shaft_doc.kernel_operations());
    const auto& refs=shaft_base.back().mesh.original_references;
    const auto face=[&](const char* key){for(const auto& ref:refs.triangle_references)if(ref.owner_id==shaft.id&&ref.semantic_key==key&&ref.surface)return ref;throw std::runtime_error("Missing shaft reference");};
    auto external=document::PartDocument::create_shaft_thread_container();external.shaft_thread.cylinder=face("side");external.shaft_thread.start=face("z_min");
    external.shaft_thread.root_diameter=8.16;external.shaft_thread.length=15;shaft_doc.history.push_back(external);
    const auto shaft_result=kernel.evaluate_history(shaft_doc.kernel_operations());
    require(std::abs(thread_length(drawing::project_edges(shaft_result.back().mesh,camera))-expected*8.16/10)<.02,"External thread did not use the root-diameter arc");
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
    std::cout<<"Native thread axial/oblique/occluded/repeated projection, chamfer, persistence, PDF and line weights passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
