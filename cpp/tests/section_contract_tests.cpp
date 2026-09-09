#include <zima/document/section.hpp>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <cmath>
#include <filesystem>
using namespace zima;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double area(const document::SectionResult& cut){double result=0;for(const auto& p:cut.patches)for(const auto& t:p.triangles){auto a=t[0],b=t[1],c=t[2];const double x=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),y=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),z=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);result+=std::sqrt(x*x+y*y+z*z)/2;}return result;}
double volume(const kernel::ViewerMesh& mesh){double sum=0;for(std::size_t i=0;i+2<mesh.triangles.size();i+=3){auto a=mesh.vertices.at(mesh.triangles[i]),b=mesh.vertices.at(mesh.triangles[i+1]),c=mesh.vertices.at(mesh.triangles[i+2]);sum+=a.x*(b.y*c.z-b.z*c.y)+a.y*(b.z*c.x-b.x*c.z)+a.z*(b.x*c.y-b.y*c.x);}return std::abs(sum)/6;}
document::SectionDefinition chain(std::initializer_list<std::array<double,2>> points){auto s=document::create_section();auto p=points.begin();for(auto q=p+1;q!=points.end();++q,++p)static_cast<void>(s.sketch.add_segment((*p)[0],(*p)[1],(*q)[0],(*q)[1]));return s;}
int main(){try{
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};
    const auto source=kernel.evaluate_history(part.kernel_operations()).back().mesh;
    auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-20,0,20,0));
    auto cut=document::calculate_section(source,section);require(std::abs(area(cut)-100)<1e-7,"Box cross-section area is incorrect");
    for(const auto& p:cut.mesh.vertices)require(p.y>=-1e-8,"Wrong half of the box was retained");
    section.reversed=true;const auto reversed=document::calculate_section(source,section);require(std::abs(area(reversed)-100)<1e-7,"Reverse changed section area");for(const auto& p:reversed.mesh.vertices)require(p.y<=1e-8,"Reverse failed");section.reversed=false;
    auto tangent=section;tangent.plane_origin.y=5;auto empty=document::calculate_section(source,tangent);require(empty.mesh.triangles.empty()&&empty.patches.empty(),"Tangent cut invented a material face");
    tangent.reversed=true;require(document::calculate_section(source,tangent).patches.empty(),"Exterior plane generated duplicate cap");
    // A square tube provides an exact independent area and empty-hole check.
    auto bore=document::PartDocument::create_box_container();bore.box={4,20,4};bore.combine_mode=document::CombineMode::Subtract;part.history.push_back(bore);
    const auto tube=kernel.evaluate_history(part.kernel_operations()).back().mesh;cut=document::calculate_section(tube,section);require(std::abs(area(cut)-84)<1e-7,"Section filled a cavity");
    const auto frame=document::section_frame(section);const auto hatch=document::section_hatch_lines(cut.patches.front(),frame,{0,1,0,0},1);
    for(const auto& e:hatch){const auto a=e.points.front(),b=e.points.back();if(std::abs(a.z)<2-1e-8)require(std::max(a.x,b.x)<=-2+1e-8||std::min(a.x,b.x)>=2-1e-8,"Hatch crossed the hole");}
    // Independent analytical checks: quarter box and a stepped half of a tube.
    auto elbow=chain({{-20,0},{0,0},{0,20}});auto bent=document::calculate_section(source,elbow);
    require(std::abs(area(bent)-100)<1e-6&&std::abs(volume(bent.mesh)-250)<1e-6,"L cut has incorrect cap area or retained volume");
    elbow.reversed=true;auto complement=document::calculate_section(source,elbow);require(std::abs(volume(complement.mesh)-750)<1e-6,"Reversed L cut is not the complementary volume");
    auto step=chain({{-20,-2},{0,-2},{0,2},{20,2}});auto stepped=document::calculate_section(tube,step);
    require(std::abs(area(stepped)-108)<1e-6&&std::abs(volume(stepped.mesh)-420)<1e-6,"Offset section filled a hole or retained the wrong side");
    step.reversed=true;require(std::abs(volume(document::calculate_section(tube,step).mesh)-420)<1e-6,"Reversed offset cut changed volume");step.reversed=false;
    for(const auto& patch:stepped.patches)for(auto edge:document::section_hatch_lines(patch,patch.frame,{0,1,.25,0},1)){
        auto p=edge.points.front(),q=edge.points.back();auto x=(p.x+q.x)/2,z=(p.z+q.z)/2;require(!(std::abs(x)<2-1e-7&&std::abs(z)<2-1e-7),"Offset hatch crossed the tube cavity");
    }
    const auto invalid=[&](auto s){try{static_cast<void>(document::calculate_section(source,s));return false;}catch(const std::invalid_argument&){return true;}};
    require(invalid(chain({{0,0},{2,0},{2,2},{0,0}})),"Closed section was accepted");
    require(invalid(chain({{-4,-4},{4,4},{-4,4},{4,-4}})),"Self-intersecting section was accepted");
    auto disconnected=chain({{0,0},{2,0}});static_cast<void>(disconnected.sketch.add_segment(5,0,7,0));require(invalid(disconnected),"Disconnected section was accepted");
    auto placed=step;placed.placement.x=12;placed.placement.rotation_z=90;placed.placement.absolute_rotation_z=90;placed.sketch.plane=sketcher::SketchPlane::XZ;document::reframe_section(placed);
    require(std::abs(placed.plane_origin.x-12)<1e-8&&std::abs(placed.plane_x.y-1)<1e-8&&std::abs(placed.plane_y.z+1)<1e-8,"Container rotation or local XZ plane was ignored");
    const auto saved_placed=document::parse_sections(document::serialize_sections({placed})).front();require(saved_placed.container_origin==placed.container_origin&&saved_placed.sketch.owner_container_id==placed.id&&std::abs(saved_placed.plane_y.z+1)<1e-8,"Section lost origin ancestry or sketch frame");
    auto geometry=part.origin_viewer_mesh().original_references;require(!geometry.points.empty(),"Origin fixture has no persistent point");
    const auto reference=geometry.points.front().reference;auto attached=chain({{-10,0},{10,0}});attached.placement.references.push_back({{},reference.owner_id,reference.semantic_key});std::vector attached_sections{attached};
    document::resolve_section_placements(attached_sections,geometry);require(attached_sections.front().placement.reference_valid,"Section could not use ordinary persisted placement reference");
    geometry.points.front().position={3,7,9};document::resolve_section_placements(attached_sections,geometry);require(std::abs(attached_sections.front().plane_origin.x-3)<1e-7&&std::abs(attached_sections.front().plane_origin.y-7)<1e-7&&std::abs(attached_sections.front().plane_origin.z-9)<1e-7,"Section did not follow regenerated placement reference");
    document::resolve_section_placements(attached_sections,{});require(!attached_sections.front().placement.reference_valid,"Missing section reference was silently accepted");
    require(!document::parse_sections(document::serialize_sections(attached_sections)).front().placement.reference_valid,"Unresolved section cannot be reopened for repair");
    auto straight_chain=chain({{-10,0},{0,0},{10,0}});require(document::section_frames(straight_chain).size()==1,"Collinear segments introduced a false cap seam");
    auto assembly_mesh=source;for(auto& r:assembly_mesh.triangle_references)r.instance_path="1:a";for(auto& e:assembly_mesh.edges)e.reference.instance_path="1:a";
    const auto base=static_cast<std::uint32_t>(assembly_mesh.vertices.size());for(auto p:source.vertices){p.x+=15;assembly_mesh.vertices.push_back(p);}for(auto i:source.triangles)assembly_mesh.triangles.push_back(i+base);
    for(auto r:source.triangle_references){r.instance_path="1:b";assembly_mesh.triangle_references.push_back(r);}for(auto e:source.edges){e.reference.instance_path="1:b";for(auto& p:e.points)p.x+=15;assembly_mesh.edges.push_back(e);}
    section.components["1:b"].mode=2;cut=document::calculate_section(assembly_mesh,section);require(cut.patches.size()==1 && cut.patches.front().component=="1:a" && std::abs(area(cut)-100)<1e-7,"Repeated occurrence exclusion was not exact");
    section.show_cut=true;const auto encoded=document::serialize_sections({section});require(document::serialize_sections(document::parse_sections(encoded))==encoded,"Section persistence failed");
    // A visible horizontal face must not acquire edges from cut-region
    // triangulation. These are not model edges or section boundaries.
    elbow.reversed=false;auto top=drawing::DrawingDocument::create_view(part.document_id,{},source,drawing::ViewOrientation::Top);top.align_section=false;top.hatching=false;top.section_id=elbow.id;top.section_snapshot=elbow;drawing::refresh_view_geometry(top,source);
    for(const auto& edge:top.projected_edges)if(!edge.hidden)for(std::size_t i=1;i<edge.points.size();++i){const auto p=edge.points[i-1],q=edge.points[i];const auto x=(p.x+q.x)/2,y=(p.y+q.y)/2;
        require(std::abs(x-5)<1e-6||std::abs(x)<1e-6||std::abs(y)<1e-6||std::abs(y-5)<1e-6,"Polyline section introduced an internal visible skin edge");
    }
    auto draw=drawing::DrawingDocument::create_default();auto view=drawing::DrawingDocument::create_view(part.document_id,{},tube);view.section_id=section.id;view.section_snapshot=section;view.hatch_style={45,2,0,0};drawing::refresh_view_geometry(view,tube);
    require(std::ranges::any_of(view.projected_edges,[](const auto& e){return e.hatch&&!e.hidden;}),"Drawing section has no visible hatching");
    for(const auto& edge:view.projected_edges)if(!edge.hatch&&!edge.hidden)for(std::size_t i=1;i<edge.points.size();++i){const auto a=edge.points[i-1],b=edge.points[i];const double x=(a.x+b.x)/2,y=(a.y+b.y)/2;
        const bool outer=std::abs(std::abs(x)-5)<1e-6||std::abs(std::abs(y)-5)<1e-6;
        const bool inner=(std::abs(std::abs(x)-2)<1e-6&&std::abs(y)<=2+1e-6)||(std::abs(std::abs(y)-2)<1e-6&&std::abs(x)<=2+1e-6);
        require(outer||inner,"Section cap triangulation produced an internal visible edge");
    }
    for(const auto scale:{.5,1.,2.})for(const auto aligned:{true,false}){
        auto test=view;test.scale=scale;test.align_section=aligned;test.camera=drawing::standard_camera(drawing::ViewOrientation::Isometric);test.hatch_style={0,1.2,.1,0};drawing::refresh_view_geometry(test,tube);
        std::vector<double> levels;for(const auto& edge:test.projected_edges)if(edge.hatch){require(std::abs(edge.points.front().y-edge.points.back().y)*scale<1e-6,"Hatch angle changed in oblique view");levels.push_back(edge.points.front().y*scale);}
        std::ranges::sort(levels);levels.erase(std::unique(levels.begin(),levels.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),levels.end());require(levels.size()>2,"Insufficient hatch lines");for(std::size_t i=1;i<levels.size();++i)require(std::abs((levels[i]-levels[i-1])-1.2)<1e-6,"Paper hatch spacing depends on view scale");
    }
    view.section_components["component"].mode=1;view.section_snapshot->body_owners["feature"]="body";
    draw.sheets.front().views.push_back(view);const auto directory=std::filesystem::temp_directory_path()/kernel::make_stable_id();std::filesystem::create_directory(directory);
    draw.save(directory/"section.drwz");auto loaded=drawing::DrawingDocument::load(directory/"section.drwz");require(loaded.sheets.front().views.front().section_id==section.id,"Drawing lost section link");
    const auto& saved_view=loaded.sheets.front().views.front();require(saved_view.section_components.at("component").mode==1&&saved_view.section_snapshot->body_owners.at("feature")=="body"&&saved_view.section_snapshot->components.at("1:b").mode==2,"Drawing mixed section defaults and component overrides");
    part.sections={section};part.save(directory/"section.prtz");require(document::PartDocument::load(directory/"section.prtz").sections.size()==1,"Part lost Section");
    auto asm_doc=assembly::AssemblyDocument::create_default();asm_doc.sections={section};asm_doc.save(directory/"section.asmz");require(assembly::AssemblyDocument::load(directory/"section.asmz").sections.size()==1,"Assembly lost Section");
    std::cout<<"Section areas, holes, reversed side, occurrence exclusions, hatching and persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
