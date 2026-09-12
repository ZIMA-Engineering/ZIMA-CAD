#include "workspace_internal.hpp"
#include <zima/workspace/metadata_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::regenerate_active_document() {
    if(workspace_.open_drawing(workspace_.displayed_document_id())!=nullptr) {
        if(auto* action=drawing_workspace_->findChild<QAction*>("regenerateDrawingViewAction");action&&action->isEnabled())action->trigger();
    } else if(workspace_.open_part(workspace_.active_document_id())!=nullptr) {
        regenerate_active_part();
    } else if(workspace_.open_assembly(workspace_.displayed_document_id())!=nullptr) {
        regenerate_assembly();
    }
}

void AssemblyWorkspaceWindow::refresh_drawing_tree() {
    if (!drawing_workspace_ || !workspace_.open_drawing(workspace_.displayed_document_id())) return;
    const auto& document=drawing_workspace_->document_for_test();
    const auto* state=workspace_.open_drawing(document.document_id); if (!state) return;
    const QSignalBlocker blocker(tree_);
    tree_->clear(); tree_->setHeaderLabels({tr("VÝKRES")});
    auto* root=new QTreeWidgetItem(tree_,{QString::fromStdString(state->path.empty()?document.name:state->path.filename().string())});
    root->setIcon(0,resource_icon("drawing"));
    for(const auto& sheet:document.sheets) {
        auto* sheet_item=new QTreeWidgetItem(root,{QString::fromStdString(sheet.name)});
        for(const auto& view:sheet.views) {
            auto* item=new QTreeWidgetItem(sheet_item,{QString::fromStdString(view.name)});
            item->setData(0,Qt::UserRole,QString::fromStdString(view.id));
            item->setData(0,Qt::UserRole+3,"drawing-view");
        }
        sheet_item->setExpanded(true);
    }
    root->setExpanded(true); tree_->setRootIndex(QModelIndex{});
}

void AssemblyWorkspaceWindow::edit_document_parameters() {
    edit_parameters_for_document(workspace_.active_document_id());
}

void AssemblyWorkspaceWindow::edit_parameters_for_document(std::string active_id) {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    if (const auto* drawing = workspace_.open_drawing(active_id)) {
        const auto source_id = drawing->document().source_document_id;
        auto source_path = drawing->document().source_path;
        if (source_path.is_relative() && !drawing->path.empty())
            source_path = drawing->path.parent_path() / source_path;
        active_id = source_id;
        try {
            if (!workspace_.find(active_id) && !source_path.empty()) {
                if (const auto open = workspace_.document_id_for_path(source_path)) active_id = *open;
                else if (source_path.extension() == ".prtz") {
                    std::vector<zima::kernel::BodyResult> cache;
                    auto source = zima::document::PartDocument::load(source_path, &cache);
                    if (!source_id.empty() && source.document_id != source_id)
                        throw std::runtime_error("Zdroj výkresu patří jinému dokumentu.");
                    active_id = source.document_id;
                    workspace_.add_part(std::move(source), std::move(cache), source_path);
                } else if (source_path.extension() == ".asmz") {
                    auto source = zima::assembly::AssemblyDocument::load(source_path);
                    if (!source_id.empty() && source.document_id != source_id)
                        throw std::runtime_error("Zdroj výkresu patří jinému dokumentu.");
                    active_id = source.document_id;
                    workspace_.add_assembly(std::move(source), source_path);
                }
                refresh_tabs();
            }
            if (!workspace_.find(active_id))
                throw std::runtime_error("Nejprve zvolte zdrojový díl nebo sestavu ve vlastnostech pohledu.");
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Parametry zdroje výkresu"), error.what());
            return;
        }
    }
    if(!workspace_.open_part(active_id) && !workspace_.open_assembly(active_id))return;
    auto data=zima::workspace::user_parameters(workspace_,active_id);
    auto* dialog = new UserParametersDialog(std::move(data),
        application_settings_.language, [this, active_id](UserParameterData values) {
            static_cast<void>(zima::workspace::set_user_parameters(workspace_,active_id,std::move(values)));
            refresh_tabs();
        }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_material() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id();
    DocumentToolData data;
    if (const auto* part = workspace_.open_part(id)) {
        const auto& document = part->session.document();
        data = {document.document_units, document.document_precision,
            document.physical_parameters, document.physical_parameter_units,
            document.material_parameter_descriptions, document.family_table};
    } else if (const auto* assembly = workspace_.open_assembly(id)) {
        const auto& document = assembly->session.document();
        data = {document.document_units, document.document_precision,
            document.physical_parameters, document.physical_parameter_units,
            document.material_parameter_descriptions, document.family_table};
    } else return;
    auto accepted = [this, id](DocumentToolData values) {
        if (auto* part = workspace_.open_part(id)) {
            auto next = part->session.document();
            next.physical_parameters = std::move(values.physical_parameters);
            next.physical_parameter_units = std::move(values.physical_parameter_units);
            next.material_parameter_descriptions = std::move(values.descriptions);
            part->session.commit(std::move(next), part->session.calculated_boundaries());
        } else if (auto* assembly = workspace_.open_assembly(id)) {
            auto next = assembly->session.document();
            next.physical_parameters = std::move(values.physical_parameters);
            next.physical_parameter_units = std::move(values.physical_parameter_units);
            next.material_parameter_descriptions = std::move(values.descriptions);
            assembly->session.commit(std::move(next));
        }
        refresh_tabs();
    };
    auto* dialog = new MaterialDialog(std::move(data), std::move(accepted), application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_relations() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id();
    std::map<std::string, std::string> parameters;
    std::vector<zima::document::ModelRelation> relations;
    std::map<std::string, double> model_values;
    int decimal_places = 3;
    if (const auto* part = workspace_.open_part(id)) {
        const auto& document = part->session.document();
        parameters = document.user_parameters; relations = document.relations;
        try { decimal_places = std::stoi(document.document_precision.at("decimal_places")); } catch (...) {}
        model_values = zima::document::physical_values(document,part->session.calculated_boundaries());
    } else if (const auto* assembly = workspace_.open_assembly(id)) {
        const auto& document = assembly->session.document();
        parameters = document.user_parameters; relations = document.relations;
        try { decimal_places = std::stoi(document.document_precision.at("decimal_places")); } catch (...) {}
        model_values = zima::assembly::physical_values(document);
    }
    else return;
    auto* dialog = new RelationsDialog(std::move(parameters), std::move(relations),
        [this, id](auto values, auto next_relations) {
            const auto sync_relation_targets = [](auto& document, const auto& parameters,
                                                   const auto& relations) {
                for (const auto& relation : relations) {
                    const auto value = parameters.find(relation.target);
                    if (value == parameters.end()) continue;
                    document.user_parameter_values[relation.target][""] = value->second;
                    if (std::find(document.user_parameter_order.begin(),
                            document.user_parameter_order.end(), relation.target) ==
                        document.user_parameter_order.end()) {
                        document.user_parameter_order.push_back(relation.target);
                    }
                }
            };
            if (auto* part = workspace_.open_part(id)) { auto next = part->session.document(); sync_relation_targets(next, values, next_relations); next.user_parameters = std::move(values); next.relations = std::move(next_relations); part->session.commit(std::move(next), part->session.calculated_boundaries()); }
            else if (auto* assembly = workspace_.open_assembly(id)) { auto next = assembly->session.document(); sync_relation_targets(next, values, next_relations); next.user_parameters = std::move(values); next.relations = std::move(next_relations); assembly->session.commit(std::move(next)); }
            refresh_tabs();
        }, application_settings_, this, std::move(model_values), decimal_places);
    if (const auto* part = workspace_.open_part(id)) {
        const auto& document = part->session.document();
        dialog->set_dimension_catalog(document.dimension_parameters(), document.dimension_identifiers);
    } else if (const auto* assembly = workspace_.open_assembly(id)) {
        const auto& document = assembly->session.document();
        dialog->set_dimension_catalog(document.dimension_parameters(), document.dimension_identifiers);
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_family_table() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id(); DocumentToolData data; QString name;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else return;
    auto* dialog = new FamilyTableDialog(name, std::move(data), [this, id](DocumentToolData values) {
        if (auto* part = workspace_.open_part(id)) { auto next = part->session.document(); next.family_table = std::move(values.family_table); part->session.commit(std::move(next), part->session.calculated_boundaries()); }
        else if (auto* assembly = workspace_.open_assembly(id)) { auto next = assembly->session.document(); next.family_table = std::move(values.family_table); assembly->session.commit(std::move(next)); }
        refresh_tabs();
    }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_file_settings() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id(); DocumentToolData data;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); data.units = d.document_units; data.precision = d.document_precision; }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); data.units = d.document_units; data.precision = d.document_precision; }
    else return;
    auto* dialog = new FileSettingsDialog(std::move(data), [this, id](DocumentToolData values) {
        const auto change=zima::workspace::set_file_settings(workspace_,kernel_,id,{std::move(values.units),std::move(values.precision)});
        if(change.calculated){preserve_view_on_refresh_=true;refresh_scene();}
        if (const auto* part=workspace_.open_part(id)) setProperty("zimaDocumentDecimalPlaces",document_decimal_places(part->session.document()));
        else if (const auto* assembly=workspace_.open_assembly(id)) setProperty("zimaDocumentDecimalPlaces",document_decimal_places(assembly->session.document()));
        refresh_tabs();
    }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

} // namespace zima::app
