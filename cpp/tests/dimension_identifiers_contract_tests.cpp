#include <zima/drawing/measurement_dimension.hpp>
#include <zima/document/document_session.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
zima::sketcher::Sketch sketch_with_zero_dimension() {
    auto sketch = zima::sketcher::Sketch::create_default();
    const auto first = zima::sketcher::Sketch::create_point(0,0);
    const auto second = zima::sketcher::Sketch::create_point(0,10);
    sketch.points = {first,second};
    sketch.dimensions.push_back(sketch.create_point_dimension(first.id,second.id,
        zima::sketcher::DimensionKind::DistanceX));
    return sketch;
}
template<class Document> void unique_catalog(const Document& document) {
    std::set<std::string> identifiers;
    for (const auto& parameter : document.dimension_parameters()) {
        const auto id = document.dimension_identifiers.identifier(parameter.owner_id,parameter.semantic_key);
        require(!id.empty(),"Dimension parameter has no identifier");
        require(identifiers.insert(id).second,"Two parameters share a document identifier");
    }
}
}
int main() {
    try {
        auto doc=zima::document::PartDocument::create_default();
        doc.history.clear();doc.sketches.clear();doc.history_order.clear();
        auto sketch=sketch_with_zero_dimension();
        const auto dimension_key="dimension:"+sketch.dimensions.front().id;
        doc.sketches.push_back(sketch);
        zima::document::DocumentSession session(doc);
        const auto first=session.document().dimension_identifiers.identifier(sketch.id,dimension_key);
        require(first=="d1","First dimension did not receive d1");
        require(sketch.dimensions.front().value==0,"Test must cover a zero dimension");
        auto next=session.document();
        auto cone=zima::document::PartDocument::create_cone_container();
        cone.cone.top_radius=0;
        next.history.push_back(cone);session.commit(next);
        const auto top=session.document().dimension_identifiers.identifier(cone.id,"parameter:top_radius");
        const auto zero=session.document().dimension_identifiers.identifier(cone.id,"parameter:placement:x");
        require(!top.empty()&&!zero.empty(),"Zero feature/placement dimensions were omitted");
        next=session.document();next.history.front().cone.top_radius=4;
        next.history.front().name="Renamed";session.commit(next);
        require(session.document().dimension_identifiers.identifier(cone.id,"parameter:top_radius")==top,
            "Value or name change renumbered a dimension");
        require(session.undo()&&session.undo(),"Undo failed");
        require(session.document().dimension_identifiers.identifier(cone.id,"parameter:top_radius")==top,
            "Undo lost allocation tombstones");
        require(session.is_dirty(),"Unsaved allocated numbers must survive saving after Undo");
        require(session.redo(),"Redo failed");
        require(session.document().dimension_identifiers.identifier(cone.id,"parameter:top_radius")==top,
            "Redo renumbered dimension");
        require(session.undo(),"Second Undo failed");
        next=session.document();auto other=zima::document::PartDocument::create_box_container();
        next.history.push_back(other);session.commit(next);
        require(session.document().dimension_identifiers.identifier(other.id,"parameter:placement:x")!=zero,
            "Branch after Undo reused an allocated identifier");
        require(session.document().dimension_identifiers.identifier(sketch.id,dimension_key)==first,
            "Feature edits renumbered sketch dimensions");
        unique_catalog(session.document());
        const auto directory=std::filesystem::temp_directory_path()/"zima-dimension-identifiers";
        std::filesystem::create_directories(directory);
        session.document().save(directory/"part.prtz");
        const auto loaded=zima::document::PartDocument::load(directory/"part.prtz");
        require(loaded.dimension_identifiers==session.document().dimension_identifiers,
            "Part save/load lost current or retired identifiers");
        auto copy=loaded;
        auto embedded=sketch_with_zero_dimension();
        auto sweep=zima::document::PartDocument::create_box_container();
        sweep.feature_kind=zima::document::FeatureKind::Sweep2D;
        sweep.sweep2d.path_sketch=embedded.serialized();sweep.sweep2d.thickness=0;
        copy.history.push_back(sweep);copy.synchronize_dimension_identifiers();
        require(!copy.dimension_identifiers.identifier(embedded.id,"dimension:"+embedded.dimensions.front().id).empty(),
            "Owned Sweep sketch dimension was omitted");
        auto point=zima::document::PartDocument::create_construction(zima::document::ConstructionKind::Point);
        copy.constructions.push_back(point);copy.synchronize_dimension_identifiers();
        require(copy.dimension_identifiers.identifier(point.id,"parameter:x")==
            copy.dimension_identifiers.identifier(point.id,"parameter:placement:x"),
            "Point editors assigned different numbers to the same coordinate");
        unique_catalog(copy);
        auto assembly=zima::assembly::AssemblyDocument::create_default();
        zima::kernel::OcctKernel kernel;
        auto part=zima::document::PartDocument::create_default();
        part.history.push_back(zima::document::PartDocument::create_box_container());
        auto occurrence=zima::assembly::AssemblyDocument::create_part_occurrence(
            "Part",part.document_id,"part.prtz",kernel.evaluate_history(part.kernel_operations()).back());
        occurrence.placement_references.emplace_back();
        occurrence.placement_references.front().offset=0;
        occurrence.placement_references.front().lower_limit=0;
        occurrence.placement_references.front().upper_limit=0;
        assembly.components.push_back(occurrence);assembly.sketches.push_back(sketch_with_zero_dimension());
        zima::assembly::AssemblySession assembly_session(assembly);
        const auto mate=assembly_session.document().dimension_identifiers.identifier(assembly.document_id,"placement-reference:"+occurrence.occurrence_id+":0");
        require(!mate.empty(),"Zero Assembly mate has no identifier");
        unique_catalog(assembly_session.document());
        auto assembly_next=assembly_session.document();
        assembly_next.components.front().placement_references.front().offset=12;
        assembly_session.commit(assembly_next);
        require(assembly_session.undo()&&assembly_session.redo(),"Assembly Undo/Redo failed");
        require(assembly_session.document().dimension_identifiers.identifier(assembly.document_id,"placement-reference:"+occurrence.occurrence_id+":0")==mate,
            "Assembly mate edit renumbered dimension");
        assembly_session.document().save(directory/"assembly.asmz");
        const auto assembly_loaded=zima::assembly::AssemblyDocument::load(directory/"assembly.asmz");
        require(assembly_loaded.dimension_identifiers==assembly_session.document().dimension_identifiers,
            "Assembly save/load lost identifiers");
        require(zima::document::DimensionIdentifiers{}.identifier("absent","parameter:x").empty(),
            "Read-only lookup allocated a dimension");
        auto drawing = zima::drawing::DrawingDocument::create_default();
        const auto view = zima::drawing::DrawingDocument::create_view(
            part.document_id, "part.prtz", occurrence.calculated_source->mesh);
        drawing.sheets.front().views.push_back(view);
        auto drawing_dimension=zima::drawing::make_drawing_dimension(view.id);drawing_dimension.id="drawing-dimension-1";
        const auto edge=occurrence.calculated_source->mesh.edges.front().reference;
        drawing_dimension.attachments={{zima::drawing::DimensionAttachmentKind::Line,edge},{zima::drawing::DimensionAttachmentKind::Line,edge}};
        drawing.sheets.front().dimensions.push_back(drawing_dimension);
        drawing.synchronize_dimension_identifiers();
        require(drawing.dimension_identifiers.identifier(drawing.document_id,
            "dimension:" + drawing_dimension.id) == "d1", "Zero Drawing dimension was omitted");
        drawing.sheets.front().dimensions.clear();
        drawing.synchronize_dimension_identifiers();
        drawing_dimension.id = "drawing-dimension-2";
        drawing.sheets.front().dimensions.push_back(drawing_dimension);
        drawing.synchronize_dimension_identifiers();
        require(drawing.dimension_identifiers.identifier(drawing.document_id,
            "dimension:" + drawing_dimension.id) == "d2", "Drawing reused a deleted dimension number");
        drawing.save(directory / "drawing.drwz");
        require(zima::drawing::DrawingDocument::load(directory / "drawing.drwz").dimension_identifiers ==
            drawing.dimension_identifiers, "Drawing save/load lost identifiers");
        bool rejected=false;
        try { static_cast<void>(zima::document::DimensionIdentifiers::from_serialized(
            R"({"next":2,"entries":[{"owner":"a","key":"x","number":1},{"owner":"b","key":"x","number":1}]})")); }
        catch(const std::exception&) { rejected=true; }
        require(rejected,"Duplicate persisted dimension numbers were accepted");
        std::cout<<"Document dimension identifier contracts passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
