int verify_inspection_history_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();auto stock=test::rectangular_feature(part);
        part.history={stock};document::BodyHistoryGraph graph;const auto body=graph.create_body("Reorder Body");
        graph.insert({document::PartHistoryKind::Feature,stock.id});part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel engine;auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
        auto fillet=document::PartDocument::create_fillet_container({calculated.back().mesh.edges.front().reference});
        fillet.edge_treatment.primary_size=1;part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);part.history.push_back(fillet);
        document::BodyProperties mass;mass.id=kernel::make_stable_id();mass.name="Historical mass";mass.body_id=body;mass.after_object_id=stock.id;
        part.body_properties.push_back(mass);
        kernel::SavedMeasurement measure;measure.id=kernel::make_stable_id();measure.name="Saved measurement";
        measure.body_id=body;measure.after_object_id=stock.id;measure.references={{kernel::MeasurementKind::Object,stock.id,{},{}}};measure.values.resize(1);
        part.measurements.push_back(measure);
        calculated=workspace::calculate_part_with_resolved_references(engine,part);
        check(calculated.back().calculation_errors.empty(),"Invalid history drag fixture");
        const auto path=directory/"inspection-history.prtz";part.save(path,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1250,900);window.show();
        QTimer guard;bool unexpected=false;
        QObject::connect(&guard,&QTimer::timeout,&window,[&]{for(auto* child:application.topLevelWidgets())
            if(auto* message=qobject_cast<QMessageBox*>(child);message&&message->isVisible()){
                std::cerr<<message->text().toStdString()<<std::endl;unexpected=true;message->reject();}});guard.start(100);
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open history drag fixture");flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()){
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
        };
        run("body.activate",{{"body",body}});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"History Tree missing");
        const auto row=[&](const std::string& id){for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;
            throw std::runtime_error("Inspection history row missing");};
        const auto drag=[&](const std::string& source,const std::string& target,bool after){
            tree->collapseAll();for(auto* parent=row(source)->parent();parent;parent=parent->parent())parent->setExpanded(true);flush();
            tree->scrollToItem(row(target));flush();
            const auto start=tree->visualItemRect(row(source)).center(),
                end=after?tree->visualItemRect(row(target)).bottomLeft()+QPoint(60,0):tree->visualItemRect(row(target)).topLeft()+QPoint(60,1);
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,QEvent::MouseButtonRelease}){
                const auto at=type==QEvent::MouseButtonPress?start:end;
                QMouseEvent mouse(type,QPointF(at),QPointF(tree->viewport()->mapToGlobal(at)),
                    type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(),&mouse);
            }flush();check(!unexpected,"History drag opened an unexpected error");
        };
        const auto order=[&]{std::vector<std::string> ids;auto* parent=row(stock.id)->parent();
            for(int i=0;i<parent->childCount();++i){const auto id=parent->child(i)->data(0,Qt::UserRole).toString().toStdString();
                if(id==stock.id||id==fillet.id||id==mass.id||id==measure.id)ids.push_back(id);}return ids;};
        const std::vector<std::string> initial{stock.id,mass.id,measure.id,fillet.id},crossed{stock.id,fillet.id,mass.id,measure.id};
        check(order()==initial,"Inspection rows were not at their stored boundary");
        const auto revision=run("history.list").at("revision");
        drag(fillet.id,mass.id,false);check(order()==crossed,"Fillet could not pass measurements in the Tree");
        check(run("history.list").at("revision")==revision.get<std::uint64_t>()+1,"Tree crossing did not commit exactly once");
        run("undo");check(order()==initial,"Tree crossing Undo failed");run("redo");check(order()==crossed,"Tree crossing Redo failed");
        drag(fillet.id,measure.id,true);check(order()==initial,"Fillet could not pass measurements in the reverse direction");
        drag(mass.id,fillet.id,true);check(order()==std::vector<std::string>{stock.id,measure.id,fillet.id,mass.id},"Body measurement could not move in the Tree");
        drag(measure.id,mass.id,true);check(order()==crossed,"Ordinary measurement could not move in the Tree");
        const auto unchanged=run("history.list").at("revision");drag(stock.id,fillet.id,true);
        check(order()==crossed&&run("history.list").at("revision")==unchanged,"Tree drag crossed the Fillet's source dependency");
        run("save");run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Moved history failed to reopen");flush();
        check(order()==crossed,"Reopening lost measurement/Fillet order");
        window.grab().save(QString::fromStdString((directory/"inspection-history.png").string()));
        std::cout<<"Actual Tree drag: Fillet across both measurement kinds in both directions, measurement moves, dependencies, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
