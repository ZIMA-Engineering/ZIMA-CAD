#include "assembly_workspace_window.hpp"
#include "body_properties_dialog.hpp"
#include "../tests/profile_solid_fixture.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QSettings>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <iostream>

namespace zima::app {
int verify_body_link(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto source=document::PartDocument::create_default();auto feature=test::rectangular_feature(source,{10,10,10});source.history={feature};
        document::BodyHistoryGraph graph;const auto first=graph.create_body("Original");graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});
        document::BodyHistory scaled;scaled.scope.id="scaled-source";scaled.name="Scaled";scaled.scale=document::BodyScale{first,2,{}};static_cast<void>(graph.create_scale(scaled));source.set_body_history(graph);
        kernel::OcctKernel kernel;const auto source_file=directory/"body-link-source.prtz",target_file=directory/"body-link-ui.prtz";
        source.save(source_file,workspace::calculate_part_with_resolved_references(kernel,source));
        auto target=document::PartDocument::create_default();auto target_graph=target.body_history;target_graph.activate({});target.set_body_history(target_graph);target.save(target_file,{});
        window.resize(1200,900);window.show();check(window.open_document_path(QString::fromStdString(target_file.string())),"Link target did not open");flush();
        check(window.execute_console_command("body.activate").ok,"Part context could not be activated");flush();
        const auto initial_count=window.workspace_.open_part(target.document_id)->session.document().body_history.bodies().size();
        const auto open=[&]{window.insert_linked_body(QString::fromStdString(source_file.string()));flush();return dynamic_cast<BodyPropertiesDialog*>(window.findChild<QDialog*>("bodyLinkDialog"));};
        auto* dialog=open();check(dialog,"Link chooser did not open");
        check(dialog->parentWidget()==&window&&(dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Link properties are not internal");
        auto* choices=dialog->findChild<QComboBox*>("bodyLinkSourceBody");check(choices&&choices->count()==2,"Source Bodies are not selectable");
        choices->setCurrentIndex(choices->findData("scaled-source"));flush();
        check(dialog->pending_value().link->body_id=="scaled-source"&&dialog->pending_value().entries.empty(),"Chooser did not select a history-free source");
        auto* refs=dialog->findChild<QTableWidget*>("bodyReferenceTable");check(refs&&dialog->pending_value().scope.placement.references.size()==5,"Link has no shared Origin placement");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* origin=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3).toString()=="document-origin"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==target.document_id+":origin"){origin=*it;break;}
        check(origin,"Target Part Origin is absent from Tree");tree->setCurrentItem(origin);flush();
        check(dialog->first_empty_position_index()==3&&dialog->pending_value().scope.placement.references.size()==5,"Whole-Origin entry lost link placement");
        auto replacement=dialog->pending_value().scope.placement.references[2];replacement.offset=5;
        check(dialog->set_reference(2,replacement,"Origin YZ"),"Link reference replacement failed");flush();
        check(dialog->pending_value().scope.placement.references[2].offset==5,"Link reference offset was lost");
        dialog->set_reference_inspected(0,true);check(!dialog->highlighted_reference_entries().empty(),"Link placement inspection failed");dialog->clear_reference_highlights();
        const auto y=refs->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width()+100,dialog->height()+150);flush();check(y==refs->mapTo(dialog,QPoint{}).y(),"Link resize redistributed rows");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.workspace_.open_part(target.document_id)->session.document().body_history.bodies().size()==initial_count,"Cancel inserted a link");
        dialog=open();choices=dialog->findChild<QComboBox*>("bodyLinkSourceBody");choices->setCurrentIndex(choices->findData("scaled-source"));flush();
        const auto id=dialog->pending_value().scope.id;
        window.grab().save(QString::fromStdString((directory/"body-link-properties.png").string()));
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("bodyLinkDialog"),"Link OK failed");
        auto* part=window.workspace_.open_part(target.document_id);check(part->session.document().body_history.find(id),"Link was not inserted");
        check(std::abs(part->session.calculated_boundaries().back().volume-8000)<1e-7,"Selected scaled Body has wrong geometry");
        part->session.document().save(target_file,part->session.calculated_boundaries());
        window.show_body_properties(id);flush();dialog=dynamic_cast<BodyPropertiesDialog*>(window.findChild<QDialog*>("bodyLinkDialog"));
        check(dialog&&dialog->pending_value().link->body_id=="scaled-source","Link did not reopen for editing");
        check(dialog->pending_value().scope.placement.references.size()==5,"Reopening lost Origin placement");
        choices=dialog->findChild<QComboBox*>("bodyLinkSourceBody");choices->setCurrentIndex(choices->findData(QString::fromStdString(first)));
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(part->session.document().body_history.find(id)->link->body_id=="scaled-source","Cancel changed linked source");
        window.show_body_properties(id);flush();
        auto* view=window.findChild<QOpenGLWidget*>("modelWorkspace");check(view,"Link View unavailable");
        const QPointF point(view->width()/2.,view->height()/2.);
        QMouseEvent confirm(QEvent::MouseButtonDblClick,point,point,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&confirm);flush();check(!window.findChild<QDialog*>("bodyLinkDialog"),"View middle double click did not confirm link");
        QTemporaryDir translations;check(translations.isValid(),"Translation fixture directory unavailable");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(translations.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString((std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization").string()));config.sync();
            const auto settings=ApplicationSettings::load(translations.path());apply_application_translations(application,settings);
            window.show_body_properties(id);flush();dialog=dynamic_cast<BodyPropertiesDialog*>(window.findChild<QDialog*>("bodyLinkDialog"));check(dialog,"Localized link properties failed");
            const auto labels=dialog->findChildren<QLabel*>();
            for(const auto* key:{"Source Part","Source Body"})check(std::ranges::any_of(labels,[&](const auto* label){return label->text()==settings.qt_translations.value(key);}),"Link source label is untranslated");
            check(dialog->findChild<QComboBox*>("bodyLinkSourceBody")->currentText()=="Scaled","Localization changed a source Body name");
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        }
        apply_application_translations(application,window.application_settings_);
        std::cout<<"Linked Body chooser, shared placement, inspection, resize, OK, reopen and Cancel passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Linked Body UI: "<<error.what()<<'\n';return 1;}
}
}
