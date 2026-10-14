static void verify_feature_selection(QApplication& application, AssemblyWorkspaceWindow& window,
                                     const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    std::filesystem::create_directories(directory);window.resize(1366,900);window.show();flush();
    auto model=document::PartDocument::create_default();
    auto path=directory/"feature-selection.prtz";
    if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_SELECTION_FILE");!source.isEmpty()) {
        path=std::filesystem::u8path(source.toStdString());model=document::PartDocument::load(path);
    } else {
        auto first=sketcher::Sketch::create_default();static_cast<void>(first.add_rectangle(0,0,30,30));
        auto add=document::PartDocument::create_feature_container(first.id);
        add.name="Selection Add";add.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;
        add.feature.sides[0].length=10;first.owner_container_id=add.id;
        auto second=sketcher::Sketch::create_default();static_cast<void>(second.add_rectangle(8,8,20,20));
        auto cut=document::PartDocument::create_feature_container(second.id);
        cut.name="Selection Cut";cut.combine_mode=document::CombineMode::Subtract;
        cut.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;cut.feature.sides[0].length=20;
        second.owner_container_id=cut.id;model.history={add,cut};model.sketches={first,second};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Selection Body"));
        graph.insert({document::PartHistoryKind::Feature,add.id});graph.insert({document::PartHistoryKind::Feature,cut.id});
        model.set_body_history(graph);model.resolve_constructions();
        model.save(path,kernel::OcctKernel{}.evaluate_history(model.kernel_operations()));
    }
    check(window.open_document_path(QString::fromStdWString(path.wstring())),"Selection fixture cannot open");flush();
    const auto activate=window.execute_console_command(QString::fromStdString(commands::Json{
        {"command","body.activate"},{"arguments",{{"body",model.body_history.active_body_id()}}}}.dump()));
    check(activate.ok,"Selection Body cannot activate");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    check(tree&&view,"Selection workspace missing");
    const auto state=window.execute_console_command("documents").data;
    const auto project=[&](kernel::Vec3 point) {
        const auto center=view->ray_at({view->width()*.5,view->height()*.5});check(center.has_value(),"Selection camera ray missing");
        const auto normal=center->second;
        const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
        const auto hit=[&](QPointF screen) {
            const auto ray=view->ray_at(screen);check(ray.has_value(),"Selection projection ray missing");
            const double t=dot(sub(point,ray->first),normal)/dot(ray->second,normal);
            return kernel::Vec3{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,ray->first.z+t*ray->second.z};
        };
        const auto origin=hit({0,0}),u=sub(hit({100,0}),origin),v=sub(hit({0,100}),origin),r=sub(point,origin);
        const double uu=dot(u,u),vv=dot(v,v),uv=dot(u,v),det=uu*vv-uv*uv;
        return QPointF(100*(dot(r,u)*vv-dot(r,v)*uv)/det,100*(dot(r,v)*uu-dot(r,u)*uv)/det);
    };
    const auto mouse=[&](QWidget* widget,QPointF point) {
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();
        }
    };
    bool all=true;std::size_t targets=0;
    for(const auto& feature:model.history) {
        if(feature.feature_kind!=document::FeatureKind::Feature||feature.feature.type!=document::FeatureType::Modeling)continue;
        tree->clearSelection();view->clear_selection();flush();
        const auto revision=view->base_mesh_revision();
        ++targets;QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container"&&
            (*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
        check(row,"Selection Feature row missing");
        for(bool expanded:{false,true}) {
            tree->clearSelection();view->clear_selection();row->setExpanded(expanded);tree->scrollToItem(row);flush();
            mouse(tree->viewport(),tree->visualItemRect(row).center());
            const bool correct=tree->currentItem()==row&&tree->selectedItems().size()==1&&row->isExpanded()==expanded;
            std::cout<<"Feature selection Tree owner="<<feature.id<<" expected_expanded="<<expanded
                <<" actual_expanded="<<row->isExpanded()<<" role="<<tree->currentItem()->data(0,Qt::UserRole+3).toString().toStdString()<<std::endl;
            all&=correct;
        }
        const auto saved_palette=application.palette();
        for(bool dark:{false,true,false}) {
            auto palette=saved_palette;palette.setColor(QPalette::WindowText,dark?QColor("#EEEEEE"):QColor("#202020"));
            palette.setColor(QPalette::ButtonText,palette.color(QPalette::WindowText));application.setPalette(palette);flush();
            for(const auto mode:{QIcon::Normal,QIcon::Active,QIcon::Selected,QIcon::Disabled}) {
                const auto normal=row->icon(0).pixmap({18,18},mode,QIcon::Off).toImage();
                const auto expanded_icon=row->icon(0).pixmap({18,18},mode,QIcon::On).toImage();
                all&=normal==expanded_icon;
            }
        }
        application.setPalette(saved_palette);flush();
        for(int child=0;child<row->childCount();++child)if(row->child(child)->data(0,Qt::UserRole+3)=="part-sketch") {
            row->setExpanded(true);auto* sketch=row->child(child);tree->scrollToItem(sketch);flush();
            mouse(tree->viewport(),tree->visualItemRect(sketch).center());
            check(tree->currentItem()==sketch,"Explicit Sketch child selection changed its owner");break;
        }
        tree->clearSelection();view->clear_selection();row->setExpanded(false);flush();
        std::optional<QPointF> position;
        const auto find_hit=[&](const auto& edges) {
            for(const auto& edge:edges) {
                if(position||edge.points.size()<2)continue;
                if(edge.reference.owner_id!=feature.id&&edge.reference.owner_id!=feature.feature.sketch_id&&edge.display_owner_id!=feature.id)continue;
                for(std::size_t i=1;i<edge.points.size()&&!position;++i) {
                    const auto& a=edge.points[i-1];const auto& b=edge.points[i];
                    const auto p=project({(a.x+b.x)*.5,(a.y+b.y)*.5,(a.z+b.z)*.5});
                    const auto candidates=view->selection_candidates_at(p);
                    if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Container&&
                        (candidates.front().owner_id==feature.id||candidates.front().owner_id==feature.feature.sketch_id))position=p;
                }
            }
        };
        find_hit(view->mesh().edges);find_hit(view->mesh().original_references.edges);
        check(position.has_value(),"Selection Feature absent from ordinary common picker");
        mouse(view,*position);
        const auto selected=view->confirmed_candidate();auto* current=tree->currentItem();
        std::cout<<"Feature selection View expected="<<feature.id<<" picked="<<(selected?selected->owner_id:"")
            <<" tree="<<(current?current->data(0,Qt::UserRole).toString().toStdString():"")
            <<" role="<<(current?current->data(0,Qt::UserRole+3).toString().toStdString():"")
            <<" expanded="<<row->isExpanded()<<std::endl;
        all&=selected&&selected->owner_id==feature.id&&current==row&&tree->selectedItems().size()==1&&!row->isExpanded();
        check(!window.findChild<QWidget*>("featurePropertiesDialog"),"Ordinary selection opened Properties");
        check(window.execute_console_command("documents").data==state&&view->base_mesh_revision()==revision,
            "Ordinary selection changed the document or republished geometry");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,*position,QPointF(view->mapToGlobal(position->toPoint())),
            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();
        const auto* inspected=tree->currentItem();
        check(inspected&&inspected->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
            inspected->data(0,Qt::UserRole+3)=="part-container","Double-click inspection lost the owning Feature");
        check(window.execute_console_command("documents").data==state,"Double-click inspection changed the document");
    }
    check(targets>0&&all,"Feature selection expanded descendants, selected Sketch or changed its icon");
}
