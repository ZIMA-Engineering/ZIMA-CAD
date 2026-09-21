#include "workspace_internal.hpp"
#include <zima/workspace/component_properties.hpp>
#include <zima/workspace/component_source_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include "../file_dialog.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::select_container(const std::string& container_id) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    QTreeWidgetItem* fallback{};
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole).toString().toStdString() == container_id) {
            const auto role = item->data(0, Qt::UserRole + 3).toString();
            // A Sketch and its owned Plane intentionally share the Sketch
            // ID in the Tree. View selection of the complete Sketch must
            // synchronize to the Sketch row, not stop on its Plane sibling.
            if (role == QStringLiteral("part-sketch") ||
                role == QStringLiteral("assembly-sketch")) {
                selected_sketch_id_ = container_id;
                tree_->setCurrentItem(item);
                return;
            }
            if (fallback == nullptr) fallback = item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
    if (fallback != nullptr) tree_->setCurrentItem(fallback);
}

void AssemblyWorkspaceWindow::select_occurrence(const std::string& instance_path) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole + 1).toString().toStdString() == instance_path) {
            tree_->setCurrentItem(item);
            return;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
}

void AssemblyWorkspaceWindow::show_component_properties(
    const std::string& selected_path, bool start_reference_entry) {
    if (properties_dialog_ != nullptr) return;
    std::string instance_path;
    try{instance_path=workspace_.derived_source_path(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(selected_path)).encoded();}
    catch(const std::exception&){return;}
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    if (occurrence->source_missing) {
        const auto path = open_file(this, tr("Zdrojový soubor"),
            QString::fromStdString(zima::document::path_to_utf8(working_directory_)),
            occurrence->source_kind == zima::assembly::ComponentSourceKind::Assembly
                ? tr("Sestavy ZIMA-CAD (*.asmz)") : tr("Díly ZIMA-CAD (*.prtz)"));
        if (path.isEmpty()) return;
        try {
            zima::workspace::relink_component_source(workspace_, address->owner_assembly_document_id,
                address->occurrence_id, std::filesystem::u8path(path.toStdString()),
                [](auto task) { run_background_task(std::move(task)); });
            refresh_scene();
            show_component_properties(instance_path, start_reference_entry);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Zdrojový soubor"), tr(error.what()));
        }
        return;
    }
    std::string generic, initial_variant;
    std::filesystem::path source_file;
    std::vector<std::pair<std::string, std::string>> variants;
    try {
        const auto opened = zima::workspace::open_component_source(workspace_, workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
        generic = opened.document_id.substr(0, opened.document_id.find(":family:"));
        source_file = opened.path;
        if (!workspace_.find(generic))
            static_cast<void>(workspace::insert_native_document(workspace_, workspace::read_native_document(opened.path)));
        if (opened.document_id != generic) initial_variant = opened.document_id.substr(generic.size()+8);
        variants.emplace_back("", tr("Výchozí (nativní)").toStdString());
        for (const auto& row : workspace::family_table(workspace_, generic).instances)
            variants.emplace_back(row.id, row.name);
        assembly = workspace_.open_assembly(address->owner_assembly_document_id);
        occurrence = assembly->session.document().find_occurrence(address->occurrence_id);
    } catch (const std::exception& error) {
        state_->setText(tr(error.what())); return;
    }
    const auto selected_variant = std::make_shared<std::string>(initial_variant);
    const auto selected_generic = std::make_shared<std::string>(generic);
    const auto selected_file = std::make_shared<std::filesystem::path>(source_file);
    const auto edit = zima::workspace::prepare_component_edit(workspace_,address->owner_assembly_document_id,address->occurrence_id);
    auto* dialog = new ComponentPropertiesDialog(
        *occurrence,
        [this,edit,generic,initial_variant,selected_variant,selected_generic,selected_file](zima::assembly::PartOccurrence committed) {
            if (*selected_generic == generic && *selected_variant == initial_variant) {
                static_cast<void>(workspace::commit_component_properties(workspace_,edit,workspace::component_properties(committed)));
                return;
            }
            // Explicit OK: evaluate the chosen family in a private workspace.
            // Publish placement and source together as one undoable transaction.
            auto pending = workspace_;
            static_cast<void>(workspace::commit_component_properties(pending,edit,workspace::component_properties(committed)));
            if (!pending.find(*selected_generic)) {
                auto prepared = workspace::read_native_document(*selected_file,workspace::native_source_resolver(pending));
                if (prepared.id() != *selected_generic) throw std::runtime_error("The component source file belongs to a different document.");
                static_cast<void>(workspace::insert_native_document(pending,std::move(prepared)));
            }
            std::string source = *selected_generic;
            if (!selected_variant->empty()) {
                const auto table = workspace::family_table(pending,*selected_generic);
                const auto row = std::ranges::find(table.instances,*selected_variant,&document::FamilyInstance::id);
                if (row == table.instances.end()) throw std::runtime_error("Family variant no longer exists.");
                source = workspace::open_family_instance(pending,kernel_,*selected_generic,row->name,false);
            }
            static_cast<void>(workspace::replace_component(pending,kernel_,edit.document_id,edit.occurrence_id,source));
            auto final_document = pending.open_assembly(edit.document_id)->session.document();
            pending.open_assembly(edit.document_id)->session = workspace_.open_assembly(edit.document_id)->session;
            pending.open_assembly(edit.document_id)->session.commit(std::move(final_document));
            workspace_ = std::move(pending);
        }, this);
    dialog->set_variant_choices(variants,initial_variant,[selected_variant](std::string value) { *selected_variant=std::move(value); });
    const auto owner_directory = assembly->path.empty() ? working_directory_ : std::filesystem::absolute(assembly->path).parent_path();
    const auto show_source = [dialog,owner_directory](const std::filesystem::path& path,bool is_assembly) {
        auto shown = path.lexically_relative(owner_directory);
        if (shown.empty()) shown = path;
        dialog->set_source_display(QString::fromStdString(document::path_to_utf8(shown)),
            QString::fromStdString(document::path_to_utf8(path.filename())),is_assembly,assembly::is_skeleton_file(path));
    };
    show_source(source_file,occurrence->source_kind == assembly::ComponentSourceKind::Assembly);
    dialog->set_source_request_callback([this,dialog,show_source,selected_generic,selected_variant,selected_file] {
        const auto path = open_file(this,tr("Zdrojový soubor"),
            QString::fromStdString(document::path_to_utf8(selected_file->parent_path())),
            tr("Komponenty ZIMA-CAD (*.prtz *.asmz)"),application_settings_.translations);
        if (path.isEmpty()) return;
        try {
            const auto file = std::filesystem::absolute(std::filesystem::u8path(path.toStdString())).lexically_normal();
            auto inspection = workspace_;
            auto id = inspection.document_id_for_path(file);
            if (!id) id = workspace::insert_native_document(inspection,workspace::read_native_document(file,workspace::native_source_resolver(inspection)));
            const auto root = workspace::family_owner(inspection,*id);
            std::vector<std::pair<std::string,std::string>> choices{{"",tr("Výchozí (nativní)").toStdString()}};
            for (const auto& row : workspace::family_table(inspection,root).instances) choices.emplace_back(row.id,row.name);
            *selected_generic = root; *selected_file = file; selected_variant->clear();
            dialog->set_variant_choices(choices,"",[selected_variant](std::string value){*selected_variant=std::move(value);});
            show_source(file,inspection.open_assembly(root)!=nullptr);
        } catch (const std::exception& error) { dialog->set_placement_error(tr(error.what())); }
    });
    dialog->set_reference_request_callback(
        [this](std::size_t index, bool component_side) {
            start_component_placement_reference_selection(index, component_side);
        });
    const auto reference_scene_prefix =
        zima::assembly::InstancePath::decode(instance_path)
            .parent().value_or(zima::assembly::InstancePath{});
    dialog->set_reference_highlights_changed_callback(
        [this, dialog, reference_scene_prefix] {
            std::set<zima::viewer::EdgeKey> keys;
            for (const auto& reference : dialog->highlighted_references()) {
                auto path = reference_scene_prefix;
                for (const auto& occurrence_id :
                     reference.instance_path.occurrence_ids) {
                    path = path.child(occurrence_id);
                }
                keys.insert({reference.owner_id, reference.semantic_key,
                    path.encoded()});
            }
            viewer_->set_constraint_reference_highlights({}, std::move(keys));
        });
    dialog->set_reference_measure_callback([this,assembly_id=address->owner_assembly_document_id](const auto& component,const auto& row)->std::optional<double> {
        const auto* assembly=workspace_.open_assembly(assembly_id);if(!assembly)return {};
        auto geometry=assembly->session.document();auto* occurrence=geometry.find_occurrence(component.occurrence_id);if(!occurrence)return {};
        *occurrence=component;
        return geometry.measure_placement_reference(row);
    });
    dialog->set_reference_label_resolver(
        [geometry=assembly->session.document().build_scene().original_references](const auto& reference) {
            return reference_display_label(reference,geometry);
        });
    component_placement_dialog_ = dialog;
    component_placement_assembly_document_id_ = address->owner_assembly_document_id;
    component_placement_occurrence_id_ = address->occurrence_id;
    viewer_->set_component_origin_handle(zima::viewer::EdgeKey{
        occurrence->source_document_id + ":origin", "origin:point", instance_path});
    properties_dialog_ = dialog;

    properties_dialog_instance_path_ = instance_path;
    viewer_->set_editing_origin_visible(true);
    preserve_view_on_refresh_ = true;
    refresh_scene();
    dialog->set_preview_callback(
        [this, dialog, reference_scene_prefix, assembly_id = address->owner_assembly_document_id,
         occurrence_id = address->occurrence_id](const auto& preview) {
            const auto* assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            auto found = std::find_if(next.components.begin(), next.components.end(),
                [&](const auto& item) {
                    return item.occurrence_id == occurrence_id;
                });
            if (found == next.components.end()) return;
            *found = preview;
            try {
                next.calculate_placement_references();
                dialog->set_solved_placement(next.find_occurrence(occurrence_id)->placement,
                    next.component_constraint_state(occurrence_id));
                viewer_->set_mesh(reference_scene_prefix.occurrence_ids.empty() ? next.build_scene() :
                    workspace_.build_scene_with_assembly_override(workspace_.displayed_document_id(),
                        reference_scene_prefix, next), false);
            } catch (const std::exception& error) {
                dialog->set_placement_error(QObject::tr(error.what()));
            }
        });
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        properties_dialog_instance_path_.clear();
        component_placement_dialog_ = nullptr;
        component_placement_assembly_document_id_.clear();
        component_placement_occurrence_id_.clear();
        pending_component_placement_index_.reset();
        component_placement_auto_advance_ = false;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_editing_origin_visible(false);
        viewer_->set_component_origin_handle(std::nullopt);
        component_drag_document_.reset();component_drag_preview_.reset();
        component_drag_occurrence_id_.clear();component_drag_document_id_.clear();component_drag_instance_path_.clear();
        refresh_scene();
    });
    dialog->show();
    if (start_reference_entry)
        start_component_placement_reference_selection(0, true, true);
}

} // namespace zima::app
