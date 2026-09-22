#include "assembly_workspace_window.hpp"
#include "symbol_properties_dialog.hpp"
#include "file_dialog.hpp"
#include <zima/symbols/definition.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <algorithm>
namespace zima::app {
void AssemblyWorkspaceWindow::start_symbol() {
    if(!active_sketch()||properties_dialog_)return;
    const auto path=open_file(this,tr("Vložit symbol"),application_settings_.resolved_paths.value("Symbols"),tr("Symbol ZIMA-CAD (*.symz)"),application_settings_.translations);
    if(path.isEmpty())return;
    try {
        const auto definition=symbols::Definition::load(std::filesystem::path(path.toStdU16String()));
        sketcher::SymbolInstance instance;instance.id=kernel::make_stable_id();instance.definition=definition.serialized();instance.variant=definition.default_variant;
        instance.use_cad_variant=!definition.variant_source.empty()&&template_sketch();
        show_symbol_properties({},std::move(instance));
    }catch(const std::exception&){QMessageBox::warning(this,tr("Symbol"),tr("Symbol nelze načíst. Zkontrolujte jeho definici."));}
}
void AssemblyWorkspaceWindow::show_symbol_properties(const std::string& id,std::optional<sketcher::SymbolInstance> initial) {
    const auto* sketch=active_sketch();if(!sketch||properties_dialog_)return;
    if(!initial){const auto found=std::ranges::find(sketch->symbols,id,&sketcher::SymbolInstance::id);if(found==sketch->symbols.end())return;initial=*found;}
    const auto owner=workspace_.active_document_id(),sketch_id=sketch->id;
    cancel_sketch_segment();clear_selected_sketch_geometry();viewer_->clear_selection();
    auto* dialog=new SymbolDialog(*initial,[this,owner,sketch_id,id](const auto& symbol){
        const auto* current=active_sketch();if(workspace_.active_document_id()!=owner||!current||current->id!=sketch_id)return;
        auto preview=*current;auto found=std::ranges::find(preview.symbols,id,&sketcher::SymbolInstance::id);
        if(found==preview.symbols.end())preview.symbols.push_back(symbol);else *found=symbol;
        show_sketch_drag_preview(preview);
    },[this,owner,sketch_id,id](auto symbol){
        if(workspace_.active_document_id()!=owner||active_sketch_id_!=sketch_id)throw std::runtime_error(QObject::tr("Skica již není aktivní.").toStdString());
        if(!mutate_active_sketch([&](auto& sketch){
            auto found=std::ranges::find(sketch.symbols,id,&sketcher::SymbolInstance::id);
            if(found==sketch.symbols.end())sketch.symbols.push_back(symbol);else *found=symbol;sketch.validate();
        }))throw std::runtime_error(QObject::tr("Skica již není aktivní.").toStdString());
    },this);
    properties_dialog_=dialog;symbol_anchor_=[dialog](double x,double y){dialog->set_anchor(x,y);};viewer_->set_selection_contract({});
    connect(dialog,&QDialog::finished,this,[this,dialog]{symbol_anchor_={};if(properties_dialog_==dialog)properties_dialog_=nullptr;preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});dialog->show();
}
void AssemblyWorkspaceWindow::remove_symbol(const std::string& id) {
    if(properties_dialog_||!active_sketch())return;
    static_cast<void>(mutate_active_sketch([&](auto& sketch){std::erase_if(sketch.symbols,[&](const auto& symbol){return symbol.id==id;});}));
    selected_symbol_.clear();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
}
void AssemblyWorkspaceWindow::select_symbol(const std::string& id) {
    clear_selected_sketch_geometry();selected_symbol_=id;const QSignalBlocker blocked(tree_);tree_->clearSelection();
    QTreeWidgetItemIterator it(tree_);while(*it){auto* item=*it++;if(item->data(0,Qt::UserRole+3)=="sketch-symbol"&&item->data(0,Qt::UserRole).toString().toStdString()==id){item->setSelected(true);tree_->scrollToItem(item);break;}}
}
}
