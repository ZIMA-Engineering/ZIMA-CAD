int verify_projection_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button=Qt::LeftButton) {
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();
        }
    };
    try {
        for(int output=0;output<5;++output) {
            const bool relief=output>=3,inward=output==4;
            const auto expected_volume=[&](double radius){return 39600.+(relief?(inward?-1.:1.)*std::numbers::pi*radius*radius*2.:0.);};
            workspace::Workspace live;kernel::OcctKernel engine;
            auto part=document::PartDocument::create_default();const auto document_id=part.document_id;
            document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Projection source");
            part.set_body_history(std::move(graph));
            live.add_part(part);live.activate(document_id);
            auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
            static_cast<void>(base_sketch.add_rectangle(-30,-30,30,30));
            auto base=document::PartDocument::create_feature_container(base_sketch.id);base.feature.sides[0].length=11;
            workspace::commit_profile(live,engine,document_id,base,workspace::ProfileEditMode::Create,base_sketch);
            auto source=sketcher::Sketch::create_default();source.plane_auto=false;const auto circle=source.add_circle(0,0,12);
            const auto radius_dimension=source.create_circle_radius_dimension(circle);source.apply_dimension(radius_dimension);
            auto feature=document::PartDocument::create_feature_container(source.id);feature.name="Projection GUI";
            feature.feature.type=document::FeatureType::Sketch;feature.placement.z=20;
            workspace::commit_profile(live,engine,document_id,feature,workspace::ProfileEditMode::Create,source);
            auto* state=live.open_part(document_id);const auto path=directory/("projection-ui-"+std::to_string(output)+".prtz");
            state->session.document().save(path,state->session.calculated_boundaries());
            window.resize(1440,1000);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Projection fixture");flush();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Projection Body activation failed");flush();
            if(output==0) {
                auto* modeling=window.findChild<QAction*>("applicationModeAction0");
                check(modeling&&modeling->isEnabled(),"Modeling mode is unavailable");
                modeling->trigger();flush();
            }
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Projection View is missing");
            const auto row=[&]()->QTreeWidgetItem* {
                auto* tree=window.findChild<QTreeWidget*>("documentTree");
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
                return nullptr;
            };
            const auto editor=[&]()->PrimitivePropertiesDialog* {
                for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
                return nullptr;
            };
            check(row(),"Projection Sketch Tree row is missing");
            const auto unchanged=window.execute_console_command("documents").data;
            const auto calculations=kernel::body_calculation_count.load();
            if(output==0) {
                auto* toolbar=window.findChild<QToolBar*>("toolsToolbar");
                auto* twist_action=window.findChild<QAction*>("modelTwistAction");
                auto* projection=window.findChild<QAction*>("projectionAction");
                check(toolbar&&twist_action,"Modeling toolbar or Twist action is missing");
                check(projection&&projection->isEnabled(),"Projection toolbar action is unavailable");
                check(!projection->icon().isNull(),"Projection toolbar icon is missing");
                const auto actions=toolbar->actions();
                check(actions.contains(twist_action)&&actions.indexOf(projection)==actions.indexOf(twist_action)+1,
                    "Projection toolbar action must follow Twist");
                auto* relief_action=window.findChild<QAction*>("projectionReliefAction");
                auto* wrap_action=window.findChild<QAction*>("wrapSketchAction");
                check(relief_action&&relief_action->isEnabled()&&!relief_action->icon().isNull()&&
                    actions.indexOf(relief_action)==actions.indexOf(projection)+1&&
                    actions.indexOf(wrap_action)==actions.indexOf(relief_action)+1,"Projection/Wrap command order or relief icon is wrong");
                auto* button=toolbar->widgetForAction(projection);
                check(button,"Projection toolbar button is missing");
                click(button,button->rect().center());check(editor(),"Projection button did not open shared Feature Properties");
                auto* type=editor()->findChild<QComboBox*>("featureType");
                check(type&&type->currentIndex()==static_cast<int>(document::FeatureType::Projection),
                    "Projection button did not preset Projection");
                for(int i=0;i<type->count();++i)check(!type->itemIcon(i).isNull(),"Feature type is missing its icon");
                editor()->reject();flush();
                check(window.execute_console_command("documents").data==unchanged,"Cancelling the Projection shortcut changed the document");
                check(kernel::body_calculation_count.load()==calculations,"Opening or cancelling the Projection shortcut calculated geometry");
                relief_action->trigger();flush();check(editor(),"Project relief did not open shared Properties");
                check(editor()->pending_value().feature.projection.output==document::ProjectionOutput::Relief&&
                    editor()->findChild<QDoubleSpinBox*>("projectionHeight")->isVisible()&&
                    !editor()->findChild<QComboBox*>("projectionOutput")->isVisible(),"Relief shortcut has the wrong controls");
                editor()->reject();flush();
            }
            window.show_tree_item_properties(row());flush();check(editor(),"Shared Feature Properties did not open");
            check(editor()->windowType()==Qt::SubWindow,"Projection uses a floating Properties window");
            editor()->findChild<QComboBox*>("featureType")->setCurrentIndex(relief?projection_relief_type_index:static_cast<int>(document::FeatureType::Projection));
            if(relief)editor()->findChild<QDoubleSpinBox*>("projectionHeight")->setValue(inward?-2.:2.);
            else editor()->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(output);
            flush();
            auto* panel=editor()->projection_panel();check(panel&&panel->isVisible(),"Projection controls are missing");
            const auto field=[&](const char* name,int row) {
                auto* table=editor()->findChild<QTableWidget*>(name);check(table,"Projection reference table missing");
                for(auto* ancestor=table->parentWidget();ancestor;ancestor=ancestor->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(table);
                flush();click(table->viewport(),table->visualItemRect(table->item(row,2)).center());
            };
            const auto pick_face=[&](bool region) {
                view->fit_all();flush();std::optional<QPointF> point;
                for(int y=24;y<view->height()-24&&!point;y+=5)for(int x=24;x<view->width()-24&&!point;x+=5) {
                    const QPointF pixel(x,y);const auto ray=view->ray_at(pixel);if(!ray)continue;
                    for(const auto& candidate:view->selection_candidates_at(pixel)) {
                        if(candidate.kind!=viewer::CandidateKind::Face||candidate.owner_id!=base.id||!candidate.semantic_key.starts_with("end:from:"))continue;
                        const auto hit=confirmed_face_hit(view->candidate_face_triangles(candidate),ray->first,ray->second);
                        if(hit&&std::abs(hit->z-11)<1e-5&&(!region||hit->x*hit->x+hit->y*hit->y<64)){point=pixel;break;}
                    }
                }
                check(point.has_value(),"Projection target/region absent from bounded-face picker");
                QMouseEvent move(QEvent::MouseMove,*point,QPointF(view->mapToGlobal(point->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&move);flush();
                for(std::size_t i=0;i<view->selection_candidates_at(*point).size();++i) {
                    const auto offered=view->offered_candidate();if(offered&&offered->owner_id==base.id&&offered->semantic_key.starts_with("end:from:"))break;
                    click(view,*point,Qt::RightButton);
                }
                click(view,*point);
            };
            field("projectionTargets",0);check(panel->active_pick()==ProjectionParameterPanel::Pick::Target,"Target field did not arm entry");
            pick_face(false);check(panel->parameters().targets.size()==1,"View click did not retain bounded target");
            auto* targets=editor()->findChild<QTableWidget*>("projectionTargets");
            targets->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();check(panel->inspected_faces().size()==1,"Target inspection did not remain independent");
            targets->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();check(panel->parameters().targets.empty()&&panel->inspected_faces().empty(),"Clearing a target retained its inspection");
            pick_face(false);check(panel->parameters().targets.size()==1,"Target replacement failed");
            if(output==2||relief){field("projectionRegions",0);pick_face(true);check(panel->parameters().regions.size()==1,"View click did not retain region intent");}
            check(window.execute_console_command("documents").data==unchanged,"Projection preview changed the document");
            check(kernel::body_calculation_count.load()==calculations,"Projection preview invoked native geometry calculation");
            bool source_preview=false,result_preview=false,relief_preview=false;
            for(const auto& edge:view->transient_edges()) {
                source_preview|=edge.reference.semantic_key.starts_with("projection:preview:source:");
                result_preview|=edge.reference.semantic_key=="projection:preview:result";
                if(edge.reference.semantic_key=="projection:preview:relief") {
                    relief_preview=true;for(const auto& point:edge.points)check(std::abs(point.z-(inward?9.:13.))<1e-6,"Relief preview is on the wrong material side");
                }
                check(edge.color=="#FFFFFF"&&!edge.preview_terminal_dashed,"Valid Projection preview has an unexpected style or helper line");
            }
            check(source_preview&&result_preview&&(!relief||relief_preview),"Live Projection preview omitted source, result or relief");
            window.grab().save(QString::fromStdString((directory/("projection-properties-"+std::to_string(output)+".png")).string()));
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor())throw std::runtime_error("Projection OK failed: "+editor()->findChild<QLabel*>("featureValidationError")->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
            check(saved.find_container(feature.id)->feature.type==document::FeatureType::Projection&&cache.back().calculation_errors.empty(),"Projection native output was not saved");
            check(std::abs(cache.back().volume-expected_volume(12))<.001,"Projection has the wrong material volume");
            if(relief)check(saved.find_container(feature.id)->feature.projection.height==2&&
                saved.find_container(feature.id)->combine_mode==(inward?document::CombineMode::Subtract:document::CombineMode::Add),"Relief correction/sign did not persist");
            const auto committed=window.execute_console_command("documents").data;const auto count=kernel::body_calculation_count.load();
            window.show_tree_item_properties(row());flush();check(editor(),"Projection cannot reopen Properties");
            auto pending_document=saved;*pending_document.find_container(feature.id)=editor()->pending_value();
            const auto pending_difference=nlohmann::json::diff(saved.serialized(),pending_document.serialized());
            if(!pending_difference.empty())std::cerr<<"Projection pending difference: "<<pending_difference.dump()<<'\n';
            const auto after_open_count=kernel::body_calculation_count.load();
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(window.execute_console_command("documents").data!=committed||kernel::body_calculation_count.load()!=count)
                std::cerr<<"Projection no-op counts before/open/OK "<<count<<'/'<<after_open_count<<'/'<<kernel::body_calculation_count.load()
                    <<" documents "<<nlohmann::json::diff(committed,window.execute_console_command("documents").data).dump()<<'\n';
            check(!editor()&&window.execute_console_command("documents").data==committed&&kernel::body_calculation_count.load()==count,"Unchanged Projection OK recalculated or changed history");
            window.show_tree_item_properties(row());flush();editor()->findChild<QComboBox*>("projectionOutput")->setCurrentIndex((output+1)%3);
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==committed,"Projection Cancel changed history");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            check(document::PartDocument::load(path).find_container(feature.id)->feature.type==document::FeatureType::Sketch,"Projection GUI Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            check(document::PartDocument::load(path).find_container(feature.id)->feature.type==document::FeatureType::Projection,"Projection GUI Redo failed");
            const auto pointer=[&](QWidget* widget,QPointF at,bool twice) {
                QMouseEvent move(QEvent::MouseMove,at,QPointF(widget->mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&move);
                QMouseEvent press(twice?QEvent::MouseButtonDblClick:QEvent::MouseButtonPress,at,QPointF(widget->mapToGlobal(at.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&press);
                QMouseEvent release(QEvent::MouseButtonRelease,at,QPointF(widget->mapToGlobal(at.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&release);flush();
            };
            auto* tree=window.findChild<QTreeWidget*>("documentTree");tree->expandAll();tree->scrollToItem(row());flush();
            const auto inspection_state=window.execute_console_command("documents").data;
            const auto inspection_calculations=kernel::body_calculation_count.load();
            pointer(tree->viewport(),tree->visualItemRect(row()).center(),false);
            check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==source.id;}),"Projection single click exposed source dimensions");
            pointer(tree->viewport(),tree->visualItemRect(row()).center(),true);
            const auto dimension=[&]() {
                const auto& values=view->mesh().dimensions;
                const auto found=std::ranges::find_if(values,[&](const auto& d){return d.reference.owner_id==source.id&&d.reference.semantic_key=="dimension:"+radius_dimension.id;});
                check(found!=values.end(),"Projection double-click omitted its source radius dimension");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=source.id;
                candidate.semantic_key=found->reference.semantic_key;candidate.geometry_index=found-values.begin();return candidate;
            };
            static_cast<void>(dimension());
            check(window.execute_console_command("documents").data==inspection_state&&kernel::body_calculation_count.load()==inspection_calculations,"Projection inspection calculated or changed history");
            pointer(tree->viewport(),tree->visualItemRect(row()).center(),false);
            QKeyEvent leave_inspection(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
            QApplication::sendEvent(view,&leave_inspection);flush();
            view->fit_all();flush();
            std::optional<QPointF> projected_hit;
            for(int y=20;y<view->height()-20&&!projected_hit;y+=3)for(int x=20;x<view->width()-20&&!projected_hit;x+=3) {
                const auto choices=view->selection_candidates_at(QPointF(x,y));
                if(!choices.empty()&&choices.front().kind==viewer::CandidateKind::Container&&choices.front().owner_id==feature.id)projected_hit=QPointF(x,y);
            }
            check(projected_hit.has_value(),"Projection has no selectable output in the View");
            pointer(view,*projected_hit,false);
            if(!row()->isSelected()||std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==source.id;})) {
                std::cerr<<"Projection View selection output="<<output<<" tree="<<row()->isSelected()<<" source dimensions="<<std::ranges::count_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==source.id;})<<'\n';
                for(const auto& candidate:view->selection_candidates_at(*projected_hit))std::cerr<<"Candidate "<<static_cast<int>(candidate.kind)<<' '<<candidate.owner_id<<' '<<candidate.semantic_key<<'\n';
            }
            check(row()->isSelected()&&std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==source.id;}),"Projection View selection lost Tree synchronization or exposed dimensions");
            const auto selected_frame=view->grabFramebuffer();
            selected_frame.save(QString::fromStdString((directory/("projection-selected-"+std::to_string(output)+".png")).string()));
            check(view->property("gpuSourceWireVertexCount").toInt()>0,"Projection View selection omitted its authored source/result wire");
            pointer(view,*projected_hit,true);static_cast<void>(dimension());
            check(window.execute_console_command("documents").data==inspection_state&&kernel::body_calculation_count.load()==inspection_calculations,"Projection View inspection calculated or changed history");
            for(double radius:{10.,14.,12.}) {
                const auto label=view->candidate_dimension_label_position(dimension());check(label.has_value(),"Projection source dimension has no clickable label");
                pointer(view,*label,true);auto* edit=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(edit,"Projection source radius editor did not open");
                edit->setText(QString::number(radius));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                std::vector<kernel::BodyResult> changed_cache;const auto changed=document::PartDocument::load(path,&changed_cache);
                const auto changed_source=std::ranges::find(changed.sketches,source.id,&sketcher::Sketch::id);
                check(changed_source!=changed.sketches.end()&&std::abs(changed_source->circles.front().radius-radius)<1e-7,"Projection source radius edit did not persist");
                check(changed_cache.back().calculation_errors.empty()&&std::abs(changed_cache.back().volume-expected_volume(radius))<.001,"Projection source dimension edit broke dependent geometry");
            }
            window.show_tree_item_properties(row());flush();check(editor(),"Projection placement editor did not reopen");
            const auto before_placement=window.execute_console_command("documents").data;
            const auto placement_calculations=kernel::body_calculation_count.load();
            auto* default_origin=editor()->findChild<QPushButton*>("containerDefaultOriginButton");
            check(default_origin&&default_origin->isEnabled(),"Projection has no shared Default Origin placement");
            default_origin->click();flush();
            check(std::abs(editor()->pending_value().placement.z)<1e-8,"Projection did not adopt the whole Origin frame");
            check(kernel::body_calculation_count.load()==placement_calculations,"Projection placement preview calculated geometry");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==before_placement,"Projection placement Cancel changed history");
            window.show_tree_item_properties(row());flush();
            editor()->findChild<QPushButton*>("containerDefaultOriginButton")->click();
            flush();
            check(kernel::body_calculation_count.load()==placement_calculations,"Projection whole Origin preview calculated geometry");
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor())throw std::runtime_error("Projection placed OK failed: "+editor()->findChild<QLabel*>("featureValidationError")->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> placed_cache;const auto placed=document::PartDocument::load(path,&placed_cache);
            const auto* placed_feature=placed.find_container(feature.id);
            check(placed_feature&&std::abs(placed_feature->placement.z)<1e-8&&document::placement_references_use_whole_origin(placed_feature->placement.references)&&
                placed_feature->feature.projection.direction==document::ProjectionDirection::SketchNormal&&placed_cache.back().calculation_errors.empty(),"Projection placement/direction did not persist");
            window.show_tree_item_properties(row());flush();
            check(std::abs(editor()->pending_value().placement.z)<1e-8,"Reopened Projection lost its placement");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            std::cout<<"Projection GUI output "<<output<<": picking, replacement, independent inspection, zero-calculation preview, OK, no-op, Cancel, save, Undo/Redo, whole Origin and automatic normal direction passed\n";
            std::cout<<"Projection GUI source dimensions: Tree single/double-click, zero-calculation inspection and three successive View radius edits passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"Projection GUI: "<<error.what()<<'\n';return 1;}
}
