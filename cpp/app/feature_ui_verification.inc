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
