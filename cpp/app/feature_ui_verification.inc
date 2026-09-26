// Surface modes use the public Feature editor and real persisted cylinder data.
static void verify_feature_surface_placement(QApplication& application, AssemblyWorkspaceWindow& window,
                                            const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);flush();
    };
    auto part=document::PartDocument::create_default();
    auto sketch=sketcher::Sketch::create_default();sketch.add_circle(0,0,5);
    auto source=document::PartDocument::create_feature_container(sketch.id);source.feature.sides[0].length=20;
    sketch.owner_container_id=source.id;part.history={source};part.sketches={sketch};
    document::BodyHistoryGraph graph;const auto body=graph.create_body("Surface placement");
    graph.insert({document::PartHistoryKind::Feature,source.id});part.set_body_history(graph);part.resolve_constructions();
    const kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto face=std::ranges::find_if(calculated.back().mesh.original_references.triangle_references,[](const auto& f) {
        return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;
    });
    check(face!=calculated.back().mesh.original_references.triangle_references.end(),"Cylinder packet missing");
    const auto path=directory/("feature-surface-"+source.id+".prtz");part.save(path,calculated);
    check(window.open_document_path(QString::fromStdString(path.string())),"Surface fixture cannot open");flush();
    run("body.activate",{{"body",body}});
    const auto dialog=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
    for(int i=0;i<6;++i) {
        auto* shortcut=window.findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i));
        check(shortcut&&shortcut->isEnabled()&&!shortcut->icon().isNull(),"Feature shortcut missing or disabled");
        shortcut->trigger();flush();check(dialog(),"Feature shortcut did not open shared dialog");
        const auto check_collapsed=[&] {
            bool found=false;
            for(QTreeWidgetItemIterator row(window.findChild<QTreeWidget*>("documentTree"));*row;++row)
                if((*row)->data(0,Qt::UserRole).toString().toStdString()==dialog()->pending_value().id &&
                   (*row)->data(0,Qt::UserRole+12).toBool()) {
                    check(!(*row)->isExpanded(),"New Feature automatically expanded in the tree");found=true;break;
                }
            check(found,"Pending Feature tree row missing");
        };
        check_collapsed();
        dialog()->findChild<QCheckBox*>("featureShowText")->setChecked(false);flush();check_collapsed();
        const auto preset=dialog()->pending_value().feature;
        check(preset.type==static_cast<document::FeatureType>(std::min(i,4)),"Feature shortcut selected wrong type");
        if(i==5)check(preset.sides[0].operation==document::FeatureSideOperation::Revolution&&
            preset.sides[1].operation==document::FeatureSideOperation::Revolution,"Rotation shortcut did not preset both sides");
        dialog()->reject();flush();
    }
    window.findChild<QAction*>("featurePrototypeAction")->trigger();flush();check(dialog(),"Axis Feature dialog missing");
    dialog()->findChild<QComboBox*>("featureType")->setCurrentIndex(1);
    check(dialog()->set_reference(0,{{},face->owner_id,face->semantic_key,0,true,"front",true},"Cylinder"),"Cylinder cannot be selected");flush();
    auto* mode=dialog()->findChild<QComboBox*>("placementReferenceType0");
    check(mode&&mode->count()==2&&!mode->currentData().toBool(),"Surface mode is not default or axis mode missing");
    auto value=dialog()->pending_value();
    check(document::resolve_placement(value.placement,calculated.back().mesh.original_references),"Surface pending placement invalid");
    check(std::abs(std::hypot(value.placement.x,value.placement.y)-5)<1e-7,"Surface placement left cylinder");
    mode->setCurrentIndex(1);flush();value=dialog()->pending_value();const auto axis_id=value.id;
    check(value.placement.references.front().use_axis,"Axis mode did not update definition");
    dialog()->findChild<QCheckBox*>("featureShowPoint")->setChecked(false);
    dialog()->findChild<QCheckBox*>("featureShowText")->setChecked(false);flush();
    auto* preview_view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    check(preview_view,"Feature preview view missing");
    const auto visible_origin=std::ranges::find_if(preview_view->mesh().points,[&](const auto& p) {
        return p.reference.owner_id==axis_id&&p.reference.semantic_key=="point";
    });
    check(visible_origin!=preview_view->mesh().points.end()&&visible_origin->always_visible&&!visible_origin->label.empty(),
        "Properties hid the origin or name when idle visibility was disabled");
    check(std::abs(value.placement.z-10)<1e-7&&std::abs(value.feature.effective_side(0).length-10)<1e-7&&
        std::abs(value.feature.effective_side(1).length-10)<1e-7,"Axis extent was not derived from whole face");
    window.grab().save(QString::fromStdString((directory/"feature-surface-axis-ui.png").string()));
    dialog()->grab().save(QString::fromStdString((directory/"feature-visibility-properties.png").string()));
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Axis Feature OK failed");run("save");
    auto saved=document::PartDocument::load(path,&calculated);
    const auto* axis=saved.find_container(axis_id);check(axis&&axis->placement.references.front().use_axis,"Axis mode not persisted");
    const auto mesh=saved.feature_result_mesh(*axis);check(mesh.axes.size()==1&&std::abs(mesh.axes.front().direction.z)>1-1e-7,
        "Feature axis is not coaxial with selected cylinder");
    check(!axis->feature.show_point&&!axis->feature.show_text&&!mesh.points.front().always_visible&&mesh.points.front().label.empty(),
        "Feature visibility settings did not persist");
    check(std::abs(calculated.back().volume-std::numbers::pi*25*20)<1e-5,"Axis changed source body");
    run("undo");run("save");check(!document::PartDocument::load(path).find_container(axis_id),"Axis Undo failed");
    run("redo");run("save");check(document::PartDocument::load(path).find_container(axis_id),"Axis Redo failed");
    auto* tree=window.findChild<QTreeWidget*>();QTreeWidgetItem* item{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==axis_id){item=*i;break;}
    check(item,"Axis tree item missing");window.show_tree_item_properties(item);flush();check(dialog(),"Axis edit missing");
    mode=dialog()->findChild<QComboBox*>("placementReferenceType0");check(mode&&mode->currentData().toBool(),"Reopened axis mode lost");
    check(!dialog()->findChild<QCheckBox*>("featureShowPoint")->isChecked()&&
        !dialog()->findChild<QCheckBox*>("featureShowText")->isChecked(),"Reopened visibility settings lost");
    dialog()->findChild<QCheckBox*>("featureShowPoint")->setChecked(true);
    dialog()->findChild<QCheckBox*>("featureShowText")->setChecked(true);
    mode->setCurrentIndex(0);flush();dialog()->reject();flush();run("save");
    check(document::PartDocument::load(path).find_container(axis_id)->placement.references.front().use_axis,"Cancel changed mode");
    check(!document::PartDocument::load(path).find_container(axis_id)->feature.show_point&&
        !document::PartDocument::load(path).find_container(axis_id)->feature.show_text,"Cancel changed visibility");
    run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Saved axis cannot reopen");flush();
    check(!window.findChild<QAction*>("cylinderAxisAction"),"Retired cylinder axis action remains");
    for(const auto* type:{"part","assembly"}) {
        window.findChild<QAction*>("newDocumentAction")->trigger();flush();
        auto* create=window.findChild<QDialog*>("newDocumentDialog");
        check(create,"New template dialog missing");
        create->findChild<QLineEdit*>("newDocumentFileName")->setText(
            QString("Surface template %1 %2").arg(type).arg(QUuid::createUuid().toString(QUuid::Id128)));
        for(auto* radio:create->findChildren<QRadioButton*>())
            radio->setChecked(radio->property("documentType").toString()==type);
        create->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("newDocumentDialog"),"Template creation did not commit");
        const auto command=std::string(type)=="part"?"featurePrototypeAction":"insertComponentAction";
        check(window.findChild<QAction*>(command)->isEnabled(),"Template has no active editable context");
    }
}

static void verify_feature_operation_tree(QApplication& application,AssemblyWorkspaceWindow& window,
                                          const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    using Op=document::FeatureSideOperation;
    auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(3,0,8,5));
    const auto axis=sketch.add_segment(0,-2,0,8);sketch.set_segment_centerline(axis,true);
    auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
    feature.feature.axis_segment_id=axis;part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;const auto body=graph.create_body("Operation tree");
    graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    const auto root=[&]() -> QTreeWidgetItem* {
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
            (*i)->data(0,Qt::UserRole+3)=="part-container")return *i;
        throw std::runtime_error("Operation tree Feature missing");
    };
    int number=0;
    for(const auto modes:std::array<std::array<int,2>,7>{{{1,0},{0,2},{1,1},{2,2},{1,2},{2,1},{0,0}}}) {
        auto& p=part.history.front().feature;p.sides[0].operation=static_cast<Op>(modes[0]);p.sides[1].operation=static_cast<Op>(modes[1]);
        p.sides[0].angle_degrees=p.sides[1].angle_degrees=90;
        const auto path=directory/("feature-operation-tree-"+feature.id+"-"+std::to_string(number++)+".prtz");
        part.save(path,kernel::OcctKernel{}.evaluate_history(part.kernel_operations()));
        check(window.open_document_path(QString::fromStdString(path.string())),"Operation tree document cannot open");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Operation tree Body activation failed");flush();
        const bool extrusion=modes[0]==1||modes[1]==1,rotation=modes[0]==2||modes[1]==2;
        const auto icon=extrusion?(rotation?"protrusion-revolve":"protrusion"):(rotation?"revolve":"sketch");
        auto* row=root();
        check(row->icon(0).pixmap(48,24).toImage()==resource_icon(icon).pixmap(48,24).toImage(),"Feature tree icon differs from its active operations");
        int operations=0;
        for(int i=0;i<row->childCount();++i) {
            auto* child=row->child(i);if(child->data(0,Qt::UserRole+3)!="feature-operation")continue;
            const int side=child->data(0,Qt::UserRole+5).toInt();++operations;
            check(modes[side]!=0&&child->text(0).contains(QObject::tr(modes[side]==2?"Rotace":"Vytažení"))&&
                child->text(0).contains(QObject::tr(side==0?"Strana 1":"Strana 2")),"Operation tree row lost kind or side");
        }
        check(operations==(modes[0]!=0)+(modes[1]!=0),"Operation tree has missing or inactive side rows");
        if(extrusion&&rotation) {
            row->setExpanded(true);tree->scrollToItem(row);flush();
            tree->grab().save(QString::fromStdString((directory/"feature-mixed-operation-tree.png").string()));
        }
        if(operations) {
            auto* operation=row->child(row->childCount()-1);tree->setCurrentItem(operation);flush();
            tree->itemClicked(operation,0);flush();
            check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==feature.id,"Operation row cannot select its owning Feature");
            window.show_tree_item_properties(root()->child(root()->childCount()-1));flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog&&dialog->pending_value().id==feature.id,"Operation row did not open the shared Feature editor");
            dialog->reject();flush();
        }
        check(window.execute_console_command("close").ok,"Operation tree document did not close");flush();
    }
}

// Exercises the public action, embedded Sketcher and one real native commit.
static void verify_feature_modeling(QApplication& application, AssemblyWorkspaceWindow& window,
                                   const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);
        flush();return result;
    };
    const auto dialog=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
    const auto name="feature-ui-model-"+document::PartDocument::create_default().document_id;
    run("new",{{"type","part"},{"name",name}});
    window.findChild<QAction*>("featurePrototypeAction")->trigger();flush();
    check(dialog(),"Feature creation properties missing");
    dialog()->findChild<QDoubleSpinBox*>("featureSideValue0")->setValue(10);
    const auto pending=dialog()->pending_value();
    check(pending.name==QObject::tr("Vytažení").toStdString()+" 001" && pending.feature.automatic_name==pending.name,
        "New Feature has no numbered automatic name");
    dialog()->findChild<QPushButton*>("featureSketchButton")->click();flush();
    check(!dialog(),"Feature Sketch action did not enter the editor");
    QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    check(view,"Feature editor has no View");
    auto camera=view->camera_state();camera[4]=50;camera[5]=camera[6]=0;camera[7]=50;view->set_camera_state(camera);flush();
    auto plane=sketcher::Sketch::create_default();
    const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Feature sketch ray missing");const auto hit=plane.intersect_ray(ray->first,ray->second);check(hit.has_value(),"Feature sketch plane not visible");return *hit;};
    const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
    const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
    const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
    const auto click=[&](QPointF p) {
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
    };
    window.findChild<QAction*>("sketchRectangleAction")->trigger();flush();
    click(screen(5,5));click(screen(15,13));
    window.findChild<QAction*>("finishSketchAction")->trigger();flush();
    check(dialog(),"Feature Sketcher did not return to the same properties");
    check(dialog()->pending_value().id==pending.id && dialog()->pending_value().feature.sides[0].length==10,
        "Feature Sketcher return changed identity or pending length");
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(!dialog(),"Feature OK failed to create a solid from the drawn rectangle");
    run("save");
    const auto path=directory/(name+".prtz");
    std::vector<kernel::BodyResult> bodies;
    auto saved=document::PartDocument::load(path,&bodies);
    check(saved.find_container(pending.id) && saved.find_container(pending.id)->feature_kind==document::FeatureKind::Feature,
        "GUI created the wrong history kind");
    check(!bodies.empty() && std::abs(bodies.back().volume-800)<1e-3,"GUI Feature has incorrect rectangle volume");
    run("undo");run("redo");run("save");
    const auto restored=document::PartDocument::load(path,&bodies);
    check(restored.find_container(pending.id)->feature_id==pending.feature_id && std::abs(bodies.back().volume-800)<1e-3,
        "GUI Feature Undo/Redo lost identity or geometry");
    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==pending.id && (*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
    check(row,"Created Feature is absent from Tree");window.show_tree_item_properties(row);flush();
    check(dialog(),"Created Feature cannot reopen for editing");
    dialog()->findChild<QDoubleSpinBox*>("featureSideValue0")->setValue(17);
    dialog()->reject();flush();run("save");
    saved=document::PartDocument::load(path,&bodies);
    check(saved.find_container(pending.id)->feature.sides[0].length==10 && std::abs(bodies.back().volume-800)<1e-3,
        "Feature Cancel changed the committed definition or geometry");
    const auto tree_row=[&](const std::string& id) {
        QTreeWidgetItem* found{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==id){found=*i;break;}
        check(found,"Feature placement Tree item missing");return found;
    };
    window.show_tree_item_properties(tree_row(pending.id));flush();
    auto* reference_table=dialog()->findChild<QTableWidget*>("primitiveReferenceTable");
    check(reference_table&&reference_table->rowCount()>0,"Feature has no shared reference entry");
    auto* item=reference_table->item(0,1);check(item,"Feature reference field missing");
    auto* entry=reference_table->viewport();
    const QPointF at(reference_table->visualItemRect(item).center());
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,at,QPointF(entry->mapToGlobal(at.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(entry,&event);
    }
    flush();
    auto* origin=tree_row(saved.body_owner_for_object(pending.id)->origin().id);
    for(auto* parent=origin->parent();parent;parent=parent->parent())parent->setExpanded(true);
    tree->scrollToItem(origin);flush();
    const QPointF position(tree->visualItemRect(origin).center());
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,position,QPointF(tree->viewport()->mapToGlobal(position.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&event);
    }
    flush();
    check(dialog()->pending_value().placement.references.size()>=3,"Feature whole-Origin entry failed");
    check(dialog()->set_inline_parameter_value("placement:reference_offset:0",4),"Feature reference offset is not editable");
    flush();const auto placement=dialog()->pending_value().placement;
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(!dialog(),"Feature placed OK failed");run("save");
    saved=document::PartDocument::load(path,&bodies);
    check(saved.find_container(pending.id)->placement==placement,"Feature reference placement was not persisted");
    check(std::abs(bodies.back().volume-800)<1e-3,"Feature placement changed its volume");
    window.show_tree_item_properties(tree_row(pending.id));flush();
    check(dialog()->pending_value().placement.references==placement.references,"Feature reopen lost Origin references");
    dialog()->set_inline_parameter_value("placement:reference_offset:0",9);dialog()->reject();flush();run("save");
    saved=document::PartDocument::load(path,&bodies);
    check(saved.find_container(pending.id)->placement==placement,"Feature reference Cancel changed placement");
    window.show_parameter_dimensions(pending.id);flush();
    check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==pending.id&&d.reference.semantic_key=="parameter:side0_length";}),
        "Feature length dimension missing outside Properties");
}

// Work-plane display must be independent of whether a body profile exists yet.
static void verify_feature_empty_preview(QApplication& application, AssemblyWorkspaceWindow& window,
                                        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);
        flush();return result;
    };
    const auto dialog=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
    const auto name="feature-empty-ui-"+document::PartDocument::create_default().document_id;
    run("new",{{"type","part"},{"name",name}});
    auto* action=window.findChild<QAction*>("featurePrototypeAction");
    action->trigger();flush();check(dialog(),"Empty Feature properties missing");
    const auto pending=dialog()->pending_value();
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    check(view,"Empty Feature View missing");
    auto* plane=dialog()->findChild<QComboBox*>("featureProfilePlane");
    auto* offset=dialog()->findChild<QDoubleSpinBox*>("featureProfileOffset");
    check(plane&&offset,"Feature work-plane controls missing");
    const auto check_plane=[&](int axis,double value) {
        const auto& edges=view->mesh().edges;
        const auto border=std::ranges::find_if(edges,[&](const auto& edge){
            return edge.reference.owner_id==pending.feature_id&&edge.reference.semantic_key=="border";
        });
        check(border!=edges.end()&&border->points.size()==5,"Empty Feature has no offset-plane border");
        for(const auto& point:border->points) {
            const double coordinate=axis==0?point.z:axis==1?point.y:point.x;
            check(std::abs(coordinate-value)<1e-7,"Feature plane/offset preview is stale before entering Sketcher");
        }
    };
    for(const double value:{3.5,-2.75,0.0}) {
        offset->setValue(value);flush();
        for(const int axis:{0,1,2,0}) {
            plane->setCurrentIndex(plane->findData(axis));flush();check_plane(axis,value);
        }
    }
    plane->setCurrentIndex(plane->findData(2));offset->setValue(-4);flush();check_plane(2,-4);
    window.grab().save(QString::fromStdString((directory/"feature-empty-plane-ui.png").string()));
    auto* add=dialog()->findChild<QPushButton*>("featureAdd");
    auto* cut=dialog()->findChild<QPushButton*>("featureSubtract");
    check(add->isChecked(),"New Feature must initially add material");
    add->click();dialog()->findChild<QCheckBox*>("featureOriginAxis")->setChecked(true);flush();
    check(!add->isChecked()&&!cut->isChecked()&&dialog()->pending_value().feature.sketch_only(),"Cannot choose origin axis without material");
    check_plane(2,-4);
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(!dialog(),"Axis-only Feature OK incorrectly requires a closed profile");
    run("save");
    const auto path=directory/(name+".prtz");
    std::vector<kernel::BodyResult> bodies;
    auto saved=document::PartDocument::load(path,&bodies);
    const auto* feature=saved.find_container(pending.id);
    check(feature&&feature->feature.sketch_only()&&feature->feature.origin_centerline,"Axis-only Feature flags were not saved");
    check(!bodies.empty()&&std::abs(bodies.back().volume)<1e-10,"Axis-only Feature fabricated a solid");
    check(std::ranges::any_of(bodies.back().mesh.original_references.points,[](const auto& point){
        return point.reference.semantic_key.find("centerline:from:origin:")!=std::string::npos;
    }),"Axis-only Feature lost its origin references");
    check(saved.sketches.size()==1&&saved.sketches.front().plane==sketcher::SketchPlane::YZ&&
        !saved.sketches.front().plane_auto&&saved.sketches.front().plane_offset==-4,"Feature lost its work-plane choice on OK");
    run("undo");run("save");check(!document::PartDocument::load(path).find_container(pending.id),"Axis-only Feature Undo failed");
    run("redo");run("save");saved=document::PartDocument::load(path,&bodies);
    check(saved.find_container(pending.id)&&saved.find_container(pending.id)->feature.sketch_only(),"Axis-only Feature Redo failed");
    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if((*it)->data(0,Qt::UserRole).toString().toStdString()==pending.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
    check(row,"Axis-only Feature missing from Tree");window.show_tree_item_properties(row);flush();
    check(dialog(),"Axis-only Feature cannot reopen");
    add=dialog()->findChild<QPushButton*>("featureAdd");cut=dialog()->findChild<QPushButton*>("featureSubtract");
    check(!add->isChecked()&&!cut->isChecked()&&add->isEnabled()&&cut->isEnabled(),"Reopened axis-only Feature forces a material operation");
    add->click();check(!dialog()->pending_value().feature.sketch_only(),"Reopened Feature cannot re-enable material");
    dialog()->findChild<QComboBox*>("featureProfilePlane")->setCurrentIndex(0);
    dialog()->reject();flush();run("save");
    const auto cancelled=document::PartDocument::load(path);
    check(cancelled.find_container(pending.id)->feature==saved.find_container(pending.id)->feature&&
        cancelled.sketches.front().serialized()==saved.sketches.front().serialized(),"Cancel changed axis-only Feature or plane");
}

// Exercise an actual native axis/sketch through Properties, Sketcher and file cycles.
static void verify_feature_sketch_visibility(QApplication& application, AssemblyWorkspaceWindow& window,
                                            const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);
        flush();return result;
    };
    const auto dialog=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* fit=window.findChild<QAction*>("fitViewAction");
    check(view&&tree&&fit,"Feature visibility controls missing");
    for(const bool axis_only:{true,false}) {
        auto part=document::PartDocument::create_default();
        auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.name=QObject::tr("Vytažení").toStdString()+" 001";feature.feature.automatic_name=feature.name;
        feature.feature.sides[0].operation=document::FeatureSideOperation::None;
        sketch.owner_container_id=feature.id;
        const auto axis=sketch.add_segment(120,70,240,70);sketch.set_segment_centerline(axis,true);
        const auto hidden=sketch.add_segment(120,100,240,100);sketch.set_segment_centerline(hidden,true);
        sketch.set_geometry_visible_in_3d(hidden,false);
        std::vector<std::string> curves{axis};
        if(!axis_only) {
            const auto rectangle=sketch.add_rectangle(140,80,200,130);
            curves.insert(curves.end(),rectangle.begin(),rectangle.end());
        }
        part.history={feature};part.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Feature wire");
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const kernel::OcctKernel kernel;
        const auto path=directory/(std::string(axis_only?"feature-axis-cycle-":"feature-sketch-cycle-")+feature.id+".prtz");
        part.save(path,kernel.evaluate_history(part.kernel_operations()));
        check(window.open_document_path(QString::fromStdString(path.string())),"Feature wire fixture cannot open");flush();
        run("body.activate",{{"body",body}});
        const auto original=document::PartDocument::load(path).sketches.front().serialized();
        const auto expect=[&](bool editing_sketch=false,double offset=0.) {
            for(const auto& id:curves) {
                const auto& edges=view->mesh().edges;
                const auto count=std::ranges::count_if(edges,[&](const auto& edge){return edge.reference.owner_id==sketch.id&&edge.reference.semantic_key=="segment:"+id;});
                check(count==1,"Feature axis/sketch is missing or duplicated during an editing transition");
                const auto& edge=*std::ranges::find_if(edges,[&](const auto& item){return item.reference.owner_id==sketch.id&&item.reference.semantic_key=="segment:"+id;});
                check(edge.points.size()==2&&!edge.infinite,"Feature wire lost finite endpoints");
                if(id==axis)check(edge.dash_dot,"Feature axis lost its role during an editing transition");
                for(const auto& point:edge.points)check(std::abs(point.z-offset)<1e-7,"Feature wire uses a stale work plane");
            }
            const auto hidden_count=std::ranges::count_if(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==sketch.id&&edge.reference.semantic_key=="segment:"+hidden;});
            check(hidden_count==(editing_sketch?1:0),"Feature wire ignored its 3D visibility flag");
        };
        const auto fit_wire=[&] {
            auto camera=view->camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;
            camera[4]=.1f;camera[5]=500;camera[6]=-350;view->set_camera_state(camera);flush();
            fit->trigger();flush();const auto first=view->camera_state();
            check(first[4]>10&&first[4]<1000&&first[5]==0&&first[6]==0,"Fit View cannot frame an axis-only/sketch-only Feature");
            for(int repeat=0;repeat<3;++repeat){fit->trigger();flush();check(view->camera_state()==first,"Repeated Fit View changes the axis/sketch framing");}
            const auto upper=view->ray_at({0,0}),right=view->ray_at({double(view->width()),0}),lower=view->ray_at({0,double(view->height())});
            check(upper&&right&&lower,"Fit View rays missing");
            const double width=right->first.x-upper->first.x,height=lower->first.y-upper->first.y;
            check(std::abs(width)>1e-8&&std::abs(height)>1e-8,"Fit View has degenerate bounds");
            for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==sketch.id)
                for(const auto& point:edge.points) {
                    const double x=(point.x-upper->first.x)/width,y=(point.y-upper->first.y)/height;
                    check(x>.01&&x<.99&&y>.01&&y<.99,"Fit View clips the finite axis or sketch");
                }
        };
        const auto open_properties=[&] {
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
            check(row,"Feature wire row missing");window.show_tree_item_properties(row);flush();check(dialog(),"Feature wire properties missing");
        };
        expect();fit_wire();
        if(!axis_only) {
            for(int type=0;type<5;++type) {
                open_properties();
                const auto inactive=dialog()->pending_value().feature;
                auto* selector=dialog()->findChild<QComboBox*>("featureType");
                check(selector&&selector->count()==5,"Feature type selector missing");
                selector->setCurrentIndex(type);flush();
                const auto pending=dialog()->pending_value();
                const char* prefixes[]={"Bod","Osa","Rovina","Skica","Vytažení"};
                check(pending.name==QObject::tr(prefixes[type]).toStdString()+" 001" && pending.feature.automatic_name==pending.name,
                    "GUI type switch did not update its automatic name");
                check(pending.feature.sides==inactive.sides&&pending.feature.origin_centerline==inactive.origin_centerline&&
                      pending.feature.centroid_centerline==inactive.centroid_centerline,"Type switch discarded inactive settings");
                check(dialog()->findChild<QGroupBox*>("featurePlaneGroup")->isVisible()==(type!=0),"Wrong work-plane controls for Feature type");
                check(dialog()->findChild<QGroupBox*>("featureSketchGroup")->isVisible()==(type>=3),"Wrong Sketch controls for Feature type");
                check(dialog()->findChild<QGroupBox*>("featureResultGroup")->isVisible()==(type==4),"Wrong modeling controls for Feature type");
                check(dialog()->findChild<QCheckBox*>("featureOriginAxis")->isVisible()==(type==4)&&
                      dialog()->findChild<QCheckBox*>("featureCentroidAxis")->isVisible()==(type==4),"Datum type exposes optional path checkboxes");
                check(dialog()->findChild<QCheckBox*>("featureSymmetric")->isVisible()==(type==1||type==4),"Wrong symmetry control for Feature type");
                const auto source_count=std::ranges::count_if(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==sketch.id;});
                check((source_count>0)==(type>=3),"Feature type published the wrong source geometry");
                const auto point=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==feature.id&&p.reference.semantic_key=="point";});
                check(point!=view->mesh().points.end()&&point->label==pending.name,"Feature name is missing at its origin");
                if(type==4) {
                    const auto* thin=dialog()->findChild<QComboBox*>("featureThinSide");
                    const auto* symmetric=dialog()->findChild<QCheckBox*>("featureSymmetric");
                    check(thin->mapTo(dialog(),QPoint(0,thin->height())).y()<=symmetric->mapTo(dialog(),QPoint()).y(),
                        "Result controls overlap the symmetry row");
                }
                window.grab().save(QString::fromStdString((directory/("feature-type-"+std::to_string(type)+"-ui.png")).string()));
                dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Feature type cannot commit");
                run("save");const auto saved=document::PartDocument::load(path);
                check(saved.find_container(feature.id)->name==pending.name && saved.find_container(feature.id)->feature.automatic_name==pending.name,
                    "Save lost automatic Feature name");
                const char* icons[]={"point","axis","plane","sketch","sketch"};
                QTreeWidgetItem* typed_row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){typed_row=*it;break;}
                check(typed_row&&typed_row->text(0).contains(QString::fromStdString(pending.name)),"Tree did not update the Feature name");
                int sketch_rows=0,result_rows=0;
                for(int child=0;child<typed_row->childCount();++child) {
                    const auto role=typed_row->child(child)->data(0,Qt::UserRole+3).toString();
                    sketch_rows+=role=="part-sketch";
                    result_rows+=role=="part-container-entity";
                }
                check(sketch_rows==(type>=3?1:0)&&result_rows==(type>=3?0:1),
                    "Feature tree exposes an unused Sketch or duplicate Sketch result");
                check(typed_row->icon(0).pixmap(24,24).toImage()==resource_icon(icons[type]).pixmap(24,24).toImage(),
                    "Tree did not update its type-specific icon");
                check(static_cast<int>(saved.find_container(feature.id)->feature.type)==type&&saved.sketches.front().serialized()==original,
                    "Type commit changed the owned Sketch or lost the selected type");
                run("undo");run("redo");run("save");
                check(static_cast<int>(document::PartDocument::load(path).find_container(feature.id)->feature.type)==type,"Type Undo/Redo failed");
                open_properties();check(dialog()->findChild<QComboBox*>("featureType")->currentIndex()==type,"Reopening lost Feature type");
                dialog()->findChild<QComboBox*>("featureType")->setCurrentIndex((type+1)%5);dialog()->reject();flush();run("save");
                check(static_cast<int>(document::PartDocument::load(path).find_container(feature.id)->feature.type)==type,"Cancel committed Feature type");
            }
        }
        for(int cycle=0;cycle<3;++cycle) {
            open_properties();expect();fit_wire();
            auto* add=dialog()->findChild<QPushButton*>("featureAdd");
            check(add&&!add->isChecked(),"Sketch-only Feature reopened with Add enabled");
            add->click();flush();expect();
            check(!dialog()->pending_value().feature.sketch_only(),"Feature cannot enable a material operation");
            add->click();flush();expect();fit_wire();
            check(dialog()->pending_value().feature.sketch_only(),"Feature cannot return to sketch-only mode");
            dialog()->findChild<QDoubleSpinBox*>("featureProfileOffset")->setValue(7);flush();expect(false,7);
            dialog()->findChild<QDoubleSpinBox*>("featureProfileOffset")->setValue(0);flush();expect();
            dialog()->findChild<QPushButton*>("featureSketchButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            check(!dialog(),"Feature Sketcher entry failed");expect(true);fit_wire();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();check(dialog(),"Feature Sketcher return failed");expect();fit_wire();
            if(cycle==1)dialog()->reject();else dialog()->buttons()->button(QDialogButtonBox::Ok)->click();
            flush();check(!dialog(),"Feature wire properties did not close");expect();fit_wire();run("save");
            const auto saved=document::PartDocument::load(path);
            check(saved.sketches.front().serialized()==original,"Feature round trip changed native sketch geometry, IDs or flags");
        }
        window.grab().save(QString::fromStdString((directory/(axis_only?"feature-axis-cycle-ui.png":"feature-sketch-cycle-ui.png")).string()));
        run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Feature wire cannot reopen from disk");flush();
        run("body.activate",{{"body",body}});expect();fit_wire();open_properties();expect();dialog()->reject();flush();expect();
    }
}
