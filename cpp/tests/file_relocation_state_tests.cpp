#include <zima/command_host/host.hpp>
#include <zima/document/file_relocation.hpp>
#include <zima/document/file_path.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace zima;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action>
void rejects(Action action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid relocation succeeded");
}
}
int main() {
    try {
        auto part = document::PartDocument::create_default();
        part.name = "original";
        part.history.push_back(document::PartDocument::create_box_container());
        kernel::OcctKernel kernel;
        const auto boundaries = kernel.evaluate_history(part.kernel_operations());
        require(!boundaries.empty() && boundaries.back().volume > 0, "No calculated source fixture");
        const auto root = fs::canonical(fs::temp_directory_path()) / ("zima-relocation-" + part.document_id);
        fs::create_directory(root); fs::create_directory(root / "nested");
        const auto old_part = root / fs::path(u8"původní díl.prtz");
        const auto new_part = root / fs::path(u8"nový díl.prtz");
        auto group = assembly::AssemblyDocument::create_default();
        const auto old_assembly = root / "nested" / "original.asmz";
        const auto new_assembly = root / "nested" / fs::path(u8"nová sestava.asmz");
        const auto relative = fs::path("..") / old_part.filename();
        auto first = assembly::AssemblyDocument::create_part_occurrence("first", part.document_id, relative, boundaries.back());
        auto second = assembly::AssemblyDocument::create_part_occurrence("second", part.document_id, old_part, boundaries.back());
        group.components = {first, second};
        auto drawing = drawing::DrawingDocument::create_default();
        const auto old_drawing = root / "original.drwz", new_drawing = root / fs::path(u8"nový výkres.drwz");
        drawing.source_document_id = part.document_id; drawing.source_path = old_part; drawing.source_name = "original";
        drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(
            part.document_id, old_part, boundaries.back().mesh));
        drawing::BomRow row; row.item_number = 1; row.name = "original row";
        row.source_document_id = part.document_id; row.source_path = old_part; row.file_stem = "original";
        drawing.sheets.front().bom_rows.push_back(row);
        auto unrelated = row; unrelated.item_number = 2; unrelated.source_document_id = "unrelated";
        unrelated.source_path = root / "unrelated.prtz"; unrelated.file_stem = "unrelated";
        drawing.sheets.front().bom_rows.push_back(unrelated);
        const std::array moves{
            document::FileRelocation{part.document_id, old_part, new_part},
            document::FileRelocation{group.document_id, old_assembly, new_assembly},
            document::FileRelocation{drawing.document_id, old_drawing, new_drawing}};
        document::DocumentSession session(part, boundaries);
        auto edited = part; edited.user_parameters["EDIT"] = "one"; session.commit(edited, boundaries);
        edited.user_parameters["EDIT"] = "two"; session.commit(edited, boundaries);
        require(session.undo(), "Part Undo fixture failed");
        const auto revision = session.revision(), generation = session.data_generation();
        const auto* cached = &session.calculated_boundaries().back();
        session.rebase_native_files(moves);
        require(session.revision() == revision && session.data_generation() == generation + 1 && session.is_dirty() &&
            session.document().name == document::path_to_utf8(new_part.stem()) && session.can_undo() && session.can_redo() &&
            cached == &session.calculated_boundaries().back() &&
            session.calculated_boundaries().back().source_fingerprint == boundaries.back().source_fingerprint &&
            session.document().document_id == part.document_id, "Part relocation lost identity, cache, dirty state or history");
        const auto no_op_generation = session.data_generation(); session.rebase_native_files(moves);
        require(session.data_generation() == no_op_generation, "Repeated rebase created a false change");
        require(session.undo() && !session.is_dirty() && session.document().name == document::path_to_utf8(new_part.stem()) &&
            !session.document().user_parameters.contains("EDIT") &&
            session.calculated_boundaries().back().volume == boundaries.back().volume, "Part Undo restored old file metadata or lost body");
        require(session.redo() && session.document().user_parameters.at("EDIT") == "one" &&
            session.redo() && session.document().user_parameters.at("EDIT") == "two", "Part Redo was cleared or altered");
        session.document().save(new_part, session.calculated_boundaries());
        std::vector<kernel::BodyResult> loaded_body;
        const auto loaded_part = document::PartDocument::load(new_part, &loaded_body);
        require(loaded_part.name == session.document().name && loaded_part.document_id == part.document_id &&
            loaded_body.back().source_fingerprint == boundaries.back().source_fingerprint &&
            loaded_body.back().mesh.original_references.triangle_references == boundaries.back().mesh.original_references.triangle_references,
            "Relocated Part native file changed topology or calculated geometry");

        assembly::AssemblySession assembly_session(group);
        auto edited_group = group; edited_group.components[0].name = "first edit"; assembly_session.commit(edited_group);
        edited_group.components[0].name = "second edit"; assembly_session.commit(edited_group); require(assembly_session.undo(), "Assembly Undo fixture failed");
        const auto assembly_revision = assembly_session.revision(), assembly_generation = assembly_session.data_generation();
        const auto shared = assembly_session.document().components[0].calculated_source;
        assembly_session.rebase_native_files(moves, old_assembly);
        require(assembly_session.revision() == assembly_revision && assembly_session.data_generation() == assembly_generation + 1 &&
            assembly_session.is_dirty() && assembly_session.can_undo() && assembly_session.can_redo() &&
            assembly_session.document().name == document::path_to_utf8(new_assembly.stem()), "Assembly rebase reset editing state");
        const auto check_group = [&] {
            for (const auto& component : assembly_session.document().components)
                require(component.source_path == new_part && component.source_document_id == part.document_id&&component.name==document::path_to_utf8(new_part.stem()),
                    "Assembly restored an obsolete source path/name/identity");
            require(assembly_session.document().components[0].calculated_source.shares_with(shared),
                "Assembly rebase copied or rebuilt its source geometry");
        };
        check_group();
        require(assembly_session.undo() && !assembly_session.is_dirty(), "Assembly clean history state lost"); check_group();
        require(assembly_session.redo(), "Assembly first Redo state lost"); check_group();
        require(assembly_session.redo(), "Assembly second Redo state lost"); check_group();
        assembly_session.document().save(new_assembly);
        const auto loaded_group = assembly::AssemblyDocument::load(new_assembly);
        require(loaded_group.document_id == group.document_id && loaded_group.name == document::path_to_utf8(new_assembly.stem()) &&
            loaded_group.components[0].source_path == new_part &&
            loaded_group.components[0].occurrence_id == first.occurrence_id &&
            loaded_group.components[1].occurrence_id == second.occurrence_id, "Native Assembly rebase changed occurrence identity");

        workspace::DrawingState drawing_state(drawing, old_drawing);
        auto edited_drawing = drawing; edited_drawing.sheets.front().bom_rows.front().name = "first edit"; drawing_state.commit(edited_drawing);
        edited_drawing.sheets.front().bom_rows.front().name = "second edit"; drawing_state.commit(edited_drawing);
        require(drawing_state.undo(), "Drawing Undo fixture failed");
        const auto drawing_revision = drawing_state.revision(), drawing_generation = drawing_state.data_generation();
        const auto projection = drawing_state.document().sheets.front().views.front().measurement_geometry;
        drawing_state.rebase_native_files(moves);
        require(drawing_state.revision() == drawing_revision && drawing_state.data_generation() == drawing_generation + 1 &&
            drawing_state.is_dirty() && drawing_state.can_undo() && drawing_state.can_redo() && drawing_state.path == old_drawing,
            "Drawing rebase changed its path before the filesystem transaction or lost editing state");
        const auto check_drawing = [&] {
            const auto& value = drawing_state.document(); const auto& sheet = value.sheets.front();
            require(value.document_id == drawing.document_id && value.name == document::path_to_utf8(new_drawing.stem()) &&
                value.source_path == new_part && value.source_name == document::path_to_utf8(new_part.stem()) &&
                sheet.views.front().source_path == new_part && sheet.views.front().source_document_id == part.document_id &&
                sheet.views.front().measurement_geometry == projection &&
                sheet.bom_rows.front().source_path == new_part && sheet.bom_rows.front().file_stem == document::path_to_utf8(new_part.stem()) &&
                sheet.bom_rows[1].source_path == unrelated.source_path && sheet.bom_rows[1].file_stem == "unrelated",
                "Drawing lost its root/view/BOM source reference, unrelated row or shared projection");
        };
        check_drawing(); require(drawing_state.undo() && !drawing_state.is_dirty(), "Drawing clean history state lost"); check_drawing();
        require(drawing_state.redo() && drawing_state.document().sheets.front().bom_rows.front().name == "first edit", "Drawing Undo content changed"); check_drawing();
        require(drawing_state.redo() && drawing_state.document().sheets.front().bom_rows.front().name == "second edit", "Drawing Redo content changed"); check_drawing();
        drawing_state.document().save(new_drawing);
        const auto loaded_drawing = drawing::DrawingDocument::load(new_drawing);
        require(loaded_drawing.document_id == drawing.document_id && loaded_drawing.source_path == new_part &&
            loaded_drawing.sheets.front().views.front().id == drawing.sheets.front().views.front().id &&
            loaded_drawing.sheets.front().bom_rows.front().source_path == new_part, "Native Drawing rebase lost IDs or source paths");

        // A conflict found in an older Undo state must leave every queued edit untouched.
        auto bad_group = group; bad_group.components[1].source_document_id = "wrong-source";
        assembly::AssemblySession bad(bad_group); bad.commit(group);
        const auto bad_generation = bad.data_generation(), bad_revision = bad.revision();
        rejects([&] { bad.rebase_native_files(moves, old_assembly); });
        require(bad.document().name == group.name && bad.document().components[0].source_path == relative &&
            bad.data_generation() == bad_generation && bad.revision() == bad_revision && bad.can_undo(),
            "Rejected historical reference conflict partially changed current metadata");
        require(bad.undo() && bad.document().components[1].source_document_id == "wrong-source" &&
            bad.document().components[0].source_path == relative, "Rejected relocation damaged historical references");
        const std::array duplicated{moves[0], moves[0]};
        rejects([&] { session.rebase_native_files(duplicated); });
        const std::array invalid{document::FileRelocation{part.document_id, "relative.prtz", new_part}};
        rejects([&] { session.rebase_native_files(invalid); });
        const auto unchanged = session.data_generation();
        session.rebase_native_files({});
        require(session.data_generation() == unchanged, "Empty relocation changed the session");


        // Prepare a single batch for several open documents without copying any geometry.
        document::DocumentSession batch_part(part, boundaries);
        assembly::AssemblySession batch_assembly(group);
        workspace::DrawingState batch_drawing(drawing, old_drawing);
        const auto* batch_body = &batch_part.calculated_boundaries().back();
        document::FileRelocationEdits batch(moves);
        batch_part.prepare_native_file_rebase(batch);
        batch_assembly.prepare_native_file_rebase(batch, old_assembly);
        batch_drawing.prepare_native_file_rebase(batch);
        require(batch_part.document().name == part.name && batch_assembly.document().components[0].source_path == relative &&
            batch_drawing.document().source_path == old_part && batch_part.data_generation() == 0 &&
            batch_assembly.data_generation() == 0 && batch_drawing.data_generation() == 0,
            "Preparing a cross-document batch changed live metadata");
        batch.apply();
        require(batch_part.document().name == document::path_to_utf8(new_part.stem()) &&
            batch_assembly.document().components[0].source_path == new_part && batch_drawing.document().source_path == new_part &&
            batch_part.data_generation() == 1 && batch_assembly.data_generation() == 1 && batch_drawing.data_generation() == 1 &&
            batch_body == &batch_part.calculated_boundaries().back() &&
            !batch_part.is_dirty() && !batch_assembly.is_dirty() && !batch_drawing.is_dirty(),
            "Cross-document publication copied geometry, changed dirty state or missed a generation");
        batch.apply();
        require(batch_part.data_generation() == 1 && batch_drawing.data_generation() == 1,
            "Completed metadata batch could be applied twice");
        document::DocumentSession refused_part(part, boundaries);
        document::FileRelocationEdits refused(moves);
        refused_part.prepare_native_file_rebase(refused);
        rejects([&] { bad.prepare_native_file_rebase(refused, old_assembly); });
        require(refused_part.document().name == part.name && refused_part.data_generation() == 0,
            "A later document conflict changed an earlier document");

        // Closed snapshots use the same typed metadata rules, preserving saved edits.
        part.save(old_part, boundaries); group.save(old_assembly); drawing.save(old_drawing);
        for (const auto& move : moves) {
            auto prepared = workspace::read_native_document(move.from);
            require(prepared.id() == move.document_id, "Closed fixture identity changed");
            require(prepared.is_drawing_for(part.document_id) == (move.from == old_drawing) &&
                !prepared.is_drawing_for("unrelated") && !prepared.is_drawing_for(""),
                "Companion ownership was inferred from a filename or unrelated document");
            require(prepared.rebase_native_files(moves) && !prepared.rebase_native_files(moves),
                "Closed snapshot rebase failed or repeated a completed change");
            const auto output = root / "staged" / move.to.filename();
            fs::create_directories(output.parent_path());
            prepared.write(output);
            require(workspace::read_native_document(output).id() == move.document_id,
                "Closed snapshot write changed native document identity");
        }
        const auto staged_part = document::PartDocument::load(root / "staged" / new_part.filename(), &loaded_body);
        require(staged_part.name == document::path_to_utf8(new_part.stem()) && !staged_part.user_parameters.contains("EDIT") &&
            loaded_body.back().source_fingerprint == boundaries.back().source_fingerprint &&
            session.document().user_parameters.at("EDIT") == "two",
            "Closed snapshot staging saved unrelated live edits or lost calculated geometry");
        const auto staged_assembly = assembly::AssemblyDocument::load(root / "staged" / new_assembly.filename());
        const auto staged_drawing = drawing::DrawingDocument::load(root / "staged" / new_drawing.filename());
        require(staged_assembly.components[0].source_path == new_part &&
            staged_drawing.source_path == new_part && staged_drawing.sheets.front().bom_rows[0].source_path == new_part &&
            assembly::AssemblyDocument::load(old_assembly).components[0].source_path == relative &&
            drawing::DrawingDocument::load(old_drawing).source_path == old_part,
            "Private staging failed to update references or modified original files");
        require(fs::canonical(root).parent_path() == fs::canonical(fs::temp_directory_path()), "Unsafe fixture cleanup");
        fs::remove_all(root);
        std::cout << "Native file metadata, stable IDs, cached geometry, history and atomic rejection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
