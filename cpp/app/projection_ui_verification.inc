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
        for(int output=0;output<3;++output) {
            workspace::Workspace live;kernel::OcctKernel engine;
            auto part=document::PartDocument::create_default();const auto document_id=part.document_id;
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
            const auto body=state->session.document().body_history.active_body_id();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Projection Body activation failed");flush();
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
            window.show_tree_item_properties(row());flush();check(editor(),"Shared Feature Properties did not open");
            check(editor()->windowType()==Qt::SubWindow,"Projection uses a floating Properties window");
            editor()->findChild<QComboBox*>("featureType")->setCurrentIndex(static_cast<int>(document::FeatureType::Projection));
            editor()->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(output);
            editor()->findChild<QCheckBox*>("projectionReverse")->setChecked(true);flush();
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
            if(output==2){field("projectionRegions",0);pick_face(true);check(panel->parameters().regions.size()==1,"View click did not retain region intent");}
            check(window.execute_console_command("documents").data==unchanged,"Projection preview changed the document");
            check(kernel::body_calculation_count.load()==calculations,"Projection preview invoked native geometry calculation");
            window.grab().save(QString::fromStdString((directory/("projection-properties-"+std::to_string(output)+".png")).string()));
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor())throw std::runtime_error("Projection OK failed: "+editor()->findChild<QLabel*>("featureValidationError")->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
            check(saved.find_container(feature.id)->feature.type==document::FeatureType::Projection&&cache.back().calculation_errors.empty(),"Projection native output was not saved");
            check(std::abs(cache.back().volume-39600)<.001,"Projection changed material volume");
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
            window.show_tree_item_properties(row());flush();editor()->findChild<QCheckBox*>("projectionReverse")->toggle();
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
                check(changed_cache.back().calculation_errors.empty()&&std::abs(changed_cache.back().volume-39600)<.001,"Projection source dimension edit broke dependent geometry");
            }
            window.show_tree_item_properties(row());flush();check(editor(),"Projection placement editor did not reopen");
            const auto before_placement=window.execute_console_command("documents").data;
            const auto placement_calculations=kernel::body_calculation_count.load();
            auto* default_origin=editor()->findChild<QPushButton*>("containerDefaultOriginButton");
            check(default_origin&&default_origin->isEnabled(),"Projection has no shared Default Origin placement");
            default_origin->click();editor()->findChild<QCheckBox*>("projectionReverse")->setChecked(false);flush();
            check(std::abs(editor()->pending_value().placement.z)<1e-8,"Projection did not adopt the whole Origin frame");
            check(kernel::body_calculation_count.load()==placement_calculations,"Projection placement preview calculated geometry");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==before_placement,"Projection placement Cancel changed history");
            window.show_tree_item_properties(row());flush();
            editor()->findChild<QPushButton*>("containerDefaultOriginButton")->click();
            editor()->findChild<QCheckBox*>("projectionReverse")->setChecked(false);
            editor()->findChild<QComboBox*>("projectionDirection")->setCurrentIndex(1);flush();
            field("projectionDirectionReference",0);view->fit_all();flush();
            std::optional<QPointF> axis_hit;
            for(int y=20;y<view->height()-20&&!axis_hit;y+=3)for(int x=20;x<view->width()-20&&!axis_hit;x+=3) {
                const auto choices=view->selection_candidates_at(QPointF(x,y));
                if(!choices.empty()&&choices.front().kind==viewer::CandidateKind::Axis&&choices.front().semantic_key=="origin:axis:z")axis_hit=QPointF(x,y);
            }
            check(axis_hit.has_value(),"Projection direction picker omitted the native Z axis");
            pointer(view,*axis_hit,false);
            check(editor()->projection_panel()->parameters().direction_reference.semantic_key=="origin:axis:z","Projection direction click lost its native reference");
            check(kernel::body_calculation_count.load()==placement_calculations,"Projection direction picking calculated geometry");
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor())throw std::runtime_error("Projection placed OK failed: "+editor()->findChild<QLabel*>("featureValidationError")->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> placed_cache;const auto placed=document::PartDocument::load(path,&placed_cache);
            const auto* placed_feature=placed.find_container(feature.id);
            check(placed_feature&&std::abs(placed_feature->placement.z)<1e-8&&document::placement_references_use_whole_origin(placed_feature->placement.references)&&
                placed_feature->feature.projection.direction_reference.semantic_key=="origin:axis:z"&&placed_cache.back().calculation_errors.empty(),"Projection placement/direction did not persist");
            window.show_tree_item_properties(row());flush();
            check(std::abs(editor()->pending_value().placement.z)<1e-8,"Reopened Projection lost its placement");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            std::cout<<"Projection GUI output "<<output<<": picking, replacement, independent inspection, zero-calculation preview, OK, no-op, Cancel, save, Undo/Redo, whole Origin and native axis direction passed\n";
            std::cout<<"Projection GUI source dimensions: Tree single/double-click, zero-calculation inspection and three successive View radius edits passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"Projection GUI: "<<error.what()<<'\n';return 1;}
}
