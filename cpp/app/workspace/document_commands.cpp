#include <zima/drawing/measurement_dimension.hpp>
#include "../drawing_annotation_layout.hpp"
#include "workspace_internal.hpp"
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include "../sheet_state_dialog.hpp"
#include "../sheet_state_selection.hpp"

namespace zima::app {
using namespace workspace_detail;

void AssemblyWorkspaceWindow::show_sheet_state_properties(bool unfold,const std::string& container_id) {
    if(properties_dialog_)return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    try {
        const auto document_id=workspace_.active_document_id();
        const auto occurrence=resolve_active_occurrence(document_id);
        if(!occurrence)throw std::invalid_argument("Activate the exact Part occurrence first.");
        const auto* input=workspace::calculated_operation_input(part->session,container_id);
        if(!input)throw std::invalid_argument("Sheet state requires a calculated input body.");
        const auto* stored=part->session.document().find_container(container_id);
        auto feature=stored?*stored:document::PartDocument::create_sketch_container();
        feature.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
        if(!stored)feature.name=(unfold?tr("Rozvinout"):tr("Ohnout zpět")).toStdString();
        std::map<std::string,QString> available;
        for(const auto& region:workspace::sheet_state_regions(part->session.document(),container_id))
            if(kernel::sheet_material::eligible(region,unfold))
                if(const auto* source=part->session.document().find_container(region.owner_id))available.emplace(region.owner_id,QString::fromStdString(source->name));
        const auto rollback=stored?part->session.rollback_boundary(container_id):std::optional<document::HistoryRollbackBoundary>{};
        if(stored&&!rollback)throw std::invalid_argument("Regenerate the Part before editing this feature.");
        auto* dialog=new SheetStateDialog(feature,available,[this,document_id](auto pending){
            static_cast<void>(workspace::commit_sheet_state(workspace_,kernel_,document_id,std::move(pending)));
        },this);
        properties_dialog_=dialog;properties_dialog_instance_path_=*occurrence;track_tree_edit(dialog);
        if(rollback)part_rollback_=PartRollbackContext{document_id,*occurrence,rollback->history_index,rollback->input_body};
        tree_->setProperty("commandSelectionActive",true);
        const auto selection=std::make_shared<SheetStateSelection>(input->mesh);
        const auto update=[this,dialog,selection,path=*occurrence] {
            viewer_->set_original_container_selection(false);
            viewer_->set_result_face_selection(true);
            viewer_->set_selection_contract({viewer::CandidateKind::Container,viewer::CandidateKind::Face,viewer::CandidateKind::Edge});
            viewer_->set_candidate_filter([dialog,selection,path](const auto& candidate){return dialog->selecting()&&
                candidate.instance_path==path&&dialog->available(selection->resolve(candidate));},false);
            viewer_->set_feature_selected_edges(selection->wire(dialog->highlighted(),path));
        };
        feature_reference_pick_=[this,dialog,selection,path=*occurrence](const auto& candidate) {
            if(!dialog->selecting()||candidate.instance_path!=path||
                !viewer::matches_selection_filter(candidate,viewer_->selection_filter()))return;
            auto region=selection->resolve(candidate);
            // Tree rows address source features directly; View candidates
            // have already passed the calculated-input candidate filter.
            if(region.empty()&&candidate.kind==viewer::CandidateKind::Container&&dialog->available(candidate.owner_id))region=candidate.owner_id;
            if(!dialog->available(region))return;
            dialog->toggle(region);viewer_->clear_selection();
        };
        feature_reference_end_=[dialog]{dialog->end_entry();};
        dialog->selection_changed=update;
        connect(dialog,&QDialog::finished,this,[this,dialog] {
            dialog->selection_changed={};feature_reference_pick_={};feature_reference_end_={};properties_dialog_=nullptr;
            part_rollback_.reset();properties_dialog_instance_path_.clear();
            viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
            viewer_->set_result_face_selection(false);viewer_->set_feature_selected_edges({});viewer_->clear_selection();
            tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        preserve_view_on_refresh_=true;refresh_scene();dialog->show();update();
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
}


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
    tree_->clear(); tree_->setHeaderLabels({QString{}});
    auto* root=new QTreeWidgetItem(tree_,{QString::fromStdString(state->path.empty()?document.name:state->path.filename().string())});
    root->setIcon(0,resource_icon("drawing"));
    root->setData(0,Qt::UserRole,QString::fromStdString(document.document_id));
    root->setData(0,Qt::UserRole+3,"drawing-document");
    for(const auto& sheet:document.sheets) {
        auto* sheet_item=new QTreeWidgetItem(root,{QString::fromStdString(sheet.name)});
        sheet_item->setData(0,Qt::UserRole,QString::fromStdString(sheet.id));
        sheet_item->setData(0,Qt::UserRole+3,"drawing-sheet");
        for(const auto& view:sheet.views) {
            auto* item=new QTreeWidgetItem(sheet_item,{QString::fromStdString(view.name)});
            item->setData(0,Qt::UserRole,QString::fromStdString(view.id));
            item->setData(0,Qt::UserRole+3,"drawing-view");
            for(const auto kind:{drawing::ModelAnnotationKind::Dimension,drawing::ModelAnnotationKind::Axis,drawing::ModelAnnotationKind::Construction}) {
                QTreeWidgetItem* group=nullptr;
                for(const auto& annotation:view.model_annotations) {
                    if(annotation.kind!=kind||!annotation.visible)continue;
                    if(!group)group=new QTreeWidgetItem(item,{kind==drawing::ModelAnnotationKind::Dimension?tr("Kóty"):kind==drawing::ModelAnnotationKind::Axis?tr("Osy"):tr("Konstrukční geometrie")});
                    const auto key=model_annotation_key(annotation.source);
                    auto label=QString::fromStdString(annotation.text);
                    if(label.isEmpty())label=QString::fromStdString(annotation.source.semantic_id);
                    if(drawing::break_annotation_hidden(view,annotation))label+=tr(" — skryto přerušením");
                    auto* child=new QTreeWidgetItem(group,{label});
                    child->setData(0,Qt::UserRole,QString::fromStdString(view.id+":"+key));
                    child->setData(0,Qt::UserRole+3,"drawing-annotation");
                    child->setData(0,Qt::UserRole+4,QString::fromStdString(view.id));
                    child->setData(0,Qt::UserRole+5,QString::fromStdString(key));
                    child->setToolTip(0,QString::fromStdString(annotation.source.owner_id+" / "+annotation.source.semantic_id));
                    child->setIcon(0,resource_icon(kind==drawing::ModelAnnotationKind::Dimension?"show-erase":kind==drawing::ModelAnnotationKind::Axis?"axis":"sketch"));
                }
                if(kind==drawing::ModelAnnotationKind::Dimension)for(const auto& dimension:sheet.dimensions) {
                    if(dimension.view_id!=view.id)continue;
                    if(!group)group=new QTreeWidgetItem(item,{tr("Kóty")});
                    auto label=QString::fromStdString(document.dimension_identifiers.identifier(document.document_id,"dimension:"+dimension.id));
                    if(label.isEmpty())label=tr("Měřená kóta");
                    const auto evaluation=drawing::evaluate_drawing_dimension(view,dimension);
                    if(evaluation.state==drawing::MeasurementState::Hidden&&!view.breaks.empty())label+=tr(" — skryto přerušením");
                    auto* child=new QTreeWidgetItem(group,{label});
                    child->setData(0,Qt::UserRole,QString::fromStdString("drawing-dimension:"+dimension.id));
                    child->setData(0,Qt::UserRole+3,"drawing-dimension");
                    child->setData(0,Qt::UserRole+4,QString::fromStdString(view.id));
                    child->setData(0,Qt::UserRole+5,QString::fromStdString(dimension.id));
                    child->setIcon(0,resource_icon("show-erase"));
                }
            }
            item->setExpanded(true);
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
    if(!workspace_.open_part(id) && !workspace_.open_assembly(id))return;
    auto material=zima::workspace::material_data(workspace_,id);DocumentToolData data;
    data.physical_parameters=std::move(material.properties);data.physical_parameter_units=std::move(material.units);data.descriptions=std::move(material.descriptions);
    auto accepted = [this, id](DocumentToolData values) {
        static_cast<void>(zima::workspace::set_material_data(workspace_,id,{std::move(values.physical_parameters),std::move(values.physical_parameter_units),std::move(values.descriptions)}));
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
    if(!workspace_.open_part(id) && !workspace_.open_assembly(id))return;
    auto data=zima::workspace::model_relations(workspace_,id);
    auto* dialog = new RelationsDialog(std::move(data.parameters), std::move(data.relations),
        [this, id](auto next_relations) {
            static_cast<void>(zima::workspace::set_model_relations(workspace_,id,std::move(next_relations)));
            refresh_tabs();
        }, application_settings_, this);
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
    edit_family_table_for_document(workspace_.active_document_id());
}

void AssemblyWorkspaceWindow::edit_family_table_for_document(std::string document_id) {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto active_owner=workspace::family_owner(workspace_,workspace_.active_document_id());
    const auto id = workspace::family_owner(workspace_,document_id); DocumentToolData data; QString name;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else return;
    auto* dialog = new FamilyTableDialog(name, std::move(data), [this, id](DocumentToolData values) {
        static_cast<void>(zima::workspace::set_family_table(workspace_,id,zima::document::parse_family_table(values.family_table)));
        refresh_tabs();
    }, application_settings_, this);
    dialog->setProperty("familyDocumentId",QString::fromStdString(id));
    dialog->setProperty("familyInstancePath",QString::fromStdString(
        id==active_owner ? workspace_.active_occurrence_path() : std::string{}));
    dialog->set_references(workspace::family_references(workspace_,id));
    dialog->setAttribute(Qt::WA_DeleteOnClose);properties_dialog_=dialog;
    const auto previous_dimensions=construction_dimension_object_id_;
    const auto previous_visibility=viewer_->reference_visible(viewer::ReferenceVisibility::Dimensions);
    dialog->entry_changed=[this]{update_family_selection();};
    dialog->open_instance=[this,id](const std::string& name){
        // Finish the properties transaction before switching the displayed tab.
        QTimer::singleShot(0,this,[this,id,name]{try {
            static_cast<void>(workspace::open_family_instance(workspace_,kernel_,id,name));
            refresh_tabs();refresh_scene();viewer_->fit_all();
        }catch(const std::exception& e){QMessageBox::warning(this,tr("Family Table"),tr(e.what()));}});
    };
    tree_->setProperty("commandSelectionActive",true);
    connect(dialog,&QDialog::finished,this,[this,dialog,previous_dimensions,previous_visibility]{
        dialog->entry_changed={};properties_dialog_=nullptr;
        construction_dimension_object_id_=previous_dimensions;
        viewer_->set_reference_visibility(viewer::ReferenceVisibility::Dimensions,previous_visibility);
        viewer_->set_original_container_selection(false);viewer_->set_candidate_priority({});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
        viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->show();update_family_selection();
}

void AssemblyWorkspaceWindow::update_family_selection() {
    auto* dialog=dynamic_cast<FamilyTableDialog*>(properties_dialog_);if(!dialog)return;
    const auto document_id=dialog->property("familyDocumentId").toString().toStdString();
    const auto prefix=dialog->property("familyInstancePath").toString().toStdString();
    const auto references=workspace::family_references(workspace_,document_id);
    const bool part=workspace_.open_part(document_id);
    viewer_->set_original_container_selection(part);
    viewer_->set_selection_contract({viewer::CandidateKind::Dimension,viewer::CandidateKind::Container,viewer::CandidateKind::Occurrence});
    viewer_->set_candidate_filter([dialog,references,prefix,part](const auto& candidate){
        if(dialog->active_column()<0)return false;
        auto owner=candidate.owner_id;
        if(candidate.kind==viewer::CandidateKind::Occurrence) {
            const auto parent=assembly::InstancePath::decode(prefix),path=assembly::InstancePath::decode(candidate.instance_path);
            if(path.occurrence_ids.size()!=parent.occurrence_ids.size()+1 || !std::equal(parent.occurrence_ids.begin(),parent.occurrence_ids.end(),path.occurrence_ids.begin()))return false;
            owner=path.occurrence_ids.back();
        } else if(candidate.instance_path!=prefix)return false;
        return std::ranges::any_of(references,[&](const auto& r){return r.binding.owner_id==owner &&
            (candidate.kind==viewer::CandidateKind::Dimension ? r.binding.kind=="dimension"&&r.binding.semantic_key==candidate.semantic_key : r.binding.kind!="dimension");});
    },false);
    viewer_->set_candidate_priority([](const auto& c){return c.kind==viewer::CandidateKind::Dimension?0:1;});
    viewer_->set_dimension_layout_editable(false);
    std::set<viewer::EdgeKey> highlights;
    const auto mesh=workspace_.authoritative_viewer_mesh(document_id);
    for(const auto& binding:dialog->inspected_references()) {
        if(binding.kind=="dimension")highlights.insert({binding.owner_id,binding.semantic_key,prefix});
        else if(binding.kind=="component") {
            const auto path=assembly::InstancePath::decode(prefix).child(binding.owner_id).encoded();
            for(const auto& edge:mesh.edges)if(edge.reference.instance_path==assembly::InstancePath{}.child(binding.owner_id).encoded())highlights.insert({edge.reference.owner_id,edge.reference.semantic_key,path});
        } else for(const auto& edge:mesh.original_references.edges) {
            bool match=edge.reference.owner_id==binding.owner_id;
            if(binding.kind=="body")if(const auto* state=workspace_.open_part(document_id))
                if(const auto* owner=state->session.document().body_history.owner(edge.reference.owner_id))match=owner->scope.id==binding.owner_id;
            if(match)highlights.insert({edge.reference.owner_id,edge.reference.semantic_key,prefix});
        }
    }
    viewer_->set_constraint_reference_highlights({},std::move(highlights));
}
bool AssemblyWorkspaceWindow::accept_family_reference(const viewer::ViewerCandidate& candidate,bool dimensions) {
    auto* dialog=dynamic_cast<FamilyTableDialog*>(properties_dialog_);if(!dialog)return false;
    if(dialog->active_column()<0)return true;
    if(const auto filter=viewer_->candidate_filter();filter&&!filter(candidate))return true;
    auto owner=candidate.owner_id;
    if(candidate.kind==viewer::CandidateKind::Occurrence)owner=assembly::InstancePath::decode(candidate.instance_path).occurrence_ids.back();
    if(dimensions&&candidate.kind==viewer::CandidateKind::Container) {
        viewer_->set_reference_visibility(viewer::ReferenceVisibility::Dimensions,true);show_parameter_dimensions(owner);update_family_selection();return true;
    }
    const auto document_id=dialog->property("familyDocumentId").toString().toStdString();
    const auto references=workspace::family_references(workspace_,document_id);
    for(const auto& r:references)if(r.binding.owner_id==owner &&
        (candidate.kind==viewer::CandidateKind::Dimension?r.binding.kind=="dimension"&&r.binding.semantic_key==candidate.semantic_key:r.binding.kind!="dimension")) {
        dialog->choose_reference(r);
        {
            const QSignalBlocker blocker(tree_);tree_->clearSelection();
            for(QTreeWidgetItemIterator item(tree_);*item;++item)
                if((*item)->data(0,Qt::UserRole).toString().toStdString()==owner &&
                    (*item)->data(0,Qt::UserRole+1).toString().toStdString()==candidate.instance_path) {
                    tree_->setCurrentItem(*item);break;
                }
        }
        viewer_->confirm_reference(candidate.owner_id,candidate.semantic_key,candidate.instance_path,candidate.kind);
        return true;
    }
    return true;
}

void AssemblyWorkspaceWindow::edit_file_settings(bool sheet_metal) {
    if (properties_dialog_ != nullptr) {
        if(sheet_metal)if(auto* settings=dynamic_cast<FileSettingsDialog*>(properties_dialog_))settings->show_sheet_metal_page();
        properties_dialog_->raise(); return;
    }
    const auto id = workspace_.active_document_id(); DocumentToolData data;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); data.units = d.document_units; data.precision = d.document_precision;data.sheet_metal=zima::document::sheet_metal_defaults(d); }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); data.units = d.document_units; data.precision = d.document_precision; }
    else return;
    auto* dialog = new FileSettingsDialog(std::move(data), [this, id](DocumentToolData values) {
        const auto change=zima::workspace::set_file_settings(workspace_,kernel_,id,{std::move(values.units),std::move(values.precision),values.sheet_metal});
        if(change.calculated){preserve_view_on_refresh_=true;refresh_scene();}
        if (const auto* part=workspace_.open_part(id)) setProperty("zimaDocumentDecimalPlaces",document_decimal_places(part->session.document()));
        else if (const auto* assembly=workspace_.open_assembly(id)) setProperty("zimaDocumentDecimalPlaces",document_decimal_places(assembly->session.document()));
        refresh_tabs();
    }, application_settings_, this);
    if(sheet_metal)dialog->show_sheet_metal_page();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    update_application_actions();
    rebuild_application_toolbar();
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
        update_application_actions();
        rebuild_application_toolbar();
    });
    dialog->show();
}

} // namespace zima::app
