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
    check(pending.name==QObject::tr("Prvek").toStdString()+" 001" && pending.feature.automatic_name==pending.name,
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
        feature.name=QObject::tr("Prvek").toStdString()+" 001";feature.feature.automatic_name=feature.name;
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
                const char* prefixes[]={"Bod","Osa","Rovina","Skica","Prvek"};
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
                const char* icons[]={"point","axis","plane","sketch","protrusion"};
                QTreeWidgetItem* typed_row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){typed_row=*it;break;}
                check(typed_row&&typed_row->text(0).contains(QString::fromStdString(pending.name)),"Tree did not update the Feature name");
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
