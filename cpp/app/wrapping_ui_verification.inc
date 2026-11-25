int verify_wrapping_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button=Qt::LeftButton) {
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();
        }
    };
    try {
        if(const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_WRAP_DOWNSTREAM_MODEL");!fixture.isEmpty()) {
            const auto path=directory/"downstream-wrap.prtz";
            std::filesystem::copy_file(std::filesystem::path(fixture.toStdWString()),path,std::filesystem::copy_options::overwrite_existing);
            window.resize(1440,1000);window.show();
            check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open downstream Wrap copy");flush();
            for(int repeat=0;repeat<3;++repeat) {
                window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                std::vector<kernel::BodyResult> cache;const auto part=document::PartDocument::load(path,&cache);
                check(!cache.empty()&&cache.back().calculation_errors.empty(),"Downstream Wrap regeneration failed");
                auto* tree=window.findChild<QTreeWidget*>("documentTree");
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container") {
                    if((*it)->data(0,missing_reference_role).toBool())std::cerr<<"False missing reference: "<<(*it)->text(0).toStdString()<<'\n';
                    check(!(*it)->data(0,missing_reference_role).toBool(),"Valid history input is marked red after a later extrusion");
                }
            }
            window.grab().save(QString::fromStdWString((directory/"downstream-wrap-repaired.png").wstring()));
            std::cout<<"Downstream Wrap: three explicit GUI regenerations, native save and valid Tree reference states passed\n";return 0;
        }
        if(const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_WRAP_SELECTION_FILE");!fixture.isEmpty()) {
            const auto model=document::PartDocument::load(std::filesystem::path(fixture.toStdWString()));
            window.resize(1440,1000);window.show();
            check(window.open_document_path(fixture),"Wrap selection fixture did not open");flush();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},
                {"arguments",{{"body",model.body_history.active_body_id()}}}}.dump())).ok,"Wrap selection Body did not activate");flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Wrap selection workspace is missing");
            view->fit_all();flush();
            const auto project=[&](kernel::Vec3 point) {
                const auto center=view->ray_at({view->width()*.5,view->height()*.5});check(center.has_value(),"Camera ray missing");
                const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
                const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
                const auto hit=[&](QPointF screen) {
                    const auto ray=view->ray_at(screen);check(ray.has_value(),"Projection ray missing");
                    const double t=dot(sub(point,ray->first),center->second)/dot(ray->second,center->second);
                    return kernel::Vec3{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,ray->first.z+t*ray->second.z};
                };
                const auto origin=hit({0,0}),u=sub(hit({100,0}),origin),v=sub(hit({0,100}),origin),r=sub(point,origin);
                const double uu=dot(u,u),vv=dot(v,v),uv=dot(u,v),det=uu*vv-uv*uv;
                return QPointF(100*(dot(r,u)*vv-dot(r,v)*uv)/det,100*(dot(r,v)*uu-dot(r,u)*uv)/det);
            };
            const auto state=window.execute_console_command("documents").data;
            const auto calls=kernel::body_calculation_count.load();
            for(const auto& feature:model.history)if(feature.feature_kind==document::FeatureKind::Feature&&
                    feature.feature.type==document::FeatureType::Wrapping&&feature.feature.wrapping.output==document::WrappingOutput::Curves) {
                tree->clearSelection();view->clear_selection();flush();
                std::vector<kernel::ViewerEdge> curves;
                for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==feature.id&&edge.reference.semantic_key.starts_with("wrap:sketch:"))curves.push_back(edge);
                check(!curves.empty(),"Wrap result curves are absent after reopening");
                unsigned hits{};QPointF inspection_hit;
                for(const auto& edge:curves)if(edge.points.size()>1) {
                    const auto a=edge.points[(edge.points.size()-1)/2],b=edge.points[(edge.points.size()-1)/2+1];
                    const auto pixel=project({(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2});
                    if(!view->rect().adjusted(4,4,-4,-4).contains(pixel.toPoint()))continue;
                    QMouseEvent hover(QEvent::MouseMove,pixel,QPointF(view->mapToGlobal(pixel.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&hover);flush();
                    const auto image=view->grabFramebuffer();const double scale=double(image.width())/view->width();
                    image.save(QString::fromStdWString((directory/("wrap-hover-"+std::to_string(hits)+".png")).wstring()));
                    const auto candidate=view->hovered_candidate();
                    std::cout<<"Wrap curve hover: overlay="<<edge.overlay<<" owner="<<(candidate?candidate->owner_id:"none")
                        <<" kind="<<(candidate?int(candidate->kind):-1)<<" wire="<<view->property("gpuSourceWireVertexCount").toInt()<<std::endl;
                    check(candidate&&candidate->kind==viewer::CandidateKind::Container&&candidate->owner_id==feature.id,"Hover on a wrapped curve selects another object");
                    bool azure{};const auto p=(pixel*scale).toPoint();
                    for(int y=p.y()-3;y<=p.y()+3;++y)for(int x=p.x()-3;x<=p.x()+3;++x)if(image.rect().contains(x,y)) {
                        const auto c=image.pixelColor(x,y);azure|=c.red()<60&&c.green()>140&&c.blue()>180;
                    }
                    check(azure&&view->property("gpuSourceWireVertexCount").toInt()>0,"Wrapped curve hover has no visible azure preview");
                    ++hits;inspection_hit=pixel;
                }
                check(hits>0,"No wrapped curve hover was exercised");
                check(std::ranges::none_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==feature.feature.sketch_id;}),
                    "Hover exposed the planar source Sketch");
                click(view,inspection_hit);flush();
                QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
                    (*it)->data(0,Qt::UserRole+3)=="part-container")row=*it;
                check(row&&row->isSelected(),"Wrap View click did not select its Tree feature");
                check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature.id||d.reference.owner_id==feature.feature.sketch_id;}),
                    "Single click exposed Wrap dimensions");
                QMouseEvent inspect(QEvent::MouseButtonDblClick,inspection_hit,QPointF(view->mapToGlobal(inspection_hit.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(view,&inspect);flush();
                row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
                    (*it)->data(0,Qt::UserRole+3)=="part-container")row=*it;
                check(row&&row->isSelected(),"Wrap inspection lost Tree selection");
                check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==feature.feature.sketch_id;}),
                    "Double click did not display the planar source Sketch");
                check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==feature.id&&edge.reference.semantic_key.starts_with("wrap:sketch:");}),
                    "Double click hid the wrapped result");
                check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature.id||d.reference.owner_id==feature.feature.sketch_id;}),
                    "Double click omitted Wrap dimensions");
                view->grabFramebuffer().save(QString::fromStdWString((directory/"wrap-source-and-result.png").wstring()));
            }
            check(window.execute_console_command("documents").data==state&&kernel::body_calculation_count.load()==calls,"Wrap hover changed history or calculated geometry");
            return 0;
        }
        for(int output=0;output<4;++output) {
            workspace::Workspace live;kernel::OcctKernel engine;
            auto part=document::PartDocument::create_default();const auto document_id=part.document_id;
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Wrap source");part.set_body_history(std::move(graph));
            live.add_part(part);live.activate(document_id);
            auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
            static_cast<void>(base_sketch.add_circle(0,0,30));
            auto base=document::PartDocument::create_feature_container(base_sketch.id);base.feature.sides[0].length=20;
            base.feature.sides[1].operation=document::FeatureSideOperation::Extrusion;base.feature.sides[1].length=20;
            workspace::commit_profile(live,engine,document_id,base,workspace::ProfileEditMode::Create,base_sketch);
            auto source=sketcher::Sketch::create_default();source.plane_auto=false;
            static_cast<void>(source.add_rectangle(-5,-4,5,4));
            sketcher::TemplateImage artwork;artwork.id="wrap-artwork";artwork.name="Embedded artwork";
            artwork.format="svg";artwork.x=-4;artwork.y=-3;artwork.width=8;artwork.height=6;artwork.pixel_width=80;artwork.pixel_height=60;artwork.template_coordinates=false;
            artwork.data_base64=QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='60'><rect width='80' height='60' fill='#ff0000'/><rect x='40' width='40' height='60' fill='#0000ff'/><path d='M10 45V15H30' stroke='white' stroke-width='5' fill='none'/></svg>").toBase64().toStdString();
            source.reference_images.push_back(artwork);
            auto feature=document::PartDocument::create_feature_container(source.id);feature.name="Wrap GUI";
            feature.feature.type=document::FeatureType::Sketch;feature.placement.x=30*std::cos(-.37);feature.placement.y=30*std::sin(-.37);
            feature.placement.rotation_z=-.37*180/std::numbers::pi;
            feature.placement.rotation_y=90;feature.placement.absolute_rotation_y=90;
            workspace::commit_profile(live,engine,document_id,feature,workspace::ProfileEditMode::Create,source);
            auto* state=live.open_part(document_id);const auto file=directory/("wrap-ui-"+std::to_string(output)+".prtz");
            state->session.document().save(file,state->session.calculated_boundaries());
            window.resize(1440,1000);window.show();check(window.open_document_path(QString::fromStdWString(file.wstring())),"Wrap fixture did not open");flush();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Wrap Body activation failed");flush();
            if(auto* modeling=window.findChild<QAction*>("applicationModeAction0"))modeling->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Wrap View is missing");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto row=[&]()->QTreeWidgetItem* {
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
                return nullptr;
            };
            const auto editor=[&]()->PrimitivePropertiesDialog* {
                for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
                return nullptr;
            };
            const auto baseline=window.execute_console_command("documents").data;
            const auto before=kernel::body_calculation_count.load();
            for(bool relief:{false,true}) {
                auto* action=window.findChild<QAction*>(relief?"wrapReliefAction":"wrapSketchAction");
                check(action&&action->isEnabled(),"Standalone Wrap command is unavailable");action->trigger();flush();
                check(editor()&&editor()->pending_value().feature.type==document::FeatureType::Wrapping&&
                    editor()->pending_value().feature.wrapping.output==(relief?document::WrappingOutput::Relief:document::WrappingOutput::Curves),
                    "Standalone Wrap command opened the wrong feature type");
                editor()->reject();flush();
                check(window.execute_console_command("documents").data==baseline&&kernel::body_calculation_count.load()==before,
                    "Cancelled standalone Wrap changed history or calculated geometry");
            }
            check(row(),"Wrap Tree row missing");window.show_tree_item_properties(row());flush();check(editor(),"Wrap shared Properties did not open");
            check(editor()->windowType()==Qt::SubWindow,"Wrap Properties is not an internal window");
            auto* type=editor()->findChild<QComboBox*>("featureType");check(type&&type->count()>int(document::FeatureType::Wrapping),"Wrap is absent from Feature types");
            type->setCurrentIndex(output==2?wrap_relief_type_index:feature_type_index(document::FeatureType::Wrapping));flush();
            click(type,QPointF(type->width()-8,type->height()/2));
            check(type->view()->isVisible()&&type->view()->window()->geometry().top()>=type->mapToGlobal(QPoint(0,type->height())).y(),"Feature type menu did not open downward");
            type->hidePopup();flush();
            check(!type->itemIcon(feature_type_index(document::FeatureType::Wrapping)).isNull(),"Wrap icon is missing");
            auto* panel=editor()->wrapping_panel();check(panel&&panel->isVisible(),"Wrap panel is missing");
            if(output!=2) {
                auto* result=editor()->findChild<QComboBox*>("wrappingOutput");
                result->setCurrentIndex(result->findData(output));
            }flush();
            check(panel->parameters().output==static_cast<document::WrappingOutput>(output),"Wrap type selected the wrong output mode");
            auto* target=editor()->findChild<QTableWidget*>("wrappingTarget");check(target,"Wrap target table is missing");
            for(auto* ancestor=target->parentWidget();ancestor;ancestor=ancestor->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(target);
            flush();click(target->viewport(),target->visualItemRect(target->item(0,2)).center());check(panel->active_pick(),"Wrap target field did not arm picking");
            const auto pick=[&] {
                view->fit_all();flush();std::optional<QPointF> point;
                const auto cylinder=[&](const auto& candidate) {
                    const kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,candidate.instance_path};
                    for(const auto* faces:{&view->mesh().triangle_references,&view->mesh().original_references.triangle_references})
                        for(const auto& face:*faces)if(kernel::wrapped_image_face_matches(reference,face)) {
                            const auto support=face.surface?face.surface:face.contour_surface;
                            if(support&&support->kind==kernel::SurfaceGeometry::Kind::Cylinder)return true;
                        }
                    return false;
                };
                for(int y=20;y<view->height()-20&&!point;y+=7)for(int x=20;x<view->width()-20&&!point;x+=7) {
                    const auto candidates=view->selection_candidates_at(QPointF(x,y));
                    const auto face=std::ranges::find_if(candidates,[](const auto& candidate){return candidate.kind==viewer::CandidateKind::Face;});
                    if(face!=candidates.end()&&face->owner_id==base.id&&cylinder(*face))point=QPointF(x,y);
                }
                if(!point) {
                    window.grab().save(QString::fromStdWString((directory/"wrap-pick-failure.png").wstring()));
                    std::set<std::string> diagnostics;
                    for(const auto& face:view->mesh().triangle_references)diagnostics.insert(face.owner_id+" / "+face.semantic_key+" / "+face.instance_path+" / "+(face.surface?std::to_string(int(face.surface->kind)):"no-support"));
                    for(const auto& line:diagnostics)std::cerr<<line<<'\n';
                    std::cerr<<"base="<<base.id<<" view="<<view->width()<<"x"<<view->height()<<'\n';
                }
                check(point.has_value(),"Cylindrical face is missing from Wrap picker");
                QMouseEvent move(QEvent::MouseMove,*point,QPointF(view->mapToGlobal(point->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
                click(view,*point);check(panel->parameters().target.has_value(),"Wrap face click did not retain the target");
            };
            pick();target->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();check(panel->inspected_faces().size()==1,"Wrap inspection failed");
            target->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();check(!panel->parameters().target&&panel->inspected_faces().empty()&&panel->active_pick(),"Wrap clear did not reset inspection and arm entry");pick();
            {
                const auto first=*panel->parameters().target;
                auto other=first;bool found=false;
                for(const auto& face:view->mesh().original_references.triangle_references)if(face.owner_id==base.id&&(face.surface?face.surface:face.contour_surface)&&
                    (face.surface?face.surface:face.contour_surface)->kind==kernel::SurfaceGeometry::Kind::Cylinder&&face!=first.reference){other.reference=face;found=true;break;}
                check(found,"Two-sided cylinder has no second authored face");
                check(std::ranges::any_of(view->mesh().edges,[](const auto& edge){return kernel::opposite_feature_side_faces(edge)&&!edge.parameter_seam;}),
                    "Viewer omitted the boundary between the two selectable cylindrical sides");
                click(target->viewport(),target->visualItemRect(target->item(1,2)).center());
                check(panel->active_pick()&&panel->set_target(other),"Wrap continuation row did not accept another face");flush();
                check(panel->parameters().continuations.size()==1&&target->rowCount()==3,"Wrap removed a redundant authored target");
                check(target->columnCount()==4&&!panel->findChild<QPushButton*>("wrappingTargetsUp")&&
                    !panel->findChild<QPushButton*>("wrappingTargetsDown")&&target->findChildren<QCheckBox*>().empty(),
                    "Wrap target table retains meaningless ordering controls");
                target->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
                check(panel->parameters().target->reference==first.reference&&panel->parameters().continuations.front().reference==other.reference&&
                    panel->inspected_faces().size()==1,"Inspection changed authored target order");
                panel->begin_entry(1);check(panel->set_target(other),"Wrap continuation replacement failed");flush();
                check(panel->inspected_faces().size()==1,"Replacing another row cleared independent inspection");
                target->cellWidget(1,1)->findChild<QPushButton*>()->click();flush();
                check(panel->parameters().continuations.empty()&&panel->active_pick(),"Wrap removal failed to remove a list row");
                check(panel->set_target(other),"Wrap could not reinsert a continuation");flush();
                auto* type=editor()->findChild<QComboBox*>("featureType");const int original_type=type->currentIndex();
                for(int kind:{5,projection_relief_type_index,6,wrap_relief_type_index,4,5,6}) {
                    type->setCurrentIndex(kind);flush();
                    const auto pending=editor()->pending_value();
                    if(pending.feature.type==document::FeatureType::Projection)
                        check(pending.feature.projection.targets==std::vector<document::ExtrusionParameters::EndTarget>{first,other},
                            "Type conversion lost exact Projection targets or order");
                    if(pending.feature.type==document::FeatureType::Wrapping)
                        check(pending.feature.wrapping.target==first&&pending.feature.wrapping.continuations==std::vector<document::ExtrusionParameters::EndTarget>{other},
                            "Type conversion lost exact Wrap targets or order");
                }
                type->setCurrentIndex(original_type);flush();
                if(output!=2)editor()->findChild<QComboBox*>("wrappingOutput")->setCurrentIndex(editor()->findChild<QComboBox*>("wrappingOutput")->findData(output));
                flush();
                const auto retained=panel->parameters();const int row_height=target->rowHeight(0);
                editor()->resize(editor()->width()+80,editor()->height()+70);flush();
                check(panel->parameters()==retained&&target->rowHeight(0)==row_height,"Wrap resizing changed rows or parameters");
            }
            check(!view->transient_edges().empty(),"Wrap has no live authored wire");
            check(kernel::body_calculation_count.load()==before,"Wrap picking or preview invoked OCCT");
            if(output==2) {
                auto* correction=panel->findChild<QDoubleSpinBox*>("wrappingHeight");
                for(double value:{-2.,3.,-1.}) {
                    check(editor()->set_inline_parameter_value("relief_depth",value),"Relief inline value did not reach Properties");flush();const auto pending=editor()->pending_value();
                    for(const auto& edge:view->transient_edges())check(edge.color=="#00D1FF","Wrap relief material preview is not azure");
                    check(pending.feature.wrapping.height==std::abs(value)&&
                        pending.combine_mode==(value<0?document::CombineMode::Subtract:document::CombineMode::Add),
                        "Signed correction did not determine the material direction");
                }
                const auto retained=editor()->pending_value();
                const auto original_settings=ApplicationSettings::load(QDir::currentPath());
                QTemporaryDir temporary;check(temporary.isValid(),"Wrap localization directory unavailable");
                for(const auto* language:{"cs","en","de","fr","ru"}) {
                    QSettings ini(temporary.filePath("config.ini"),QSettings::IniFormat);
                    ini.setValue("Application/Language",QString::fromLatin1(language));
                    ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
                    auto settings=ApplicationSettings::load(temporary.path());
                    apply_application_translations(application,settings);flush();
                    check(type->itemText(feature_type_index(document::FeatureType::Wrapping))==settings.qt_translations.value("Wrap sketch")&&type->itemText(wrap_relief_type_index)==settings.qt_translations.value("Wrap relief"),"Wrap Feature types did not translate");
                    check(type->itemText(5)==settings.qt_translations.value("Project sketch")&&
                        type->itemText(projection_relief_type_index)==settings.qt_translations.value("Project relief"),"Projection Feature types did not translate");
                    check(!panel->findChild<QCheckBox*>("wrappingReverseU")&&!panel->findChild<QCheckBox*>("wrappingReverseV"),"Wrap retained redundant direction controls");
                    check(panel->findChild<QDoubleSpinBox*>("wrappingHeight")->toolTip()==settings.qt_translations.value("Positive correction adds material outward; negative correction removes material inward."),"Signed Wrap correction did not translate");
                    check(std::ranges::any_of(panel->findChildren<QLabel*>(),[&](const auto* label){return label->text()==settings.qt_translations.value("Korekce");}),"Wrap correction did not translate");
                    for(const auto* theme:{"dark","light"}) {
                        settings.theme=theme;apply_application_appearance(application,settings);flush();
                        check(!type->itemIcon(6).pixmap(24,24).isNull(),"Wrap theme icon is missing");
                        window.grab().save(QString::fromStdWString((directory/(std::string("wrap-")+language+"-"+theme+".png")).wstring()));
                    }
                    check(editor()->pending_value()==retained,"Wrap language or theme changed authored data");
                }
                apply_application_translations(application,original_settings);apply_application_appearance(application,original_settings);flush();
                check(kernel::body_calculation_count.load()==before,"Wrap language or theme calculated geometry");
            }
            window.grab().save(QString::fromStdWString((directory/("wrap-properties-"+std::to_string(output)+".png")).wstring()));
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor()) {
                std::string errors;for(auto* label:editor()->findChildren<QLabel*>())if(label->isVisible())errors+=label->text().toStdString()+" / ";
                throw std::runtime_error("Wrap OK failed: "+errors);
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> cached;const auto saved=document::PartDocument::load(file,&cached);
            check(saved.find_container(feature.id)->feature.type==document::FeatureType::Wrapping&&!cached.empty()&&cached.back().calculation_errors.empty(),"Saved Wrap definition or geometry is invalid");
            check(saved.find_container(feature.id)->feature.wrapping.continuations.size()==1,"Saved Wrap lost the redundant authored target row");
            if(output==2)check(saved.find_container(feature.id)->combine_mode==document::CombineMode::Subtract,
                "Negative correction did not persist its inward material operation");
            if(output==0)for(const auto& point:view->mesh().points)if(point.reference.owner_id==feature.id)
                check(!point.always_visible,"Wrapped Sketch retained ordinary black endpoint markers");
            check(cached.back().mesh.images.size()==1&&!cached.back().mesh.images.front().surface_vertices.empty(),"Saved Wrap lost the embedded curved image");
            check(cached.back().mesh.images.front().data_base64==artwork.data_base64,"Wrap changed embedded image bytes");
            const auto committed=window.execute_console_command("documents").data;const auto calculations=kernel::body_calculation_count.load();
            window.show_tree_item_properties(row());flush();check(editor(),"Wrap Properties could not reopen");
            {
                auto pending_document=saved;*pending_document.find_container(feature.id)=editor()->pending_value();
                std::ofstream(directory/"wrap-saved.json")<<saved.serialized().dump(2);
                std::ofstream(directory/"wrap-pending.json")<<pending_document.serialized().dump(2);
            }
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor()||window.execute_console_command("documents").data!=committed||kernel::body_calculation_count.load()!=calculations)
                std::cerr<<"Unchanged diagnostics: open="<<(editor()!=nullptr)<<" calls="<<calculations<<" -> "<<kernel::body_calculation_count.load()<<" documents="<<committed.dump()<<" -> "<<window.execute_console_command("documents").data.dump()<<'\n';
            check(!editor()&&window.execute_console_command("documents").data==committed&&kernel::body_calculation_count.load()==calculations,"Unchanged Wrap OK recalculated or changed the document");
            window.show_tree_item_properties(row());flush();
            editor()->findChild<QLineEdit*>("featureName")->setText("Cancelled Wrap edit");flush();
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==committed&&kernel::body_calculation_count.load()==calculations,"Wrap Cancel changed the document or calculated geometry");
            view->fit_all();flush();const auto artwork_frame=view->grabFramebuffer();artwork_frame.save(QString::fromStdWString((directory/("wrap-result-"+std::to_string(output)+".png")).wstring()));
            check(std::ranges::any_of(view->mesh().images,[](const auto& image){return !image.surface_vertices.empty();}),"Body activation dropped the curved image from the View");
            if(output!=2) {
                int red=0,blue=0;for(int y=0;y<artwork_frame.height();++y)for(int x=0;x<artwork_frame.width();++x) {
                    const auto color=artwork_frame.pixelColor(x,y);
                    red+=color.red()>180&&color.green()<70&&color.blue()<70;
                    blue+=color.blue()>180&&color.green()<70&&color.red()<70;
                }
                check(red>30&&blue>30,"Wrapped artwork pixels are missing or occluded");
            }
            check(view->property("wrappedImageUploadCount").toULongLong()>0,"Wrap texture was not uploaded");
            const auto uploads=view->property("wrappedImageUploadCount");view->update();flush();view->grabFramebuffer();
            check(view->property("wrappedImageUploadCount")==uploads,"Repainting re-uploaded the Wrap image");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            cached.clear();const auto redone=document::PartDocument::load(file,&cached);
            check(redone.find_container(feature.id)->feature==saved.find_container(feature.id)->feature&&cached.back().mesh.images.size()==1,"Wrap Undo/Redo lost native state");
            const auto selection_state=window.execute_console_command("documents").data;
            const auto selection_calls=kernel::body_calculation_count.load();
            tree->expandAll();tree->scrollToItem(row());flush();
            click(tree->viewport(),tree->visualItemRect(row()).center());
            view->grabFramebuffer();
            check(row()->isSelected()&&view->property("gpuSourceWireVertexCount").toInt()>0,"Wrap Tree selection omitted the authored preview wire");
            check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==source.id||d.reference.owner_id==feature.id;}),"Wrap single click exposed parameter dimensions");
            view->fit_all();flush();std::optional<QPointF> selected_hit;
            for(int y=20;y<view->height()-20&&!selected_hit;y+=3)for(int x=20;x<view->width()-20&&!selected_hit;x+=3) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Container&&candidates.front().owner_id==feature.id)selected_hit=QPointF(x,y);
            }
            check(selected_hit.has_value(),"Wrap result has no selectable feature in the View");
            view->clear_selection();
            QMouseEvent hover(QEvent::MouseMove,*selected_hit,QPointF(view->mapToGlobal(selected_hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&hover);flush();
            check(view->hovered_candidate()&&view->hovered_candidate()->owner_id==feature.id,"Wrapped geometry cannot be hovered");
            click(view,*selected_hit);check(row()->isSelected(),"Wrap View selection did not synchronize the Tree");
            QMouseEvent inspect(QEvent::MouseButtonDblClick,*selected_hit,QPointF(view->mapToGlobal(selected_hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&inspect);flush();
            check(!editor()&&window.execute_console_command("documents").data==selection_state&&kernel::body_calculation_count.load()==selection_calls,"Wrap selection or inspection calculated geometry or changed the document");
            const auto placement_state=window.execute_console_command("documents").data;
            const auto placement_calls=kernel::body_calculation_count.load();
            for(bool accept:{false,true}) {
                window.show_tree_item_properties(row());flush();check(editor(),"Wrap placement editor did not reopen");
                auto* origin=editor()->findChild<QPushButton*>("containerDefaultOriginButton");
                check(origin&&origin->isEnabled(),"Wrap is missing shared whole-Origin placement");origin->click();flush();
                check(document::placement_references_use_whole_origin(editor()->pending_value().placement.references),"Wrap did not adopt whole-Origin references");
                check(kernel::body_calculation_count.load()==placement_calls,"Wrap whole-Origin preview calculated geometry");
                editor()->buttons()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
                check(!editor(),"Wrap whole-Origin confirmation failed");
                if(!accept)check(window.execute_console_command("documents").data==placement_state,"Wrap placement Cancel changed history");
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();cached.clear();
            const auto placed=document::PartDocument::load(file,&cached);
            check(document::placement_references_use_whole_origin(placed.find_container(feature.id)->placement.references)&&cached.back().calculation_errors.empty(),"Wrap whole-Origin placement did not persist");
            window.show_tree_item_properties(row());flush();
            check(document::placement_references_use_whole_origin(editor()->pending_value().placement.references),"Wrap edit lost whole-Origin placement");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            // Exercise explicit source regeneration independently of GUI caches.
            auto regenerated=saved;kernel::OcctKernel cold;
            auto fresh=workspace::calculate_part_with_resolved_references(cold,regenerated);
            check(!fresh.empty()&&fresh.back().calculation_errors.empty(),"Cold Wrap regeneration failed");
            const auto volume=fresh.back().volume;
            const auto identities=[&](const kernel::BodyResult& result) {
                std::set<std::string> keys;
                for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id)keys.insert(edge.reference.semantic_key);
                return keys;
            };
            const auto stable_edges=identities(fresh.back());
            auto fingerprint=fresh.back().source_fingerprint;double previous_scale=1;
            for(double scale:{.8,1.2,1.}) {
                auto& sketch=*std::ranges::find(regenerated.sketches,source.id,&sketcher::Sketch::id);
                for(auto& point:sketch.points)point.x*=scale/previous_scale;
                previous_scale=scale;
                fresh=workspace::calculate_part_with_resolved_references(cold,regenerated);
                check(fresh.back().calculation_errors.empty()&&fresh.back().source_fingerprint!=fingerprint,"Edited Wrap source reused stale geometry");
                check(identities(fresh.back())==stable_edges,"Edited Wrap source changed curve ancestry");
                fingerprint=fresh.back().source_fingerprint;
            }
            check(std::abs(fresh.back().volume-volume)<1e-6,"Returning the Wrap source to its initial size changed material");
            document::FamilyTable family;family.columns={"Wrap"};family.bindings["Wrap"]={"feature",feature.id,{}};
            for(const auto* presence:{"no","yes"}) {
                document::FamilyInstance variant;variant.name="Wrapped variant";variant.values["Wrap"]=presence;
                workspace::apply_family_variant(regenerated,family,variant);
                fresh=workspace::calculate_part_with_resolved_references(cold,regenerated);
                const bool enabled=std::string_view(presence)=="yes";
                check(fresh.back().calculation_errors.empty()&&(!fresh.back().mesh.images.empty())==enabled,"Wrap Family suppression left stale artwork or failed");
                if(enabled)check(std::abs(fresh.back().volume-volume)<1e-6,"Wrap Family reactivation changed material");
            }
            auto broken=regenerated;const auto retained_frame=broken.find_container(feature.id)->placement;
            broken.find_container(feature.id)->feature.wrapping.target->reference.semantic_key="missing-wrap-target";
            bool rejected=false;
            try {const auto invalid=workspace::calculate_part_with_resolved_references(cold,broken);rejected=!invalid.back().calculation_errors.empty();}
            catch(const std::exception&){rejected=true;}
            check(rejected&&broken.find_container(feature.id)->placement==retained_frame,"Lost Wrap target calculated material or changed the last frame");
            broken.find_container(feature.id)->feature.wrapping.target=regenerated.find_container(feature.id)->feature.wrapping.target;
            const auto repaired=workspace::calculate_part_with_resolved_references(cold,broken);
            check(repaired.back().calculation_errors.empty()&&std::abs(repaired.back().volume-volume)<1e-6,"Replacing a lost Wrap target did not repair it");
            if(output==2)verify_relief_depth_edit(application,window,view,row,editor,feature.id,file);
            if(output==2)verify_relief_failure_confirmation(application,window,row,editor,feature.id,"wrappingTarget",file);
            std::cout<<"Wrap GUI output "<<output<<": target picking, clear, inspection, no-OCCT preview, persistence, unchanged OK, Cancel, embedded image and Undo/Redo passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"Wrap GUI: "<<error.what()<<'\n';return 1;}
}
