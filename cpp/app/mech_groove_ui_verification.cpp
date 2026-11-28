#include "assembly_workspace_window.hpp"
#include "console_ui_verification.hpp"
#include "primitive_properties_dialog.hpp"
#include "mech_groove_parameter_panel.hpp"
#include "application_settings.hpp"
#include "resource_icon.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidgetItemIterator>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QScopeGuard>
#include <QEventLoop>
#include <QTimer>
#include <QToolBar>
#include <iostream>

namespace zima::app {
// This entry point is deliberately separate from native tests. It creates
// windows and captures evidence only when the explicit GUI gate is invoked.
int verify_mech_groove_workspace_ui(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
    const auto click=[&](QWidget* target,QPointF point,Qt::MouseButton button=Qt::LeftButton){
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,QPointF(target->mapToGlobal(point.toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(target,&event);flush();
        }
    };
    try {
        for(bool internal:{false,true})for(bool circular:{false,true}) {
            workspace::Workspace live;kernel::OcctKernel kernel;
            auto part=document::PartDocument::create_default();const auto id=part.document_id;
            const auto body=part.body_history.create_body("B-01");
            live.add_part(part);live.activate(id);
            auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
            static_cast<void>(sketch.add_circle(0,0,internal?35:25));
            if(internal)static_cast<void>(sketch.add_circle(0,0,25));
            auto shaft=document::PartDocument::create_feature_container(sketch.id);shaft.name="F-01";shaft.feature.sides[0].length=40;
            workspace::commit_profile(live,kernel,id,shaft,workspace::ProfileEditMode::Create,sketch);
            const auto file=directory/(std::string("groove-")+(internal?"internal":"external")+(circular?"-circular":"-rectangular")+".prtz");
            const auto* native=live.open_part(id);native->session.document().save(file,native->session.calculated_boundaries());
            window.resize(1500,1050);window.show();
            check(window.open_document_path(QString::fromStdWString(file.wstring())),"Groove fixture did not open");flush();
            const auto activate=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump()));
            check(activate.ok,"Groove Body activation failed");flush();
            auto* mode=window.findChild<QAction*>("applicationModeAction9");check(mode&&mode->isEnabled(),"Mechanics mode is unavailable");mode->trigger();flush();
            auto* create=window.findChild<QAction*>("mechGrooveAction");check(create&&create->isEnabled()&&!create->icon().isNull(),"Groove action/icon is unavailable");
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Groove workspace is unavailable");
            const auto editor=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("mechGroovePropertiesDialog"));};
            const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
            if(!internal&&!circular) {
                const auto before=window.execute_console_command("documents").data;
                create->trigger();flush();auto* failed=editor();check(failed,"Failed Groove creation did not open");
                const auto failed_id=failed->container_id();
                failed->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(editor()&&window.execute_console_command("documents").data==before,"First failed creation changed live history");
                const auto failed_calls=kernel::body_calculation_count.load();
                failed->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!editor()&&kernel::body_calculation_count.load()==failed_calls,"Second failed creation recalculated or stayed open");
                const auto unresolved=save();
                check(unresolved.find_container(failed_id)&&unresolved.reference_errors.contains(failed_id),"Failed creation did not save an unresolved Groove");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(!save().find_container(failed_id),"Failed creation did not undo in one step");
            }
            const auto baseline=window.execute_console_command("documents").data;
            auto calls=kernel::body_calculation_count.load();
            std::string feature_id;
            for(bool plane_first:{true,false}) {
                create->trigger();flush();auto* dialog=editor();check(dialog&&dialog->mech_groove_panel(),"Groove properties did not open");
                check(!dialog->findChild<QWidget*>("primitiveRotation")&&!dialog->findChild<QWidget*>("primitiveOrientationTable"),"Groove exposes redundant angular placement");
                auto* panel=dialog->mech_groove_panel();
                panel->findChild<QComboBox*>("mechGrooveProfile")->setCurrentIndex(circular?1:0);flush();
                const auto pick_plane=[&] {
                    QTreeWidgetItem* plane=nullptr;
                    const auto origin=native->session.document().body_history.find(body)->origin().id;
                    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+5)=="origin:plane:xy"&&
                            ((*it)->data(0,Qt::UserRole+6).isValid()?(*it)->data(0,Qt::UserRole+6):(*it)->data(0,Qt::UserRole)).toString().toStdString()==origin){plane=*it;break;}
                    check(plane,"Groove axial datum is missing from Tree");
                    for(auto* p=plane->parent();p;p=p->parent())p->setExpanded(true);
                    tree->scrollToItem(plane);flush();click(tree->viewport(),tree->visualItemRect(plane).center());
                };
                const auto pick_cylinder=[&] {
                    view->set_view_direction({-1,-1,-2});view->fit_all();settle();
                    const auto& refs=native->session.calculated_boundaries().back().body_outputs.at(body)->mesh.original_references;
                    std::optional<QPointF> hit;
                    for(int y=20;y<view->height()-20&&!hit;y+=4)for(int x=20;x<view->width()-20&&!hit;x+=4) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Face)continue;
                        const auto& candidate=candidates.front();
                        const auto face=std::ranges::find_if(refs.triangle_references,[&](const auto& f){return f.owner_id==candidate.owner_id&&f.semantic_key==candidate.semantic_key;});
                        if(face!=refs.triangle_references.end()&&face->surface&&face->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                                std::abs(face->surface->radius-25)<1e-7&&face->surface->reversed==internal)hit=QPointF(x,y);
                    }
                    check(hit.has_value(),"Exact groove cylinder is not mouse selectable");click(view,*hit);
                };
                if(plane_first){pick_plane();pick_cylinder();}else{pick_cylinder();pick_plane();}
                const auto pending=dialog->pending_value();check(pending.placement.references.size()==2,"Groove did not accept both placement references");
                check(std::ranges::count_if(pending.placement.references,[](const auto& r){return r.use_axis;})==1,"Cylinder was not entered as Axis");
                check(std::ranges::none_of(pending.placement.references,[](const auto& r){return r.use_angle;}),"Groove plane became an angular reference");
                for(auto* mode:dialog->findChildren<QComboBox*>())if(mode->objectName().startsWith("placementReferenceType"))
                    check(mode->findData(2)<0,"Groove reference entry offers an angular mode");
                const auto axial_index=plane_first?0:1;
                check(dialog->set_inline_parameter_value(std::string("placement:reference_offset:")+std::to_string(axial_index),20),"Groove axial offset is not editable");flush();
                check(panel->parameters().internal==internal&&std::abs(panel->parameters().support_diameter-50)<1e-7,"Groove inferred the wrong material side or diameter");
                check(!view->transient_edges().empty()&&kernel::body_calculation_count.load()==calls,"Groove preview is missing or calculated material");
                if(!internal&&!circular&&!plane_first) {
                    const auto original=ApplicationSettings::load(QDir::currentPath());
                    const auto restore=qScopeGuard([&]{apply_application_translations(application,original);apply_application_appearance(application,original);flush();});
                    QTemporaryDir temporary;check(temporary.isValid(),"Groove language fixture is unavailable");
                    const auto definition=dialog->pending_value();
                    for(const auto* language:{"cs","en","de","fr","ru"}) {
                        QSettings ini(temporary.filePath("config.ini"),QSettings::IniFormat);
                        ini.setValue("Application/Language",QString::fromLatin1(language));
                        ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
                        auto settings=ApplicationSettings::load(temporary.path());
                        apply_application_translations(application,settings);flush();
                        auto* profiles=panel->findChild<QComboBox*>("mechGrooveProfile");
                        check(profiles->itemText(0)==settings.qt_translations.value("Rectangular groove")&&
                            profiles->itemText(1)==settings.qt_translations.value("Circular groove"),"Groove profile labels did not translate");
                        check(panel->findChild<QComboBox*>("mechGrooveCatalogue")->itemText(0)==settings.qt_translations.value("Custom"),"Groove custom label did not translate");
                        for(const auto* theme:{"light","dark","light"}) {
                            settings.theme=QString::fromLatin1(theme);apply_application_appearance(application,settings);flush();
                            check(!resource_icon("groove").isNull(),"Groove icon disappeared after changing theme");
                            check(dialog->pending_value()==definition&&kernel::body_calculation_count.load()==calls,"Groove language/theme change modified or calculated material");
                            dialog->grab().save(QString::fromStdWString((directory/(std::string("groove-dialog-")+language+"-"+theme+".png")).wstring()));
                        }
                    }
                }
                if(plane_first) {
                    dialog->reject();flush();
                    check(window.execute_console_command("documents").data==baseline,"Groove create Cancel modified the document");
                } else {
                    feature_id=dialog->container_id();
                    dialog->grab().save(QString::fromStdWString((directory/(file.stem().string()+"-properties.png")).wstring()));
                    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Groove OK did not close");
                }
            }
            auto persisted=save();const auto* feature=persisted.find_container(feature_id);
            check(feature&&feature->feature_kind==document::FeatureKind::MechGroove&&!feature->mech_groove.section.empty(),"Groove definition did not persist");
            if(!internal&&!circular)std::filesystem::copy_file(file,directory/"groove-guide-source.prtz",std::filesystem::copy_options::overwrite_existing);
            const auto row=[&]()->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature_id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;return nullptr;};
            check(row(),"Groove Tree row is missing");
            calls=kernel::body_calculation_count.load();const auto unchanged=window.execute_console_command("documents").data;
            window.show_tree_item_properties(row());flush();check(editor(),"Groove Properties did not reopen");
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!editor()&&window.execute_console_command("documents").data==unchanged&&kernel::body_calculation_count.load()==calls,"Unchanged groove OK calculated or committed");
            window.show_tree_item_properties(row());flush();auto* dialog=editor();
            check(dialog&&dialog->set_inline_parameter_value("groove_depth",1.2),"Groove depth edit is unavailable");flush();
            dialog->reject();flush();check(window.execute_console_command("documents").data==unchanged&&kernel::body_calculation_count.load()==calls,"Groove preview/Cancel changed the document");
            window.show_tree_item_properties(row());flush();dialog=editor();
            check(dialog&&dialog->set_inline_parameter_value("groove_depth",1.2),"Groove depth edit cannot reopen");flush();
            click(view,{25,25},Qt::MiddleButton);check(editor(),"Short middle click committed Groove");
            QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(25,25),QPointF(view->mapToGlobal(QPoint(25,25))),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(view,&confirm);flush();check(!editor(),"Middle double-click did not confirm Groove");
            check(std::abs(save().find_container(feature_id)->mech_groove.depth-1.2)<1e-7,"Groove edited depth was not saved");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(save().find_container(feature_id)->mech_groove.depth==feature->mech_groove.depth,"Groove Undo lost its depth");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(std::abs(save().find_container(feature_id)->mech_groove.depth-1.2)<1e-7,"Groove Redo lost its depth");
            // Whole-Origin entry keeps the separately persisted cylindrical
            // support while replacing only the ordinary placement references.
            calls=kernel::body_calculation_count.load();
            window.show_tree_item_properties(row());flush();dialog=editor();check(dialog,"Groove whole-Origin Properties did not open");
            auto* references=dialog->findChild<QTableWidget*>("primitiveReferenceTable");check(references,"Groove reference table is missing");
            for(int index=int(std::min<std::size_t>(3,dialog->first_empty_position_index()))-1;index>=0;--index) {
                auto* cell=references->cellWidget(index,0);check(cell,"Groove reference control is missing");
                auto* clear=cell->findChild<QPushButton*>();check(clear,"Groove reference clear control is missing");clear->click();flush();
            }
            click(references->viewport(),references->visualItemRect(references->item(0,1)).center());
            QTreeWidgetItem* origin_row=nullptr;const auto origin_id=native->session.document().body_history.find(body)->origin().id;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==origin_id){origin_row=*it;break;}
            check(origin_row,"Groove Body Origin is missing");
            for(auto* p=origin_row->parent();p;p=p->parent())p->setExpanded(true);
            tree->scrollToItem(origin_row);flush();click(tree->viewport(),tree->visualItemRect(origin_row).center());
            check(document::placement_references_use_whole_origin(dialog->pending_value().placement.references),"Groove whole-Origin mouse entry failed");
            check(dialog->set_inline_parameter_value("placement:reference_offset:0",20),"Groove whole-Origin axial correction is unavailable");flush();
            dialog->set_reference_inspected(0,true);flush();dialog->clear_reference_highlights();flush();
            dialog->resize(dialog->width()+45,dialog->height()+60);flush();
            check(kernel::body_calculation_count.load()==calls,"Groove Origin entry/inspection/resize calculated material");
            const auto whole=dialog->pending_value().placement;
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Groove whole-Origin OK failed");
            auto whole_saved=save();check(whole_saved.find_container(feature_id)->placement==whole,"Groove whole-Origin placement did not persist");
            window.show_tree_item_properties(row());flush();check(editor()&&editor()->pending_value().placement==whole,"Groove whole-Origin edit did not reopen");editor()->reject();flush();
            // Ordinary click and double-click share the authored wire source;
            // neither is a geometry or document-edit operation.
            calls=kernel::body_calculation_count.load();const auto inspection_baseline=window.execute_console_command("documents").data;
            view->fit_all();click(view,{3,3});flush();std::optional<QPointF> groove_hit;
            for(int y=20;y<view->height()-20&&!groove_hit;y+=3)for(int x=20;x<view->width()-20&&!groove_hit;x+=3) {
                const auto candidates=view->selection_candidates_at({double(x),double(y)});
                if(!candidates.empty()&&candidates.front().owner_id==feature_id)groove_hit=QPointF(x,y);
            }
            check(groove_hit.has_value(),"Groove result is not mouse selectable");click(view,*groove_hit);
            check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==feature_id&&tree->currentItem()==row(),"Groove click did not synchronize Tree selection");
            check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature_id&&d.reference.semantic_key.starts_with("parameter:groove_");}),"Groove single click exposed dimensions");
            QMouseEvent inspect(QEvent::MouseButtonDblClick,*groove_hit,QPointF(view->mapToGlobal(groove_hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&inspect);flush();
            check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature_id&&d.reference.semantic_key=="parameter:groove_depth";}),"Groove double-click omitted its dimensions");
            check(kernel::body_calculation_count.load()==calls&&window.execute_console_command("documents").data==inspection_baseline,"Groove inspection calculated or changed the document");
            window.grab().save(QString::fromStdWString((directory/(file.stem().string()+"-result.png")).wstring()));
            if(!circular) {
                const auto before_failure=save();
                const auto before_state=window.execute_console_command("documents").data;
                calls=kernel::body_calculation_count.load();
                window.show_tree_item_properties(row());flush();dialog=editor();
                auto* catalogue=dialog->findChild<QComboBox*>("mechGrooveCatalogue");
                const auto& catalogues=document::groove_catalogues();
                const auto family=std::ranges::find_if(catalogues,[&](const auto& c){return c.profile==document::GrooveProfile::Rectangular&&c.internal==internal;});
                check(family!=catalogues.end(),"Groove catalogue fixture is missing");
                catalogue->setCurrentIndex(catalogue->findData(QString::fromStdString(family->id)));flush();
                auto* width=dialog->findChild<QDoubleSpinBox*>("mechGrooveWidth");
                const auto& size=document::groove_size(family->id,50);
                for(double value:{size.width_min,size.width_max,(size.width_min+size.width_max)/2}) {
                    width->setValue(value);flush();
                    check(std::abs(dialog->pending_value().mech_groove.width-value)<1e-7,"Groove catalogue width range did not update the actual draft");
                }
                check(kernel::body_calculation_count.load()==calls,"Groove catalogue preview calculated material");
                dialog->reject();flush();
                check(window.execute_console_command("documents").data==before_state,"Groove catalogue Cancel changed history");
                window.show_tree_item_properties(row());flush();dialog=editor();
                catalogue=dialog->findChild<QComboBox*>("mechGrooveCatalogue");
                const auto incompatible=std::ranges::find_if(catalogues,[&](const auto& c){return c.profile==document::GrooveProfile::Rectangular&&c.internal!=internal;});
                catalogue->setCurrentIndex(catalogue->findData(QString::fromStdString(incompatible->id)));flush();
                dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(editor()&&window.execute_console_command("documents").data==before_state,"First failed Groove OK changed live history or closed Properties");
                const auto failed_calls=kernel::body_calculation_count.load();
                dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!editor()&&kernel::body_calculation_count.load()==failed_calls,"Second failed Groove OK did not reuse the failure");
                check(save().find_container(feature_id)->mech_groove.catalogue==incompatible->id,"Unresolved Groove definition was not saved");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(save().find_container(feature_id)->mech_groove==before_failure.find_container(feature_id)->mech_groove,"Failed Groove confirmation did not undo in one step");
            }
        }
        // Capture each guide from a newly initialized native application in
        // that language. No relabeling or raster editing of another locale.
        const auto original=ApplicationSettings::load(QDir::currentPath());
        const auto restore=qScopeGuard([&]{apply_application_translations(application,original);apply_application_appearance(application,original);});
        const auto guide_source=directory/"groove-guide-source.prtz";
        std::vector<kernel::BodyResult> cached;
        auto guide_part=document::PartDocument::load(guide_source,&cached);
        for(auto& feature:guide_part.history)if(feature.feature_kind==document::FeatureKind::MechGroove)feature.name="G-01";
        const auto guide_file=directory/"groove-guide.prtz";guide_part.save(guide_file,cached);
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir settings_dir;check(settings_dir.isValid(),"Groove guide settings directory failed");
            QSettings ini(settings_dir.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));
            ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
            auto settings=ApplicationSettings::load(settings_dir.path());settings.theme="dark";
            apply_application_translations(application,settings);apply_application_appearance(application,settings);
            AssemblyWorkspaceWindow guide(QString::fromStdWString(directory.wstring()),settings_dir.path());
            guide.resize(1500,1050);guide.show();
            check(guide.open_document_path(QString::fromStdWString(guide_file.wstring())),"Groove guide fixture failed to open");flush();
            guide.findChild<QAction*>("applicationModeAction9")->trigger();flush();
            auto* action=guide.findChild<QAction*>("mechGrooveAction");
            check(action&&action->text()==settings.qt_translations.value("Groove"),"Groove guide command is not localized");
            auto* toolbar=guide.findChild<QToolBar*>("toolsToolbar");
            auto* button=toolbar->widgetForAction(action);check(button&&button->isVisible(),"Groove guide button missing");
            const auto output=directory/"guide-captures"/language;std::filesystem::create_directories(output);
            check(button->grab().save(QString::fromStdWString((output/"mechGrooveAction.png").wstring())),"Groove guide button capture failed");
            auto* tree=guide.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
            const auto feature=std::ranges::find_if(guide_part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::MechGroove;});
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==feature->id){row=*it;break;}
            check(row,"Groove guide feature missing");guide.show_tree_item_properties(row);flush();
            auto* dialog=guide.findChild<QWidget*>("mechGroovePropertiesDialog");check(dialog,"Groove guide Properties missing");
            check(dialog->grab().save(QString::fromStdWString((output/"groove.png").wstring())),"Groove guide dialog capture failed");
            static_cast<PrimitivePropertiesDialog*>(dialog)->reject();flush();
        }
        std::cout<<"Groove GUI: both sides/profiles, mouse reference orders, preview, Cancel, persistence, unchanged OK, depth edits, Undo/Redo and middle confirmation passed\n";return 0;
    } catch(const std::exception& error) {
        window.grab().save(QString::fromStdWString((directory/"groove-failure.png").wstring()));
        std::cerr<<"Groove GUI: "<<error.what()<<'\n';return 1;
    }
}
}
