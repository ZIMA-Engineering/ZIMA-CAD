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
    const auto display=document::section_display_mesh(cut,section);
    require(std::ranges::any_of(display.edges,[](const auto& e){return e.color=="#00C000";}),"3D section did not show green hatching");
    auto hidden3d=section;for(const auto& p:cut.patches)hidden3d.components[p.component].mode=1;
    const auto hidden_display=document::section_display_mesh(cut,hidden3d);
    require(hidden_display.triangles==display.triangles&&std::ranges::none_of(hidden_display.edges,[](const auto& e){return e.color=="#00C000";}),"3D hatch visibility changed the cut geometry");
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
    // Cutting traces extend short sketch ends to the entire paper-space view,
    // and accent both bends of an offset section like the engineering example.
    auto trace_view=drawing::DrawingDocument::create_view(part.document_id,{},source,drawing::ViewOrientation::Top);
    const auto short_section=chain({{-1,0},{1,0}});
    for(double scale:{.5,1.,2.}){trace_view.scale=scale;const auto trace=drawing::section_trace_layout(trace_view,short_section);
        require(trace&&trace->chain.size()==1&&trace->accents.size()==2,"Straight section trace has missing ends");
        require(std::abs(trace->chain.front()[0].x-(5*scale+7))<1e-7&&std::abs(trace->chain.front()[1].x+(5*scale+7))<1e-7,"Cut trace depends on sketch length or has wrong paper margin");
        for(auto line:trace->accents)require(std::abs(std::hypot(line[1].x-line[0].x,line[1].y-line[0].y)-6)<1e-7,"Heavy section ends scale with the model");
    }
    // Side projection looks along the defining sketch line. Its cutting
    // plane is nevertheless visible as a vertical trace across the body.
    for(const auto orientation:{drawing::ViewOrientation::Left,drawing::ViewOrientation::Right})for(double scale:{.5,1.,2.})for(bool rotated:{false,true}){
        auto side_view=drawing::DrawingDocument::create_view(part.document_id,{},source,orientation);side_view.scale=scale;
        auto side_section=chain({{-10,0},{0,0},{10,0}});side_section.plane_origin={0,2,70};
        if(rotated){side_section.plane_x={-1,0,0};side_section.plane_y={0,-1,0};}
        const auto trace=drawing::section_trace_layout(side_view,side_section);
        require(trace&&trace->chain.size()==1&&trace->accents.size()==2,"Side view dropped an edge-on cutting plane");
        const auto line=trace->chain.front();const double x=orientation==drawing::ViewOrientation::Left?-2*scale:2*scale;
        require(std::abs(line[0].x-x)<1e-7&&std::abs(line[1].x-x)<1e-7,"Side trace does not lie on the actual cutting plane");
        require(std::abs(std::min(line[0].y,line[1].y)+5*scale+7)<1e-7&&std::abs(std::max(line[0].y,line[1].y)-5*scale-7)<1e-7,"Side trace was limited to the sketch placement instead of the full view");
        side_section.reversed=true;const auto reversed=drawing::section_trace_layout(side_view,side_section);
        require(reversed&&reversed->chain==trace->chain,"Reversing a side-view trace exchanged its end handles");
        for(int end=0;end<2;++end)require(std::abs(trace->arrow_directions[end].x+reversed->arrow_directions[end].x)<1e-9&&std::abs(trace->arrow_directions[end].y)<1e-9,"Side trace arrows do not reverse across the cutting plane");
        side_view.section_marker_offsets[side_section.id]={3,4};const auto moved=drawing::section_trace_layout(side_view,side_section);
        require(moved&&moved->end_offsets==std::array<double,2>{3,4},"Side trace lost independent paper-space end offsets");
    }
    trace_view.scale=1;const auto step_trace=drawing::section_trace_layout(trace_view,step);require(step_trace&&step_trace->chain.size()==3&&step_trace->accents.size()==6,"Offset trace did not accent both significant bends");
    auto reversed_trace=step;reversed_trace.reversed=true;const auto reversed_layout=drawing::section_trace_layout(trace_view,reversed_trace);
    require(reversed_layout&&reversed_layout->chain==step_trace->chain,"Reversing a section moved its trace");
    for(int end=0;end<2;++end)require(std::abs(reversed_layout->arrow_directions[end].x+step_trace->arrow_directions[end].x)+std::abs(reversed_layout->arrow_directions[end].y+step_trace->arrow_directions[end].y)<1e-9,"Reversing a section did not reverse both arrows");
    const auto close_step=chain({{-20,-.5},{0,-.5},{0,.5},{20,.5}});const auto close_layout=drawing::section_trace_layout(trace_view,close_step);
    require(close_layout&&close_layout->accents.size()==6,"Closely spaced bends lost their accents");
    for(std::size_t i=2;i<close_layout->accents.size();++i){const auto line=close_layout->accents[i];require(std::hypot(line[1].x-line[0].x,line[1].y-line[0].y)<=3+1e-9,"Bend accent exceeds its nominal length");
        if(std::abs(line[1].x-line[0].x)<1e-9)require(std::min(line[0].y,line[1].y)>=-.5-1e-9&&std::max(line[0].y,line[1].y)<=.5+1e-9,"Bend accent crossed the next bend");}
    auto obstacles=step_trace->chain;obstacles.insert(obstacles.end(),step_trace->accents.begin(),step_trace->accents.end());
    for(std::size_t end=0;end<2;++end){const auto initial=drawing::section_letter_position(*step_trace,end,{4,5},obstacles,1);
        auto blocked=obstacles;blocked.push_back({drawing::Point2{initial.x-4,initial.y},drawing::Point2{initial.x+4,initial.y}});
        const auto shifted=drawing::section_letter_position(*step_trace,end,{4,5},blocked,1);require(std::hypot(shifted.x-initial.x,shifted.y-initial.y)>1,"Letter was not moved away from a colliding line");
        for(auto line:blocked){const bool apart=shifted.x-2>std::max(line[0].x,line[1].x)+1||shifted.x+2<std::min(line[0].x,line[1].x)-1||shifted.y-2.5>std::max(line[0].y,line[1].y)+1||shifted.y+2.5<std::min(line[0].y,line[1].y)-1;require(apart,"Letter clearance still intersects a line");}
    }
    auto moved_trace=trace_view;moved_trace.section_marker_offsets[short_section.id]={8,-1000};const auto moved_layout=drawing::section_trace_layout(moved_trace,short_section);
    require(moved_layout&&std::abs(moved_layout->end_offsets[0]-8)<1e-9&&moved_layout->end_offsets[1]==moved_layout->minimum_offsets[1],"Section manipulation did not clamp the end at the next bend");
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
    elbow.reversed=false;auto top=drawing::DrawingDocument::create_view(part.document_id,{},source,drawing::ViewOrientation::Top);top.section_id=elbow.id;top.section_snapshot=elbow;drawing::refresh_view_geometry(top,source);
    for(const auto& edge:top.projected_edges)if(!edge.hidden&&!edge.hatch)for(std::size_t i=1;i<edge.points.size();++i){const auto p=edge.points[i-1],q=edge.points[i];const auto x=(p.x+q.x)/2,y=(p.y+q.y)/2;
        require(std::abs(x-5)<1e-6||std::abs(x)<1e-6||std::abs(y)<1e-6||std::abs(y-5)<1e-6,"Polyline section introduced an internal visible skin edge");
    }
    auto draw=drawing::DrawingDocument::create_default();auto view=drawing::DrawingDocument::create_view(part.document_id,{},tube,drawing::ViewOrientation::Front);view.section_id=section.id;view.section_snapshot=section;drawing::refresh_view_geometry(view,tube);
    require(std::ranges::any_of(view.projected_edges,[](const auto& e){return e.hatch&&!e.hidden;}),"Drawing section has no visible hatching");
    for(const auto& edge:view.projected_edges)if(!edge.hatch&&!edge.hidden)for(std::size_t i=1;i<edge.points.size();++i){const auto a=edge.points[i-1],b=edge.points[i];const double x=(a.x+b.x)/2,y=(a.y+b.y)/2;
        const bool outer=std::abs(std::abs(x)-5)<1e-6||std::abs(std::abs(y)-5)<1e-6;
        const bool inner=(std::abs(std::abs(x)-2)<1e-6&&std::abs(y)<=2+1e-6)||(std::abs(std::abs(y)-2)<1e-6&&std::abs(x)<=2+1e-6);
        require(outer||inner,"Section cap triangulation produced an internal visible edge");
    }
    for(const auto scale:{.5,1.,2.})for(const auto orientation:{drawing::ViewOrientation::Front,drawing::ViewOrientation::Isometric}){
        auto test=view;test.scale=scale;test.camera=drawing::standard_camera(orientation);for(const auto& p:document::calculate_section(tube,*test.section_snapshot).patches)test.section_snapshot->components[p.component]={0,{0,1.2,.1,0},true};drawing::refresh_view_geometry(test,tube);
        std::vector<double> levels;for(const auto& edge:test.projected_edges)if(edge.hatch){require(std::abs(edge.points.front().y-edge.points.back().y)*scale<1e-6,"Hatch angle changed in oblique view");levels.push_back(edge.points.front().y*scale);}
        std::ranges::sort(levels);levels.erase(std::unique(levels.begin(),levels.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),levels.end());require(levels.size()>2,"Insufficient hatch lines");for(std::size_t i=1;i<levels.size();++i)require(std::abs((levels[i]-levels[i-1])-1.2)<1e-6,"Paper hatch spacing depends on view scale");
    }
    // A slightly slanted cut must never tilt the chosen view camera.
    for(const double slope:{0.004646209938652496,0.5,-1.0})for(int orientation=0;orientation<7;++orientation)for(const bool reverse:{false,true}) {
        auto section=chain({{-20,-20*slope},{20,20*slope}});section.reversed=reverse;
        auto test=drawing::DrawingDocument::create_view(part.document_id,{},tube,static_cast<drawing::ViewOrientation>(orientation));
        const auto camera=test.camera;test.section_id=section.id;test.section_snapshot=section;
        drawing::refresh_view_geometry(test,tube);
        require(test.camera.horizontal==camera.horizontal&&test.camera.vertical==camera.vertical&&test.camera.depth==camera.depth,
            "Section geometry or side overrode the chosen view camera");
        test.section_id.clear();drawing::refresh_view_geometry(test,tube);
        require(test.camera.horizontal==camera.horizontal&&test.camera.vertical==camera.vertical&&test.camera.depth==camera.depth,
            "Removing the Section changed the view camera");
    }
    // Both sides of a through-cut must remain open when the view turns.
    // The source side is intentionally opposite; it is never written back.
    const auto hatch_count=[](const auto& v){return std::ranges::count_if(v.projected_edges,[](const auto& e){return e.hatch&&!e.hidden;});};
    auto automatic=view;automatic.section_snapshot=section;automatic.section_snapshot->reversed=true;
    const auto source_definition=document::serialize_sections({*automatic.section_snapshot});
    for(const auto orientation:{drawing::ViewOrientation::Front,drawing::ViewOrientation::Back,drawing::ViewOrientation::Isometric}){
        automatic.camera=drawing::standard_camera(orientation);
        drawing::refresh_view_geometry(automatic,tube);require(hatch_count(automatic)>0,"Turning the view concealed its automatic cut");
        require(document::serialize_sections({*automatic.section_snapshot})==source_definition,"Automatic side rewrote the source snapshot");
        const auto side=automatic.section_display_reversed;const auto count=hatch_count(automatic);drawing::refresh_view_geometry(automatic,tube);
        require(automatic.section_display_reversed==side&&hatch_count(automatic)==count,"Repeated regeneration toggled automatic section side");

    }
    automatic.camera=drawing::standard_camera(drawing::ViewOrientation::Top);drawing::refresh_view_geometry(automatic,tube);
    require(hatch_count(automatic)==0,"Edge-on section invented visible hatch area");
    for(const auto orientation:{drawing::ViewOrientation::Front,drawing::ViewOrientation::Back,drawing::ViewOrientation::Isometric}){
        automatic.camera=drawing::standard_camera(orientation);automatic.section_snapshot=step;automatic.section_id=step.id;drawing::refresh_view_geometry(automatic,tube);
        require(hatch_count(automatic)>0,"Offset section disappeared after a view turn");
    }
    // Arrow directions consume the actual cut side, including a preview,
    // without re-clipping geometry during paint or PDF export.
    automatic.section_snapshot=section;automatic.section_id=section.id;automatic.camera=drawing::standard_camera(drawing::ViewOrientation::Front);drawing::refresh_view_geometry(automatic,tube);
    auto marker=trace_view;marker.section_markers={section};automatic.section_parent_id=marker.id;
    std::vector<const drawing::DrawingView*> linked{&marker,&automatic};
    const auto front_trace=drawing::section_trace_layout(marker,section,linked);
    automatic.camera=drawing::standard_camera(drawing::ViewOrientation::Back);drawing::refresh_view_geometry(automatic,tube);
    const auto back_trace=drawing::section_trace_layout(marker,section,linked);
    require(front_trace&&back_trace&&front_trace->chain==back_trace->chain,"View turn moved the cutting trace");
    for(int end=0;end<2;++end)require(std::abs(front_trace->arrow_directions[end].x+back_trace->arrow_directions[end].x)+std::abs(front_trace->arrow_directions[end].y+back_trace->arrow_directions[end].y)<1e-9,"Cutting arrows disagree with the turned view");
    view.section_display_reversed=true;
    view.hidden_hatch_components.insert("component");view.section_snapshot->body_owners["feature"]="body";
    view.section_markers={section,step};draw.sheets.front().views.push_back(view);const auto directory=std::filesystem::temp_directory_path()/kernel::make_stable_id();std::filesystem::create_directory(directory);
    draw.save(directory/"section.drwz");auto loaded=drawing::DrawingDocument::load(directory/"section.drwz");require(loaded.sheets.front().views.front().section_id==section.id,"Drawing lost section link");
    const auto& saved_view=loaded.sheets.front().views.front();require(saved_view.section_display_reversed,"Drawing lost calculated section side");require(saved_view.section_markers.size()==2&&saved_view.section_markers.back().id==step.id,"Drawing lost selected section traces");require(saved_view.hidden_hatch_components.contains("component")&&saved_view.section_snapshot->body_owners.at("feature")=="body"&&saved_view.section_snapshot->components.at("1:b").mode==2,"Drawing mixed section defaults and component overrides");
    part.sections={section};part.save(directory/"section.prtz");require(document::PartDocument::load(directory/"section.prtz").sections.size()==1,"Part lost Section");
    auto asm_doc=assembly::AssemblyDocument::create_default();asm_doc.sections={section};asm_doc.save(directory/"section.asmz");require(assembly::AssemblyDocument::load(directory/"section.asmz").sections.size()==1,"Assembly lost Section");
    std::cout<<"Section areas, holes, reversed side, occurrence exclusions, hatching and persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
