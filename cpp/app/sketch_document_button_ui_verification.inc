int verify_sketch_document_button_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto original_palette=application.palette();
    try {
        unsigned cases=0;
        for(bool assembly_mode:{false,true})for(unsigned theme=0;theme<3;++theme) {
            std::cout<<"Sketch header case "<<cases<<" assembly="<<assembly_mode<<" theme="<<theme<<std::endl;
            auto appearance=app::ApplicationSettings{};appearance.theme=theme==1?"dark":"light";
            app::apply_application_appearance(application,appearance);
            auto sketch=sketcher::Sketch::create_default();
            auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;
            const auto path=directory/("sketch-header-"+std::to_string(cases)+(assembly_mode?".asmz":".prtz"));
            std::string body;
            if(assembly_mode) {
                auto doc=assembly::AssemblyDocument::create_default();doc.insert_sketch(sketch,owner);doc.save(path);
            } else {
                auto doc=document::PartDocument::create_default();doc.history={owner};doc.sketches={sketch};
                document::BodyHistoryGraph graph;body=graph.create_body("Sketch header test");
                graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.save(path);
            }
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));
            window.resize(1200,850);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Sketch header fixture did not open");flush();
            if(!assembly_mode){check(activate_test_body(application,window,body),"Sketch header Body did not activate");flush();}
            app::apply_application_appearance(application,appearance);flush();
            check((application.palette().color(QPalette::Window).lightness()<128)==(theme==1),"Sketch header initial theme did not apply");
            auto* button=window.findChild<QToolButton*>("documentKindButton");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            check(button&&button->isVisible()&&tree,"Drawing shortcut missing outside Sketcher");
            for(unsigned cycle=0;cycle<2;++cycle) {
                QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&
                    (*it)->data(0,Qt::UserRole+3)==(assembly_mode?"assembly-sketch":"part-sketch")){row=*it;break;}
                check(row,"Sketch header row missing");window.show_tree_item_properties(row);flush();
                QPushButton* open{};
                for(auto* candidate:window.findChildren<QPushButton*>("sketchOpenButton"))if(candidate->isVisible()){open=candidate;break;}
                if(!open)window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"sketch-header-properties-failure.png")));
                check(open,"Sketch Properties did not open");
                open->click();flush();
                check(!button->isVisible(),"Drawing shortcut remains visible inside Sketcher");
                check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("sketch-header-inside-"+std::to_string(cases)+"-"+std::to_string(cycle)+".png")))),"Sketcher proof capture failed");
                const auto dark=theme==1;
                auto changed=app::ApplicationSettings{};changed.theme=dark?"light":"dark";
                app::apply_application_appearance(application,changed);flush();
                check(!button->isVisible(),"Theme refresh exposed the Drawing shortcut inside Sketcher");
                auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Sketch finish unavailable");
                finish->trigger();flush();
                check(button->isVisible(),"Drawing shortcut did not return after finishing Sketcher");
                QDialog* returned{};
                for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible()&&candidate->findChild<QPushButton*>("sketchOpenButton")){returned=candidate;break;}
                check(returned,"Finishing Sketcher did not return to Properties");
                auto* actions=returned->findChild<QDialogButtonBox*>();check(actions,"Sketch Properties actions missing");
                actions->button(cycle?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
                check(button->isVisible(),"Closing Sketch Properties hid the Drawing shortcut");
            }
            check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("sketch-header-"+std::to_string(cases)+".png")))),"Sketch header proof capture failed");
            ++cases;window.hide();
        }
        application.setPalette(original_palette);
        std::cout<<"Sketch header Drawing shortcut: "<<cases<<" Part/Assembly Light/Dark/Light cases, two editing cycles, OK/Cancel and live theme refresh passed\n";
        return 0;
    }catch(const std::exception& error){application.setPalette(original_palette);std::cerr<<error.what()<<'\n';return 1;}
}
