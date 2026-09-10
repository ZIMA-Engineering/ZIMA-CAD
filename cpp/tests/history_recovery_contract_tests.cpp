#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
using namespace zima;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        kernel::OcctKernel kernel;
        std::vector<kernel::HistoryOperation> operations{
            {"first", kernel::BoxRequest{10, 10, 10}},
            {"broken", kernel::FilletRequest{{{"removed", "missing-edge", {}}}, 1}},
            {"later", kernel::BoxRequest{5, 5, 5}}};
        auto results = kernel.evaluate_history_recovering(operations);
        require(results.size() == 3, "Recovery lost history boundaries");
        require(results.front().calculation_errors.empty(), "Later failure tainted earlier feature");
        require(results.back().calculation_errors.contains("broken") &&
                results.back().calculation_errors.contains("later"), "Failure owners were lost");
        require(std::abs(results.back().volume - 1000) < 1e-8, "Valid preceding geometry disappeared");
        require(results.back().kernel_shape == results.front().kernel_shape, "Recovery used a stale final body");
        require(results.back().source_fingerprint == kernel::history_fingerprint(operations, 3), "Recovery fingerprint mismatch");
        bool rejected = false;
        try { static_cast<void>(kernel.evaluate_history_incremental(operations, results)); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "Strict evaluation accepted a failed cache");
        const auto packet = document::load_body_result(document::serialize_body_result(results.back()));
        require(packet.calculation_errors == results.back().calculation_errors, "Diagnostics did not persist");
        auto repaired = operations;
        repaired[1].primitive = kernel::FilletRequest{{{"first", "edge:x_max:y_min:z_max--x_max:y_min:z_min", {}}}, 1};
        auto recalculated = kernel.evaluate_history_recovering(repaired, results);
        require(recalculated.back().calculation_errors.empty(), "Repair reused failed cache");
        require(recalculated[1].volume < 1000, "Repaired fillet was not calculated");
        kernel::OcctKernel reopened_kernel;
        const auto failed_again = reopened_kernel.evaluate_history_recovering(operations, recalculated);
        require(!failed_again.back().kernel_shape.empty(), "Recovery retained a viewer-only intermediate cache");
        auto invalid_first = operations;
        invalid_first.front().primitive = kernel::BoxRequest{-1, 10, 10};
        const auto no_input = kernel.evaluate_history_recovering(invalid_first);
        require(no_input.back().calculation_errors.contains("first") && no_input.back().mesh.triangles.empty(), "Invalid first operation fabricated geometry");
        operations[0].body.id = operations[1].body.id = "body-a";
        operations[2].body.id = "body-b";
        operations[2].body.translation = {30, 0, 0};
        results = kernel.evaluate_history_recovering(operations);
        require(results.back().body_outputs.at("body-b").calculation_errors.empty(), "Independent body was blocked");
        require(std::abs(results.back().volume - 1125) < 1e-8, "Independent body or valid input disappeared");
        const auto restored = document::load_body_result(document::serialize_body_result(results.back()));
        require(restored.body_boundaries.at("body-a").back().calculation_errors.contains("broken"), "Branch diagnostics did not persist");
        operations[1].primitive = repaired[1].primitive;
        recalculated = kernel.evaluate_history_recovering(operations, results);
        require(recalculated.back().calculation_errors.empty(), "Body repair reused failed input/output cache");
        auto combined = operations;
        combined[1].primitive = kernel::FilletRequest{{{"removed", "missing-edge", {}}}, 1};
        kernel::HistoryOperation boolean{"combine", kernel::FeatureGroupRequest{}};
        boolean.body.id = "combined";
        boolean.body.combination = kernel::BodyCombination::Subtract;
        boolean.body.target_id = "body-a";
        boolean.body.source_id = "body-b";
        combined.push_back(boolean);
        const auto blocked_boolean = kernel.evaluate_history_recovering(combined);
        require(blocked_boolean.back().calculation_errors.contains("combine"), "Boolean consumed a failed source");
        require(std::abs(blocked_boolean.back().volume - 1125) < 1e-8, "Blocked Boolean hid its preceding inputs");
        auto part = document::PartDocument::create_default();
        auto first = document::PartDocument::create_box_container(); first.box = {10,10,10};
        auto profile = document::PartDocument::create_extrusion_container("missing-sketch");
        profile.extrusion.sketch_id = "missing-sketch";
        part.history = {first, profile};
        document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Preparation failure"));
        graph.insert({document::PartHistoryKind::Feature, first.id});
        graph.insert({document::PartHistoryKind::Feature, profile.id});
        part.set_body_history(graph);
        const auto prepared = kernel.evaluate_history_recovering(part.kernel_operations(false, true));
        require(prepared.back().calculation_errors.contains(profile.id) && std::abs(prepared.back().volume - 1000) < 1e-8,
                "Missing profile erased the preceding body");
        document::DocumentSession session(part);
        session.update_calculated_boundaries(prepared);
        require(!session.rollback_boundary(first.id)->input_body, "First feature acquired later geometry");
        require(std::abs(session.rollback_boundary(profile.id)->input_body->volume - 1000) < 1e-8, "Broken feature lost its real input");
        const auto temporary = std::filesystem::temp_directory_path() / (part.document_id + ".prtz");
        part.save(temporary, prepared);
        std::vector<kernel::BodyResult> loaded;
        const auto reloaded = document::PartDocument::load(temporary, &loaded);
        std::filesystem::remove(temporary);
        require(loaded.back().calculation_errors == prepared.back().calculation_errors && std::abs(loaded.back().volume - 1000) < 1e-8,
                "Part save/load discarded partial calculation");
        if (argc > 1) {
            auto part = document::PartDocument::load(argv[1]);
            const auto calculated = kernel.evaluate_history_recovering(part.kernel_operations(false, true));
            require(!calculated.empty(), "Model has no calculated boundaries");
            document::DocumentSession session(part);
            session.update_calculated_boundaries(calculated);
            require(!session.body_context_mesh().triangles.empty(), "Model has no visible valid body");
            for (const auto& [id, error] : calculated.back().calculation_errors)
                std::cout << id << ": " << error << '\n';
            std::cout << "Model volume: " << calculated.back().volume << '\n';
        }
        std::cout << "History recovery contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
