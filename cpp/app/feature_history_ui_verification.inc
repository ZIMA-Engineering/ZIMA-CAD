static int verify_feature_history_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_FEATURE_HISTORY_SOURCE").toStdString());
        const auto owner=qEnvironmentVariable("ZIMA_VERIFY_FEATURE_HISTORY_OWNER").toStdString();
        const auto file=directory/"feature-history-edit.prtz";
        std::filesystem::copy_file(source,file,std::filesystem::copy_options::overwrite_existing);
        const auto original=document::PartDocument::load(file);const auto* body=original.body_owner_for_object(owner);
        const auto* feature=original.find_container(owner);check(body&&feature,"History performance source Feature/Body missing");
        const auto& side=feature->feature.sides[0];
        const bool up_to=side.extrusion_extent==document::EndCondition::UpTo;
        const bool thickness=qEnvironmentVariableIsSet("ZIMA_VERIFY_FEATURE_HISTORY_THICKNESS");
        const auto initial_length=thickness?feature->feature.thin_thickness:(up_to?side.up_to.length_correction:side.length);
        const std::array increments=thickness?std::array{.2,.4}:std::array{5.,10.};
        const auto parameter=up_to?"side0_up_to_length_correction":"side0_length";
        app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1280,900);window.show();
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"History performance copy cannot open");flush();
        check(activate_test_body(application,window,body->scope.id),"History performance Body cannot activate");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"History performance Tree missing");
        unsigned step{};
        for(double increment:increments) {
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==owner&&
                (*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row,"History performance Feature row missing");
            QElapsedTimer timer;timer.start();window.show_tree_item_properties(row);flush();
            auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog,"History performance Properties missing");
            std::cout<<"Feature history GUI open step="<<step<<" ms="<<timer.elapsed()<<std::endl;
            const auto before=window.execute_console_command("documents").data;
            std::cout<<"Feature history GUI change BEGIN step="<<step<<std::endl;timer.restart();
            if(thickness) {
                auto* field=dialog->findChild<QDoubleSpinBox*>("featureThickness");
                check(field&&field->isVisible()&&field->isEnabled(),"History performance thin thickness is not editable");
                field->setValue(initial_length+increment);
            } else check(dialog->set_inline_parameter_value(parameter,initial_length+increment),"History performance driving value is not editable");
            flush();
            check(window.execute_console_command("documents").data==before,"Draft length edit changed the model before OK");
            std::cout<<"Feature history GUI draft step="<<step<<" ms="<<timer.elapsed()<<std::endl;timer.restart();
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            std::cout<<"Feature history GUI change END step="<<step<<" commit_ms="<<timer.elapsed()<<std::endl;
            check(!window.findChild<QWidget*>("featurePropertiesDialog"),"History performance OK did not complete");
            ++step;
        }
        check(window.execute_console_command("undo").ok,"History performance Undo failed");flush();
        check(window.execute_console_command("redo").ok,"History performance Redo failed");flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto saved=document::PartDocument::load(file);
        const auto* saved_feature=saved.find_container(owner);
        check(saved_feature&&std::abs((thickness?saved_feature->feature.thin_thickness:(up_to?saved_feature->feature.sides[0].up_to.length_correction:saved_feature->feature.sides[0].length))-initial_length-increments.back())<1e-9,
            "History performance native save/reopen lost the edited value");
        const auto old_sketch=std::ranges::find(original.sketches,feature->feature.sketch_id,&sketcher::Sketch::id);
        const auto new_sketch=std::ranges::find(saved.sketches,feature->feature.sketch_id,&sketcher::Sketch::id);
        check(saved.history.size()==original.history.size()&&saved.sketches.size()==original.sketches.size()&&
            old_sketch!=original.sketches.end()&&new_sketch!=saved.sketches.end()&&old_sketch->serialized()==new_sketch->serialized(),
            "History performance length edit changed its authored profile/history count");
        std::cout<<"Feature history GUI: first/repeated parameter edits, draft isolation, OK, Undo/Redo and native value reopen passed"<<std::endl;
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
