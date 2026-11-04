static int verify_sketch_external_midpoint_ui(QApplication& application,
        const std::filesystem::path& parent_directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent_directory/"external-midpoint-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create isolated midpoint workspace");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        QSettings config(isolated.filePath("config.ini"),QSettings::IniFormat);
        config.setValue("Application/Language","en");
        for(const auto* key:{"Uppercase","RemoveDiacritics","ReplaceSpaces"})config.setValue(QString("DocumentNames/")+key,false);
        config.sync();
        for(bool in_assembly:{false,true}) {
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
                const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
                if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
            };
            const auto name=in_assembly?"external-midpoint-assembly":"external-midpoint-part";
            run("new",{{"type","part"},{"name",std::string(name)+"-source"}});
            const std::string source=run("sketch.create",{{"name","Source"},{"plane","XY"}}).at("sketch");
            const std::string segment=run("sketch.segment.create",{{"sketch",source},{"first",{-30,12}},{"second",{70,12}}}).at("geometry");
            const auto source_document=run("documents").back().at("id").get<std::string>();
            run("save");const auto source_file=directory/(std::string(name)+"-source.prtz");
            const auto source_definition=document::PartDocument::load(source_file).sketches.front().serialized();
            std::string occurrence;
            if(in_assembly) {
                run("new",{{"type","assembly"},{"name",name}});
                occurrence=run("component.insert",{{"source",source_document}}).at("occurrence");
            }
            const std::string target=run("sketch.create",{{"name","Target"},{"plane","XY"}}).at("sketch");
            auto reference=commands::Json{{"sketch",target},{"kind","edge"},{"owner",source},{"key","segment:"+segment}};
            if(in_assembly)reference["instance_path"]=assembly::InstancePath{}.child(occurrence).encoded();
            run("sketch.reference.create",std::move(reference));
            std::string midpoint;
            const auto entities=run("sketch.entities",{{"sketch",target}});
            for(const auto& item:entities.at("items"))if(item.at("kind")=="external_reference") {
                const auto entity=run("sketch.entity.get",{{"sketch",target},{"entity",item.at("id")}}).at("entity");
                if(entity.at("kind")=="edge_midpoint")midpoint=item.at("id");
            }
            check(!midpoint.empty(),"GUI reference creation omitted its independently stored midpoint");
            run("save");const auto file=in_assembly?directory/(std::string(name)+".asmz"):source_file;
            const auto read=[&] {
                if(in_assembly)return assembly::AssemblyDocument::load(file).sketches;
                return document::PartDocument::load(file).sketches;
            };
            const auto before=read();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target&&
                ((*i)->data(0,Qt::UserRole+3)=="part-sketch"||(*i)->data(0,Qt::UserRole+3)=="assembly-sketch")){row=*i;break;}
            check(row,"Target Sketch Tree row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open&&open->isVisible(),"Target Sketch cannot enter");open->click();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Midpoint GUI View missing");
            view->fit_points({{-60,-40,0},{100,50,0}});flush();QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            const auto ray_local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Midpoint GUI ray missing");const auto q=before.back().intersect_ray(ray->first,ray->second);check(q.has_value(),"Midpoint GUI plane missing");return *q;};
            const auto a=ray_local({0,0}),b=ray_local({100,0}),c=ray_local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const QPointF at(((20-a[0])*vy-(12-a[1])*vx)/det,(ux*(12-a[1])-uy*(20-a[0]))/det);
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            auto* point_action=window.findChild<QAction*>("sketchPointAction");check(point_action&&point_action->isEnabled(),"Point drawing action missing");point_action->trigger();flush();
            mouse(at,QEvent::MouseMove,Qt::NoButton);
            check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="external_point:"+midpoint,"Point tool cannot catch the external midpoint cross");
            mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            const auto metadata=run("sketch.get",{{"sketch",target}});
            check(metadata.at("counts").at("points")==1&&metadata.at("counts").at("constraints")==1,"Midpoint click did not create exactly one native point and one C");
            row=nullptr;
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==midpoint&&(*i)->data(0,Qt::UserRole+3)=="sketch-external-reference"){row=*i;break;}
            check(row,"Stored midpoint has no independently removable Tree row");
            for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
            tree->scrollToItem(row);flush();const auto pixel=tree->visualItemRect(row).center();
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(pixel),QPointF(tree->viewport()->mapToGlobal(pixel)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(pixel),QPointF(tree->viewport()->mapToGlobal(pixel)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&release);flush();
            QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&window,&remove);flush();
            const auto removed=run("sketch.get",{{"sketch",target}}).at("counts");
            check(removed.at("external_references")==3&&removed.at("points")==1&&removed.at("constraints")==0,"Deleting midpoint cross changed its edge/ends/native point or retained C");
            for(const auto* action:{"undoAction","redoAction","undoAction"}){window.findChild<QAction*>(action)->trigger();flush();}
            check(run("sketch.get",{{"sketch",target}}).at("counts").at("constraints")==1,"Midpoint deletion Undo/Redo lost C");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* box=dialog->findChild<QDialogButtonBox*>();box&&box->button(QDialogButtonBox::Ok)){box->button(QDialogButtonBox::Ok)->click();flush();break;}
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto after=read();const auto result=std::ranges::find(after,target,&sketcher::Sketch::id);
            check(result!=after.end()&&result->external_references.size()==4&&result->constraints.size()==1&&result->constraints.front().kind==sketcher::ConstraintKind::PointReference&&result->constraints.front().second_point_id==midpoint,"Native reopen lost the exact midpoint C identity");
            check(document::PartDocument::load(source_file).sketches.front().serialized()==source_definition,"Target midpoint editing changed its read-only source Sketch");
        }
        std::cout<<"External midpoint GUI: Part/Assembly source creation, exact cross picking, point C, Tree/Delete independent removal, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"External midpoint GUI: "<<e.what()<<'\n';return 1;}
}
