#include "assembly_workspace_window.hpp"
#include "render_body_dialog.hpp"
#include "render_object_dialog.hpp"
#include "../tests/profile_solid_fixture.hpp"
#include <zima/workspace/render_operations.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/file_path.hpp>
#include <zima/render/renderer.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QImage>
#include <QMouseEvent>
#include <QProgressBar>
#include <QListWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <iostream>

namespace zima::app {
int verify_render_body(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto wait=[&](auto done,int timeout=15000){QElapsedTimer clock;clock.start();while(!done()&&clock.elapsed()<timeout)application.processEvents(QEventLoop::AllEvents,20);flush();check(done(),"Render GUI wait timed out");};
        auto part=document::PartDocument::create_default();
        auto floor=test::rectangular_feature(part,{120,90,3});floor.placement.z=-3;
        auto sphere=test::spherical_feature(part,15);sphere.placement.x=-20;sphere.placement.z=13;
        auto block=test::rectangular_feature(part,{25,25,25});block.placement.x=23;block.placement.z=12;
        part.history={floor,sphere,block};document::BodyHistoryGraph graph;std::vector<std::string> sources;
        for(const auto& f:part.history){sources.push_back(graph.create_body("Source"));graph.insert({document::PartHistoryKind::Feature,f.id});}
        graph.activate({});part.set_body_history(graph);part.resolve_constructions({});
        part.appearance.bodies[sources[1]]={"#D09946",.25,.8};part.appearance.bodies[sources[2]]={"#277B9C",.35,.1};
        kernel::OcctKernel kernel;const auto path=directory/"render-scene.prtz";part.save(path,workspace::calculate_part(kernel,part));
        window.resize(1400,950);window.show();flush();check(window.open_document_path(QString::fromStdWString(path.wstring())),"Render fixture did not open");flush();
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_RENDER_COMPACT")){const auto ratio=window.devicePixelRatioF();window.resize(int(1920/ratio),int(1040/ratio));flush();}
        auto& session=window.workspace_.open_part(part.document_id)->session;
        auto* view=window.viewer_;auto* tree=window.tree_;
        const auto row=[&](const std::string& id)->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;return nullptr;};
        const auto click=[&](QWidget* target,QPointF at,Qt::MouseButton button=Qt::LeftButton,bool twice=false) {
            const auto global=QPointF(target->mapToGlobal(at.toPoint()));
            QMouseEvent move(QEvent::MouseMove,at,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(target,&move);
            for(auto type:{twice?QEvent::MouseButtonDblClick:QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,at,global,button,type==QEvent::MouseButtonRelease?Qt::NoButton:button,Qt::NoModifier);QApplication::sendEvent(target,&event);flush();
            }
        };
        const auto tree_click=[&](const std::string& id){auto* item=row(id);check(item,"Render Tree row missing");for(auto* p=item->parent();p;p=p->parent())p->setExpanded(true);tree->scrollToItem(item);flush();click(tree->viewport(),tree->visualItemRect(item).center());};
        const auto view_hit=[&](const std::string& id) {
            for(int y=12;y<view->height()-12;y+=4)for(int x=12;x<view->width()-12;x+=4) {
                const auto candidates=view->selection_candidates_at({double(x),double(y)});
                if(!candidates.empty()&&candidates.front().owner_id==id)return QPointF(x,y);
            }
            throw std::runtime_error("Render object/source unavailable in the common View picker: "+id);
        };
        const auto settings=[&]{return dynamic_cast<RenderBodyDialog*>(window.properties_dialog_);};
        const auto object=[&]{return dynamic_cast<RenderObjectDialog*>(window.properties_dialog_);};
        const auto count=kernel::body_calculation_count.load();
        window.activate_body({});flush();view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();
        QEventLoop settle;QTimer::singleShot(400,&settle,&QEventLoop::quit);settle.exec();flush();
        for(auto projection:{viewer::ProjectionMode::Orthographic,viewer::ProjectionMode::Perspective}) {
            view->set_projection_mode(projection);flush();const auto camera=window.render_camera_from_view();
            const auto center=view->ray_at({view->width()/2.,view->height()/2.});
            const auto ray=render::Scene::camera_ray(camera,320,200,160,100);
            check(center&&std::hypot(ray.direction.x-center->second.x,ray.direction.y-center->second.y,ray.direction.z-center->second.z)<1e-5,"Render camera does not follow View direction");
        }
        view->set_projection_mode(viewer::ProjectionMode::Orthographic);
        const auto revision=session.revision();
        auto* create=window.findChild<QAction*>("createRenderAction");check(create&&create->isEnabled()&&!create->icon().isNull(),"Create Render action missing");
        create->trigger();flush();check(settings()&&settings()->windowType()==Qt::SubWindow,"Render settings are not shared internal Properties");
        check(window.rect().contains(settings()->geometry())&&settings()->buttons()->isVisible(),"Render settings exceed the application bounds");
        settings()->reject();flush();check(session.revision()==revision,"Render creation Cancel changed the document");
        create->trigger();flush();auto* dialog=settings();check(dialog,"Render settings did not reopen");
        const auto render_id=dialog->pending.scope.id;dialog->findChild<QSpinBox*>("renderWidth")->setValue(320);dialog->findChild<QSpinBox*>("renderHeight")->setValue(200);dialog->findChild<QSpinBox*>("renderSamples")->setValue(8);
        const auto initial_camera=dialog->pending.render->objects.front();
        click(view,{view->width()/2.,view->height()/2.},Qt::MiddleButton,true);
        check(!settings()&&window.active_render_body()&&row(render_id),"Render creation did not commit/activate via middle double-click");
        check(!window.sketch_action_->isEnabled()&&window.render_mode_button_->isVisible()&&!window.fem_mode_button_->isVisible(),"Render editing context exposes material commands");
        check(window.tools_toolbar_->actions().contains(window.export_action_),"Render does not reuse the File Export action");
        const auto definition=[&]()->const document::RenderDefinition&{return *session.document().body_history.find(render_id)->render;};
        check(definition().objects.size()==2&&workspace::read_render_sources(session,render_id).size()==3,"Render did not initialize camera/light/all visible sources");
        for(const auto pixel:{QPointF{160,100},QPointF{160,0},QPointF{0,100}}) {
            const auto a=render::Scene::camera_ray(initial_camera,320,200,pixel.x(),pixel.y());
            const auto b=render::Scene::camera_ray(definition().objects.front(),320,200,pixel.x(),pixel.y());
            check(std::hypot(a.origin.x-b.origin.x,a.origin.y-b.origin.y,a.origin.z-b.origin.z)<1e-6&&
                std::hypot(a.direction.x-b.direction.x,a.direction.y-b.direction.y,a.direction.z-b.direction.z)<1e-8,"Render commit changed the captured camera rays");
        }
        const auto stable=session.revision();window.show_render_body_properties(render_id);flush();settings()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==stable,"Unchanged Render OK created Undo");
        window.show_render_body_properties(render_id);flush();dialog=settings();dialog->findChild<QComboBox*>("renderSourceMode")->setCurrentIndex(1);
        dialog->arm(0);tree_click(sources[0]);dialog->arm(1);tree_click(sources[2]);
        check(dialog->pending.render->source_ids==std::vector<std::string>{sources[0],sources[2]},"Render source Tree picking lost identities");
        dialog->arm(0);tree_click(sources[2]);check(dialog->pending.render->source_ids==std::vector<std::string>{sources[0],sources[2]},"Duplicate source selection mutated Render");
        dialog->arm(1);tree_click(sources[1]);check(dialog->pending.render->source_ids.back()==sources[1],"Render source replacement failed");
        dialog->arm(1);click(view,view_hit(sources[2]));
        check(dialog->pending.render->source_ids.back()==sources[2],"Render source View picking lost identity");
        auto* table=dialog->findChild<QTableWidget*>("renderBodySources");auto* eye=table->cellWidget(0,3)->findChild<QToolButton*>();check(eye,"Render source eye missing");eye->click();flush();
        check(dialog->inspected_sources().contains(sources[0])&&!dialog->active_input(),"Render inspection implicitly armed input");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(workspace::read_render_sources(session,render_id).size()==2,"Explicit Render source set did not commit");
        window.undo_action_->trigger();flush();check(definition().visible_bodies,"Render source Undo failed");window.redo_action_->trigger();flush();check(!definition().visible_bodies,"Render source Redo failed");window.undo_action_->trigger();flush();
        const auto before_objects=session.revision();
        for(auto kind:{document::RenderObjectKind::Camera,document::RenderObjectKind::PointLight,document::RenderObjectKind::Sun}) {
            window.show_render_object_properties(kind);flush();auto* entry=object();check(entry&&entry->windowType()==Qt::SubWindow&&!entry->buttons()->button(QDialogButtonBox::Apply),"Render object bypasses shared Properties");
            check(window.rect().contains(entry->geometry())&&entry->buttons()->isVisible(),"Render object Properties exceed the application bounds");
            auto* origin=entry->findChild<QPushButton*>("containerDefaultOriginButton");check(origin&&origin->isEnabled(),"Render object has no whole-Origin placement");origin->click();flush();
            check(document::placement_references_use_whole_origin(entry->pending_value().placement.references),"Render whole-Origin placement failed");
            const auto id=entry->pending_value().id;entry->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!object()&&window.active_render_object(id)&&document::placement_references_use_whole_origin(window.active_render_object(id)->placement.references),"Render object placement was not persisted");
            const auto saved=*window.active_render_object(id);const auto object_revision=session.revision();window.show_render_object_properties(kind,id);flush();object()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(session.revision()==object_revision,"Unchanged Render object OK created Undo");
            window.show_render_object_properties(kind,id);flush();entry=object();
            auto* refs=entry->findChild<QTableWidget*>("renderPlacementReferences");
            auto* inspection=refs->cellWidget(0,4)->findChild<QToolButton*>();check(inspection,"Render placement eye is missing");
            inspection->click();flush();check(!entry->highlighted_reference_entries().empty(),"Render placement inspection failed");
            click(refs->viewport(),refs->visualItemRect(refs->item(0,1)).center());
            auto* other_origin=row(sources[1]+":origin");check(other_origin,"Source Origin is unavailable");
            QTreeWidgetItem* plane=nullptr;for(int n=0;n<other_origin->childCount();++n)if(other_origin->child(n)->data(0,Qt::UserRole+5)=="origin:plane:xz")plane=other_origin->child(n);
            check(plane,"Source Origin plane is unavailable");other_origin->setExpanded(true);tree->scrollToItem(plane);flush();click(tree->viewport(),tree->visualItemRect(plane).center());
            check(entry->pending_value().placement.references.front().owner_id==sources[1]+":origin","Individual Render placement reference replacement failed");
            entry->reject();flush();check(*window.active_render_object(id)==saved,"Reference replacement Cancel changed Render placement");
            window.show_render_object_properties(kind,id);flush();object()->findChild<QLineEdit*>("renderObjectName")->setText("Discarded");object()->reject();flush();check(*window.active_render_object(id)==saved,"Render object Cancel leaked edits");
            window.remove_render_object(id);flush();check(!window.active_render_object(id),"Render object removal failed");window.undo_action_->trigger();flush();check(window.active_render_object(id),"Render object Undo failed");window.redo_action_->trigger();flush();check(!window.active_render_object(id),"Render object Redo failed");
        }
        check(session.revision()>before_objects&&kernel::body_calculation_count.load()==count,"Render object edits calculated CAD");
        const auto camera_id=definition().active_camera_id;tree_click(camera_id);check(view->mesh().dimensions.empty(),"Single Render object click exposed dimensions");
        click(tree->viewport(),tree->visualItemRect(row(camera_id)).center(),Qt::LeftButton,true);flush();check(!view->mesh().dimensions.empty(),"Render object double-click inspection has no dimensions");
        tree_click(definition().objects.back().id);check(view->mesh().dimensions.empty(),"Selecting another Render object retained unrelated dimensions");
        click(view,{20,20});
        const auto camera_hit=view_hit(camera_id);const auto selection_revision=session.revision();
        click(view,camera_hit);check(tree->currentItem()==row(camera_id)&&view->mesh().dimensions.empty(),
            "Render View selection did not synchronize Tree or exposed dimensions");
        click(view,camera_hit,Qt::LeftButton,true);
        check(!view->mesh().dimensions.empty(),"Render View double-click did not expose dimensions");
        check(tree->currentItem()==row(camera_id),"Render View double-click lost Tree selection");
        check(session.revision()==selection_revision&&!window.properties_dialog_,"Render View double-click mutated or opened Properties");
        auto render_revision=session.revision();window.start_render();flush();wait([&]{return window.render_result_current();});
        check(session.revision()==render_revision&&kernel::body_calculation_count.load()==count,"Rendering changed document or calculated CAD");
        check(window.active_render_page_&&window.render_stack_->isVisible()&&!window.document_splitter_->isVisible(),"Render did not open a separate tab");
        auto* page=window.active_render_page_.data();auto* canvas=page->findChild<QWidget*>("renderImageCanvas");check(canvas,"Render image canvas missing");
        const auto initial_height=canvas->height();window.resize(window.width(),window.height()+100);flush();
        check(canvas->height()>initial_height+60,"Render image did not consume available height");
        auto* render_status=page->findChild<QLabel*>("renderProgressStatus");
        check(render_status&&render_status->mapTo(page,QPoint{}).y()-canvas->mapTo(page,QPoint(0,canvas->height())).y()<=16,"Render footer contains unused vertical space");
        check(!page->findChild<QProgressBar*>("renderProgress")->isVisible(),"Completed Render retained its progress controls");
        check(window.grab().save(QString::fromStdWString((directory/"render-result.png").wstring())),"Render result screenshot failed");
        for(const auto* theme:{"light","dark","light"}) {
            auto themed=window.application_settings_;themed.theme=theme;apply_application_appearance(application,themed);flush();
            const auto palette=render_status->palette();
            check(std::abs(palette.color(QPalette::WindowText).lightness()-palette.color(QPalette::Window).lightness())>60,
                "Render result text lost contrast after a live theme change");
            check(session.revision()==render_revision&&kernel::body_calculation_count.load()==count&&window.render_result_current(),
                "Render theme change mutated CAD or invalidated the image");
            check(window.grab().save(QString::fromStdWString((directory/(std::string("render-result-")+theme+".png")).wstring())),"Render theme screenshot failed");
        }
        apply_application_appearance(application,window.application_settings_);flush();
        render::SceneInput oracle;oracle.definition=definition();oracle.appearance=session.document().appearance;
        for(const auto& [id,body]:workspace::read_render_sources(session,render_id))oracle.sources.push_back({id,body});
        const auto expected_pixels=render::Scene(oracle).render({}, {},2);
        const QImage expected(expected_pixels.rgba.data(),int(expected_pixels.width),int(expected_pixels.height),QImage::Format_RGBA8888);
        for(const auto* extension:{"png","jpg"}) {
            const auto target=directory/(std::string("render-export.")+extension);std::filesystem::remove(target);
            QTimer responder;responder.setInterval(20);bool offered=false;
            QObject::connect(&responder,&QTimer::timeout,[&]{if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
                responder.stop();for(const auto& filter:file->nameFilters())if(filter.contains(QString("*.")+extension)){offered=true;file->selectNameFilter(filter);break;}
                file->selectFile(QString::fromStdWString(target.wstring()));QMetaObject::invokeMethod(file,"accept",Qt::DirectConnection);
            }});responder.start();window.export_action_->trigger();flush();responder.stop();
            const QImage exported(QString::fromStdWString(target.wstring()));check(offered&&exported.size()==QSize(320,200),"F7 did not export Render pixels at configured resolution");
            if(std::string_view(extension)=="png")check(exported.convertToFormat(QImage::Format_RGBA8888)==expected,"PNG export contains different pixels or View markers");
        }
        bool export_cancelled=false;QTimer responder;responder.setInterval(20);QObject::connect(&responder,&QTimer::timeout,[&]{if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){export_cancelled=true;responder.stop();file->reject();}});
        responder.start();window.export_action_->trigger();flush();check(export_cancelled&&session.revision()==render_revision,"Render export Cancel changed the document");
        page->findChild<QPushButton*>("renderKeepImage")->click();flush();
        check(definition().images.size()==1&&!page->findChild<QPushButton*>("renderKeepImage")->isEnabled(),"Keep image did not persist exactly once");
        auto* gallery=page->findChild<QListWidget*>("renderGalleryImages");check(gallery->count()==2,"Gallery has no retained thumbnail");
        gallery->setCurrentRow(1);flush();gallery->item(1)->setText("Saved view");flush();
        check(definition().images.front().name=="Saved view","Gallery rename did not commit");
        page->findChild<QPushButton*>("renderDeleteImage")->click();flush();check(definition().images.empty(),"Gallery delete did not commit");
        window.undo_action_->trigger();flush();check(definition().images.size()==1,"Gallery delete Undo failed");
        window.redo_action_->trigger();flush();check(definition().images.empty(),"Gallery delete Redo failed");window.undo_action_->trigger();flush();
        const auto model_tab=[&]{for(int i=0;i<window.tabs_->count();++i)if(window.tabs_->tabData(i).toString().toStdString()==part.document_id)return i;return -1;};
        window.tabs_->setCurrentIndex(model_tab());flush();check(!window.active_render_page_&&view->isVisible(),"Returning from Render did not restore the model View");
        auto changed=*session.document().body_history.find(render_id);changed.render->settings.samples=4096;
        static_cast<void>(workspace::commit_render(window.workspace_,workspace::prepare_render_edit(session.document(),render_id),changed));
        check(!window.render_result_current(),"Changed Render settings reused old result");window.start_render();flush();check(window.render_running(),"Long render did not run in background");
        window.close_render_tab(window.tabs_->currentIndex());flush();check(window.render_running(),"Closing the gallery cancelled background rendering");
        window.stop_render();wait([&]{return !window.render_running();});check(!window.render_result_current(),"Cancelled image was accepted as complete");
        changed.render->settings.samples=256;
        static_cast<void>(workspace::commit_render(window.workspace_,workspace::prepare_render_edit(session.document(),render_id),changed));
        render::SceneInput frozen;frozen.definition=definition();frozen.appearance=session.document().appearance;
        for(const auto& [id,body]:workspace::read_render_sources(session,render_id))frozen.sources.push_back({id,body});
        window.start_render();flush();window.tabs_->setCurrentIndex(model_tab());flush();window.activate_body(sources[0]);flush();
        check(view->isVisible()&&window.sketch_action_->isEnabled()&&window.render_running(),"Background render blocked ordinary modeling");
        const auto background_started=session.revision();const auto background_count=kernel::body_calculation_count.load();
        changed.render->settings.exposure_ev=1;
        static_cast<void>(workspace::commit_render(window.workspace_,workspace::prepare_render_edit(session.document(),render_id),changed));
        // Edit source appearance while the private snapshot is still computing.
        auto edited=session.document();edited.appearance.bodies[sources[2]].color="#CC3333";session.commit_metadata(std::move(edited));
        check(session.revision()>background_started&&window.render_running(),"Model editing did not proceed during rendering");
        QElapsedTimer edit_time;edit_time.start();window.show_tree_item_properties(row(floor.id));flush();
        auto* modeling=window.properties_dialog_;check(modeling,"Ordinary feature Properties did not open during rendering");
        auto* length=modeling->findChild<QDoubleSpinBox*>("extrusionHeight");
        check(length,"Background modeling fixture lost its extrusion length control");length->setValue(3);flush();
        modeling->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.properties_dialog_&&std::abs(test::rectangular_depth(*session.document().find_container(floor.id))-6)<1e-8,
            "Background modeling did not commit the edited solid");
        check(kernel::body_calculation_count.load()>background_count,"Explicit background modeling failed to calculate the solid");
        std::cout<<"Render concurrent extrusion Properties/OK: "<<edit_time.elapsed()<<" ms\n";
        const auto after_modeling=kernel::body_calculation_count.load();
        wait([&]{return !window.render_running();},30000);flush();
        check(kernel::body_calculation_count.load()==after_modeling,"Background render calculated CAD after the explicit edit");
        window.activate_body(render_id);window.show_render_result();flush();page=window.active_render_page_;
        wait([&]{return page->findChild<QPushButton*>("renderKeepImage")->isEnabled();});
        check(!window.render_result_current()&&page->findChild<QPushButton*>("renderExport")->isEnabled(),"Earlier snapshot was cancelled or misidentified as current");
        page->findChild<QPushButton*>("renderKeepImage")->click();flush();check(definition().images.size()==2,"Second snapshot did not join the gallery");
        const auto frozen_pixels=render::Scene(std::move(frozen)).render({}, {},2);
        const QImage frozen_expected(frozen_pixels.rgba.data(),int(frozen_pixels.width),int(frozen_pixels.height),QImage::Format_RGBA8888);
        const auto retained=QImage::fromData(QByteArray::fromBase64(QByteArray::fromStdString(definition().images.back().data->png_base64)),"PNG");
        check(retained.convertToFormat(QImage::Format_RGBA8888)==frozen_expected,"Concurrent source editing contaminated frozen render pixels");
        window.close_render_tab(window.tabs_->currentIndex());flush();window.render_job_.reset();window.show_render_result();flush();
        page=window.active_render_page_;gallery=page->findChild<QListWidget*>("renderGalleryImages");
        check(gallery->count()==2&&!window.render_running()&&page->findChild<QPushButton*>("renderExport")->isEnabled(),"Gallery reopening requires a new render");
        render_revision=session.revision();
        session.document().save(path,session.calculated_boundaries());const auto reopened=document::PartDocument::load(path);check(*reopened.body_history.find(render_id)->render==definition(),"Render native save/reopen changed its definition");
        window.tabs_->setCurrentIndex(model_tab());flush();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir temporary;QSettings ini(temporary.filePath("config.ini"),QSettings::IniFormat);ini.setValue("Application/Language",language);ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
            const auto localized=ApplicationSettings::load(temporary.path());apply_application_translations(application,localized);
            window.show_render_result();flush();page=window.active_render_page_;
            for(const auto& control:std::vector<std::pair<const char*,const char*>>{{"renderKeepImage","Keep image"},{"renderFit","Fit image"},{"renderRenameImage","Rename"},{"renderDeleteImage","Delete image"}})
                check(page->findChild<QPushButton*>(control.first)->text()==localized.qt_translations.value(control.second),"Gallery control did not translate");
            window.tabs_->setCurrentIndex(model_tab());flush();
            window.show_render_body_properties(render_id);flush();check(settings()->windowTitle()==localized.qt_translations.value("Render Body properties"),"Render title did not translate");
            check(settings()->findChild<QComboBox*>("renderSourceMode")->itemText(0)==localized.qt_translations.value("Visible Bodies"),"Render source mode did not translate");settings()->reject();flush();
            window.show_render_object_properties(document::RenderObjectKind::Camera,camera_id);flush();check(object()->windowTitle()==localized.qt_translations.value("Render object properties"),"Render object title did not translate");
            check(std::ranges::any_of(object()->findChildren<QLabel*>(),[&](const auto* label){return label->text()==localized.qt_translations.value("Camera frame height");}),"Render camera parameter did not translate");object()->reject();flush();
        }
        apply_application_translations(application,window.application_settings_);window.activate_body(sources[0]);flush();check(window.sketch_action_->isEnabled()&&!window.render_mode_button_->isVisible(),"Leaving Render did not restore material commands");
        const auto saved_images=definition().images;window.save_active_document();flush();window.close_document(model_tab());flush();
        check(!window.workspace_.open_part(part.document_id)&&window.render_tabs_.empty(),"Closing the Part retained orphan Render tabs");
        check(window.open_document_path(QString::fromStdWString(path.wstring())),"Gallery Part did not reopen in the GUI");flush();
        window.activate_body(render_id);window.show_render_result();flush();page=window.active_render_page_;
        const auto& loaded_images=window.workspace_.open_part(part.document_id)->session.document().body_history.find(render_id)->render->images;
        check(loaded_images==saved_images&&page&&page->findChild<QListWidget*>("renderGalleryImages")->count()==2,
            "GUI save/close/reopen lost retained images or required rendering");
        check(window.grab().save(QString::fromStdWString((directory/"render-gallery-reopened.png").wstring())),"Reopened gallery screenshot failed");
        std::cout<<"Render GUI: source selection, whole-Origin objects, Cancel/unchanged OK, Undo/Redo, private rendering, JPEG/PNG export, background snapshots, gallery Undo/Redo, save/reopen and five languages passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';window.grab().save(QString::fromStdWString((directory/"render-failure.png").wstring()));return 1;}
}
} // namespace zima::app
