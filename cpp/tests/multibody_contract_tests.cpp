#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/body_history.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <iostream>
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace zima;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void volume(const kernel::BodyResult& result, double expected) {
    require(std::abs(result.volume - expected) < 1e-7, "Unexpected body volume");
}
template<class Action> static void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid body graph was accepted");
}
static kernel::HistoryOperation box(const std::string& owner, const std::string& body,
    kernel::Vec3 placement = {}) {
    kernel::HistoryOperation operation{owner, kernel::BoxRequest{10, 10, 10}};
    operation.body.id = body;
    operation.body.translation = placement;
    return operation;
}

int main() {
    try {
        {
            auto part=document::PartDocument::create_default();document::BodyHistoryGraph history;
            const auto a=history.create_body("A");auto feature=document::PartDocument::create_box_container();
            part.history.push_back(feature);history.insert({document::PartHistoryKind::Feature,feature.id});
            const auto b=history.create_body("B");const auto cut=history.create_boolean("Cut",kernel::BodyCombination::Subtract,a,b);
            auto hidden=*history.find(a);hidden.visible=false;history.update_body(hidden);
            history.activate({});part.set_body_history(history);
            const auto unchanged=part.body_history.serialized();
            rejects([&]{part.erase_history_object(a);});
            require(part.body_history.serialized()==unchanged&&part.history.size()==1,"Rejected deletion changed source body");
            part.erase_history_object(cut);
            require(part.body_history.find(a)->visible,"Deleting Boolean left its released target hidden");
            require(part.body_history.available_before(part.body_history.order().size())==std::vector<std::string>({a,b}),"Deleting Boolean failed to restore input bodies");
            for(bool pattern:{false,true}) {
                auto graph=part.body_history;document::BodyHistory copy;copy.name=pattern?"Pattern":"Mirror";
                copy.derived_copy=document::DerivedCopyParameters{};copy.derived_copy->source_id=a;
                copy.derived_copy->resolved_plane={{0,0,0},{1,0,0}};
                if(pattern)copy.derived_copy->pattern=kernel::PatternRequest{};
                const auto id=graph.create_derived_copy(copy);part.set_body_history(graph);
                const auto before=part.body_history.serialized();rejects([&]{part.erase_history_object(a);});
                require(part.body_history.serialized()==before,"Dependent copy rejection modified source");
                part.erase_history_object(id);
                require(part.body_history.find(a)&&!part.body_history.find(id),"Deleting Mirror/Pattern deleted its source");
            }
            document::DocumentSession session(part);auto next=part;next.erase_history_object(a);session.commit(next,{});
            require(session.document().history.empty()&&!session.document().body_history.find(a)&&session.document().body_history.find(b),"Deleting Body left owned features or deleted sibling");
            session.undo();require(session.document().history.size()==1&&session.document().body_history.find(a),"Undo did not restore deleted Body");
            next.erase_history_object(b);require(next.body_history.order().empty(),"Cannot delete the last Body");
        }
        document::BodyHistoryGraph graph;
        rejects([&] { graph.insert({document::PartHistoryKind::Feature, "orphan"}); });
        const auto a = graph.create_body("Polotovar");
        graph.insert({document::PartHistoryKind::Feature, "box-a"});
        graph.insert({document::PartHistoryKind::Construction, "datum-a"});
        const auto b = graph.create_body("Nástroj");
        graph.insert({document::PartHistoryKind::Feature, "box-b"});
        graph.activate(a);
        graph.set_history_cursor(a, 1);
        graph.insert({document::PartHistoryKind::Sketch, "sketch-a"});
        require(graph.find(a)->entries[1].id == "sketch-a" && graph.find(b)->entries.size() == 1,
            "Insertion modified another body's history");
        require(graph.visible_context() == std::vector<std::string>{a},
            "Editing the first body exposes downstream bodies");
        graph.activate(b);
        require(graph.visible_context() == std::vector<std::string>({a,b}),
            "Editing the second body lost preceding context");
        require(graph.rollback_before("sketch-a") == document::BodyHistoryBoundary{a,1},
            "Rollback used a different body's history boundary");
        auto tool = *graph.find(b);
        tool.scope.placement.x = 5;
        graph.update_body(tool);
        const auto cut = graph.create_boolean("Odečet", kernel::BodyCombination::Subtract, a, b);
        const auto unchanged = graph.serialized();
        rejects([&] { graph.move_step(cut, 0); });
        require(graph.serialized() == unchanged, "Rejected body move modified the document");
        rejects([&] { graph.insert({document::PartHistoryKind::Feature, "box-a"}); });
        require(graph.serialized() == unchanged, "Rejected duplicate ownership modified the document");
        graph.set_insertion_cursor(1);
        const auto cursor_before_move = graph.serialized();
        graph.move_step(a, 0);
        require(graph.serialized() == cursor_before_move, "No-op move changed the insertion cursor");
        graph.set_insertion_cursor(graph.order().size());
        auto copy = document::BodyHistoryGraph::from_serialized(unchanged);
        require(copy.serialized() == unchanged && copy.find(a)->origin() == graph.find(a)->origin(),
            "Body history persistence lost ownership, cursor or origin identity");
        graph.activate({});
        require(graph.visible_context() == std::vector<std::string>{cut},
            "Document result exposes consumed Boolean inputs");
        const auto c = graph.create_body("Další těleso");
        require(graph.visible_context() == std::vector<std::string>({cut,c}),
            "Active body context duplicated consumed branches");
        graph.move_body(c, 0);
        require(graph.bodies().front().scope.id == c && graph.find_boolean(cut)->target_id == a,
            "Independent body reorder detached a Boolean from its body");
        const auto compiler = [](const document::PartHistoryEntry& entry) -> std::optional<kernel::HistoryOperation> {
            if (entry.kind != document::PartHistoryKind::Feature) return std::nullopt;
            return kernel::HistoryOperation{entry.id, kernel::BoxRequest{10,10,10}};
        };
        kernel::OcctKernel kernel;
        volume(kernel.evaluate_history(graph.compile(compiler)).back(), 500);

        auto part = document::PartDocument::create_default();
        auto first = document::PartDocument::create_box_container();
        auto second = document::PartDocument::create_box_container();
        first.box = {10,10,10}; second.box = {10,10,10};
        part.history = {first, second};
        document::BodyHistoryGraph part_graph;
        const auto first_body = part_graph.create_body("První těleso");
        part_graph.insert({document::PartHistoryKind::Feature, first.id});
        const auto second_body = part_graph.create_body("Druhé těleso");
        part_graph.insert({document::PartHistoryKind::Feature, second.id});
        auto second_definition = *part_graph.find(second_body);
        second_definition.scope.placement.x = 5;
        part_graph.update_body(second_definition);
        part.set_body_history(part_graph);
        {
            auto deleted = part;
            const auto other_body = *deleted.body_history.find(second_body);
            deleted.erase_history_object(first.id);
            deleted.validate_body_ownership();
            require(!deleted.find_container(first.id) && !deleted.body_history.owner(first.id),
                "Deleted feature remains in body ownership");
            require(deleted.body_history.find(first_body)->cursor == 0 &&
                *deleted.body_history.find(second_body) == other_body,
                "Deletion moved the wrong body cursor or changed another body");
            auto sketch = zima::sketcher::Sketch::create_default();
            auto extrusion = document::PartDocument::create_extrusion_container(sketch.id);
            sketch.owner_container_id = extrusion.id;
            deleted.sketches.push_back(sketch);
            deleted.history.push_back(extrusion);
            auto graph = deleted.body_history;
            graph.insert({document::PartHistoryKind::Feature, extrusion.id});
            deleted.set_body_history(graph);
            deleted.erase_history_object(extrusion.id);
            require(deleted.sketches.empty(), "Deleting a feature orphaned its owned Sketch");
            // Deleting only the profile preserves a repairable broken feature.
            deleted.history.push_back(extrusion);
            deleted.sketches.push_back(sketch);
            graph = deleted.body_history;
            graph.insert({document::PartHistoryKind::Feature, extrusion.id});
            deleted.set_body_history(graph);
            deleted.erase_history_object(sketch.id);
            require(deleted.find_container(extrusion.id) && deleted.kernel_operations().back().suppressed,
                "Missing profile blocked deletion or generated replacement geometry");
            const auto path = std::filesystem::temp_directory_path() /
                ("zima-delete-" + kernel::make_stable_id() + ".prtz");
            deleted.save(path, {});
            const auto reopened = document::PartDocument::load(path);
            std::filesystem::remove(path);
            reopened.validate_body_ownership();
            require(reopened.find_container(extrusion.id)->extrusion.sketch_id == sketch.id,
                "Save/reopen lost the broken profile reference");
        }
        {
            auto deleted = part;
            auto datum = document::PartDocument::create_construction(document::ConstructionKind::Point);
            deleted.constructions.push_back(datum);
            auto graph = deleted.body_history;
            graph.insert({document::PartHistoryKind::Construction, datum.id});
            deleted.set_body_history(graph);
            deleted.find_container(second.id)->placement.references = {{{}, datum.id, "point"}};
            deleted.resolve_constructions();
            deleted.erase_history_object(datum.id);
            deleted.resolve_constructions();
            deleted.validate_body_ownership();
            require(!deleted.find_construction(datum.id) && !deleted.body_history.owner(datum.id),
                "Referenced construction could not be deleted");
            require(!deleted.find_container(second.id)->placement.reference_valid &&
                deleted.find_container(second.id)->placement.references.front().owner_id == datum.id,
                "Deleted reference was not retained and marked broken");
        }
        auto framed = part;
        auto frame_graph = framed.body_history;
        auto frame_body = *frame_graph.find(second_body);
        frame_body.scope.placement = {20,30,40};
        frame_body.scope.placement.rotation_z = 90;
        frame_body.scope.placement.absolute_rotation_z = 90;
        frame_graph.update_body(frame_body);
        framed.set_body_history(frame_graph);
        framed.find_container(second.id)->placement.references = {
            {{}, frame_body.origin().id, "origin:point"}};
        framed.resolve_constructions();
        const auto near = [](kernel::Vec3 actual, kernel::Vec3 expected) {
            return std::abs(actual.x-expected.x) < 1e-7 && std::abs(actual.y-expected.y) < 1e-7 &&
                std::abs(actual.z-expected.z) < 1e-7;
        };
        const auto position = [&](const document::PartDocument& value) {
            const auto& placement = value.find_container(second.id)->placement;
            require(placement.reference_valid, "Body-frame placement failed to resolve");
            return kernel::Vec3{placement.x,placement.y,placement.z};
        };
        require(near(position(framed), {}), "Body origin did not resolve in its own local frame");
        framed.find_container(second.id)->placement.references = {
            {{}, framed.document_id + ":origin", "origin:point"}};
        framed.resolve_constructions();
        require(near(position(framed), {-30,20,-40}), "Document origin was transformed incorrectly into body frame");
        framed.find_container(first.id)->placement = {103,4,5};
        framed.find_container(second.id)->placement.references = {
            {{}, first.container_origin.id, "origin:point"}};
        framed.resolve_constructions();
        require(near(position(framed), {-26,-83,-35}), "Cross-body origin reference applied placement twice");
        require(framed.body_history.find(second_body)->dependencies == std::vector<std::string>{first_body},
            "Cross-body placement did not persist its dependency");
        rejects([&] { framed.body_history.move_body(second_body, 0); });
        auto surface = std::make_shared<kernel::SurfaceGeometry>();
        surface->origin = {20,31,40}; surface->axis = {0,1,0}; surface->radial = {-1,0,0};
        kernel::ViewerReferenceGeometry world_reference;
        kernel::FaceReference face;
        face.surface = surface;
        world_reference.triangle_references.push_back(face);
        const auto local_reference = framed.construction_reference_geometry_for(second.id, world_reference);
        const auto& local_surface = *local_reference.triangle_references.front().surface;
        require(near(local_surface.origin, {1,0,0}) && near(local_surface.axis, {1,0,0}) &&
            near(local_surface.radial, {0,1,0}) && near(surface->origin, {20,31,40}),
            "Analytic surface frame conversion is incorrect or mutated its shared source");
        auto located = part;
        auto projected_sketch = zima::sketcher::Sketch::create_default();
        projected_sketch.owner_container_id = second.id;
        projected_sketch.resolved_origin = {2,3,0};
        auto projected_point = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Point);
        projected_point.source_document_id = framed.document_id;
        projected_point.source_owner_id = first.id;
        projected_point.source_semantic_key = "source-point";
        projected_point.cached_points = {{0,0}};
        projected_sketch.add_external_reference(projected_point);
        kernel::ViewerReferenceGeometry projection_source;
        projection_source.points.push_back({{20,31,40},{first.id,"source-point",{}},{},false});
        const auto refresh_projection = [&](const document::PartDocument& value) {
            static_cast<void>(projected_sketch.refresh_external_references(value.document_id,
                value.sketch_reference_geometry_for(projected_sketch, projection_source)));
            const auto& reference = projected_sketch.external_references.front();
            require(!reference.broken && reference.cached_points.size()==1 &&
                reference.source_owner_id==first.id,"Projected reference lost its original owner");
            return reference.cached_points.front();
        };
        auto projected = refresh_projection(framed);
        require(std::hypot(projected[0]+1,projected[1]+3)<1e-7,
            "External point projection missed the Body or Sketch frame");
        auto shifted_target=framed;
        auto shifted_graph=shifted_target.body_history;
        auto shifted_body=*shifted_graph.find(second_body);
        shifted_body.scope.placement.x+=10;
        shifted_graph.update_body(shifted_body);shifted_target.set_body_history(shifted_graph);
        projected=refresh_projection(shifted_target);
        require(std::hypot(projected[0]+1,projected[1]-7)<1e-7,
            "External reference did not follow a changed target Body frame");
        require(near(projection_source.points.front().position,{20,31,40}),
            "Projection mutated its source document geometry");
        projection_source.points.front().position.y+=2;
        projected=refresh_projection(shifted_target);
        require(std::hypot(projected[0]-1,projected[1]-7)<1e-7,
            "External reference did not follow changed source geometry");
        const auto stored_projection=projected_sketch.serialized();
        require(zima::sketcher::Sketch::from_serialized(stored_projection).serialized()==stored_projection,
            "Projected external reference did not survive persistence");
        auto located_graph=located.body_history;
        auto located_a=*located_graph.find(first_body);
        located_a.scope.placement={100,0,0};located_graph.update_body(located_a);
        auto located_b=*located_graph.find(second_body);
        located_b.scope.placement.references={{{},first.container_origin.id,"origin:point"}};
        located_b.scope.placement.absolute_rotation_z=90;
        located_graph.update_body(located_b);located.set_body_history(located_graph);
        located.find_container(first.id)->placement={3,4,5};
        located.find_container(second.id)->placement.references={{{},located_b.origin().id,"origin:point"}};
        located.resolve_constructions();
        require(near(located.body_history.find(second_body)->scope.translation(),{103,4,5}) &&
            near(position(located),{}), "Referenced body placement or its child frame is incorrect");
        require(std::abs(located.body_history.find(second_body)->scope.placement.rotation_z-90)<1e-7,
            "Body placement lost manual rotation after resolving references");
        const auto located_definition=located.body_history.serialized();
        require(document::BodyHistoryGraph::from_serialized(located_definition).serialized()==located_definition,
            "Body placement references did not survive serialization");
        located.find_container(first.id)->placement.x=13;
        located.resolve_constructions();
        require(near(located.body_history.find(second_body)->scope.translation(),{113,4,5}) &&
            near(position(located),{}), "Body placement did not follow the changed source frame");
        located.resolve_constructions();
        require(near(located.body_history.find(second_body)->scope.translation(),{113,4,5}) &&
            near(located.body_history.find(second_body)->scope.rotation_degrees(),{0,0,90}) &&
            near(position(located),{}),
            "Repeated body placement resolution changed the resolved frame");
        auto self_placed=located;
        auto self_graph=self_placed.body_history;
        auto self_body=*self_graph.find(first_body);
        self_body.scope.placement.references={{{},first.container_origin.id,"origin:point"}};
        self_graph.update_body(self_body);self_placed.set_body_history(self_graph);
        rejects([&] { self_placed.resolve_constructions(); });
        auto cyclic_body=located;
        auto cyclic_graph=cyclic_body.body_history;
        auto cyclic_first=*cyclic_graph.find(first_body);
        cyclic_first.scope.placement.references={{{},located_b.origin().id,"origin:point"}};
        cyclic_graph.update_body(cyclic_first);cyclic_body.set_body_history(cyclic_graph);
        const auto cyclic_snapshot=cyclic_body.body_history.serialized();
        rejects([&] { cyclic_body.resolve_constructions(); });
        require(cyclic_body.body_history.serialized()==cyclic_snapshot,"Rejected body reference changed the document");
        auto part_boundaries = kernel.evaluate_history(part.kernel_operations());
        document::DocumentSession session(part, part_boundaries);
        require(session.rollback_boundary(second.id).has_value() &&
            !session.rollback_boundary(second.id)->input_body.has_value(),
            "Second body's first feature incorrectly inherited the first body as input");
        auto next = part;
        auto added = document::PartDocument::create_box_container();
        added.box = {2,2,2}; added.placement.x = 20;
        next.insert_history_entry(document::PartHistoryKind::Feature, added.id);
        next.history.push_back(added);
        auto next_boundaries = kernel.evaluate_history_incremental(next.kernel_operations(), part_boundaries);
        session.commit(next, next_boundaries);
        require(session.document().body_history.owner(added.id)->scope.id == second_body,
            "Part insertion did not use the active body's ownership");
        const auto context = session.body_context_mesh();
        double context_max_x = -1e9;
        for (const auto& vertex : context.vertices) context_max_x = std::max(context_max_x, vertex.x);
        // Part primitives are centered: local center 20 + half-length 1 + body offset 5.
        require(std::abs(context_max_x - 26) < 1e-7, "Active body display did not apply its placement exactly once");
        auto inspect_first = next;
        inspect_first.body_history.activate(first_body);
        document::DocumentSession first_context(inspect_first, next_boundaries);
        for (const auto& face : first_context.body_context_mesh().original_references.triangle_references)
            require(face.owner_id == first.id, "Active first body displayed downstream reference geometry");
        inspect_first.body_history.activate(second_body);
        inspect_first.body_history.set_history_cursor(second_body, 0);
        document::DocumentSession empty_second_context(inspect_first, next_boundaries);
        const auto prefix_context = empty_second_context.body_context_mesh();
        require(!prefix_context.vertices.empty(), "Empty active body lost preceding passive context");
        for (const auto& face : prefix_context.original_references.triangle_references)
            require(face.owner_id == first.id, "Body cursor at start displayed later own geometry");
        auto rollback = session.rollback_boundary(added.id);
        require(rollback && rollback->input_body, "Body-local rollback lost its preceding boundary");
        volume(*rollback->input_body, 1000);
        for (const auto& face : rollback->input_body->mesh.original_references.triangle_references)
            require(face.owner_id == second.id, "Body rollback leaked references from another body");
        require(session.undo() && !session.document().body_history.owner(added.id),
            "Undo did not restore body ownership");
        require(session.redo() && session.document().body_history.owner(added.id),
            "Redo did not restore body ownership");
        const auto part_path = std::filesystem::temp_directory_path() /
            ("zima-body-part-" + kernel::make_stable_id() + ".prtz");
        session.document().save(part_path, session.calculated_boundaries());
        std::vector<kernel::BodyResult> loaded_boundaries;
        const auto loaded = document::PartDocument::load(part_path, &loaded_boundaries);
        std::filesystem::remove(part_path);
        require(loaded.body_history.serialized() == session.document().body_history.serialized(),
            "Part file did not persist body histories and active body");
        document::DocumentSession loaded_session(loaded, loaded_boundaries);
        volume(*loaded_session.rollback_boundary(added.id)->input_body, 1000);
        auto unrelated_invalid = loaded;
        unrelated_invalid.history.front().feature_kind = document::FeatureKind::Extrusion;
        bool unrelated_rejected = false;
        try { static_cast<void>(unrelated_invalid.kernel_operations()); }
        catch (const std::exception&) { unrelated_rejected = true; }
        require(unrelated_rejected, "Invalid unrelated feature fixture unexpectedly compiled");
        document::DocumentSession inspect_cached(unrelated_invalid, loaded_boundaries);
        volume(*inspect_cached.rollback_boundary(added.id)->input_body, 1000);

        require(loaded_boundaries.back().body_boundaries.size() == 2,
            "Part file did not preserve independent body calculation snapshots");
        auto suppressed_part = next;
        suppressed_part.history.back().suppressed = true;
        const auto suppressed_result = kernel.evaluate_history_incremental(suppressed_part.kernel_operations(), next_boundaries);
        suppressed_part.save(part_path, suppressed_result);
        std::vector<kernel::BodyResult> suppressed_loaded;
        static_cast<void>(document::PartDocument::load(part_path, &suppressed_loaded));
        std::filesystem::remove(part_path);
        require(suppressed_loaded.back().body_boundaries.size() == 2,
            "Suppressed final feature discarded document body snapshots during persistence");
        volume(suppressed_loaded.back(), 2000);

        auto boolean_part = next;
        auto boolean_graph = boolean_part.body_history;
        const auto boolean_id = boolean_graph.create_boolean("Otisk", kernel::BodyCombination::Subtract,
            first_body, second_body);
        boolean_part.set_body_history(boolean_graph);
        const auto boolean_result = kernel.evaluate_history_incremental(boolean_part.kernel_operations(), next_boundaries);
        boolean_part.save(part_path, boolean_result);
        std::vector<kernel::BodyResult> boolean_loaded;
        auto loaded_boolean_part = document::PartDocument::load(part_path, &boolean_loaded);
        std::filesystem::remove(part_path);
        volume(boolean_loaded.back(), 500);
        require(loaded_boolean_part.body_history.find_boolean(boolean_id)->target_id == first_body,
            "Part roundtrip lost the explicit Boolean target");
        document::DocumentSession boolean_session(loaded_boolean_part, boolean_loaded);
        volume(*boolean_session.rollback_boundary(added.id)->input_body, 1000);
        const auto edit_inputs = boolean_session.boolean_edit_inputs(boolean_id);
        require(edit_inputs && edit_inputs->target_id == first_body && edit_inputs->tool_id == second_body,
            "Boolean edit lost its exact cached input owners");
        volume(edit_inputs->target, 1000);
        volume(edit_inputs->tool, 1008);
        require(!boolean_session.boolean_edit_inputs(second_body), "Body was treated as a Boolean edit");
        require(!document::DocumentSession(boolean_part).boolean_edit_inputs(boolean_id),
            "Uncalculated Boolean fabricated input geometry");
        require(loaded_boolean_part.body_history.order().back() == boolean_id &&
            loaded_boolean_part.body_history.find(second_body)->entries.size() == 2,
            "Boolean is not a standalone history step");
        auto edited_boolean = *boolean_graph.find_boolean(boolean_id);
        edited_boolean.operation = kernel::BodyCombination::Intersect;
        boolean_graph.update_boolean(edited_boolean);
        boolean_part.set_body_history(boolean_graph);
        const auto intersected = kernel.evaluate_history_incremental(boolean_part.kernel_operations(), boolean_result);
        volume(intersected.back(), 500);
        require(document::serialize_body_result(intersected.back().body_inputs.at(second_body)) ==
            document::serialize_body_result(boolean_result.back().body_inputs.at(second_body)),
            "Editing Boolean recalculated an unchanged body input");
        const auto valid_graph = boolean_graph.serialized();
        edited_boolean.tool_id = edited_boolean.target_id;
        rejects([&] { boolean_graph.update_boolean(edited_boolean); });
        rejects([&] { boolean_graph.move_step(boolean_id, 1); });
        require(boolean_graph.serialized() == valid_graph, "Rejected Boolean edit mutated history");
        boolean_session.commit(boolean_part, intersected);
        require(boolean_session.undo() &&
            boolean_session.document().body_history.find_boolean(boolean_id)->operation == kernel::BodyCombination::Subtract,
            "Undo did not restore Boolean operation");
        require(boolean_session.redo() &&
            boolean_session.document().body_history.find_boolean(boolean_id)->operation == kernel::BodyCombination::Intersect,
            "Redo did not restore Boolean operation");
        auto invalid_part = loaded;
        invalid_part.history.push_back(document::PartDocument::create_box_container());
        rejects([&] { static_cast<void>(invalid_part.kernel_operations()); });

        std::vector<kernel::HistoryOperation> operations{box("box-a", "a"), box("box-b", "b", {5,0,0})};
        auto boundaries = kernel.evaluate_history(operations);
        volume(boundaries.back(), 2000); // Overlap does not implicitly fuse independent bodies.
        require(boundaries.back().body_boundaries.size() == 2, "Missing independent histories");
        volume(boundaries.back().body_inputs.at("a"), 1000);
        volume(boundaries.back().body_inputs.at("b"), 1000);
        const auto& placed = boundaries.back().body_inputs.at("b").mesh;
        double minimum_x = std::numeric_limits<double>::infinity();
        for (const auto& vertex : placed.vertices) minimum_x = std::min(minimum_x, vertex.x);
        require(std::abs(minimum_x-5) < 1e-9, "Body placement was not applied exactly once");
        for (const auto& face : placed.original_references.triangle_references)
            require(face.owner_id == "box-b", "Placement changed original reference ownership");

        // A second kernel has no live OCCT cache: persisted branch snapshots
        // must suffice to leave A untouched while B changes.
        auto restored = document::load_body_result(document::serialize_body_result(boundaries.back()));
        const auto original_a = document::serialize_body_result(restored.body_boundaries.at("a").back());
        kernel::OcctKernel cold;
        operations[1].body.translation.x = 7;
        auto moved = cold.evaluate_history_incremental(operations, {restored});
        require(document::serialize_body_result(moved.back().body_boundaries.at("a").back()) == original_a,
            "Independent history changed while moving another body");
        volume(moved.back(), 2000);
        operations[1].body.translation.x = 5;
        kernel::HistoryOperation combine;
        combine.owner_id = "boolean-ab";
        combine.body.id = "boolean-ab";
        combine.body.target_id = "a";
        combine.body.source_id = "b";
        combine.body.combination = kernel::BodyCombination::Add;
        operations.push_back(combine);
        for (const auto [mode, expected] : {
                std::pair{kernel::BodyCombination::Add, 1500.0},
                std::pair{kernel::BodyCombination::Subtract, 500.0},
                std::pair{kernel::BodyCombination::Intersect, 500.0}}) {
            operations[2].body.combination = mode;
            auto combined = cold.evaluate_history_incremental(operations, moved);
            volume(combined.back(), expected);
            volume(combined.back().body_inputs.at("a"), 1000);
            volume(combined.back().body_inputs.at("b"), 1000);
            bool a = false, b = false;
            for (const auto& face : combined.back().mesh.original_references.triangle_references) {
                a |= face.owner_id == "box-a";
                b |= face.owner_id == "box-b";
            }
            require(a && b, "Boolean lost original source references");
            const auto saved = document::serialize_body_result(combined.back());
            require(document::serialize_body_result(document::load_body_result(saved)) == saved,
                "Body histories did not survive persistence");
            const auto reused = cold.evaluate_history_incremental(operations, combined);
            require(document::serialize_body_result(reused.back()) == saved,
                "Unchanged body document was not reused");
        }
        operations[2].body.combination = kernel::BodyCombination::Subtract;
        operations.push_back(box("box-c", "c", {0,0,5}));
        combine.owner_id = "boolean-next";
        combine.body.id = "boolean-next";
        combine.body.target_id = "boolean-ab";
        combine.body.source_id = "c";
        combine.body.combination = kernel::BodyCombination::Subtract;
        operations.push_back(combine);
        const auto chained = kernel.evaluate_history(operations);
        volume(chained.back(), 250);
        std::vector<kernel::BodyResult> restored_chain;
        for (const auto& boundary : chained)
            restored_chain.push_back(document::load_body_result(document::serialize_body_result(boundary)));
        const auto stable_c = document::serialize_body_result(restored_chain.back().body_inputs.at("c"));
        auto changed_chain = operations;
        changed_chain[1].body.translation.x = 2;
        kernel::OcctKernel reopened_chain;
        const auto changed_result = reopened_chain.evaluate_history_incremental(changed_chain, restored_chain);
        volume(changed_result.back(), 100);
        volume(changed_result.back().body_outputs.at("boolean-ab"), 200);
        require(document::serialize_body_result(changed_result.back().body_inputs.at("c")) == stable_c,
            "Editing an upstream Boolean tool changed an independent later body");
        changed_chain.back().body.combination = kernel::BodyCombination::Add;
        const auto added_result = reopened_chain.evaluate_history_incremental(changed_chain, changed_result);
        volume(added_result.back(), 1100);
        changed_chain.back().body.combination = kernel::BodyCombination::Intersect;
        volume(reopened_chain.evaluate_history_incremental(changed_chain, added_result).back(), 100);
        auto invalid = operations;
        invalid.back().body.target_id = "a";
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });
        invalid = operations;
        invalid.front().body.target_id = "c";
        invalid.front().body.combination = kernel::BodyCombination::Add;
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });
        invalid = operations;
        invalid.back().body.translation.z = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });
        invalid = operations;
        invalid.back().body.id.clear();
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });
        invalid = operations;
        invalid.back().operation = kernel::BooleanOperation::Subtract;
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });
        invalid = operations;
        invalid.back().owner_id = invalid.front().owner_id;
        rejects([&] { static_cast<void>(kernel.evaluate_history(invalid)); });

        auto rotated = box("rotated-box", "rotated", {30,40,50});
        rotated.body.rotation_degrees.z = 90;
        const auto rotation = kernel.evaluate_history({rotated}).back();
        volume(rotation, 1000);
        double min_x = 1e9, min_y = 1e9, min_z = 1e9;
        for (const auto& vertex : rotation.mesh.vertices) {
            min_x = std::min(min_x, vertex.x);
            min_y = std::min(min_y, vertex.y);
            min_z = std::min(min_z, vertex.z);
        }
        require(std::abs(min_x-20) < 1e-9 && std::abs(min_y-40) < 1e-9 && std::abs(min_z-50) < 1e-9,
            "Body rotation/translation composition is incorrect");

        operations.resize(3);
        operations[2].body.combination = kernel::BodyCombination::Intersect;
        operations[1].body.translation.x = 50;
        volume(kernel.evaluate_history(operations).back(), 0);
        operations[2].body.combination = kernel::BodyCombination::Subtract;
        operations[1].body.translation.x = 0;
        volume(kernel.evaluate_history(operations).back(), 0);

        // The source file disappears after the initial calculation. Rebuilding
        // unchanged A would fail, so this checks actual branch reuse, including
        // reuse after persistence and without a live OCCT cache.
        const auto path = std::filesystem::temp_directory_path() /
            ("zima-multibody-" + kernel::make_stable_id() + ".step");
        kernel.export_step({{kernel.make_box({10,10,10}), {}, {}}}, path.string());
        kernel::HistoryOperation imported{"import", kernel::StepRequest{path.string()}};
        imported.body.id = "import-body";
        std::vector<kernel::HistoryOperation> independent{imported, box("changed-box", "changed-body", {20,0,0})};
        auto imported_boundaries = kernel.evaluate_history(independent);
        std::filesystem::remove(path);
        for (auto& boundary : imported_boundaries)
            boundary = document::load_body_result(document::serialize_body_result(boundary));
        std::get<kernel::BoxRequest>(independent.back().primitive).height = 20;
        kernel::OcctKernel reopened;
        volume(reopened.evaluate_history_incremental(independent, imported_boundaries).back(), 3000);
        // A mold cut uses the persisted imported solid even after reopening
        // without its STEP file. Changing the stock must not reimport the tool.
        auto stock = box("mold-stock", "stock-body");
        stock.primitive = kernel::BoxRequest{20,20,20};
        kernel::HistoryOperation cavity;
        cavity.owner_id = "mold-cavity";
        cavity.body.id = "mold-cavity";
        cavity.body.target_id = "stock-body";
        cavity.body.source_id = "import-body";
        cavity.body.combination = kernel::BodyCombination::Subtract;
        std::vector<kernel::HistoryOperation> mold{stock, imported, cavity};
        const auto cut_mold = reopened.evaluate_history_incremental(mold, imported_boundaries);
        volume(cut_mold.back(), 7000);
        std::vector<kernel::BodyResult> restored_mold;
        for (const auto& boundary : cut_mold)
            restored_mold.push_back(document::load_body_result(document::serialize_body_result(boundary)));
        std::get<kernel::BoxRequest>(mold.front().primitive).height = 30;
        kernel::OcctKernel reopened_mold;
        const auto resized_mold = reopened_mold.evaluate_history_incremental(mold, restored_mold);
        volume(resized_mold.back(), 11000);
        require(document::serialize_body_result(resized_mold.back().body_inputs.at("import-body")) ==
                document::serialize_body_result(restored_mold.back().body_inputs.at("import-body")),
            "Resizing mold stock changed the persisted imported tool");
        std::cout << "Multi-body kernel contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
