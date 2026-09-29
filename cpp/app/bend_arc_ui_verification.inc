int verify_arc_only_bend_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();flush();
        auto* sheet=window.findChild<QAction*>("applicationModeAction2");
        auto* menu=window.findChild<QMenu*>("insertMenu");
        // Exercise the actual Sheet Metal command on an otherwise empty Body.
        auto bend_part=document::PartDocument::create_default();document::BodyHistoryGraph bend_graph;
        const auto bend_body=bend_graph.create_body("Bend body");bend_part.set_body_history(bend_graph);
        document::set_sheet_metal_defaults(bend_part,{2.,.4});const auto bend_path=directory/"bend-ui.prtz";bend_part.save(bend_path);
        check(window.open_document_path(QString::fromStdString(bend_path.string())),"Bend fixture failed to open");flush();
        check(activate_test_body(application,window,bend_body),"Cannot activate Bend body");sheet->trigger();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* model_view=window.findChild<QOpenGLWidget*>();
        check(tree&&model_view,"Bend document View or Tree is missing");
        const QPointF click(50,50);
        QMouseEvent middle(QEvent::MouseButtonDblClick,click,QPointF(model_view->mapToGlobal(click.toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        auto* flat_action=window.findChild<QAction*>("flatAction");
        const auto flat_dialog=[&]{return dynamic_cast<app::SketchPropertiesDialog*>(window.findChild<QDialog*>("flatPropertiesDialog"));};
        app::SketchPropertiesDialog* flat=nullptr;

        auto* bend_action=window.findChild<QAction*>("bendAction");check(bend_action&&!bend_action->icon().isNull()&&menu->actions().contains(bend_action),"Bend missing from Sheet Metal toolbar/Insert");
        const auto bend_dialog=[&]{return window.findChild<QDialog*>("bendPropertiesDialog");};
        bend_action->trigger();flush();auto* bend=bend_dialog();check(bend&&bend->windowType()==Qt::SubWindow,"Bend properties did not open internally");
        auto* thickness=bend->findChild<QDoubleSpinBox*>("bendThickness");auto* override=bend->findChild<QCheckBox*>("bendThicknessOverride");
        check(thickness&&override&&!override->isChecked()&&!thickness->isEnabled()&&thickness->value()==2.,"Bend did not inherit Part thickness");
        auto* radius_override=bend->findChild<QCheckBox*>("bendRadiusOverride");
        check(radius_override&&!radius_override->isChecked(),"New Bend must inherit its radius by default");flush();
        check(!bend->findChild<QDoubleSpinBox*>("bendRadius")->isEnabled()&&bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==2.,"Bend linked radius does not follow thickness");
        radius_override->setChecked(true);flush();
        check(bend->findChild<QDoubleSpinBox*>("bendRadius")->isEnabled(),"Custom radius did not enable editing");
        bend->findChild<QDoubleSpinBox*>("bendRadius")->setValue(7.);
        radius_override->setChecked(false);flush();
        check(!bend->findChild<QDoubleSpinBox*>("bendRadius")->isEnabled()&&bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==2.,
            "Disabling custom radius did not restore thickness inheritance");
        radius_override->setChecked(true);flush();
        check(bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==2.,"Enabling custom radius changed the effective radius");
        auto* hem=bend->findChild<QCheckBox*>("bendHem");check(hem&&!hem->isChecked(),"New Bend unexpectedly starts as a hem");
        hem->setChecked(true);flush();
        check(bend->findChild<QDoubleSpinBox*>("bendAngle")->value()==180.&&bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==0.&&
            !bend->findChild<QDoubleSpinBox*>("bendAngle")->isEnabled()&&!radius_override->isEnabled(),"Hem must set and protect 180 degrees / zero inner radius");
        hem->setChecked(false);flush();
        check(bend->findChild<QDoubleSpinBox*>("bendAngle")->value()==90.&&bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==2.&&radius_override->isEnabled(),"Disabling hem must restore pending Bend parameters");
        for(const auto* button_name:{"bendPathSketchButton","sketchOpenButton","bendEndSketchButton"}) {
            auto* button=bend->findChild<QPushButton*>(button_name);check(button,"One of the three Bend Sketch editors is missing");
            check(button->styleSheet().contains("#00D1FF"),"Bend Sketch button does not use the shared green style");
            button->click();flush();check(!bend->isVisible(),"Bend properties did not yield to owned Sketch editor");
            auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Owned Bend Sketch did not activate");
            finish->trigger();flush();check(bend->isVisible(),"Owned Bend Sketch did not return to properties");
        }
        override->setChecked(true);thickness->setValue(3.);override->setChecked(false);check(thickness->value()==2.,"Disabling local thickness did not restore Part value");
        bend->findChild<QDoubleSpinBox*>("bendRadius")->setValue(7.);bend->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(document::PartDocument::load(bend_path).history.empty(),"Bend Cancel inserted history");
        bend_action->trigger();flush();bend=bend_dialog();check(bend&&bend->findChild<QDoubleSpinBox*>("bendRadius")->value()==2.&&
            !bend->findChild<QCheckBox*>("bendRadiusOverride")->isChecked(),"Bend Cancel did not restore the inherited radius default");
        // Remaining scenarios deliberately exercise independent radius editing.
        bend->findChild<QCheckBox*>("bendRadiusOverride")->setChecked(true);flush();
        check(!bend->findChild<QComboBox*>("bendState"),"Obsolete profile state control is still present");flush();window.grab().save("build/bend-properties.png");
        bend->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!bend_dialog(),"Bend OK failed to close");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();const auto saved_bend=document::PartDocument::load(bend_path);
        check(saved_bend.history.size()==1&&saved_bend.history.front().feature_kind==document::FeatureKind::Bend,"Sheet Profile was not committed as one history feature");
        const auto initial_path=sketcher::Sketch::from_serialized(saved_bend.history.front().bend.auxiliary_sketches[0]);
        check(initial_path.arcs.size()==1 && std::ranges::none_of(initial_path.segments,[](const auto& line){return !line.construction;}),
            "New Bend must contain only a circular trajectory");
        QTreeWidgetItem* bend_row{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==saved_bend.history.front().id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){bend_row=*it;break;}
        check(bend_row,"Bend history row missing");window.show_tree_item_properties(bend_row);flush();bend=bend_dialog();check(bend,"First Bend history edit cannot enter rollback");
        QApplication::sendEvent(model_view,&middle);flush();check(!bend_dialog(),"Bend middle double-click did not confirm");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(document::PartDocument::load(bend_path).history.size()==1,"Sheet Profile edit inserted another feature");
        bend_row=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==saved_bend.history.front().id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){bend_row=*it;break;}
        check(bend_row,"Bend history row missing before hem edit");window.show_tree_item_properties(bend_row);flush();bend=bend_dialog();
        bend->findChild<QCheckBox*>("bendHem")->setChecked(true);
        bend->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!bend_dialog(),"Hem OK failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto saved_hem=document::PartDocument::load(bend_path).history.front().bend;
        check(saved_hem.angle_degrees==180.&&saved_hem.radius==0.&&!saved_hem.radius_follows_thickness,"Hem parameters did not persist");
        bend_row=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==saved_bend.history.front().id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){bend_row=*it;break;}
        check(bend_row,"Hem history row missing");window.show_tree_item_properties(bend_row);flush();bend=bend_dialog();
        check(bend->findChild<QCheckBox*>("bendHem")->isChecked(),"Reopened hem lost its mode");
        bend->findChild<QCheckBox*>("bendHem")->setChecked(false);bend->findChild<QCheckBox*>("bendRadiusOverride")->setChecked(true);
        bend->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!bend_dialog(),"Leaving hem failed");
        const auto bend_id=saved_bend.history.front().id;
        window.show_parameter_dimensions(bend_id);flush();
        check(!model_view->findChild<QPushButton*>("bendViewStateButton"),"Obsolete per-profile View state button is still present");
        const auto inline_edit=[&](const std::string& suffix,const QString& value) {
            const auto& dimensions=dynamic_cast<viewer::MeshView*>(model_view)->mesh().dimensions;
            const auto found=std::ranges::find_if(dimensions,[&](const auto& d){return d.reference.semantic_key.ends_with(suffix);});
            check(found!=dimensions.end(),"Bend driving dimension is absent from View");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
            candidate.owner_id=found->reference.owner_id;candidate.semantic_key=found->reference.semantic_key;
            candidate.instance_path=found->reference.instance_path;candidate.geometry_index=std::distance(dimensions.begin(),found);
            window.edit_dimension_inline(candidate);flush();
            QPointer<QLineEdit> editor=model_view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Bend inline dimension editor did not open");
            editor->setText(value);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            check(!editor||!editor->isVisible(),"Bend inline dimension edit failed");
        };
        inline_edit(":radius","10");inline_edit(":angle","180");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(std::abs(document::PartDocument::load(bend_path).history.front().bend.angle_degrees-180.)<1e-8,"View angle edit rejected 180 degrees");
        inline_edit(":angle","45");
        inline_edit(":difference:first","3");inline_edit(":difference:last","10");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto edited_bend=document::PartDocument::load(bend_path);const auto& definition=edited_bend.history.front();
        check(std::abs(definition.bend.radius-8)<1e-6&&std::abs(definition.bend.angle_degrees-45)<1e-6,"Bend inline dimensions did not update feature parameters");
        const auto extensions=document::bend_profile_extensions(definition);
        check(std::abs(extensions[0]-3)<1e-6&&std::abs(extensions[1]-10)<1e-6,"Endpoint difference edits did not persist");
        window.grab().save("build/bend-three-sketch-view.png");
        check(window.finish_parameter_dimensions(),"Bend inspection did not finish");flush();
        check(!model_view->findChild<QPushButton*>("bendViewStateButton"),"Obsolete Bend state button appeared after inspection");
        // Exercise attached Flat through the real preview/Sketcher/commit path.
        std::optional<kernel::ViewerEdge> flat_edge;
        for(const auto& edge:dynamic_cast<viewer::MeshView*>(model_view)->mesh().original_references.edges) {
            if(edge.reference.owner_id!=bend_id)continue;
            try {const auto refs=document::flat_sheet_references(edge);
                if(refs[1].semantic_key.starts_with("sweep:cap:end:from:")){flat_edge=edge;break;}}
            catch(const std::exception&){}
        }
        check(flat_edge.has_value(),"View has no outer Bend End boundary for Flat");
        flat_action->trigger();flush();flat=flat_dialog();check(flat,"Attached Flat properties did not open");
        // Exercise the common viewer candidate stream.  Calling
        // SketchPropertiesDialog::set_reference() directly would miss a
        // regression where the generic placement DOF filter rejects the
        // boundary edge before Flat can expand it into edge + cap + endpoint.
        auto* flat_references=flat->findChild<QTableWidget*>("sketchReferenceTable");
        check(flat_references,"Attached Flat placement reference table missing");
        flat_references->cellClicked(0,1);
        auto* flat_view=dynamic_cast<viewer::MeshView*>(model_view);
        flat_view->set_selection_filter(viewer::SelectionFilter::Curves);flush();
        const auto edge_a=flat_edge->points.front(),edge_b=flat_edge->points.back();
        const kernel::Vec3 edge_middle{(edge_a.x+edge_b.x)/2,
            (edge_a.y+edge_b.y)/2,(edge_a.z+edge_b.z)/2};
        QPointF projected;double closest=std::numeric_limits<double>::infinity();
        for(int y=10;y<flat_view->height()-10;y+=2)for(int x=10;x<flat_view->width()-10;x+=2) {
            const auto ray=flat_view->ray_at(QPointF(x,y));if(!ray)continue;
            const kernel::Vec3 delta{edge_middle.x-ray->first.x,
                edge_middle.y-ray->first.y,edge_middle.z-ray->first.z};
            const auto direction=ray->second;
            const double along=(delta.x*direction.x+delta.y*direction.y+
                delta.z*direction.z)/(direction.x*direction.x+
                direction.y*direction.y+direction.z*direction.z);
            const double error=std::pow(delta.x-along*direction.x,2)+
                std::pow(delta.y-along*direction.y,2)+
                std::pow(delta.z-along*direction.z,2);
            if(error<closest){closest=error;projected=QPointF(x,y);}
        }
        std::optional<QPointF> flat_hit;std::size_t flat_offered_index{};
        for(int dy=-4;dy<=4&&!flat_hit;++dy)for(int dx=-4;dx<=4&&!flat_hit;++dx) {
            const auto pixel=projected+QPointF(dx,dy);
            const auto candidates=flat_view->selection_candidates_at(pixel);
            for(std::size_t i=0;i<candidates.size();++i)
                if(candidates[i].kind==viewer::CandidateKind::Edge&&
                   candidates[i].owner_id==flat_edge->reference.owner_id&&
                   candidates[i].semantic_key==flat_edge->reference.semantic_key) {
                    flat_hit=pixel;flat_offered_index=i;break;
                }
        }
        check(flat_hit.has_value(),"Common Flat hover list rejected the Sheet Profile terminal edge");
        const auto flat_mouse=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,*flat_hit,QPointF(flat_view->mapToGlobal(flat_hit->toPoint())),
                button,buttons,Qt::NoModifier);QApplication::sendEvent(flat_view,&event);flush();
        };
        flat_mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        for(std::size_t i=0;i<flat_offered_index;++i) {
            flat_mouse(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);
            flat_mouse(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
        }
        check(flat_view->hovered_candidate()&&
            flat_view->hovered_candidate()->owner_id==flat_edge->reference.owner_id&&
            flat_view->hovered_candidate()->semantic_key==flat_edge->reference.semantic_key,
            "Flat hover and common candidate disagree on the Sheet Profile edge");
        flat_mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
        flat_mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        flat_view->set_selection_filter(viewer::SelectionFilter::All);flush();
        const auto attached=flat->pending_value();
        check(attached.second.references.size()==3&&attached.first.external_references.size()==2,
            "Sheet Profile edge did not auto-fill Flat references and external endpoints");
        check(flat->mutate_sketch(attached.first.id,[](auto& s) {
            const auto a=s.external_references[0].cached_points.front(),b=s.external_references[1].cached_points.front();
            static_cast<void>(s.add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),12));
        }),"Cannot draw attached Flat rectangle");
        flat->findChild<QPushButton*>("sketchOpenButton")->click();flush();
        check(!flat->isVisible(),"Attached Flat did not enter Sketcher");
        for(const auto& ref:attached.first.external_references) {
            check(app::workspace_detail::sketch_external_reference_id_from_key("external_point:"+ref.id)==ref.id,
                "External Flat endpoint key was truncated during snapping");
            check(!app::workspace_detail::sketch_external_reference_id_from_key("external_point:"+ref.id+":broken"),
                "Broken external endpoint was offered for snapping");
        }
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        check(flat->isVisible()&&flat->pending_value().first.external_references.size()==2,"Attached Flat Sketcher return lost endpoints");
        check(window.grab().save("build/flat-bend-attachment.png"),"Attached Flat screenshot failed");
        flat->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!flat_dialog(),"Attached Flat did not commit");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto attached_part=document::PartDocument::load(bend_path);
        check(attached_part.history.size()==2&&attached_part.history.back().flat.sheet_attachment,
            "Workspace did not persist attached Flat");
        std::cout<<"Arc-only Bend and attached Flat UI passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
