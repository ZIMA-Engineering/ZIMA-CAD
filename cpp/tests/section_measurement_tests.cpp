#include "profile_solid_fixture.hpp"
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>
using namespace zima;using namespace drawing;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool section_line(const kernel::EdgeReference& ref){return ref.semantic_key.starts_with("drawing-section-line:");}
DimensionAttachment pick(const DrawingView& view,Point2 p) {
    MeasurementPicker picker;const auto candidates=picker.candidates(view,p,.01,{});
    const auto found=std::ranges::find_if(candidates,[](const auto& c){return c.attachment.kind==DimensionAttachmentKind::Line&&section_line(c.attachment.reference);});
    require(found!=candidates.end(),"Section straight boundary is not selectable");return found->attachment;
}
void value(const DrawingView& view,const DrawingDimension& d,double expected) {
    const auto result=evaluate_drawing_dimension(view,d);
    require(result.state==MeasurementState::Resolved&&!result.presentations.empty()&&std::abs(result.presentations.front().value-expected)<1e-7,"Section dimension has incorrect geometry");
}
void verify() {
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();
    part.history={test::rectangular_feature(part,{80,20,20})};
    auto mesh=kernel.evaluate_history(part.kernel_operations()).back().mesh;
    auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-60,0,60,0));
    DrawingView view;view.id="section-view";view.source_document_id=part.document_id;view.source_path="source.prtz";view.section_id=section.id;view.section_snapshot=section;
    view.camera={{1,0,0},{0,0,1},{0,-1,0}};refresh_view_geometry(view,mesh);
    auto dimension=make_drawing_dimension(view.id);
    dimension.attachments={pick(view,{13,-10}),pick(view,{13,10})};
    const auto identities=dimension.attachments;
    for(bool reversed:{false,true}) {
        auto d=dimension;if(reversed)std::swap(d.attachments[0],d.attachments[1]);
        value(view,d,20);place_drawing_dimension(view,d,0,{45,0});value(view,d,20);
        for(int i=0;i<3;++i){drag_drawing_dimension(view,d,0,0,{2,-1});value(view,d,20);}
    }
    // 3D section helpers retain their existing non-reference contract.
    const auto cut=document::calculate_section(mesh,section);
    require(!cut.patches.empty(),"Section patch missing");
    for(const auto& patch:cut.patches) {
        require(patch.boundary_sources.size()==patch.boundary.size(),"Section provenance was lost");
        for(const auto& boundary:patch.boundary)
            require(std::ranges::any_of(cut.mesh.edges,[&](const auto& edge){return !edge.reference.valid()&&edge.points==std::vector<kernel::Vec3>{boundary[0],boundary[1]};}),"Drawing measurement promoted a 3D cap reference");
    }
    // Full-face provenance is stable under tessellation order and cut-side changes.
    auto reordered=mesh;
    for(std::size_t i=0,j=reordered.triangle_references.size()-1;i<j;++i,--j) {
        std::swap(reordered.triangle_references[i],reordered.triangle_references[j]);
        for(int k=0;k<3;++k)std::swap(reordered.triangles[3*i+k],reordered.triangles[3*j+k]);
    }
    refresh_view_geometry(view,reordered);value(view,dimension,20);
    require(pick(view,{13,-10}).reference==identities[0].reference,"Tessellation order changed section identity");
    for(double angle:{0.,.63,1.5707963267948966}) {
        view.camera={{std::cos(angle),0,std::sin(angle)},{-std::sin(angle),0,std::cos(angle)},{0,-1,0}};
        refresh_view_geometry(view,mesh);value(view,dimension,20);
        const auto point=[&](double z){return Point2{13*std::cos(angle)+z*std::sin(angle),-13*std::sin(angle)+z*std::cos(angle)};};
        require(pick(view,point(-10)).reference==identities[0].reference,"Camera changed section identity");
    }
    view.camera={{1,0,0},{0,0,1},{0,1,0}};refresh_view_geometry(view,mesh);value(view,dimension,20);
    view.camera.depth={0,-1,0};
    for(double thickness:{24.,16.,20.}) {
        test::resize_rectangular_feature(part,part.history.front(),{80,20,thickness});
        mesh=kernel.evaluate_history(part.kernel_operations()).back().mesh;
        refresh_view_geometry(view,mesh);value(view,dimension,thickness);
        require(pick(view,{13,-thickness/2}).reference==identities[0].reference,"Source resize changed section identity");
    }
    // Missing intersections stay unresolved rather than rebinding to another face.
    view.section_snapshot->plane_origin.y=100;refresh_view_geometry(view,mesh);
    require(evaluate_drawing_dimension(view,dimension).state==MeasurementState::Unresolved,"Lost cut intersection retained a stale measurement");
    view.section_snapshot=section;refresh_view_geometry(view,mesh);value(view,dimension,20);
    auto doc=DrawingDocument::create_default();doc.sheets.front().views={view};doc.sheets.front().dimensions={dimension};
    const auto file=std::filesystem::temp_directory_path()/kernel::make_stable_id();
    doc.save(file);const auto reopened=DrawingDocument::load(file);std::filesystem::remove(file);
    require(reopened.sheets.front().dimensions.front()==dimension,"Native reopen changed section attachment or layout");
    value(reopened.sheets.front().views.front(),dimension,20);

    // A through-hole splits one face support into disjoint pickable intervals.
    auto hole=test::rectangular_feature(part,{12,8,40});hole.combine_mode=document::CombineMode::Subtract;part.history.push_back(hole);
    mesh=kernel.evaluate_history(part.kernel_operations()).back().mesh;refresh_view_geometry(view,mesh);
    const auto left=pick(view,{-15,10}),right=pick(view,{15,10});
    require(left.reference==right.reference,"One planar face lost its shared support across a hole");
    const auto gap=measurement_candidates(view,{0,10},.01,{});
    require(std::ranges::none_of(gap,[](const auto& c){return section_line(c.attachment.reference);}),"Section dimension picked an empty hole or invented a midpoint");
    // A nonplanar source face cannot be promoted from a straight sampled section.
    auto nonplanar=mesh;for(auto& ref:nonplanar.triangle_references)ref={"source","nonplanar-face",{}};
    refresh_view_geometry(view,nonplanar);
    require(std::ranges::none_of(view.measurement_geometry->curves,[](const auto& c){return section_line(c.source);}),"Nonplanar source face was promoted to straight section supports");
    refresh_view_geometry(view,mesh);
    auto lower=pick(view,{15,-10}),upper=pick(view,{15,10});
    auto hole_dimension=make_drawing_dimension(view.id);hole_dimension.attachments={lower,upper};value(view,hole_dimension,20);
    auto bent=section;bent.sketch=sketcher::Sketch::create_default();
    static_cast<void>(bent.sketch.add_segment(-60,0,0,0));static_cast<void>(bent.sketch.add_segment(0,0,60,5));
    view.section_snapshot=bent;refresh_view_geometry(view,mesh);
    const auto first_leg=pick(view,{-15,10}),second_leg=pick(view,{15,10});
    require(first_leg.reference!=second_leg.reference,"Different Section legs merged their identities");
    for(double x:{-15.,15.}){auto d=make_drawing_dimension(view.id);d.attachments={pick(view,{x,-10}),pick(view,{x,10})};value(view,d,20);}
    view.section_snapshot=section;
    auto repeated=mesh;repeated.original_references={};
    for(auto& ref:repeated.triangle_references)ref.instance_path="A";
    for(auto& edge:repeated.edges)edge.reference.instance_path="A";
    const auto offset=static_cast<std::uint32_t>(repeated.vertices.size());
    for(auto p:mesh.vertices){p.x+=120;repeated.vertices.push_back(p);}
    for(auto index:mesh.triangles)repeated.triangles.push_back(offset+index);
    for(auto ref:mesh.triangle_references){ref.instance_path="B";repeated.triangle_references.push_back(ref);}
    for(auto edge:mesh.edges){edge.reference.instance_path="B";for(auto& p:edge.points)p.x+=120;repeated.edges.push_back(std::move(edge));}
    refresh_view_geometry(view,repeated);
    const auto a=pick(view,{13,10}),b=pick(view,{133,10});
    require(a.reference.instance_path=="A"&&b.reference.instance_path=="B"&&a.reference.semantic_key==b.reference.semantic_key,"Repeated Section sources lost exact occurrence identity");
    view.section_snapshot->components["B"].mode=2;refresh_view_geometry(view,repeated);
    require(!resolve_dimension_attachment(view,b)&&resolve_dimension_attachment(view,a),"Excluded occurrence rebound to the other Section source");
    std::cout<<"Section measurements: exact 20 mm, both orders, placements/drags, rotated cameras, retained sides, tessellation order, three source edits, lost/restored cut, native reopen, holes, bent legs and occurrence exclusions passed\n";
}
}
int main(){try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
