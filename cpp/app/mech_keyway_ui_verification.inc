#include "mech_keyway_trials_ui.inc"
int verify_mech_keyway_workspace_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory){
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button=Qt::LeftButton){
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();
        }
    };
    try{
        workspace::Workspace live;kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        document::BodyHistoryGraph graph;const auto body=graph.create_body("MECH shaft");part.set_body_history(std::move(graph));
        live.add_part(part);live.activate(id);
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;static_cast<void>(sketch.add_circle(0,0,17.5));
        auto shaft=document::PartDocument::create_feature_container(sketch.id);shaft.feature.sides[0].length=90;
        shaft.name=QCoreApplication::translate("PrimitivePropertiesDialog","Tažení").toStdString();
        workspace::commit_profile(live,engine,id,shaft,workspace::ProfileEditMode::Create,sketch);
        auto* native=live.open_part(id);const auto file=directory/"mech-keyway-gui.prtz";
        native->session.document().save(file,native->session.calculated_boundaries());
        window.resize(1500,1050);window.show();check(window.open_document_path(QString::fromStdWString(file.wstring())),"MECH fixture did not open");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"MECH Body activation failed");flush();
        auto* mode=window.findChild<QAction*>("applicationModeAction9");check(mode&&mode->isEnabled(),"MECH application unavailable");mode->trigger();flush();
        auto* create=window.findChild<QAction*>("mechKeywayAction");check(create&&create->isEnabled()&&!create->icon().isNull(),"MECH keyway command/icon unavailable");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"MECH workspace missing");
        const auto editor=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("mechKeywayPropertiesDialog"));};
        const auto baseline=window.execute_console_command("documents").data;auto calls=kernel::body_calculation_count.load();
        create->trigger();flush();check(editor()&&editor()->mech_keyway_panel(),"MECH creation dialog missing");
        check(!editor()->findChild<QWidget*>("mechKeyAngle")&&!editor()->set_inline_parameter_value("mech_angle",30),"MECH exposes an independent angle");
        check(editor()->mech_keyway_panel()->active_pick()==0,"MECH creation did not arm support entry");
        editor()->reject();flush();check(window.execute_console_command("documents").data==baseline&&kernel::body_calculation_count.load()==calls,"MECH create/Cancel changed document or calculated geometry");
        create->trigger();flush();auto* dialog=editor();auto* panel=dialog->mech_keyway_panel();
        view->fit_all();flush();std::optional<QPointF> hit;
        for(int y=25;y<view->height()-25&&!hit;y+=7)for(int x=25;x<view->width()-25&&!hit;x+=7)
            for(const auto& candidate:view->selection_candidates_at({double(x),double(y)}))if(candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==shaft.id){hit=QPointF(x,y);break;}
        check(hit.has_value(),"MECH cylindrical support is not selectable with the mouse");click(view,*hit);
        check(panel->parameters().support.valid()&&panel->active_pick()==-1,"MECH support click was not accepted");
        check(dialog->pending_value().placement.references.size()>=1&&!dialog->pending_value().placement.references.front().use_axis,"MECH support did not initialize surface placement");
        const auto select_plane=[&](const char* semantic){
            QTreeWidgetItem* plane=nullptr;
            const auto origin=native->session.document().body_history.find(body)->origin().id;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+5)==semantic&&
                ((*it)->data(0,Qt::UserRole+6).isValid()?(*it)->data(0,Qt::UserRole+6):(*it)->data(0,Qt::UserRole)).toString().toStdString()==origin){plane=*it;break;}
            check(plane,"MECH placement datum missing from Tree");
            for(auto* ancestor=plane->parent();ancestor;ancestor=ancestor->parent())ancestor->setExpanded(true);
            tree->scrollToItem(plane);flush();click(tree->viewport(),tree->visualItemRect(plane).center());
        };
        const auto place_planes=[&](bool angle_first){
            check(dialog->first_empty_position_index()==1,"MECH support did not arm the next placement row");
            select_plane(angle_first?"origin:plane:yz":"origin:plane:xy");
            check(dialog->first_empty_position_index()==2,"MECH did not advance after its first plane");
            select_plane(angle_first?"origin:plane:xy":"origin:plane:yz");
            check(dialog->first_empty_position_index()==3,"MECH did not accept its second plane automatically");
            const auto placement=dialog->pending_value().placement;
            const auto angle=std::ranges::find_if(placement.references,[](const auto& reference){return reference.use_angle;});
            check(angle!=placement.references.end()&&angle->semantic_key=="origin:plane:yz","MECH plane order confused angle and axial distance");
            check(std::abs(std::hypot(placement.x,placement.y)-17.5)<1e-7&&std::abs(placement.z)<1e-7,"MECH Origin is not on its exact support surface");
        };
        place_planes(true);dialog->reject();flush();
        check(window.execute_console_command("documents").data==baseline&&kernel::body_calculation_count.load()==calls,"MECH ordered reference Cancel changed the document");
        create->trigger();flush();dialog=editor();panel=dialog->mech_keyway_panel();
        view->fit_all();flush();click(view,*hit);place_planes(false);
        const auto initial_placement=dialog->pending_value().placement;
        check(dialog->set_inline_parameter_value("placement:reference_offset:2",37),"MECH shared placement angle is not editable");flush();
        const auto angular_placement=dialog->pending_value().placement;
        check(std::abs(document::placement_reference_display_value(angular_placement.references[2])-37)<1e-8&&
            std::hypot(angular_placement.x-initial_placement.x,angular_placement.y-initial_placement.y)>1,
            "MECH shared placement angle did not move its Origin around the shaft");
        check(dialog->set_inline_parameter_value("mech_offset",5),"MECH offset field unavailable");flush();
        check(kernel::body_calculation_count.load()==calls,"MECH picking/preview calculated a solid");
        window.grab().save(QString::fromStdWString((directory/"mech-keyway-create.png").wstring()));
        check(dialog->grab().save(QString::fromStdWString((directory/"mech-keyway-dialog.png").wstring())),"Cannot capture keyway properties");
        const auto parent_id=dialog->container_id();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"MECH OK did not close");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto persisted=document::PartDocument::load(file);const auto* parent=persisted.find_container(parent_id);
        check(parent&&parent->feature_kind==document::FeatureKind::MechKeyway,"MECH did not persist the logical owner");
        check(parent->placement==angular_placement,"MECH angular placement did not persist");
        check(parent->mech_keyway.width==10&&parent->mech_keyway.length==45,"MECH catalogue size/length differs from preview");
        const auto row=[&]()->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==parent_id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;return nullptr;};
        check(row()&&row()->childCount()>=2,"MECH native children are not grouped in Tree");
        calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();dialog=editor();
        check(dialog&&dialog->pending_value().placement==angular_placement,"MECH reopening changed angular placement");
        for(double angle:{-90.,.01,180.}){
            check(dialog->set_inline_parameter_value("placement:reference_offset:2",angle),"MECH repeated angle edit failed");flush();
            check(std::abs(document::placement_reference_display_value(dialog->pending_value().placement.references[2])-angle)<1e-8,"MECH angle edit was lost");
        }
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->placement==angular_placement&&
            kernel::body_calculation_count.load()==calls,"MECH angular preview/Cancel changed material or placement");
        window.show_tree_item_properties(row());flush();dialog=editor();
        check(dialog&&dialog->set_inline_parameter_value("placement:reference_offset:2",90),"MECH angle edit cannot reopen");flush();
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor(),"MECH angular edit did not commit");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::placement_reference_display_value(document::PartDocument::load(file).find_container(parent_id)->placement.references[2])==90,"MECH edited angle did not persist");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->placement==angular_placement,"MECH angular Undo lost placement");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::placement_reference_display_value(document::PartDocument::load(file).find_container(parent_id)->placement.references[2])==90,"MECH angular Redo lost placement");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        calls=kernel::body_calculation_count.load();const auto saved=window.execute_console_command("documents").data;
        window.show_tree_item_properties(row());flush();check(editor(),"MECH edit dialog did not reopen");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor()&&window.execute_console_command("documents").data==saved&&kernel::body_calculation_count.load()==calls,"Unchanged MECH OK calculated or committed");
        view->set_view_direction({-parent->placement.x,-parent->placement.y,-8});
        {QEventLoop wait;QTimer::singleShot(950,&wait,&QEventLoop::quit);wait.exec();}
        view->fit_all();click(view,{3,3});flush();
        const auto move=[&](QPointF point){QMouseEvent event(QEvent::MouseMove,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        const auto cyan=[&]{const auto image=view->grabFramebuffer();int count=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.red()<40&&c.green()>150&&c.blue()>220)++count;}
            return count;};
        move({3,3});const auto unselected_pixels=cyan();
        std::optional<QPointF> keyway_hit;
        for(int y=25;y<view->height()-25&&!keyway_hit;y+=4)for(int x=25;x<view->width()-25&&!keyway_hit;x+=4){
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(candidates.empty())continue;
            const auto* child=persisted.find_container(candidates.front().owner_id);
            if(child&&(child->id==parent_id||child->mech_keyway_binding.parent_id==parent_id))keyway_hit=QPointF(x,y);
        }
        check(keyway_hit.has_value(),"MECH calculated groove is not mouse selectable");
        move(*keyway_hit);const auto hover_pixels=cyan();check(hover_pixels>unselected_pixels+30,"MECH hover lost its authored wire");
        click(view,*keyway_hit);move({3,3});
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==parent_id&&tree->currentItem()==row(),"MECH View click did not confirm its logical owner in Tree");
        check(cyan()>unselected_pixels+30,"MECH confirmation removed its authored cyan wire");
        check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==parent_id&&d.reference.semantic_key.starts_with("parameter:mech_");}),"MECH single click exposed parameter dimensions");
        check(window.execute_console_command("documents").data==saved&&kernel::body_calculation_count.load()==calls,"MECH hover/click changed document or calculated geometry");
        tree->scrollToItem(row());flush();click(tree->viewport(),tree->visualItemRect(row()).center());
        check(tree->currentItem()==row(),"MECH single click lost Tree selection");
        const auto position=QPointF(tree->visualItemRect(row()).center());
        QMouseEvent double_click(QEvent::MouseButtonDblClick,position,QPointF(tree->viewport()->mapToGlobal(position.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&double_click);flush();
        check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==parent_id&&d.reference.semantic_key=="parameter:mech_length";}),"MECH double click omitted dimensions");
        check(window.execute_console_command("documents").data==saved&&kernel::body_calculation_count.load()==calls,"MECH inspection changed geometry/document");
        window.grab().save(QString::fromStdWString((directory/"mech-keyway-inspect.png").wstring()));
        window.show_tree_item_properties(row());flush();dialog=editor();check(dialog,"MECH second edit unavailable");
        check(dialog->set_inline_parameter_value("mech_length",55),"MECH length could not be edited");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"MECH changed OK failed");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->mech_keyway.length==45,"MECH Undo did not restore length");
        window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->mech_keyway.length==55,"MECH Redo did not restore length");
        calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();dialog=editor();check(dialog,"MECH texture properties unavailable");
        panel=dialog->mech_keyway_panel();
        auto* textures=panel->findChild<QCheckBox*>("mechKeyRoughness");check(textures&&textures->isEnabled(),"MECH surface texture library is unavailable");
        textures->setChecked(true);flush();
        check(panel->pending_annotations().size()==2,"MECH did not draft two surface texture symbols");
        auto* standard=panel->findChild<QComboBox*>("mechKeyRoughnessStandard");
        check(standard->count()==2&&standard->currentData()=="ze:surface-texture:iso21920","MECH did not default to the newer surface texture standard");
        panel->findChild<QPushButton*>("mechKeyWallTexture")->click();flush();
        auto* symbol=dynamic_cast<ui::PropertiesSubWindow*>(window.findChild<QWidget*>("symbolPropertiesDialog"));
        check(symbol&&!dialog->isEnabled(),"MECH did not open the ordinary Symbol editor");
        symbol->findChild<QComboBox*>("symbolField:Specification")->setCurrentText("Ra 1.6");
        symbol->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(dialog->isEnabled()&&panel->roughness_instance(0).text_values.at("Specification")=="Ra 1.6","MECH wall texture edit was lost");
        standard->setCurrentIndex(1);flush();check(panel->roughness_instance(0).text_values.at("Specification")=="Ra 1.6","Switching texture standards lost the authored value");
        standard->setCurrentIndex(0);flush();
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"MECH texture OK failed");
        check(kernel::body_calculation_count.load()==calls,"MECH annotation-only change recalculated material");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        persisted=document::PartDocument::load(file);
        check(persisted.symbol_annotations.size()==2,"MECH textures did not survive native save/reopen");
        for(const auto& value:persisted.symbol_annotations)check(value.reference&&!value.unresolved,"MECH texture lost its real face attachment");
        window.grab().save(QString::fromStdWString((directory/"mech-keyway-textures.png").wstring()));
        QTreeWidgetItem* symbol_row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==persisted.find_container(parent_id)->mech_keyway.walls_symbol_id){symbol_row=*it;break;}
        check(symbol_row&&symbol_row->parent()==row(),"MECH surface texture is not grouped with its logical owner");
        row()->setExpanded(true);tree->setCurrentItem(symbol_row);flush();
        const auto grip=view->symbol_handle_position(1);check(grip.has_value(),"MECH surface texture has no 3D handle");
        std::optional<QPointF> symbol_hit;
        for(int y=-12;y<=12&&!symbol_hit;y+=2)for(int x=-12;x<=12&&!symbol_hit;x+=2){
            const auto point=*grip+QPointF(x,y);const auto candidates=view->selection_candidates_at(point);
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Symbol)symbol_hit=point;
        }
        check(symbol_hit.has_value(),"MECH surface texture is not pickable in 3D");
        QMouseEvent symbol_double(QEvent::MouseButtonDblClick,*symbol_hit,QPointF(view->mapToGlobal(symbol_hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&symbol_double);flush();
        auto* attachment=dynamic_cast<ui::PropertiesSubWindow*>(window.findChild<QWidget*>("symbolAttachmentDialog"));
        check(attachment,"Double clicking a 3D MECH surface texture did not open its ordinary editor");
        attachment->findChild<QComboBox*>("symbolField:Specification")->setCurrentText("Ra 0.8");
        attachment->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(kernel::body_calculation_count.load()==calls,"Editing a 3D surface texture recalculated material");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).symbol_annotations==persisted.symbol_annotations,"Cancelling 3D surface texture editing changed annotations");
        // Exercise the Woodruff floor at the Boolean seam through actual
        // Properties confirmation, retaining both surface texture attachments.
        window.show_tree_item_properties(row());flush();dialog=editor();
        auto* key_type=dialog->findChild<QComboBox*>("mechKeyType");check(key_type,"MECH key type control missing");
        key_type->setCurrentIndex(int(document::KeywayType::Woodruff));flush();
        check(dialog->set_inline_parameter_value("placement:reference_offset:2",0)&&
            dialog->set_inline_parameter_value("mech_offset",25),"Woodruff placement edit failed");flush();
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Rotated Woodruff OK failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto woodruff=document::PartDocument::load(file);
        check(woodruff.find_container(parent_id)->mech_keyway.type==document::KeywayType::Woodruff,"Woodruff type did not persist");
        const auto annotation=std::ranges::find_if(woodruff.history,[&](const auto& c){return c.mech_keyway_binding.parent_id==parent_id&&c.mech_keyway_binding.role=="annotation";});
        check(annotation!=woodruff.history.end(),"Woodruff measurement is missing");
        const auto measured=std::ranges::find(woodruff.sketches,annotation->id,&sketcher::Sketch::owner_container_id);
        check(measured!=woodruff.sketches.end(),"Woodruff measurement Sketch is missing");
        const auto* floor=measured->find_point(measured->id+":floor");
        const auto* surface=measured->find_point(measured->id+":surface");check(floor&&surface,"Woodruff measurement points are missing");
        const auto depth=kernel::dimension_sub(measured->world_point(floor->x,floor->y),measured->world_point(surface->x,surface->y));
        check(std::abs(std::sqrt(kernel::dimension_dot(depth,depth))-woodruff.find_container(parent_id)->mech_keyway.depth)<1e-8,
            "Woodruff GUI depth equation is wrong");
        for(const auto& value:woodruff.symbol_annotations)check(value.reference&&!value.unresolved,"Woodruff GUI texture lost its support");
        const auto& woodruff_placement=woodruff.find_container(parent_id)->placement;
        view->set_view_direction({-woodruff_placement.x+.5*woodruff_placement.y,-woodruff_placement.y-.5*woodruff_placement.x,-8});
        {QEventLoop wait;QTimer::singleShot(950,&wait,&QEventLoop::quit);wait.exec();}
        view->fit_all();click(view,{3,3});flush();
        window.grab().save(QString::fromStdWString((directory/"mech-keyway-woodruff.png").wstring()));
        std::filesystem::copy_file(file,directory/"woodruff-gui.prtz",std::filesystem::copy_options::overwrite_existing);
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->mech_keyway.type==document::KeywayType::Parallel,"Woodruff Undo did not restore the parallel key");
        calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();dialog=editor();check(dialog,"MECH placement properties unavailable");
        auto* references=dialog->findChild<QTableWidget*>("primitiveReferenceTable");
        check(references&&references->item(0,1),"MECH shared placement table unavailable");
        // Whole-Origin entry fills the existing empty three-plane slots.
        // Clear the automatically initialized axis/planes through their real controls first.
        for(int index=int(std::min<std::size_t>(3,dialog->first_empty_position_index()))-1;index>=0;--index){
            auto* cell=references->cellWidget(index,0);check(cell,"MECH placement reference row missing");
            auto* clear=cell->findChild<QPushButton*>();
            check(clear,"MECH placement reference clear control missing");clear->click();flush();
        }
        click(references->viewport(),references->visualItemRect(references->item(0,1)).center());
        const auto origin_id=persisted.body_owner_for_object(parent_id)->origin().id;
        QTreeWidgetItem* origin_row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==origin_id){origin_row=*it;break;}
        check(origin_row,"MECH Body Origin missing from Tree");
        for(auto* ancestor=origin_row->parent();ancestor;ancestor=ancestor->parent())ancestor->setExpanded(true);
        tree->scrollToItem(origin_row);flush();click(tree->viewport(),tree->visualItemRect(origin_row).center());
        check(document::placement_references_use_whole_origin(dialog->pending_value().placement.references),"MECH whole-Origin mouse entry failed");
        check(dialog->set_inline_parameter_value("placement:reference_offset:0",4),"MECH reference offset unavailable");flush();
        dialog->set_reference_inspected(0,true);flush();dialog->clear_reference_highlights();flush();
        const auto whole=dialog->pending_value().placement;
        const auto height=dialog->height();dialog->resize(dialog->width()+50,height+60);flush();
        check(kernel::body_calculation_count.load()==calls,"MECH Origin entry/inspection/resize calculated material");
        click(view,{35,35},Qt::MiddleButton);check(editor(),"A short middle click committed MECH");
        QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(35,35),QPointF(view->mapToGlobal(QPoint(35,35))),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&confirm);flush();check(!editor(),"MECH middle double-click outside Properties did not confirm");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        persisted=document::PartDocument::load(file);
        check(persisted.find_container(parent_id)->placement==whole,"MECH whole Origin did not persist");
        calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();dialog=editor();
        check(dialog&&dialog->pending_value().placement.references==whole.references,"MECH reopened Origin references changed");
        check(dialog->set_inline_parameter_value("placement:reference_offset:0",9),"MECH reopened reference offset unavailable");flush();
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(file).find_container(parent_id)->placement==whole&&kernel::body_calculation_count.load()==calls,"MECH placement Cancel changed material or placement");
        verify_mech_keyway_trials_ui(application,window,directory);
        std::cout<<"MECH workspace: mouse selection, shared placement, create/edit, persistence, no-op OK, inspection, length Undo/Redo, both surface texture standards, property overrides, 3D double-click/Cancel, whole-Origin mouse entry, correction, reopen/Cancel, inspection, resize and middle confirmation passed\n";return 0;
    }catch(const std::exception& error){window.grab().save(QString::fromStdWString((directory/"mech-keyway-failure.png").wstring()));std::cerr<<"MECH workspace: "<<error.what()<<'\n';return 1;}
}
