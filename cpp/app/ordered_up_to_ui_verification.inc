// Full-window regression: the real View picker owns each target click.
Q_NEVER_INLINE static int verify_ordered_up_to_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto exact=[&](PrimitivePropertiesDialog* dialog,bool valid=true) {
            QElapsedTimer wait;wait.start();
            while(dialog->property("featureExactPreviewState").toString()=="pending"&&wait.elapsed()<30000) {
                application.processEvents();QThread::msleep(5);
            }
            if(dialog->property("featureExactPreviewState").toString()!=(valid?"ready":"failed")) {
                std::cerr<<"Exact preview state="<<dialog->property("featureExactPreviewState").toString().toStdString()
                    <<" calculations="<<dialog->property("featureExactPreviewCalculations").toInt()
                    <<" notice="<<dialog->findChild<QLabel*>("featureUpToDraftNotice")->text().toStdString()<<std::endl;
                throw std::runtime_error("Exact preview did not reach its expected state");
            }
        };
        const auto opening_is_read_only=[&](PrimitivePropertiesDialog* dialog) {
            QEventLoop wait;QTimer::singleShot(220,&wait,&QEventLoop::quit);wait.exec();
            check(dialog->property("featureExactPreviewCalculations").toInt()==0,"Opening Properties calculated an exact preview");
        };
        if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_ORDERED_UP_TO_SOURCE");!source.isEmpty()) {
            std::vector<kernel::BodyResult> geometry;
            const auto model=document::PartDocument::load(std::filesystem::u8path(source.toStdString()),&geometry);
            check(model.history.size()>1,"Correction review requires the preceding Feature");
            const auto selected=std::ranges::find_if(model.history,[](const auto& value){return value.name=="Vytažení 004";});
            check(selected!=model.history.end(),"Actual correction Feature 004 is missing");
            const auto& target=*selected;
            const bool native=document::feature_up_to_requires_exact_preview(target.feature);
            const auto file=directory/"single-face-correction-review.prtz";model.save(file,geometry);
            check(window.open_document_path(QString::fromStdString(file.string())),"Correction review copy did not open");
            window.showMaximized();flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==target.id){row=*it;break;}
            check(row,"Correction review Feature is absent from Tree");
            auto* opening_view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            check(opening_view,"Correction review View is missing before opening");
            const auto scenes_before=opening_view->base_mesh_revision();
            window.show_tree_item_properties(row);flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            check(dialog&&view,"Correction review Properties or View is missing");
            opening_is_read_only(dialog);
            const auto material_wire=[&] {
                const auto pending=dialog->pending_value();
                if(pending.feature.result_type==document::ProfileResultType::Thin)
                    tests::verify_rectangular_thin_start(model,pending,view->transient_edges());
            };
            material_wire();
            check(view->base_mesh_revision()==scenes_before+1,"Opening Feature published more than its one rollback scene");
            if(target.feature.result_type==document::ProfileResultType::Thin)
                check(std::ranges::none_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==target.feature.sketch_id;}),
                    "Thin Feature Properties show an extra source-profile contour");
            const auto opening_scenes=view->base_mesh_revision();
            for(double value:{0.,-2.,1.25,0.}) {
                dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(value);flush();
                if(native)exact(dialog);
                material_wire();
                check(!view->transient_edges().empty()&&
                    dialog->findChild<QLabel*>("featureUpToDraftNotice")->isVisible()==native,
                    "Actual model correction discarded its exact transient wire or status");
                check(view->base_mesh_revision()==opening_scenes,"Numeric correction republished the base scene");
            }
            if(native&&target.feature.result_type==document::ProfileResultType::Thin) {
                auto* mode=dialog->findChild<QComboBox*>("featureThinSide");check(mode,"Thin side control missing");
                const auto initial=mode->currentIndex();
                for(int choice:{0,1,2,initial}) {
                    mode->setCurrentIndex(choice);flush();exact(dialog);
                    material_wire();
                    check(!view->transient_edges().empty(),"A Thin side choice lost its operation wire");
                    check(std::ranges::none_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==target.feature.sketch_id;}),
                        "A Thin side choice added a third source-profile contour");
                }
            }
            if(target.feature.sides[0].targets.size()>1) {
                auto* limits=static_cast<FeatureLimitTable*>(dialog->findChild<QWidget*>("featureSideLimits0"));
                auto* table=limits->table();
                // Remove the second target through the actual clear control,
                // then reinsert it with the same real View picker as the user.
                const auto wanted=target.feature.sides[0].targets[1].reference;
                auto* clear=table->cellWidget(1,2)->findChild<QPushButton*>();
                check(clear,"Actual target clear control missing");clear->click();flush();
                if(dialog->property("featureExactPreviewState")=="pending")exact(dialog);
                material_wire();
                const auto send=[&](QWidget* widget,QPointF p) {
                    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                        QMouseEvent event(type,p,widget->mapToGlobal(p.toPoint()),Qt::LeftButton,
                            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(widget,&event);flush();
                    }
                };
                send(table->viewport(),table->visualItemRect(table->item(1,3)).center());
                view->set_view_direction({1,-2,3},0);QEventLoop orbit;QTimer::singleShot(950,&orbit,&QEventLoop::quit);orbit.exec();view->fit_all();flush();
                std::optional<QPointF> hit;
                for(int y=10;y<view->height()-10&&!hit;y+=3)for(int x=10;x<view->width()-10&&!hit;x+=3) {
                    const auto candidates=view->selection_candidates_at({double(x),double(y)});
                    if(!candidates.empty()&&candidates.front().owner_id==wanted.owner_id&&candidates.front().semantic_key==wanted.semantic_key)hit=QPointF(x,y);
                }
                check(hit.has_value(),"Actual second target face cannot be offered on rollback input");send(view,*hit);
                check(dialog->pending_value().feature.sides[0].targets.size()==2,"Actual second target was not reinserted");
                exact(dialog);check(!view->transient_edges().empty(),"Actual second target has no exact draft wire before OK");
                material_wire();
            }
            window.grab().save(QString::fromStdString((directory/"single-face-correction-review.png").string()));
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            if(target.feature.result_type==document::ProfileResultType::Thin) {
                const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
                    const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(arguments)}}.dump()));
                    check(result.ok,"Actual Thin review console action failed");flush();return result.data;
                };
                const auto revision=run("feature.get",{{"container",target.id}}).at("revision");
                const auto scenes=view->base_mesh_revision();
                document::DocumentSession snapshot(model,geometry);const auto authored=authored_feature_wire(snapshot,target.id);
                tests::verify_rectangular_thin_start(model,target,authored);
                std::optional<QPointF> hit;
                for(int y=10;y<view->height()-10&&!hit;y+=4)for(int x=10;x<view->width()-10&&!hit;x+=4) {
                    const auto offered=view->selection_candidates_at({double(x),double(y)});
                    if(!offered.empty()&&offered.front().kind==viewer::CandidateKind::Container&&offered.front().owner_id==target.id)hit=QPointF(x,y);
                }
                check(hit.has_value(),"Actual Thin Feature is not offered by ordinary hover");
                QMouseEvent move(QEvent::MouseMove,*hit,view->mapToGlobal(hit->toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&move);flush();static_cast<void>(view->grabFramebuffer());
                std::size_t vertices{};for(const auto& edge:authored)if(edge.points.size()>1)vertices+=2*(edge.points.size()-1);
                check(view->property("gpuSourceWireVertexCount").toULongLong()==vertices,"Actual Thin hover omitted or duplicated authored wire strokes");
                check(view->base_mesh_revision()==scenes,"Thin hover republished the base scene");
                window.show_parameter_dimensions(target.id);flush();
                tests::verify_rectangular_thin_start(model,target,view->container_inspection_wire());
                // The existing dimension inspector publishes one scene carrying
                // its annotations. Hover itself must remain overlay-only.
                check(view->base_mesh_revision()==scenes+1&&run("feature.get",{{"container",target.id}}).at("revision")==revision,
                    "Thin inspection published duplicate annotation scenes or changed the document");
                run("save");
                std::vector<kernel::BodyResult> reopened_geometry;const auto reopened=document::PartDocument::load(file,&reopened_geometry);
                document::DocumentSession reopened_session(reopened,reopened_geometry);
                tests::verify_rectangular_thin_start(reopened,*reopened.find_container(target.id),authored_feature_wire(reopened_session,target.id));
            }
            std::cout<<"Actual saved Feature: complete "<<(native?"exact multi-target":"single-face corrected")<<" wire passed\n";
        }
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Ordered GUI"));std::vector<std::string> sources;
        const std::array<double,3> slopes{{1,0,-.5}},heights{{10,12,15}},first{{-2,2.3,6.3}},last{{1.7,5.7,10}};
        for(unsigned row=0;row<3;++row) {
            auto target=document::create_general_surface();target.name="Ordered target "+std::to_string(row+1);
            const auto p=[&](double x,double y){return kernel::Vec3{x,y,slopes[row]*x+heights[row]};};
            const std::array<kernel::Vec3,4> corners{{p(first[row],-2),p(last[row],-2),p(last[row],5),p(first[row],5)}};
            for(unsigned edge=0;edge<4;++edge) {
                target.general_surface.boundaries[edge]=document::create_general_surface_curve(target);
                auto& curve=*target.general_surface.boundaries[edge].curve;curve.curve_type=document::Curve3DType::Polyline;curve.curve_points.clear();
                for(const auto point:{corners[edge],corners[(edge+1)%4]}) {
                    auto owned=document::create_owned_point(curve.id);owned.origin=point;curve.curve_points.push_back(owned);
                }
            }
            sources.push_back(target.id);graph.insert({document::PartHistoryKind::Feature,target.id});part.history.push_back(std::move(target));
        }
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,0,10,3));
        auto feature=document::PartDocument::create_feature_container(sketch.id);feature.name="Ordered GUI feature";
        sketch.owner_container_id=feature.id;feature.feature.sides[0].length=8;
        part.sketches={sketch};part.history.push_back(feature);graph.insert({document::PartHistoryKind::Feature,feature.id});
        part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel engine;auto geometry=workspace::calculate_part_with_resolved_references(engine,part);
        const auto file=directory/("ordered-feature-ui-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()+".prtz");
        part.save(file,geometry);window.showMaximized();flush();
        check(window.open_document_path(QString::fromStdString(file.string())),"Ordered GUI fixture did not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Ordered GUI View or Tree is missing");
        const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
            auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(arguments)}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
        };
        const auto get=[&]{return run("feature.get",{{"container",feature.id}});};
        const auto original=get();
        const auto edit=[&]() {
            QTreeWidgetItem* item=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){item=*it;break;}
            check(item,"Ordered Feature Tree row is missing");window.show_tree_item_properties(item);flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog&&dialog->isVisible(),"Ordered Feature Properties did not open");return dialog;
        };
        const auto mouse=[&](QWidget* widget,QPointF point,Qt::MouseButton button) {
            const auto global=widget->mapToGlobal(point.toPoint());
            QMouseEvent move(QEvent::MouseMove,point,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&move);flush();
            QMouseEvent press(QEvent::MouseButtonPress,point,global,button,button,Qt::NoModifier);QApplication::sendEvent(widget,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,global,button,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&release);flush();
        };
        const auto select=[&](PrimitivePropertiesDialog* dialog) {
            dialog->findChild<QComboBox*>("featureSideEnd0")->setCurrentIndex(1);flush();view->fit_all();flush();
            auto* limits=static_cast<FeatureLimitTable*>(dialog->findChild<QWidget*>("featureSideLimits0"));auto* table=limits->table();
            for(unsigned row=0;row<3;++row) {
                table->scrollToItem(table->item(row,3));
                for(auto* ancestor=table->parentWidget();ancestor;ancestor=ancestor->parentWidget())
                    if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(table);
                flush();mouse(table->viewport(),table->visualItemRect(table->item(row,3)).center(),Qt::LeftButton);
                std::optional<QPointF> pixel;
                for(int y=12;y<view->height()-12&&!pixel;y+=7)for(int x=12;x<view->width()-12&&!pixel;x+=7) {
                    const auto hits=view->selection_candidates_at({double(x),double(y)});
                    if(!hits.empty()&&hits.front().owner_id==sources[row]&&hits.front().kind==viewer::CandidateKind::Face)pixel=QPointF(x,y);
                }
                check(pixel.has_value(),"Ordered source face was not offered by the real View picker");
                mouse(view,*pixel,Qt::LeftButton);
                const auto pending=dialog->pending_value();
                check(pending.feature.sides[0].targets.size()==row+1&&pending.feature.sides[0].targets[row].reference.owner_id==sources[row],
                    "View target click replaced another row or reached placement input");
                if(row>0){exact(dialog);check(!view->transient_edges().empty(),"Adding another target discarded the exact operation wire");}
            }
            check(dialog->findChild<QLabel*>("featureUpToDraftNotice")->isVisible()&&!view->transient_edges().empty(),
                "Ordered draft did not expose its exact wire and status");
            for(double value:{-100.,1.5,0.}) {
                dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(value);flush();
                exact(dialog,value!=-100.);
                if(value==-100.){check(view->transient_edges().empty(),"Impossible correction displayed stale valid geometry");continue;}
                check(!view->transient_edges().empty(),"An ordered correction discarded its exact operation wire");
                bool contact=false;
                for(const auto& edge:view->transient_edges())for(const auto p:edge.points) {
                    const double height=(p.x<2?p.x+10:p.x<6?12:15-.5*p.x)+value;
                    check(p.z>=-1e-5&&p.z<=height+1e-5,"Exact GUI wire exceeded its independent piecewise planar bound");
                    contact|=std::abs(p.z-height)<1e-5;
                }
                check(contact,"Ordered GUI wire retained the nominal length instead of its targets/correction");
            }
            const auto draft=dialog->pending_value();
            auto* eye=table->cellWidget(0,6)->findChild<QToolButton*>();mouse(eye,eye->rect().center(),Qt::LeftButton);
            mouse(table->viewport(),table->visualItemRect(table->item(1,3)).center(),Qt::LeftButton);
            check(dialog->highlighted_reference_entries().size()==1,"Arming another target cleared its independent eye");
            mouse(view,{view->width()*.5,view->height()*.5},Qt::MiddleButton);
            check(!limits->active_row()&&dialog->highlighted_reference_entries().empty()&&dialog->pending_value().feature==draft.feature,
                "Short middle click deleted data, retained entry or committed the Feature");
        };
        auto* dialog=edit();select(dialog);check(get().at("revision")==original.at("revision"),"Draft picking committed the native document");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(get().at("parameters")==original.at("parameters")&&get().at("revision")==original.at("revision"),"Ordered target Cancel changed the native Feature");
        dialog=edit();select(dialog);dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(1.5);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("parameters").at("sides")[0].at("targets").size()==3,"Ordered GUI OK lost its target chain");run("save");
        std::vector<kernel::BodyResult> saved_geometry;const auto saved=document::PartDocument::load(file,&saved_geometry);
        auto measured=saved_geometry.back();engine.ensure_body_properties(measured);
        check(std::abs(measured.volume-450.)<1e-5,"Ordered GUI result disagrees with the independent volume");
        document::DocumentSession session(saved,saved_geometry);const auto wire=authored_feature_wire(session,feature.id);
        check(!wire.empty(),"Accepted ordered Feature has no original operand wire");
        for(const auto& edge:wire)for(const auto p:edge.points)
            check(edge.reference.owner_id==feature.id&&p.x>=-2-1e-6&&p.x<=10+1e-6&&p.y>=-1e-6&&p.y<=3+1e-6,
                "Accepted original wire contains a target's or another body's edges");
        const auto accepted=get();dialog=edit();opening_is_read_only(dialog);
        check(dialog->property("featureExactPreviewState")=="ready"&&!view->transient_edges().empty(),"Unchanged Properties did not reuse the accepted wire");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("revision")==accepted.at("revision"),"Unchanged ordered GUI OK created an Undo transaction");
        window.show_parameter_dimensions(feature.id);flush();check(get().at("revision")==accepted.at("revision"),"Correction inspection changed the document");
        bool correction=false;
        for(std::size_t index=0;index<10000;++index) {
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.geometry_index=index;
            const auto source=view->dimension_source(candidate);if(!source)break;
            if(source->reference.owner_id==feature.id&&source->reference.semantic_key=="parameter:side0_up_to_length_correction") {
                check(source->label_only&&std::abs(source->value-1.5)<1e-9,"Correction annotation has the wrong value or geometry");correction=true;
            }
        }
        check(correction,"Ordered correction is absent from ordinary parameter inspection");
        run("undo");check(get().at("parameters")==original.at("parameters"),"Ordered GUI Undo lost its original definition");
        run("redo");check(get().at("parameters")==accepted.at("parameters"),"Ordered GUI Redo lost retained sides or correction");
        dialog=edit();dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(-100.);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QWidget*>("featurePropertiesDialog"),"Failed calculation kept structurally valid Properties open");
        const auto failed=get();run("save");
        std::vector<kernel::BodyResult> failed_geometry;static_cast<void>(document::PartDocument::load(file,&failed_geometry));
        check(failed.at("parameters")!=accepted.at("parameters")&&!failed_geometry.back().calculation_errors.empty(),
            "Failed GUI definition or its calculated error was not saved");
        dialog=edit();opening_is_read_only(dialog);
        check(dialog->property("featureExactPreviewState")=="unavailable"&&view->transient_edges().empty(),"Failed Properties offered a successful stored wire");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("revision")==failed.at("revision"),"Unchanged failed GUI OK created an Undo transaction");
        dialog=edit();dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(1.5);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();run("save");
        std::vector<kernel::BodyResult> repaired_geometry;static_cast<void>(document::PartDocument::load(file,&repaired_geometry));
        check(get().at("parameters")==accepted.at("parameters")&&repaired_geometry.back().calculation_errors.empty(),
            "Failed GUI definition could not be reopened and repaired");
        run("undo");check(get().at("parameters")==failed.at("parameters"),"GUI repair Undo lost the failed definition");
        run("redo");check(get().at("parameters")==accepted.at("parameters"),"GUI repair Redo lost the valid definition");
        view->grabFramebuffer().save(QString::fromStdString((directory/"ordered-feature-ui-result.png").string()));
        std::cout<<"Ordered Feature GUI: View row picking, single-face correction wire, independent eyes, short MMB, Cancel, exact OK, original operand wire, unchanged OK, correction inspection, failed-definition persistence/repair and Undo/Redo passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
