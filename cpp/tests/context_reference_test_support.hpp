#pragma once
#include "profile_solid_fixture.hpp"
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <cmath>
namespace zima::test_support {
struct ContextReferenceFixture {
    document::PartDocument source=document::PartDocument::create_default(),target=document::PartDocument::create_default();
    assembly::AssemblyDocument inner=assembly::AssemblyDocument::create_default(),top=assembly::AssemblyDocument::create_default();
    std::vector<kernel::BodyResult> calculated;
    kernel::ViewerEdge edge;
    assembly::InstancePath source_path,target_path,other_target_path;
    std::string sketch_id,reference_id,curve_id;
    std::filesystem::path directory;
    ContextReferenceFixture(const kernel::OcctKernel& kernel,const std::filesystem::path& dir):directory(dir) {
        auto box=zima::test::rectangular_feature(source,{10,10,10});source.history={box};
        calculated=kernel.evaluate_history(source.kernel_operations());
        edge.reference={box.id,"fixture-original-rational-edge",{}};edge.points={{1,0,0},{0,1,0}};
        edge.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
        calculated.back().mesh.original_references.edges.push_back(edge);
        calculated.back().mesh.edges.push_back(edge);
        auto container=document::PartDocument::create_sketch_container();auto sketch=sketcher::Sketch::create_default();
        sketch.owner_container_id=container.id;sketch_id=sketch.id;target.history={container};
        auto from=assembly::AssemblyDocument::create_part_occurrence("Source",source.document_id,"context-source.prtz",calculated.back());
        auto to=assembly::AssemblyDocument::create_part_occurrence("Target",target.document_id,"context-target.prtz",{});
        from.placement={10,20,0,0,0,90};to.placement={2,3,0,0,0,0};inner.components={from,to};
        inner.add_dependency(assembly::AssemblyDocument::create_dependency(to.occurrence_id,from.occurrence_id,assembly::ComponentDependencyKind::ExternalSketchReference));
        auto empty=sketcher::Sketch::create_default();assembly::AssemblyCut cut;
        cut.definition=document::PartDocument::create_extrusion_container(empty.id);cut.definition.combine_mode=document::CombineMode::Subtract;
        empty.owner_container_id=cut.definition.id;cut.target_occurrence_ids={from.occurrence_id};inner.sketches={empty};inner.cuts={cut};
        auto group=assembly::AssemblyDocument::create_assembly_occurrence("Group",inner.document_id,"context-inner.asmz",inner);
        auto repeated=assembly::AssemblyDocument::create_assembly_occurrence("Repeated",inner.document_id,"context-inner.asmz",inner);
        group.placement={30,40,50,10,20,30};repeated.placement={100,200,300,0,0,45};top.components={group,repeated};
        source_path={{group.occurrence_id,from.occurrence_id}};target_path={{group.occurrence_id,to.occurrence_id}};other_target_path={{repeated.occurrence_id,to.occurrence_id}};
        workspace::ReferenceFrame frame;frame.instance_prefix=source_path.encoded();
        frame.point=[](auto p){return kernel::Vec3{8-p.y,17+p.x,p.z};};frame.direction=[](auto p){return kernel::Vec3{-p.y,p.x,p.z};};
        frame.surface_point=[&](const auto&,auto p){return frame.point(p);};frame.surface_direction=[&](const auto&,auto p){return frame.direction(p);};
        kernel::ViewerReferenceGeometry geometry;const auto all=[](auto,const auto&,const auto&,const auto&){return true;};
        workspace::append_original_reference_geometry(geometry,calculated.back().mesh.original_references,frame,all);
        workspace::append_original_reference_geometry(geometry,source.origin_viewer_mesh().original_references,frame,all);
        const auto add=[&](sketcher::ExternalReferenceKind kind,const auto& identity) {
            auto r=sketcher::Sketch::create_external_reference(kind);r.source_document_id=source.document_id;
            r.source_owner_id=identity.owner_id;r.source_semantic_key=identity.semantic_key;r.source_instance_path=source_path.encoded();
            r.context_assembly_document_id=top.document_id;r.context_instance_path=target_path.encoded();
            workspace::populate_external_reference_cache(sketch,r,geometry);const auto id=r.id;sketch.add_external_reference(std::move(r));return id;
        };
        reference_id=add(sketcher::ExternalReferenceKind::Edge,edge.reference);curve_id=sketch.add_external_profile_geometry(reference_id);
        static_cast<void>(add(sketcher::ExternalReferenceKind::Point,geometry.points.front().reference));
        const auto axis=std::ranges::find_if(geometry.axes,[](const auto& a){return std::abs(a.direction.z)<.1;});
        if(axis==geometry.axes.end())throw std::runtime_error("Fixture has no projected origin axis");
        static_cast<void>(add(sketcher::ExternalReferenceKind::Axis,axis->reference));
        const auto face=std::ranges::find_if(geometry.triangle_references,[](const auto& f){return f.surface&&std::abs(f.surface->axis.y)>.9;});
        if(face==geometry.triangle_references.end())throw std::runtime_error("Fixture has no vertical plane");
        static_cast<void>(add(sketcher::ExternalReferenceKind::Face,*face));target.sketches={sketch};
        source.save(dir/"context-source.prtz",calculated);target.save(dir/"context-target.prtz",{});inner.save(dir/"context-inner.asmz");top.save(dir/"context-top.asmz");
    }
    void load(workspace::Workspace& live) const {
        live.add_part(source,calculated,directory/"context-source.prtz");live.add_part(target,{},directory/"context-target.prtz");
        live.add_assembly(inner,directory/"context-inner.asmz");live.add_assembly(top,directory/"context-top.asmz");
        live.activate(top.document_id);live.display_top_level(top.document_id);
    }
};
}
