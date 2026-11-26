int verify_cylindrical_angle_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,10));
        auto source=document::PartDocument::create_feature_container(sketch.id);source.feature.sides[0].length=25;
        sketch.owner_container_id=source.id;part.history={source};part.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Angular placement");
        graph.insert({document::PartHistoryKind::Feature,source.id});part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel engine;const auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
        const auto face=std::ranges::find_if(calculated.back().mesh.original_references.triangle_references,[](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;});
        check(face!=calculated.back().mesh.original_references.triangle_references.end(),"Angular fixture has no cylinder");
        const auto path=directory/"cylindrical-angle.prtz";part.save(path,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,950);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Angular fixture cannot open");flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
        };
        run("body.activate",{{"body",body}});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(tree&&view,"Angular View/Tree missing");
        const auto dialog=[&] {
            for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
                if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(candidate))return p;
            throw std::runtime_error("Angular Properties missing");
        };
        const auto row=[&](const std::string& id) {
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id)return *i;
            throw std::runtime_error("Angular Tree row missing");
        };
        const auto mouse=[&](QWidget* widget,QPointF at) {
            for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,at,QPointF(widget->mapToGlobal(at.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&event);
            }flush();
        };
        const auto section=[&] {
            for(auto* child:dialog()->findChildren<QObject*>())if(auto* p=dynamic_cast<ui::ContainerPlacementSection*>(child))return p;
            throw std::runtime_error("Angular placement controls missing");
        };
        window.findChild<QAction*>("featureShortcut0Action")->trigger();flush();
        const auto target=dialog()->pending_value().id;
        auto* table=section()->reference_table();table->cellClicked(0,1);view->fit_all();flush();
        std::optional<QPointF> pixel;
        for(int y=20;y<view->height()-20&&!pixel;y+=9)for(int x=20;x<view->width()-20&&!pixel;x+=9) {
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Face&&
                candidates.front().owner_id==face->owner_id&&candidates.front().semantic_key==face->semantic_key)pixel=QPointF(x,y);
        }
        check(pixel.has_value(),"Cylinder is not offered for angular placement");mouse(view,*pixel);
        check(dialog()->pending_value().placement.references.size()>=1,"Cylinder click did not enter a reference");
        document::ConstructionReference axial{{},source.container_origin.id,"origin:plane:xy",8,true};
        axial.measured_offset=0;
        check(dialog()->set_reference(1,axial,QStringLiteral("Axial plane")),"Angular axial plane entry failed");flush();
        check(section()->set_reference_offset(1,8),"Angular axial distance is not editable");flush();
        auto* type=qobject_cast<QComboBox*>(table->cellWidget(2,2));
        check(type&&type->findData(2)>=0,"Angle is missing from the empty reference row");
        type->setCurrentIndex(type->findData(2));flush();
        document::ConstructionReference zero{{},source.container_origin.id,"origin:plane:yz",0,true};
        zero=dialog()->prepare_reference_candidate(2,zero);
        check(dialog()->set_reference(2,zero,QStringLiteral("Zero plane")),"Angular zero plane entry failed");flush();
        check(dialog()->pending_value().placement.references[2].use_angle&&
            !dialog()->pending_value().placement.references[2].orientation_drives_rotation,"Angle picked up a FRONT/TOP role");
        const auto equations=[&](double angle,const document::Placement& value) {
            const double radians=(angle+90.)*std::acos(-1.)/180.;
            check(std::hypot(value.x-10*std::cos(radians),value.y-face->surface->axis.z*10*std::sin(radians),value.z-8)<1e-6,
                "Angular GUI placement violates radius/angle/axial equations");
        };
        const auto change=[&](double degrees) {
            check(section()->set_reference_offset(2,degrees),"Angular value is not editable");flush();
            equations(degrees,dialog()->pending_value().placement);
        };
        for(double angle:{0.,90.,-45.,180.,450.,30.})change(angle);
        const auto settings=app::ApplicationSettings::load(QString::fromStdString(directory.string()));
        type=qobject_cast<QComboBox*>(table->cellWidget(2,2));
        check(type->currentText()==settings.qt_translations.value(QString::fromUtf8("Úhel")),"Angle mode is not localized");
        check(table->horizontalHeaderItem(3)->text()==settings.qt_translations.value(QString::fromUtf8("Hodnota")),"Mixed quantity header is not localized");
        auto* value=qobject_cast<QDoubleSpinBox*>(table->cellWidget(2,3));check(value,"Angular numeric field missing");
        check(value->suffix().contains(QString::fromUtf8("°")),"Angular reference is shown as a length");
        window.grab().save(QString::fromStdString((directory/"angle-draft.png").string()));
        const auto pending=dialog()->pending_value().placement;
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();run("save");
        auto saved=document::PartDocument::load(path);
        check(saved.find_container(target)&&saved.find_container(target)->placement.references==pending.references,"Angle OK did not persist its reference");
        equations(30,saved.find_container(target)->placement);
        run("undo");run("save");check(!document::PartDocument::load(path).find_container(target),"Angle creation Undo retained the feature");
        run("redo");run("save");equations(30,document::PartDocument::load(path).find_container(target)->placement);
        const auto edit=[&] {window.show_tree_item_properties(row(target));flush();return dialog();};
        edit();change(120);dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
        check(document::PartDocument::load(path).find_container(target)->placement.references==pending.references,"Angle Cancel changed references");
        const auto before=run("feature.get",{{"container",target}}).at("revision");
        edit();dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(run("feature.get",{{"container",target}}).at("revision")==before,"Unchanged angular OK created an Undo transaction");
        edit();change(60);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();run("save");
        equations(60,document::PartDocument::load(path).find_container(target)->placement);
        run("undo");run("save");equations(30,document::PartDocument::load(path).find_container(target)->placement);
        run("redo");run("save");equations(60,document::PartDocument::load(path).find_container(target)->placement);
        run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Angle native reopen failed");flush();run("body.activate",{{"body",body}});
        edit();equations(60,dialog()->pending_value().placement);
        // Whole-Origin entry remains the existing three-plane contract.
        table=section()->reference_table();
        for(int index:{2,1,0}) {
            auto* remove=table->cellWidget(index,0)->findChild<QPushButton*>();check(remove,"Angular reference clear control missing");remove->click();flush();
        }
        mouse(table->viewport(),table->visualItemRect(table->item(0,1)).center());
        row(source.id)->setExpanded(false);
        auto* origins=dialog()->findChild<QPushButton*>("containerOriginSelectionButton");
        check(origins,"Origin chooser button is missing");origins->click();flush();view->fit_all();flush();
        const auto camera=view->camera_state();
        pixel.reset();
        for(int y=20;y<view->height()-20&&!pixel;y+=9)for(int x=20;x<view->width()-20&&!pixel;x+=9) {
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Container&&candidates.front().owner_id==source.id)pixel=QPointF(x,y);
        }
        check(pixel.has_value(),"Origin chooser does not offer the source solid");mouse(view,*pixel);
        auto* origin=row(source.container_origin.id);
        check(origin->isSelected()&&tree->currentItem()==origin&&row(source.id)->isExpanded()&&
            tree->viewport()->rect().intersects(tree->visualItemRect(origin)),"Origin chooser did not reveal and select its Tree row");
        check(dialog()->pending_value().placement.references.empty()&&!origins->isChecked()&&view->camera_state()==camera,
            "Revealing an Origin committed placement or changed the camera");
        mouse(tree->viewport(),tree->visualItemRect(origin).center());
        check(document::placement_references_use_whole_origin(dialog()->pending_value().placement.references),"Angular editing broke whole-Origin entry");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
        equations(60,document::PartDocument::load(path).find_container(target)->placement);
        std::cout<<"Angular placement GUI: cylinder click, creation, successive angles, localization, OK/Cancel, unchanged OK, native reopen, Undo/Redo and whole-Origin replacement passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
