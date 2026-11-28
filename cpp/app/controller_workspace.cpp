#include "assembly_workspace_window.hpp"
#include "controller_dialog.hpp"
#include "drawing_window.hpp"
#include "documentation.hpp"
#include "resource_icon.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QQuaternion>
#include <QStackedWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QTimer>

namespace zima::app {
controller::Target AssemblyWorkspaceWindow::controller_target()const {
    controller::Target result;
    auto* active=QApplication::activeWindow();
    if(QApplication::applicationState()!=Qt::ApplicationActive||!active||
        (active!=this&&!isAncestorOf(active))||controller_settings_dialog_||global_settings_dialog_||
        workspace_.displayed_document_id().empty())return result;
    if(workspace_stack_->currentWidget()==drawing_workspace_)result.context=controller::Context::Drawing;
    else if(workspace_stack_->currentWidget()==model_workspace_)
        result.context=active_sketch_id_.empty()?controller::Context::Model:controller::Context::Sketch;
    else return result;
    result.identity=QString::fromStdString(workspace_.displayed_document_id())+"/"+
        controller::context_key(result.context)+"/"+QString::fromStdString(active_sketch_id_);
    result.commands=true;
    for(auto* dialog:findChildren<QDialog*>())if(dialog->isVisible()){result.commands=false;break;}
    for(auto* focus=QApplication::focusWidget();focus&&focus!=this;focus=focus->parentWidget())
        if(qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus)||
            qobject_cast<QPlainTextEdit*>(focus)||qobject_cast<QTextEdit*>(focus)){result.commands=false;break;}
    return result;
}
void AssemblyWorkspaceWindow::initialize_controllers() {
    auto* settings_action=findChild<QAction*>("controllerSettingsAction");
    for(auto* toolbar:findChildren<QToolBar*>())if(auto* button=toolbar->widgetForAction(settings_action)) {
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button,&QWidget::customContextMenuRequested,this,[this,button](const QPoint& position) {
            const auto url=documentation::guide_url(application_settings_,"controllerSettingsAction");
            QMenu menu(this);auto* guide=menu.addAction(application_settings_.text("menu.help.guide","Show guide"));
            guide->setEnabled(!url.isEmpty());
            if(menu.exec(button->mapToGlobal(position))==guide)QTimer::singleShot(0,this,[this,url]{open_browser(url);});
        });
    }
    // Snapshot persistent application actions, excluding temporary dialog/menu
    // actions. Drawing IDs have a separate namespace to avoid ambiguous bindings.
    for(auto* action:findChildren<QAction*>()) {
        const auto name=action->objectName();
        if(name.isEmpty()||!name.endsWith("Action")||action->isSeparator()||action->menu()||name=="controllerSettingsAction")continue;
        bool drawing=false,dialog=false;
        for(auto* owner=action->parent();owner&&owner!=this;owner=owner->parent()) {
            drawing|=owner==drawing_workspace_;dialog|=qobject_cast<QDialog*>(owner)!=nullptr;
        }
        if(dialog)continue;
        const auto id=(drawing?QStringLiteral("drawing:"):QString{})+name;
        if(!controller_actions_.contains(id))controller_actions_[id]=action;
    }
    controller_input_=new controller::InputService(quintptr(winId()),this);
    controller_input_->configure(application_settings_.controllers);
    controller_input_->target=[this]{return controller_target();};
    controller_input_->navigate=[this](const controller::Motion& motion) {
        const auto target=controller_target();if(target.identity.isEmpty())return;
        if(target.context==controller::Context::Drawing) {
            drawing_workspace_->navigate_view({motion.pan_x,motion.pan_y},motion.zoom);return;
        }
        const QVector3D vector(float(motion.rotation[0]),float(motion.rotation[1]),float(motion.rotation[2]));
        const float angle=vector.length();
        // Exponential-map rotation preserves a constant angular velocity across
        // different render/input rates, including simultaneous rotation axes.
        const auto rotation=angle>0?QQuaternion::fromAxisAndAngle(vector/angle,angle):QQuaternion{};
        viewer_->navigate_camera({motion.pan_x*viewer_->height(),-motion.pan_y*viewer_->height()},
            rotation,-motion.zoom,viewer_->rect().center());
    };
    controller_input_->command=[this](const QString& id) {
        const auto target=controller_target();if(!target.commands||target.identity.isEmpty())return;
        if(id.startsWith("drawing:")&&target.context!=controller::Context::Drawing)return;
        const auto action=controller_actions_.value(id);
        if(!action||!action->isEnabled()||!action->isVisible())return;
        for(auto* owner=action->parent();owner&&owner!=this;owner=owner->parent())
            if(auto* menu=qobject_cast<QMenu*>(owner);menu&&(!menu->menuAction()->isEnabled()||!menu->menuAction()->isVisible()))return;
        action->trigger();
    };
}
void AssemblyWorkspaceWindow::show_controller_settings() {
    if(controller_settings_dialog_) {controller_settings_dialog_->raise();controller_settings_dialog_->activateWindow();return;}
    if(global_settings_dialog_) {global_settings_dialog_->raise();global_settings_dialog_->activateWindow();return;}
    QVector<controller::Command> commands;
    for(auto it=controller_actions_.cbegin();it!=controller_actions_.cend();++it)if(it.value()) {
        auto label=it.value()->text();label.remove('&');
        if(it.key().startsWith("drawing:"))label=application_settings_.text("controller.drawing","Drawing")+": "+label;
        commands.push_back({it.key(),label,it.value()->icon()});
    }
    std::sort(commands.begin(),commands.end(),[](const auto& a,const auto& b){return QString::localeAwareCompare(a.label,b.label)<0;});
    auto current=application_settings_;
    current.controllers=ApplicationSettings::load(QFileInfo(application_settings_.config_path).absolutePath()).controllers;
    auto* dialog=new controller::SettingsDialog(current,*controller_input_,std::move(commands),
        [this](const controller::Preferences& preferences,QString* error) {
            auto next=ApplicationSettings::load(QFileInfo(application_settings_.config_path).absolutePath());
            next.controllers=preferences;
            if(!next.save(error))return false;
            application_settings_.controllers=preferences;controller_input_->configure(preferences);return true;
        },this);
    controller_settings_dialog_=dialog;controller_input_->suspend();
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();
}
}
