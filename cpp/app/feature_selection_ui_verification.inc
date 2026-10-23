static void verify_feature_selection(QApplication& application, AssemblyWorkspaceWindow& window,
                                     const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    std::filesystem::create_directories(directory);window.resize(1366,900);window.show();flush();
    auto model=document::PartDocument::create_default();
    auto path=directory/"feature-selection.prtz";
    if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_SELECTION_FILE");!source.isEmpty()) {
        const auto source_path=std::filesystem::u8path(source.toStdString());
        path=directory/source_path.filename();
        if(std::filesystem::absolute(source_path)!=std::filesystem::absolute(path))
            std::filesystem::copy_file(source_path,path,std::filesystem::copy_options::overwrite_existing);
        model=document::PartDocument::load(path);
    } else if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SELECTION_SKETCH_ONLY")) {
        auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_bspline({{-30,-40},{-10,-20},{-10,5},{10,25},{30,40}},3,false,false,1e-6,true));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.feature.type=document::FeatureType::Sketch;feature.name="Native spline Sketch";sketch.owner_container_id=feature.id;
        model.history={feature};model.sketches={sketch};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Native Sketch selection"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        model.set_body_history(graph);model.resolve_constructions();
        model.save(path,kernel::OcctKernel{}.evaluate_history(model.kernel_operations()));
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
    auto active_body=model.body_history.active_body_id();
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
    const auto check_sketch_point_color=[&](const document::HistoryContainer& feature,bool azure=true) {
        if(!authored_sketch_profile(feature))return;
        const auto sketch=std::ranges::find(model.sketches,feature.id,&sketcher::Sketch::owner_container_id);
        check(sketch!=model.sketches.end(),"Sketch point color source missing");
        const auto image=view->grabFramebuffer();
        const double scale=double(image.width())/view->width();
        std::size_t checked{};
        for(const auto& point:view->mesh().points) {
            if(point.reference.owner_id!=sketch->id||!point.reference.semantic_key.starts_with("point:"))continue;
            const auto pixel=(project(point.position)*scale).toPoint();
            if(!image.rect().adjusted(2,2,-2,-2).contains(pixel))continue;
            const auto color=image.pixelColor(pixel);
            const bool matches=std::abs(color.red()-interaction::selected.red())<40&&
                std::abs(color.green()-interaction::selected.green())<40&&
                std::abs(color.blue()-interaction::selected.blue())<40;
            check(matches==azure,"Whole-Sketch native points have the wrong selection color");
            ++checked;
        }
        check(checked>0,"Sketch point framebuffer check found no visible native points");
    };
    bool all=true;std::size_t targets=0;
    for(const auto& feature:model.history) {
        const bool sketch_profile=authored_sketch_profile(feature);
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SELECTION_SKETCH_ONLY")&&!sketch_profile)continue;
        const bool modeling=feature.feature_kind==document::FeatureKind::Feature&&
            feature.feature.type==document::FeatureType::Modeling&&!feature.feature.sketch_only();
        const bool flat=feature.feature_kind==document::FeatureKind::Flat;
        const bool bend=feature.feature_kind==document::FeatureKind::Bend;
        if(!modeling&&!flat&&!bend&&!sketch_profile)continue;
        const auto* dialog_name=flat?"flatPropertiesDialog":bend?"bendPropertiesDialog":"featurePropertiesDialog";
        if(const auto* body=model.body_owner_for_object(feature.id);body&&body->scope.id!=active_body) {
            check(window.execute_console_command(QString::fromStdString(commands::Json{
                {"command","body.activate"},{"arguments",{{"body",body->scope.id}}}}.dump())).ok,
                "Library preview feature Body cannot activate");
            active_body=body->scope.id;flush();
        }
        const auto state=window.execute_console_command("documents").data;
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
            check_sketch_point_color(feature);
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
            check(tree->currentItem()==sketch,"Explicit Sketch child selection changed its owner");
            if(sketch_profile) {
                static_cast<void>(view->grabFramebuffer());
                check(view->property("gpuSourceWireVertexCount").toInt()>0,"Explicit Sketch child bypassed its native cached wire");
                check_sketch_point_color(feature);
            }
            break;
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
                    if(std::ranges::any_of(candidates,[&](const auto& candidate) {
                        return candidate.kind==viewer::CandidateKind::Container&&
                            (candidate.owner_id==feature.id||candidate.owner_id==feature.feature.sketch_id);
                    }))position=p;
                }
            }
        };
        find_hit(view->mesh().edges);find_hit(view->mesh().original_references.edges);
        check(position.has_value(),"Selection Feature absent from ordinary common picker");
        const auto wanted=[&] {
            const auto candidate=view->offered_candidate();
            return candidate&&candidate->kind==viewer::CandidateKind::Container&&
                (candidate->owner_id==feature.id||candidate->owner_id==feature.feature.sketch_id);
        };
        const auto offer_feature=[&] {
            QMouseEvent offer(QEvent::MouseMove,*position,QPointF(view->mapToGlobal(position->toPoint())),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&offer);flush();
            for(unsigned cycle=0;cycle<64&&!wanted();++cycle) {
                for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::RightButton,
                        type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&event);flush();
                }
            }
            check(wanted(),"RMB cycling cannot offer the library feature behind another object");
        };
        offer_feature();
        const auto profile_color_visible=[&](QColor color) {
            const auto image=view->grabFramebuffer();
            const double scale=double(image.width())/view->width();
            const auto pixel=(*position*scale).toPoint();
            image.save(QString::fromStdWString((directory/"sketch-profile-color-probe.png").wstring()));
            // The first offered segment can be under an endpoint marker.
            // Include its adjacent stroke, beyond that marker's fixed radius.
            const int radius=static_cast<int>(std::ceil(12*scale));
            for(int y=std::max(0,pixel.y()-radius);y<std::min(image.height(),pixel.y()+radius+1);++y)
                for(int x=std::max(0,pixel.x()-radius);x<std::min(image.width(),pixel.x()+radius+1);++x) {
                    const auto actual=image.pixelColor(x,y);
                    if(std::abs(actual.red()-color.red())<40&&std::abs(actual.green()-color.green())<40&&
                            std::abs(actual.blue()-color.blue())<40)return true;
                }
            return false;
        };
        if(sketch_profile)check(profile_color_visible(interaction::hover),
            "Native Sketch hover wire is absent or covered by the base Sketch color");
        check_sketch_point_color(feature);
        mouse(view,*position);
        if(sketch_profile) {
            QMouseEvent leave(QEvent::MouseMove,QPointF(2,2),QPointF(view->mapToGlobal(QPoint(2,2))),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&leave);flush();
            check(profile_color_visible(interaction::selected),
                "Native Sketch confirmed color disappears after the pointer leaves");
            check_sketch_point_color(feature);
        }
        static_cast<void>(view->grabFramebuffer());
        const auto preview_vertices=view->property("gpuSourceWireVertexCount").toInt();
        const auto preview_uploads=view->property("gpuSourceWireUploadCount").toULongLong();
        if(sketch_profile) {
            const auto picked=view->confirmed_candidate();
            std::cout<<"Sketch selected owner="<<(picked?picked->owner_id:"")<<" key="<<(picked?picked->semantic_key:"")
                <<" GPU vertices="<<preview_vertices<<" geometry="<<(picked?int(picked->geometry):-1)<<std::endl;
            for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==feature.feature.sketch_id)
                std::cout<<"Sketch displayed edge "<<edge.reference.semantic_key<<" display_owner="<<edge.display_owner_id
                    <<" samples="<<edge.points.size()<<" overlay="<<edge.overlay<<std::endl;
            window.grab().save(QString::fromStdWString((directory/"sketch-profile-picked.png").wstring()));
        }
        check(preview_vertices>0&&view->property("selectedFeatureFaceTriangleCount").toULongLong()==0,
            "Ordinary feature selection omitted the authored preview or filled faces");
        const auto selected=view->confirmed_candidate();auto* current=tree->currentItem();
        std::cout<<"Feature selection View expected="<<feature.id<<" picked="<<(selected?selected->owner_id:"")
            <<" tree="<<(current?current->data(0,Qt::UserRole).toString().toStdString():"")
            <<" role="<<(current?current->data(0,Qt::UserRole+3).toString().toStdString():"")
            <<" expanded="<<row->isExpanded()<<std::endl;
        all&=selected&&selected->owner_id==feature.id&&current==row&&tree->selectedItems().size()==1&&!row->isExpanded();
        check(!window.findChild<QWidget*>(dialog_name),"Ordinary selection opened Properties");
        check(window.execute_console_command("documents").data==state&&view->base_mesh_revision()==revision,
            "Ordinary selection changed the document or republished geometry");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,*position,QPointF(view->mapToGlobal(position->toPoint())),
            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();
        const auto* inspected=tree->currentItem();
        check(inspected&&inspected->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
            inspected->data(0,Qt::UserRole+3)=="part-container","Double-click inspection lost the owning Feature");
        check(window.execute_console_command("documents").data==state,"Double-click inspection changed the document");
        static_cast<void>(view->grabFramebuffer());
        check(view->property("gpuSourceWireVertexCount").toInt()==preview_vertices&&
            view->property("gpuSourceWireUploadCount").toULongLong()==preview_uploads,
            "Double-click changed or reuploaded the ordinary authored preview");
        const auto selection_preview=view->container_inspection_wire();
        if(sketch_profile) {
            const auto sketch=std::ranges::find(model.sketches,feature.id,&sketcher::Sketch::owner_container_id);
            check(sketch!=model.sketches.end(),"Selected Sketch has no native geometry");
            auto expected=sketch->viewer_mesh();sketch->filter_hidden_3d_geometry(expected);
            std::erase_if(expected.edges,[](const auto& edge){return !native_sketch_profile_edge(edge);});
            if(const auto* body=model.body_owner_for_object(feature.id))expected=model.place_body_mesh(std::move(expected),body->scope.id);
            check(selection_preview.size()==expected.edges.size(),"Sketch inspection contains duplicate or foreign profile curves");
            for(std::size_t i=0;i<expected.edges.size();++i)
                check(selection_preview[i].points==expected.edges[i].points,"Sketch selection/inspection differs from its visible native curve");
            for(int repeat=0;repeat<4;++repeat){flush();static_cast<void>(view->grabFramebuffer());}
            check(view->property("gpuSourceWireUploadCount").toULongLong()==preview_uploads,
                "Sketch repaint rebuilt or uploaded the unchanged native wire");
            view->grabFramebuffer().save(QString::fromStdWString((directory/"sketch-profile-selection.png").wstring()));
            check_sketch_point_color(feature);
            mouse(view,QPointF(2,2));
            check_sketch_point_color(feature,false);
            check(window.execute_console_command("documents").data==state&&view->base_mesh_revision()==revision,
                "Clearing whole-Sketch selection changed document geometry");
            std::cout<<"Pure Sketch hover/LMB/Tree/double-click: exact native profile points, no duplicate calculated curve, no mutation or repeated upload passed"<<std::endl;
            continue;
        }
        check(!selection_preview.empty()&&std::ranges::all_of(selection_preview,[](const auto& edge) {
            return edge.reference.semantic_key.starts_with("preview:");
        }),"Feature selection used calculated topology instead of the command preview");
        if(modeling) {
            const auto dimensions=[&] {
                return std::ranges::count_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature.id;});
            };
            const auto numeric=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d) {
                return d.reference.owner_id==feature.id&&d.reference.semantic_key=="parameter:side0_length";
            });
            if(numeric!=view->mesh().dimensions.end()) {
                viewer::ViewerCandidate target{viewer::CandidateKind::Dimension,0,
                    static_cast<std::size_t>(numeric-view->mesh().dimensions.begin()),feature.id,numeric->reference.semantic_key,{}};
                const auto label=view->candidate_dimension_label_position(target);
                check(label.has_value(),"Inspection parameter has no visible label");
                mouse(view,*label);
                QMouseEvent edit(QEvent::MouseButtonDblClick,*label,QPointF(view->mapToGlobal(*label)),
                    Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&edit);flush();
                auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                check(field&&field->isVisible(),"Inspection no-op editor did not open");
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                check(dimensions()>0&&window.execute_console_command("documents").data==state,
                    "Unchanged parameter editor changed the model or lost inspection");
                mouse(view,*position);
                for(auto type:{QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    QMouseEvent done(type,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::MiddleButton,
                        type==QEvent::MouseButtonDblClick?Qt::MiddleButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&done);flush();
                }
                check(dimensions()==0&&window.execute_console_command("documents").data==state,
                    "MMB double-click could not finish inspection after unchanged edit and selection");
                // Finishing inspection clears the prior selection. At an
                // overlapping library edge, offer the same owner again.
                offer_feature();
                mouse(view,*position);
                QMouseEvent again(QEvent::MouseButtonDblClick,*position,QPointF(view->mapToGlobal(position->toPoint())),
                    Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&again);flush();
                check(dimensions()>0,"Repeated inspection did not restore parameter dimensions");
                tree->clearSelection();flush();
                check(dimensions()==0&&window.execute_console_command("documents").data==state,
                    "Clearing Tree selection orphaned displayed parameter dimensions");
            }
        }
        QTreeWidgetItem* properties_row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole+3)=="part-container"&&
               (*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){properties_row=*it;break;}
        check(properties_row,"Preview comparison lost the owning feature");
        window.show_tree_item_properties(properties_row);flush();
        auto* properties=dynamic_cast<QDialog*>(window.findChild<QWidget*>(dialog_name));
        if(!properties||view->transient_edges().empty()) {
            const auto* status=window.findChild<QLabel*>("workspaceState");
            std::cerr<<"Preview comparison owner="<<feature.id<<" name="<<feature.name
                <<" status="<<(status?status->text().toStdString():"")<<std::endl;
        }
        check(properties&&!view->transient_edges().empty(),"Properties supplied no command preview for comparison");
        // MeshView applies the Body/occurrence mapping on submission. Both
        // exposed packets are already in scene coordinates.
        kernel::ViewerMesh command_preview;command_preview.edges=view->transient_edges();
        const auto segments=[](const auto& edges) {
            std::vector<std::array<double,6>> result;
            for(const auto& edge:edges)if(!edge.construction&&!edge.parameter_seam)
                for(std::size_t i=1;i<edge.points.size();++i) {
                    auto a=edge.points[i-1],b=edge.points[i];
                    result.push_back({a.x,a.y,a.z,b.x,b.y,b.z});
                }
            return result;
        };
        const auto actual=segments(selection_preview),expected=segments(command_preview.edges);
        check(actual.size()==expected.size(),"Selection contains extra or missing command-preview segments");
        // These are undirected strokes. Exact lexicographic sorting is unstable
        // when an equivalent rotated frame differs by a few ulps at zero.
        // Consume each expected stroke once, checking both endpoints at the
        // original coordinate tolerance; retain the exact count requirement.
        std::vector<bool> matched(expected.size());
        for(std::size_t i=0;i<actual.size();++i) {
            std::optional<std::size_t> match;
            for(std::size_t j=0;j<expected.size()&&!match;++j)if(!matched[j]) {
                bool same=true,reversed=true;
                for(std::size_t k=0;k<6;++k) {
                    same&=std::abs(actual[i][k]-expected[j][k])<1e-7;
                    reversed&=std::abs(actual[i][k]-expected[j][(k+3)%6])<1e-7;
                }
                if(same||reversed)match=j;
            }
            if(!match) {
                commands::Json details={{"selection",actual},{"properties",expected}};
                if(const auto* primitive=dynamic_cast<app::PrimitivePropertiesDialog*>(properties)) {
                    details["initial_parameters"]=document::serialize_feature_parameters(feature.feature);
                    details["pending_parameters"]=document::serialize_feature_parameters(primitive->pending_value().feature);
                }
                std::cerr<<"Unmatched preview stroke owner="<<feature.id<<" name="<<feature.name<<" segment="<<i
                    <<" details="<<details.dump()<<std::endl;
            }
            check(match.has_value(),"Selection differs from the actual Properties preview wire");
            matched[*match]=true;
        }
        properties->reject();flush();
        check(window.execute_console_command("documents").data==state,"Command preview comparison or Cancel changed the document");
    }
    check(targets>0&&all,"Feature selection expanded descendants, selected Sketch or changed its icon");
}
